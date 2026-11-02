#!/usr/bin/env python3
"""Run the private consolidated compute probes under the original inherited device lock."""
import argparse
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import time

TOP = Path(__file__).resolve().parents[2]
WORK = Path(os.environ.get('RADISH_WORK', '/run/media/jethac/Shared/radish')).expanduser().resolve()
LOCK = Path(os.environ.get('RADISH_DEVICE_LOCK', str(TOP / '.device.lock'))).expanduser().resolve()
LOADER_SHA = '75699a38b1f26c1e8bfa2062e6593177bb07d4e3e346486d117fab8a0de0104a'
THERMAL = 'for z in /sys/class/thermal/thermal_zone*; do for n in type temp; do if test -r "$z/$n"; then printf "%s=" "$z/$n"; cat "$z/$n"; fi; done; done'
ENV = dict(RADISH_TILED_GROUPS_X='1', RADISH_BATCH_CHUNKS='1', RADISH_PROFILE='1', RADISH_GPU_ELAPSED='0', IR3_SHADER_DEBUG='disasm', RADISH_RESOURCE_DIAGNOSTICS='1')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(path):
    data = path.read_bytes()
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def write_json(path, value):
    with path.open('x') as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write('\n')


def verify_script(assets, remote):
    lines = ['set -eu', 'test -d ' + shlex.quote(remote) + ' && test ! -L ' + shlex.quote(remote)]
    for name, pin in assets.items():
        path = remote + '/' + name
        quoted = shlex.quote(path)
        lines += ['test -f ' + quoted + ' && test ! -L ' + quoted,
                  'test "$(stat -c %s ' + quoted + ')" = ' + str(pin['bytes']),
                  "printf '%s\\n' " + shlex.quote(pin['sha256'] + '  ' + path) + ' | sha256sum -c -']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('compile',))
    parser.add_argument('label', help='New safe label; use the same label for compile and probe.')
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--driver-sha256', required=True)
    parser.add_argument('--probe-dir', type=Path, required=True)
    parser.add_argument('--lock-fd', type=int, default=9)
    parser.add_argument('--serial', default=os.environ.get('RADISH_SERIAL', 'feb0bd1f028e4cb0'))
    parser.add_argument('--reuse-driver', help='Exact verified completed private driver path to hardlink into a fresh payload.')
    parser.add_argument('--check-only', action='store_true', help='Check host pins only; no lock or adb.')
    args = parser.parse_args()
    require(re.fullmatch(r'[A-Za-z0-9_-]{1,48}', args.label) is not None, 'invalid run label')
    if args.reuse_driver:
        require(re.fullmatch(r'/var/tmp/radish/[A-Za-z0-9_-]+/libvulkan_radish\.so', args.reuse_driver), 'invalid private reuse path')
    remote = '/var/tmp/radish/core-compute-' + args.label
    evidence_root = WORK / ('core-compute-' + args.label)
    sources = {}
    paths = [('libvulkan_radish.so', args.driver, args.driver_sha256)]
    for name in ('resources',):
        for suffix in ('', '.spv'):
            path = (args.probe_dir / (name + suffix)).resolve(strict=True)
            paths.append((name + suffix, path, identity(path)['sha256']))
    for name, path, digest in paths:
        require(re.fullmatch('[0-9a-f]{64}', digest) is not None, 'invalid SHA256 for ' + name)
        path = path.resolve(strict=True)
        require(path.is_file(), 'not a regular file: ' + str(path))
        pin = identity(path)
        require(pin['sha256'] == digest, 'host asset mismatch: ' + str(path))
        sources[name] = dict(path=str(path), **pin)
    checker = TOP / 'radish/scripts/check-gpu-log.py'
    checker_pin = identity(checker)
    sources['check-gpu-log.py'] = dict(path=str(checker), **checker_pin)
    if args.check_only:
        print(json.dumps(dict(mode=args.mode, label=args.label, remote=remote, evidence=str(evidence_root / args.mode), sources=sources, device_actions=0), indent=2))
        return 0

    lock_stat, expected = os.fstat(args.lock_fd), LOCK.stat()
    require((lock_stat.st_dev, lock_stat.st_ino) == (expected.st_dev, expected.st_ino), 'fd is not the original device lock')
    fcntl.flock(args.lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    evidence_root.mkdir(exist_ok=True)
    require(evidence_root.is_dir() and not evidence_root.is_symlink(), 'invalid evidence directory')
    evidence = evidence_root / args.mode
    evidence.mkdir(exist_ok=False)
    frozen = evidence / 'payload'
    frozen.mkdir()
    for name, entry in sources.items():
        shutil.copyfile(entry['path'], frozen / name)
        require(identity(frozen / name) == {k: entry[k] for k in ('bytes', 'sha256')}, 'snapshot differs')
    (frozen / 'radish_icd.json').write_text(json.dumps({'file_format_version': '1.0.0', 'ICD': {
        'library_path': remote + '/libvulkan_radish.so', 'api_version': '1.1.0'}}, separators=(',', ':')) + '\n')
    assets = {path.name: identity(path) for path in frozen.iterdir()}
    with (frozen / 'payload.json').open('x') as stream:
        json.dump(dict(format='radish-core-compute-payload-v1', remote=remote, assets=assets),
                  stream, indent=2, allow_nan=False, sort_keys=True)
        stream.write('\n')
    assets['payload.json'] = identity(frozen / 'payload.json')
    verify = verify_script(assets, remote)
    (evidence / 'verify-payload.sh').write_text(verify)
    shutil.copyfile(checker, evidence / 'check-gpu-log.py')
    write_json(evidence / 'host-before.json', dict(sources=sources, checker=checker_pin, payload=assets))
    if args.mode == 'probe':
        receipt_path = evidence_root / 'compile/runner-result.json'
        receipt = json.loads(receipt_path.read_text())
        require(receipt['status'] == 'passed' and receipt['mode'] == 'compile' and receipt['payload'] == assets,
                'functional packedformat scope requires the same successful compile payload')
        require(identity(evidence_root / 'compile/run.log') == receipt['run_log'], 'compile log changed')
    unit = 'radish-core-compute-' + args.mode + '-' + str(time.time_ns())
    adb = ['adb', '-s', args.serial]
    started = False
    terminal = False
    have_before = False
    collected = False
    result_code = 1

    def call(arguments, name, check=True, timeout=90, input_text=None):
        command = adb + arguments
        with (evidence / name).open('x') as output:
            result = subprocess.run(command, input=input_text, text=True, stdout=output,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        if check and result.returncode:
            raise RuntimeError('adb command failed: ' + shlex.join(command))
        return result.returncode

    def services(name):
        call(['shell', 'systemctl is-active straylight-listen straylight-voice'], name)
        require((evidence / name).read_text().replace('\r', '').splitlines() == ['active', 'active'],
                'both services must remain active')

    def drain():
        attempt = 0
        while True:
            name = 'drain-%03d.txt' % attempt
            command = 'systemctl stop ' + shlex.quote(unit) + ' >/dev/null 2>&1 || :; systemctl show -p LoadState -p ActiveState -p SubState ' + shlex.quote(unit)
            try:
                call(['shell', command], name, check=False, timeout=40)
                state = dict(re.findall(r'^(LoadState|ActiveState|SubState)=(.*)$',
                             (evidence / name).read_text().replace('\r', ''), re.M))
                if state.get('LoadState') == 'not-found' or (state.get('ActiveState') in ('inactive', 'failed') and state.get('SubState') in ('dead', 'failed', 'exited')):
                    return
            except Exception as error:
                print('Own unit drain not yet verified: ' + str(error), file=sys.stderr, flush=True)
            print('Retaining shared lock until own unit is terminal: ' + unit, file=sys.stderr, flush=True)
            attempt += 1
            time.sleep(5)

    def collect():
        nonlocal collected
        errors = []
        actions = [lambda: call(['shell', 'dmesg'], 'dmesg-after.txt'),
                   lambda: call(['shell', 'cat /proc/meminfo'], 'mem-after.txt'),
                   lambda: call(['shell', 'df -Pk /var/tmp/radish'], 'disk-after.txt'),
                   lambda: call(['shell', 'cat /sys/class/devfreq/1c00000.gpu/trans_stat'], 'devfreq-after.txt'),
                   lambda: call(['shell', THERMAL], 'thermal-after.txt'),
                   lambda: call(['shell', 'journalctl --no-pager -u ' + shlex.quote(unit)], 'service.txt'),
                   lambda: services('services-after.txt')]
        for action in actions:
            try:
                action()
            except Exception as error:
                errors.append(str(error))
        try:
            with (evidence / 'fault-check.txt').open('x') as log:
                result = subprocess.run([sys.executable, str(evidence / 'check-gpu-log.py'),
                    str(evidence / 'dmesg-before.txt'), str(evidence / 'dmesg-after.txt')],
                    stdout=log, stderr=subprocess.STDOUT, timeout=30)
            require(result.returncode == 0, 'new GPU fault evidence')
        except Exception as error:
            errors.append(str(error))
        collected = True
        write_json(evidence / 'collection.json', dict(passed=not errors, errors=errors))
        require(not errors, '; '.join(errors))

    def interrupted(signum, frame):
        raise InterruptedError('signal ' + str(signum))

    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        services('services-before.txt')
        call(['shell', 'dmesg'], 'dmesg-before.txt')
        have_before = True
        call(['shell', 'cat /proc/meminfo'], 'mem-before.txt')
        call(['shell', 'df -Pk /var/tmp/radish'], 'disk-before.txt')
        call(['shell', 'cat /sys/class/devfreq/1c00000.gpu/trans_stat'], 'devfreq-before.txt')
        call(['shell', THERMAL], 'thermal-before.txt')
        available = re.search(r'^MemAvailable:\s+(\d+)\s+kB$', (evidence / 'mem-before.txt').read_text().replace('\r', ''), re.M)
        require(available is not None and int(available[1]) >= (384 + 192) * 1024,
                'need 384MiB cap plus 192MiB available memory reserve')
        exists = call(['shell', 'test -e ' + shlex.quote(remote) + ' || test -L ' + shlex.quote(remote)],
                      'payload-exists.txt', check=False)
        require(exists in (0, 1), 'could not check payload existence')
        reused_driver = bool(exists and args.reuse_driver)
        if reused_driver:
            reuse_dir = str(Path(args.reuse_driver).parent)
            call(['shell', 'sh -s'], 'reuse-driver-before.txt', input_text=verify_script({'libvulkan_radish.so': assets['libvulkan_radish.so']}, reuse_dir))
        payload_bytes = sum(item['bytes'] for item in assets.values()) if exists else 0
        if reused_driver:
            payload_bytes -= assets['libvulkan_radish.so']['bytes']
        required_kib = (payload_bytes + 8 * 1024 * 1024 + 64 * 1024 * 1024 + 1023) // 1024
        disk = (evidence / 'disk-before.txt').read_text().splitlines()
        require(len(disk) == 2 and int(disk[1].split()[3]) >= required_kib,
                'insufficient disk for payload, raw outputs and 64MiB reserve')
        write_json(evidence / 'preflight.json', dict(required_disk_kib=required_kib,
                   payload_bytes_to_upload=payload_bytes, existing_payload=exists == 0, reused_driver=args.reuse_driver if reused_driver else None))
        if exists:
            call(['shell', 'set -eu; test -d /var/tmp/radish; test ! -L /var/tmp/radish; mkdir ' + shlex.quote(remote)], 'mkdir.txt')
            for name in assets:
                if name == 'libvulkan_radish.so' and reused_driver:
                    command = 'set -eu; ln ' + shlex.quote(args.reuse_driver) + ' ' + shlex.quote(remote + '/' + name) + '; test ' + shlex.quote(args.reuse_driver) + ' -ef ' + shlex.quote(remote + '/' + name)
                    call(['shell', command], 'reuse-driver-link.txt')
                else:
                    call(['push', str(frozen / name), remote + '/' + name], 'upload-' + name + '.txt')
        call(['shell', 'sh -s'], 'assets-before.txt', input_text=verify)
        environment = dict(ENV, LD_LIBRARY_PATH=remote, VK_DRIVER_FILES=remote + '/radish_icd.json')
        if args.mode == 'compile':
            environment['IR3_SHADER_DEBUG'] = 'disasm'
        command = ['systemd-run', '--unit=' + unit, '--wait', '--pipe', '--collect',
                   '-p', 'MemoryMax=384M', '-p', 'MemorySwapMax=128M', '-p', 'RuntimeMaxSec=90',
                   '-p', 'TimeoutStopSec=15', '-p', 'LimitCORE=0']
        command += ['--setenv=' + key + '=' + value for key, value in environment.items()]
        commands = ['set -eu', 'command -v awk', 'dmesg > ' + shlex.quote(remote + '/batch-dmesg-before.txt')]
        fault_awk = 'function stamp(line, s) { s=line; sub(/^\\[ */, "", s); sub(/\\].*$/, "", s); return s+0 }\nNR==FNR { if ($0 ~ /^\\[ *[0-9]+\\.[0-9]+\\]/) { n++; t=stamp($0); if(t>cutoff)cutoff=t; old[$0]=1 } next }\n$0 ~ /^\\[ *[0-9]+\\.[0-9]+\\]/ { m++; t=stamp($0); if(t>last)last=t; line=tolower($0); if(t>=cutoff && !( $0 in old ) && (line ~ /hangcheck detected|gpu.*fault|gpu.*recover/)) { print "FAIL: new GPU fault/recovery: " $0; failed=1 } }\nEND { if(!n || !m || last<cutoff) { print "FAIL: missing/reset kernel timestamps"; failed=1 } if(!failed)print "PASS: no new GPU fault/recovery messages"; exit failed }\n'
        check_after = 'dmesg > ' + shlex.quote(remote + '/batch-dmesg-after.txt') + '; awk ' + shlex.join([fault_awk, remote + '/batch-dmesg-before.txt', remote + '/batch-dmesg-after.txt'])
        argv = [remote + '/resources', remote + '/libvulkan_radish.so',
                remote + '/resources.spv']
        if args.mode == 'compile':
            argv.append('--compile-only')
        commands += ['echo CONTROL_FLOW_'  + args.mode.upper() + '_BEGIN', shlex.join(argv), check_after]
        command += ['/bin/sh', '-c', '\n'.join(commands)]
        shell = 'chmod 755 ' + ' '.join(shlex.quote(remote + '/' + name) for name in ('resources',)) + ' && ' + shlex.join(command)
        (evidence / 'run-command.txt').write_text(shell + '\n')
        write_json(evidence / 'launch.json', dict(mode=args.mode, unit=unit, serial=args.serial,
                   remote=remote, environment=environment, payload=assets))
        started = True
        result_code = call(['shell', shell], 'run.log', check=False, timeout=120)
        require(result_code == 0, 'probe/compile process returned ' + str(result_code))
        terminal = True
        call(['shell', 'sh -s'], 'assets-after.txt', input_text=verify)
        for name, entry in sources.items():
            expected_pin = {key: entry[key] for key in ('bytes', 'sha256')}
            require(identity(Path(entry['path'])) == identity(frozen / name) == expected_pin, 'host asset changed: ' + name)
        require(identity(checker) == checker_pin, 'fault checker changed')
        collect()
        log = (evidence / 'run.log').read_text()
        require(not re.search(r'vkCreateComputePipelines failed|new_pipeline failed|\bFAIL\b', log), 'failure in process log')
        require(re.search(r'^CONTROL_FLOW_COMPILED active=2 buffers=2 images=0 native=\d+ full=\d+ half=\d+ constlen=\d+ private_per_fiber=\d+ private_per_sp=\d+ private_bo_bytes=\d+ shader_bo_bytes=\d+ shared=0 local=1,1,1 robust_buffer_access=0 before_data_allocations=1 no_spill_inference=1$',log,re.M),'missing exact control-flow interface/resource report')
        require('PASS CONTROL_FLOW compile-only: source-exact original control-flow module/core pipeline; no data, commands or dispatch; native admission separate' in log,'missing compile-only result')
        require('RADISH_PROFILE dispatch' not in log and 'RADISH_PROFILE image_transfer' not in log,'unexpected compile workload')
        write_json(evidence / 'runner-result.json', dict(status='passed', mode=args.mode, exit_code=0,
                   payload=assets, run_log=identity(evidence / 'run.log'),
                   scope='Raw ICD assets/services/faults and consolidated assertions passed. This is experimental compute coverage, not Vulkan conformance.'))
        print('PASS ' + args.mode + '; evidence ' + str(evidence), flush=True)
        return 0
    except BaseException as error:
        if started and not terminal:
            signal.signal(signal.SIGTERM, signal.SIG_IGN)
            signal.signal(signal.SIGINT, signal.SIG_IGN)
            drain()
        if have_before and not collected:
            try:
                collect()
            except Exception as collection_error:
                print('Post-failure collection: ' + str(collection_error), file=sys.stderr)
        write_json(evidence / 'failure.json', dict(status='failed', error=str(error), started=started,
                   terminal=terminal, process_returncode=result_code, remote=remote, unit=unit))
        print('FAIL: ' + str(error) + '; evidence ' + str(evidence), file=sys.stderr)
        return result_code if 0 < result_code < 126 else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit('FAIL: ' + str(error))

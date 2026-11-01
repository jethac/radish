#!/usr/bin/env python3
"""Run the fixed GPU-elapsed probe under an inherited device-lock descriptor."""
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
ENV = dict(RADISH_NCNN_TILED='1', RADISH_NCNN_TILED32='0', RADISH_NCNN_PLANAR='1',
           RADISH_NCNN_PAD_CHANNELS='0', RADISH_NCNN_SHARED32='0', RADISH_NCNN_PIPELINE_CACHE='1',
           RADISH_TILED_GROUPS_X='1', RADISH_BATCH_CHUNKS='8', RADISH_TILED_BATCH_CHUNKS='8',
           RADISH_SHARED_GROUPS='8', RADISH_PROFILE='1', RADISH_GPU_ELAPSED='0', RADISH_TILED_STATE_REUSE='0')


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
    parser.add_argument('mode', choices=('compile', 'probe'))
    parser.add_argument('label', help='New safe label; use the same label for compile and probe.')
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--driver-sha256', required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--probe-sha256', required=True)
    parser.add_argument('--loader', type=Path, default=WORK / 'build-loader/loader/libvulkan.so')
    parser.add_argument('--lock-fd', type=int, default=9)
    parser.add_argument('--serial', default=os.environ.get('RADISH_SERIAL', 'feb0bd1f028e4cb0'))
    parser.add_argument('--check-only', action='store_true', help='Check host pins only; no lock or adb.')
    args = parser.parse_args()
    require(re.fullmatch(r'[A-Za-z0-9_-]{1,48}', args.label) is not None, 'invalid run label')
    remote = '/var/tmp/radish/state-reuse-' + args.label
    evidence_root = WORK / ('state-reuse-' + args.label)
    sources = {}
    for name, path, digest in [('libvulkan_radish.so', args.driver, args.driver_sha256),
                               ('state-reuse-probe', args.probe, args.probe_sha256),
                               ('libvulkan.so.1', args.loader, LOADER_SHA)]:
        require(re.fullmatch('[0-9a-f]{64}', digest) is not None, 'invalid SHA256 for ' + name)
        path = path.resolve(strict=True)
        require(path.is_file(), 'not a regular file: ' + str(path))
        pin = identity(path)
        require(pin['sha256'] == digest, 'host asset mismatch: ' + str(path))
        sources[name] = dict(path=str(path), **pin)
    checker = TOP / 'radish/scripts/check-gpu-log.py'
    checker_pin = identity(checker)
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
        'library_path': remote + '/libvulkan_radish.so', 'api_version': '1.0.0'}}, separators=(',', ':')) + '\n')
    assets = {path.name: identity(path) for path in frozen.iterdir()}
    write_json(frozen / 'payload.json', dict(format='radish-state-reuse-payload-v1', remote=remote, assets=assets))
    assets['payload.json'] = identity(frozen / 'payload.json')
    verify = verify_script(assets, remote)
    (evidence / 'verify-payload.sh').write_text(verify)
    shutil.copyfile(checker, evidence / 'check-gpu-log.py')
    write_json(evidence / 'host-before.json', dict(sources=sources, checker=checker_pin, payload=assets))
    if args.mode == 'probe':
        receipt_path = evidence_root / 'compile/runner-result.json'
        receipt = json.loads(receipt_path.read_text())
        require(receipt['status'] == 'passed' and receipt['mode'] == 'compile' and receipt['payload'] == assets,
                'probe requires successful compile process for exactly the same payload')
        require(identity(evidence_root / 'compile/run.log') == receipt['run_log'], 'compile log changed')
        write_json(evidence / 'compile-receipt.json', dict(receipt=identity(receipt_path),
                   scope='Compile process passed; resource admission is separately reviewed by root before invoking probe.'))

    unit = 'radish-state-reuse-' + args.mode + '-' + str(time.time_ns())
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
        require(args.mode == 'compile' or exists == 0, 'probe cannot stage a missing compile payload')
        payload_bytes = sum(item['bytes'] for item in assets.values()) if exists else 0
        required_kib = (payload_bytes + 14 * 109056 + 1024 * 1024 + 64 * 1024 * 1024 + 1023) // 1024
        disk = (evidence / 'disk-before.txt').read_text().splitlines()
        require(len(disk) == 2 and int(disk[1].split()[3]) >= required_kib,
                'insufficient disk for payload, raw outputs and 64MiB reserve')
        write_json(evidence / 'preflight.json', dict(required_disk_kib=required_kib,
                   payload_bytes_to_upload=payload_bytes, existing_payload=exists == 0))
        if exists:
            call(['shell', 'set -eu; test -d /var/tmp/radish; test ! -L /var/tmp/radish; mkdir ' + shlex.quote(remote)], 'mkdir.txt')
            for name in assets:
                call(['push', str(frozen / name), remote + '/' + name], 'upload-' + name + '.txt')
        call(['shell', 'sh -s'], 'assets-before.txt', input_text=verify)
        remote_output = remote + '/probe-output'
        call(['shell', 'test ! -e ' + shlex.quote(remote_output) + ' && test ! -L ' + shlex.quote(remote_output)], 'output-absent.txt')
        environment = dict(ENV, LD_LIBRARY_PATH=remote, VK_DRIVER_FILES=remote + '/radish_icd.json')
        if args.mode == 'compile':
            environment['IR3_SHADER_DEBUG'] = 'disasm'
        command = ['systemd-run', '--unit=' + unit, '--wait', '--pipe', '--collect',
                   '-p', 'MemoryMax=384M', '-p', 'MemorySwapMax=128M', '-p', 'RuntimeMaxSec=90',
                   '-p', 'TimeoutStopSec=15', '-p', 'LimitCORE=0']
        command += ['--setenv=' + key + '=' + value for key, value in environment.items()]
        command += [remote + '/state-reuse-probe', '--compile-only' if args.mode == 'compile' else remote_output]
        shell = 'chmod 755 ' + shlex.quote(remote + '/state-reuse-probe') + ' && ' + shlex.join(command)
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
        raw_pins = {}
        if args.mode == 'compile':
            require('PASS: GPU state reuse probe compile-only;' in log, 'missing compile PASS')
            require('GPU_STATE_REUSE_RUN_BEGIN' not in log and 'RADISH_GPU_ELAPSED ' not in log, 'unexpected operator/timestamp work during compile-only')
        else:
            require('PASS: GPU state reuse balanced probe warmup_runs=4 measured_runs=8 outputs_per_run=27264 all_outputs=327168;' in log, 'missing full probe PASS')
            names = ['run-%02d.f32' % index for index in range(12)] + ['input-before.f32', 'input-after.f32', 'result.json']
            checks = ['set -eu', 'test -d ' + shlex.quote(remote_output), 'test ! -L ' + shlex.quote(remote_output),
                      'test ! -e ' + shlex.quote(remote_output + '/pending.json')]
            for name in names:
                quoted = shlex.quote(remote_output + '/' + name)
                checks += ['test -f ' + quoted + ' && test ! -L ' + quoted, 'sha256sum ' + quoted]
            call(['shell', 'sh -s'], 'outputs-before.sha256', input_text='\n'.join(checks) + '\n')
            raw = evidence / 'raw'
            call(['pull', remote_output, str(raw)], 'output-pull.txt', timeout=90)
            require(set(path.name for path in raw.iterdir()) == set(names), 'unexpected output inventory')
            hashes = {}
            for line in (evidence / 'outputs-before.sha256').read_text().splitlines():
                digest, path = line.split(None, 1)
                require(re.fullmatch('[0-9a-f]{64}', digest) is not None and path.startswith(remote_output + '/'), 'invalid remote hash')
                name = path[len(remote_output) + 1:]
                require(name in names and name not in hashes, 'invalid or duplicate remote output')
                hashes[name] = digest
            for name in names:
                require((raw / name).is_file() and not (raw / name).is_symlink(), 'non-regular captured output')
                pin = identity(raw / name)
                require(pin['sha256'] == hashes[name], 'pulled output hash mismatch')
                if name.endswith('.f32'):
                    data = (raw / name).read_bytes()
                    require(len(data) == 109056 and all(math.isfinite(v[0]) for v in struct.iter_unpack('<f', data)), 'bad raw output')
                raw_pins[name] = pin
            result = json.loads((raw / 'result.json').read_text())
            require(result['status'] == 'passed' and result['format'] == 'radish-state-reuse-probe-v1'
                    and result['total_validated_values'] == 327168 and result['all_outputs_bit_identical'] is True
                    and result['input_preserved'] is True and result['per_run_output_poison'] is True
                    and result['expected_timestamp_samples'] == 12 and result['expected_reuse_samples'] == 6, 'helper result did not pass')
            require(all((raw / ('run-%02d.f32' % i)).read_bytes() == (raw / 'run-00.f32').read_bytes() for i in range(12)), 'output byte identity failed')
            require((raw / 'input-before.f32').read_bytes() == (raw / 'input-after.f32').read_bytes(), 'input bytes changed')
            call(['shell', 'sh -s'], 'outputs-after.sha256', input_text='\n'.join(checks) + '\n')
            require((evidence / 'outputs-before.sha256').read_bytes() == (evidence / 'outputs-after.sha256').read_bytes(), 'remote output changed during pull')
        write_json(evidence / 'runner-result.json', dict(status='passed', mode=args.mode, exit_code=0,
                   payload=assets, raw_files=raw_pins, run_log=identity(evidence / 'run.log'),
                   scope='Execution/assets/services/faults/raw captures passed. Compiler admission and diagnostic timing interpretation require separate root/reviewer audit.'))
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
        if started and args.mode == 'probe' and not (evidence / 'raw').exists():
            try:
                call(['pull', remote + '/probe-output', str(evidence / 'partial-raw')], 'partial-pull.txt', check=False, timeout=60)
            except Exception:
                pass
        write_json(evidence / 'failure.json', dict(status='failed', error=str(error), started=started,
                   terminal=terminal, process_returncode=result_code, remote=remote, unit=unit))
        print('FAIL: ' + str(error) + '; evidence ' + str(evidence), file=sys.stderr)
        return result_code if 0 < result_code < 126 else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit('FAIL: ' + str(error))

#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess


def identity(path):
    data = path.read_bytes()
    return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}


def main():
    parser = argparse.ArgumentParser(description='Build the internal-dispatch API5 probe against an existing exact Radish Mesa build.')
    parser.add_argument('mesa_build', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--gcc-install-dir', type=Path, default=os.environ.get('RADISH_GCC_INSTALL'))
    args = parser.parse_args()
    top = Path(__file__).resolve().parents[2]
    build = args.mesa_build.resolve(strict=True)
    output = args.output.resolve()
    entries = json.loads((build / 'compile_commands.json').read_text())
    entry = next(item for item in entries if item['file'].endswith('/rd_command.c'))
    command = shlex.split(entry['command'])
    command = command[:command.index('-MD')]
    sysroot = next(Path(arg.split('=', 1)[1]) for arg in command if arg.startswith('--sysroot='))
    gcc = args.gcc_install_dir or sysroot / 'usr/lib/gcc/aarch64-linux-gnu/16'
    gcc = gcc.resolve(strict=True)
    output.mkdir(parents=True, exist_ok=False)
    source = top / 'radish/tests/core_api5.c'
    shader = top / 'radish/tests/shaders/core-api5.comp'
    command += ['-fuse-ld=lld', '--gcc-install-dir=' + str(gcc),
                '-I' + str(top / 'radish/tests'), str(source), '-o', str(output / 'api5-probe'), '-ldl']
    commands = [command, ['glslangValidator', '-V', '--target-env', 'vulkan1.0',
                         str(shader), '-o', str(output / 'probe.spv')]]
    (output / 'commands.json').write_text(json.dumps({'cwd': entry['directory'], 'commands': commands}, indent=2) + '\n')
    for index, argv in enumerate(commands):
        with (output / ('build-%d.log' % index)).open('x') as log:
            subprocess.run(argv, cwd=entry['directory'], stdout=log, stderr=subprocess.STDOUT, check=True)
    paths = [source, shader, output / 'api5-probe', output / 'probe.spv']
    (output / 'receipt.json').write_text(json.dumps({'files': {str(path): identity(path) for path in paths},
        'scope': 'Host build only; no device execution. Internal structure ABI requires this matching driver build.'}, indent=2) + '\n')
    print('PASS host build: ' + str(output))


if __name__ == '__main__':
    main()

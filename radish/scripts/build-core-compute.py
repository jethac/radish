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
    parser = argparse.ArgumentParser(description='Build the consolidated compute probes against the matching isolated Radish Mesa build.')
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
    units = [('api5', 'core_api5.c', 'core-api5.comp', 'vulkan1.0'),
             ('event-query', 'core_events_queries.c', 'core-events-queries.comp', 'vulkan1.0'),
             ('resources', 'core_resources.c', 'core-resources.comp', 'vulkan1.0'),
             ('subgroup', 'core_subgroup_basic.c', 'core-subgroup-basic.comp', 'vulkan1.1')]
    records = {}
    for name, cfile, glsl, target in units:
        source = top / 'radish/tests' / cfile
        shader = top / 'radish/tests/shaders' / glsl
        argv = command + ['-fuse-ld=lld', '--gcc-install-dir=' + str(gcc),
                  '-I' + str(top / 'radish/tests'), str(source), '-o', str(output / name), '-ldl', '-pthread']
        commands = [argv, ['glslangValidator', '-V', '--target-env', target, str(shader), '-o', str(output / (name + '.spv'))]]
        for index, argv in enumerate(commands):
            with (output / (name + '-%d.log' % index)).open('x') as log:
                subprocess.run(argv, cwd=entry['directory'], stdout=log, stderr=subprocess.STDOUT, check=True)
        records[name] = {'commands': commands, 'files': {str(path): identity(path) for path in [source, shader, output / name, output / (name + '.spv')]}}
    (output / 'receipt.json').write_text(json.dumps({'units': records, 'scope': 'Host build only; probes require the matching internal driver ABI.'}, indent=2) + '\n')
    print('PASS host build: ' + str(output))


if __name__ == '__main__':
    main()

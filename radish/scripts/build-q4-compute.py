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
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description='Build the existing Q4 consumer and real profile-patched backend modules against matching Radish headers.')
    parser.add_argument('mesa_build', type=Path)
    parser.add_argument('shader_directory', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--gcc-install-dir', type=Path, default=os.environ.get('RADISH_GCC_INSTALL'))
    args = parser.parse_args()
    top = Path(__file__).resolve().parents[2]
    build = args.mesa_build.resolve(strict=True)
    shaders = args.shader_directory.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    entries = json.loads((build / 'compile_commands.json').read_text())
    entry = next(item for item in entries if item['file'].endswith('/rd_command.c'))
    base = shlex.split(entry['command'])
    base = base[:base.index('-MD')]
    sysroot = next(Path(arg.split('=', 1)[1]) for arg in base if arg.startswith('--sysroot='))
    gcc = (args.gcc_install_dir or sysroot / 'usr/lib/gcc/aarch64-linux-gnu/16').resolve(strict=True)
    source = top / 'radish/tests/core_q4.c'
    commands = []
    def run(argv, label):
        commands.append(argv)
        with (output / (label + '.log')).open('x') as log:
            subprocess.run(argv, cwd=entry['directory'], stdout=log, stderr=subprocess.STDOUT, check=True)
    run(base + ['-fuse-ld=lld', '--gcc-install-dir=' + str(gcc), '-I' + str(top / 'radish/tests'), str(source), '-o', str(output / 'resources'), '-ldl', '-pthread'], 'helper')
    common = ['-DFLOAT_TYPE=float', '-DFLOAT_TYPEV2=vec2', '-DDATA_A_Q4_K=1', '-DGGML_VULKAN_KHR_PROFILE=1']
    modules = [('dequant_q4_k.comp', 'resources.spv', ['-DD_TYPE=float16_t']),
               ('mul_mat_vec_q4_k.comp', 'matvec.spv', ['-DB_TYPE=float', '-DB_TYPEV2=vec2', '-DB_TYPEV4=vec4', '-DD_TYPE=float'])]
    records = {}
    expected = {'Shader', 'Int8', 'StorageBuffer16BitAccess', 'StorageBuffer8BitAccess'}
    for root, name, extra in modules:
        module = output / name
        run(['glslc', '--target-env=vulkan1.0', '-O', *common, *extra, '-MD', '-MF', str(output / (name + '.d')), str(shaders / root), '-o', str(module)], name + '-compile')
        run(['spirv-val', '--target-env', 'vulkan1.0', str(module)], name + '-validate')
        assembly = output / (name + '.spvasm')
        run(['spirv-dis', str(module), '-o', str(assembly)], name + '-disassemble')
        capabilities = [line.split()[-1] for line in assembly.read_text().splitlines() if 'OpCapability' in line]
        if set(capabilities) != expected:
            raise RuntimeError('unexpected capabilities: ' + repr(capabilities))
        records[name] = dict(source=identity(shaders / root), module=identity(module), capabilities=capabilities)
    receipt = dict(status='host build only', commands=commands, cwd=entry['directory'], helper_source=identity(source), helper=identity(output / 'resources'), modules=records, scope='Requires matching private headers and driver. Device compile/native admission precedes the numerical scope.')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS Q4 host build: ' + str(output))


if __name__ == '__main__':
    main()

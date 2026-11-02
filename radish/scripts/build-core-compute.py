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
    parser.add_argument('--radish-headers', type=Path, help='Matching frozen Radish header directory when reproducing an older private driver ABI.')
    parser.add_argument('--only', nargs='+', help='Build only the named probe units.')
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
             ('subgroup', 'core_subgroup_basic.c', 'core-subgroup-basic.comp', 'vulkan1.1'),
             ('storage', 'core_shader_storage.c', 'core-shader-storage.comp', 'vulkan1.0'),
             ('robust', 'core_robust_buffers.c', 'core-robust-buffers.comp', 'vulkan1.0'),
             ('array', 'core_array_resources.c', 'core-array-resources.comp', 'vulkan1.0'),
             ('multisample', 'core_multisample.c', 'core-multisample.comp', 'vulkan1.0'),
             ('subword', 'core_subword_resources.c', 'core-subword-resources.comp', 'vulkan1.0'),
             ('capacity', 'core_native_capacity.c', 'core-native-capacity.comp', 'vulkan1.0'),
             ('barrier', 'core_workgroup_barriers.c', 'core-workgroup-barriers.comp', 'vulkan1.0'),
             ('oversized', 'core_oversized_image.c', 'core-oversized-image.comp', 'vulkan1.0'),
             ('storage-cube', 'core_storage_cube.c', 'core-storage-cube.comp', 'vulkan1.0'),
             ('ms-lanes', 'core_ms_lanes.c', 'core-ms-lanes.comp', 'vulkan1.0'),
             ('oversized-ms', 'core_oversized_ms.c', 'core-oversized-ms.comp', 'vulkan1.0'),
             ('storage16', 'core_storage16.c', 'core-storage16.comp', 'vulkan1.0'),
             ('storage8', 'core_storage8.c', 'core-storage8.comp', 'vulkan1.0'),
             ('timeline', 'core_timeline.c', 'core-timeline.comp', 'vulkan1.0'),
             ('int8', 'core_int8.c', 'core-int8.comp', 'vulkan1.0'),
             ('public11', 'core_public11_basic.c', 'core-public11-basic.comp', 'vulkan1.1'),
             ('combined', 'core_combined_resources.c', 'core-combined-resources.comp', 'vulkan1.0'),
             ('packed-storage', 'core_packed_storage.c', 'core-packed-storage.comp', 'vulkan1.0'),
             ('snorm-storage', 'core_snorm_storage.c', 'core-snorm-storage.comp', 'vulkan1.0'),
             ('mixed-push', 'core_mixed_push.c', 'core-mixed-push.comp', 'vulkan1.0'),
             ('attachment', 'core_attachment.c', 'core-attachment.comp', 'vulkan1.0'),
             ('descriptor-host', 'core_descriptor_host.c', None, None),
             ('core-lookup', 'core_lookup.c', None, None),
             ('native-attachment', 'core_native_attachment.c', None, None),
             ('blend-query', 'core_blend_query.c', None, None),
             ('required-atomics', 'core_required_atomics.c', 'core-required-atomics.comp', 'vulkan1.0'),
             ('mixed-depth', 'core_mixed_depth.c', None, None),
             ('combined-ds', 'core_combined_ds.c', 'core-combined-ds.comp', 'vulkan1.0'),
             ('etc2', 'core_etc2.c', 'core-etc2.comp', 'vulkan1.0'),
             ('etc2-observe', 'core_etc2_observe.c', 'core-etc2.comp', 'vulkan1.0'),
             ('etc2-unorm-taps', 'core_etc2_unorm_taps.c', 'core-etc2-unorm-taps.comp', 'vulkan1.0'),
             ('etc2-rgba8-srgb-taps', 'core_etc2_rgba8_srgb_taps.c', 'core-etc2-rgba8-srgb-taps.comp', 'vulkan1.0')]
    if args.only:
        unknown = set(args.only) - {unit[0] for unit in units}
        if unknown:
            parser.error('Unknown units: ' + ', '.join(sorted(unknown)))
        units = [unit for unit in units if unit[0] in args.only]
    records = {}
    for name, cfile, glsl, target in units:
        source = top / 'radish/tests' / cfile
        shader = top / 'radish/tests/shaders' / glsl if glsl else None
        prefix = command + ['-fuse-ld=lld', '--gcc-install-dir=' + str(gcc),
                            '-I' + str(top / 'radish/tests')]
        if args.radish_headers:
            headers = args.radish_headers.resolve(strict=True)
            for required in ('rd_private.h', 'rd_image.h', 'rd_sampling.h'):
                if not (headers / required).is_file():
                    parser.error('Missing matching private header: ' + str(headers / required))
            prefix.insert(1, '-I' + str(headers))
        inputs = [source]
        if name in ('etc2', 'etc2-observe', 'etc2-unorm-taps', 'etc2-rgba8-srgb-taps'):
            oracle = top / 'radish/tests/etc2-oracle'
            prefix += ['-I' + str(oracle)]
            inputs += [oracle / path for path in ['etc_oracle.c', 'vendor/decompress-etc.c',
                                                  'vendor/decompress-eac.c', 'vendor/clamp.c']]
            objects = [output / (name + '-%d.o' % i) for i in range(len(inputs))]
            commands = [prefix + (['-Wno-error=missing-prototypes'] if 'vendor' in path.parts else []) +
                        ['-c', str(path), '-o', str(obj)] for path, obj in zip(inputs, objects)]
            commands += [prefix + list(map(str, objects)) + ['-o', str(output / name), '-ldl', '-pthread', '-lm']]
        else:
            commands = [prefix + [str(source), '-o', str(output / name), '-ldl', '-pthread']]
        files = inputs + [output / name]
        if shader:
            commands += [['glslangValidator', '-V', '--target-env', target, str(shader), '-o', str(output / (name + '.spv'))],
                         ['spirv-val', '--target-env', target, str(output / (name + '.spv'))]]
            files += [shader, output / (name + '.spv')]
        for index, argv in enumerate(commands):
            with (output / (name + '-%d.log' % index)).open('x') as log:
                subprocess.run(argv, cwd=entry['directory'], stdout=log, stderr=subprocess.STDOUT, check=True)
        records[name] = {'commands': commands, 'files': {str(path): identity(path) for path in files}}
    (output / 'receipt.json').write_text(json.dumps({'units': records, 'scope': 'Host build only; probes require the matching internal driver ABI.'}, indent=2) + '\n')
    print('PASS host build: ' + str(output))


if __name__ == '__main__':
    main()

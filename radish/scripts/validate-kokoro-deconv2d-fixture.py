#!/usr/bin/env python3
"""Recheck full CPU evidence for pinned height-one deconvolution graph variants."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location('radish_affine_fixture', Path(__file__).with_name('validate-kokoro-affine-snake-fixture.py'))
affine = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(affine)
require, asset, checked_bytes = affine.require, affine.asset, affine.checked_bytes
comparison, packed_log = affine.comparison, affine.packed_log
WEIGHTS_SHA256, REFERENCE_SHA256 = affine.WEIGHTS_SHA256, affine.REFERENCE_SHA256
COUNT, ATOL, RTOL, INPUTS = affine.COUNT, affine.ATOL, affine.RTOL, affine.INPUTS
VARIANTS = {
    'snake-deconv2d': dict(param_sha256='5ba4b3fd791904b4c7560846072919027fd9d0c7d764f38028a79862e09e4980',
                          original_param_sha256=affine.ORIGINAL_PARAM_SHA256, layers=912, blobs=1073,
                          original_cpu_layers=901, cpu_layers=907, evidence_branch='deconv2d',
                          rewrite='prepare-kokoro-deconv2d.py'),
    'affine-snake-deconv2d': dict(param_sha256='e211a573578f5ad45000b487f6e36ac1376ae4bbdcfa6a6e32a95070ffbc807d',
                                 original_param_sha256=affine.PARAM_SHA256, layers=720, blobs=881,
                                 original_cpu_layers=709, cpu_layers=715, evidence_branch='composed',
                                 rewrite='prepare-kokoro-affine-deconv2d.py'),
    'norm-affine-snake-deconv2d': dict(param_sha256='ccab4ff803f05c6ca1f9dbc86b557e28d72488c94f0584f149108523b474aa43',
                                      original_param_sha256='e211a573578f5ad45000b487f6e36ac1376ae4bbdcfa6a6e32a95070ffbc807d',
                                      layers=594, blobs=755, original_cpu_layers=715, cpu_layers=589,
                                      evidence_branch='norm_affine', rewrite='fuse-kokoro-norm-affine-snake.py',
                                      require_byte_identity=True),
}


def validate(directory):
    directory = Path(directory).resolve()
    names = ('deconv2d-validation.json', 'decoder.ncnn.param', 'decoder.ncnn.bin', 'reference.f32',
             'deconv2d-cpu-validation.log', 'deconv2d-cpu-output.f32', 'original.ncnn.param',
             'original-cpu-validation.log', 'original-cpu-output.f32') + tuple(INPUTS)
    files = {name: asset(directory / name) for name in names}
    marker = json.loads(checked_bytes(directory / names[0], files[names[0]]))
    variant = marker['variant']
    require(variant in VARIANTS, 'unsupported fixed deconvolution graph variant')
    expected = VARIANTS[variant]
    require(marker['format'] == 'radish-kokoro-deconv2d-validation-v1'
            and marker['status'] == 'cpu-reference-passed' and marker['layers'] == expected['layers']
            and marker['blobs'] == expected['blobs'], 'missing exact successful CPU validation marker')
    for key, name, digest in (('param_sha256', 'decoder.ncnn.param', expected['param_sha256']),
                              ('weights_sha256', 'decoder.ncnn.bin', WEIGHTS_SHA256),
                              ('reference_sha256', 'reference.f32', REFERENCE_SHA256)):
        require(marker[key] == digest and files[name]['sha256'] == digest, 'unexpected pinned asset: ' + name)
    require(files['decoder.ncnn.bin']['bytes'] == 213042160, 'unexpected weight byte count')
    require(all(files[name] == pin for name, pin in INPUTS.items()), 'fixed input bytes changed')
    cpu = marker['cpu_validation']
    require(cpu['backend'] == 'cpu-packed' and cpu['outputs'] == COUNT and cpu['mismatches'] == 0
            and cpu['atol'] == ATOL and cpu['rtol'] == RTOL, 'wrong CPU backend/count/reference gates')
    require(isinstance(cpu['runner_sha256'], str) and re.fullmatch(r'[0-9a-f]{64}', cpu['runner_sha256']),
            'missing validated same-runner CPU hash')
    for key, name in (('log_sha256', 'deconv2d-cpu-validation.log'), ('output_sha256', 'deconv2d-cpu-output.f32'),
                      ('original_log_sha256', 'original-cpu-validation.log'), ('original_output_sha256', 'original-cpu-output.f32')):
        require(cpu[key] == files[name]['sha256'], 'CPU evidence hash mismatch: ' + name)
    require(cpu['original_param_sha256'] == expected['original_param_sha256']
            and files['original.ncnn.param']['sha256'] == expected['original_param_sha256'], 'wrong original graph')
    candidate_threads = packed_log(checked_bytes(directory / 'deconv2d-cpu-validation.log', files['deconv2d-cpu-validation.log']).decode(), expected['cpu_layers'])
    original_threads = packed_log(checked_bytes(directory / 'original-cpu-validation.log', files['original-cpu-validation.log']).decode(), expected['original_cpu_layers'])
    require(candidate_threads == original_threads == cpu['threads'], 'CPU thread configurations differ')
    output = checked_bytes(directory / 'deconv2d-cpu-output.f32', files['deconv2d-cpu-output.f32'])
    original = checked_bytes(directory / 'original-cpu-output.f32', files['original-cpu-output.f32'])
    reference = checked_bytes(directory / 'reference.f32', files['reference.f32'])
    if expected.get('require_byte_identity', False):
        require(output == original and cpu.get('original_output_byte_identical') is True,
                'original and NormAffineSnake full CPU outputs differ or lack identity proof')
    result, original_result = comparison(output, reference), comparison(original, reference)
    return dict(format='radish-kokoro-deconv2d-fixture-check-v1', variant=variant, directory=str(directory),
                marker=marker, files=files, cpu_comparison=result, original_cpu_comparison=original_result,
                original_output_byte_identical=output == original,
                validator_dependencies={name: asset(Path(__file__).with_name(name)) for name in
                                        ('validate-kokoro-deconv2d-fixture.py', 'validate-kokoro-affine-snake-fixture.py',
                                         'validate-kokoro-snake-fixture.py')})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(validate(args.directory), indent=2, sort_keys=True))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

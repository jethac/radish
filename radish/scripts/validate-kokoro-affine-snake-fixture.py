#!/usr/bin/env python3
"""Recheck full CPU evidence for the pinned AffineSnake fixture; never write files."""

import argparse
import importlib.util
import json
import math
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location('radish_snake_fixture', Path(__file__).with_name('validate-kokoro-snake-fixture.py'))
snake = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(snake)
require, asset, checked_bytes = snake.require, snake.asset, snake.checked_bytes
PARAM_SHA256 = 'dbd9fbfe67e6053382f1e1734b6480c3a42f0bc59e57b85627fb16a6e9783d5a'
ORIGINAL_PARAM_SHA256 = snake.PARAM_SHA256
WEIGHTS_SHA256, REFERENCE_SHA256 = snake.WEIGHTS_SHA256, snake.REFERENCE_SHA256
COUNT, ATOL, RTOL, INPUTS = snake.COUNT, snake.ATOL, snake.RTOL, snake.INPUTS


def comparison(output_data, reference_data):
    output, reference = snake.floats(output_data), snake.floats(reference_data)
    maximum = error_power = reference_power = 0.0
    for i, (value, expected) in enumerate(zip(output, reference)):
        require(math.isfinite(value) and math.isfinite(expected), 'nonfinite CPU/reference value at ' + str(i))
        error = abs(value - expected)
        require(error <= ATOL + RTOL * abs(expected), 'CPU/reference mismatch at ' + str(i))
        maximum = max(maximum, error)
        error_power += error * error
        reference_power += expected * expected
    require(reference_power > 0, 'reference has zero signal power')
    return dict(outputs=COUNT, mismatches=0, atol=ATOL, rtol=RTOL,
                max_abs=maximum, relative_rms=math.sqrt(error_power / reference_power))


def packed_log(log, cpu_layers):
    matches = re.findall(r'^ncnn Kokoro neural core CPU packed: [^\n]*, 562342 outputs, [^\n]*, '
                         r'mismatches 0, atol=0\.002 rtol=0\.002, packing=on threads=([1-6])$', log, re.MULTILINE)
    require(len(matches) == 1
            and 'PASS: Kokoro neural-core runtime inputs, full FP32-reference output comparison; source/iSTFT outside graph' in log
            and 'execution configuration: CPU, FP32, packing=on threads=' + matches[0] in log
            and f'loaded graph: 0 GPU-capable, {cpu_layers} CPU, 5 Input layers; inventory is not an execution trace' in log,
            'CPU log lacks the expected packed graph and complete full-reference pass')
    return int(matches[0])


def validate(directory):
    directory = Path(directory).resolve()
    names = ('affine-snake-validation.json', 'decoder.ncnn.param', 'decoder.ncnn.bin', 'reference.f32',
             'affine-snake-cpu-validation.log', 'affine-snake-cpu-output.f32', 'original-snake.ncnn.param',
             'original-snake-cpu-validation.log', 'original-snake-cpu-output.f32') + tuple(INPUTS)
    files = {name: asset(directory / name) for name in names}
    marker = json.loads(checked_bytes(directory / names[0], files[names[0]]))
    require(marker['format'] == 'radish-kokoro-affine-snake-validation-v1'
            and marker['status'] == 'cpu-reference-passed' and marker['layers'] == 714 and marker['blobs'] == 875,
            'missing successful exact AffineSnake CPU validation marker')
    for key, name, digest in (('param_sha256', 'decoder.ncnn.param', PARAM_SHA256),
                              ('weights_sha256', 'decoder.ncnn.bin', WEIGHTS_SHA256),
                              ('reference_sha256', 'reference.f32', REFERENCE_SHA256)):
        require(marker[key] == digest and files[name]['sha256'] == digest, 'unexpected pinned AffineSnake asset: ' + name)
    require(files['decoder.ncnn.bin']['bytes'] == 213042160, 'unexpected weight byte count')
    require(all(files[name] == expected for name, expected in INPUTS.items()), 'fixed AffineSnake input bytes changed')
    cpu = marker['cpu_validation']
    require(cpu['backend'] == 'cpu-packed' and cpu['outputs'] == COUNT and cpu['mismatches'] == 0
            and cpu['atol'] == ATOL and cpu['rtol'] == RTOL, 'wrong CPU backend/count/comparison gates')
    require(isinstance(cpu['runner_sha256'], str) and re.fullmatch(r'[0-9a-f]{64}', cpu['runner_sha256']),
            'missing validated same-runner CPU hash')
    for key, name in (('log_sha256', 'affine-snake-cpu-validation.log'), ('output_sha256', 'affine-snake-cpu-output.f32'),
                      ('original_log_sha256', 'original-snake-cpu-validation.log'), ('original_output_sha256', 'original-snake-cpu-output.f32')):
        require(cpu[key] == files[name]['sha256'], 'CPU validation evidence hash mismatch: ' + name)
    require(cpu['original_param_sha256'] == ORIGINAL_PARAM_SHA256
            and files['original-snake.ncnn.param']['sha256'] == ORIGINAL_PARAM_SHA256,
            'unexpected original Snake graph')
    affine_threads = packed_log(checked_bytes(directory / 'affine-snake-cpu-validation.log', files['affine-snake-cpu-validation.log']).decode(), 709)
    original_threads = packed_log(checked_bytes(directory / 'original-snake-cpu-validation.log', files['original-snake-cpu-validation.log']).decode(), 901)
    require(affine_threads == original_threads == cpu['threads'], 'original and fused CPU thread configurations differ')
    output = checked_bytes(directory / 'affine-snake-cpu-output.f32', files['affine-snake-cpu-output.f32'])
    original = checked_bytes(directory / 'original-snake-cpu-output.f32', files['original-snake-cpu-output.f32'])
    require(output == original and cpu['original_output_byte_identical'] is True,
            'original and AffineSnake full CPU outputs differ')
    result = comparison(output, checked_bytes(directory / 'reference.f32', files['reference.f32']))
    return dict(format='radish-kokoro-affine-snake-fixture-check-v1', directory=str(directory),
                marker=marker, files=files, cpu_comparison=result, original_output_byte_identical=True,
                validator_dependencies={name: asset(Path(__file__).with_name(name)) for name in
                                        ('validate-kokoro-affine-snake-fixture.py', 'validate-kokoro-snake-fixture.py')})


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

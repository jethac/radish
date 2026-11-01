#!/usr/bin/env python3
"""Import completed same-runner CPU evidence into a new validated AffineSnake fixture."""

import argparse
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True


def sibling(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


check = sibling('radish_affine_fixture', 'validate-kokoro-affine-snake-fixture.py')
rewrite = sibling('radish_affine_rewrite', 'fuse-kokoro-affine-snake.py')
require, asset, checked_bytes = check.require, check.asset, check.checked_bytes


def prepare(source, evidence, runner, output):
    source, evidence, runner = source.resolve(), evidence.resolve(), runner.resolve()
    require(not output.exists() and not output.is_symlink(), 'output must be new')
    output = output.resolve()
    require(output.is_relative_to(rewrite.WORK.resolve()) and output != rewrite.WORK.resolve()
            and not output.is_relative_to(source) and not output.is_relative_to(evidence),
            'output must be a new private WORK directory outside source and evidence')
    results_file = asset(evidence / 'results.json')
    results = json.loads(checked_bytes(evidence / 'results.json', results_file))
    runner_file = asset(runner)
    require(results['runner_sha256'] == runner_file['sha256'], 'completed CPU runner hash differs')
    require(results['full_output_bytes_identical'] is True, 'completed CPU result lacks original/output identity')
    imports = {}
    for branch, prefix, param_pin in (('original', 'original-snake', check.ORIGINAL_PARAM_SHA256),
                                       ('affine', 'affine-snake', check.PARAM_SHA256)):
        require(asset(evidence / branch / 'decoder.ncnn.param')['sha256'] == param_pin, 'wrong completed CPU graph')
        for name, pin in dict(check.INPUTS, **{'decoder.ncnn.bin': {'bytes': 213042160, 'sha256': check.WEIGHTS_SHA256},
                                             'reference.f32': {'bytes': check.COUNT * 4, 'sha256': check.REFERENCE_SHA256}}).items():
            require(asset(evidence / branch / name) == pin, 'completed CPU input/weights/reference changed: ' + branch + '/' + name)
        log_path = evidence / (branch + '-cpu.log')
        out_path = evidence / branch / 'ncnn-cpu-packed-output.f32'
        log_info, out_info = asset(log_path), asset(out_path)
        result = results['full'][branch]
        require(result['log_sha256'] == log_info['sha256'] and result['output_sha256'] == out_info['sha256']
                and result['outputs'] == check.COUNT and result['mismatches'] == 0
                and result['atol'] == check.ATOL and result['rtol'] == check.RTOL, 'completed CPU result hashes/gates differ')
        imports[prefix + '-cpu-validation.log'] = checked_bytes(log_path, log_info)
        imports[prefix + '-cpu-output.f32'] = checked_bytes(out_path, out_info)
    require(imports['affine-snake-cpu-output.f32'] == imports['original-snake-cpu-output.f32'], 'CPU outputs differ')
    threads = check.packed_log(imports['affine-snake-cpu-validation.log'].decode(), 709)
    require(threads == check.packed_log(imports['original-snake-cpu-validation.log'].decode(), 901), 'CPU thread counts differ')
    output.parent.mkdir(parents=True, exist_ok=True)
    container = Path(tempfile.mkdtemp(prefix='.validated-affine-snake-', dir=output.parent))
    stage = container / 'fixture'
    reserved = False
    try:
        with contextlib.redirect_stdout(sys.stderr):
            rewrite.create(source, stage)
        require(asset(stage / 'decoder.ncnn.param')['sha256'] == check.PARAM_SHA256, 'regenerated candidate graph differs')
        for name, data in imports.items():
            (stage / name).write_bytes(data)
        original_param = checked_bytes(source / 'decoder.ncnn.param', asset(source / 'decoder.ncnn.param'))
        (stage / 'original-snake.ncnn.param').write_bytes(original_param)
        comparison = check.comparison(imports['affine-snake-cpu-output.f32'], checked_bytes(stage / 'reference.f32', asset(stage / 'reference.f32')))
        cpu = dict(comparison, backend='cpu-packed', threads=threads, runner_sha256=runner_file['sha256'],
                   original_param_sha256=check.ORIGINAL_PARAM_SHA256, original_output_byte_identical=True)
        for key, name in (('log_sha256', 'affine-snake-cpu-validation.log'), ('output_sha256', 'affine-snake-cpu-output.f32'),
                          ('original_log_sha256', 'original-snake-cpu-validation.log'), ('original_output_sha256', 'original-snake-cpu-output.f32')):
            cpu[key] = asset(stage / name)['sha256']
        marker = dict(format='radish-kokoro-affine-snake-validation-v1', status='cpu-reference-passed', layers=714, blobs=875,
                      param_sha256=check.PARAM_SHA256, weights_sha256=check.WEIGHTS_SHA256,
                      reference_sha256=check.REFERENCE_SHA256, cpu_validation=cpu,
                      evidence_directory=str(evidence), evidence_results_sha256=results_file['sha256'],
                      validation_scope='Completed host CPU evidence only; GPU correctness and performance remain separate gates.')
        (stage / 'affine-snake-validation.json').write_text(json.dumps(marker, indent=2, sort_keys=True) + '\n')
        check.validate(stage)
        output.mkdir()
        reserved = True
        os.replace(stage, output)
        reserved = False
    finally:
        if container.exists():
            shutil.rmtree(container)
        if reserved:
            output.rmdir()
    print(json.dumps(check.validate(output), indent=2, sort_keys=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='pinned original Snake fixture')
    parser.add_argument('evidence', type=Path, help='completed host validation directory with results.json, original/ and affine/')
    parser.add_argument('runner', type=Path, help='unchanged executable that produced both completed CPU runs')
    parser.add_argument('output', type=Path, help='new fixture directory; never overwrites')
    args = parser.parse_args()
    try:
        prepare(args.source, args.evidence, args.runner, args.output)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

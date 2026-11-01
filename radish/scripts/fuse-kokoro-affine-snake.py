#!/usr/bin/env python3
"""Prepare a provisional FP32 style-affine Snake graph without executing tensors."""

import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

WORK = Path('/run/media/jethac/Shared/radish')
PARAM_SHA256 = 'e01c56a4f23d6d3c8f92dbecb0c3fc37f6f74e9f305f58b82d5196278a450d38'
sys.dont_write_bytecode = True
specification = importlib.util.spec_from_file_location(
    'radish_snake_graph', Path(__file__).with_name('fuse-kokoro-snake.py'))
snake = importlib.util.module_from_spec(specification)
specification.loader.exec_module(snake)
require, sha256 = snake.require, snake.sha256
SOURCE_INVENTORY = dict(snake.TARGET_INVENTORY)
TARGET_INVENTORY = dict(SOURCE_INVENTORY, BinaryOp=72, Split=62, RadishAffineSnake=48)
del TARGET_INVENTORY['RadishSnake']


def rewrite(data):
    require(hashlib.sha256(data).hexdigest() == PARAM_SHA256, 'unexpected pinned Snake graph')
    rows, producers, shapes, consumers = snake.read_graph(data, 906, 1067, SOURCE_INVENTORY)
    groups, removed, replacements, constants = [], set(), {}, set()
    style_vectors = set()

    def operation(row, kind, params, inputs, outputs=1):
        require(row['type'] == kind and row['params'] == params
                and len(row['bottoms']) == inputs and len(row['tops']) == outputs,
                'unexpected affine-Snake operator: ' + row['name'])
        return row

    def sole(blob, row, slot):
        require(consumers[blob] == [(row, slot)], 'affine-Snake intermediate has another consumer: ' + blob)

    def normalized_source(blob):
        row = producers[blob]
        while row['type'] == 'Split':
            require(not row['params'] and len(row['bottoms']) == 1
                    and all(shapes[top] == shapes[blob] for top in row['tops']),
                    'nontransparent normalized-input Split')
            blob = row['bottoms'][0]
            row = producers[blob]
        require(row['type'] == 'Reshape' and row['name'].startswith('radish_norm_')
                and row['name'].endswith('_to2d') and len(row['bottoms']) == len(row['tops']) == 1,
                'affine input does not originate from a normalized rank-two tensor')
        norm = producers[row['bottoms'][0]]
        require(norm['type'] == 'InstanceNorm' and norm['params'] ==
                {'0': str(shapes[blob][2]), '1': '1.000000e-5', '2': '0', '31': '2'},
                'unexpected normalization origin')
        return row['name'], blob

    for layer in rows:
        if layer['type'] != 'RadishSnake':
            continue
        operation(layer, 'RadishSnake', {}, 3)
        styled_alias, alpha, reciprocal = layer['bottoms']
        split = operation(producers[styled_alias], 'Split', {}, 1)
        sole(styled_alias, layer, 0)
        styled = split['bottoms'][0]
        add = operation(producers[styled], 'BinaryOp', {'0': '0'}, 2)
        sole(styled, split, 0)
        product, beta = add['bottoms']
        mul = operation(producers[product], 'BinaryOp', {'0': '2'}, 2)
        sole(product, add, 0)
        gamma1, x = mul['bottoms']
        plus = operation(producers[gamma1], 'BinaryOp', {'0': '0', '1': '1', '2': '1.0'}, 1)
        sole(gamma1, mul, 0)
        gamma = plus['bottoms'][0]
        sole(gamma, plus, 0)
        sole(beta, add, 1)
        sole(x, mul, 1)
        logical = shapes[x]
        require(logical in ([2, 4260, 256, 1, 1], [2, 25561, 128, 1, 1]),
                'unexpected pinned affine-Snake shape')
        channels = logical[2]
        vector = [2, 1, channels, 1, 1]
        require(all(shapes[blob] == logical for blob in
                    (product, styled, styled_alias, layer['tops'][0]))
                and all(shapes[blob] == vector for blob in (gamma, gamma1, beta, alpha, reciprocal)),
                'affine-Snake shape mismatch')
        style_slice = operation(producers[gamma], 'Slice', {'-23300': f'2,{channels},-233', '1': '0'}, 1, 2)
        require(producers[beta] is style_slice and style_slice['tops'] == [gamma, beta]
                and shapes[style_slice['bottoms'][0]] == [2, 1, 2 * channels, 1, 1],
                'gamma/beta must retain their original projection Slice order')
        require(not style_vectors.intersection((gamma, beta)), 'style vector reused across a fused chain')
        style_vectors.update((gamma, beta))
        origin, normalized = normalized_source(x)
        for blob, slot in ((alpha, 1), (reciprocal, 2)):
            constant = operation(producers[blob], 'MemoryData', {'0': '1', '1': str(channels)}, 0)
            require(blob not in constants and constant['tops'] == [blob], 'reused Snake constant')
            sole(blob, layer, slot)
            constants.add(blob)
        chain = [plus, mul, add, split]
        indexes = {row['index'] for row in chain}
        require(len(indexes) == 4 and not indexes.intersection(removed | set(replacements))
                and layer['index'] not in removed | set(replacements), 'overlapping affine-Snake chains')
        require(plus['index'] < mul['index'] < add['index'] < split['index'] < layer['index'],
                'affine-Snake chain is not ordered')
        removed.update(indexes)
        replacements[layer['index']] = dict(layer, type='RadishAffineSnake',
                                             bottoms=[x, gamma, beta, alpha, reciprocal], params={})
        groups.append(dict(layer=layer['name'], output=layer['tops'][0], inputs=[x, gamma, beta, alpha, reciprocal],
                           shape=logical, normalized_source=normalized, normalized_layer=origin,
                           style_slice=style_slice['name'], removed_layers=[row['name'] for row in chain],
                           removed_blobs=[row['tops'][0] for row in chain],
                           arithmetic=['gamma+1', 'gamma1*x', 'product+beta', 'alpha*styled', 'sin',
                                       'square', 'stored_reciprocal*square', 'styled+product']))
    require(len(groups) == len(replacements) == 48 and len(removed) == 192
            and len(constants) == len(style_vectors) == 96, 'expected exactly48 disjoint affine-Snake chains')
    require(Counter(group['shape'][2] for group in groups) == {128: 24, 256: 24},
            'unexpected affine-Snake shape families')
    require(constants == {row['tops'][0] for row in rows if row['type'] == 'MemoryData'},
            'all96 original stored constants must be retained')
    lines = ['7767517\n', '714 875\n']
    for row in rows:
        if row['index'] in removed:
            continue
        lines.append(snake.render(replacements[row['index']], shapes)
                     if row['index'] in replacements else row['raw'])
    rewritten = ''.join(lines).encode()
    kept, final_producers, final_shapes, _ = snake.read_graph(rewritten, 714, 875, TARGET_INVENTORY)
    weighted = [row for row in rows if row['type'] in snake.WEIGHTED]
    require(len(weighted) == 226 and [row['raw'] for row in kept if row['type'] in snake.WEIGHTED]
            == [row['raw'] for row in weighted], 'weighted rows or their order changed')
    require(all(final_shapes[name] == shapes[name] for name in final_shapes), 'retained logical shape changed')
    removed_blobs = {blob for group in groups for blob in group['removed_blobs']}
    require(len(removed_blobs) == 192 and set(producers) - set(final_producers) == removed_blobs,
            'unexpected retained blob-name changes')
    unchanged = {row['name']: row['raw'] for row in rows if row['index'] not in removed | set(replacements)}
    require(all(row['raw'] == unchanged[row['name']] for row in kept if row['name'] in unchanged),
            'unrelated row changed')
    return rewritten, groups, weighted


def create(source, output):
    source = source.resolve()
    require(not output.exists() and not output.is_symlink(), 'output must be new')
    output = output.resolve()
    require(output.is_relative_to(WORK.resolve()) and output != WORK.resolve()
            and not output.is_relative_to(source), 'output must be a new private WORK directory outside source')
    parameters, groups, weighted = rewrite((source / 'decoder.ncnn.param').read_bytes())
    linked = {}
    for name, (size, digest) in snake.helpers.FIXTURES.items():
        path = source / name
        require(path.is_file() and path.stat().st_size == size and sha256(path) == digest,
                'unexpected immutable source asset: ' + name)
        linked[name] = {'bytes': size, 'sha256': digest}
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.kokoro-affine-snake-', dir=output.parent))
    reserved = False
    try:
        for name in linked:
            os.link(source / name, stage / name)
        (stage / 'decoder.ncnn.param').write_bytes(parameters)
        require(sha256(source / 'decoder.ncnn.param') == PARAM_SHA256, 'source graph changed')
        for name, expected in linked.items():
            require(sha256(stage / name) == expected['sha256'], 'immutable linked asset changed: ' + name)
        manifest = dict(format='radish-kokoro-affine-snake-provisional-v1', status='provisional',
                        source_directory=str(source), source_param_sha256=PARAM_SHA256,
                        param_sha256=hashlib.sha256(parameters).hexdigest(), layers=714, blobs=875,
                        inventory=TARGET_INVENTORY, affine_snake_chains=48, retained_memorydata=96,
                        unchanged_weighted_rows=[row['name'] for row in weighted],
                        removed_style_arithmetic_rows=144, removed_one_output_splits=48,
                        removed_blob_names=192, groups=groups, linked_files=linked,
                        required_custom_layer='RadishAffineSnake', input_order=['x', 'gamma', 'beta', 'alpha', 'stored_reciprocal'],
                        precision='FP32 only; retain every ordered FP32 intermediate and original stored reciprocal',
                        validation='Structural rewrite only. No tensors executed; full CPU/reference and GPU validation pending.',
                        metadata_validation='Fixed pinned shape hints retained; metadata-engine integration and dynamic shape checks pending.',
                        cpu_validation='Pending; not a validated fixture.', gpu_validation='Pending; no correctness or performance claim.',
                        accuracy_gates={'core_atol': 0.002, 'core_rtol': 0.002})
        (stage / 'affine-snake-provisional.json').write_text(json.dumps(manifest, indent=2) + '\n')
        output.mkdir()
        reserved = True
        os.replace(stage, output)
        reserved = False
    finally:
        if stage.exists():
            shutil.rmtree(stage)
        if reserved:
            output.rmdir()
    print(f'PROVISIONAL:48 affine-Snake chains /714 layers /875 blobs: {output}')
    print('Graph SHA256: ' + hashlib.sha256(parameters).hexdigest() + '; no tensors executed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='exact pinned kokoro-core-shapes-snake fixture')
    parser.add_argument('output', type=Path, help='new private provisional fixture; never overwrites')
    args = parser.parse_args()
    try:
        create(args.source, args.output)
    except (OSError, ValueError, KeyError, IndexError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

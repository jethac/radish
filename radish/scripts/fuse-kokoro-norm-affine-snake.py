#!/usr/bin/env python3
"""Prepare 42 sole-consumer normalization/AffineSnake chains; CPU proof is separate."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

WORK = Path('/run/media/jethac/Shared/radish')
PARAM_SHA256 = 'e211a573578f5ad45000b487f6e36ac1376ae4bbdcfa6a6e32a95070ffbc807d'
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('radish_snake_graph', Path(__file__).with_name('fuse-kokoro-snake.py'))
snake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(snake)
require, sha256 = snake.require, snake.sha256
SOURCE_INVENTORY = {'Input': 5, 'Split': 62, 'Crop': 1, 'InnerProduct': 58, 'Reshape': 174,
                    'Slice': 58, 'Convolution1D': 69, 'Concat': 5, 'InstanceNorm': 54,
                    'BinaryOp': 72, 'ReLU': 13, 'MemoryData': 96, 'RadishAffineSnake': 48,
                    'Interp': 1, 'DeconvolutionDepthWise': 1, 'Deconvolution': 2, 'Padding': 1}
TARGET_INVENTORY = dict(SOURCE_INVENTORY, Reshape=90, InstanceNorm=12, RadishAffineSnake=6,
                        RadishNormAffineSnake=42)
WEIGHTED = {'InnerProduct', 'Convolution1D', 'Deconvolution', 'DeconvolutionDepthWise', 'MemoryData'}
SHARED_AFFINES = {'add_270', 'add_275', 'add_280', 'add_508', 'add_513', 'add_518'}


def rewrite(data):
    require(hashlib.sha256(data).hexdigest() == PARAM_SHA256, 'unexpected pinned AffineSnake/deconv2d graph')
    rows, producers, shapes, consumers = snake.read_graph(data, 720, 881, SOURCE_INVENTORY)
    names = {row['name'] for row in rows}
    removed, replacements, groups, shared = set(), {}, [], set()
    for affine in rows:
        if affine['type'] != 'RadishAffineSnake':
            continue
        require(len(affine['bottoms']) == 5 and len(affine['tops']) == 1 and not affine['params'],
                'unexpected AffineSnake arity or parameters')
        to2d = producers[affine['bottoms'][0]]
        if to2d['type'] == 'Split':
            shared.add(affine['name'])
            continue
        match = re.fullmatch(r'radish_norm_(0|[1-9][0-9]*)_to2d', to2d['name'])
        require(to2d['type'] == 'Reshape' and match is not None
                and len(to2d['bottoms']) == len(to2d['tops']) == 1, 'unexpected direct normalization output')
        prefix = 'radish_norm_' + match[1]
        norm = producers[to2d['bottoms'][0]]
        require(norm['type'] == 'InstanceNorm' and norm['name'] == prefix + '_instance'
                and len(norm['bottoms']) == len(norm['tops']) == 1, 'unexpected normalization row')
        to3d = producers[norm['bottoms'][0]]
        require(to3d['type'] == 'Reshape' and to3d['name'] == prefix + '_to3d'
                and len(to3d['bottoms']) == len(to3d['tops']) == 1, 'unexpected normalization input')
        raw, output = to3d['bottoms'][0], affine['tops'][0]
        dims, width, channels, depth, last = shapes[raw]
        require(dims == 2 and channels in (128, 256) and depth == last == 1
                and width == (25561 if channels == 128 else 4260), 'unexpected fixed raw input shape')
        require(to3d['params'] == {'0': '-1', '1': '1', '2': str(channels), '31': '2'}
                and norm['params'] == {'0': str(channels), '1': '1.000000e-5', '2': '0', '31': '2'}
                and to2d['params'] == {'0': '-1', '1': str(channels), '31': '2'},
                'normalization parameters changed')
        require(shapes[to3d['tops'][0]] == shapes[norm['tops'][0]] == [3, width, 1, 1, channels]
                and shapes[to2d['tops'][0]] == shapes[output] == shapes[raw]
                and all(shapes[name] == [2, 1, channels, 1, 1] for name in affine['bottoms'][1:]),
                'normalization/AffineSnake shape contract differs')
        chain = (to3d, norm, to2d, affine)
        require(all(a['index'] < b['index'] for a, b in zip(chain, chain[1:]))
                and all(consumers[a['tops'][0]] == [(b, 0)] for a, b in zip(chain, chain[1:])),
                'normalization intermediate is shared or not topological')
        indices = {row['index'] for row in chain}
        require(not indices.intersection(removed | set(replacements)), 'overlapping replacement chains')
        new_name = prefix + '_affine_snake'
        require(new_name not in names, 'new layer name collision')
        names.add(new_name)
        removed.update(row['index'] for row in chain[:3])
        replacement = dict(affine, type='RadishNormAffineSnake', name=new_name,
                           bottoms=[raw] + affine['bottoms'][1:])
        replacements[affine['index']] = replacement
        groups.append(dict(norm_prefix=prefix, original_layers=[row['name'] for row in chain],
                           original_indices=[row['index'] for row in chain], name=new_name,
                           inputs=replacement['bottoms'], output=output, shape=shapes[output],
                           removed_blobs=[row['tops'][0] for row in chain[:3]]))
    require(len(groups) == 42 and len(removed) == 126 and shared == SHARED_AFFINES
            and Counter(group['shape'][2] for group in groups) == {128: 21, 256: 21},
            'expected exactly 42 direct chains and six shared normalization branches')
    lines = ['7767517\n', '594 755\n']
    for row in rows:
        if row['index'] in removed:
            continue
        lines.append(snake.render(replacements[row['index']], shapes) if row['index'] in replacements else row['raw'])
    result = ''.join(lines).encode()
    rewritten, new_producers, new_shapes, _ = snake.read_graph(result, 594, 755, TARGET_INVENTORY)
    removed_blobs = {name for group in groups for name in group['removed_blobs']}
    require(set(producers) - set(new_producers) == removed_blobs and not (set(new_producers) - set(producers))
            and new_shapes == {name: value for name, value in shapes.items() if name not in removed_blobs},
            'unexpected removed, added or changed blob shapes')
    weighted_old = [row for row in rows if row['type'] in WEIGHTED]
    weighted_new = [row for row in rewritten if row['type'] in WEIGHTED]
    require(len(weighted_old) == len(weighted_new) == 226
            and [row['raw'] for row in weighted_old] == [row['raw'] for row in weighted_new],
            'weighted rows or their order changed')
    untouched = {row['name']: row['raw'] for row in rows if row['index'] not in removed | set(replacements)}
    require(all(row['raw'] == untouched[row['name']] for row in rewritten if row['name'] in untouched),
            'unrelated graph row changed')
    return result, groups, [row['name'] for row in weighted_old]


def create(source, output):
    source = source.resolve()
    require(not output.exists() and not output.is_symlink(), 'output must be new')
    output = output.resolve()
    require(output.is_relative_to(WORK.resolve()) and output != WORK.resolve() and not output.is_relative_to(source),
            'output must be a new private WORK directory outside source')
    param = source / 'decoder.ncnn.param'
    require(param.is_file() and not param.is_symlink(), 'source graph must be a regular file')
    parameters, groups, weighted = rewrite(param.read_bytes())
    assets = {}
    for name, (size, digest) in snake.helpers.FIXTURES.items():
        path = source / name
        require(path.is_file() and not path.is_symlink() and path.stat().st_size == size and sha256(path) == digest,
                'unexpected source asset: ' + name)
        assets[name] = dict(bytes=size, sha256=digest)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.norm-affine-snake-', dir=output.parent))
    reserved = False
    try:
        for name in assets:
            os.link(source / name, stage / name)
        (stage / 'decoder.ncnn.param').write_bytes(parameters)
        require(sha256(param) == PARAM_SHA256 and all(sha256(stage / name) == pin['sha256'] for name, pin in assets.items()),
                'source graph or immutable asset changed')
        marker = dict(format='radish-kokoro-norm-affine-snake-provisional-v1', status='provisional',
                      source_param_sha256=PARAM_SHA256, param_sha256=hashlib.sha256(parameters).hexdigest(),
                      layers=594, blobs=755, inventory=TARGET_INVENTORY, replacements=groups,
                      weighted_row_order=weighted, linked_assets=assets, required_custom_layer='RadishNormAffineSnake',
                      input_order=['raw', 'gamma', 'beta', 'alpha', 'stored_reciprocal'],
                      validation='Structural rewrite only; same-runner CPU reference and byte-identity proof, GPU and waveform gates remain required.')
        (stage / 'norm-affine-snake-provisional.json').write_text(json.dumps(marker, indent=2) + '\n')
        output.mkdir()
        reserved = True
        os.replace(stage, output)
        reserved = False
    finally:
        if stage.exists():
            shutil.rmtree(stage)
        if reserved:
            output.rmdir()
    print(json.dumps(marker, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        create(args.source, args.output)
    except (OSError, ValueError, KeyError, IndexError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

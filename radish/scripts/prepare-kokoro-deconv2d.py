#!/usr/bin/env python3
"""Prepare exactly three height-one 2D deconvolutions from the pinned Snake graph."""
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
spec = importlib.util.spec_from_file_location('radish_snake_graph', Path(__file__).with_name('fuse-kokoro-snake.py'))
snake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(snake)
require, sha256 = snake.require, snake.sha256
TARGETS = {
    'deconvdw1d_312': ('DeconvolutionDepthWise1D', 'DeconvolutionDepthWise', 1090, 213,
                     {'0':'1090','1':'3','18':'1','2':'1','3':'2','4':'1','5':'1','6':'3270','7':'1090'}),
    'deconv1d_84': ('Deconvolution1D', 'Deconvolution', 512, 426,
                   {'0':'256','1':'20','18':'0','2':'1','3':'10','4':'5','5':'1','6':'2621440'}),
    'deconv1d_85': ('Deconvolution1D', 'Deconvolution', 256, 4260,
                   {'0':'128','1':'12','18':'0','2':'1','3':'6','4':'3','5':'1','6':'393216'}),
}
VERTICAL = {'11':'1','12':'1','13':'1','14':'0','16':'0','19':'0','21':'0'}
INVENTORY = dict(snake.TARGET_INVENTORY, Reshape=174, DeconvolutionDepthWise=1, Deconvolution=2)
del INVENTORY['DeconvolutionDepthWise1D'], INVENTORY['Deconvolution1D']


def rewrite(data):
    require(hashlib.sha256(data).hexdigest() == PARAM_SHA256, 'unexpected pinned Snake graph')
    rows, producers, shapes, _ = snake.read_graph(data, 906, 1067, snake.TARGET_INVENTORY)
    replacements, groups = {}, []
    for row in rows:
        if row['name'] not in TARGETS:
            continue
        oldtype, newtype, channels, width, params = TARGETS[row['name']]
        require(row['type'] == oldtype and row['params'] == params and len(row['bottoms']) == len(row['tops']) == 1,
                'unexpected exact deconvolution row: ' + row['name'])
        bottom, top = row['bottoms'][0], row['tops'][0]
        require(shapes[bottom] == [2,width,channels,1,1], 'unexpected deconvolution input shape')
        out_channels, kernel, stride = int(params['0']), int(params['1']), int(params['3'])
        out_width = (width-1)*stride + kernel + int(params['18']) - 2*int(params['4'])
        require(shapes[top] == [2,out_width,out_channels,1,1], 'unexpected deconvolution output shape')
        prefix = 'radish_deconv2d_' + str(len(groups))
        input3d, output3d = prefix + '_input', prefix + '_output'
        require(input3d not in producers and output3d not in producers
                and all(x['name'] not in (prefix+'_to3d',prefix+'_to2d') for x in rows), 'new graph name collision')
        shapes[input3d] = [3,width,1,1,channels]
        shapes[output3d] = [3,out_width,1,1,out_channels]
        before = dict(type='Reshape', name=prefix+'_to3d', bottoms=[bottom], tops=[input3d], params={'0':'-1','1':'1','2':str(channels)})
        converted = dict(row, type=newtype, bottoms=[input3d], tops=[output3d], params=dict(row['params'], **VERTICAL))
        after = dict(type='Reshape', name=prefix+'_to2d', bottoms=[output3d], tops=[top], params={'0':'-1','1':str(out_channels)})
        replacements[row['index']] = [before,converted,after]
        groups.append(dict(name=row['name'], original_type=oldtype, new_type=newtype, input=bottom, output=top,
                           input3d=input3d, output3d=output3d, input_shape=shapes[bottom], output_shape=shapes[top],
                           horizontal_parameters=dict(params), explicit_vertical_parameters=VERTICAL,
                           unchanged_weight_values=int(params['6']), unchanged_bias_values=out_channels))
    require(len(groups) == 3 and {g['name'] for g in groups} == set(TARGETS), 'expected exactly three replacements')
    lines = ['7767517\n','912 1073\n']
    for row in rows:
        if row['index'] in replacements:
            lines.extend(snake.render(x,shapes) for x in replacements[row['index']])
        else:
            lines.append(row['raw'])
    result = ''.join(lines).encode()
    rewritten, new_producers, new_shapes, _ = snake.read_graph(result,912,1073,INVENTORY)
    require(all(new_shapes[name] == shape for name,shape in shapes.items()), 'retained or inserted shape changed')
    require(set(new_producers)-set(producers) == {g[k] for g in groups for k in ('input3d','output3d')}, 'unexpected new blobs')
    weighted_old = [r for r in rows if r['type'] in snake.WEIGHTED]
    weighted_new = [r for r in rewritten if r['type'] in snake.WEIGHTED | {'Deconvolution','DeconvolutionDepthWise'}]
    require(len(weighted_old) == len(weighted_new) == 226, 'weighted row count changed')
    for old,new in zip(weighted_old,weighted_new):
        require(old['name'] == new['name'], 'weighted row order changed')
        if old['name'] in TARGETS:
            require(new['params'] == dict(old['params'],**VERTICAL), 'weight-consuming parameters changed')
        else:
            require(old['raw'] == new['raw'], 'unrelated weighted row changed')
    untouched = {r['name']:r['raw'] for r in rows if r['name'] not in TARGETS}
    require(all(r['raw'] == untouched[r['name']] for r in rewritten if r['name'] in untouched), 'unrelated graph row changed')
    return result, groups, [r['name'] for r in weighted_old]


def create(source, output):
    source = source.resolve()
    require(not output.exists() and not output.is_symlink(), 'output must be new')
    output = output.resolve()
    require(output.is_relative_to(WORK.resolve()) and output != WORK.resolve() and not output.is_relative_to(source),
            'output must be a new private WORK directory outside source')
    parameters, groups, weighted = rewrite((source/'decoder.ncnn.param').read_bytes())
    assets = {}
    for name,(size,digest) in snake.helpers.FIXTURES.items():
        p = source/name
        require(p.is_file() and not p.is_symlink() and p.stat().st_size == size and sha256(p) == digest, 'unexpected source asset: '+name)
        assets[name] = dict(bytes=size,sha256=digest)
    output.parent.mkdir(parents=True,exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.deconv2d-',dir=output.parent));reserved=False
    try:
        for name in assets:os.link(source/name,stage/name)
        (stage/'decoder.ncnn.param').write_bytes(parameters)
        require(sha256(source/'decoder.ncnn.param') == PARAM_SHA256, 'source graph changed')
        require(all(sha256(stage/name) == v['sha256'] for name,v in assets.items()), 'source asset changed')
        marker=dict(format='radish-kokoro-deconv2d-provisional-v1',status='provisional',source_param_sha256=PARAM_SHA256,
                    param_sha256=hashlib.sha256(parameters).hexdigest(),layers=912,blobs=1073,inventory=INVENTORY,
                    replacements=groups,weighted_row_order=weighted,linked_assets=assets,
                    validation='Structural rewrite only; CPU numerical and GPU compiler/full-output gates remain required.',
                    scope='Three exact height-one 2D replacements only; accepted Snake remains unchanged; no AffineSnake combination.')
        (stage/'deconv2d-provisional.json').write_text(json.dumps(marker,indent=2)+'\n')
        output.mkdir();reserved=True;os.replace(stage,output);reserved=False
    finally:
        if stage.exists():shutil.rmtree(stage)
        if reserved:output.rmdir()
    print(json.dumps(marker,indent=2))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    try:create(a.source,a.output)
    except (OSError,ValueError,KeyError,IndexError) as e:p.error(str(e))

if __name__=='__main__':main()

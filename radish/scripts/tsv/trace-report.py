#!/usr/bin/env python3
"""Summarise a Radish execution trace (RADISH_TRACE file) joined with ncnn's
RADISH_TRACE layer lines (process stderr).

usage: trace-report.py TRACE STDERR [--request N] [--top K]
"""
import argparse, collections, re

kv = re.compile(r'(\w+)=(\S+)')
def parse(line):
    return dict(kv.findall(line))

ap = argparse.ArgumentParser()
ap.add_argument('trace'); ap.add_argument('stderr')
ap.add_argument('--request', type=int, default=-1, help='extraction index (default: last)')
ap.add_argument('--top', type=int, default=25)
ap.add_argument('--param', help='ncnn .param with shape hints, for per-layer GFLOP/s')
a = ap.parse_args()

cmds = [parse(l) for l in open(a.trace) if l.startswith('C ')]
for c in cmds:
    for k in ('seq','type','chunks','submits','bytes','host_start','host','flush','wait','gpu','gpu_first','gpu_last','slots','shared','barrier'):
        c[k] = int(c[k])
phases, layers = [], []
for l in open(a.stderr, errors='replace'):
    if 'RADISH_TRACE' not in l: continue
    d = parse(l[l.index('RADISH_TRACE'):])
    if d.get('event') == 'phase': phases.append(d)
    elif 'layer' in d and 'type' in d: layers.append(d)

# extraction windows from phase markers
ext = []
for p in phases:
    if p['name'] == 'extract_begin': ext.append({'begin': int(p['t']), 'seq0': int(p['dispatch_seq'])})
    elif ext: ext[-1][p['name']] = int(p['t']); ext[-1][p['name']+'_seq'] = int(p['dispatch_seq'])
e = ext[a.request]
t0, t1 = e['begin'], e['final_done']
sel = [c for c in cmds if t0 <= c['host_start'] <= t1]
wall = (t1 - t0) / 1e9
S = lambda k, xs=sel: sum(c[k] for c in xs) / 1e9
disp = [c for c in sel if c['type'] == 0]
print(f'extraction {a.request}: wall {wall:.3f} s, {len(sel)} driver commands ({len(disp)} dispatches)')
print(f'  in driver (submit thread): host {S("host"):.3f} s = flush/ioctl {S("flush"):.3f} + fence wait {S("wait"):.3f} + encode/other {S("host")-S("flush")-S("wait"):.3f}')
print(f'  GPU busy (sum of submit timestamps): {S("gpu"):.3f} s;  kernel submits {sum(c["submits"] for c in sel)}; chunks {sum(c["chunks"] for c in sel)}')
gs = [c for c in sel if c['gpu_first']]
if gs:
    span_ticks = max(c['gpu_last'] for c in gs) - min(c['gpu_first'] for c in gs)
    host_span = (max(c['host_start'] + c['host'] for c in gs) - min(c['host_start'] for c in gs)) / 1e9
    print(f'  GPU tick span {span_ticks/19.2e6:.3f} s at 19.2 MHz vs host span {host_span:.3f} s')
by_type = collections.defaultdict(lambda: [0, 0, 0, 0, 0])
names = {0: 'dispatch', 1: 'copy', 2: 'fill', 3: 'update', 4: 'image_copy', 5: 'image_clear'}
for c in sel:
    t = by_type[names.get(c['type'], 'type%d' % c['type'])]
    t[0] += 1; t[1] += c['host']; t[2] += c['gpu']; t[3] += c['submits']; t[4] += c['bytes']
for k, (n, h, g, s, b) in sorted(by_type.items(), key=lambda kv: -kv[1][1]):
    print(f'  {k:10s} n={n:5d} host {h/1e9:7.3f} s gpu {g/1e9:7.3f} s submits {s:6d} bytes {b}')
# host-side ncnn time outside the driver
ph = {k: (e.get(k, 0) - t0) / 1e9 for k in ('upload_submit', 'upload_done', 'final_submit', 'final_done')}
print('  phases (s from extract_begin):', ' '.join(f'{k}={v:.3f}' for k, v in ph.items()))

# join dispatches to ncnn layers by dispatch sequence
lay = [l for l in layers if t0 <= int(l['t']) <= t1]
seq_layer = {}
prev = e['seq0']
for l in lay:
    end = int(l['dispatch_end'])
    for s in range(prev, end): seq_layer[s] = l
    prev = max(prev, end)
rec = sum(int(l['record_ns']) for l in lay) / 1e9
print(f'  ncnn layer forward/record time (host, main thread): {rec:.3f} s over {len(lay)} layers')
agg = collections.defaultdict(lambda: [0, 0, 0, 0, 0, set()])
for c in disp:
    l = seq_layer.get(c['seq'])
    key = (l['type'] if l else '?', c['grid'], c['local'], c['instrlen'], c['regs'], c['barrier'], c['shared'])
    g = agg[key]; g[0] += 1; g[1] += c['host']; g[2] += c['gpu']; g[3] += c['submits']; g[4] += c['chunks']
    if l: g[5].add(l['name'])
tot_host = S('host')
print(f'\ntop dispatch groups by driver host time (layer type, grid, local, instrlen, regs full/half, barrier, shared):')
print(f'{"host s":>8} {"%":>5} {"gpu s":>7} {"n":>4} {"subm":>6} {"chunks":>6}  key')
for key, (n, h, g, s, ch, nm) in sorted(agg.items(), key=lambda kv: -kv[1][1])[:a.top]:
    print(f'{h/1e9:8.3f} {100*h/tot_host/1e9:5.1f} {g/1e9:7.3f} {n:4d} {s:6d} {ch:6d}  {key[0]} grid={key[1]} local={key[2]} il={key[3]} regs={key[4]} bar={key[5]} sh={key[6]}  e.g. {sorted(nm)[:2]}')
by_ltype = collections.defaultdict(lambda: [0, 0, 0])
for key, (n, h, g, s, ch, nm) in agg.items():
    t = by_ltype[key[0]]; t[0] += n; t[1] += h; t[2] += g
print('\nby ncnn layer type:')
for k, (n, h, g) in sorted(by_ltype.items(), key=lambda kv: -kv[1][1]):
    print(f'  {k:22s} dispatches {n:5d} host {h/1e9:7.3f} s gpu {g/1e9:7.3f} s')

if a.param:
    flops = {}
    for line in open(a.param):
        f = line.split()
        if len(f) < 6 or f[0] not in ('Convolution1D', 'Deconvolution', 'InnerProduct'): continue
        d = dict(x.split('=', 1) for x in f[6:] if '=' in x)
        hint = [int(v) for v in d.get('-23330', '0').split(',')[1:]]
        wsize = int(d.get('6', 0))
        if f[0] == 'Convolution1D' and hint: flops[f[1]] = 2 * wsize * hint[1]
        elif f[0] == 'Deconvolution' and hint: flops[f[1]] = 2 * wsize * hint[1] // int(d.get('3', 1))
    per = collections.defaultdict(lambda: [0, 0, ''])
    for c in disp:
        l = seq_layer.get(c['seq'])
        if not l: continue
        per[l['name']][0] += c['gpu']; per[l['name']][1] += 1; per[l['name']][2] = l['type']
    tot_gpu = sum(v[0] for v in per.values())
    print(f'\nper layer, top {a.top} by GPU time (GFLOP counts multiply-add as 2):')
    acc = [0, 0]
    for name, (g, n, t) in sorted(per.items(), key=lambda kv: -kv[1][0])[:a.top]:
        fl = flops.get(name)
        rate = f'{fl/g:6.2f} GFLOP/s' if fl and g else ''
        print(f'  {name:32s} {t:22s} gpu {g/1e9:6.3f} s ({100*g/tot_gpu:4.1f}%) dispatches {n:3d} {fl/1e9 if fl else 0:7.3f} GFLOP {rate}')
    cf = sum(flops.get(n, 0) for n in per); cg = sum(v[0] for n, v in per.items() if n in flops)
    print(f'  conv/deconv/linear total: {cf/1e9:.2f} GFLOP in {cg/1e9:.3f} s GPU = {cf/cg:.2f} GFLOP/s; everything else {(tot_gpu-cg)/1e9:.3f} s')

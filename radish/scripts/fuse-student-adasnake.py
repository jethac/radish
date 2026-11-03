#!/usr/bin/env python3
"""Fuse AdaIN (+ Snake) chains of a pnnx-converted Kokoro decoder core into
RadishAdaSnake layers (tests/ncnn/radish_ada_snake.cpp).

Matched chain (blob names from the ncnn .param):
  Reshape(x2d -> W,1,C) -> InstanceNorm(C, affine) -> Reshape(-> W,C) = n
  BinaryOp add(g, 1.0) = g1; BinaryOp mul(g1, n) = m; BinaryOp add(m, b) = y
  optional: Split(y) -> y1, y2; mul(alpha, y1); sin; square; mul(inv, .); add(y2, .)
The InstanceNorm becomes two MemoryData layers at its position (same weight
reads, so the .bin is unchanged); the fused layer replaces the last layer of
the chain; the other layers of the chain are dropped.

usage: fuse-student-adasnake.py IN.param OUT.param
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src).read().splitlines()
magic, header = lines[0], lines[1]
layers = []
for l in lines[2:]:
    f = l.split()
    if not f:
        continue
    t, name, ni, no = f[0], f[1], int(f[2]), int(f[3])
    ins, outs, params = f[4:4 + ni], f[4 + ni:4 + ni + no], f[4 + ni + no:]
    layers.append(dict(type=t, name=name, ins=ins, outs=outs, params=params))
producer = {b: i for i, L in enumerate(layers) for b in L['outs']}
consumers = {}
for i, L in enumerate(layers):
    for b in L['ins']:
        consumers.setdefault(b, []).append(i)


def only_consumer(blob, typ):
    c = consumers.get(blob, [])
    return c[0] if len(c) == 1 and layers[c[0]]['type'] == typ else None


def pv(L, key):
    for p in L['params']:
        if p.startswith(key + '='):
            return p.split('=', 1)[1]
    return None


drop, replace, fused = set(), {}, 0
for i, L in enumerate(layers):
    if L['type'] != 'InstanceNorm' or pv(L, '2') != '1' or int(pv(L, '0')) % 4:
        continue  # RadishAdaSnake needs pack4 (C % 4 == 0)
    r1 = producer.get(L['ins'][0])
    r2 = only_consumer(L['outs'][0], 'Reshape')
    if r1 is None or layers[r1]['type'] != 'Reshape' or r2 is None:
        continue
    n = layers[r2]['outs'][0]
    m = only_consumer(n, 'BinaryOp')
    if m is None or pv(layers[m], '0') != '2':
        continue
    g1 = [b for b in layers[m]['ins'] if b != n][0]
    a1 = producer.get(g1)
    if a1 is None or layers[a1]['type'] != 'BinaryOp' or pv(layers[a1], '1') != '1' or pv(layers[a1], '2') not in ('1.0', '1.000000e+00'):
        continue
    g = layers[a1]['ins'][0]
    ad = only_consumer(layers[m]['outs'][0], 'BinaryOp')
    if ad is None or pv(layers[ad], '0') != '0':
        continue
    b = [x for x in layers[ad]['ins'] if x != layers[m]['outs'][0]][0]
    y = layers[ad]['outs'][0]
    chain = [r1, i, r2, a1, m, ad]
    if len(consumers.get(g1, [])) != 1:
        continue
    out, snake, extra = y, 0, []
    sp = only_consumer(y, 'Split')
    if sp is not None and len(layers[sp]['outs']) == 2:
        y1, y2 = layers[sp]['outs']
        ma = only_consumer(y1, 'BinaryOp')
        ma = ma if ma is not None else only_consumer(y2, 'BinaryOp')
        if ma is not None and pv(layers[ma], '0') == '2':
            yin = y1 if y1 in layers[ma]['ins'] else y2
            yres = y2 if yin == y1 else y1
            alpha = [x for x in layers[ma]['ins'] if x != yin][0]
            sn = only_consumer(layers[ma]['outs'][0], 'UnaryOp')
            sq = only_consumer(layers[sn]['outs'][0], 'UnaryOp') if sn is not None else None
            mi = only_consumer(layers[sq]['outs'][0], 'BinaryOp') if sq is not None else None
            if (sn is not None and pv(layers[sn], '0') == '9' and sq is not None and pv(layers[sq], '0') == '4'
                    and mi is not None and pv(layers[mi], '0') == '2'):
                inv = [x for x in layers[mi]['ins'] if x != layers[sq]['outs'][0]][0]
                fa = only_consumer(layers[mi]['outs'][0], 'BinaryOp')
                if fa is not None and pv(layers[fa], '0') == '0' and yres in layers[fa]['ins']:
                    chain += [sp, ma, sn, sq, mi, fa]
                    out, snake, extra = layers[fa]['outs'][0], 1, [alpha, inv]
    C = int(pv(L, '0'))
    eps = pv(L, '1') or '1.000000e-5'
    x = layers[r1]['ins'][0]
    nw, nb = f'{L["name"]}_w', f'{L["name"]}_b'
    replace[i] = [f'MemoryData {L["name"]}_w 0 1 {nw} 0={C}', f'MemoryData {L["name"]}_b 0 1 {nb} 0={C}']
    last = chain[-1]
    ins = [x, g, b, nw, nb] + extra
    replace[last] = replace.get(last, []) + [
        f'RadishAdaSnake {L["name"]}_ada {len(ins)} 1 {" ".join(ins)} {out} 0={C} 1={snake} 2={eps}']
    drop.update(c for c in chain if c not in (i, last))
    fused += 1

outl = []
for i, L in enumerate(layers):
    if i in drop:
        continue
    if i in replace:
        outl += replace[i]
        continue
    outl.append(' '.join([L['type'], L['name'], str(len(L['ins'])), str(len(L['outs']))] + L['ins'] + L['outs'] + L['params']))
nl, nb_ = map(int, header.split())
open(dst, 'w').write(f'{magic}\n{len(outl)} {nb_ + 2 * fused}\n' + '\n'.join(outl) + '\n')
print(f'fused {fused} chains; layers {nl} -> {len(outl)}')

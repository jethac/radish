#!/usr/bin/env python3
"""Generate microbenchmark compute shaders into OUT_DIR as .comp and .spv,
and write OUT_DIR/jobs.txt for ubench (device paths under DEVICE_DIR)."""
import os, subprocess, sys
out, devdir = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
HDR = """#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : enable
layout(local_size_x_id = 0) in;
layout(push_constant) uniform P { uint iters; uint n; uint mask; uint threads; } p;
"""
UNROLL = 8
jobs = []
def emit(name, src, local, groups, kind, work, mib=0):
    path = f'{out}/{name}.comp'
    open(path, 'w').write(src)
    subprocess.run(['glslc', '-O', '--target-env=vulkan1.1', path, '-o', f'{out}/{name}.spv'], check=True)
    jobs.append(f'{name}-l{local} {devdir}/{name}.spv {local} {groups} {kind} {work} {mib}')

def alu(prec, width, chains, mode='const'):
    T = {('f32',1):'float',('f32',4):'vec4',('f16',1):'float16_t',('f16',2):'f16vec2',('f16',4):'f16vec4'}[(prec,width)]
    S = 'float' if prec == 'f32' else 'float16_t'
    init = '\n'.join(f'    {T} x{c} = {T}({S}(gid & 255u) * {S}(0.001) + {S}({c}.0 * 0.01));' for c in range(chains))
    if mode == 'add':
        body = '\n'.join(f'        x{c} = x{c} + b;' for _ in range(UNROLL) for c in range(chains))
    elif mode == 'mul':
        body = '\n'.join(f'        x{c} = x{c} * a;' for _ in range(UNROLL) for c in range(chains))
    elif mode == 'madaa':
        body = '\n'.join(f'        x{c} = fma(x{c}, a, a);' for _ in range(UNROLL) for c in range(chains))
    elif mode == 'muladd':
        body = '\n'.join(f'        {{ precise {T} t{c} = x{c} * a; x{c} = t{c} + b; }}' for _ in range(UNROLL) for c in range(chains))
    else:
        body = '\n'.join(f'        x{c} = fma(x{c}, a, b);' for _ in range(UNROLL) for c in range(chains))
    ab = (f'{T} a = {T}({S}(0.999)), b = {T}({S}(0.0001));' if mode == 'const' else
          f'{T} a = {T}({S}(o[0].x)), b = {T}({S}(o[0].y));')
    tot = ' + '.join(f'x{c}' for c in range(chains))
    return HDR + f"""layout(std430, binding = 0) buffer O {{ vec4 o[]; }};
void main() {{
    uint gid = gl_GlobalInvocationID.x;
    {ab}
{init}
    for (uint i = 0u; i < p.iters; i++) {{
{body}
    }}
    {T} s = {tot};
    o[gid] = vec4({'s' if width == 4 else f'vec{width}(s)' if width == 2 else 's'}{'' if width == 4 else ', 0.0, 0.0' if width == 2 else ', 0.0, 0.0, 0.0'});
}}
"""
# ALU: width x chains, fixed register sweep through chains
G = 1024
for prec, widths in (('f32', (1, 4)), ('f16', (1, 2, 4))):
    for w in widths:
        for c in (1, 2, 4, 8, 16):
            if w * c > 64: continue
            name = f'alu_{prec}_w{w}_c{c}'
            for local in (64, 128):
                emit(name, alu(prec, w, c), local, G, 'alu', UNROLL * c * w * 2)
# 2-source ops count 1 op per lane; reported GFLOP/s = G lane-ops/s
for mode in ('add', 'mul', 'madaa'):
    for c in (4, 8, 12):
        name = f'ops_{mode}_f32_w4_c{c}'
        emit(name, alu('f32', 4, c, mode), 128, 64, 'alu', UNROLL * c * 4 * (2 if mode == 'madaa' else 1))
for mode in ('reg', 'muladd'):
    for prec, w in (('f32', 1), ('f32', 4), ('f16', 4), ('f16', 2)):
        for c in (2, 4, 8):
            name = f'alu_{mode}_{prec}_w{w}_c{c}'
            for local in (64, 128):
                emit(name, alu(prec, w, c, mode), local, 64, 'alu', UNROLL * c * w * 2)
# occupancy / register sweep: register operands, vec4 chains, workgroup count
for c in (1, 2, 4, 8, 12):
    name = f'alu_reg_f32_w4_c{c}'
    if c in (1, 12):
        src = alu('f32', 4, c, 'reg')
        open(f'{out}/{name}.comp', 'w').write(src)
        subprocess.run(['glslc', '-O', '--target-env=vulkan1.1', f'{out}/{name}.comp', '-o', f'{out}/{name}.spv'], check=True)
    for local in (32, 64, 128):
        for groups in (1, 2, 4, 8, 16, 64, 256):
            jobs.append(f'occ_{name}-l{local}-g{groups} {devdir}/{name}.spv {local} {groups} alu {UNROLL*c*4*2}')
LOADS = """
    float s = 0.0;
    uint gid = gl_GlobalInvocationID.x;
    for (uint i = 0u; i < p.iters; i += 4u) {
        uint j = gid + i * p.threads;
        vec4 v0 = LOAD((j) & p.mask);
        vec4 v1 = LOAD((j + p.threads) & p.mask);
        vec4 v2 = LOAD((j + 2u * p.threads) & p.mask);
        vec4 v3 = LOAD((j + 3u * p.threads) & p.mask);
        s += dot(v0 + v1 + v2 + v3, vec4(1.0));
    }
    o[gid] = vec4(s);
"""
ssbo = HDR + """layout(std430, binding = 0) readonly buffer I { vec4 v[]; };
layout(std430, binding = 1) writeonly buffer O { vec4 o[]; };
#define LOAD(k) v[k]
void main() {""" + LOADS + "}\n"
ubo = HDR + """layout(std140, binding = 0) uniform U { vec4 u[1024]; };
layout(std430, binding = 1) writeonly buffer O { vec4 o[]; };
#define LOAD(k) u[(k) & 1023u]
void main() {""" + LOADS + "}\n"
texel = HDR + """layout(binding = 0) uniform samplerBuffer t;
layout(std430, binding = 1) writeonly buffer O { vec4 o[]; };
#define LOAD(k) texelFetch(t, int(k))
void main() {""" + LOADS + "}\n"
copy = HDR + """layout(std430, binding = 0) readonly buffer I { vec4 v[]; };
layout(std430, binding = 1) writeonly buffer O { vec4 w[]; };
void main() {
    uint gid = gl_GlobalInvocationID.x;
    for (uint i = 0u; i < p.iters; i++) {
        uint j = gid + i * p.threads;
        if (j < p.n) w[j] = v[j];
    }
}
"""
for local in (64, 128):
    for mib in (0.25, 4, 32):
        emit('bw_ssbo', ssbo, local, 1024, 'ssbo', 16, mib)
        emit('bw_texel', texel, local, 1024, 'texel', 16, min(mib, 16))
        emit('bw_texel16', texel, local, 1024, 'texel16', 8, min(mib, 16))
    emit('bw_ubo', ubo, local, 1024, 'ubo', 16, 0)
    emit('bw_copy', copy, local, 1024, 'copy', 32, 32)
open(f'{out}/jobs.txt', 'w').write('\n'.join(jobs) + '\n')
print(len(jobs), 'jobs')

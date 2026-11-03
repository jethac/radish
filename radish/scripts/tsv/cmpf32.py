#!/usr/bin/env python3
"""Compare two float32 files: max abs error, relative RMS, max |ref|."""
import sys, array, math
a = array.array('f'); a.frombytes(open(sys.argv[1], 'rb').read())
b = array.array('f'); b.frombytes(open(sys.argv[2], 'rb').read())
assert len(a) == len(b), (len(a), len(b))
se = sr = mx = mr = 0.0
for x, y in zip(a, b):
    d = x - y; se += d * d; sr += y * y; mx = max(mx, abs(d)); mr = max(mr, abs(y))
print(f'n={len(a)} max_abs={mx:.3e} rel_rms={math.sqrt(se / sr):.3e} max_ref={mr:.3e} identical={a == b}')

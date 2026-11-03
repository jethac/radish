#!/usr/bin/env python3
"""Reference for the Radish F(6,3) Winograd Conv1D: kernels split into 3-tap
chunks, dilation via residue-class tiles. Checks against direct correlation
y[co][x] = b[co] + sum_ci sum_j w[co][ci][j] * in[ci][x + j*D]."""
import random
from fractions import Fraction as F
BT = [[1,0,F(-21,4),0,F(21,4),0,-1,0],[0,1,1,F(-17,4),F(-17,4),1,1,0],[0,-1,1,F(17,4),F(-17,4),-1,1,0],
      [0,F(1,2),F(1,4),F(-5,2),F(-5,4),2,1,0],[0,F(-1,2),F(1,4),F(5,2),F(-5,4),-2,1,0],
      [0,2,4,F(-5,2),-5,F(1,2),1,0],[0,-2,4,F(5,2),-5,F(-1,2),1,0],[0,-1,0,F(21,4),0,F(-21,4),0,1]]
G = [[1,0,0],[F(-2,9)]*3,[F(-2,9),F(2,9),F(-2,9)],[F(1,90),F(1,45),F(2,45)],[F(1,90),F(-1,45),F(2,45)],
     [F(32,45),F(16,45),F(8,45)],[F(32,45),F(-16,45),F(8,45)],[0,0,1]]
AT = [[1,1,1,1,1,1,1,0],[0,1,-1,2,-2,F(1,2),F(-1,2),0],[0,1,1,4,4,F(1,4),F(1,4),0],
      [0,1,-1,8,-8,F(1,8),F(-1,8),0],[0,1,1,16,16,F(1,16),F(1,16),0],[0,1,-1,32,-32,F(1,32),F(-1,32),1]]
BT=[[float(v) for v in r] for r in BT]; G=[[float(v) for v in r] for r in G]; AT=[[float(v) for v in r] for r in AT]

def wino(inp, w, b, r, D, outw):
    cin, cout = len(inp), len(w); C = (r + 2) // 3
    tiles_per_res = -(-outw // (6 * D)); T = D * tiles_per_res
    Tv = T + (C // 2) * D
    win = len(inp[0])
    def at(ci, x): return inp[ci][x] if x < win else 0.0
    def start(t): return (t // D) * 6 * D + t % D
    # input transform V[o][i][ci][t]
    V = [[[[sum(BT[i][j] * at(ci, start(t) + 3*o*D + j*D) for j in range(8)) for t in range(Tv)]
           for ci in range(cin)] for i in range(8)] for o in range(2)]
    # weight transform U[i][co][c*cin+ci]
    def tap(co, ci, k): return w[co][ci][k] if k < r else 0.0
    U = [[[sum(G[i][j] * tap(co, ci, 3*c + j) for j in range(3)) for c in range(C) for ci in range(cin)]
          for co in range(cout)] for i in range(8)]
    # GEMM: M[i][co][t] = sum_{c,ci} U[i][co][c*cin+ci] * V[c%2][i][ci][t + (c//2)*D]
    M = [[[sum(U[i][co][c*cin+ci] * V[c % 2][i][ci][t + (c // 2) * D] for c in range(C) for ci in range(cin))
           for t in range(T)] for co in range(cout)] for i in range(8)]
    y = [[0.0] * outw for _ in range(cout)]
    for co in range(cout):
        for t in range(T):
            for j in range(6):
                x = start(t) + j * D
                if x < outw: y[co][x] = b[co] + sum(AT[j][i] * M[i][co][t] for i in range(8))
    return y

random.seed(1)
for r, D in ((3, 1), (7, 3), (11, 5), (11, 1), (7, 1)):
    cin, cout, outw = 3, 2, 41
    win = outw + (r - 1) * D
    inp = [[random.uniform(-1, 1) for _ in range(win)] for _ in range(cin)]
    w = [[[random.uniform(-1, 1) for _ in range(r)] for _ in range(cin)] for _ in range(cout)]
    b = [random.uniform(-1, 1) for _ in range(cout)]
    ref = [[b[co] + sum(w[co][ci][j] * inp[ci][x + j*D] for ci in range(cin) for j in range(r)) for x in range(outw)] for co in range(cout)]
    y = wino(inp, w, b, r, D, outw)
    err = max(abs(y[co][x] - ref[co][x]) for co in range(cout) for x in range(outw))
    print(f'r={r} D={D} max_abs_err={err:.2e}')

#!/usr/bin/env python3
"""凸多面体の Steiner の公式で offset の真値を出す。

    V(P (+) B_d) = V + A*d + M*d^2 + (4/3)*pi*d^3
    M = (1/2) * sum_e ( edge_length * exterior_dihedral_angle )

★ 凸であることが前提 (geodesic 球・box はどちらも凸)。
"""
import sys, math
from collections import defaultdict

def read_off(path):
    with open(path) as f:
        toks = []
        for line in f:
            line = line.split('#')[0].strip()
            if line: toks.append(line)
    assert toks[0].startswith('OFF')
    if toks[0] == 'OFF': nv, nf, _ = map(int, toks[1].split()); rest = toks[2:]
    else: nv, nf, _ = map(int, toks[0][3:].split()); rest = toks[1:]
    V = [tuple(map(float, rest[i].split()[:3])) for i in range(nv)]
    F = []
    for i in range(nf):
        p = list(map(int, rest[nv+i].split()))
        F.append(p[1:1+p[0]])
    return V, F

def sub(a,b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def dot(a,b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def norm(a): return math.sqrt(dot(a,a))

def measures(V, F):
    """(volume, area, mean-width項 M) を返す。三角形面のみ想定。"""
    vol = 0.0; area = 0.0
    normals = []
    for f in F:
        assert len(f) == 3, "三角形面のみ対応"
        a,b,c = (V[i] for i in f)
        n = cross(sub(b,a), sub(c,a))
        ln = norm(n)
        area += 0.5*ln
        vol  += dot(a, cross(b,c)) / 6.0
        normals.append((n[0]/ln, n[1]/ln, n[2]/ln))
    # 稜: 各無向辺を共有する 2 面の法線から外部二面角を出す
    edges = defaultdict(list)
    for fi, f in enumerate(F):
        for k in range(3):
            u, v = f[k], f[(k+1)%3]
            edges[(min(u,v), max(u,v))].append(fi)
    M = 0.0
    for (u,v), fs in edges.items():
        assert len(fs) == 2, "閉じた 2-多様体でない稜がある: %s -> %d 面" % ((u,v), len(fs))
        n1, n2 = normals[fs[0]], normals[fs[1]]
        c = max(-1.0, min(1.0, dot(n1,n2)))
        theta = math.acos(c)            # 法線どうしのなす角 = 外部二面角 (凸なら)
        M += norm(sub(V[u], V[v])) * theta
    return vol, area, 0.5*M

def steiner(vol, area, M, d):
    return vol + area*d + M*d*d + (4.0/3.0)*math.pi*d**3

if __name__ == "__main__":
    path = sys.argv[1]
    ds = [float(x) for x in sys.argv[2:]] or [0.1, 0.2]
    V, F = read_off(path)
    vol, area, M = measures(V, F)
    print("# %s: %d verts %d faces" % (path, len(V), len(F)))
    print("V= %.15f  A= %.15f  M= %.15f" % (vol, area, M))
    for d in ds:
        print("d= %-6g  true= %.15f" % (d, steiner(vol, area, M, d)))

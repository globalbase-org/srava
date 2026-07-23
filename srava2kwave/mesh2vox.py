#!/usr/bin/env python3
"""mesh2vox.py — srava が出した OFF メッシュを Cartesian 格子にボクセル化し、中立な vox.h5 を書く。

vox.h5（srava と k-Wave の間の中立フォーマット）:
  attrs: format="srava-vox", version="1"
  datasets: Nx,Ny,Nz(int64), dx,dy,dz(float64), origin(float64[3]=格子原点=セル(0,0,0)中心)
  group "masks/<name>": uint8[Nx,Ny,Nz]  1=その領域に属する

領域 = メッシュ + side("inside"|"outside")。空気が内か外かは side で選ぶ。
ボクセル化は watertight 三角メッシュを前提に、各 (ix,iy) 列で +z レイの交差 z を集め、
ソートしてペア区間（内部）を塗る z-パリティ法（numpy）。CGAL 不要・追加依存なし。

使い方:
  python3 mesh2vox.py coil.off vox.h5 --dx 1.0 --pad 8 --name air --side inside
"""
import sys, argparse
import numpy as np
import h5py


def read_off(path):
    with open(path) as f:
        toks = f.read().split()
    i = 0
    assert toks[i].upper().startswith("OFF"); i += 1
    nv, nf = int(toks[i]), int(toks[i+1]); i += 3  # skip ne
    V = np.array(toks[i:i+3*nv], dtype=np.float64).reshape(nv, 3); i += 3*nv
    F = []
    for _ in range(nf):
        k = int(toks[i]); i += 1
        idx = list(map(int, toks[i:i+k])); i += k
        for t in range(1, k-1):           # 扇状三角形分割
            F.append((idx[0], idx[t], idx[t+1]))
    return V, np.array(F, dtype=np.int64)


def voxelize(V, F, dx, pad):
    """三角メッシュ → (mask_inside[Nx,Ny,Nz] bool, origin[3], (Nx,Ny,Nz))。"""
    lo = V.min(0) - pad*dx
    hi = V.max(0) + pad*dx
    Nx, Ny, Nz = (np.ceil((hi-lo)/dx).astype(int) + 1)
    origin = lo                                   # セル(i)の中心 = origin + i*dx
    xs = origin[0] + np.arange(Nx)*dx
    ys = origin[1] + np.arange(Ny)*dx
    # 各列 (ix,iy) の交差 z を集める
    cross = [[] for _ in range(Nx*Ny)]
    A = V[F[:,0]]; B = V[F[:,1]]; C = V[F[:,2]]
    for a, b, c in zip(A, B, C):
        # 三角形の xy 投影 bbox → 候補列レンジ
        minx, maxx = min(a[0],b[0],c[0]), max(a[0],b[0],c[0])
        miny, maxy = min(a[1],b[1],c[1]), max(a[1],b[1],c[1])
        ix0 = max(0, int(np.ceil((minx-origin[0])/dx)));  ix1 = min(Nx-1, int(np.floor((maxx-origin[0])/dx)))
        iy0 = max(0, int(np.ceil((miny-origin[1])/dx)));  iy1 = min(Ny-1, int(np.floor((maxy-origin[1])/dx)))
        if ix0 > ix1 or iy0 > iy1:
            continue
        # 重心座標（xy）: det
        d = (b[1]-c[1])*(a[0]-c[0]) + (c[0]-b[0])*(a[1]-c[1])
        if abs(d) < 1e-18:
            continue
        gx = xs[ix0:ix1+1]
        gy = ys[iy0:iy1+1]
        PX, PY = np.meshgrid(gx, gy, indexing='ij')    # [nx,ny]
        l1 = ((b[1]-c[1])*(PX-c[0]) + (c[0]-b[0])*(PY-c[1])) / d
        l2 = ((c[1]-a[1])*(PX-c[0]) + (a[0]-c[0])*(PY-c[1])) / d
        l3 = 1.0 - l1 - l2
        inside = (l1 >= 0) & (l2 >= 0) & (l3 >= 0)
        if not inside.any():
            continue
        Z = l1*a[2] + l2*b[2] + l3*c[2]
        ii, jj = np.where(inside)
        for k in range(ii.size):
            ix = ix0 + ii[k]; iy = iy0 + jj[k]
            cross[iy*Nx + ix].append(Z[ii[k], jj[k]])
    # 列ごとに内部 z 区間を塗る
    mask = np.zeros((Nx, Ny, Nz), dtype=bool)
    zc = origin[2] + np.arange(Nz)*dx
    for iy in range(Ny):
        for ix in range(Nx):
            zs = cross[iy*Nx + ix]
            if len(zs) < 2:
                continue
            zs.sort()
            for k in range(0, len(zs)-1, 2):
                sel = (zc >= zs[k]) & (zc <= zs[k+1])
                if sel.any():
                    mask[ix, iy, sel] = True
    return mask, origin, (int(Nx), int(Ny), int(Nz))


def write_vox(path, masks, origin, dims, dx):
    Nx, Ny, Nz = dims
    with h5py.File(path, "w") as f:
        f.attrs["format"] = "srava-vox"
        f.attrs["version"] = "1"
        f.create_dataset("Nx", data=np.int64(Nx)); f.create_dataset("Ny", data=np.int64(Ny)); f.create_dataset("Nz", data=np.int64(Nz))
        f.create_dataset("dx", data=np.float64(dx)); f.create_dataset("dy", data=np.float64(dx)); f.create_dataset("dz", data=np.float64(dx))
        f.create_dataset("origin", data=np.asarray(origin, dtype=np.float64))
        g = f.create_group("masks")
        for name, m in masks.items():
            g.create_dataset(name, data=m.astype(np.uint8), compression="gzip")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("off"); ap.add_argument("out")
    ap.add_argument("--dx", type=float, required=True)
    ap.add_argument("--pad", type=int, default=8, help="bbox 外側に足すボクセル数（PML 余白用）")
    ap.add_argument("--name", default="air")
    ap.add_argument("--side", choices=["inside", "outside"], default="inside")
    a = ap.parse_args()
    V, F = read_off(a.off)
    inside, origin, dims = voxelize(V, F, a.dx, a.pad)
    mask = inside if a.side == "inside" else ~inside
    write_vox(a.out, {a.name: mask}, origin, dims, a.dx)
    Nx, Ny, Nz = dims
    print(f"[mesh2vox] grid {Nx}x{Ny}x{Nz} dx={a.dx} origin={origin}  "
          f"region '{a.name}' (side={a.side}) voxels={int(mask.sum())} -> {a.out}")


if __name__ == "__main__":
    main()

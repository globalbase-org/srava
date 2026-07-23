#!/usr/bin/env python3
"""vox2input.py — 中立 vox.h5（srava 由来の格子＋領域マスク）から k-Wave C++ の input.h5 を組む。

ここに k-Wave 固有の知識を集約（インタフェース変化はここで吸収）。
- 材料割当: マスク名 → (c0, rho0)。既定 air=(343,1.2) / それ以外=wall=(1500,1000) 強反射。
- dt は CFL から自動（dt = cfl·dx/c_max）。Nt は引数。
- input.h5 のスキーマは make_input.py と同一（スカラ属性 data_type/domain_type 等）。
- 断面 PNG（xy@mid-z, xz@mid-y, yz@mid-x）で c0 を確認用に書き出す。

使い方:
  python3 vox2input.py vox.h5 input.h5 --nt 300 --png-prefix /tmp/kw/medium
"""
import sys, argparse, datetime
import numpy as np
import h5py
from pngio import write_png, colorize


def build_input(vox_path, out_path, materials, nt, cfl, pml, png_prefix):
    with h5py.File(vox_path, "r") as v:
        Nx, Ny, Nz = int(v["Nx"][()]), int(v["Ny"][()]), int(v["Nz"][()])
        dx = float(v["dx"][()]); dy = float(v["dy"][()]); dz = float(v["dz"][()])
        masks = {k: v["masks"][k][()].astype(bool) for k in v["masks"].keys()}

    # ---- 材料割当: 既定は wall、マスク順に上書き（後勝ち）----
    c0   = np.full((Nx, Ny, Nz), materials["__default__"][0], dtype=np.float32)
    rho0 = np.full((Nx, Ny, Nz), materials["__default__"][1], dtype=np.float32)
    for name, m in masks.items():
        if name not in materials:
            continue
        c, rho = materials[name]
        c0[m] = c; rho0[m] = rho
    c_max = float(c0.max())
    dt = cfl * dx / c_max

    # ---- 交互格子（staggered）密度: 隣接平均（界面で適正化）----
    def stagger(a, axis):
        s = a.copy()
        sl0 = [slice(None)]*3; sl1 = [slice(None)]*3
        sl0[axis] = slice(0, -1); sl1[axis] = slice(1, None)
        s[tuple(sl0)] = 0.5*(a[tuple(sl0)] + a[tuple(sl1)])
        return s.astype(np.float32)

    with h5py.File(out_path, "w") as f:
        sdt = h5py.string_dtype(encoding="ascii", length=32)
        for k, val in [("file_type","input"),("major_version","1"),("minor_version","2"),
                       ("created_by","srava2kwave/vox2input.py"),
                       ("creation_date", datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")),
                       ("file_description","srava voxel medium")]:
            f.attrs.create(k, val, dtype=sdt)

        def ds_index(name, value):
            d = f.create_dataset(name, data=np.array([[[value]]], dtype=np.uint64))
            d.attrs.create("data_type","long",dtype=sdt); d.attrs.create("domain_type","real",dtype=sdt)
        def ds_float(name, value):
            d = f.create_dataset(name, data=np.array([[[value]]], dtype=np.float32))
            d.attrs.create("data_type","float",dtype=sdt); d.attrs.create("domain_type","real",dtype=sdt)
        def ds_idx_arr(name, arr):
            d = f.create_dataset(name, data=arr.astype(np.uint64))
            d.attrs.create("data_type","long",dtype=sdt); d.attrs.create("domain_type","real",dtype=sdt)
        def ds_flt_arr(name, arr):
            d = f.create_dataset(name, data=arr.astype(np.float32))
            d.attrs.create("data_type","float",dtype=sdt); d.attrs.create("domain_type","real",dtype=sdt)

        for n, val in [("Nx",Nx),("Ny",Ny),("Nz",Nz),("Nt",nt)]: ds_index(n, val)
        for n, val in [("dx",dx),("dy",dy),("dz",dz),("dt",dt),("c_ref",c_max),
                       ("alpha_coeff",0.0),("alpha_power",1.0),("BonA",0.0)]: ds_float(n, val)
        ds_idx_arr("t_index", np.arange(1, nt+1, dtype=np.uint64).reshape(1, nt))

        pml_z = pml if Nz > 1 else 0
        for n, val in [("pml_x_size",pml),("pml_y_size",pml),("pml_z_size",pml_z)]: ds_index(n, val)
        for n, val in [("pml_x_alpha",2.0),("pml_y_alpha",2.0),("pml_z_alpha",2.0 if Nz>1 else 0.0)]: ds_float(n, val)
        for fl in ["absorbing_flag","nonlinear_flag","axisymmetric_flag","nonuniform_grid_flag",
                   "p_source_flag","ux_source_flag","uy_source_flag","uz_source_flag",
                   "p0_source_flag","transducer_source_flag"]: ds_index(fl, 0)
        ds_index("sensor_mask_type", 0)

        ds_flt_arr("c0", c0); ds_flt_arr("rho0", rho0)
        ds_flt_arr("rho0_sgx", stagger(rho0, 0)); ds_flt_arr("rho0_sgy", stagger(rho0, 1))
        if Nz > 1:
            ds_flt_arr("rho0_sgz", stagger(rho0, 2))

        sm = np.zeros((Nx, Ny, Nz), dtype=np.uint64); sm[Nx//2, Ny//2, Nz//2] = 1
        ds_idx_arr("sensor_mask", sm); ds_idx_arr("sensor_mask_index", np.array([[[1]]], dtype=np.uint64))

    print(f"[vox2input] grid {Nx}x{Ny}x{Nz} dx={dx} dt={dt:.3e} Nt={nt} c_max={c_max} -> {out_path}")

    if png_prefix:
        for tag, sl in [("xy", c0[:, :, Nz//2].T), ("xz", c0[:, Ny//2, :].T), ("yz", c0[Nx//2, :, :].T)]:
            write_png(f"{png_prefix}_{tag}.png", colorize(sl, vmin=c0.min(), vmax=c0.max()))
        print(f"[vox2input] cross-sections -> {png_prefix}_{{xy,xz,yz}}.png")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vox"); ap.add_argument("out")
    ap.add_argument("--air", type=float, nargs=2, default=[343.0, 1.2], metavar=("C","RHO"))
    ap.add_argument("--wall", type=float, nargs=2, default=[1500.0, 1000.0], metavar=("C","RHO"))
    ap.add_argument("--nt", type=int, default=300)
    ap.add_argument("--cfl", type=float, default=0.3)
    ap.add_argument("--pml", type=int, default=8)
    ap.add_argument("--png-prefix", default=None)
    a = ap.parse_args()
    materials = {"__default__": tuple(a.wall), "air": tuple(a.air), "wall": tuple(a.wall)}
    build_input(a.vox, a.out, materials, a.nt, a.cfl, a.pml, a.png_prefix)


if __name__ == "__main__":
    main()

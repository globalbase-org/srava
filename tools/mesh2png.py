#!/usr/bin/env python3
"""mesh2png.py — STL/OFF を numpy だけでソフトレンダリングして PNG にする(OpenGL/表示サーバ不要)。

ヘッドレスな Linux でメッシュの「見た目」を画像で確認するための簡易ビューア。
z-buffer + Lambert シェーディングの素朴なラスタライザ。依存は numpy のみ(PNG は zlib/stdlib)。

使い方:
  python3 mesh2png.py model.stl out.png                 # 既定=4面グリッド(iso/front/top/side)
  python3 mesh2png.py model.off out.png --size 1000      # 1パネル解像度(px)
  python3 mesh2png.py model.stl out.png --view iso       # 単一アングル(iso/front/top/side/right)
  python3 mesh2png.py model.stl out.png --view 30,20      # 単一アングル(方位az,仰角el 度)
グリッド配置: 左上=iso / 右上=front(+X) / 左下=top(+Z) / 右下=side(+Y)。
"""
import sys, struct, zlib, argparse
import numpy as np


def write_png(path, rgb):
    rgb = np.ascontiguousarray(rgb.astype(np.uint8)); H, W, _ = rgb.shape
    raw = np.concatenate([np.zeros((H, 1), np.uint8), rgb.reshape(H, W * 3)], 1).tobytes()
    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def load_stl(path):
    data = open(path, "rb").read()
    head = data[:512].lower()
    if data[:5].lower() == b"solid" and b"facet" in head:           # ASCII STL
        verts = []
        for line in data.decode("ascii", "replace").splitlines():
            s = line.split()
            if len(s) >= 4 and s[0] == "vertex":
                verts.append([float(s[1]), float(s[2]), float(s[3])])
        return np.array(verts, np.float64).reshape(-1, 3, 3)
    n = struct.unpack("<I", data[80:84])[0]                          # binary STL
    tri = np.frombuffer(data[84:84 + 50 * n], np.uint8).reshape(n, 50)
    return tri[:, 12:48].copy().view("<f4").reshape(n, 3, 3).astype(np.float64)


def load_off(path):
    toks = open(path).read().split()
    i = 0
    if not toks[i].upper().startswith("OFF"):
        raise ValueError("not an OFF file")
    # 先頭が "OFF" 単独 or "OFF<counts>"。counts は次の3トークン。
    i = 1 if toks[0].upper() == "OFF" else 0
    if i == 0:
        toks[0] = toks[0][3:]
    nv, nf = int(toks[i]), int(toks[i + 1]); i += 3
    V = np.array(toks[i:i + 3 * nv], np.float64).reshape(nv, 3); i += 3 * nv
    faces = []
    for _ in range(nf):
        k = int(toks[i]); idx = list(map(int, toks[i + 1:i + 1 + k])); i += 1 + k
        for j in range(1, k - 1):
            faces.append([idx[0], idx[j], idx[j + 1]])               # 扇状三角形分割
    return V[np.array(faces, int)]


def load_mesh(path):
    return load_off(path) if path.lower().endswith(".off") else load_stl(path)


def render(tris, size, az, el, base=(0.60, 0.65, 0.74), bg=(26, 28, 32)):
    az = np.radians(az); el = np.radians(el)
    fwd = np.array([np.cos(el) * np.cos(az), np.cos(el) * np.sin(az), np.sin(el)])  # 物体→カメラ
    up = np.array([0, 0, 1.0])
    right = np.cross(up, fwd)
    if np.linalg.norm(right) < 1e-6:
        right = np.array([1.0, 0, 0])
    right /= np.linalg.norm(right); upv = np.cross(fwd, right)

    c = tris.reshape(-1, 3).mean(0)
    Q = tris - c
    X = Q @ right; Y = Q @ upv; Z = Q @ fwd                          # 画面x,y と 深さz(大=手前)
    mnx, mxx, mny, mxy = X.min(), X.max(), Y.min(), Y.max()
    span = max(mxx - mnx, mxy - mny) * 1.12 + 1e-9
    sc = (size - 1) / span
    sx = (X - (mnx + mxx) / 2) * sc + size / 2
    sy = size / 2 - (Y - (mny + mxy) / 2) * sc

    e1 = tris[:, 1] - tris[:, 0]; e2 = tris[:, 2] - tris[:, 0]
    nrm = np.cross(e1, e2); ln = np.linalg.norm(nrm, axis=1, keepdims=True); ln[ln == 0] = 1; nrm /= ln
    lw = np.array([0.35, 0.25, 0.90]); lw /= np.linalg.norm(lw)
    shade = 0.25 + 0.75 * np.abs(nrm @ lw)                           # 二面 Lambert
    col = np.clip(np.array(base)[None, :] * shade[:, None] * 255, 0, 255)

    img = np.empty((size, size, 3), np.float64); img[:] = np.array(bg)
    zbuf = np.full((size, size), -1e30)
    for t in np.argsort(Z.mean(1)):                                 # 奥から
        xs, ys, zs = sx[t], sy[t], Z[t]
        x0, x1 = int(np.floor(xs.min())), int(np.ceil(xs.max()))
        y0, y1 = int(np.floor(ys.min())), int(np.ceil(ys.max()))
        x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, size - 1), min(y1, size - 1)
        if x1 < x0 or y1 < y0:
            continue
        d = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(d) < 1e-9:
            continue
        xx, yy = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        a = ((ys[1] - ys[2]) * (xx - xs[2]) + (xs[2] - xs[1]) * (yy - ys[2])) / d
        b = ((ys[2] - ys[0]) * (xx - xs[2]) + (xs[0] - xs[2]) * (yy - ys[2])) / d
        g = 1 - a - b
        inside = (a >= 0) & (b >= 0) & (g >= 0)
        if not inside.any():
            continue
        zpix = a * zs[0] + b * zs[1] + g * zs[2]
        zsub = zbuf[y0:y1 + 1, x0:x1 + 1]
        win = inside & (zpix > zsub)
        if not win.any():
            continue
        zsub[win] = zpix[win]
        img[y0:y1 + 1, x0:x1 + 1][win] = col[t]
    return img.astype(np.uint8)


VIEWS = {"iso": (-50, 28), "front": (0, 0), "right": (90, 0), "side": (90, 0), "top": (0, 90)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mesh"); ap.add_argument("out")
    ap.add_argument("--size", type=int, default=520, help="1パネルの解像度(px)")
    ap.add_argument("--view", default=None, help="単一アングル: iso/front/top/side/right または az,el")
    a = ap.parse_args()
    tris = load_mesh(a.mesh)
    if tris is None or len(tris) == 0:
        sys.exit("mesh2png: 三角形が読めません: " + a.mesh)

    if a.view is None:                                              # 4面グリッド
        S = a.size; sep = 4; bgc = np.array([60, 63, 68])
        panels = [render(tris, S, *VIEWS[v]) for v in ("iso", "front", "top", "side")]
        H = W = S * 2 + sep * 3
        canvas = np.empty((H, W, 3), np.uint8); canvas[:] = bgc
        pos = [(sep, sep), (sep, sep * 2 + S), (sep * 2 + S, sep), (sep * 2 + S, sep * 2 + S)]
        for (y, x), p in zip(pos, panels):
            canvas[y:y + S, x:x + S] = p
        write_png(a.out, canvas)
        print("mesh2png: %d tris → %s (TL=iso TR=front BL=top BR=side)" % (len(tris), a.out))
    else:
        if "," in a.view:
            az, el = (float(t) for t in a.view.split(","))
        else:
            az, el = VIEWS[a.view]
        write_png(a.out, render(tris, a.size if a.size > 200 else 900, az, el))
        print("mesh2png: %d tris → %s (az=%g el=%g)" % (len(tris), a.out, az, el))


if __name__ == "__main__":
    main()

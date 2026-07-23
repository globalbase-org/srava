"""pngio.py — 依存なしの最小 PNG 書き出し（stdlib zlib のみ）。matplotlib 不要。"""
import struct, zlib
import numpy as np


def write_png(path, rgb):
    """rgb: uint8 [H,W,3] を PNG で書く。"""
    rgb = np.ascontiguousarray(rgb.astype(np.uint8))
    H, W, _ = rgb.shape
    # 各行頭にフィルタバイト0
    raw = np.concatenate([np.zeros((H, 1), np.uint8), rgb.reshape(H, W*3)], axis=1).tobytes()

    def chunk(typ, data):
        c = typ + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)   # 8bit, color=2(RGB)
    idat = zlib.compress(raw, 6)
    with open(path, "wb") as f:
        f.write(sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b""))


def colorize(field, vmin=None, vmax=None):
    """2D float 配列 → RGB（簡易 viridis 風グレー→色）。NaN/一定値も安全。"""
    f = np.asarray(field, dtype=np.float64)
    if vmin is None: vmin = np.nanmin(f)
    if vmax is None: vmax = np.nanmax(f)
    if vmax <= vmin: vmax = vmin + 1.0
    t = np.clip((f - vmin)/(vmax - vmin), 0, 1)
    # シンプルな青→緑→黄→赤
    r = np.clip(1.5*t - 0.5, 0, 1)
    g = np.clip(1.5 - np.abs(2*t-1)*1.5, 0, 1)
    b = np.clip(1.0 - 1.5*t, 0, 1)
    return (np.stack([r, g, b], -1)*255).astype(np.uint8)

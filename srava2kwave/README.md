# srava2kwave（サンプル）

srava の `export_vox` が出す中立フォーマット **`vox.h5`** を、格子音響ソルバ
**[k-Wave](http://www.k-wave.org/)**（kspaceFirstOrder-CUDA/OMP）の入力 `input.h5` へ
変換するための Python ツール群。連携の流れは `docs/srava_kwave.md` 参照。

```
  export_vox(...) ──vox.h5──▶ vox2input.py ──input.h5──▶ kspaceFirstOrder-CUDA ──▶ output.h5
```

| ファイル | 役割 |
|---|---|
| `mesh2vox.py`  | OFF メッシュ → `vox.h5`（`export_vox` の Python 等価。srava を経由しない経路） |
| `vox2input.py` | `vox.h5` → `input.h5`（マスク→材料・dt/PML・断面 PNG）。**k-Wave 固有の知識はここに集約** |
| `pngio.py`     | 自前 PNG 出力ヘルパ（numpy + zlib のみ） |

依存は **numpy + h5py のみ**（voxelize も PNG も自前 = trimesh/scipy/matplotlib 不要）。

## 位置づけ（重要）

- これは**サンプル**であり、ここに同梱した版が手元の k-Wave インストールでそのまま動くことは
  **保証しない**。`input.h5` は k-Wave C++ の簡略/暫定スキーマで、本家の完全フォーマットや
  バージョン差はここを編集して吸収する前提。
- cmake では `share/srava/srava2kwave/` にコピーされるだけ（ビルド対象ではない）。
  自分の作業ディレクトリへコピーして自由に書き換えて使う。

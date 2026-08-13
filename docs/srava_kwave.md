# srava → k-Wave 音響シミュレーション

srava で作った形状（管・コイル・空洞など）を、格子ベースの音響ソルバ **[k-Wave](http://www.k-wave.org/)**
（時間領域・擬スペクトル法）で解くための連携ガイド。

形状は srava、格子ソルバ固有の設定（媒質値・音源・センサ・時間）は Python に分離し、間に**中立フォーマット
`vox.h5`** を挟む。これにより k-Wave のインタフェース変更は Python 側だけで吸収できる。

```
  srava                         python (srava2kwave/)              solver
  ─────                         ────────────────────              ──────
  export_vox(...)  ──vox.h5──▶  vox2input.py  ──input.h5──▶  kspaceFirstOrder-CUDA  ──▶ output.h5
   形状を voxel 化               マスク→材料/音源/                                        + 断面 PNG（確認用）
   名前付きマスク                センサ・dt/PML・k-Wave スキーマ
```

---

## 1. srava: 形状を voxel 化する — `export_vox`

複数の領域メッシュを共通の Cartesian 格子へボクセル化し、各領域を名前付きマスクとして `vox.h5` に書く。

```
export_vox("vox.h5",
   { dx: 1.0, pad: 8,
     regions: [ {name:"air",    side:"inside"},
                {name:"wall",   side:"outside"},
                {name:"source", side:"inside"},
                {name:"sensor", side:"inside"} ] },
   air_mesh, wall_mesh, source_mesh, sensor_mesh);   // メッシュは可変個の位置引数（regions と同順）
```

- **領域 = メッシュ + `side`**。`side:"inside"` はメッシュ内部、`"outside"` は外部を 1 とする。
  「空気が管の内か外か」を `side` で選べる（中空管の内腔モデルでも、壁構造モデルでも対応）。
- 格子は全メッシュの**共通 bounding box + `pad`·`dx`** をピッチ `dx` で刻む（`pad` は PML 用の余白）。
- **メッシュは位置引数**（`combine` と同様）。srava ではメッシュ（cache）と値（ハッシュ）が別経路で渡るため、
  `regions` ハッシュの中にメッシュは入れられない。`regions[i]` が `i` 番目のメッシュに対応する。
- 空気・壁だけでなく、**抵抗媒体・音源位置・センサ位置も別メッシュ（名前付き領域）として焼ける**。
  値（音速・密度・吸収・音源波形）の割り当ては Python 側（次節）で行う。

### `vox.h5`（中立フォーマット）

| 要素 | 内容 |
|---|---|
| `Nx, Ny, Nz` | 格子寸法（int64） |
| `dx, dy, dz` | ピッチ（float64） |
| `origin` | セル(0,0,0) 中心の座標（float64[3]） |
| `masks/<name>` | 各領域の占有マスク（uint8[Nx,Ny,Nz]・1=その領域） |
| 属性 `format` | `"srava-vox"` |

> srava を経由しない外部メッシュ（OFF）からも `python3 srava2kwave/mesh2vox.py mesh.off vox.h5 --dx 1.0 --name air --side inside`
> で同じ `vox.h5` を作れる（`export_vox` の Python 等価）。

---

## 2. Python: `vox.h5` → `input.h5`（材料・音源・時間）— `vox2input.py`

k-Wave C++ ソルバの入力 HDF5（暫定/簡略スキーマ）を組む。**k-Wave 固有の知識はここに集約**。

```
python3 srava2kwave/vox2input.py vox.h5 input.h5 \
    --air 343 1.2 --wall 1500 1000 \
    --nt 300 --cfl 0.3 --pml 8 \
    --png-prefix medium
```

| オプション | 意味 | 既定 |
|---|---|---|
| `--air C RHO` | `air` マスクの音速[m/s]・密度[kg/m³] | `343 1.2` |
| `--wall C RHO` | `air` 以外（壁）の音速・密度 | `1500 1000` |
| `--nt` | 時間ステップ数 | `300` |
| `--cfl` | CFL 数（`dt = cfl·dx/c_max` を自動計算） | `0.3` |
| `--pml` | PML 層厚（ボクセル） | `8` |
| `--png-prefix P` | 媒質 `c0` の断面 PNG `P_{xy,xz,yz}.png` を出す（確認用・3D は直交断面） | なし |

- マスク → 媒質配列 `c0`/`rho0`（+ 交互格子 `rho0_sgx/sgy/sgz` を隣接平均で生成）。
- `dt` は CFL 条件から自動。`c_ref` は最大音速。
- **材料・音源・センサの割り当てはこのスクリプトを編集して拡張**する（マスク名 → 物性のマップ。
  抵抗媒体の吸収 `alpha_coeff`、`source`/`sensor` マスクの利用など）。k-Wave のスキーマ変更もここで吸収。

---

## 3. ソルバ実行

```
kspaceFirstOrder-CUDA -i input.h5 -o output.h5      # GPU 版
# あるいは kspaceFirstOrder-OMP -i input.h5 -o output.h5   # CPU 版
```

`output.h5` に時系列の圧力場（センサ出力等）が入る。可視化は Python（h5py + `srava2kwave/pngio.py`）で。

---

## ツールの所在と依存

- srava: `export_vox` は **`cgal.so` モジュールが提供**（`-DSRAVA_MODULE_CGAL=ON`・既定 ON）。
  加えてビルドに **HDF5 が必要**（`find_package(HDF5)`）。→ [モジュールリファレンス §cgal.so](srava_module_reference.html#cgal)
- Python: `srava2kwave/{mesh2vox.py, vox2input.py, pngio.py}`。依存は **numpy + h5py のみ**
  （voxelize も PNG も自前 = trimesh/scipy/matplotlib 不要）。リポジトリの `srava2kwave/` に同梱、
  `cmake --install` で `$PREFIX/share/srava/srava2kwave/` へコピーされる（**サンプル**であり、手元の
  k-Wave でそのまま動くことは保証しない。自分の作業ディレクトリへコピーして書き換えて使う）。
- ソルバ: k-Wave C++（kspaceFirstOrder-CUDA/OMP）。

## 注意

- `input.h5` は k-Wave C++ の**簡略/暫定スキーマ**（最小構成）。本家 k-Wave の完全フォーマットとは差異があり、
  source/sensor/吸収/非線形の本格設定は `vox2input.py` を拡張して足す。
- 格子は `dx` で決まる。波長 `λ = c/f` に対し **1 波長あたり数ボクセル以上**（目安 `dx ≤ λ/4` 程度）が必要。
  高周波ほど細かい格子＝大きい `Nx·Ny·Nz` になるので、`dx` と対象周波数のバランスに注意。

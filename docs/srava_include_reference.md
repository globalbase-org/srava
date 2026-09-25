---
title: srava インクルードライブラリ
---

# srava インクルードライブラリ

`include "std/…";` で取り込む **srava 自身で書かれたライブラリ**の一覧。`<prefix>/share/srava/lib/std/*.sra`
に**平文で置かれている**ので、動きを確かめたいときはそのまま読める。

> ★ ここに載っているのは **op ではない**。`srava --module-info` や
> [op × モジュール表](srava_function_reference.html#module-matrix) には出てこない。
> 必要なのは `module()` ではなく **`include`** で、`.so` は関係しない。
> 各関数の詳しい仕様は [関数リファレンス](srava_function_reference.html)（〔`stdlib: <名前>`〕タグ付き）。

## モジュールとの関係 — 2 つに分けて扱う {#module-policy}

| | 扱い |
|---|---|
| **必須モジュール** | そのライブラリの関数が**そのモジュールでしか動かない**もの。**ライブラリ自身が先頭で `module()` を宣言する**ので、利用側は **`module()` を書かなくてよい** |
| **使えるモジュール** | **複数のカーネルから選べる**もの。どれを使うかは利用側の判断なので、ライブラリは宣言せず、ここに記載する。★ **利用側は実際に使う 1 本（か数本）を `module()` で名指し、候補列に並べる** — 一覧の全部をロードする必要はない |

`module()` の再宣言は安全である。既にロード済みでも問題なく、**利用側が先に付けた `priority` 指定を
上書きしない**（素の `module("x.so", {})` は優先度を変えない）。

⚠⚠ **ライブラリが宣言するのは `module()`（ロード）であって、利用側の `use`（候補列）ではない。**
2 つは別の軸である。**自分のスクリプトからその op を直接呼ぶなら、候補列には自分で並べる**必要がある。

```
include "std/roll.sra";          // roll.sra が pipe_proximity を module() でロードする
use ["cgal"];                    // ← ここに "pipe_proximity" が無い
var a = pipe_sample(ctrl, [r,m], 0);   // ✗ 自分で呼んでいるので落ちる
                                       //   → use ["cgal", "pipe_proximity"];
```

★ ライブラリ関数の**中**は、関数が自分で候補列を宣言しているので影響を受けない
（[ライブラリ関数の宣言](srava_language_reference.html#lib-use-decl)）。効かないのは
**利用側が自分で書いた呼び出し**である。

⚠ 「使えるモジュール」側は**どれもロードしていないと落ちる**。落ち方はこの形:

```
*** ERROR[.../std/guide.sra,13] no module can execute op 'tube_ruled'
    on input types (none) (no module declares this op / not loaded / disabled by module(so,"off")) ***
```

## 一覧 {#list}

| ライブラリ | 役割 | 必須モジュール（宣言済み） | 使えるモジュール | 依存 include |
|---|---|---|---|---|
| [`std/math.sra`](#math) | 角度・ベクトル・行列の**純粋な値計算** | なし | `rotate_v` だけ `transform` を使う（9 本） | — |
| [`std/curve.sra`](#curve) | 曲線・パスの生成と定幅掃引 | なし | `tube_ruled` を持つ 8 本 | `math` |
| [`std/layout.sra`](#layout) | mesh 配列の整列・配置 | なし | `bbox` と `transform`(`>>>`) の**両方**を持つ 7 本 | — |
| [`std/guide.sra`](#guide) | 可視化ガイド（定規） | なし（ただし **cgal か manifold** が要る） | `combine` が cgal / manifold のみ | — |
| [`std/inspect.sra`](#inspect) | 検査結果の可視化 | **`cgal`** | — | — |
| [`std/roll.sra`](#roll) | 螺旋巻きつけ（トロッカス v2 BLH） | **`pipe_proximity`** | 幾何カーネルは**不要** | `math`, `curve` |

---

## `std/math.sra` — 値計算 {#math}

**必須モジュール**: なし。`rotate_v` を呼ばなければ**幾何カーネルを 1 本もロードせずに使える**
（lambda は定義時に評価されない）。

**使えるモジュール**: `rotate_v(m, v1, v2)` だけが mesh を受けて `transform` を使う
（`cgal` / `cherchi` / `geogram` / `manifold` / `nef_hybrid` / `nef_snc` / `occt` / `openvdb` / `points`）。

`rad` `deg` `range` `range2` `linspace` `vadd` `vsub` `vscale` `vdot` `vlen` `vnorm`
`reverse` `slice` `matvec` `rotmat2` `rotmat_z` `rotmat_x` `rotmat_y` `vcross`
`mat34` `mat34_t` `rotmat_2v` `rotate_v`

⚠ `range(n)` は **1 引数**（`range(lo,hi)` ではない）。範囲は `range2(lo, hi)`。

## `std/curve.sra` — 曲線とパス {#curve}

**必須モジュール**: なし。値だけを返す関数（`arc` / `bezier` / `spline` / `clothoid` / `arclen` /
`*_pts` / `tube_wall*`）は**カーネルを一切使わない**。

**使えるモジュール**: `tube_fw_ruled` だけが `tube_ruled` を呼ぶ。`tube_ruled` を持つのは
`cgal` / `manifold` / `nef_hybrid` / `nef_snc` / `geogram` / `cherchi` / `openvdb` / `occt` の 8 本。
⚠ `occt` の `tube_ruled` は背骨が折れ線なのは同じだが**断面が厳密な円**なので、メッシュ系と
値は一致しない（`occt` の `tube` は B-spline の背骨で、さらに別の形）。
⚠ `occt` の `tube_ruled` は **3D のパスだけ**を受ける（2D 点列は明示エラー）。
`tube_fw_ruled` に 2D 点列を渡して帯を作るなら、`cgal` / `manifold` など**メッシュ系**が要る。

`arc` `translate_pts` `scale_pts` `rotate_pts` `lerp_pts` `arc_tan` `arc_start_tan` `arc_end_tan`
`bezier` `spline` `clothoid` `tube_fw_ruled` `tube_fw` `arclen` `tube_wall` `tube_wall_var`

⚠⚠ **`ribbon2d` は `tube_fw_ruled` へ改名された**（`tube` / `tube_ruled` の対に揃えたもの。
実装は最初から次元非依存だったので、名前から `2d` を落とした）。**別名は置いていない**ので、
`ribbon2d(...)` を呼んでいる既存のスクリプトは `undefined variable` で落ちる。
⇒ `ribbon2d(` を `tube_fw_ruled(` に置き換える（引数は変わらない）。

★ **`tube_fw` は `tube_fw_ruled` のなめらか版**（op の対 `tube` / `tube_ruled` を stdlib へ
そのまま写した名前）。分かれ目は**背骨** — `tube` は点を**通る** C2 の B-spline、
`tube_ruled` は点を直線で結ぶ折れ線。

| | 呼ぶ op | 要るモジュール | 2D 点列を渡すと |
|---|---|---|---|
| `tube_fw_ruled(pts, w)` | `tube_ruled` | `tube_ruled` を持つ 8 本のどれか | **2D の帯**（メッシュ系のみ。`occt` は明示エラー） |
| `tube_fw(pts, w)` | `tube` | **`occt` だけ** | **平たい 3D の立体**（`tube` は `[x,y]` を z=0 として受ける） |

⚠ `tube_fw` は `occt` が要る。ロードしていなければ `no module can execute op 'tube'` で
落ちる（**黙って折れ線版に化けることは無い**）。帯が欲しいときは `tube_fw_ruled` を使う。

⚠ `tube_wall` / `tube_wall_var` は**管を作らない** — `[位置, 半径]` の path を受けて
**path を返す**（壁オフセット）。管にするには `tube_ruled` へ渡す。

## `std/layout.sra` — 整列・配置 {#layout}

**必須モジュール**: なし。

**使えるモジュール**: `bbox` と `transform`（`>>>` 演算子）の**両方**が要る。両方を持つのは
`cgal` / `manifold` / `nef_hybrid` / `nef_snc` / `occt` / `openvdb` / `points` の 7 本。
`cherchi` / `geogram` は `transform` は持つが `bbox` を持たないので、
**`geomutils.so` を併せてロードすれば**使える。

`stack` `row` `column` `grid` `grid3` `align`

⚠ 返り値は**配列**（reduce しない）。1 つにまとめたいなら `union(...)` を呼ぶ。
2D / 3D 両対応（bbox の隅の次元で判定）。

## `std/guide.sra` — 可視化ガイド {#guide}

**必須モジュール**: なし。ただし `combine` を持つのが `cgal` / `manifold` の 2 本だけなので、
**実質どちらかが要る**。`tube_ruled` の方は 8 本が持つ。

`ruler(axis, len, step, r)`

## `std/inspect.sra` — 検査の可視化 {#inspect}

**必須モジュール**: **`cgal`**（ライブラリ先頭で宣言済み）。`thin_spots` を持つのは cgal だけで、
`combine` も cgal / manifold の 2 本しかない。

`thin_markers` `thin_spots_band` `thin_markers_band`

## `std/roll.sra` — 螺旋巻きつけ {#roll}

**必須モジュール**: **`pipe_proximity`**（ライブラリ先頭で宣言済み）。
`pipe_sample` / `pipe_scene_adjust` を持つのはこのモジュールだけ。

**使えるモジュール**: なし。**幾何カーネルを 1 本もロードしなくても動く**。

`cross3` `clamp1` `zdist` `resample_n` `respace` `respace_range` `solve` `roll_initial` `roll_step`

詳細は [螺旋巻きつけライブラリ](srava_roll_reference.html)。ドライバ例は `examples/roll_sample.sra`。

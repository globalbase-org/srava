---
title: srava ドキュメント
---

# srava ドキュメント

**srava** は幾何カーネルベースの並列ポリゴン／ソリッド編集 DSL。2D スケッチと 3D ソリッドを逐次実行型の
スクリプトで記述したものを **DAG 化**し、並列実行する。演算ごとに結果をキャッシュすることにより、
**編集差分のみの実行**・**同一部分式のキャッシュ再利用**が可能となり、効率的に計算を実行できる。

サポートされる演算は、srava 本体とは独立した**モジュール（ダイナミックリンクライブラリ）**で供給される。
演算の種類や幾何カーネルを必要に応じて追加でき、**混在利用**が可能。

演算の実行形式には、srava 本体のプロセス内のスレッドで実行する **thread agent** 方式と、srava 本体とは
別のプロセスで実行する **process agent** 方式があり、その混在利用も可能である。`cgal.so` のように
**スレッド・リエントラントでない**幾何カーネルを必要とするものも、process agent で実行することで並列化を
実現できる。

## ドキュメント

- [**インストールガイド**](srava_install_guide.html) — Linux / macOS / Windows(MSYS2・Cygwin)での
  ビルド + インストール手順。各モジュールの依存関係(幾何カーネル CGAL 厳密 / Manifold 高速)・
  tinyState・HDF5、外部依存の自動取得(FetchContent)まで。**まず動かすならこちら。**
- [**モジュールリファレンス**](srava_module_reference.html) — モジュール(`.so`)の探索路・優先度・
  記述子 ABI と、**モジュールが効かないときの診断**:
  [`srava --modules`](srava_install_guide.html#modules)（**どの `.so` が効いているか** = 配置）と
  [`srava --module-info`](srava_install_guide.html#module-info)（**何を申告しているか** = 中身）。
- [**言語リファレンス**](srava_language_reference.html) — 文法・lambda/クロージャ・評価モデル・
  **並列に走るもの / 走らないもの**・2D/3D ディスパッチ・キャッシュ・エラー表示・
  **設定と環境変数**(srava 変数 → 環境変数 → 既定 の解決順・負荷コントロール一覧)。
  まず全体像を掴むならこちら。
- [**関数リファレンス**](srava_function_reference.html) — 全関数・演算子を統一形式で一覧した逆引き
  カタログ。「この関数のシグネチャと使い方をすぐ知りたい」ときに。冒頭に
  **[op × モジュールの ○× 表](srava_function_reference.html#module-matrix)**（どの演算をどのモジュールが
  実装しているか・記述子から機械生成）がある。
- [**螺旋巻きつけライブラリ**](srava_roll_reference.html) — 標準ライブラリ `std/roll.sra`（芯パイプに
  太さ可変パイプを密接に巻きつけて螺旋ホーンを生成する継続法）の関数リファレンス。**要 pipe_proximity
  モジュール**。ドライバ例は `examples/roll_sample.sra`。
- [**モジュールリファレンス**](srava_module_reference.html) — モジュール機構（`.so`）の概要と、同梱
  モジュールの依存・対応型・op 一覧。幾何カーネルは **cgal.so**（CGAL・厳密）/ **manifold.so**（高速）/
  **nef_snc.so・nef_hybrid.so**（Nef 多面体）/ **geogram.so**（厳密 mesh arrangement）/
  **cherchi.so**（indirect predicates）/ **occt.so**（B-rep・解析曲面）/ **openvdb.so**（ボリューム）、
  表現をまたぐ**橋渡し** **occt_mf.so** / **openvdb_mf・cg・gg.so**、
  解析モジュールは **pipe_proximity**（可変太さ配管の自己接近検出・距離調整）。
  ★ 既定でビルドされるのは cgal / manifold / nef / pipe_proximity で、残りは opt-in。
- [**モジュール設計**](srava_module_design.html) — 自作モジュール（`.so`）を書くための設計ガイド。
  記述子 ABI・op 申告・型/4CC 登録・実行方式・cross-module 型変換・ビルド/配置。
- [**k-Wave 音響シミュレーション**](srava_kwave.html) — srava の形状を `export_vox` で voxel 化し、
  格子音響ソルバ **k-Wave** で解く連携ガイド（vox.h5 → `vox2input.py` → input.h5 → ソルバ・断面 PNG）。

## クイック例

```
// 2D スケッチ → 押し出し → ブール → STL
var plate = rect(40, 55) >>> [-20, -27.5, 0];     // 中心化
var holes = union(map([[0,0],[15,0],[0,20]], \(p){ circle(2, 24) >>> p; }));
export("plate.stl", extrude(difference(plate, holes), 3));
```

```
// stdlib の曲線 + tube
include "std/curve.sra";
var path = map(bezier([[0,0,0],[20,20,0],[40,0,10]], 24), \(p){ [p, 2]; });
export("horn.stl", tube(path, 24));
```

## 実行

```
srava model.sra            # ファイルを実行(先頭の #! シェバングは読み飛ばす)
```

★ **モジュールは `module()` で名指したときにロードされる**。
とりあえず実カーネル一式(cgal / geogram / manifold / occt / openvdb / nef_hybrid)を使いたいときは
`include "module/all.sra";` か `SRAVA_MODULE_ALL=1`。**「全モジュール」ではない** — `nef_snc`・
橋渡し(`openvdb_mf` / `openvdb_cg` / `openvdb_gg`)・`pipe_proximity`・`d2`-`d5` は個別に `module()` する。

```
module("manifold.so", {});                 // 使うモジュールを明示する
include "module/all.sra";                  // または一式まとめて
```

環境変数 `SRAVA_AGENT`（agent バイナリ）・`SRAVA_CACHE_DIR`（キャッシュ置き場）・
`SRAVA_CACHE_RETAIN`（終了時キャッシュ掃除の保持方針・既定は即削除）・
`SRAVA_PATH`（`include` の探索パス）・`SRAVA_LOAD_CPU`（並列ワーカー上限）など。詳細は言語リファレンス。

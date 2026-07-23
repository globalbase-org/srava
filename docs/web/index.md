---
title: srava ドキュメント
---

# srava ドキュメント

**srava** は CGAL ベースの Lisp 風（S 式）ポリゴン／ソリッド DSL。2D スケッチと 3D ソリッドを
テキストで記述し、演算ごとにプロセス分離した**内容アドレス・キャッシュ付きの並列ランタイム**で
評価する（編集差分・同一部分式を 1 回だけ計算）。

## ドキュメント

- [**言語リファレンス**](srava_language_reference.html) — 文法・lambda/クロージャ・評価モデル・
  2D/3D ディスパッチ・キャッシュ・エラー表示・環境変数。まず全体像を掴むならこちら。
- [**関数リファレンス**](srava_function_reference.html) — 全関数・演算子を統一形式で一覧した逆引き
  カタログ。「この関数のシグネチャと使い方をすぐ知りたい」ときに。
- [**プラグインリファレンス**](srava_plugin_reference.html) — 本体を再ビルドせず機能を足す
  プラグイン機構の概要・インストール手順と、同梱プラグイン **pipe_proximity**（可変太さ配管の
  自己接近検出・距離調整・N 体）の各 op の使い方。
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

環境変数 `SRAVA_AGENT`（agent バイナリ）・`SRAVA_CACHE_DIR`（キャッシュ置き場）・
`SRAVA_CACHE_RETAIN`（終了時キャッシュ掃除の保持方針・既定は即削除）・
`SRAVA_PATH`（`include` の探索パス）・`PIG_MAX_WORKERS`（並列ワーカー上限）など。詳細は言語リファレンス。

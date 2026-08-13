---
title: srava 関数リファレンス
---

# srava 関数リファレンス

srava の**全関数・演算子**を統一形式で一覧する逆引きリファレンス。言語の文法・評価モデル・
設計思想の詳しい解説は [**言語リファレンス**](srava_language_reference.html) を参照。ここは
「この関数のシグネチャ・**引数の型**・使い方をすぐ知りたい」ためのカタログ。

## 読み方

各エントリは次の形式:

> ### `name(引数)` — 短い説明  〔stdlib: ライブラリ名〕
> `次元タグ` · → `戻り値型`
>
> 説明。
>
> **入力**
> - `引数` 役割 — 型
>
> **出力** 説明 — 型
>
> - 既定: 省略時の値（あれば）
> - 例: `コード`
> - 関連: 近い関数

- **〔stdlib〕** 付きは `include "std/….sra";` が要るライブラリ関数（カーネル非依存・合成で実装）。
  付かないものは**組込**（パーサ／agent／planner）。
- 角度はすべて**ラジアン**（度数は `rad()`/`deg()` で変換）。座標は無単位の数値。

### 型の凡例

引数・戻り値の「型」は次の語で表す:

| 型 | 表すもの | 例 |
|---|---|---|
| `スカラ` | 数値（実数） | `3`, `1.5`, `-2.0` |
| `整数` | 個数・分割数などの整数 | `n`, `segs`, `cols` |
| `文字列` | 文字列 | `"x"`, `"part.stl"`, `"center"` |
| `2D/3D ベクトル` | 点・方向。`[x,y]` か `[x,y,z]` | `[10,0]`, `[1,1,0]` |
| `2D ベクトル` / `3D ベクトル` | 次元が固定のとき | `[x,y]` / `[x,y,z]` |
| `ベクトル` | 任意長の数値配列 | `[a,b,c,…]` |
| `点列` | ベクトルの配列 `[[…],…]` | `[[0,0],[1,1]]` |
| `行列` | 行ベクトルの配列 | `[[c,-s],[s,c]]` |
| `mesh` | 幾何（2D 領域 or 3D ソリッド・キャッシュ継続） | `box(1,1,1)` |
| `mesh 配列` | mesh の配列 | `[box(1,1,1), …]` |
| `配列` / `ハッシュ` | 一般の配列 / 連想配列 | `[…]` / `{k:v}` |
| `関数` | ラムダ `\(…){…}` | `\(p){ p[0]; }` |
| `null` | 無し（副作用のみの戻り） | |

> mesh は **2D（スケッチ）と 3D（ソリッド）**があり、ほとんどの op は入力 mesh の次元で
> 2D/3D を自動判別する。次元の制約があるものは各エントリの次元タグと説明に記す。

## カテゴリ

1. [演算子](#演算子) 2. [3D プリミティブ](#prim3d) 3. [2D プリミティブ](#prim2d)
4. [スイープ・2D⇄3D](#スイープ2d3d) 5. [ブール演算](#ブール演算) 6. [アフィン変換](#アフィン変換) · [カーネル・型変換](#kernel-conv)
7. [計測・検査・修復](#計測検査修復) 8. [近接](#近接2-メッシュ間3d-専用) 9. [配列・数値ユーティリティ](#配列数値ユーティリティ)
10. [初等関数](#初等関数) 11. [I/O・システム](#io) 12. [stdlib: math](#stdlib-math数学定数ベクトル行列)
13. [stdlib: curve](#stdlib-curve曲線生成点列) 14. [stdlib: layout](#stdlib-layoutmesh-配列レイアウト)

---

## 演算子

### `>>>` — 平行移動（transform シュガー）
`2D・3D` · → `mesh` / `mesh 配列`

**実装**: `cgal.so` + `manifold.so`（= 対応する名前付き op）· 型 入力を保存（`cg-mesh3d`/`mf-mesh3d` 等）

`m >>> v` ＝ `translate(m, v)`。左辺が **mesh 配列**なら各要素へ適用し配列を返す（broadcast / zip / instancing）。

**入力**
- 左辺 `m` 対象 — `mesh` または `mesh 配列`
- 右辺 `v` 移動量 — `2D/3D ベクトル`（mesh 配列では `点列` で zip/instancing）

**出力** 移動後 — `mesh`（左辺が配列なら `mesh 配列`）

- 例: `box(2,2,2) >>> [1,0,0]` / `[a,b] >>> [1,0,0]`（broadcast）/ `box(1,1,1) >>> [[0,0,0],[3,0,0]]`（複製配置）
- 関連: `translate`, `<>`, `***`, `@`

### `<>` — 鏡像（transform シュガー）
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so`（= 対応する名前付き op）· 型 入力を保存（`cg-mesh3d`/`mf-mesh3d` 等）

`m <> axis` ＝ `mirror(m, axis)`。原点通過平面での反射。

**入力**
- 左辺 `m` 対象 — `mesh`
- 右辺 `axis` 鏡映軸/法線 — `文字列`（`"x"`/`"y"`/`"z"`）または `3D ベクトル`

**出力** 反射後 — `mesh`

- 例: `box(1,2,2) <> "x"` / `m <> [1,1,0]`（任意法線）
- 関連: `mirror`

### `***` — 拡大縮小（transform シュガー）
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so`（= 対応する名前付き op）· 型 入力を保存（`cg-mesh3d`/`mf-mesh3d` 等）

`m *** s` ＝ `scale(m, s)`。負値＝反射。

**入力**
- 左辺 `m` 対象 — `mesh`
- 右辺 `s` 倍率 — `スカラ`（均等）または `ベクトル`（軸別 `[sx,sy,sz]`）

**出力** 拡縮後 — `mesh`

- 例: `box(1,1,1) *** 2` / `box(1,1,1) *** [2,3,4]`
- 関連: `scale`

### `@` — 回転（transform シュガー）
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so`（= 対応する名前付き op）· 型 入力を保存（`cg-mesh3d`/`mf-mesh3d` 等）

`m @ (axis, deg)` ＝ `rotate(m, axis, deg)`（度数）。2D は軸不要で `m @ (deg)`。

**入力**
- 左辺 `m` 対象 — `mesh`
- 右辺 `axis` 回転軸 — `文字列`（`"x"`/`"y"`/`"z"`）または `3D ベクトル`（2D は省略）
- 右辺 `deg` 角度（度） — `スカラ`

**出力** 回転後 — `mesh`

- 例: `box(4,4,1) @ ("z", 45)` / `poly @ (30)`（2D 面内回転）/ `m @ ([1,1,0], 90)`（任意軸）
- 関連: `rotate`

### `|||` — 和（ブールシュガー）
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 3D `cg-mesh3d`(MESH)/`mf-mesh3d`(MFM3)・2D `cg-cross2d`(PLY2)/`mf-cross2d`(MFC2)

`a ||| b` ＝ `union(a, b)`。可換・結合。多数を畳むなら `union(配列)`（並列二分木）。

**入力** 左辺 `a`・右辺 `b` 被演算 — ともに `mesh`（同次元）

**出力** 和 — `mesh`

- 注: 面接触・同一平面は失敗しやすい → わざと少し重ねる
- 関連: `union`, `&&&`, `---`, `+++`

### `&&&` — 積（ブールシュガー）
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 3D `cg-mesh3d`(MESH)/`mf-mesh3d`(MFM3)・2D `cg-cross2d`(PLY2)/`mf-cross2d`(MFC2)

`a &&& b` ＝ `intersection(a, b)`。可換・結合。

**入力** 左辺 `a`・右辺 `b` — ともに `mesh`（同次元）

**出力** 積 — `mesh`

- 関連: `intersection`

### `---` — 差（ブールシュガー）
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 3D `cg-mesh3d`(MESH)/`mf-mesh3d`(MFM3)・2D `cg-cross2d`(PLY2)/`mf-cross2d`(MFC2)

`a --- b` ＝ `difference(a, b)`。**非可換**（`a` から `b` を引く）。`a --- {} = a`、`{} --- a = {}`。

**入力** 左辺 `a` 被減数・右辺 `b` 減数 — ともに `mesh`（同次元）

**出力** 差 — `mesh`

- 関連: `difference`

### `+++` — 単純合体（ブールシュガー）
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 3D `cg-mesh3d`(MESH)/`mf-mesh3d`(MFM3)・2D `cg-cross2d`(PLY2)/`mf-cross2d`(MFC2)

`a +++ b` ＝ `combine(a, b)`。corefinement せず連結（交差許容・軽い）。本番ブール前の確認やガイド線の重ね合わせに。

**入力** 左辺 `a`・右辺 `b` — ともに `mesh`（同次元・ガイド線も可）

**出力** 合体 — `mesh`

- 関連: `combine`, `line`

### `+` `-` `*` `/` — 算術
`スカラ・配列` · → 同型

**実装**: 組み込み · 型 -

数値の四則。**配列なら要素ごと**（`array op scalar` はブロードキャスト、`array op array` は要素ごと・長さ一致）。単項マイナス `-a` もあり。

**入力** 左辺・右辺 — `スカラ` または `配列`（**配列が左辺のときのみ**。`s * [a,b]` のスカラ左は未対応）

**出力** 結果 — 入力に応じ `スカラ` または `配列`

- 例: `[3,4] * [2,5]` → `[6,20]` / `[3,4] * 2` → `[6,8]`

### `%` — 剰余
`スカラ・配列` · → 同型

**実装**: 組み込み · 型 -

`a % b` ＝ `mod(a, b)`（fmod）。

**入力** `a`,`b` — `スカラ` または `配列`

**出力** 剰余 — 同型

- 関連: `mod`

### `==` `!=` `<` `>` `<=` `>=` — 比較
`スカラ` · → `整数`(0/1)

**実装**: 組み込み · 型 -

数値比較。`if`/`while` の条件に。

**入力** `a`,`b` — `スカラ`

**出力** 真偽 — `整数`（`1`/`0`）

### `||` `&&` `!` — 論理
`スカラ` · → `整数`(0/1)

**実装**: 組み込み · 型 -

論理 OR / AND / NOT。**短絡評価なし**（両辺とも評価）。優先順位は `||` < `&&` < 比較 < 算術。

**入力** 被演算 — `スカラ`（非ゼロ=真）

**出力** 真偽 — `整数`（`1`/`0`）

---

## 3D プリミティブ {#prim3d}

形状はすべて**原点基準**で生成。`box`/`prism`/`pyramid`/`extrude` は **Z 軸が高さ**で統一。

### `box(w, h, d)` — 直方体
`3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

w×h×d の軸並行直方体（原点隅・8 頂点 / 12 三角形）。

**入力**
- `w` X 方向の幅 — `スカラ`
- `h` Y 方向の高さ — `スカラ`
- `d` Z 方向の奥行き — `スカラ`

**出力** 直方体 — `mesh`（3D）

- 既定: `1,1,1`
- 例: `box(40, 55, 9)`
- 関連: `boxa`, `rect`(2D), `extrude`, `prism`

### `boxa([w, h, d])` — 直方体（配列版）
`3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

`box` と同じだが寸法を**配列 1 個**で渡す。計算で作った寸法ベクトルをそのまま渡せる。

**入力** `[w, h, d]` 寸法 — `3D ベクトル`（スカラ 3 個の配列）

**出力** 直方体 — `mesh`（3D）

- 例: `boxa([40, 55, 9])`
- 関連: `box`

### `prism(n, h, r)` — 正 n 角柱
`3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

底面が正 n 角形（外接半径 r・XY 平面 z=0）、高さ h（Z 軸）。`extrude(ngon(n,r), h)` と完全に等価。

**入力**
- `n` 角数 — `整数`（<3 は 3 に切上げ）
- `h` 高さ（Z） — `スカラ`
- `r` 底面の外接半径 — `スカラ`

**出力** 角柱 — `mesh`（3D）

- 既定: `3,1,1`
- 例: `prism(6, 10, 4)`（六角柱）
- 関連: `pyramid`, `ngon`, `extrude`

### `pyramid(n, h, r)` — 正 n 角錐
`3D` · → `mesh`

**実装**: `cgal.so` · 型 `cg-mesh3d`(MESH)

底面が正 n 角形（z=0）、頂点が z=h の角錐。

**入力**
- `n` 角数 — `整数`
- `h` 高さ（Z） — `スカラ`
- `r` 底面の外接半径 — `スカラ`

**出力** 角錐 — `mesh`（3D）

- 既定: `3,1,1`
- 例: `pyramid(4, 8, 5)`（四角錐）
- 関連: `prism`

### `sphere(r[, seg])` — 球（円周分割数指定）
`3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

半径 r の測地球（正八面体を分割して球面投影）。`seg` は円周分割数（連続値）。

**入力**
- `r` 半径 — `スカラ`
- `seg` 円周分割数 — `整数`（省略可）

**出力** 球 — `mesh`（3D）

- 既定: `r=1, seg=32` 相当（八面体 n=8・**258 頂点 512 面**）。面数 = 8·n²、n=(seg+3)/4。
- 例: `sphere(5, 64)`
- ★**cgal / manifold で頂点・面が一致**し体積が bit レベルで揃う（共通生成器 `src/h/common/geodesic.h`）。
- 細分回数（4 倍刻み）で指定したいときは `icosphere(r, subdiv)`。
- 関連: `icosphere`, `revolve`, `offset`

---

### `icosphere(r[, subdiv])` — 球（細分回数指定）
`3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

半径 r の測地球（正二十面体を `2^subdiv` 分割して球面投影）。旧 `sphere(r, subdiv)` の意味論はこの op が継ぐ。

**入力**
- `r` 半径 — `スカラ`
- `subdiv` 細分回数 — `整数`（省略可）

**出力** 球 — `mesh`（3D）

- 既定: `r=1, subdiv=0`（正二十面体 20 面）。`1`=80 面 / `2`=320 面 / `3`=1280 面 …（4 倍刻み・上限 6）。
- 例: `icosphere(5, 2)`（= 旧 `sphere(5, 2)`・162 頂点 320 面）
- ★**cgal / manifold で頂点・面が一致**（`sphere` と同じ共通生成器）。
- 関連: `sphere`, `revolve`, `offset`

---

## 2D プリミティブ {#prim2d}

2D 多角形（`cgMesh2D`・穴あき可）は「断面」として `extrude`/`revolve` で 3D に持ち上がる。

### `rect(w, h)` — 長方形
`2D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-cross2d`(PLY2) / `mf-cross2d`(MFC2)

原点隅・軸並行の長方形（CCW）。

**入力**
- `w` X 方向の幅 — `スカラ`（>0）
- `h` Y 方向の高さ — `スカラ`（>0）

**出力** 長方形 — `mesh`（2D）

- 注: 負/0 は明示エラー
- 例: `rect(40, 55)`
- 関連: `box`(3D), `polygon`, `extrude`

### `ngon(n, r)` — 正 n 角形
`2D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-cross2d`(PLY2) / `mf-cross2d`(MFC2)

外接半径 r・原点中心・CCW の正 n 角形。

**入力**
- `n` 角数 — `整数`
- `r` 外接半径 — `スカラ`

**出力** 正多角形 — `mesh`（2D）

- 例: `ngon(6, 10)`
- 関連: `circle`, `prism`

### `circle(r[, segs])` — 円
`2D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-cross2d`(PLY2) / `mf-cross2d`(MFC2)

正多角形で近似した円。

**入力**
- `r` 半径 — `スカラ`
- `segs` 辺数（精度ピッチ） — `整数`（省略可・最小 3）

**出力** 円（近似多角形） — `mesh`（2D）

- 既定: `segs=32`。`circle(r,8)`＝八角形
- 例: `circle(10, 64)`
- 関連: `ngon`, `revolve`

### `polygon(pts)` ／ `polygon(p0, p1, …)` — 塗り多角形
`2D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-cross2d`(PLY2) / `mf-cross2d`(MFC2)

明示した点列の塗り多角形（任意 n 角形）。単純なら CW を CCW に正規化。自己交差も許容（→ `valid`/`repair`）。

**入力** `pts` 頂点列 — `点列`（2D ベクトルの配列 `[[x,y],…]`）。点を**別々の引数**として `polygon(p0, p1, …)` でも可（各 `pi` は `2D ベクトル`）

**出力** 塗り多角形 — `mesh`（2D）

- 例: `polygon([[0,0],[10,0],[5,8]])` / `polygon([0,0],[10,0],[5,8])`
- 関連: `line`（塗らない注釈線）, `repair`

### `line(pts)` ／ `line(p0, p1, …)` — ガイド線（開ポリライン）
`2D` · → `mesh`

**実装**: `cgal.so` · 型 `cg-cross2d`(PLY2)

塗らない**開ポリライン**をガイド層に作る（寸法線・ガイド用）。ブール対象外。`+++` で部品に重ねる。`>>>`/`@` 等は効く。

**入力** `pts` 頂点列（2 点以上） — `点列`（2D ベクトルの配列）。別々の引数 `line(p0, p1, …)` でも可

**出力** ガイド線 — `mesh`（2D・ガイド層）

- 出力ファイル: SVG＝塗りなしストローク / DXF＝レイヤ GUIDES の開 LWPOLYLINE
- 例: `part +++ line([[0,-5],[260,-5]])`
- 関連: `polygon`, `combine`

---

## スイープ・2D⇄3D

### `extrude(poly, h)` — 押し出し
`2D→3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 入力 `cg-cross2d`(PLY2)→出力 `cg-mesh3d`(MESH) / 入力 `mf-cross2d`(MFC2)→出力 `mf-mesh3d`(MFM3)

2D 多角形を高さ h でまっすぐ押し出して角柱化。**穴対応**（CDT 三角化）。

**入力**
- `poly` 断面 — `mesh`（2D）
- `h` 高さ（Z） — `スカラ`

**出力** 角柱 — `mesh`（3D）

- 例: `extrude(rect(40,20), 10)`
- 関連: `prism`, `revolve`

### `revolve(poly[, angle[, segs]])` — 回転体
`2D→3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 入力 `cg-cross2d`(PLY2)→出力 `cg-mesh3d`(MESH) / 入力 `mf-cross2d`(MFC2)→出力 `mf-mesh3d`(MFM3)

2D プロファイル（x=半径≥0, y=高さ）を **Y 軸**まわりに回して回転体化。

**入力**
- `poly` プロファイル — `mesh`（2D）
- `angle` 回転角（度） — `スカラ`（省略可）
- `segs` 全周の分割数 — `整数`（省略可）

**出力** 回転体 — `mesh`（3D）

- 既定: `angle=360`, `segs=32`
- 例: `revolve(polygon([[0,0],[5,0],[5,10],[0,10]]))`
- 注: `sphere` ≈ 半円の revolve。回転軸のみ Y 軸
- 関連: `extrude`

### `tube(path[, segs])` — パス掃引（太さ可変）
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)

折れ線に沿って丸断面を掃引。各頂点が `[位置, 半径]`。**位置の次元で 2D/3D を自動判別**。

**入力**
- `path` パス — `配列`。各要素は `[位置, 半径]` のペア（位置＝`2D/3D ベクトル`、半径＝`スカラ`。2D では半径＝半幅）
- `segs` 3D 断面円の辺数 — `整数`（省略可）

**出力** 管（3D）／帯（2D） — `mesh`

- 既定: `segs=32`
- 注: 連続重複頂点は自動間引き。r=0 端は尖って閉じる。滑らかな曲線は `std/curve.sra` でサンプリングしてから渡す
- 注: 掃引の幾何は両モジュール共通（`src/h/common/tube.h`）なので頂点・三角形の並びが一致する。tube 主体の連鎖は manifold.so 側で in-proc のまま走る
- 例: `tube([[[0,0,0],0.5],[[3,1,0],0.4],[[3,3,0],0.0]], 24)`
- 関連: `ribbon2d`, `bezier`, `spline`

### `section(mesh, P, N)` — 断面
`3D→2D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 入力 `cg-mesh3d`(MESH)→出力 `cg-cross2d`(PLY2) / 入力 `mf-mesh3d`(MFM3)→出力 `mf-cross2d`(MFC2)

点 P を通り法線 N の平面で 3D メッシュを切り、2D 断面（塗り領域・穴検出）を返す。3D 専用。

**入力**
- `mesh` 対象 — `mesh`（3D）
- `P` 切断点 — `3D ベクトル`
- `N` 平面の法線 — `3D ベクトル`

**出力** 断面 — `mesh`（2D）

- 例: `section(box(10,10,10), [0,0,5], [0,0,1])`（z=5 水平断面）
- 関連: `extrude`(再立体化)

---

## ブール演算

`a,b` は同次元 mesh。**2D と 3D を混ぜるとエラー**。接触/同一平面は失敗しやすい（少し重ねる）。

### `union(a, b)` ／ `union([a,…])` ／ `a ||| b` — 和
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)

ブール和（corefinement）。**配列 1 引数は並列二分木で畳み込み**（直列 fold の最大 23x）。可換。`union([])`＝`{}`（単位元）。

**入力** `a, b` 被演算 — ともに `mesh`（同次元）。または引数 1 個に `mesh 配列`

**出力** 和 — `mesh`

- 例: `union(box(2,2,2), box(1,1,3))` / `union(parts)`
- 関連: `intersection`, `difference`, `combine`

### `intersection(a, b)` ／ `a &&& b` — 積
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)

ブール積（corefinement）。可換。配列 1 引数で並列畳み込み。

**入力** `a, b` — ともに `mesh`（同次元）。または `mesh 配列` 1 個

**出力** 積 — `mesh`

- **`+++`(combine)被演算子の分配則**: 被演算子が**構文的に** `+++` の時だけ、`(a +++ b) &&& c` を `(a&&&c) +++ (b&&&c)` にパース時展開（`∪aᵢ∩∪bⱼ`・両側可）。combine は複数の閉立体を解決せず束ねた形で corefinement に渡せないため。`|||`(union)結果のような妥当なメッシュは combine ではないので分配しない（演算子自体はランタイムで最適化しない方針）。
- 関連: `union`, `combine`

### `difference(a, b)` ／ `a --- b` — 差
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)

`a` から `b` を引く。**非可換**（n-ary は左 fold `((a-b)-c)…`）。

**入力** `a` 被減数・`b` 減数 — ともに `mesh`（同次元）。または `mesh 配列` 1 個（左 fold）

**出力** 差 — `mesh`

- 例: `difference(box(4,4,4), sphere(2.5))`
- **`+++`(combine)被演算子の分配則**: 被演算子が構文的に `+++` の時だけパース時展開。`(a +++ b) --- c` = `(a---c) +++ (b---c)`（左 combine は成分ごとに引いて束ね）。`c --- (a +++ b)` = `c --- a --- b`（右 combine は逐次差）。`|||` 結果等は分配しない。
- 関連: `union`, `combine`

### `combine(a, b)` ／ `a +++ b` — 単純合体
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)

corefinement せず連結（別連結成分）。軽い。

⚠ **カーネルで意味論が違う**（cgal と manifold を混ぜて使うときの注意）:

| | 重なった部分の扱い | `volume(box(2,2,2) +++ box(1,1,3))` |
|---|---|---|
| `cgal.so` | **そのまま残す**（自己交差した非閉立体になる） | 11（= 8 + 3・重なりを二重に数える） |
| `manifold.so` | **解消する**（実質 `union`） | 9（= 重なり分を差し引いた値） |

Manifold カーネルの値は**常に妥当な 2-manifold 立体**であることが型の不変条件なので、「自己交差した
2 枚の殻をそのまま持つ」表現が原理的に存在しない（`Manifold::Compose` も v3.5.2 では
`BatchBoolean(OpType::Add)` そのもの = 実体は union で、deprecated 扱い）。cgal 側の `Surface_mesh` は
妥当性を要求しない単なるポリゴン容器なので保持できる、という違い。**実装の都合ではなくカーネルの
不変条件**なので、manifold 側でこの差を埋めることはできない。

実用上の注意: 色分けした部品を重ねて可視化する用途（`color(本体,"gray") +++ color(マーカ,"red")`）では、
manifold カーネルだと**他方に完全に埋まった成分は吸収されて消える**。マーカは表面から
はみ出す位置に置くこと（`examples/pipe_clearance.sra` は接近点＝表面に置いているので問題ない）。
重なりを残したまま観察したい場合は cgal カーネルを使う。

**入力** `a, b` — ともに `mesh`（同次元）。または `mesh 配列` 1 個

**出力** 合体 — `mesh`

- 例: `export(a +++ b)`（重なり確認）
- 関連: `union`, `line`

---

## アフィン変換

mesh を変換して新 mesh を返す（位相不変）。演算子シュガーは [演算子](#演算子)参照。

### `translate(m, v)` ／ `translate(m, x, y, z)` ／ `m >>> v` — 平行移動
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)

平行移動（EPECK 厳密）。

**入力**
- `m` 対象 — `mesh`
- `v` 移動量 — `2D/3D ベクトル`（または `x, y, z` の `スカラ` 3 個）

**出力** 移動後 — `mesh`

- 例: `translate(box(2,2,2), [1,0,0])` / `translate(box(2,2,2), 1,0,0)`
- 関連: `>>>`, `translate_pts`(点列版)

### `rotate(m, axis, deg)` ／ `rotate(m, deg)` ／ `m @ (axis, deg)` — 回転
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)

原点まわりの回転（度数）。2D は軸不要（面内回転）。

**入力**
- `m` 対象 — `mesh`
- `axis` 回転軸 — `文字列`（`"x"`/`"y"`/`"z"`）または `3D ベクトル`（Rodrigues・2D は省略）
- `deg` 角度（度） — `スカラ`

**出力** 回転後 — `mesh`

- 注: cos/sin は double 近似だが座標は厳密有理数のまま。`[0,0,0]` 軸はエラー
- 例: `rotate(box(4,4,1), "z", 45)` / `rotate(poly, 30)`(2D)
- 関連: `@`, `rotate_pts`(点列版), `mirror`

### `mirror(m, axis)` ／ `m <> axis` — 鏡像
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)

原点通過平面での反射（面の向きは自動復元）。

**入力**
- `m` 対象 — `mesh`
- `axis` 鏡映軸/法線 — `文字列`（`"x"`/`"y"`/`"z"`）または `3D ベクトル`（Householder）

**出力** 反射後 — `mesh`

- 例: `mirror(m, "x")`
- 関連: `<>`, `scale`(負値)

### `scale(m, s)` ／ `scale(m, sx, sy, sz)` ／ `m *** s` — 拡大縮小
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)

原点中心の拡大縮小。負値＝反射。

**入力**
- `m` 対象 — `mesh`
- `s` 倍率 — `スカラ`（均等）または `ベクトル`（軸別・または `sx, sy, sz` の `スカラ` 3 個）

**出力** 拡縮後 — `mesh`

- 注: 係数 0 は退化エラー
- 例: `scale(box(1,1,1), 2)` / `scale(box(1,1,1), [2,3,4])`
- 関連: `***`, `scale_pts`(点列版)

### `transform(m, matrix)` — 一般アフィン
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)

行優先の同次行列で一般アフィン変換。

**入力**
- `m` 対象 — `mesh`
- `matrix` 変換行列 — `配列`（行優先・12 要素=3×4 または 16 要素=4×4 の平坦な数値配列）

**出力** 変換後 — `mesh`

- 例: `transform(box(2,2,2), [1,0,0,1, 0,1,0,0, 0,0,1,0])`（+x 平行移動）
- 関連: `matvec`(点列・stdlib)

### `offset(m, d[, n])` — オフセット
`2D・3D` · → `mesh`

**実装**: `cgal.so`(2D+3D) + `manifold.so`(2D 専用) · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-cross2d`(MFC2)

`d>0` で膨張、`d<0` で収縮。2D＝straight skeleton（面取り）/ 3D＝半径 d の球との Minkowski 和（重い）。

**入力**
- `m` 対象 — `mesh`
- `d` オフセット量 — `スカラ`
- `n` 3D の球細分化 — `整数`（省略可・2D は無視）

**出力** オフセット形状 — `mesh`

- 既定: `n=1`
- 注: 肉厚 ＝ `offset(m, t) --- m`（外殻）/ `m --- offset(m, -t)`（内殻）
- 例: `offset(rect(20,10), 2)`
- 関連: `difference`

### `color(m, c)` — 面に色をつける
`3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

mesh の**全面に色 `c`** を付ける（cgal は per-face プロパティ `f:color`、manifold は頂点プロパティ ch3..5。どちらも全体を一様に塗るので見え方は同じ）。`+++`（combine）で重ねたとき**各成分の色が保持される**ので、要素ごとの色分けに使う（本体グレー＋ものさし赤、など）。色は**色対応フォーマットで出力**される。

**入力**
- `m` 対象 — `mesh`（3D。2D はエラー）
- `c` 色 — `文字列`（名前 `"red"`/`"green"`/`"blue"`/`"yellow"`/`"cyan"`/`"magenta"`/`"orange"`/`"purple"`/`"white"`/`"black"`/`"gray"`、または `"#RRGGBB"`）または `[r, g, b]`（0–255）

**出力** 着色した mesh — `mesh`（3D・`f:color` つき）

- 出力での色: **3MF**（colorgroup・スライサ/viewer で色つき）/ **AMF**（`<color>`）/ **OFF**（COFF・面ごと RGB）/ **PLY**（面色）。**STL/OBJ は色非対応**で無視される
- 未着色の面は combine 時に灰（180）になる
- 例: `color(box(80,40,30), "gray") +++ color(ruler(0,80,10,0.4), "red")` → `export("p.3mf", …)`
- 関連: `combine`, `ruler`, `export`

---

## 幾何カーネルの選択と型変換 {#kernel-conv}

どの幾何カーネル（CGAL 厳密 / Manifold 高速）で計算するかは mesh の**型**で決まる。`cast` は
**型**（`"cg-…"`/`"mf-…"`）を明示変換する op であり、幾何カーネルそのものを直接切り替えるのではない
（型が変われば結果としてその型をサポートする幾何カーネルへ移る）。概念・選択規則・ポリシーは
[**言語リファレンス §10 幾何カーネル**](srava_language_reference.html#kernel) を参照。

### `cast(target_type, mesh)` — 目標型への明示変換 {#cast}
`3D`/`2D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`(MFC2)（目標型を産出できるモジュールへ routing）

`mesh` を目標**型** `target_type` へ明示的に移す（rev4 型ディスパッチ）。無損失方向（Manifold→CGAL の昇格）は
自動でも `cast` でも起こせる。損失方向（CGAL→Manifold のダウングレード）は `cast` で明示する。

**入力**
- `target_type` 目標型 — `string`（`"cg-mesh3d"`=CGAL 厳密 3D / `"mf-mesh3d"`=Manifold 3D /
  `"cg-cross2d"`=CGAL 2D / `"mf-cross2d"`=Manifold 2D）
- `mesh` 対象 — `mesh`(3D)または `poly`(2D)

**出力** 同じ形状を `target_type` で表した mesh — `mesh`

- ★**成立条件**: `cast(T, mesh)` は「**`mesh` が書き出す 4CC を、目標型 `T` のモジュールが読める**」ときだけ成立する。
  - cgal.so は `MESH`/`PLY2`（読み書き）に加え `MFM3`/`MFC2` を **readonly 昇格**で読む → `cast("cg-mesh3d", …)` / `cast("cg-cross2d", …)` は cg でも mf 入力でも可（mf→cg は無損失）。
  - manifold.so は `MFM3`/`MFC2`（読み書き）に加え、**`MESH`（CGAL 3D）/ `PLY2`（CGAL 2D）を readonly ダウングレードで読む**（有理数→double・損失）→ `cast("mf-mesh3d", cg_mesh)` / `cast("mf-cross2d", cg_cross2d)` とも**成立（損失）**。
- **型相互変換表**（入力型 × 目標型の全対応）:

| 入力型 (4CC) ＼ 目標型 | `cg-mesh3d` | `cg-cross2d` | `mf-mesh3d` | `mf-cross2d` |
|---|---|---|---|---|
| `cg-mesh3d`（`MESH`） | =（no-op） | — | ✅ **損失**（降格） | — |
| `cg-cross2d`（`PLY2`） | — | =（no-op） | — | ✅ **損失**（降格） |
| `mf-mesh3d`（`MFM3`） | ✅ 無損失（昇格） | — | =（no-op） | — |
| `mf-cross2d`（`MFC2`） | — | ✅ 無損失（昇格） | — | =（no-op） |

  - **昇格**（mf→cg）: `cg-mf-upgrade` codec。double は 2 進有理数なので**無損失**で EPECK 厳密化。自動（sig routing）でも `cast` でも起こる。
  - **降格**（cg→mf）: `mf-cg-downgrade` codec。有理数→double 化で**損失**。損失を伴うため `cast` による**明示**のみ。
  - `—`: **次元（3d↔2d）は跨げない**。次元を変えるのは `extrude`（2D→3D）/ `section`（3D→2D）等の op であって `cast` ではない。
- `"cg-…"`: → CGAL(厳密)。Manifold(double)入力は**無損失で厳密化**（double は 2 進有理数）。
- 目標型は**次元を含む**（3d/2d）ので、旧 `cast("exact")`（次元非依存）と違い曖昧さがない。
- 既に目標型なら実質 no-op（再エンコードのみ）。
- 例: `var m = cast("cg-mesh3d", box(40,40,40) ||| sphere(20));  // Manifold で速く作り無損失で厳密化`
- 例: `export("fast.stl", cast("mf-mesh3d", box(2,2,2) ||| box(1,1,3)));  // 厳密 3D→高速へ（損失）`
- ⚠ 旧 `cast("exact")` / `cast("manifold")`（カーネル名指し）は**廃止**（後方互換なし・rev4）。
- 関連: `valid`, `volume`

### `module(so[, {priority, exec_default}])` — モジュールの選択・実行方式の上書き
文 · → `null`

**実装**: 組み込み · 型 -

.so **モジュール**をロードし、その選択優先度・実行方式を**上書き**する planner 側関数。**省略可能**——
呼ばなくても各モジュールは既定設定（記述子の priority / exec_default）で動く。既定の挙動を変えたい
ときだけ書く。設定上書きのない軽量ロードは `load(so)`。

**入力**
- `so` モジュール — `文字列`（`"cgal.so"` / `"manifold.so"` / `"pipe_proximity.so"` 等）
- `{priority, exec_default}` 上書き設定 — `ハッシュ`（省略可）
  - `priority` 選択優先度 — `整数`。同じ型/op を複数モジュールが提供するとき、どれを既定に寄せるか（大きいほど優先）
  - `exec_default` 実行方式 — `文字列`（`"process"` 別プロセス / `"thread"` 同一プロセス内スレッド）。op ごとの重さに応じて上書き

**出力** — `null`（副作用でモジュール登録・設定を書き換える）

- 例: `module("manifold.so", {priority: 20});`（既定幾何カーネルを Manifold 寄りにする）
- 例: `module("cgal.so", {exec_default: "thread"});`（cgal の op を同一プロセスで実行）
- `load(so)` は設定上書きなしの軽量版（ロードのみ）。
- 構文は `ns_sravaParser.y:457-468`（`load`/`module`）で確認可。
- 詳細な引数仕様・記述子 ABI は[**モジュールリファレンス**](srava_module_reference.html)へ。
- 関連: `cast`, `load`

---

## 計測・検査・修復

「値返し op」は結果を式で観測できる（`if (valid(m)==1){…}`・`area(a)+area(b)`）。配列返しは添字可。

### `area(m)` — 面積
`2D・3D` · → `スカラ`

**実装**: `cgal.so` + `manifold.so` · 型: value

2D＝囲み面積（外周−穴）/ 3D＝表面積。

**入力** `m` 対象 — `mesh`

**出力** 面積 — `スカラ`

- 例: `area(rect(4,5))` → `20`

### `volume(m)` — 体積
`3D` · → `スカラ`

**実装**: `cgal.so` + `manifold.so` · 型: value

囲む体積（閉メッシュ・発散定理）。2D はエラー。

**入力** `m` 対象 — `mesh`（3D）

**出力** 体積 — `スカラ`

### `perimeter(m)` — 周長
`2D` · → `スカラ`

**実装**: `cgal.so` · 型: value

境界長（外周＋穴の周長）。3D はエラー。

**入力** `m` 対象 — `mesh`（2D）

**出力** 周長 — `スカラ`

### `centroid(m)` — 重心
`2D・3D` · → `ベクトル`

**実装**: `cgal.so` + `manifold.so` · 型: value

面積/体積重心。

**入力** `m` 対象 — `mesh`

**出力** 重心 — `2D/3D ベクトル`（2D＝`[x,y]` / 3D＝`[x,y,z]`）

- 例: `centroid(m)[0]`（x 成分）

### `bbox(m)` — バウンディングボックス
`2D・3D` · → `配列`

**実装**: `cgal.so` + `manifold.so` · 型: value

軸平行 AABB を `[min隅, max隅]` で返す。

**入力** `m` 対象 — `mesh`

**出力** 範囲 — `点列`（`[min, max]` の 2 要素・各隅は `2D/3D ベクトル`）

- 例: `var bb = bbox(m); var sz = bb[1] - bb[0];`（サイズ）
- 注: 空集合はエラー → `valid` でガード
- 関連: `centroid`, layout 各関数

### `valid(m)` — 検証
`2D・3D` · → `整数`(0/1)

**実装**: `cgal.so` + `manifold.so` · 型: value

`1`=正常 / `0`=問題。3D＝閉∧自己交差なし / 2D＝全リング単純。

**入力** `m` 対象 — `mesh`

**出力** 正常か — `整数`（`1`/`0`）

- 例: `if (valid(m) == 1) { export("ok.stl", m); }`
- 関連: `repair`

### `repair(m)` — 修復
`2D・3D` · → `mesh`

**実装**: `cgal.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2)

2D＝`Polygon_repair`（even-odd 正規化・完全）/ 3D＝`autorefine`（自己交差をエッジ化・best-effort）。

**入力** `m` 対象 — `mesh`

**出力** 修復後 — `mesh`

- 関連: `valid`, `polygon`

---

## 近接（2 メッシュ間・3D 専用）

いずれも 3D-3D 専用。2D / 次元混在はエラー。

### `distance(a, b)` — 最近接距離
`3D` · → `スカラ`

**実装**: `cgal.so` · 型: value

2 メッシュ間の最近接距離（AABB 近似・頂点↔面の双方向最小）。

**入力** `a, b` 対象 — ともに `mesh`（3D）

**出力** 最近接距離 — `スカラ`

- 注: 辺-辺の谷を取りこぼし得る（密メッシュで真値に収束）
- 関連: `closest`

### `closest(a, b)` — 最近接点対
`3D` · → `配列`

**実装**: `cgal.so` · 型: value

**入力** `a, b` 対象 — ともに `mesh`（3D）

**出力** — `配列` `[距離(スカラ), a上の点(3Dベクトル), b上の点(3Dベクトル)]`

- 例: `closest(a,b)[1]`（a 上の点）
- 関連: `distance`, `farthest`

### `farthest(a, b)` — 最遠点対
`3D` · → `配列`

**実装**: `cgal.so` · 型: value

頂点ペア総当り（距離値は厳密・大メッシュで重い O(|VA|·|VB|)）。

**入力** `a, b` 対象 — ともに `mesh`（3D）

**出力** — `配列` `[距離(スカラ), a上の点(3Dベクトル), b上の点(3Dベクトル)]`

- 関連: `closest`

### `thin_spots(m, t_min [, rays [, cone]])` — 肉厚解析（薄肉検出）
`3D` · → `配列`

**実装**: `cgal.so` · 型: value

**肉厚 SDF（Shape Diameter Function）**で「薄すぎて 3D プリントで割れる箇所」を位置つきで拾う。各面で内向きに錐状のレイ（全角 `cone`°・`rays` 本）を飛ばし、反対側の壁までの距離の加重平均＝その場所の**肉厚**を測り、`t_min` 未満の面だけを返す。値は絶対距離（モデル単位＝mm 等）。入力は閉じた三角形メッシュ前提（`valid` で前段確認可）。

🔑 **`cone`（コーン全角）が肝**。既定 **45°** は「壁にほぼ垂直方向の肉厚」を測るので、ダクトや壁の**角（複数の薄壁が収束する所）でも過小評価しない**。CGAL 既定の **120°** は広角で形状診断向きだが、角で周囲の壁を拾って 1.5mm 壁を 0.6mm 等と**過小評価**する（＝偽陽性）。角の誤検出が気になるなら 30〜45°、なだらかな曲面の薄肉も拾いたいなら広めに。

⚙️ **マルチスレッド並列**（面ごと独立・AABB ツリー共有）。10 万面級でも数秒（24 コアで約 8 万面 ≈ 1 秒）。計算量 ≈ 面数 × `rays`。`rays` を下げると速いが取りこぼす。

**入力**
- `m` 対象 — `mesh`（3D）
- `t_min` 肉厚しきい値 — `スカラ`（これ未満の面を危険とみなす）
- `rays` 面ごとのレイ本数 — `整数`（任意・既定 `25`）。大＝正確で遅い／小＝速いが取りこぼす
- `cone` コーン全角（度）— `スカラ`（任意・既定 `45`）。小＝垂直方向の真の肉厚（角の偽陽性が減る）／大＝広角 SDF

**出力** — `配列` `[[x, y, z, thk], ...]`（各危険点の面重心 `x,y,z` と肉厚 `thk`）。危険箇所が無ければ空配列 `[]`。

- 例（可視化・stdlib inspect）: `var m = ...; m +++ thin_markers(m, 1.0, 0.5)`
- 例（角の偽陽性を抑える・既定）/（広角で診断）: `thin_spots(part, 1.0, 25, 45)` / `thin_spots(part, 1.0, 25, 120)`
- 関連: `valid`, `thin_markers`〔stdlib: inspect〕

### `thin_markers(solid, t_min, r)` — 薄肉箇所の球マーカ  〔stdlib: inspect〕
`3D` · → `mesh`

**実装**: `include "std/inspect.sra"` · 型 -

`thin_spots(solid, t_min)` の各危険点に半径 `r` の球を置いて `combine`(+++)した mesh(可視化用なので corefinement しない=球が多くても重くならない) を返す。元モデルと `+++`（combine）で重ねると、割れそうな場所が viewer で一目で分かる。危険箇所が無ければ空（`{}`）。

**入力**
- `solid` 対象 — `mesh`（3D）
- `t_min` 肉厚しきい値 — `スカラ`
- `r` マーカ球の半径 — `スカラ`

**出力** 球マーカの集合 — `mesh`

- 例: `m +++ thin_markers(m, 1.0, 0.5)`
- 関連: `thin_spots`, `thin_spots_band`

### `thin_spots_band(solid, t_lo, t_hi)` — 帯域で薄肉抽出  〔stdlib: inspect〕
`3D` · → `配列`

**実装**: `include "std/inspect.sra"` · 型 -

`thin_spots` は 0〜`t_hi` を全部拾うため、ブール演算の許容差（例: `margin_t=0.01`）由来の**印刷できない極薄スリバー**まで混ざる。`t_lo` に「印刷可能な下限」を入れてそのノイズを落とし、**`t_lo` 以上 `t_hi` 未満**の危険点だけを返す。設計公称肉厚より薄い“本当の問題箇所”だけを切り出すのに使う。

**入力**
- `solid` 対象 — `mesh`（3D）
- `t_lo` 下限肉厚 — `スカラ`（これ未満＝印刷不能スリバーとして捨てる）
- `t_hi` 上限肉厚 — `スカラ`（これ以上＝十分な肉厚として除外）

**出力** — `配列` `[[x, y, z, thk], ...]`（`t_lo ≤ thk < t_hi` の危険点）

- 例: `thin_spots_band(m, 0.5, 1.0)`（0.04mm スリバーを無視し、設計 1.5mm より薄い 0.5〜1.0mm だけ）
- 関連: `thin_spots`, `thin_markers_band`

### `thin_markers_band(solid, t_lo, t_hi, r)` — 帯域薄肉の球マーカ  〔stdlib: inspect〕
`3D` · → `mesh`

**実装**: `include "std/inspect.sra"` · 型 -

`thin_spots_band(solid, t_lo, t_hi)` の各点に半径 `r` の球を置いて `combine`(+++)。スリバーノイズを除いた薄肉だけを可視化する版。`m +++ thin_markers_band(m, 0.5, 1.0, 0.4)` のように重ねる。

**入力**
- `solid` 対象 — `mesh`（3D）
- `t_lo` 下限肉厚 — `スカラ`（これ未満＝印刷不能スリバーとして無視）
- `t_hi` 上限肉厚 — `スカラ`（これ以上＝十分な肉厚として除外）
- `r` マーカ球の半径 — `スカラ`

**出力** 球マーカの集合 — `mesh`（帯域内の危険点が無ければ空 `{}`）

- 例: `m +++ thin_markers_band(m, 0.5, 1.0, 0.4)`
- 関連: `thin_markers`, `thin_spots_band`

---

## 配列・数値ユーティリティ

planner 側 op（agent 不要・CGAL に触れない）。

### `length(x)` — 要素数
`配列・文字列` · → `整数`

**実装**: 組み込み · 型 -

配列 / ハッシュの要素数。それ以外はエラー。

**入力** `x` 対象 — `配列` または `ハッシュ`

**出力** 要素数 — `整数`

- 例: `length([1,2,3])` → `3`

### `float(x)` — 浮動小数へ変換
`文字列・整数・浮動小数` · → `浮動小数`

**実装**: 組み込み · 型 -

値を浮動小数へ変換する。**文字列**は数値としてパース、**整数**は昇格、**浮動小数**はそのまま。配列 / ハッシュはスカラでないためエラー。planner 側で評価(agent 不要)。

**入力** `x` 対象 — `文字列` / `整数` / `浮動小数`

**出力** — `浮動小数`

- 整数を浮動小数化すると除算が浮動小数除算になる: `float(7)/2` → `3.5`(素の `7/2` は整数除算で `3`)。
- 例: `float("3.14")` → `3.14` / `float("42")` → `42`(値は浮動小数)/ `float("1.5") + 2` → `3.5`

### `int(x)` — 整数へ変換
`文字列・浮動小数・整数` · → `整数`

**実装**: 組み込み · 型 -

値を整数へ変換する。**文字列**は数値としてパース、**浮動小数**は 0 方向へ**切り捨て**、**整数**はそのまま。配列 / ハッシュはスカラでないためエラー。planner 側で評価(agent 不要)。

**入力** `x` 対象 — `文字列` / `浮動小数` / `整数`

**出力** — `整数`

- 切り捨て(丸めではない): `int(3.9)` → `3`、`int(-2.7)` → `-2`。
- 例: `int("42")` → `42` / `int("7") + 1` → `8` / `int(float("3.5"))` → `3`

### `concat(a, b, …)` — 連結
`配列` · → `配列`

**実装**: 組み込み · 型 -

配列引数は要素展開、非配列引数は 1 要素として追加。

**入力** `a, b, …` 連結対象（任意個） — `配列` または 任意の値

**出力** 連結結果 — `配列`

- 例: `concat([1,2],[3,4],5)` → `[1,2,3,4,5]`

### `map(arr, fn)` — 写像
`配列` · → `配列`

**実装**: 組み込み · 型 -

各要素に `fn` を適用した新配列（長さ不変・reduce しない）。各要素は遅延＝並列。

**入力**
- `arr` 入力配列 — `配列`
- `fn` 写像関数 — `関数`（`\(e){…}` または `\(e,i){…}`・`e`=要素, `i`=添字）

**出力** 写像結果 — `配列`（各要素の型は `fn` 次第。mesh を返せば `mesh 配列`）

- 例: `map([10,20,30], \(p,i){ p+i*100; })` → `[10,120,230]`
- 例: `union(map(positions, \(p){ box(1,1,1) >>> p; }))`（インスタンス化→畳み込み）
- 関連: `union`(配列), layout 各関数

### `transpose(arr)` — 転置
`配列` · → `配列`

**実装**: 組み込み · 型 -

矩形「配列の配列」を入替 `[n][m]→[m][n]`。座標列↔点列の変換（曲線生成の核）。

**入力** `arr` 矩形 2 次元配列 — `配列`（`点列`/列の配列）

**出力** 転置 — `配列`（`点列`）

- 例: `transpose([cos(t)*r, sin(t)*r])` → 点列

### `cumsum(arr)` — 累積和
`配列` · → `配列`

**実装**: 組み込み · 型 -

`[a0, a0+a1, …]`（同長・浮動小数）。数値積分の核。

**入力** `arr` 数値列 — `配列`（数値）

**出力** 累積和 — `配列`（同長）

- 例: `cumsum([1,2,3])` → `[1,3,6]`

### `sum(arr)` — 総和
`配列` · → `スカラ`

**実装**: 組み込み · 型 -

**入力** `arr` 数値列 — `配列`（数値）

**出力** 総和 — `スカラ`

- 例: `sum([1,2,3])` → `6`

### `print(x, …)` — 表示
任意 · → 最後の引数

**実装**: 組み込み · 型 -

各引数を stdout に 1 行表示し、最後の値を返す（passthrough）。mesh は計算完了を待ってキャッシュパスを表示。

**入力** `x, …` 表示対象（任意個） — 任意の値（`mesh` も可）

**出力** 最後の引数の値（そのまま）

- 例: `var m = print(box(2,2,2));`（途中デバッグ）
- ⚠ `print(…, volume(m), bbox(m))` のように独立な計測を並べても、引数は**左から1つずつ評価**され**直列**になる（並列にしたいなら引数を配列 `[..]` にして評価するか `async` を使う）。

### `print_async(x, …)` — 非ブロッキング表示
任意 · → `null`

**実装**: 組み込み · 型 -

`print` の非ブロッキング版。引数を**並列起動**して即リターンし、**周りの並列性を妨げない**。出力は「**発行順**を保ちつつ、**前の `print_async` が出力済み かつ 自分の引数が揃った**」時点で行われる（準備でき次第・末尾まで溜め込まない）。重い計測を多数ログしたいが本流を止めたくないときに。

**入力** `x, …` 表示対象（任意個・`mesh`/値）

**出力** `null`（即リターン）

- **発行順は厳守**：先に出した `print_async` が（引数が遅くても）必ず先に出る。`export_async` の print 版。
- 出力タイミングは「前が出た後＋自分の引数が揃った後」。`print`（同期・その場で出力）とは混ざる順序が直感とずれることがある。
- プログラム末尾で未出力分は自動で出し切る（発行順）。
- **実体は `async` のシュガー**：`print_async(a, b)` は `async { var t = [a, b]; sync: print(t[0], t[1]); }` に展開される（引数を `[..]` で並列評価し、`sync:` で発行順に出力）。
- 例: `print_async("part vol", volume(part)); export_async("p.stl", part); /* 本流は止まらない */`
- 関連: `print`, `async`, `export_async`, `gate`

### `async { … }` / `async { … sync: STMT }` — 並列な制御文
文 · → （文・値は返さない）

**実装**: 組み込み · 型 -

ブロックを**非ブロッキング**に起動し、本流を止めずに次へ進む。複数の `async` ブロックは**互いに並列**に走る。ブロック内部は通常の `{}` と同じく**直列**で、**スコープも共有**（`body` の `var` を `sync:` 文から参照できる）。`sync:`（省略可・**ブロックの最終文に 1 つ**）を付けると、その文の**実行（出力・副作用）だけ**が全 `async` を跨いで**ソース出現順**に整列する（重い計算は並列のまま）。`print_async`/`export_async`（および旧 `par`）を畳む統一プリミティブ。

**入力** ブロック内の文の並び（+ 省略可 `sync:` 文）

**出力** なし（即リターン。完了はプログラム末尾の drain か `flush()` で待つ）

- **約束**：制御文（`{}`/`if`/`while`/`for`）は直列、関数引数と `[..]` は並列、並列な制御は `async`。
- `sync:` は**順序付き出力**であって順序付き実行ではない。`body` の計算は並列、`sync:` 文の**出力**だけが順序付く。
- `sync:` 文は `body` の**後**に走るので、`async { export("a.stl", m); sync: print("saved a"); }` の "saved a" は **a.stl 書き込み完了後**に出る（`export_async`+`gate` の「計算完了 ≠ 書込完了」問題が無い）。
- **エラーは continue-and-collect**：`body`/`sync:` でエラーが出ても他の `async` は止まらず、エラーは末尾でまとめて報告され終了コードに反映（当該 `async` の `sync:` はスキップ）。チェーンは必ず前進するのでデッドロックしない。
- 例（並列計算・順序表示）: `async { var v = volume(m); sync: print("vol", v); }` を複数並べる。
- 例（`par` の代替）: `var ab = [volume(part), bbox(part)]; print("vol", ab[0], "bbox", ab[1]);`（`[..]` が並列評価）
- 関連: `print_async`, `export_async`, `flush`, `gate`

> **`par` は撤去**：`par(a,b,c)` は配列リテラル `[a,b,c]` と同義。配列リテラル `[..]` が全要素を並列評価するので、`par` の代わりに `[..]` を使う。

### `gate(inp1, inp2)` — 完了フック
任意 · → `inp1`

**実装**: 組み込み · 型 -

`inp1` を**そのまま返す**（計算は変えない pass-through）。一方で、`inp1` の**計算が完了した時点**で `inp2` を1回だけ評価する（副作用のみ・値は捨てる）。ある計算の**完了通知/フック**用。

**入力** `inp1` 値を通す対象（mesh でも値でも可）／`inp2` 完了時に走らせる式（`print` 等）

**出力** `inp1`（と同一・gate を挟んでも結果・並列性は変わらない）

- **起動時ではなく完了時**に発火する（mesh 継続の `cdr→cdr` 解決を待つ）。`inp1` を非ブロッキングに通すため内部は tinyState ヘルパ。
- 例: `union(gate(box(1,1,1), print("box1 done")), box(2,2,2))` → box1 の計算完了時に表示。
- `inp1` を誰も使わない（forced されない）と `inp1` は計算されず `inp2` も発火しない（遅延評価のまま）。
- 関連: `async`, `print`

---

## 初等関数

カーネル組込・planner 側・**ベクトル化**（配列は要素ごと）・角度は**ラジアン**。結果は浮動小数。
**入力・出力ともに `スカラ` または `配列`（同型）**。配列同士は zip、スカラは broadcast。

| 1 引数 `f(x)` | 意味 | | 2 引数 `f(a, b)` | 意味 |
|---|---|---|---|---|
| `sin(x)` | サイン | | `atan2(y, x)` | 2 引数逆正接 |
| `cos(x)` | コサイン | | `pow(b, e)` | べき乗 b^e |
| `tan(x)` | タンジェント | | `mod(a, b)` / `a % b` | 剰余（fmod） |
| `asin(x)` `acos(x)` `atan(x)` | 逆三角 | | `min(a, b)` | 最小 |
| `sqrt(x)` | 平方根 | | `max(a, b)` | 最大 |
| `exp(x)` `log(x)` | 指数・自然対数 | | | |
| `abs(x)` `sign(x)` | 絶対値・符号 | | | |
| `floor(x)` `ceil(x)` `round(x)` | 床・天井・丸め | | | |

- 入力 `x`（`a`,`b`,`y`,`e`）= `スカラ` または `配列` / 出力 = 同型。
- 配列対応: `sqrt([1,4,9])` → `[1,2,3]`、`atan2([1,1],[1,0])` → `[π/4, π/2]`。
- 例: `sin(rad(30))` → `0.5`
- 定数 `PI`/`TAU`/`E`・`rad`/`deg`・`range`/`linspace` は `include "std/math.sra";`（[stdlib: math](#stdlib-math数学定数ベクトル行列)）。

---

## I/O・システム {#io}

### `export(path, mesh[, unit])` ／ `export(mesh)` — 書き出し
`2D・3D` · → `mesh`/`null`

**実装**: `cgal.so` + `manifold.so` · 型: value（cgal は cg-/mf- 全型を引受＝universal reader）

mesh をファイルへ書き出し（形式は拡張子で自動判別）。引数 1 個は passthrough（書き出さず継続を値化）。

**入力**
- `path` 出力パス — `文字列`
- `mesh` 対象 — `mesh`
- `unit` 単位 — `文字列`（`"mm"`/`"cm"`/`"m"`/`"in"`/`"ft"`/`"micron"`…・**AMF/3MF/SVG/DXF が使用**・OFF/STL/OBJ/PLY は無視・省略可）

manifold.so が直接書けるのは `stl` / `off` / **`3mf` / `amf`**（3MF/AMF は cgal.so と同じ共通ライタ `src/h/common/mesh3mf.h`・色と単位を保持）。それ以外（obj/ply/svg/dxf…）は cgal.so が引き受ける。

**出力** — 引数 1 個版は `mesh`（passthrough）、書き出し版は `null`

- 形式: 3D＝`.off`/`.stl`/`.obj`/`.ply`/`.amf`/`.3mf` / 2D＝`.svg`(入出力)/`.dxf`(出力のみ)。次元と拡張子の不一致はエラー
- **単位を埋め込みたいなら AMF か 3MF**: STL/OFF/OBJ/PLY は座標値のみで**単位情報を持たない**（スライサが慣習で mm 解釈）。`.amf`（XML）も `.3mf`（XML を zip で固めた OPC）も `unit` 引数を単位属性に刻む（既定 `millimeter`）。**どちらも自前実装で外部ライブラリ依存なし＝Linux/macOS 問わず同じ `.amf`/`.3mf` 一発で出る**。3MF はスライサ（PrusaSlicer/Cura 等）が直接読める実寸つきフォーマット
- 例: `export("part.stl", box(40,55,9))` / `export("part.3mf", part)` / `export("part.amf", part, "mm")` / `export("plan.svg", sketch, "mm")`
- 関連: `export_async`, `import`

### `export_async(path, mesh[, unit])` — 非ブロッキング書き出し
`2D・3D` · → `null`

**実装**: 組み込み · 型 -

`export` の非ブロッキング版。起動だけして即リターン → 複数の書き出しが並列に走る。完了は `flush()` かプログラム末尾で待つ。

**入力** `path`(`文字列`), `mesh`(`mesh`), `unit`(`文字列`・省略可) — `export` と同じ

**出力** — `null`

- **実体は `async` のシュガー**：`export_async(path, m)` は `async { export(path, m); }`（`sync:` 無し）に展開される。完了待ちは `flush()`／末尾 drain（`async` と共通の機構）。
- 例: `export_async("a.svg", partA); export_async("b.svg", partB); flush();`
- 関連: `flush`, `export`, `async`

### `export_vox(path, params, mesh…)` — ボクセル化して vox.h5 を書く
`3D` · → `null`

**実装**: `cgal.so` · 型: value（HDF5 vox.h5）

複数の領域メッシュを共通の Cartesian 格子へボクセル化し、各領域を名前付きマスクとして中立フォーマット **vox.h5**（格子 + マスク）へ書き出す。k-Wave 等の格子ソルバ連携用（→ [シミュレーション（k-Wave）](srava_kwave.html)）。

**入力**
- `path` 出力パス（`.h5`）— `文字列`
- `params` — `ハッシュ`：`{ dx, pad, regions }`
  - `dx`（必須）格子ピッチ（mesh と同じ単位）
  - `pad`（既定 8）形状の bounding box 外側に足すボクセル数（PML 余白用）
  - `regions` 領域メタ配列 `[{name, side}, …]`。`name`=マスク名、`side`=`"inside"`（メッシュ内部）/`"outside"`（外部）。`regions[i]` が `i` 番目のメッシュに対応
- `mesh…` 領域メッシュ（**可変個の位置引数**・`regions` と同順）— `mesh`

**出力** — `null`（副作用で vox.h5 を書く）

- 格子は全メッシュの**共通 bbox + pad·dx**、ピッチ `dx` で自動決定（`origin`=セル(0,0,0)中心）。各セル中心を内外判定（z-パリティ法）して `side` で焼く。
- **メッシュは位置引数**（`combine` と同じく）。srava ではメッシュ（cache）と値（ハッシュ/配列）が別経路で渡るため、`regions` の中にメッシュは入れられない。
- vox.h5 スキーマ: `Nx/Ny/Nz`(int64)・`dx/dy/dz`(float64)・`origin`(float64[3])・`masks/<name>`(uint8[Nx,Ny,Nz])。属性 `format="srava-vox"`。
- 例:
  ```
  export_vox("vox.h5",
     { dx: 1.0, pad: 8, regions: [ {name:"air", side:"inside"}, {name:"wall", side:"outside"} ] },
     air_mesh, wall_mesh);
  ```
- 関連: [シミュレーション（k-Wave）](srava_kwave.html), `tube`, `export`

### `flush()` — 書き出しバリア
— · → `null`

**実装**: 組み込み · 型 -

未完了の `export_async` をその地点で全部待つ。出力ファイルを読む `system`/`import` の直前に置く。

**入力** なし

**出力** — `null`

- 関連: `export_async`

### `import(path)` — 読み込み
`2D・3D` · → `mesh`

**実装**: `cgal.so` + `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)

外部メッシュを DAG の葉に読み込む（`(path,size,mtime)` でキャッシュ）。失敗は明示エラー。

**入力** `path` 入力パス — `文字列`

**出力** 読み込んだ幾何 — `mesh`

- 例: `import("ref.stl")`
- 関連: `export`, `include`

### `include "path";` — コード取り込み
文 · → —

**実装**: 組み込み · 型 -

別の srava スクリプト（定義）を字句的に取り込む（C の `#include` 相当）。ライブラリ読込用。`import`（幾何）とは別物。

**入力** `path` 取り込むファイル — `文字列リテラル`（**文**として書く。式ではない）

**出力** なし（定義が同一 env に展開される）

- 探索: ①取り込み元 dir → ②`$SRAVA_PATH` → ③そのまま。多重 include は自動スキップ
- 例: `include "std/layout.sra";`
- 関連: `import`

### `system(cmd)` — シェル実行
`文字列` · → `整数`

**実装**: 組み込み · 型 -

シェルコマンドを非同期実行（イベントループを塞がない）・完了まで待つ・終了コードを返す。

**入力** `cmd` コマンド — `文字列`

**出力** 終了コード — `整数`（0=成功）

- 例: `system("mkdir -p out"); var rc = system("convert a.svg a.png");`

### `exit msg;` ／ `exit;` — プログラム終了
文 · → —

**実装**: 組み込み · 型 -

その地点でプログラムを**正常終了**（exit code 0）する。`msg`（省略可）があれば `[srava] exit: <msg>` を stderr に出す。ガード節（早期リターン）用。

**入力** `msg` 終了メッセージ — 任意の値（`get_str` で文字列化・**省略可**）

**出力** なし（プログラムが終了する）

- **文**であり式ではない（値を返さない・`;` で終える）。括弧は任意（`exit "done";` も `exit("done");` も可）。
- **エラーではない**ので終了コードは 0。先行する `export_async` は終了前に drain され、キャッシュ掃除も通常通り走る（`is_error` で中断する fatal エラーとは別経路）。
- 典型: 処理対象が空のときに後段（`combine([])` 等）でエラーにせず、メッセージを出して畳む。
- 例:
  ```
  var hits = thin_spots(part, 1.0);
  if (length(hits) == 0) exit("薄肉なし: 出力をスキップ");
  export("markers.stl", combine(map(hits, \(h){ sphere(0.5) >>> h; })));
  ```
- 関連: `print`, `system`

---

## stdlib: math（数学定数・ベクトル・行列）

`include "std/math.sra";`。点＝数値配列。すべて「配列を左に」書く。

### `PI` ／ `TAU` ／ `E` — 定数  〔stdlib: math〕
`スカラ`

**実装**: `include "std/math.sra"` · 型 -

`PI`=π / `TAU`=2π（一周）/ `E`=e。**値**（引数なし）。

### `rad(d)` ／ `deg(r)` — 角度変換  〔stdlib: math〕
`スカラ・配列` · → 同型

**実装**: `include "std/math.sra"` · 型 -

**入力** `d`（度）/ `r`（ラジアン） — `スカラ` または `配列`

**出力** 変換後 — 同型

- 例: `sin(rad(30))` → `0.5`

### `range(n)` — 整数列  〔stdlib: math〕
`配列`

**実装**: `include "std/math.sra"` · 型 -

**入力** `n` 個数 — `整数`

**出力** `[0, 1, …, n-1]` — `配列`（整数）

- 例: `range(4)` → `[0,1,2,3]`

### `range2(lo, hi)` — 整数列（範囲）  〔stdlib: math〕
`配列`

**実装**: `include "std/math.sra"` · 型 -

**入力** `lo, hi` 範囲（半開） — `整数`

**出力** `[lo, …, hi-1]` — `配列`（整数）

### `linspace(lo, hi, n)` — 等間隔サンプル  〔stdlib: math〕
`配列`

**実装**: `include "std/math.sra"` · 型 -

両端含む n 個の等間隔値。曲線の媒介変数生成に。

**入力** `lo, hi` 範囲端（`スカラ`）, `n` 個数（`整数`）

**出力** 等間隔値 — `配列`（n 個）

- 例: `linspace(0, 1, 5)` → `[0,0.25,0.5,0.75,1]`

### `vadd(a, b)` ／ `vsub(a, b)` — ベクトル和/差  〔stdlib: math〕
`配列` · → `配列`

**実装**: `include "std/math.sra"` · 型 -

**入力** `a, b` — `ベクトル`（同次元）

**出力** 要素ごとの和/差 — `ベクトル`

### `vscale(a, s)` — ベクトルスカラ倍  〔stdlib: math〕
`配列` · → `配列`

**実装**: `include "std/math.sra"` · 型 -

**入力** `a`（`ベクトル`）, `s` 倍率（`スカラ`・配列なら軸別）

**出力** — `ベクトル`

### `vdot(a, b)` — 内積  〔stdlib: math〕
`配列` · → `スカラ`

**実装**: `include "std/math.sra"` · 型 -

**入力** `a, b` — `ベクトル`（同次元）

**出力** `Σ aᵢbᵢ` — `スカラ`

### `vlen(a)` — ノルム  〔stdlib: math〕
`配列` · → `スカラ`

**実装**: `include "std/math.sra"` · 型 -

**入力** `a` — `ベクトル`

**出力** `|a|` — `スカラ`

### `vnorm(a)` — 単位ベクトル  〔stdlib: math〕
`配列` · → `配列`

**実装**: `include "std/math.sra"` · 型 -

**入力** `a` — `ベクトル`

**出力** `a / |a|` — `ベクトル`

### `reverse(a)` — 逆順  〔stdlib: math〕
`配列` · → `配列`

**実装**: `include "std/math.sra"` · 型 -

**入力** `a` — `配列`

**出力** 逆順 — `配列`

- 例: `reverse([1,2,3])` → `[3,2,1]`

### `slice(a, lo, hi)` — 部分配列  〔stdlib: math〕
`配列` · → `配列`

**実装**: `include "std/math.sra"` · 型 -

`a[lo]…a[hi-1]` を取り出す（**lo 以上 hi 未満**の半開区間・`range2` と同じ規約）。範囲は `[0, length(a)]` にクランプ、`lo >= hi` なら `[]`。

**入力** `a` — `配列` / `lo`,`hi` 範囲 — `整数`（半開 `[lo, hi)`）

**出力** 部分配列 — `配列`

- 例: `slice([10,11,12,13,14], 1, 4)` → `[11,12,13]`（`ary[10..20]` 相当・上端は含まない）
- 関連: `range2`, `concat`, `reverse`

### `matvec(M, p)` — 行列×ベクトル  〔stdlib: math〕
`配列` · → `配列`

**実装**: `include "std/math.sra"` · 型 -

`result[i] = M[i]·p`。

**入力** `M` 行列（`行列`＝行ベクトルの配列）, `p`（`ベクトル`）

**出力** — `ベクトル`

- 関連: `rotmat2`, `rotate_pts`

### `rotmat2(th)` — 2D 回転行列  〔stdlib: math〕
`配列`

**実装**: `include "std/math.sra"` · 型 -

**入力** `th` 角度 — `スカラ`（ラジアン）

**出力** 2×2 回転行列 — `行列`

- 例: `rotate_pts(ps, rotmat2(rad(30)))`

### `rotmat_x(th)` ／ `rotmat_y(th)` ／ `rotmat_z(th)` — 3D 軸回転行列  〔stdlib: math〕
`配列`

**実装**: `include "std/math.sra"` · 型 -

**入力** `th` 角度 — `スカラ`（ラジアン）

**出力** 3×3 回転行列 — `行列`

- 例: `rotate_pts(ps, rotmat_z(rad(30)))`

---

## stdlib: curve（曲線生成・点列）

`include "std/curve.sra";`。返り値は**点列** `[[x,y],…]` / `[[x,y,z],…]` → `polygon`/`line`/`tube`/`extrude`/`revolve` に流せる。

### `arc(cx, cy, r, a0, a1, segs)` — 円弧（中心指定）  〔stdlib: curve〕
`2D` · → `点列`

**実装**: `include "std/curve.sra"` · 型 -

中心 (cx,cy)・半径 r・角 a0→a1（ラジアン）の円弧。

**入力**
- `cx, cy` 中心 — `スカラ`
- `r` 半径 — `スカラ`
- `a0, a1` 始角・終角（ラジアン） — `スカラ`
- `segs` 分割数 — `整数`

**出力** 円弧の点列（segs+1 点） — `点列`（2D ベクトルの配列）

- 例: `line(arc(0,0, 10, 0, rad(90), 24))`
- 関連: `arc_tan`

### `arc_tan(p0, p1, t0, segs)` ／ `arc_start_tan(…)` — 円弧（始点接ベクトル）  〔stdlib: curve〕
`2D・3D` · → `点列`

**実装**: `include "std/curve.sra"` · 型 -

始点 p0・終点 p1 を通り、p0 で接ベクトル t0 に接する円弧。t0 の向きに p1 まで掃く。3D は両端＋接線で平面が決まる。直線退化は `lerp_pts` にフォールバック。

**入力**
- `p0` 始点 — `2D/3D ベクトル`
- `p1` 終点 — `2D/3D ベクトル`
- `t0` 始点接ベクトル（向きのみ・正規化不要） — `2D/3D ベクトル`
- `segs` 分割数 — `整数`

**出力** 円弧の点列（segs+1 点） — `点列`（2D/3D ベクトルの配列）

- 例: `line(arc_tan([0,0],[40,20],[1,0], 24))`
- 関連: `arc_end_tan`, `arc`, `lerp_pts`

### `arc_end_tan(p0, p1, t1, segs)` — 円弧（終点接ベクトル）  〔stdlib: curve〕
`2D・3D` · → `点列`

**実装**: `include "std/curve.sra"` · 型 -

終点 p1 で接ベクトル t1 に接する版（p1→p0 を −t1 で掃いて反転）。

**入力**
- `p0` 始点 — `2D/3D ベクトル`
- `p1` 終点 — `2D/3D ベクトル`
- `t1` **終点**接ベクトル（向きのみ） — `2D/3D ベクトル`
- `segs` 分割数 — `整数`

**出力** 円弧の点列（segs+1 点） — `点列`

- 関連: `arc_tan`

### `bezier(ctrl, segs)` — ベジエ曲線  〔stdlib: curve〕
`2D・3D` · → `点列`

**実装**: `include "std/curve.sra"` · 型 -

制御点列 ctrl のベジエ曲線（次数＝`length(ctrl)-1`）。各座標を独立に Bernstein 評価するので**点の次元をそのまま保つ**（`[[v],…]` の 1D 制御点ならスカラ補間）。

**入力**
- `ctrl` 制御点列 — `点列`（各制御点は同次元の `ベクトル`）
- `segs` 分割数 — `整数`

**出力** 曲線の点列（segs+1 点） — `点列`（制御点と同次元）

- 例: `tube(map(bezier([[0,0,0],[2,2,0],[4,0,2]], 16), \(p){ [p, 0.3]; }))`
- 関連: `spline`, `tube`

### `spline(ctrl, segs)` — Catmull-Rom スプライン  〔stdlib: curve〕
`2D・3D` · → `点列`

**実装**: `include "std/curve.sra"` · 型 -

制御点を必ず通る曲線。各区間 segs+1 点を連結（端は clamp・区間境界点は重複＝`tube`/`line` は許容）。

**入力**
- `ctrl` 制御点列 — `点列`（各点は同次元の `ベクトル`）
- `segs` 区間ごとの分割数 — `整数`

**出力** 曲線の点列 — `点列`

- 関連: `bezier`

### `clothoid(k0, rate, L, segs)` — オイラー螺旋  〔stdlib: curve〕
`2D` · → `点列`

**実装**: `include "std/curve.sra"` · 型 -

曲率 κ(s)=k0+rate·s を弧長 L まで前進積分（緩和曲線）。初期方位 0・**原点付近**始まり（inclusive cumsum で先頭が 1 ステップ進む）。

**入力**
- `k0` 初期曲率 — `スカラ`
- `rate` 曲率変化率（弧長あたり） — `スカラ`
- `L` 全弧長 — `スカラ`
- `segs` 分割数 — `整数`

**出力** 螺旋の点列（segs+1 点） — `点列`（2D ベクトルの配列）

- 関連: `cumsum`

### `lerp_pts(p0, p1, segs)` — 直線点列  〔stdlib: curve〕
`2D・3D` · → `点列`

**実装**: `include "std/curve.sra"` · 型 -

**入力** `p0, p1` 端点（`2D/3D ベクトル`）, `segs` 分割数（`整数`）

**出力** p0→p1 の直線上 segs+1 点 — `点列`

- 関連: `arc_tan`

### `translate_pts(pts, v)` — 点列の平行移動  〔stdlib: curve〕
`2D・3D` · → `点列`

**実装**: `include "std/curve.sra"` · 型 -

各点に v を加える（mesh の `translate` とは別・点列専用）。

**入力** `pts` 点列（`点列`）, `v` 移動量（`2D/3D ベクトル`）

**出力** 移動後の点列 — `点列`

- 例: `translate_pts(ps, [10,0])`

### `scale_pts(pts, s)` — 点列の拡大縮小  〔stdlib: curve〕
`2D・3D` · → `点列`

**実装**: `include "std/curve.sra"` · 型 -

**入力** `pts` 点列（`点列`）, `s` 倍率（`スカラ`＝一様 / `ベクトル`＝軸別）

**出力** 拡縮後の点列 — `点列`

- 例: `scale_pts(ps, 2.0)` / `scale_pts(ps, [2,1])`

### `rotate_pts(pts, M)` — 点列の回転  〔stdlib: curve〕
`2D・3D` · → `点列`

**実装**: `include "std/curve.sra"` · 型 -

回転行列 M を各点へ適用（mesh の `rotate` とは別）。

**入力** `pts` 点列（`点列`）, `M` 回転行列（`行列`＝`rotmat2`/`rotmat_x/y/z`）

**出力** 回転後の点列 — `点列`

- 例: `rotate_pts(ps, rotmat2(rad(30)))`
- 関連: `matvec`, `rotmat2`

### `ribbon2d(pts, w)` — 定幅 2D 帯  〔stdlib: curve〕
`2D` · → `mesh`

**実装**: `include "std/curve.sra"` · 型 -

2D 折れ線を一定幅 w で太らせた帯（丸ジョイント/丸キャップ）。`tube` に半幅 `w/2` を渡す薄いラッパ。可変幅は `tube` を直接。

**入力** `pts` 折れ線（`点列`＝2D ベクトルの配列・半径なし）, `w` 全幅（`スカラ`）

**出力** 帯領域 — `mesh`（2D）

- 例: `export("trace.svg", ribbon2d(bezier([[0,0],[20,20],[40,0]], 16), 5), "mm")`
- 関連: `tube`

### `arclen(pts)` — 頭からの累積弧長  〔stdlib: curve〕
`2D・3D` · → `配列`

**実装**: `include "std/curve.sra"` · 型 -

点列の各点までの累積弧長を返す。`arclen[0]=0`・`arclen[i]=Σ|pₖ−pₖ₋₁|`・末尾＝全長 L。長さは `pts` と同じ。2D/3D 共通（`vlen` が次元非依存）。隣接差分→`cumsum` で積分。

**入力** `pts` 点列 — `点列`（`[[x,y(,z)],…]`）

**出力** 累積弧長 — `配列`（先頭 0・末尾 L・要素数 = `length(pts)`）

- 全長だけなら `arclen(pts)[length(pts)-1]`。媒介変数 → 弧長の対応付けや等弧長リサンプルに。
- 例: `arclen([[0,0],[3,0],[3,4]])` → `[0,3,7]`
- 関連: `cumsum`, `vlen`, `bezier`

### `tube_wall(path, d)` ／ `tube_wall_var(path, ds)` — パイプ壁オフセット  〔stdlib: curve〕
`3D` · → `tube パス`（`[[v,r],…]`）

**実装**: `include "std/curve.sra"` · 型 -

`tube` 用の `[v,r]` 列（`v`=中心線点・`r`=半径）を、面に**垂直距離 d** だけ外側へオフセットした新しい `[v,r]` 列にする。元の管と引き算したとき、残る**壁厚が一様に d** になるよう半径変化（テーパ）を補正する。

**入力**
- `path` — `tube パス`（`[[[x,y,z], r], …]`・2 点以上・節は相異なる）
- `d`（`tube_wall`）肉厚 — `スカラ`（**負で内側**へオフセット＝内壁）
- `ds`（`tube_wall_var`）節ごとの肉厚 — `配列`（`path` と同じ要素数）

**出力** オフセット後の `tube パス` — `[[v',r'],…]`（要素数は `path` と同じ）

- 補正式: 単位接線 `t̂`・弧長微分 `r'=dr/ds` に対し `[v,r] → [v − d·r'/√(1+r'²)·t̂,  r + d/√(1+r'²)]`。
  `r'=0`（円筒）なら半径に `+d` するだけ。テーパ管では中心線を接線方向へ引いて壁を**垂直化**する（半径に素朴に `+d` すると斜面の壁は d より薄くなる）。
- 使い方（肉厚 d のシェル）: `difference(tube(tube_wall(path, d)), tube(path))`。可変肉厚は `tube_wall_var(path, ds)`。
- 開いた端では両管の長さが異なり端面に薄いリップが出る（垂直オフセットの性質）。必要なら端を平面でトリム/キャップする。
- 例: `var shell = difference(tube(tube_wall(path, 2)), tube(path));`
- 関連: `tube`, `arclen`, `difference`, `offset`

---

## stdlib: layout（mesh 配列レイアウト）

`include "std/layout.sra";`。`bbox`+`map`+`>>>` で実装。2D/3D 両対応。返り値は**配列**（まとめるなら `union(...)`）。

### `stack(arr, axis, gap)` — 軸並べ  〔stdlib: layout〕
`2D・3D` · → `mesh 配列`

**実装**: `include "std/layout.sra"` · 型 -

指定軸に、各 mesh の bbox 幅 ＋ gap で隙間を空けて並べる。

**入力**
- `arr` 対象 — `mesh 配列`
- `axis` 並べる軸 — `整数`（0=x, 1=y, 2=z）
- `gap` 隙間 — `スカラ`

**出力** 配置後 — `mesh 配列`

- 例: `union(stack(parts, 0, 5))`
- 関連: `row`, `column`, `grid`

### `row(arr, gap)` — 横並び  〔stdlib: layout〕
`2D・3D` · → `mesh 配列`

**実装**: `include "std/layout.sra"` · 型 -

X 軸に並べる（`stack(arr,0,gap)`）。

**入力** `arr`（`mesh 配列`）, `gap` 隙間（`スカラ`）

**出力** — `mesh 配列`

### `column(arr, gap)` — 縦並び  〔stdlib: layout〕
`2D・3D` · → `mesh 配列`

**実装**: `include "std/layout.sra"` · 型 -

Y 軸に並べる（`stack(arr,1,gap)`）。

**入力** `arr`（`mesh 配列`）, `gap` 隙間（`スカラ`）

**出力** — `mesh 配列`

### `grid(arr, cols, gap)` — グリッド配置  〔stdlib: layout〕
`2D・3D` · → `mesh 配列`

**実装**: `include "std/layout.sra"` · 型 -

cols 列のグリッド。**`gap` は格子のピッチ（原点間隔）**で、要素 i をそのまま格子点 `(c*gx, r*gy)` へ平行移動するだけ（bbox を一切見ない＝単純で予測しやすい）。**要素 0 が原点 (0,0)、行内は x が右へ・行が進むと y が上へ伸びる（第1象限）**。例: `grid(m, 2, 1)` → `m[0]>>>[0,0], m[1]>>>[1,0], m[2]>>>[0,1], m[3]>>>[1,1]`。`grid(m, 2, [1, 1.5])` → 行ピッチが 1.5（`m[2]>>>[0,1.5]`）。

**入力**
- `arr` 対象 — `mesh 配列`
- `cols` 列数 — `整数`
- `gap` 隙間 — `スカラ`（全軸同一）または `[gx, gy]`（軸別）

**出力** 配置後 — `mesh 配列`

- 例: `union(grid(parts, 4, 5))` / `grid(parts, 4, [3, 8])`（x=3, y=8）
- 関連: `grid3`

### `grid3(arr, cols, rows, gap)` — 3D グリッド配置  〔stdlib: layout〕
`2D・3D` · → `mesh 配列`

**実装**: `include "std/layout.sra"` · 型 -

cols 列(X)× rows 行(Y)で 1 層を埋め、**層は Z 方向に自動で伸ばす**（要素数が cols×rows を超えたら次の層へ）。**`gap` は格子のピッチ（原点間隔）**で、要素 i をそのまま格子点 `(c*gx, r*gy, L*gz)` へ平行移動するだけ（bbox を一切見ない）。**要素 0 が原点 (0,0,0)、x（行内）→ y（行）→ z（層）の順にいずれも正方向へ伸びる**。

**入力**
- `arr` 対象 — `mesh 配列`
- `cols` X 方向の列数 — `整数`
- `rows` Y 方向の行数 — `整数`
- `gap` 隙間 — `スカラ`（全軸同一）または `[gx, gy, gz]`（軸別）

**出力** 配置後 — `mesh 配列`

- 例: `union(grid3(parts, 3, 3, [2, 2, 5]))`
- 関連: `grid`

### `align(arr, axis, mode)` — 整列  〔stdlib: layout〕
`2D・3D` · → `mesh 配列`

**実装**: `include "std/layout.sra"` · 型 -

指定軸で全 mesh を一直線に揃える（他成分は保つ）。基準は先頭要素。

**入力**
- `arr` 対象 — `mesh 配列`
- `axis` 揃える軸 — `整数`（0=x, 1=y, 2=z）
- `mode` 揃え方 — `文字列`（`"min"` / `"center"` / `"max"`）

**出力** 整列後 — `mesh 配列`

- 例: `align(parts, 1, "center")`

---

## stdlib: guide（計測ガイド・ものさし）

`include "std/guide.sra";`。3D の三角形フォーマット（STL/3MF/OFF/AMF）はエッジ（細線）を表現できないので、計測ガイドは**細い `tube` ソリッド**として作り、`part +++ ruler(...)` で重ねる（`combine`＝corefinement なしで軽い）。決定的なのでキャッシュが効き、生成は初回のみ。

### `ruler(axis, len, step, r)` — ものさし（目盛つき直線ガイド）  〔stdlib: guide〕
`3D` · → `mesh`

**実装**: `include "std/guide.sra"` · 型 -

軸 `axis`（`0`=x / `1`=y / `2`=z）方向に長さ `len` の ものさし。原点から +`axis` 方向へ細い主線（tube）を引き、`step` 間隔で直交方向に短い目盛（tick）を出す。目盛長 ＝ `step*0.4`。

**入力**
- `axis` 軸 — `整数`（0/1/2）
- `len` 全長 — `スカラ`
- `step` 目盛間隔 — `スカラ`
- `r` 線の半径（細く） — `スカラ`

**出力** ものさし — `mesh`（3D・細い tube ソリッドの combine）

- 例: `var part = box(80,40,30); export("p.3mf", part +++ ruler(0, 80, 10, 0.4));`
- 3 軸ぶん: `part +++ ruler(0,L,s,r) +++ ruler(1,L,s,r) +++ ruler(2,L,s,r)`
- 関連: `tube`, `combine`, `line`（2D ガイド）

---

*このリファレンスは [言語リファレンス](srava_language_reference.html) の補助です。評価モデル・キャッシュ・
2D/3D ディスパッチ・エラー表示などの詳細はそちらを参照してください。*

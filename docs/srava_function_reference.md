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

### 分割数 `segs` と辺数 `n` の規約

分割数を取る op（`circle` / `sphere` / `cylinder` / `cone` / `torus` / `tube_ruled` / `revolve`）と、
辺数そのものを取る op（`ngon` / `prism` / `pyramid`）では、**3 未満を渡したときの意味が違う**。
違いは引数の役割から来る:

⚠ ここで言う「分割数を取る op」は **メッシュ系カーネル**のものです。`occt.so` は
解析曲面で作るので **`segs` という引数を持ちません**（下の ★ を参照）。

| 引数 | 役割 | 省略 | `0` | `1` / `2` / 負 | `3` 以上 |
|---|---|---|---|---|---|
| `segs` | **近似の細かさ**（既定値がある） | 既定値 | 既定値（＝省略と同じ） | **明示エラー** | その値 |
| `n` | **形そのもの**（既定値が無い） | — | **明示エラー** | **明示エラー** | その値 |

- 既定は多くの op で `segs=32`。
- エラー文言は `… : segments must be >= 3 (use 0 or omit it for the default)` /
  `… : n must be >= 3`。
- ★ `ngon(0)` がエラーで `circle(r,0)` が 32 角形なのは、`n` が形そのもので既定値を持たず、
  `segs` は近似の細かさなので既定値を持つため。
- ★★ `occt.so` は分割数を**そもそも取りません**（2026-09-21）。解析曲面なので
  細かさの概念が無く、原則が「そのモジュールで必要のない引数は撤去する」に変わったためです。
  ⇒ `"occt"::sphere(1.5, 32)` は **エラー**（`op 'sphere' — no candidate takes 2 argument(s)
  (occt: takes 1)`）。`"occt"::sphere(1.5)` と書いてください。
  - 2026-09-21 以前は「受け取るが無視し、検査はする」でした。理由は「同じ式が
    `cgal` / `manifold` で落ちて `occt` で通るのを防ぐ」ことでしたが、引数の個数が
    **routing の条件**になったので、その心配は別の形で解けています。
- ⚠ `openvdb.so` は末尾のボクセルサイズ `dx` が必須なので、`segs` を**位置的に省略できない**
  （`cylinder(r,h,segs,dx)`）。`0` を渡せば既定値になる点は同じ。

> ★ 2026-09-15以前は、この規約が **op ごとに 4 通り併存**していた。たとえば
> `circle(1,0)` は `cgal` で三角形・`manifold` で 32 角形になり、エラーも警告も無く面積が
> 2.4 倍違う答えを返していた。`revolve` は「省略すると 32・`0` と書くと 3」で、同じ「未指定」
> の 2 通りの書き方が別の形になっていた。現在は全 op・全カーネルで上の表 1 つに揃っている。

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

★ 各エントリの **実装** 行に出る `cg-mesh3d` / `mf-cross2d` のような綴りが**型**で、
どの型をどの `.so` が作り・受けるかは、一目で見るなら
[§型 × モジュール対応表](#type-grid)（丸罰の格子）・詳細は
[モジュールリファレンス §型 × モジュール一覧](srava_module_reference.html#type-matrix)にまとめてある。

### 引数の個数

`[ ]` で囲んだ引数は**省略できる**（`sphere(r[, seg])` など）。省略時の既定値は各エントリに記す。

- **多すぎる引数はエラーになる**。`sphere(1, 32, 5)` のように余分を書くと
  `sphere: too many arguments (takes 2)` で止まる（黙って捨てられることはない）。
- **必要な数に足りなければエラー**。`sphere()` は `sphere: expected 1 to 2 argument(s), got 0`。
- ⚠ **省略できるかどうかは実行するモジュールで違う**ことがある。`openvdb.so` の生成 op は
  末尾に `dx`（ボクセルサイズ）を取り、これは**省略できない** — ボリューム表現に分割数は
  意味を持たず、合成が格子（transform）の一致を要求するため。
  例: `sphere(r[, seg])` はメッシュ系では `seg` を省略できるが、`openvdb.so` では
  `sphere(r, dx)` の 2 引数が必須。どのモジュールが実行するかは
  [対応表](#module-matrix)と `module(..., {priority})` で決まる。

### op 名の付け方 — `元の名前_修飾` {#name-suffix}

**同じ族だが約束が違う**操作は、新しい名前を作らずに **`元の op 名` + `_修飾`** で並べる
（名前の数が増えすぎないように、という方針）。同じ前半を持つ op は*同じ族*で、後半が
**何を約束するかの違い**を表す。

| 名前 | 何を指定して、何を守るか |
|---|---|
| `simplify(m, n)` | **面数**を指定する。形（体積）は保つ |
| `simplify_cleanup(m, tol)` | **形のずれの上限**を指定する。面数は成り行き |

⚠ 逆に言うと、**約束が同じなら実装が違っても同じ名前**にする（`refine` は cgal と manifold で
中身がまったく違うが、「形は変えない・全辺が `len` 以下」という約束が同じなので 1 つの名前）。

## カテゴリ

0. [**型 × モジュール対応表**](#type-grid)（どの `.so` がどの型を扱うか）
0. [**モジュール対応表**](#module-matrix)（op × モジュールの ○× 表）
1. [演算子](#演算子) 2. [3D プリミティブ](#prim3d) 3. [2D プリミティブ](#prim2d)
4. [スイープ・2D⇄3D](#スイープ2d3d) 5. [ブール演算](#ブール演算) 6. [アフィン変換](#アフィン変換) · [カーネル・型変換](#kernel-conv)
7. [計測・検査・修復](#計測検査修復) 8. [近接](#近接2-メッシュ間3d-専用) 8b. [**点群**](#pointcloud) 9. [配列・数値ユーティリティ](#配列数値ユーティリティ)
10. [初等関数](#初等関数) 11. [I/O・システム](#io) 12. [stdlib: math](#stdlib-math数学定数ベクトル行列)
13. [stdlib: curve](#stdlib-curve曲線生成点列) 14. [stdlib: layout](#stdlib-layoutmesh-配列レイアウト)

---

## 型 × モジュール対応表（どの `.so` がどの型を扱うか）{#type-grid}

★ **記述子から機械生成**（`srava --module-info` の `provides` と各 op の `sig`）。
前節までの op の表が「**何ができるか**」なら、こちらは「**その値を誰が扱えるか**」。
⚠ 型ごとの 4CC・読める形式・所有の理由といった詳細は
[モジュールリファレンス §型 × モジュール一覧](srava_module_reference.html#type-matrix)にある
（こちらは**一目で当たりを見るための格子**で、あちらが本文）。

| 印 | 意味 |
|---|---|
| ○ | **サポートする型**。その型の実体クラスを持つ（記述子の `provides` に読み書きの配線を書いてある） |
| △ | **読める型**。`sig` の入力に名乗る。⇒ 読んだ値は**そのモジュールがサポートする型のクラスへ材料化**される（＝実質「自分の型に直して受ける」） |
| × | 扱わない |

★ **△ が「混ぜて書ける」ことの実体**。どのクラスで材料化するかは op の行が
`OPWIRE(計算, 本体クラス, …)` で**引数スロットごとに**縛っているので、他カーネルの値を
渡しても受け取る側は必ず自分の型として読む。⚠ 材料化が**不可逆なこともある**
（`cg-mesh3d` の厳密な有理数を double のカーネルが読むと戻せない）→
[型変換の規約](srava_module_reference.html#conversion)。

★ 読み方の例: `mf-mesh3d` の行を横に見ると、**サポートは `manifold`** だが `cgal` /
`geogram` / `cherchi` / `geomutils` / `nef_*` が △ ⇒ manifold が作った立体をそのまま
cgal の op に渡せる。逆に `oc-brep3d` の行は `occt` / `occt_mf` にしか印が無い ⇒
**B-rep は橋を通さないと他カーネルへ渡せない**。

★ 列の並びは**カーネルごとに本体 → その橋渡し**の順（`openvdb` の次に `openvdb_cg` …）。
次節の [op × モジュール表](#module-matrix) と**同じ並び**にしてあるので、2 つを見比べられる。

⚠ 試験用の擬似カーネル（`d2` / `d3` / `d4` / `d5` / `demo`）とその型は**外してある**。

| 型 | `cgal` | `nef_hybrid` | `nef_snc` | `nef_cg` | `nef_mf` | `geogram` | `cherchi` | `manifold` | `geomutils` | `openvdb` | `openvdb_cg` | `openvdb_gg` | `openvdb_mf` | `occt` | `occt_mf` | `points` |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **3D ソリッド / ボリューム** | | | | | | | | | | | | | | | | |
| `cg-mesh3d` | ○ | △ | △ | ○ | × | △ | △ | △ | △ | × | ○ | × | × | × | × | × |
| `nfb-mesh3d` | △ | ○ | × | × | × | × | × | △ | × | × | △ | × | × | × | × | × |
| `nf-mesh3d` | × | × | ○ | ○ | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `gg-mesh3d` | △ | △ | △ | × | × | ○ | △ | △ | △ | × | △ | ○ | × | × | × | × |
| `ch-mesh3d` | △ | △ | △ | × | × | △ | ○ | △ | △ | × | △ | × | × | × | × | × |
| `mf-mesh3d` | △ | △ | △ | × | ○ | △ | △ | ○ | △ | × | △ | × | ○ | × | ○ | × |
| `gu-mesh3d` | △ | △ | △ | × | × | △ | △ | △ | ○ | × | × | × | × | × | × | × |
| `vd-grid3d` | × | × | × | × | × | × | × | × | × | ○ | ○ | ○ | ○ | × | × | × |
| `oc-brep3d` | × | × | × | × | × | × | × | × | × | × | × | × | × | ○ | ○ | × |
| **型** | **`cgal`** | **`nef_hybrid`** | **`nef_snc`** | **`nef_cg`** | **`nef_mf`** | **`geogram`** | **`cherchi`** | **`manifold`** | **`geomutils`** | **`openvdb`** | **`openvdb_cg`** | **`openvdb_gg`** | **`openvdb_mf`** | **`occt`** | **`occt_mf`** | **`points`** |
| **2D 領域 — `z=0` の簡易表現** | | | | | | | | | | | | | | | | |
| `cg-cross2d` | ○ | × | × | × | × | × | × | △ | △ | × | ○ | × | × | × | × | × |
| `mf-cross2d` | △ | × | × | × | × | × | × | ○ | △ | × | × | × | × | × | ○ | × |
| `gu-cross2d` | △ | × | × | × | × | × | × | △ | ○ | × | × | × | × | × | × | × |
| `oc-cross2d` | × | × | × | × | × | × | × | × | × | × | × | × | × | ○ | △ | × |
| **型** | **`cgal`** | **`nef_hybrid`** | **`nef_snc`** | **`nef_cg`** | **`nef_mf`** | **`geogram`** | **`cherchi`** | **`manifold`** | **`geomutils`** | **`openvdb`** | **`openvdb_cg`** | **`openvdb_gg`** | **`openvdb_mf`** | **`occt`** | **`occt_mf`** | **`points`** |
| **2D 領域 — 空間に置ける一般表現** | | | | | | | | | | | | | | | | |
| `cg-face3d` | ○ | × | × | × | × | × | × | △ | △ | × | ○ | × | × | × | × | × |
| `mf-face3d` | △ | × | × | × | × | × | × | ○ | △ | × | × | × | × | × | ○ | × |
| `gu-face3d` | △ | × | × | × | × | × | × | △ | ○ | × | × | × | × | × | × | × |
| `oc-face3d` | × | × | × | × | × | × | × | × | × | × | × | × | × | ○ | △ | × |
| **型** | **`cgal`** | **`nef_hybrid`** | **`nef_snc`** | **`nef_cg`** | **`nef_mf`** | **`geogram`** | **`cherchi`** | **`manifold`** | **`geomutils`** | **`openvdb`** | **`openvdb_cg`** | **`openvdb_gg`** | **`openvdb_mf`** | **`occt`** | **`occt_mf`** | **`points`** |
| **点群** | | | | | | | | | | | | | | | | |
| `pt-cloud2d` | ○ | × | × | × | × | × | × | × | ○ | ○ | × | × | × | ○ | × | ○ |
| `pt-cloud3d` | ○ | × | × | × | × | ○ | × | × | ○ | ○ | × | × | × | ○ | × | ○ |

---

## モジュール対応表（op × モジュール）{#module-matrix}

★ **この表は記述子（各モジュールの `ops[]`）の `sig` から機械生成したもの**（2026-09-19 時点）。
**どのモジュールが実際に呼ばれるかは入力 mesh の型で決まる**（型が決まらない生成 op だけ
`module(..., {priority})` で選ぶ）。印の意味は 3 つ:

| 印 | 意味 |
|---|---|
| `○` | **そのモジュールが自分の型で受ける**（`sig` の入力にそのモジュールの型がある） |
| `△` | 自分では持たないが、**`geomutils.so` を併せてロードすれば**その型の値に対して呼べる |
| `○△` | **次元で分かれる** — 自分の 2D（`mf-cross2d` / `mf-face3d` 等）は自前、**3D は `geomutils.so` 経由** |

⚠ `△` / `○△` の行は **`module("geomutils.so", {})` を書かないと「no module can execute op」になる**。
メッシュ系（manifold / geogram / cherchi）の計測・位相・片取りは  /  で
`geomutils.so` へ**寄せた**（実装は 1 本・答えは寄せる前と 1 ビットも変わらない）。

⚠ **モジュールは `module("<name>.so", {})` で名指さないとロードされない**。表に ○ があっても、
そのモジュールをロードしていなければ使えない。
★ **原則は「実際に使うモジュールに絞る」** — この表は*どれを名指すか*を選ぶための表である。
一式ロード（`include "module/all.sra";` / `SRAVA_MODULE_ALL=1`）は移行中・探索中の便宜品で、
名指さずに載せると**どの `.so` が答えるかはロード順と `priority` 任せ**になる。併せて
[`use` で候補列を書く](srava_pseudo_module_reference.html#name-the-modules)。

⚠⚠ **この表に出てこない名前は、op ではなく標準ライブラリの関数のことがある。**
その場合に要るのは `module()` ではなく **`include`** で、`.so` は一切関係しない。
紛らわしい実例:

| 名前 | 正体 | 要るもの |
|---|---|---|
| `tube` / `tube_ruled` | **op** | それを持つモジュールのロード |
| `tube_wall` / `tube_wall_var` | **標準ライブラリの関数**（`std/curve.sra`） | `include "std/curve.sra";` |

⇒ `tube_ruled` は表に在るのに `tube_wall` が `srava --module-info` に出てこない、というのは
**正常**である。

★ **このページの本体 (各関数の項) では、標準ライブラリの関数に 〔`stdlib: <ライブラリ名>`〕
タグが付いている。** 名前で引けば、op か std 関数かはそこで分かる
（例: [`tube_wall`](#tube_wallpath-d-tube_wall_varpath-ds-パイプ壁オフセット-stdlib-curve) には
〔`stdlib: curve`〕が付いている）。**この表だけを見て「無い」と判断しないこと** —
表は op の対応表なので、std 関数は最初から載らない。

手元で確かめるなら、標準ライブラリは `<prefix>/share/srava/lib/std/*.sra` に
**平文で置かれている**ので、`grep` すれば op か関数かが確定する。

```sh
grep -rn 'tube_wall' /usr/local/share/srava/lib/std/
```

（ここに一覧は載せない。標準ライブラリは増減するので、焼いた一覧は必ず古くなる。）

| op | cgal | nef ※1 | geogram | cherchi | manifold | geomutils ※7 | openvdb | openvdb 橋渡し ※3 | occt | occt_mf ※2 | points ※6 | pipe_proximity |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **生成（3D）** | | | | | | | | | | | | |
| `box` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| `boxa` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| `sphere` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| `icosphere` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| `prism` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| `pyramid` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| `cylinder` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| `cone` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| `torus` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| `tetrahedron` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| `empty3d` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | × | × |
| **op** | **cgal** | **nef ※1** | **geogram** | **cherchi** | **manifold** | **geomutils ※7** | **openvdb** | **openvdb 橋渡し ※3** | **occt** | **occt_mf ※2** | **points ※6** | **pipe_proximity** |
| **生成（2D）** | | | | | | | | | | | | |
| `rect` | ○ | × | × | × | ○ | × | × | × | ○ | × | × | × |
| `ngon` | ○ | × | × | × | ○ | × | × | × | ○ | × | × | × |
| `circle` | ○ | × | × | × | ○ | × | × | × | ○ | × | × | × |
| `polygon` | ○ | × | × | × | ○ | × | × | × | ○ | × | × | × |
| `line` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `empty2d` | ○ | × | × | × | ○ | × | × | × | ○ | × | × | × |
| `text` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| **op** | **cgal** | **nef ※1** | **geogram** | **cherchi** | **manifold** | **geomutils ※7** | **openvdb** | **openvdb 橋渡し ※3** | **occt** | **occt_mf ※2** | **points ※6** | **pipe_proximity** |
| **スイープ・2D⇄3D** | | | | | | | | | | | | |
| `extrude` | ○ | × | × | × | ○ | × | × | × | ○ | × | × | × |
| `revolve` | ○ | × | × | × | ○ | × | × | × | ○ | × | × | × |
| `tube` ※9 | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `tube_ruled` ※9 | ○ | ○ | ○ | ○ | ○ | × | ○ | × | × | × | × | × |
| `section` | ○ | × | × | × | ○ | × | × | × | ○ | × | × | × |
| **op** | **cgal** | **nef ※1** | **geogram** | **cherchi** | **manifold** | **geomutils ※7** | **openvdb** | **openvdb 橋渡し ※3** | **occt** | **occt_mf ※2** | **points ※6** | **pipe_proximity** |
| **ブール** | | | | | | | | | | | | |
| `union` ※10 | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | ○ | × |
| `intersection` | ○ | ○ | ○ | ○ | ○ | ○ | ○ | × | ○ | × | △ | × |
| `difference` | ○ | ○ | ○ | ○ | ○ | ○ | ○ | × | ○ | × | △ | × |
| `combine` | ○ | × | × | × | ○ | × | × | × | × | × | × | × |
| **op** | **cgal** | **nef ※1** | **geogram** | **cherchi** | **manifold** | **geomutils ※7** | **openvdb** | **openvdb 橋渡し ※3** | **occt** | **occt_mf ※2** | **points ※6** | **pipe_proximity** |
| **アフィン変換** | | | | | | | | | | | | |
| `translate` ※10 | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | ○ | × |
| `rotate` ※10 | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | ○ | × |
| `mirror` ※10 | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | ○ | × |
| `scale` ※10 | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | ○ | × |
| `transform` ※10 | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | ○ | × |
| **op** | **cgal** | **nef ※1** | **geogram** | **cherchi** | **manifold** | **geomutils ※7** | **openvdb** | **openvdb 橋渡し ※3** | **occt** | **occt_mf ※2** | **points ※6** | **pipe_proximity** |
| **加工** | | | | | | | | | | | | |
| `offset` | ○ | ○ | × | × | ○ | × | ○ | × | ○ | × | × | × |
| `offset_thicken` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `hull` | ○ | ○ | ○ | × | ○ | × | × | × | × | × | × | × |
| `minkowski` | × | ○ | × | × | ○ ※5 | × | × | × | × | × | × | × |
| `refine` | ○ | × | × | × | ○ | × | × | × | × | × | × | × |
| `remesh` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `simplify` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `simplify_cleanup` | × | × | × | × | ○ | × | × | × | × | × | × | × |
| `fillet` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `chamfer` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `face` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `face_at` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `project` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `project_flatten` | ○ | × | × | × | ○ | × | × | × | ○ | × | × | × |
| `hlr` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `surface_type` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `surface_through` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `surface_control` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `poles` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `set_poles` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `loft` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `loft_ruled` | ○ | × | × | × | ○ | × | × | × | ○ | × | × | × |
| `unify_faces` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `solidify` | × | ○ | ○ | × | × | × | × | × | × | × | × | × |
| `unify` | × | ○ | × | × | × | × | × | × | × | × | × | × |
| `complement` | × | ○ | × | × | × | × | × | × | × | × | × | × |
| `convex_decomposition` | × | ○ | × | × | × | × | × | × | × | × | × | × |
| `vert` | ○ | × | △ | △ | △ | ○ | × | × | ○ | × | ○ | × |
| `verts` | ○ | × | △ | △ | △ | ○ | × | × | ○ | × | × | × |
| `face_verts` | ○ | × | △ | △ | △ | ○ | × | × | ○ | × | × | × |
| `part` | ○ | ○ | △ | △ | △ | ○ | × | × | × | × | × | × |
| `part_at` | ○ | × | △ | △ | △ | ○ | × | × | × | × | × | × |
| `shell` | ○ | × | △ | △ | △ | ○ | × | × | × | × | × | × |
| `shell_at` | ○ | × | △ | △ | △ | ○ | × | × | × | × | × | × |
| `repair` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `renormalize` | × | × | × | × | × | × | ○ | × | × | × | × | × |
| **op** | **cgal** | **nef ※1** | **geogram** | **cherchi** | **manifold** | **geomutils ※7** | **openvdb** | **openvdb 橋渡し ※3** | **occt** | **occt_mf ※2** | **points ※6** | **pipe_proximity** |
| **計測・検査** | | | | | | | | | | | | |
| `volume` | ○ | ○ | ○ | ○ | ○ | ○ | ○ | × | ○ | × | × | × |
| `area` | ○ | ○ | △ | △ | ○△ | ○ | ○ | × | ○ | × | × | × |
| `perimeter` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `centroid` | ○ | ○ | △ | △ | ○△ | ○ | ○ | × | ○ | × | ○ | × |
| `bbox` | ○ | ○ | △ | △ | ○△ | ○ | ○ | × | ○ | × | ○ | × |
| `nverts` | ○ | ○ | △ | △ | ○△ | ○ | × | × | ○ | × | ○ | × |
| `vert` | ○ | × | △ | △ | △ | ○ | × | × | ○ | × | ○ | × |
| `verts` | ○ | × | △ | △ | △ | ○ | × | × | ○ | × | × | × |
| `nfaces` | ○ | ○ | △ | △ | ○△ | ○ | × | × | ○ | × | × | × |
| `nedges` | × | × | × | × | × | × | × | × | ○ | × | × | × |
| `nparts` | ○ | ○ | △ | △ | △ | ○ | × | × | × | × | × | × |
| `nshells` | ○ | × | △ | △ | △ | ○ | × | × | × | × | × | × |
| `genus` | ○ | × | △ | △ | △ | ○ | × | × | × | × | × | × |
| `voxels` | × | × | × | × | × | × | ○ | × | × | × | × | × |
| `valid` | ○ | ○ | △ | △ | △ | ○ | ○ | × | ○ | × | ○ | × |
| `thin_spots` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `distance` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `distance_at` | ○ | × | ○ | △ | △ | ○ | ○ | × | ○ | × | × | × |
| `closest` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `farthest` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| **op** | **cgal** | **nef ※1** | **geogram** | **cherchi** | **manifold** | **geomutils ※7** | **openvdb** | **openvdb 橋渡し ※3** | **occt** | **occt_mf ※2** | **points ※6** | **pipe_proximity** |
| **型変換・表現変換** | | | | | | | | | | | | |
| `cast` | ○ | ○ | ○ | ○ | ○ | ○ | × | × | ○ | × | × | × |
| `voxelize` | × | × | × | × | × | × | × | ○ | × | × | × | × |
| `isosurface` | × | × | × | × | × | × | × | ○ | × | × | × | × |
| `triangulate` | × | × | × | × | × | × | × | × | × | ○ | × | × |
| `polygonize` | × | × | × | × | × | × | × | × | × | ○ | × | × |
| **op** | **cgal** | **nef ※1** | **geogram** | **cherchi** | **manifold** | **geomutils ※7** | **openvdb** | **openvdb 橋渡し ※3** | **occt** | **occt_mf ※2** | **points ※6** | **pipe_proximity** |
| **I/O・付随** | | | | | | | | | | | | |
| `import` | ○ | ○ | ○ | ○ | ○ | × | ○ | × | ○ | × | ○ | × |
| `export` | ○ | ○ | ○ | ○ | ○ | × | × | × | ○ | × | ○ | × |
| `export_vox` | × | × | × | × | × | × | × | ○ ※4 | × | × | × | × |
| `color` | ○ | × | × | × | ○ | × | × | × | × | × | × | × |
| **op** | **cgal** | **nef ※1** | **geogram** | **cherchi** | **manifold** | **geomutils ※7** | **openvdb** | **openvdb 橋渡し ※3** | **occt** | **occt_mf ※2** | **points ※6** | **pipe_proximity** |
| **近接（解析モジュール）** | | | | | | | | | | | | |
| `pipe_proximity` | × | × | × | × | × | × | × | × | × | × | × | ○ |
| `pipe_adjust` | × | × | × | × | × | × | × | × | × | × | × | ○ |
| `pipe_sample` | × | × | × | × | × | × | × | × | × | × | × | ○ |
| `pipe_scene_proximity` | × | × | × | × | × | × | × | × | × | × | × | ○ |
| `pipe_scene_adjust` | × | × | × | × | × | × | × | × | × | × | × | ○ |
| **op** | **cgal** | **nef ※1** | **geogram** | **cherchi** | **manifold** | **geomutils ※7** | **openvdb** | **openvdb 橋渡し ※3** | **occt** | **occt_mf ※2** | **points ※6** | **pipe_proximity** |
| **点群** | | | | | | | | | | | | |
| `points2d` | × | × | × | × | × | × | × | × | × | × | ○ | × |
| `points3d` | × | × | × | × | × | × | × | × | × | × | ○ | × |
| `rand` | × | × | × | × | × | × | × | × | × | × | ○ | × |
| `rand_gaussian` | × | × | × | × | × | × | × | × | × | × | ○ | × |
| `estimate_normals` | ○ | × | ○ | × | × | × | × | × | × | × | × | × |
| `delaunay` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `voronoi` | ○ | × | × | × | × | × | × | × | × | × | × | × |
| `hull` | ○ | ○ | ○ | × | ○ | × | × | × | × | × | × | × |

- ※1 **nef** は同じソースから `nef_snc.so`（SNC 表現）と `nef_hybrid.so`（境界表現ハイブリッド）の
  2 変種をビルドする。op の顔ぶれは同じ。
- ※2 **occt_mf** は occt とメッシュ系の橋渡し（`triangulate` 1 op だけ）。occt 本体を manifold に
  依存させないために分けてある（`triangulate` を使うにはこれをロードする）。
- ※3 **openvdb 橋渡し** は `openvdb_mf.so` / `openvdb_cg.so` / `openvdb_gg.so`。
  ボリューム（`vd-grid3d`）とメッシュ（`mf-` / `cg-` / `gg-mesh3d`）の間を渡す `voxelize` /
  `isosurface` の 2 op を持ち、「どのメッシュ型と行き来するか」で分かれている
  （openvdb 本体を manifold や CGAL に依存させないため）。
- ※4 `export_vox` は **`openvdb_cg.so` だけ**が持つ（3 本目の op）。hdf5 への依存を
  cgal 本体から外すために置き場所をここにしてある。
- ※5 `minkowski` は **`manifold.so` は `mf` どうし（自型）だけ**を受ける。混成（`mf`+`cg` 等）と
  `cg` / `gg` 由来は従来どおり `nef` 系が受ける。両方ロードしていると `mf` どうしは
  `manifold.so` が受ける（priority 10 > 5）。詳細は [`minkowski`](#minkowskia-b-ミンコフスキー和) の項。
- ※6 **points** は **外部ライブラリを持たない**カーネル中立のモジュール。点群型
  （`pt-cloud2d` / `pt-cloud3d`）と `xyz` の I/O を持つ。CGAL / geogram / OCCT / OpenVDB /
  manifold の**どれもが点群を「平坦な double 配列」で受け取る**ので、型をどれか 1 つの
  カーネルに置くと残りがそこへ依存してしまう（cgal に置けば非 GPL のカーネルが GPL を
  引き込む）。⇒ 中立の `libsrava_pt` に本体クラスと codec を置き、消費側はそれを借りる。
  **常にビルドされる**（依存が無いため）。★ `estimate_normals` は外部ライブラリが要るので
  ここには無く、**`cgal.so`（pca + mst）と `geogram.so`（Co3Ne）が 1 つの op 名で 2 実装**を持つ。
- ※10 **points の `union` とアフィン変換は点群専用**。`union` は**単純に混ぜる**（ブール
  和ではない・重複を落とさない・n 項可）。アフィン変換は**面外へ出す変換だけ `pt-cloud3d` を返す**。
  ⚠ 点群には**枠（平面）が無い**ので、`cgal` / `manifold` の `cg-face3d`（置かれた 2D）に
  あたるものは無い — 平面を出れば即 3 次元の点群になる。→ [アフィン変換（点群）](#pointcloud-transform)
- ※8 **格子から自由曲面** 。`surface_through` は通過点を、`surface_control` は制御点を
  与えて B-spline 面を作る (**別の op に分けてある** — 同じ格子でも意味が違うため)。
  `poles` / `set_poles` はその制御点の読み書き。⚠ 2026-09-17 の統合まで**この表に載っていなかった**
  (実物と突き合わせて見つけた)。
- ※9 `tube` と `tube_ruled` は **別の op**（2026-09-20 に名前を分けた）。`tube` = occt の
  B-spline 掃引（滑らか・厳密な円）／`tube_ruled` = 折れ線の掃引（`loft` / `loft_ruled` と同じ対）。
  以前はどちらも `tube` という 1 つの名前で、**どちらが走るかがロード構成で決まっていた**。
- ※7 **geomutils**（`geomutils.so`・型 `gu-mesh3d` / `gu-cross2d` / `gu-face3d`）は
  points と同じ**外部ライブラリを持たないカーネル中立**のモジュール。`manifold` / `geogram` /
  `cherchi` が作った値を**そのまま受けて**、計測・位相・片取り・頂点読みに答える。
  ⇒ その 3 者の列は `△`（geomutils 経由）で、**自分の型で受ける** geomutils の列が `○`。
  `face_verts` は geomutils と occt だけが持つ。
  ⚠⚠ **`part(mf-mesh3d, i)` / `part(gg-mesh3d, i)` の返り型は `gu-mesh3d`**（従来 `nf-mesh3d`）。
  geomutils は priority 7 で nef(5) に勝つ。厳密な答えが要るなら `"nef_snc"::part(...)` と名指す。
  ⚠ `cgal` は名乗らせていない（priority 20 に負けて死に行になるため）・`gg-mesh3d` の
  `distance_at` も載せていない（geogram が自前の AABB を持っているので奪わない）。
- **2026-08-31 以降、同梱モジュールは既定で全部ビルドされる**（`nef_snc.so` も 2026-09-16 に
  既定 ON へ戻った）。不要なものは `-DSRAVA_MODULE_<名前>=OFF` で外す
  （→ [モジュールリファレンス](srava_module_reference.html)）。
- 表に無い関数（`sin` / `map` / `print` など）は**モジュールではなく組込 or stdlib**。
  各エントリの **実装** 行を参照。

---

## 演算子

### `>>>` — 平行移動（transform シュガー）
`2D・3D` · → `mesh` / `mesh 配列`

**実装**: 全カーネル（= 対応する名前付き op・→ [対応表](#module-matrix)）· 型 入力を保存（`cg-mesh3d`/`mf-mesh3d` 等）

`m >>> v` ＝ `translate(m, v)`。左辺が **mesh 配列**なら各要素へ適用し配列を返す（broadcast / zip / instancing）。

**入力**
- 左辺 `m` 対象 — `mesh` または `mesh 配列`
- 右辺 `v` 移動量 — `2D/3D ベクトル`（mesh 配列では `点列` で zip/instancing）

**出力** 移動後 — `mesh`（左辺が配列なら `mesh 配列`）

- 例: `box(2,2,2) >>> [1,0,0]` / `[a,b] >>> [1,0,0]`（broadcast）/ `box(1,1,1) >>> [[0,0,0],[3,0,0]]`（複製配置）
- 関連: `translate`, `<>`, `***`, `@`

### `<>` — 鏡像（transform シュガー）
`2D・3D` · → `mesh`

**実装**: 全カーネル（= 対応する名前付き op・→ [対応表](#module-matrix)）· 型 入力を保存（`cg-mesh3d`/`mf-mesh3d` 等）

`m <> axis` ＝ `mirror(m, axis)`。原点通過平面での反射。

**入力**
- 左辺 `m` 対象 — `mesh`
- 右辺 `axis` 鏡映軸/法線 — `文字列`（`"x"`/`"y"`/`"z"`）または `3D ベクトル`

**出力** 反射後 — `mesh`

- 例: `box(1,2,2) <> "x"` / `m <> [1,1,0]`（任意法線）
- 関連: `mirror`

### `***` — 拡大縮小（transform シュガー）
`2D・3D` · → `mesh`

**実装**: 全カーネル（= 対応する名前付き op・→ [対応表](#module-matrix)）· 型 入力を保存（`cg-mesh3d`/`mf-mesh3d` 等）

`m *** s` ＝ `scale(m, s)`。負値＝反射。

**入力**
- 左辺 `m` 対象 — `mesh`
- 右辺 `s` 倍率 — `スカラ`（均等）または `ベクトル`（軸別 `[sx,sy,sz]`）

**出力** 拡縮後 — `mesh`

- 例: `box(1,1,1) *** 2` / `box(1,1,1) *** [2,3,4]`
- 関連: `scale`

### `@` — 回転（transform シュガー）
`2D・3D` · → `mesh`

**実装**: 全カーネル（= 対応する名前付き op・→ [対応表](#module-matrix)）· 型 入力を保存（`cg-mesh3d`/`mf-mesh3d` 等）

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

**実装**: `cgal.so` / `manifold.so` · 型 3D `cg-mesh3d`(MESH)/`mf-mesh3d`(MFM3)・2D `cg-cross2d`/`cg-face3d`(PLY2)・`mf-cross2d`/`mf-face3d`(MFC2)

`a ||| b` ＝ `union(a, b)`。可換・結合。多数を畳むなら `union(配列)`（並列二分木）。

**入力** 左辺 `a`・右辺 `b` 被演算 — ともに `mesh`（同次元）

**出力** 和 — `mesh`

- 注: 面接触・同一平面は失敗しやすい → わざと少し重ねる
- 関連: `union`, `&&&`, `---`, `+++`

### `&&&` — 積（ブールシュガー）
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` · 型 3D `cg-mesh3d`(MESH)/`mf-mesh3d`(MFM3)・2D `cg-cross2d`/`cg-face3d`(PLY2)・`mf-cross2d`/`mf-face3d`(MFC2)

`a &&& b` ＝ `intersection(a, b)`。可換・結合。

**入力** 左辺 `a`・右辺 `b` — ともに `mesh`（同次元）

**出力** 積 — `mesh`

- 関連: `intersection`

### `---` — 差（ブールシュガー）
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` · 型 3D `cg-mesh3d`(MESH)/`mf-mesh3d`(MFM3)・2D `cg-cross2d`/`cg-face3d`(PLY2)・`mf-cross2d`/`mf-face3d`(MFC2)

`a --- b` ＝ `difference(a, b)`。**非可換**（`a` から `b` を引く）。`a --- {} = a`、`{} --- a = {}`。

**入力** 左辺 `a` 被減数・右辺 `b` 減数 — ともに `mesh`（同次元）

**出力** 差 — `mesh`

- 関連: `difference`

### `+++` — 単純合体（ブールシュガー）
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` · 型 3D `cg-mesh3d`(MESH)/`mf-mesh3d`(MFM3)・2D `cg-cross2d`/`cg-face3d`(PLY2)・`mf-cross2d`/`mf-face3d`(MFC2)

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

★ **置き場所の規約**（カーネルを跨いで同じ）:
- `box` / `boxa` — **角が原点**（`[0,0,0]` 〜 `[w,h,d]`）
- `prism` / `pyramid` — 底面が **z=0**・上端（頂点）が z=h
- `sphere` / `icosphere` / `cylinder` / `cone` / `torus` / `tetrahedron` — **原点中心**

⚠ 体積は平行移動で変わらないので、**体積を突き合わせるだけでは置き場所の食い違いを検出できない**。
カーネル一致の表には位置を見るモデル（`box_cut` / `prism_cut`）を、`occt` の解析曲面には
閉形式との突き合わせ（`srava_occt.sh place`）を置いてある。

### `box(w, h, d)` — 直方体
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

w×h×d の軸並行直方体（原点隅・8 頂点 / 12 三角形）。

**入力**
- `w` X 方向の幅 — `スカラ`
- `h` Y 方向の高さ — `スカラ`
- `d` Z 方向の奥行き — `スカラ`

**出力** 直方体 — `mesh`（3D）

- 既定: `1,1,1`
- ⚠ **寸法は 3 つとも `> 0`**。0 や負は**明示エラー**（`box: sizes must be > 0`）。
  0 を許すと「体積 0 の立体」ができてしまい、カーネルによって値が食い違う
- 例: `box(40, 55, 9)`
- 関連: `boxa`, `rect`(2D), `extrude`, `prism`

### `boxa([w, h, d])` — 直方体（配列版）
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

`box` と同じだが寸法を**配列 1 個**で渡す。計算で作った寸法ベクトルをそのまま渡せる。

**入力** `[w, h, d]` 寸法 — `3D ベクトル`（スカラ 3 個の配列）

**出力** 直方体 — `mesh`（3D）

- 例: `boxa([40, 55, 9])`
- 関連: `box`

### `prism(n, h, r)` — 正 n 角柱
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

★ 生成器は全カーネル共通（`src/h/common/solids.h`）。`occt.so` 版は下の別項（平面 n+2 枚なので厳密に一致）。

底面が正 n 角形（外接半径 r・XY 平面 z=0）、高さ h（Z 軸）。`extrude(ngon(n,r), h)` と完全に等価。

**入力**
- `n` 角数 — `整数`（**3 以上**）
- `h` 高さ（Z） — `スカラ`
- `r` 底面の外接半径 — `スカラ`

**出力** 角柱 — `mesh`（3D）

- 既定: `3,1,1`
- ⚠ **`n >= 3` / `h > 0` / `r > 0`**。外れると**明示エラー**（`prism: n must be >= 3` など）。
  ★ 2026-09-12まで `cgal.so` **だけ**が `n < 3` を黙って 3 へ切り上げていた
  （他の 6 実装はエラー）。同じ式が cgal では通り nef では落ちる、という食い違いを消した
- 例: `prism(6, 10, 4)`（六角柱）
- 関連: `pyramid`, `ngon`, `extrude`

### `pyramid(n, h, r)` — 正 n 角錐
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `openvdb.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

底面が正 n 角形（外接半径 r・XY 平面 z=0）、頂点が z=h の角錐。底面の頂点は `ngon` / `prism` と
同じ並び（角度 2πk/n・+X 始点・CCW）。

★ 生成器は全カーネル共通（`src/h/common/solids.h`）なので、**頂点・面の並びがカーネル間で一致する**。
occt でも平面 n+1 枚の多面体なので厳密に一致する（近似が入るのは球・円柱・トーラス側）。

**入力**
- `n` 角数 — `整数`（3 未満はエラー）
- `h` 高さ（Z） — `スカラ`（0 以下はエラー）
- `r` 底面の外接半径 — `スカラ`（0 以下はエラー）
- ★ `openvdb.so` では末尾に `dx`（ボクセルサイズ）が要る: `pyramid(n, h, r, dx)`

**出力** 角錐 — `mesh`（3D）

- 例: `pyramid(4, 8, 5)`（四角錐）
- 関連: `prism`, `cone`, `tetrahedron`

### `cylinder(r, h[, seg])` — 円柱
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `openvdb.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3) / `oc-brep3d`(OCBR)

半径 r・高さ h の円柱。**原点中心**・軸は +Z（z は -h/2 〜 +h/2）。`seg` は円周分割数（既定 32）。

★ `occt.so` だけは**厳密**（側面が円筒面 1 枚・Face 3 枚）で、`seg` は**無視される**
（`sphere` と同じ扱い。近似しないので分割数が意味を持たない）。メッシュ系とは体積が構造的に違う
（内接多角柱 対 真の円柱）ので、カーネル一致の表には入れていない。

**入力**
- `r` 半径 — `スカラ`（0 以下はエラー）
- `h` 高さ（Z） — `スカラ`（0 以下はエラー）
- `seg` 円周分割数 — `整数`（省略/`0` で 32・`1`/`2`/負は**エラー**→「分割数 `segs` と辺数 `n` の規約」）
- ★ `openvdb.so` では末尾に `dx`: `cylinder(r, h, seg, dx)`

**出力** 円柱 — `mesh`（3D）

- 例: `cylinder(4, 10, 64)`
- 関連: `cone`, `prism`, `torus`, `tube_ruled`

### `cone(r, h[, seg])` — 円錐
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `openvdb.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3) / `oc-brep3d`(OCBR)

底面半径 r・高さ h の円錐。**原点中心**・軸は +Z（底面 z=-h/2・頂点 z=+h/2）。`cylinder` の
片方の半径を 0 にしたものなので、位置の規約は `cylinder` に合わせてある（`pyramid` は
`prism` に合わせて z=0〜h なので**そこだけ違う**）。

★ `occt.so` は**厳密**（円錐面 1 枚）で `seg` は無視される。

**入力**
- `r` 底面半径 — `スカラ`（0 以下はエラー）
- `h` 高さ（Z） — `スカラ`（0 以下はエラー）
- `seg` 円周分割数 — `整数`（省略/0 で 32・`1`/`2`/負は**エラー**→「分割数 `segs` と辺数 `n` の規約」）
- ★ `openvdb.so` では末尾に `dx`: `cone(r, h, seg, dx)`

**出力** 円錐 — `mesh`（3D）

- 例: `cone(4, 10, 64)`
- 関連: `cylinder`, `pyramid`

### `torus(R, r[, seg])` — トーラス
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `openvdb.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3) / `oc-brep3d`(OCBR)

大円半径 R・管半径 r のトーラス。**原点中心**・軸は +Z（穴が Z 方向に空く）。
`seg` は**大円・管断面の両方**の分割数（分割の knob は 1 つ = `sphere` / `circle` / `tube_ruled` と同じ）。

★ `occt.so` は**厳密**（トーラス面 1 枚）。メッシュ系では必ず近似になる形の代表で、
B-rep では厳密に持てるという違いがそのまま出る。

**入力**
- `R` 軸から管中心までの距離 — `スカラ`（0 以下はエラー）
- `r` 管半径 — `スカラ`（0 以下、または `R` 以上はエラー = 自己交差する）
- `seg` 分割数 — `整数`（省略/0 で 32・`1`/`2`/負は**エラー**→「分割数 `segs` と辺数 `n` の規約」）
- ★ `openvdb.so` では末尾に `dx`: `torus(R, r, seg, dx)`

**出力** トーラス — `mesh`（3D）

- 例: `torus(20, 3, 64)`
- 関連: `cylinder`, `tube_ruled`

### `tetrahedron(r)` — 正四面体
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `openvdb.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

外接球半径 r の正四面体。**原点中心**。立方体の対角 4 頂点を使う閉形式なので分割数を持たない。
occt でも平面 4 枚なのでメッシュ系と厳密に一致する。

**入力**
- `r` 外接球半径 — `スカラ`（0 以下はエラー）
- ★ `openvdb.so` では末尾に `dx`: `tetrahedron(r, dx)`

**出力** 正四面体 — `mesh`（3D）

- 例: `tetrahedron(10)`
- 関連: `pyramid`, `box`

### `sphere(r[, seg])` — 球（円周分割数指定）
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

半径 r の測地球（正八面体を分割して球面投影）。`seg` は円周分割数（連続値）。

**入力**
- `r` 半径 — `スカラ`
- `seg` 円周分割数 — `整数`（省略可・`1`/`2`/負は**エラー**→「分割数 `segs` と辺数 `n` の規約」）

**出力** 球 — `mesh`（3D）

- 既定: `r=1, seg=32` 相当（八面体 n=8・**258 頂点 512 面**）。面数 = 8·n²、n=(seg+3)/4。
- 例: `sphere(5, 64)`
- ★**cgal / manifold で頂点・面が一致**し体積が bit レベルで揃う（共通生成器 `src/h/common/geodesic.h`）。
- 細分回数（4 倍刻み）で指定したいときは `icosphere(r, subdiv)`。
- 関連: `icosphere`, `revolve`, `offset`

---

### `icosphere(r[, subdiv])` — 球（細分回数指定）
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `openvdb.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

★ 生成器は全カーネル共通（`src/h/common/geodesic.h`）。★ `occt.so` でも **厳密に一致する** —
`icosphere` は近似球ではなく **測地多面体**（平面三角形の集まり）なので、B-rep でも同じ立体になる
（近似が入る `sphere` とはここが違う）。

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

### `cylinder(r, h)` — 円柱（解析曲面）
`3D` · → `mesh`

**実装**: `occt.so` · 型 `oc-brep3d`(BREP)

半径 `r`・高さ `h` の円柱。**分割数を持たない** — 側面は円筒面そのものとして保持されるので、
体積は π·r²·h と 16 桁一致する（メッシュ系の `revolve` とは原理的に別物）。

**入力**
- `r` 半径 — `スカラ`
- `h` 高さ — `スカラ`

**出力** 円柱 — `mesh`（`oc-brep3d`）

- 既定: `r=1, h=1`
- 例: `module("occt.so",{}); volume(cylinder(1, 2))` → `6.2831853071795853`（π·2）
- ★ `nfaces` は**三角形数ではなく Face 数**（円柱は 3 = 円筒 1 + 平面 2）。
- 関連: `torus`, `sphere`, `triangulate`

---

### `torus(R, r)` — 輪環面（解析曲面）
`3D` · → `mesh`

**実装**: `occt.so` · 型 `oc-brep3d`(BREP)

主半径 `R`・管半径 `r` のトーラス。`cylinder` と同じく**近似が入らない**（体積は 2π²Rr² と一致）。

**入力**
- `R` 主半径 — `スカラ`
- `r` 管半径 — `スカラ`

**出力** トーラス — `mesh`（`oc-brep3d`）

- 既定: `R=1, r=0.25`
- 例: `torus(2, 0.5)` → 体積 `9.869604401089358`
- ★ `nfaces` は **1**（トーラス全体が 1 つの Face）。同じ形をメッシュ系に持たせれば数千面になる。
- 関連: `cylinder`, `fillet`

---

## 2D プリミティブ {#prim2d}

2D 多角形（`cgMesh2D`・穴あき可）は「断面」として `extrude`/`revolve` で 3D に持ち上がる。

### `rect(w, h)` — 長方形
`2D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `occt.so` · 型 `cg-cross2d`/`cg-face3d`(PLY2) / `mf-cross2d`/`mf-face3d`(MFC2)

★ `occt.so` でも作れる（平面 4 辺なので厳密に一致）。

原点隅・軸並行の長方形（CCW）。

**入力**
- `w` X 方向の幅 — `スカラ`（>0）
- `h` Y 方向の高さ — `スカラ`（>0）

**出力** 長方形 — `mesh`（2D）

- 注: 負/0 は明示エラー（`rect: width and height must be > 0`）。
  ★ 2026-09-12に文言を 3 実装で統一した（旧 `must be positive`・挙動は不変）
- 例: `rect(40, 55)`
- 関連: `box`(3D), `polygon`, `extrude`

### `ngon(n, r)` — 正 n 角形
`2D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `occt.so` · 型 `cg-cross2d`/`cg-face3d`(PLY2) / `mf-cross2d`/`mf-face3d`(MFC2)

★ `occt.so` でも作れる（平面 n 辺なので厳密に一致）。

外接半径 r・原点中心・CCW の正 n 角形。

**入力**
- `n` 角数 — `整数`（**3 以上**）
- `r` 外接半径 — `スカラ`（**> 0**）

**出力** 正多角形 — `mesh`（2D）

- ⚠ 外れると**明示エラー**（`ngon: n must be >= 3` / `ngon: radius must be > 0`）。
  ★ 2026-09-12まで `cgal.so` / `manifold.so` は検査を持たず、`ngon(2,1)` が
  「2 角形」として面積 1.299 を返し、`ngon(6,-1)` が `ngon(6,1)` と同じ面積を返していた
- 例: `ngon(6, 10)`
- 関連: `circle`, `prism`

### `circle(r[, segs])` — 円
`2D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `occt.so` · 型 `cg-cross2d`/`cg-face3d`(PLY2) / `mf-cross2d`/`mf-face3d`(MFC2)

★ `occt.so` の円は **厳密**（解析曲線 1 本）で `segs` は無視される。したがって内接正多角形で
作るメッシュ系とは面積が構造的に違い、カーネル一致の表には入れていない（`sphere` と同じ理由）。

正多角形で近似した円。

**入力**
- `r` 半径 — `スカラ`（**> 0**）
- `segs` 辺数（精度ピッチ） — `整数`（省略可・`0`＝既定・`1`/`2`/負は**エラー**→「分割数 `segs` と辺数 `n` の規約」）

**出力** 円（近似多角形） — `mesh`（2D）

- 既定: `segs=32`。`circle(r,8)`＝八角形
- ⚠ `r <= 0` は**明示エラー**（`circle: radius must be > 0`）。
  ★ 2026-09-12まで `cgal.so` / `manifold.so` は検査を持たず、`circle(-1)` が
  **cgal=3.12（≈π）/ manifold=0** とカーネルごとに違う値を黙って返していた
- 例: `circle(10, 64)`
- 関連: `ngon`, `revolve`

### `polygon(pts)` ／ `polygon(p0, p1, …)` — 塗り多角形
`2D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `occt.so` · 型 `cg-cross2d`/`cg-face3d`(PLY2) / `mf-cross2d`/`mf-face3d`(MFC2)

★ `occt.so` でも作れる（平面の折れ線なので厳密に一致）。

明示した点列の塗り多角形（任意 n 角形）。単純なら CW を CCW に正規化。自己交差も許容（→ `valid`/`repair`）。

**入力** `pts` 頂点列 — `点列`（2D ベクトルの配列 `[[x,y],…]`）。点を**別々の引数**として `polygon(p0, p1, …)` でも可（各 `pi` は `2D ベクトル`）

**出力** 塗り多角形 — `mesh`（2D）

- 例: `polygon([[0,0],[10,0],[5,8]])` / `polygon([0,0],[10,0],[5,8])`
- 関連: `line`（塗らない注釈線）, `repair`

### `line(pts)` ／ `line(p0, p1, …)` — ガイド線（開ポリライン）
`2D` · → `mesh`

**実装**: `cgal.so` · 型 `cg-cross2d`/`cg-face3d`(PLY2)

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

**実装**: `cgal.so` / `manifold.so` · 型 入力 `cg-cross2d`/`cg-face3d`(PLY2)→出力 `cg-mesh3d`(MESH) / 入力 `mf-cross2d`/`mf-face3d`(MFC2)→出力 `mf-mesh3d`(MFM3)

2D 多角形を高さ h でまっすぐ押し出して角柱化。**穴対応**（CDT 三角化）。

**入力**
- `poly` 断面 — `mesh`（2D）
- `h` 高さ（Z） — `スカラ`

**出力** 角柱 — `mesh`（3D）

- 例: `extrude(rect(40,20), 10)`
- 関連: `prism`, `revolve`

### `revolve(poly[, angle[, segs]])` — 回転体
`2D→3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 入力 `cg-cross2d`/`cg-face3d`(PLY2)→出力 `cg-mesh3d`(MESH) / 入力 `mf-cross2d`/`mf-face3d`(MFC2)→出力 `mf-mesh3d`(MFM3) / 入力 `oc-face3d`(BRP2)→出力 `oc-brep3d`(BREP)

2D プロファイル（x=半径≥0, y=高さ）を **world Y 軸**まわりに回して回転体化。

**入力**
- `poly` プロファイル — `mesh`（2D）
- `angle` 回転角（度） — `スカラ`（省略可）
- `segs` 全周の分割数 — `整数`（省略可・`0`＝既定・`1`/`2`/負は**エラー**→「分割数 `segs` と辺数 `n` の規約」）

**出力** 回転体 — `mesh`（3D）

- 既定: `angle=360`, `segs=32`
- 例: `revolve(polygon([[0,0],[5,0],[5,10],[0,10]]))`
- 注: `sphere` ≈ 半円の revolve
- ★★ **軸は world Y に固定**（原点を通る）。`extrude` が「world +Z のまま」なのと同じ規約で、
  3 カーネルとも同じ。★ 断面を**空間に置いてからでも回せる**— 置いた断面は
  そのまま world Y まわりに掃引されるので、たとえば `z=h` へ持ち上げた矩形を回すと
  半径 `√(x²+h²)` のドーナツになる。
  ★ 軸を断面と一緒に動かしたいなら **先に `revolve` して、できた立体を動かす**。
- ⚠ **断面が軸をまたぐ／軸が断面を貫くと明示エラー**（掃引が自分自身を通り抜けるため）。
  `profile x (radius) must be >= 0` もこの一種。
- ⚠ メッシュ系（`cgal` / `manifold`）は `segs` 分割の**多角形近似**なので、`occt`（解析曲面）
  とは値が一致しない。★ 空間に置いた断面では `cgal` と `manifold` は一致する。
- 関連: `extrude`, `loft_ruled`

### `tube_ruled(path[, segs])` — パス掃引（太さ可変・折れ線の背骨）
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_*.so` / `geogram.so` / `cherchi.so` / `openvdb.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`/`cg-face3d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`/`mf-face3d`(MFC2) ほか各カーネルの 3D 型

折れ線に沿って丸断面を掃引。各頂点が `[位置, 半径]`。**位置の次元で 2D/3D を自動判別**。

★ 掃引の本体は共通ヘッダ `src/h/common/tube.h` なので、**カーネルが違っても頂点・三角形の並びは同じ**。
ただし **2D の帯（リボン）を作れるのは `cgal` / `manifold` だけ** — 他は 2D 型を持たないので、
2D パスを渡すと明示エラーになる（`occt` は 2D 型を持つが、この op はまだ 3D 専用なので同じく明示エラー）。
★ `occt` は**断面が厳密な円**なので、共通ヘッダ組（メッシュ系 6 本）とは値が一致しない。★ `openvdb.so` は `tube_ruled(path, segs, dx)`（`dx` も必須）。

**入力**
- `path` パス — `配列`。各要素は `[位置, 半径]` のペア（位置＝`2D/3D ベクトル`、半径＝`スカラ`。2D では半径＝半幅）
- `segs` 3D 断面円の辺数 — `整数`（省略可・`0`＝既定・`1`/`2`/負は**エラー**→「分割数 `segs` と辺数 `n` の規約」）

**出力** 管（3D）／帯（2D） — `mesh`

- 既定: `segs=32`
- 注: 連続重複頂点は自動間引き。r=0 端は尖って閉じる。滑らかな曲線は `std/curve.sra` でサンプリングしてから渡す
- 注: 幾何が全カーネル共通なので、`tube_ruled` 主体の連鎖は manifold.so 側で in-proc のまま走る
- 例: `tube_ruled([[[0,0,0],0.5],[[3,1,0],0.4],[[3,3,0],0.0]], 24)`
- 関連: `tube`（occt の B-spline 版）, `tube_fw_ruled` / `tube_fw`（定幅の薄いラッパ）, `loft_ruled`

> ## ★ `tube` と `tube_ruled` は **別の op**（→ で改名）
>
> 分かれ目は **背骨**（なめらかに通すか、直線で結ぶか）。断面の表現は op では変わらず、
> **カーネルで変わります**（メッシュ系は `segs` 角形近似・`occt` は厳密な円）。
>
> | | 背骨 | 断面 | `segs` | 持っているカーネル |
> |---|---|---|---|---|
> | `tube_ruled` | 点を**直線で結ぶ折れ線**（頂点で角が立つ） | `segs` 角形近似の円 | 効く（既定 32） | cgal / manifold / nef / geogram / cherchi / openvdb |
> | `tube_ruled`（`occt`） | 同上（折れ線） | **厳密な円** | **取らない** | **occt** |
> | `tube` | 点を**通る C2 の B-spline**（滑らか） | **厳密な円** | **取らない**（第 2 引数は `{closed:1}` のハッシュ） | **occt だけ** |
>
> ★ `occt` の `tube_ruled` は「**折れ線に沿って置いた円の `loft_ruled`**」そのもので、
> メッシュ系と**同じ構成**です。⚠ それでも値は一致しません（円 対 多角形）。
>
> ⚠ **`occt` の `tube_ruled` は 3D のパスだけ**を受けます（2D 点列は明示エラー）。帯が要るなら
> メッシュ系のカーネルを使ってください。これは `occt` に帯が作れないという意味ではなく、
> **この op がまだ 3D 専用**だという範囲の話です。
>
> ⚠ 角のあるパスで `"occt"::tube` を `tube_ruled` の代わりに使うことは**できません** —
> B-spline の背骨は角を丸めるので別の形になります。角を尖らせたいなら `"occt"::tube_ruled`。
>
> ★ `closed:1` で**半径を変えられる**のは `tube_ruled` だけです（`tube` は明示エラー）。
>
> 2026-09-20 まで **どちらも `tube` という 1 つの名前**でした。同じ `path` を渡しても体積も
> 形状も一致せず（精度ではなく表現の違いなので許容誤差を緩めても一致しない）、
> *どちらが走るかがロード構成で決まる* 状態だったので、`loft` / `loft_ruled` と同じ対に
> 名前を分けました。⇒ **名前で決まるので、ロード構成では変わりません。**
>
> ```
> tube_ruled(path, 64)        折れ線の管。メッシュカーネル 6 本が同じ形を出す
> "occt"::tube_ruled(path)    折れ線の管・断面は厳密な円。角が尖ったまま解析曲面で持てる
> "occt"::tube(path)          滑らかな管。解析曲面なので offset が厳密・fillet が効く・STEP に曲面が載る
> ```
>
> ⚠ **移行**: 以前の `tube(折れ線)` は `tube_ruled(…)` に書き換えてください。別名は置いていないので、
> `occt` をロードしたまま `tube(…)` と書くと **エラーにならず occt の形**になります。
>
> ★ 関係は**収束**として観察できます。同じ直線パスで `tube_ruled` の `segs` を上げると
> `occt` の `tube` の値（閉形式 `π r² L`）へ寄ります。`occt` 側が「近似のない値」です。

### `polygonize(cross2d, defl)` — 曲線の輪郭を**折れ線へ落とす**（2D 版の `triangulate`）
`2D` · → `cross`

**実装**: `occt_mf.so` · 型 `mf-face3d`(MFC2)

`oc-face3d`（輪郭が Bezier / B-spline）を **`defl` の粒度で折れ線化**して `mf-cross2d` にする。
落とした先では既存の 2D 資産（2D ブール・`extrude`・`revolve`）がそのまま使え、
`cast("cg-cross2d", …)` は**無損失昇格**なので cgal の 2D（`Boolean_set_operations_2`・
straight-skeleton `offset`・`repair`）へも既存経路で渡る。

**入力**
- `cross2d` 2D 領域 — `oc-face3d`。**平面に載っていること**（曲面上の面は不可・下の ⚠）
- `defl` 粒度（曲線と弦の最大距離）— `スカラ`。**必須**（0 以下は明示エラー）

**出力** 2D 断面 — `mf-face3d`（occt の 2D は常に空間の面なので、行き先も一般表現）

- ★★ **面外へ出た 2D も渡せる**。面が載っている平面が、そのまま `mf-cross2d` の
  **枠**になる。⇒ 傾けた `rect(2,3)` の面積は **6 のまま**（射影していたら cos45 倍の 4.2426）、
  持ち上げた 2D は z=5 に居たまま渡り、`extrude` すると **z=5 から** 立つ。
  ⚠ これは**射影ではない** — 捨てている成分が無いので、面積も輪の形も変わらない。
  ⚠ 〜 の間はここが**明示エラー**だった。当時は行き先の `mf-cross2d` が z=0 に
  縛られていて、受ければ*黙って XY へ射影する*以外に手が無かったため。 で
  `mf-cross2d` が枠を持つようになったので、断る理由が無くなった。
- ⚠ **曲面上に切り取られた面は従来どおり断る**。折れ線にできないのは「平面かどうか」とは
  別の理由（円筒面の上の領域には平面の輪郭が無い）。⇒ 先に平面で切ること。
- ⚠ **複数の平面に散った面**（`oc-face3d` が別々の平面の面を持つ）も断る。1 つの平面へ
  まとめると、残りは黙って射影されることになるため。
- ★ 枠は**平面だけから決まる**（`plane_frame_canonical`）。OCCT の面が持つ `gp_Ax3` の
  `XDirection` は*面の作られ方で変わる*ので使わない — 使うと同じ幾何が 2 通りの局所座標になる。
  ⚠ 軸に平行な平面では `cgal` の `section` の基底表と同じ取り方になる。一般の平面では
  枠が違いうる（あちらは利用者が渡した `P` を原点に置くが、面からは `P` を知りようがない）。
  **同じ平面の別の枠**なので幾何は変わらず、`reexpress` が吸収する。
- ★ z=0 の平面に載った面の結果は **1 ビットも変わらない**（枠が既定になるので局所座標は
  従来どおりの `x,y`）

> ★★ **これは `cast` ではない。** 曲線を折れ線に落とすには粒度の指定が要るため
> （→ [型変換の規約](srava_module_reference.html#conversion)）。3D で「曲面を三角形に落とす」
> `triangulate(s, defl)` が `cast` でないのと**同じ理由**で、`defl` の**単位も揃えてある**
> （どちらも `BRepMesh_IncrementalMesh` に渡る弦誤差）。
>
> ★ 粒度は**省略できない**。既定値を黙って使うと「同じ入力から違う結果」が理由不明に出るため。

- 穴は保たれる（外周を CCW・穴を CW に揃えて `CrossSection` へ渡す）
- 粒度 `defl` を小さくするほど面積は occt 側の厳密値へ**単調に寄る**（曲線を内側から
  折れ線で近似するので、常に小さめに出る）。⚠ 粗い側では `defl` を少し変えても
  **同じ折れ線**になることがある（分割数が整数で決まるため）。

### `text(fontPath, str[, size])` — TrueType の字形を **2D 曲線のまま**取り込む
`2D` · → `cross`

**実装**: `occt.so` · 型 `oc-face3d`（BRP2）

TrueType / OpenType の字形を、輪郭を **Bezier / B-spline のまま**保った 2D 領域（平面上の
`TopoDS_Face`）にする。`extrude` すれば **側面が平面の帯ではなく厳密な押し出し面**になる。

**入力**
- `fontPath` フォントファイルのパス（`.ttf` / `.otf`）— `文字列`
- `str` 文字列（UTF-8）— `文字列`
- `size` 字の大きさ — `スカラ`（省略時 10）

**出力** 2D 領域 — `oc-face3d`

> ★★ **フォントは必ずパスで指定する。フォント名は受け付けない。**
>
> ```
> "occt"::text("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "あ", 10)   ← ○
> "occt"::text("DejaVu Sans", "あ", 10)                                       ← ✗ 明示エラー
> ```
>
> 名前で引く（fontconfig）と **同じスクリプトが機械によって違う形を出す**。srava は値ベースの
> DAG でキャッシュするので、それでは再現性が壊れる。
>
> ★ `fontPath` は `import` と同じ **D_REF** 扱いで、キャッシュキーに**ファイル内容のハッシュ**が
> 入る（content-addressed）。フォントを差し替えればキーが変わり、正しく再計算される。

- 穴は自動的に引かれる（`O` や `あ` の内側の輪郭）。`area` は外周 − 内周
- 空白だけの文字列は輪郭を持たないので明示エラー
- レイアウトは 1 行の単純な前進（縦書き・複数行・カーニングの細かい制御は未対応）

**例**
```
var f = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
var o = "occt"::text(f, "O", 10);
print(area(o));                 // 穴が引かれた面積
print(volume(extrude(o, 5)));   // = area x 5 (厳密に一致)
print(nfaces(extrude(o, 5)));   // 18 — 多角形化されていれば桁違いに増える
```

### `extrude(cross2d, h)` / `revolve(cross2d, angle[, segs])` — **occt 版**
`2D→3D` · → `mesh`

**実装**: `occt.so` · 型 `oc-brep3d`

`oc-face3d` を押し出す／Y 軸まわりに回す。同名 op が `cgal` / `manifold` にもあるが
**入力型が違う**（`cg-cross2d` / `mf-cross2d`）ので sig ディスパッチで自然に分かれる。

- `extrude` は `BRepPrimAPI_MakePrism`。輪郭が曲線のままなので**側面が厳密な押し出し面**になる
- `revolve` は `BRepPrimAPI_MakeRevol`。回転面が厳密になるので **`segs` を取らない**
  （`occt` の `sphere` と同じ）
- ⚠ `revolve` で断面が回転軸をまたぐと自己交差する。OCCT が失敗し明示エラーになる
- ⚠⚠ **掃引が単調でないと明示エラー**。掃引方向が面の中を向いている場合
  （傾けた 2D を面内方向へ押し出す・断面が軸に対して退化する回転）は、`BRepPrimAPI` が
  組む境界の**符号つき体積が厳密に 0 に打ち消し合う**。★ これは「潰れている」という意味では
  なく、**掃引された領域そのものは体積を持つ**（ミンコフスキー和）。壊れているのは
  境界表現の方なので、0 の立体を黙って返さずエラーにする。
  ★ 曲面とは無関係に**元から在った穴**で、平面の面でも面内方向へ押し出せば 0 になる。

### `prism(n, h, r)` — **occt 版**
`3D` · → `mesh`

**実装**: `occt.so` · 型 `oc-brep3d`

正 n 角柱（**プリミティブ**であって 2D→3D op ではない）。`r` は**外接円半径**。
★ 平面 n+2 枚でできるので **`cgal` / `manifold` と厳密に一致する**（`box` と同じ理由。
`sphere` のように構造的にずれる op とは違う）。

★ 置き場所も他カーネルと同じ **底面 z=0**（→ [置き場所の規約](#prim3d)）。

### `tube(path[, opts])` — パス掃引・**occt 版**（点を通る B-spline）
`3D` · → `mesh`

**実装**: `occt.so` · 型 `oc-brep3d`

⚠ **この `tube` を持つのは `occt.so` だけ**。折れ線の掃引は別 op `tube_ruled`（前項）で、
2026-09-20 に名前を分けた。⇒ どちらが走るかは **名前で決まる**（ロード構成では変わらない）。

点を**通る** C2 の B-spline を背骨にし、その上を**厳密な円**の断面で掃引する。
`path` の書き方はメッシュ版と同じなので、**入力を変えずにカーネルを変えるだけ**で滑らかになる。

**入力**
- `path` パス — `配列`。各要素は `[位置, 半径]`（位置＝`[x,y,z]`。`[x,y]` は z=0 として受ける）
- `opts` — `ハッシュ`（省略可）：`{closed: 1}` で周期スプライン（閉じた輪）
  ※ srava に真偽値リテラルは無いので `0/1` で書く（`module(so,{optional:1})` と同じ）

**出力** 管 — `oc-brep3d`

- ⚠ **半径は全頂点で > 0**。B-rep の円断面は半径 0 を作れないため（メッシュ版の「r=0 端は尖って閉じる」は
  使えない）。尖り端が要るなら `tube_ruled` を使う
- ⚠ **自己交差する背骨は明示エラー**。メッシュ版は自己交差を許容する仕様（とぐろを値として作れて
  `valid()`/`repair()` で扱う）だが、OCCT は掃引に失敗するか壊れた B-rep を作るので、黙って返さない
- ⚠ `{closed:1}` のとき **半径は一定**でなければならない（OCCT が閉じた背骨に複数断面を与えると
  `Build()` で落ちるため。明示エラーになる）
- ★ 第 2 引数は **ハッシュ（`{closed:1}`）のときだけ** この op が選ばれます。
  `tube(path, 24)` のように数を渡すと occt は候補から外れます（背骨も断面も滑らかなので
  分割数に意味が無い）。分割数を使いたいときは `tube_ruled(path, segs)` です。

**例**
```
var ring = "occt"::tube(pts, {closed:1});   // 閉じた輪
volume("occt"::tube([[[0,0,0],0.5],[[10,0,0],0.5]]))   // 7.853982 = π·0.5²·10
```
- 関連: `tube_ruled`, `tube_fw_ruled`, `tube_fw`, `bezier`, `spline`

### `section(mesh, P, N)` — 断面（3 要素配列）
`3D→2D` · → `配列[mesh]`

**実装**: `cgal.so` / `manifold.so` / `occt.so` · 型 入力 `cg-mesh3d`(MESH)→出力 `cg-face3d`(PLY2) / 入力 `mf-mesh3d`(MFM3)→出力 `mf-face3d`(MFC2) / 入力 `oc-brep3d`(OCB3)→出力 `oc-face3d`(OCF2)

点 P を通り法線 N の平面で 3D メッシュを切り、2D 断面（塗り領域・穴検出）を返す。3D 専用。

平面がメッシュの面と**ちょうど重なる**（共面）とき、「平面上の断面」は面そのものになって一意に決まらない。そこで戻り値を **3 要素配列**にして、平面ちょうどの結果と、平面の直下・直上の極限を同時に返す:

| 要素 | 内容 | 共面あり | 共面なし |
|---|---|---|---|
| `[0]` | 平面ちょうど | **空集合** | 断面 |
| `[1]` | 平面の直下の極限 | 断面 | **空集合** |
| `[2]` | 平面の直上の極限 | 断面 | **空集合** |

- 共面かどうかは**厳密に判定**される（座標が丸め誤差で近いだけの面は共面とみなさない）。
- 極限側の断面がそもそも何も無い場合（立体の最上面を切ったときの `[2]` など）はその要素が空集合になる。
- 空要素は `empty2d()` と同じ **空集合のメッシュ**。`{}`（fold の中立元）ではないので、`intersection` に渡せば正しく空になる。空判定は `area(s[0]) == 0` で足りる。
- 3 つの要素は**並列に計算される**（3 引数形は `[section(…,0), section(…,-1), section(…,+1)]` の配列リテラルに展開され、要素が一斉に起動される）。

**入力**
- `mesh` 対象 — `mesh`（3D）
- `P` 切断点 — `3D ベクトル`
- `N` 平面の法線 — `3D ベクトル`

**出力** `[平面ちょうど, 直下, 直上]` — `配列[mesh（2D）]`

- 例: `var s = section(box(10,10,10), [0,0,5], [0,0,1]); area(s[0])`（z=5 水平断面 → 共面でないので `s[0]`）
- ★★ **断面は切った場所に返る**。2D が**平面（枠）を持つ**ようになったので、
  切断平面がそのまま断面の平面になる。⇒ `extrude(section(b,[0,0,5],[0,0,1])[0], 1)` は
  **z=5 から** 立ち上がる。⚠ それ以前は断面が黙って `z=0` に戻っていた（切った場所の情報が消えていた）。
  ★ 局所座標は動かさないので **面積は変わらない**。変わるのは「どこに居るか」だけ。
  ⚠ 切った平面が `world +Z` を含む向き（`x=5` や `y=5` の断面など）だと、`extrude` は
  **明示エラー**になる（掃引方向が面の中を向くため）。断面を先に寝かせてから押し出す。
  ★ **3 カーネルとも同じ**。⚠ `occt` は 2026-09-14 まで断面を `z=0` へ**運んで**いた
  （より前の `cgal` に座標まで合わせるための実装で、当時は正しかった）。
  ⚠ ただし `occt` の 2D は *枠 + 局所座標* ではなく **面そのものが置き場所を持つ**
  （`TopoDS_Face`）ので、`bbox` は **world の 3 座標**で返る。`cgal` / `manifold` の 2D は
  局所座標の 2 値。⇒ 置き場所を比べるときはこの差に注意（面積・周長は同じ土俵で比べられる）。
- ★★ **`occt` は解析曲面のまま切る**。球を高さ `h` で切った断面は **真円**（`Geom_Circle`）で出るので
  面積が `π(r²-h²)` に丸め誤差の範囲で一致する。メッシュ系は内接多角形なので構造的に小さい
（分割数を上げるほど真値へ寄る）。体積・面積と同じ関係。
- ⚠ `manifold` は **Z 法線だけ**（`[0,0,1]`）。任意平面は `cgal` / `occt`。
- ⚠ `occt` の限界: 平面の**両側に材料がある**共面（接している 2 立体の継ぎ目など）では、そこに境界面が
  無いので `[1]` `[2]` とも空になる。真の極限は両側とも断面なので、そこだけは答えられない。
- 関連: `section(mesh,P,N,mode)`(単一断面), `empty2d`, `extrude`(再立体化)

### `section(mesh, P, N, mode)` — 断面（単一）
`3D→2D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `occt.so` · 型 上と同じ

`mode` を明示して**断面 1 枚**を返す形。3 要素配列が要らない場面（共面が起きないと分かっている、あるいは片側の極限だけ欲しい）で使う。

**入力**
- `mesh` / `P` / `N` — 上と同じ
- `mode` — `整数`：`0`=平面ちょうど / `-1`=平面の直下 / `+1`=平面の直上

**出力** 断面 — `mesh`（2D）

- `mode=0` は共面のとき空集合を返す（「平面ちょうど」は定義できないため）。共面を跨いで確実に何か得たいなら `-1` か `+1` を使う。
- 例: `section(box(10,10,10), [0,0,5], [0,0,1], 0)`
- 関連: `section(mesh,P,N)`(3 要素配列)

### `empty2d()` / `empty3d()` — 空集合
`2D` / `3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` · 型 `->cg-cross2d` / `->cg-mesh3d`（manifold では `mf-cross2d` / `mf-mesh3d`）

★ `empty2d` は `occt.so` でも作れる。`empty3d` は **全カーネル**にある
（`cgal` / `manifold` / `nef_snc` / `nef_hybrid` / `geogram` / `cherchi` / `occt` / `openvdb`）。
★ `openvdb.so` の `empty3d` は **`dx` を取る**: `empty3d(dx)`。ボリューム同士の合成は
transform の一致を要求するので、空でも「どの格子の上の空か」を決めないと使えない。

**値としての空集合**（領域・頂点を 1 つも持たないメッシュ）を作る。`box()` などと同じ leaf。

**入力** なし　**出力** 空のメッシュ

`{}`（空ハッシュ）との違い:

| | 意味 | `intersection(a, x)` | `union(a, x)` |
|---|---|---|---|
| `{}` | **fold の中立元**（演算子を適用しない印） | `a` | `a` |
| `empty3d()` | **空集合そのもの** | **空** | `a` |

累積の初期値には `{}` が便利（`var acc = {}; for(..) acc = acc ||| x;`）。集合として空を渡したいときは `empty2d()` / `empty3d()` を使う。

- 関連: `section`(空要素に使われる), `union`, `intersection`

---

## ブール演算

`a,b` は同次元 mesh。**2D と 3D を混ぜるとエラー**。接触/同一平面は失敗しやすい（少し重ねる）。

### `union(a, b)` ／ `union([a,…])` ／ `a ||| b` — 和
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH)・`cg-cross2d`/`cg-face3d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`/`mf-face3d`(MFC2)

ブール和（corefinement）。**配列 1 引数は並列二分木で畳み込む**（左 fold の直線ではなく段数 log₂N の木にする）。可換。`union([])`＝`{}`（単位元）。

**入力** `a, b` 被演算 — ともに `mesh`（同次元）。または引数 1 個に `mesh 配列`

**出力** 和 — `mesh`

- 例: `union(box(2,2,2), box(1,1,3))` / `union(parts)`
- 関連: `intersection`, `difference`, `combine`

### `intersection(a, b)` ／ `a &&& b` — 積
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH)・`cg-cross2d`/`cg-face3d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`/`mf-face3d`(MFC2)

ブール積（corefinement）。可換。配列 1 引数で並列畳み込み。

**入力** `a, b` — ともに `mesh`（同次元）。または `mesh 配列` 1 個

**出力** 積 — `mesh`

- **`+++`(combine)被演算子の分配則**: 被演算子が**構文的に** `+++` の時だけ、`(a +++ b) &&& c` を `(a&&&c) +++ (b&&&c)` にパース時展開（`∪aᵢ∩∪bⱼ`・両側可）。combine は複数の閉立体を解決せず束ねた形で corefinement に渡せないため。`|||`(union)結果のような妥当なメッシュは combine ではないので分配しない（演算子自体はランタイムで最適化しない方針）。
- ★★ **`occt` だけ 2D×3D を受ける**: `oc-face3d &&& oc-brep3d` は**面を立体で切り取り**、
  `oc-face3d` を返す。★ 曲面種は保たれ、面積は加法的
  （側面 = ∩立体 + −立体）。可換なので**両向き**書ける。⇒ `face` / `face_at` と組むと
  「**曲面上に切り取られた 2D**」が作れる。
- 関連: `union`, `combine`, `face`, `face_at`

### `difference(a, b)` ／ `a --- b` — 差
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH)・`cg-cross2d`/`cg-face3d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`/`mf-face3d`(MFC2)

`a` から `b` を引く。**非可換**（n-ary は左 fold `((a-b)-c)…`）。

**入力** `a` 被減数・`b` 減数 — ともに `mesh`（同次元）。または `mesh 配列` 1 個（左 fold）

**出力** 差 — `mesh`

- 例: `difference(box(4,4,4), sphere(2.5))`
- ★★ **`occt` だけ 2D×3D を受ける**: `oc-face3d --- oc-brep3d` は面から立体の部分を
  除き `oc-face3d` を返す。⚠ **逆向き（立体 − 面）は書けない** — 体積 0 の面で立体を切っても
  何も変わらないので、黙って no-op になるより断る方を選んだ。
  ⚠ 同じ理由で **2D ∪ 3D も書けない**（次元の違う和を表現できる型が無い）。
- **`+++`(combine)被演算子の分配則**: 被演算子が構文的に `+++` の時だけパース時展開。`(a +++ b) --- c` = `(a---c) +++ (b---c)`（左 combine は成分ごとに引いて束ね）。`c --- (a +++ b)` = `c --- a --- b`（右 combine は逐次差）。`|||` 結果等は分配しない。
- 関連: `union`, `combine`

### `combine(a, b)` ／ `a +++ b` — 単純合体 {#combine}
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`/`cg-face3d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`/`mf-face3d`(MFC2)

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

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH)・`cg-cross2d`/`cg-face3d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`/`mf-face3d`(MFC2)

平行移動（EPECK 厳密）。

**入力**
- `m` 対象 — `mesh`
- `v` 移動量 — `2D/3D ベクトル`（または `x, y, z` の `スカラ` 3 個）

**出力** 移動後 — `mesh`

- 例: `translate(box(2,2,2), [1,0,0])` / `translate(box(2,2,2), 1,0,0)`
- ★ **2D も空間に置ける**。`cgal` / `manifold` の 2D は **平面（枠）を持つ**ので、
  面外へ出す変換は z 成分を枠へ渡す（`occt` の `oc-face3d` は元から面を直接動かす）。
  ★ 同じ平面に載っていれば**軸の取り方が違ってもブールできる**（先頭の被演算子の枠で表し直す）。
  ⚠ 平面が線に潰れる変換と、**本当に別の平面**どうしのブールは明示エラー →
  [モジュールリファレンス](srava_module_reference.html)
- 関連: `>>>`, `translate_pts`(点列版)

### `rotate(m, axis, deg)` ／ `rotate(m, deg)` ／ `m @ (axis, deg)` — 回転
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH)・`cg-cross2d`/`cg-face3d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`/`mf-face3d`(MFC2)

原点まわりの回転（度数）。2D は軸不要（面内回転）。

**入力**
- `m` 対象 — `mesh`
- `axis` 回転軸 — `文字列`（`"x"`/`"y"`/`"z"`）または `3D ベクトル`（Rodrigues・2D は省略）
- `deg` 角度（度） — `スカラ`

**出力** 回転後 — `mesh`

- 注: cos/sin は double 近似だが座標は厳密有理数のまま。`[0,0,0]` 軸はエラー
- 例: `rotate(box(4,4,1), "z", 45)` / `rotate(poly, 30)`(2D)
- ★ **2D も空間に置ける**。`cgal` / `manifold` の 2D は **平面（枠）を持つ**ので、
  面外へ出す変換は z 成分を枠へ渡す（`occt` の `oc-face3d` は元から面を直接動かす）。
  ★ 同じ平面に載っていれば**軸の取り方が違ってもブールできる**（先頭の被演算子の枠で表し直す）。
  ⚠ 平面が線に潰れる変換と、**本当に別の平面**どうしのブールは明示エラー →
  [モジュールリファレンス](srava_module_reference.html)
- 関連: `@`, `rotate_pts`(点列版), `mirror`

### `mirror(m, axis)` ／ `m <> axis` — 鏡像
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH)・`cg-cross2d`/`cg-face3d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`/`mf-face3d`(MFC2)

原点通過平面での反射（面の向きは自動復元）。

**入力**
- `m` 対象 — `mesh`
- `axis` 鏡映軸/法線 — `文字列`（`"x"`/`"y"`/`"z"`）または `3D ベクトル`（Householder）

**出力** 反射後 — `mesh`

- 例: `mirror(m, "x")`
- ★ **2D も空間に置ける**。`cgal` / `manifold` の 2D は **平面（枠）を持つ**ので、
  面外へ出す変換は z 成分を枠へ渡す（`occt` の `oc-face3d` は元から面を直接動かす）。
  ★ 同じ平面に載っていれば**軸の取り方が違ってもブールできる**（先頭の被演算子の枠で表し直す）。
  ⚠ 平面が線に潰れる変換と、**本当に別の平面**どうしのブールは明示エラー →
  [モジュールリファレンス](srava_module_reference.html)
- 関連: `<>`, `scale`(負値)

### `scale(m, s)` ／ `scale(m, sx, sy, sz)` ／ `m *** s` — 拡大縮小
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH)・`cg-cross2d`/`cg-face3d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`/`mf-face3d`(MFC2)

原点中心の拡大縮小。負値＝反射。

**入力**
- `m` 対象 — `mesh`
- `s` 倍率 — `スカラ`（均等）または `ベクトル`（軸別・または `sx, sy, sz` の `スカラ` 3 個）

**出力** 拡縮後 — `mesh`

- 注: 係数 0 は退化エラー
- 例: `scale(box(1,1,1), 2)` / `scale(box(1,1,1), [2,3,4])`
- ★ **2D も空間に置ける**。`cgal` / `manifold` の 2D は **平面（枠）を持つ**ので、
  面外へ出す変換は z 成分を枠へ渡す（`occt` の `oc-face3d` は元から面を直接動かす）。
  ★ 同じ平面に載っていれば**軸の取り方が違ってもブールできる**（先頭の被演算子の枠で表し直す）。
  ⚠ 平面が線に潰れる変換と、**本当に別の平面**どうしのブールは明示エラー →
  [モジュールリファレンス](srava_module_reference.html)
- 関連: `***`, `scale_pts`(点列版)

### `transform(m, matrix)` — 一般アフィン
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH)・`cg-cross2d`/`cg-face3d`(PLY2) / `mf-mesh3d`(MFM3)・`mf-cross2d`/`mf-face3d`(MFC2)

行優先の同次行列で一般アフィン変換。

**入力**
- `m` 対象 — `mesh`
- `matrix` 変換行列 — `配列`（行優先・12 要素=3×4 または 16 要素=4×4 の平坦な数値配列）

**出力** 変換後 — `mesh`

- ⚠ **線形部の行列式が 0 の行列（特異行列）は明示エラー**。立体が平面・直線・点へ潰れて
  しまうため（`scale` の 0 倍を弾くのと同じ理由）。
  ★ 行列式が**負**（反射）は正当なので通る
- 例: `transform(box(2,2,2), [1,0,0,1, 0,1,0,0, 0,0,1,0])`（+x 平行移動）
- ★ **2D も空間に置ける**。`cgal` / `manifold` の 2D は **平面（枠）を持つ**ので、
  面外へ出す変換は z 成分を枠へ渡す（`occt` の `oc-face3d` は元から面を直接動かす）。
  ★ 同じ平面に載っていれば**軸の取り方が違ってもブールできる**（先頭の被演算子の枠で表し直す）。
  ⚠ 平面が線に潰れる変換と、**本当に別の平面**どうしのブールは明示エラー →
  [モジュールリファレンス](srava_module_reference.html)
- 関連: `matvec`(点列・stdlib), `scale`, `mirror`

### `offset(m, d[, n])` — オフセット
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `occt.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 2D `cg-cross2d`(PLY2) / `mf-cross2d`(MFC2)・3D `nf-mesh3d`(NEF3) / `nfb-mesh3d`(NEFB)
⚠ `cgal.so` / `manifold.so` は **2D のみ**。**3D は `nef_snc.so` / `nef_hybrid.so`** が引き受ける。

`d>0` で膨張、`d<0` で収縮。2D＝straight skeleton（角は**尖ったまま**）/ 3D＝半径 d の球との Minkowski 和（重い）。

#### ⚠⚠ 2D の**外側**オフセットは、角の処理が**カーネルごとに違う** {#offset-2d-corners}

上流のアルゴリズムがそれぞれ別物なので、**どれが正しいというものではない**。揃えることはしない。

| カーネル | アルゴリズム | 凸角 | `rect(10,6)` を `+0.75` |
|---|---|---|---|
| `cgal` | straight skeleton | **尖ったまま**（miter） | `60 + 24 + 4d²` = **86.250000** |
| `manifold` | Clipper2 | **面取り** | `60 + 24 + 2d²` = **85.125000** |
| `occt` | 輪郭を面内で移動 | **円弧**（Steiner） | `60 + 24 + πd²` = **85.767146** |

⚠ **内側**（`d<0`）は 3 者とも一致する（`38.25`）。凹側は角を継ぎ足さないため。

★ **入力を細かくすれば互いに収束する。** 曲がり角 θ での角の面積は
miter が `d²tan(θ/2)`・面取りが `d²sin(θ)/2`・円弧が `d²θ/2` で、**θ→0 で 3 つとも `d²θ/2` に一致**する。
差は `O(θ³)`、角の数は `N` なので全体で **`O(1/N²)`**（`circle(10,N)` を `+0.75` した実測でも、
`N` を倍にするたび差が 1/4 になる）。

⚠⚠ **ただし設計図に実在する角は収束しない。** 上の収束は「滑らかな曲線を折れ線で近似した角」の話で、
本物の 90° の角は分割を上げても 90° のまま。**角のある形をカーネルをまたいで比較しない**こと。
比較するなら、同じカーネルで通すか、丸めを明示的に入れる。

★**3D は nef モジュールが持つ**。中身が Minkowski 和（Nef + 凸分解）なので、実装を持つモジュールに置いてある。
3D で使うときは nef モジュールをロードし、結果を cg で続けたければ `cast("cg-mesh3d", …)` を書く。
`cg`/`mf` の 3D mesh をそのまま渡してもよい（昇格読み・結果は `nf` 型）。

⚠ `occt.so` の 2D は**面の中で**輪郭を動かす（外側は角が円弧で丸まる = Steiner の形
`6 + 10d + πd²`・内側は角が立つ）→ [上の角の表](#offset-2d-corners)。**面に厚みを付けて立体にする**のは別 op
[`offset_thicken`](#offset-thicken) で、曲面はそちらだけが受ける。

**入力**
- `m` 対象 — `mesh`
- `d` オフセット量 — `スカラ`
- `n` 3D の球細分化 — `整数`（省略可・**2D は無視**。2D の角の細かさは
  [カーネルのアルゴリズムが決める](#offset-2d-corners)ので、この引数では変えられない）

**出力** オフセット形状 — `mesh`

- 既定: `n=1`
- 注: 肉厚 ＝ `offset(m, t) --- m`（外殻）/ `m --- offset(m, -t)`（内殻）
- 例: `offset(rect(20,10), 2)` (2D) / `module("nef_hybrid.so"); offset(box(2,2,2), 0.5)` (3D)
- 3D の `d<0`（収縮）は補集合トリック `A − dilate(bbox − A, |d|)`。角は鋭いまま残る
- ★★ **`openvdb.so` の `offset` は距離場の dilate / erode そのもの**なので、**open / close が
  式で書ける**（専用 op は作らないと判断した）:

  ```
  offset(offset(v, -r),  r)    # open  — 2r より細い橋・突起を落とす
  offset(offset(v,  r), -r)    # close — 2r より細い隙間・穴を埋める
  ```

  閾値は素直に効く: **`2r` より細い橋は消え、太い橋は残る**（体積が箱だけの場合に一致するか
  どうかで確かめられる）。残差は `dx` の誤差の範囲で、閉形式（Steiner の公式）と突き合わせられる。
  ⚠ 間に `renormalize` を**挟まないこと** — 帯の再構築で界面が動き、かえって閉形式から離れる
- 関連: `difference`, `minkowski`, `renormalize`

### `offset_thicken(area2d, d)` — 面に**厚み**を付けて立体にする {#offset-thicken}
`2D→3D` · → `mesh`

**実装**: `occt.so` だけ（→ [対応表](#module-matrix)）· 型 `oc-face3d` / `oc-cross2d` → `oc-brep3d`

2D 領域（面）の**法線側に厚み `d` を付けて**立体にする。平面でも**曲面でも**定義できるので、
`face(cylinder(r,h), 0)` のような曲面をそのまま太らせられる。★ **閉形式で検算できる**:

```
volume(offset_thicken(rect(3,2), d))                // 面積 x d（平面なら厳密）
volume(offset_thicken(circle(r), d))                // πr²d
volume(offset_thicken(face(cylinder(r,h),0),  d))   // π((r+d)² − r²)·h  — 外へ
volume(offset_thicken(face(cylinder(r,h),0), -d))   // π(r² − (r−d)²)·h  — 内へ
```

- ⚠⚠ **`offset` とは別の操作**なので名前を分けてある（命名規約は
  [`元の名前_修飾`](#name-suffix)）:

  | op | 何をするか | 出る次元 |
  |---|---|---|
  | `offset(area2d, d)` | 面の **中で**輪郭を動かす | **2D のまま** |
  | `offset_thicken(area2d, d)` | 面の**法線側**に厚みを付ける（**片側だけ**） | **3D** |
  | `offset(solid, d)` | 全方向に ⊕ 球(`d`) | 3D |

  同じ名前にすると、`*-face3d` が「曲面」ではなく「空間に置かれた 2D」の意味である以上
  （[規約①](srava_language_reference.html#two-2d-types)・平面も大量に含む）、
  **同じ入力に対して cgal / manifold は 2D を返し occt だけ 3D を返す**ことになる。
- ★ `d` の符号は**どちら側に伸ばすか**。平面では体積が変わらない（`|d|` が効く）が、
  曲面では内側（凹側）に伸ばすほうが体積が小さい。
- ⚠ **厚みが凹側の曲率半径を超えるとエラー**（`radius of curvature` を含む文言）。
  OCCT の `Geom_OffsetSurface` は自己交差を消しも検査もせず、`IsDone()=true` のまま
  **軸を越えた環**を返すので、srava 側で断っている。
- ⚠ **曲面の面内オフセット**（`offset(face(cylinder…), d)`）は持たない。OCCT に道具が無く、
  UV でずらすと距離が保てないため — エラー文言がこの op を案内する。

**入力** `area2d` 面 — `oc-face3d` / `oc-cross2d` ／ `d` 厚み — `スカラ`

### `hull(m)` ／ `hull(a, b, …)` — 凸包
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so`（→ [対応表](#module-matrix)）

与えた形すべてを**点の集合とみなし**、それを包む最小の凸形状を返す。2 個以上渡すと
**全部まとめて 1 つの凸包**になる（`hull(a, b)` は `hull(a ||| b)` と同じ値）。

**入力** `a, b, …` 被演算 — `mesh` または **点群**を**1 個以上**

**出力** 凸包 — `mesh`（★ **点群を渡しても出るのは mesh**）

- ★★ **次元の決まり方**（[2D 領域の 2 つの型](srava_language_reference.html#two-2d-types)）:
  3D 立体か 3D 点群が 1 つでも混ざれば**立体**、2D だけなら **2D 領域**。
  2D どうしの型は「全部 `*-cross2d` なら `*-cross2d`・`*-face3d` が混ざれば `*-face3d`」。
  ```
  hull(rect(2,2), rect(1,4))                      // 2D
  hull(translate(circle(1),[10,0,0]), circle(1))  // 2D の長穴。xy 内の translate なので両方 cross2d
  hull(box(2,2,2), rect(2,2))                     // 立体
  hull(rotate(rect(2,2),"x",90), rect(1,4))       // ⚠ 本当に別の平面 → 明示エラー
  ```
  ⚠ 2D 点群（`pt-cloud2d`）は**常に z=0 に居る**（動かす op が無い）ので、別の平面に置かれた
  2D 領域とは混ぜられない（明示エラー）。空間の凸包が要るなら `points3d(...)` で書く。

- ★**情報を落とす op**。穴も凹みも消えるので、形を整える道具ではない
- ★**面を見ない**ので、閉じていない／自己交差した入力でも凸包は取れる（ブールと違う点）
- ★★ **点群（`pt-cloud2d` / `pt-cloud3d`）を直接受ける**（`cgal.so`）。`hull` はもともと
  入力から**頂点しか使っていない**ので、点群は拡張ではなく**素の入力**で、メッシュを渡す方が
  「頂点以外を捨てる」特殊ケースだった。例: `hull(import("scan.xyz"))`
  ⚠ **点群だけを渡した呼び出し**は入力にメッシュ型が 1 つも無いので、型による振り分けが効かず
  **既定カーネルが決める**（`box()` と同じ leaf 的な振る舞い）。`"cgal"::hull(p)` で名指しできる
- ★`hull(hull(a, b), c)` は `hull(a, b, c)` と同値（凸包に効かない内点しか落ちない）。
  ⚠ ただし **木には分解しない**（sig の `(*!)`・）。分解すると「点 → メッシュを作って
  読み戻し、面を捨ててまた頂点に戻す」を段ごとに繰り返して損なうえ、⚠⚠ **退化検査が部分集合に
  ついて閉じていない**ので落ちる — 立方体の 8 頂点を 2 点ずつ 4 群に割ると群の凸包は同一平面になり、
  全体は立体なのにエラーになる
- ⚠ **退化は明示エラー**。点が 1 点・1 直線上・1 平面上に乗っていると凸包は立体にならない。
  黙って体積 0 を返さず、5 実装とも同じ文言でエラーにする
- 2D を持つのは `cgal.so` / `manifold.so` だけ（`nef` / `geogram` は 3D のみ）
- 例: `var b = box(1,1,1); volume(hull(b, translate(b,[2,0,0])))` → `3`（3×1×1 の箱）
- 関連: `minkowski`, `convex_decomposition`, `union`

### `minkowski(a, b)` — ミンコフスキー和
`3D` · → `mesh`

**実装**: `nef_snc.so` / `nef_hybrid.so`（型 `nf-mesh3d`(NEF3) / `nfb-mesh3d`(NEFB)）·
`manifold.so`（型 `mf-mesh3d`・**3D 自型どうしのみ**）

`A ⊕ B = { p + q | p ∈ A, q ∈ B }`。`offset(m, d)` はこの特殊形（半径 `d` の球との和）＝
こちらがプリミティブ。

**入力** `a, b` 被演算 — ともに `mesh`（3D・**両方とも有界**であること）

**出力** Minkowski 和 — `mesh`

- **`nef` 系**は `nf` / `cg` / `mf` / `gg` の**全 16 組**を受ける（`minkowski(mfBody, cgBody)` の
  ような**異カーネル混成**も可）。`cg`/`mf`/`gg` は昇格読みで `nf` になり、結果は常に `nf` 系
- **`manifold.so`** は `mf` どうし（`(mf-mesh3d, mf-mesh3d)`）**だけ**を名乗る。混成を書かないのは、
  同じ入力型の組を 2 モジュールが名乗ると「どちらが計算したか」が `module()` の書き順に
  依存してしまうため
- ⚠ したがって**両方をロードしていると、`mf` どうしの `minkowski` は `manifold.so` が受ける**
  （priority 10 > 5）。`nef` に行かせたいときは `"nef_snc"::minkowski(…)` と名指しする
- ★**中身がまったく違う**:
  - `nef`（`CGAL::minkowski_sum_3`）… 両者を**凸分解**して m×n ペアの和を取り全部 union する。
    最悪 O(n³m³)。凸どうしなら頂点対の和の凸包で済むので軽い
  - `manifold`（`Manifold::MinkowskiSum`）… ★**凸分解を使わない**。`A` の三角形ごとに
    （3 頂点 ⊕ `B`）の凸包を取り、1000 個ずつ `BatchBoolean` で畳む。境界の三角形分割を
    分解の代わりに使うので、**union の速さがそのまま効く**
  - ⚠ 両者は**同じ値に収束するが bit 一致はしない**（厳密有理数 対 double）
- **非有界**（`complement` の結果など）は**明示エラー**。CGAL 自身は片方をそのまま返すので、その前に弾く
- ⚠ `cgal.so` は `minkowski` を**持たない**。CGAL の `minkowski_sum_3` は `Nef_polyhedron_3` の
  上の関数なので、持ち主は `nef` 系である（`cgal.so` に置くと Surface_mesh→Nef→Surface_mesh の
  往復になり、「他カーネルの機能を借りて自分の顔で出す」ことになる）
- 例: `module("nef_hybrid.so"); minkowski(box(2,2,2), box(1,1,1))` → `box(3,3,3)`（体積 27）
- 関連: `offset`, `union`, `hull`

### `nverts(m)` ／ `nfaces(m)` ／ `nedges(m)` — 頂点数・面数・稜数
`2D・3D` · → `値`

**実装**: `cgal.so` / `nef_snc.so` / `nef_hybrid.so` / `occt.so` / `geomutils.so`（`mf-` / `gg-` / `ch-` の 3D と `gu-*`・**要ロード**） / `manifold.so`（**2D のみ**）／ `points.so`（`nverts` のみ）（→ [対応表](#module-matrix)）
／ `nedges` は **`occt.so` だけ**

mesh の**頂点数**と**面数**。2D は面を持たないので `nfaces` は 0、`nverts` は点の総数（外周 + 穴 + ガイド）。

★ **`nedges` は稜（edge）の本数**。`occt` だけが持つ理由は 2 つ:
- B-rep の稜は「曲線 1 本」なので数えることに意味がある（円は**1 本**であって 360 本ではない）
- **図面**（面を持たない 2D・`hlr` の出力）は `nfaces` でも `area` でも何も分からず、
  *本数*だけが中身を語る

```
nedges(box(2,2,2))                             // 12  — ⚠ 共有は 1 本と数える
nedges(circle(1,16))                           // 1   — 円は 1 本
nedges(hlr(box(2,2,2),[-1,-1,-1]))             // 9   — 可視の稜
nedges(hlr(box(2,2,2),[-1,-1,-1],"hidden"))    // 3   — 隠れた稜
```

  頂点数・面数は三角形メッシュ固有の語彙で、planner（カーネル中立）が持つべき概念ではない。
  実害も出ていた — 表示は cg の `MESH` 形式を決め打ちで読んでおり、nef の cache では**嘘の数**が出ていた
- `nef` 系は境界表現に落としてから数える（非有界は境界を持たないので明示エラー）
- 例: `var m = box(2,2,2); print("NVF", nverts(m), nfaces(m));` → `NVF 8 12`
- 関連: `area`, `volume`, `bbox`, `vert`

### `vert(m, i)` — i 番目の頂点の座標
`2D・3D` · → `値`

**実装**: `cgal.so`（`cg-*`）／ `geomutils.so`（`mf-` / `gg-` / `ch-` / `gu-*`）／ `points.so`（点群）

`nverts` で数えられる列の **i 番目の座標**を返す。返す成分数は `bbox` / `centroid` と**同じ約束**:

| 型 | 返り |
|---|---|
| 3D | `[x, y, z]` |
| `cross2d`（z=0 の 2D） | `[x, y]` |
| `face3d`（平面に置かれた 2D） | **`[x, y, z]`（world）** |

- ⚠⚠ **2026-09-17 に `face3d` の答えが変わった**（`cgal` の `cache_version` 8→9）。
  以前は `face3d` でも枠の中の `[x, y]` を返しており、**置き場所が黙って落ちて**いた:
  高さだけ違う 2 枚の断面が同じ答えを返し、`hull(verts(sec))` が常に z=0 に出ていた。
  `bbox` / `centroid` が  で先に world へ揃っていたので、そちらに合わせた。

- `nverts` と**同じ列を同じ順**で見るので、`i` の範囲は `0 .. nverts(m)-1`。範囲外は明示エラー。
- 例: `var b = box(2,3,4); print("V", vert(b,0)[0], vert(b,0)[1], vert(b,0)[2]);`
- ⚠ **索引は実装依存**。列挙順をなぞるだけなので、版・ビルド・入力順で変わりうる。
  同じ木の中では決定的（別のキャッシュで計算し直しても同じ並びになることを検査している）が、
  **版を跨いで `i` が同じ頂点を指す保証は無い**。
- ⚠ 座標を返す op であって、**構成要素の番号を返すものではない**。連結性（どの面がどの頂点を使うか）
  が要るなら `face_verts(m, i)`。
- 全頂点をまとめて欲しい場合にこの op を回すのは向かない（1 点ずつ値で返すため）。
- 関連: `nverts`, `verts`, `face_verts`, `bbox`, `centroid`

### `verts(m)` — 全頂点を点群で返す
`2D・3D` · → `点群`

**実装**: `cgal.so`（`cg-*`）／ `geomutils.so`（`mf-` / `gg-` / `ch-` / `gu-*`）

`vert` の「まとめて」版。全頂点を **点群**として返す。返る点群の次元は `vert` の成分数と同じ:

| 型 | 返り |
|---|---|
| 3D | `pt-cloud3d` |
| `cross2d` | `pt-cloud2d`（枠の中の座標） |
| `face3d` | **`pt-cloud3d`（world）** |

⚠⚠ `face3d` の返り型は 2026-09-17 に `pt-cloud2d` → `pt-cloud3d` に変わった。
わけは `vert` の項を参照。⇒ 平面上の点群なので、`hull` に渡すと「1 つの平面に載っている＝立体にならない」
という明示エラーになる（以前は z=0 の 2D hull が黙って返っていた）。2D の凸包が欲しいなら
元の 2D 値をそのまま `hull` に渡すか、`project_flatten` で z=0 へ落とす。

- `verts(m)` の `i` 番目は `vert(m, i)` と**同じ点**（検査がこの等式を見ている）。
- 点群なので `distance` / `closest` / `hull` / `voronoi` / `delaunay` にそのまま渡せる。
  例: `volume(hull(verts(box(2,3,4))))` は `24`。
- 1 点だけ欲しいときは `vert` を使う（`verts` は全点をキャッシュに書くため）。
- ⚠ 法線は付かない。要るなら `estimate_normals` を明示的に通す。
- 関連: `vert`, `face_verts`, `nverts`, `hull`, `estimate_normals`

### `face_verts(m, i)` — 面 i の頂点**番号**
`3D` · → `値`

**実装**: `cgal.so`（`cg-mesh3d`）／ `geomutils.so`（`mf-` / `gg-` / `ch-` / `gu-mesh3d`）

面 `i` を構成する 3 頂点の**番号** `[i0, i1, i2]` を返す。番号は `vert(m, i)` / `verts(m)` と**同じ列**を
指すので、座標が欲しければ `vert(m, face_verts(m,i)[k])` と繋ぐ。

- ★★ `vert` と**わざと分けてある**。「*座標*で返す」のと「*構成要素の番号*で返す」は**別の約束**で、
  連結関係（どの面がどの頂点を共有するか）が要る場面では番号が要る。座標から番号へ引き戻すのは
  double のカーネルでは丸めが入って危うい。
- ⚠ mesh 系の「面」は**三角形 1 枚**。`occt` の `face`（トリム面）とは桁が違う値になるが、
  *その違いこそ表現の要点*なのであえて同じ語を使っている（`nfaces` と同じ判断）。
- ★ `face(m, i)`（三角形そのものを mesh で返す op）は**作らない**。百万個の実装依存の索引を
  キャッシュに焼き付けることになり、欲しいのは三角形ではなく**座標か番号**だから。
- ⚠ **2D は面を持たない**ので受け付けない（ルータが弾く）。2D の境界はリングで、`nfaces` は 0。
- ⚠ 索引は実装依存。同じ木の中では決定的（検査が別キャッシュで並びの一致を見ている）だが、
  版を跨いだ保証は無い。⚠⚠ `manifold` が作った値では走ごとに変わりうる
  （[言語リファレンス §10](srava_language_reference.html#mf-tri-order)）。
- 例（面積を自分で組み立てる）:
  ```
  module("manifold.so"); module("geomutils.so",{});
  var b = box(2,3,4);
  var f = face_verts(b, 0);
  print("TRI", vert(b,f[0]), vert(b,f[1]), vert(b,f[2]));
  ```
- 関連: `vert`, `verts`, `nfaces`, `nverts`

### `shell(m, i)` — i 番目の殻を取り出す
`3D` · → `3D`

**実装**: `cgal.so`（`cg-mesh3d`）／ `geomutils.so`（`mf-` / `gg-` / `ch-` / `gu-mesh3d` → `gu-mesh3d`）

`nshells` で数えられる **面の連結成分**の i 番目を取り出す。中空の箱なら 塊 1 個 に対して 殻 2 枚（外側の箱 + 空洞の境界）。

- ★ **向きはそのまま**返す。空洞の殻は法線が内を向いているので `volume` が **負**になる。
  ⇒ **符号がそのまま「外殻か空洞か」の判別子**。
- ★ そのため **Σ 符号つき体積 = 全体の体積** が成り立つ（中空の箱なら `64 + (-8) = 56`）。
- ⚠ `part`（塊）とは**別物**。`part` は「値を分割する片」なので入れ子（どの空洞がどの塊のものか）が要る。
  `shell` は面の連結成分そのものなので入れ子を知らずに取り出せる。
- ⚠ したがって殻は「値を分割する片」ではなく、片の測度の和が全体に一致するのは **符号つきのとき**だけ。
- ⚠ 索引は連結成分の走査順（実装依存）。番号は *指し示す* ためではなく **列挙のため**のもので、
  同じ木の中では決定的だが版を跨いだ保証は無い。
- ⚠⚠ さらに、**manifold が作った値では三角形の並びそのものが走ごとに変わりうる**（仕様として
  受けている）。Manifold は出力の三角形を「元になった mesh が構築された順」で並べるため。
  ⇒ その値では `shell(m, i)` の `i` は**同じセッションの中でしか**同じ殻を指さない。
  位置で指す `shell_at` を使うこと。詳細は
  [言語リファレンス §10 Manifold の三角形の並び](srava_language_reference.html#mf-tri-order)。
- 関連: `nshells`, `nparts`, `part`, `part_at`, `shell_at`, `volume`

### `shell_at(m, [x,y,z])` — 点にいちばん近い殻を取り出す
`3D` · → `3D`

**実装**: `cgal.so`（`cg-mesh3d`）／ `geomutils.so`（`mf-` / `gg-` / `ch-` / `gu-mesh3d` → `gu-mesh3d`）

`shell` の「位置で指す」版。指定した点に **いちばん近い殻**を返す。

- ★ **なぜ索引と 2 通りあるのか**: 殻の番号は連結成分の走査順で、「列挙のため」のもの。
  モデルの書き方を変えると殻の集合そのものが変わるので、番号は当然別の殻を指す。
  ⇒ 書き換えても同じ殻を指し続けたいなら**位置で指すしかない**（`face` / `face_at` が同じ理由で 2 通りある）。
- 例（中空の箱）: `shell_at(m,[0,0,0])` は外殻、`shell_at(m,[2,2,2])` は空洞の殻。
- ⚠ **同距離の殻が 2 つ以上あるときは明示エラー**。黙って片方を選ぶと「同じ式に 2 通りの値」になるため。
  中空の箱で外殻の底と空洞の底から等距離にある点がその例。
- `cgal` では距離は厳密（EPECK）なので同距離の判定も厳密に効く。`geomutils` は double なので
  **相対許容差**（1e-12）で同距離を見る。⚠ 向きは安全側 — 許容差を持たせると*断る側*に倒れる。
  厳密比較にすると丸めで同距離が同距離に見えなくなり、**黙って片方を選ぶ**という一番まずい形になる。
- ★★ `part_at` とは**意味がわざと違う**。`shell_at` は「いちばん近い」・`part_at` は「**含む**」。
  **立体は内側を持ち、曲面は持たない**からで、殻（面の連結成分 = 曲面）には「含む」が定義できない。
- 関連: `shell`, `part_at`, `nshells`, `face_at`

### `nshells(m)` ／ `nparts(m)` ／ `genus(m)` — 位相を数える
`3D` · → `値`

**実装**: `cgal.so` ／ `geomutils.so`（`mf-` / `gg-` / `ch-` / `gu-*`・**要ロード**）（`nparts` は `nef_snc.so` / `nef_hybrid.so` にもある）

形の**位相**を整数で返す 3 本。体積や面積と違って、**近似の誤差が入らない**（数える対象が整数なので、
一致するかしないかしかない）。

| op | 数えるもの | 球 | トーラス | 中空の箱 | 同心球の xor (N 枚) |
|---|---|---|---|---|---|
| `nshells(m)` | 境界シェル = **面**の連結成分の枚数 | 1 | 1 | **2** | **N** |
| `nparts(m)` | 塊 = **立体**の連結成分の数 | 1 | 1 | **1** | **N/2** |
| `genus(m)` | 種数 = 取っ手の総数 | 0 | **1** | 0 | 0 |

- ★★ **`nshells` と `nparts` は別の数**。中空の箱はシェル 2 枚（外殻と空洞の壁）だが塊は 1 個。
  「連結成分の数」とだけ言うとどちらの意味にも取れるので、**2 つとも名前を付けてある**。
- ★ `nparts` は `nef` の `nparts`（SNC の marked volume）と**同じ約束**。
  メッシュ系は面の連結成分しか直接は得られないので、**符号つき体積が正のシェルを数えて**塊に直している
  （向きの揃った閉じた面では、外殻の発散定理の積分が正・空洞の壁は負になる）。
  ⇒ 同じ入力に対して `cgal` / `manifold` / `geogram` / `nef` が同じ数を返す。
- ★ **`nef` へ変換せずに数えられる**ことが要点。掃引規模のメッシュを Nef へ通すと 100GB 級になるので、
  そこでは `nef` の `nparts` は事実上使えなかった。
- ⚠ `genus` は**閉じた 2-多様体でなければ明示エラー**（`chi = 2-2g` が成り立たないため）。
  疑わしいときは先に `valid(m)` で見る。`nshells` / `nparts` は境界があっても数として意味を持つので断らない。
- ⚠ `nshells` / `nparts` は**シェルの入れ子関係までは出さない**（どの空洞がどの塊のものか）。
  塊を*取り出す*のは `part(m, i)` / `part_at(m, p)` で、そちらが入れ子を解く。
- ⚠⚠ **接している立体では塊に割れない**。球の xor（三日月 2 つ）は交線の円で接しているので
  面の連結成分としては 1 枚に繋がり、`nshells` = `nparts` = 1・`valid` = 0 になる。
  掃引の xor 模型（線分上に並べた N 球）も同じ形なので、**「成分数 = N/2」は同心球のときだけ**成り立つ。
- 例:
```
var H = box(3,3,3) --- translate(box(1,1,1),[1,1,1]);   // 中空の箱
print("TOPO", nshells(H), nparts(H), genus(H));          // → TOPO 2 1 0
print("TORUS", genus(torus(3,1,64)));                    // → TORUS 1
```
- 何に使えるか: 位相の健全性を**体積の代理指標に頼らず**判定できる。空洞が埋まる壊れ方
  （OCCT 7.8.1 の）やシェルの脱落は、体積のずれを見る前に `nshells` / `nparts` の整数で出る。
- 関連: `valid`, `volume`, `part`, `convex_decomposition`

### `convex_decomposition(m)` — 凸分解
`3D` · → `mesh`

**実装**: `nef_snc.so` / `nef_hybrid.so` · 型 `nf-mesh3d`(NEF3) / `nfb-mesh3d`(NEFB)

凹形状を**凸片の集まり**へ分解する（`CGAL::convex_decomposition_3`）。`minkowski` が内部で使うのと
同じ分解で、3D `offset` が重いのはここの片数が m×n で効くため。物理エンジンの凸コリジョン形状・
3D プリントのサポート生成などに使う。

**入力** `m` 対象 — `mesh`（3D・**有界**であること）

**出力** 分解した形状 — `mesh`（`nf` 系。**凸片は 1 つの mesh の中に別々の連結成分として入る**）

- ★片は **`nparts(d)` で数えて `part(d, i)` で 1 つずつ取り出せる**（mesh の配列を返す仕組みが無いので
  「数 + n 番目」の 2 本で扱う）。`export` すると片が別成分としても書かれる
- ★分解しても**体積は変わらない**（片は内部で交わらない）
- ★結果は**内壁を持つ**ので `cast("cg-mesh3d", …)` はできない。内壁を消すなら `unify`
- 関連: `nparts`, `part`, `unify`, `minkowski`, `hull`

### `fillet(s, r)` — 稜を丸める
`3D` · → `mesh`

**実装**: `occt.so` · 型 `oc-brep3d`(BREP)

**すべての稜**を半径 `r` で丸める（`BRepFilletAPI_MakeFillet`）。解析曲面のまま丸めるので、
丸めた分の体積は Steiner の公式と 15 桁で照合できる。

**入力**
- `s` 対象 — `mesh`（`oc-brep3d`）
- `r` 半径 — `スカラ`（0 以上）

**出力** 丸めた立体 — `mesh`（`oc-brep3d`）

- `r=0` は入力をそのまま返す。**負値はエラー**（収縮は `offset` の仕事）。
- ⚠ 半径が大きすぎて自己交差する等で OCCT が「作れない」と言ったら **明示エラー**になる
  （黙って入力を返さない）。
- 例: `fillet(box(2,2,2), 0.2)`
- 関連: `chamfer`, `offset`

---

### `chamfer(s, d)` — 稜を面取りする
`3D` · → `mesh`

**実装**: `occt.so` · 型 `oc-brep3d`(BREP)

**すべての稜**を距離 `d` で 45 度に面取りする（`BRepFilletAPI_MakeChamfer`）。

**入力**
- `s` 対象 — `mesh`（`oc-brep3d`）
- `d` 距離 — `スカラ`（0 以上）

**出力** 面取りした立体 — `mesh`（`oc-brep3d`）

- `d=0` は入力をそのまま返す。負値はエラー。
- ⚠ **角（3 稜が集まる点）の扱いには流儀が 2 つある**。OCCT は「角にも平面を立てる」方で、
  素朴に「稜だけ削る」と仮定した式とは 0.25% ずれる（数値積分で確認済み）。
- 例: `chamfer(box(2,2,2), 0.2)`
- 関連: `fillet`, `offset`

---

### `face(s, i)` — **i 番目の面**を取り出す
`2D・3D` · → `2D`

**実装**: `occt.so` · 型 `oc-brep3d`(BREP) / `oc-face3d`(BRP2) → `oc-face3d`(BRP2)

`i` 番目の面を 2D 領域として取り出す（`TopExp_Explorer`）。★ 入力は**立体でも 2D でもよい** —
`project` やブールの結果は**1 枚とは限らない**ので、束から 1 枚を取り出す口が要る。★ 取り出した面は
**平面とは限らない** — 円柱の側面は円筒面のまま出る。`oc-face3d` の実体が
`TopoDS_Face`（任意の曲面 + その (u,v) を切り取るワイヤ）だからで、これは
**メッシュ系が原理的に持てない**（あちらの 2D は平面に生きている）。

**入力**
- `s` 対象 — `mesh`（`oc-brep3d`）または `2D`（`oc-face3d`）
- `i` 面の索引 — `整数`（`0 <= i < nfaces(s)`・**省略不可**）

**出力** その面 — `2D`（`oc-face3d`）

- 例: `area(face(cylinder(0.5,4), 0))` → 円筒側面の面積
- ★ **巡回順は決定的**。同じ面集合なら、同じ式を何度書いても・ブールを挟んでも・
  キャッシュに書いて読み直しても同じ並びになる（4 通りの条件で確かめてある）。
- ⚠ ただし立体の**作り方**を変えると面集合そのものが変わる。`box(2,3,4)` は 6 面だが、
  同じ形を「2 つの箱を積む」で作ると継ぎ目が残って 10 面になる。このとき同じ `i` は
  別の面を指す。**順序規約をどう決めても直らない**（順序ではなく面の集合の問題）⇒
  モデルを書き換えても同じ面を指し続けたいなら `face_at` を使う。
- ⚠ 範囲外の索引・整数でない索引は**明示エラー**（既定の 0 を黙って使わない）。
- ⚠ 取り出した面は多くが曲面なので `polygonize` は**断る**（*平面でありさえすれば*
  `z=0` の外でも渡せる。断られるのは曲面上の面）。
  検証は `area` で行う。
- 関連: `face_at`, `nfaces`, `area`, `extrude`

---

### `face_at(s, [x,y,z])` — 点に**いちばん近い面**を取り出す
`2D・3D` · → `2D`

**実装**: `occt.so` · 型 `oc-brep3d`(BREP) / `oc-face3d`(BRP2) → `oc-face3d`(BRP2)

指定した点にいちばん近い面を 2D 領域として取り出す（`BRepExtrema_DistShapeShape`）。
`face` が索引で指すのに対し、こちらは**位置で指す**。面の割れ方が変わっても
「天面」「あの穴の内壁」を指し続けられる。★ `face` と同じく**立体でも 2D でも**受ける。

**入力**
- `s` 対象 — `mesh`（`oc-brep3d`）または `2D`（`oc-face3d`）
- 点 — `3D ベクトル`（立体の外でも中でもよい）

**出力** その面 — `2D`（`oc-face3d`）

- 例: `area(face_at(box(2,3,4), [1,1.5,9]))` → `6`（天面）
- ⚠ **同距離の面が複数あるときは明示エラー**。稜や角の真上は 2〜3 枚が同じ距離になり、
  どの面を指したいのか式から決まらない（黙って片方を選ぶと同じ式に 2 通りの値が出る）。
  点を稜からずらすか、`face(s, i)` を使う。
- 関連: `face`, `nfaces`, `area`

---

### `loft(s1, s2, …)` ／ `loft_ruled(s1, s2, …)` — 断面の列を通る立体
`2D→3D` · → `mesh`

**実装**: `loft` は `occt.so` のみ · `loft_ruled` は `occt.so` / `manifold.so` / `cgal.so`
（→ [対応表](#module-matrix)） · 型 `oc-face3d`(BRP2) → `oc-brep3d`(BREP) /
`mf-cross2d`(MFC2) → `mf-mesh3d`(MFM3) / `cg-cross2d`(PLY2) → `cg-mesh3d`(MESH)

断面（2D）を 2 枚以上受けて、それらを通る立体を作る。
`extrude` / `revolve` / `tube_ruled` が**断面 1 枚**なのに対し、**断面が複数枚で形が変わってよい**のが
`loft`。船体・翼・ダクトの遷移部など「断面図の列で設計する」分野の中心的な道具。

- `loft` … 断面の列を**なめらかな曲面で通す**（側面は B-spline）
- `loft_ruled` … 断面間を**直線で結ぶ**（線織面）

**入力** `s1, s2, …` 断面 — `2D`（`oc-face3d`）を 2 個以上。★ **配列 1 個**でも渡せる

**出力** 断面を通る立体 — `mesh`（`oc-brep3d`）

- ★★ **断面の置き場所は op が決めない**。利用者が `transform` で空間に置いたものをそのまま使う:

  ```
  loft(circle(1), translate(circle(2), [0,0,4]))                 円錐台
  loft_ruled(rect(2,3), translate(rotate(rect(2,3),"x",20), [0,0,4]))   傾いた断面
  ```

  これが書けるのは 2D が **z=0 平面の外へ出られる**から（`oc-face3d` は  /
  `mf-cross2d` と `cg-cross2d` は  で**平面（枠）を持つ**ようになった）。
- ★ **`loft` と `loft_ruled` は別 op**。線織面は三角形で表せるので**メッシュ系でも実装できる**が、
  なめらかな方は解析曲面が要る。分けてあるので「どのカーネルがどちらを持つか」が表に出せる
  （断面 3 枚で半径を `1 → 2 → 1` と振ると、なめらかな方は膨らむので体積が大きくなる。
  側面の曲面種も `bspline` と `cone` で違う）。
- ★★ **メッシュ系（`manifold.so` / `cgal.so`）の `loft_ruled`** — 断面どうしの点の対応づけは、外周を
  **弧長で正規化**し、**全断面の頂点の和集合**で標本化して、**前の断面の始点に最も近い頂点**から
  始める。どの断面の頂点も失われないので、**対応する稜が同一平面にあるかぎり厳密**
  （平行移動・拡大・傾け）で、`occt` と一致する。
  ⚠ **断面を面内で回した（ねじった）場合は `occt` と一致しない**。対応する稜が同一平面に
  無いとその四角形は双線形パッチになり、三角形では表せないため。立場は `circle(r,segs)` の
  `segs` と同じで、**細かくしたい人は中間断面を足す**（刻むほど `occt` に近づく）。
  ⚠ メッシュ系に `loft`（なめらか）は**無い**（解析曲面が要る）。
  ★ `cgal` は 2D の座標が厳密なので `occt` と**ビット単位で一致**する。`manifold` は 2D が
  Clipper2 の格子に載るぶん下位桁がずれる。★ ねじれた断面では `cgal` と `manifold` が
  **同じ三角形分割**を使うので、両者の値は一致する。
  ⚠ 円を断面にするときは **分割数を明示する**（`circle(1,32)`）。`circle(r,0)` の既定は
  カーネルで違う。
- 検証（閉形式）: 円柱 `πr²h` / 円錐台 `(πh/3)(r₁²+r₁r₂+r₂²)` / 角柱 `w·d·h`
- ⚠ 断面が**複数の面**を持つ場合は**明示エラー**（どの輪をどの輪につなぐかが決まらない）。
  `face(section, i)` で 1 枚に絞る。
- ★ **断面は配列で渡せる**（`loft(A)` = `loft(A[0], A[1], …)`）。断面は式で生成するもの
  （翼型を N 枚・船体の肋骨を M 枚）なので、手で並べずに済む。
  値もキャッシュキーも並べた形と**完全に一致**する。
- ⚠ 断面は 2 枚以上必要。立体（`oc-brep3d`）は断面にできない。
- ⚠ 断面は**平面**でなければならない。`oc-face3d` は曲面の上にも居られるが、
  `loft` は**断面そのものを蓋にする**ので、蓋が平面でないと内外が決まらない ⇒ 明示エラー。
- ⚠ 「断面 → 点」で円錐にするのは**まだ書けない**（頂点を断面として渡す口が無い）。
- 関連: `extrude`, `revolve`, `tube_ruled`, `transform`, `face`

---

### `unify_faces(s)` — 同じ曲面に載る隣り合う面を**1 枚に畳む**
`2D・3D` · → 入力と同じ型

**実装**: `occt.so` · 型 `oc-brep3d`(BREP) / `oc-face3d`(BRP2)

**形は変えない**（面の分け方だけを変える）。`ShapeUpgrade_UnifySameDomain`。

**入力** `s` 対象 — `mesh`（`oc-brep3d`）または `2D`（`oc-face3d`）

**出力** 面を畳んだ同じ形 — 入力と同じ型

- ★ 判定は「接しているか」ではなく**同じ曲面に載っているか**。例:

  | | 前 | 後 | |
  |---|---|---|---|
  | `box(2,2,2)` | 6 | 6 | 曲面が違うので畳まない |
  | `fillet(box(2,2,2),0.3)` | 26 | **26** | ★ 平面と円筒は**接していても別の曲面** |
  | `box ||| 上に積んだ box` | 10 | **6** | 同一平面の継ぎ目が消える |
  | 縦置きトーラスの切り取り | 6 | **4** | パラメータの継ぎ目で割れた分だけ畳まれる |

- ★ これを通すと `nfaces` が**幾何的な意味**を持つ（「本物の 2 か所」と「継ぎ目で割れただけ」を
  区別できる）。
- ⚠ 面積・体積は**厳密には一致しない**。pcurve を作り直すので下位桁が動く。
  値の比較には許容差を使う。
- ★★ **`unify` とは別物**。`unify`（`nef`）は内壁を消すので**体積が変わる**。こちらは形を
  一切変えない。名前を分けてあるのはそのため。
- ★ **黙ってはやらない**（明示 op）。ブールや `project` の出口で勝手に畳むと、枚数が減った
  理由が式に出なくなる。
- 関連: `nfaces`, `face`, `project`, `unify`

---

### `surface_type(f)` — 2D 領域が**どんな曲面の上にあるか**訊く
`2D` · → `値`

**実装**: `occt.so` · 型 `oc-face3d`(BRP2)

`face` / `face_at` で取り出した 2D は**平面とは限らない**ので、`cast` / `polygonize` /
`extrude` が通るのかを**踏む前に**判断する手段として使う。

**入力**
- `f` 対象 — `2D`（`oc-face3d`）

**出力** 曲面種の名前 — `文字列`

`"plane"` / `"cylinder"` / `"cone"` / `"sphere"` / `"torus"` / `"bezier"` / `"bspline"` /
`"revolution"` / `"extrusion"` / `"offset"` / `"other"`

- ⚠ 領域が複数の面を持ち種類が揃っていなければ `"mixed"`、面が 1 枚も無ければ `"empty"`。
  **0 面はエラーではない**（交わらない交差の正当な答え）。面の枚数は `nfaces` で訊ける。
- 例: `surface_type(face_at(cylinder(1,3), [5,0,0]))` → `"cylinder"`
- 関連: `face`, `face_at`, `nfaces`, `bbox`, `centroid`

---

### `surface_through(grid [, mode])` — 格子の点を**通る**自由曲面 {#surface-through}
`値` · → `2D`

**実装**: `occt.so` · 型 `oc-face3d`(BRP2) ·

行×列に並べた点を **通過点**として B-spline 曲面を当てる（OCCT `GeomAPI_PointsToBSplineSurface`）。

**入力**
- `grid` 点の格子 — `[[[x,y,z], …], …]`（`[x,y]` なら `z=0`）。**行数・列数とも 2 以上**
  - または **高さ場** — `[[z, z, …], …]`。`x,y` は原点 0・刻み 1 の等間隔（置き直しは `transform` で書く）
  - ⚠ 1 つの格子に点と数を**混ぜると断る**
- `mode` 省略可 — `"fit"`（既定・許容差 1e-4 で近似）/ `"interp"`（厳密に通す）

**出力** 当てはめた曲面の面 — `2D`（`oc-face3d`）

- ★ **既定が近似なのは、補間のほうが点と点の間で暴れるから**。球冠を格子で標本化して
  比べると、**どちらも与えた点は通る**（差は数値誤差の桁）。違いが出るのは**点と点の間**で、
  そこでは `"interp"` のほうが元の曲面から離れる。
- ⚠ 値配列は op へ渡す段が O(N^1.9)。実用上限は **2 万点 ≒ 141×141**。手で打つ格子を想定した op で、
  密なスキャンを流す口ではない。
- ⚠ 格子は**矩形**であること。ragged なら「どの行が何点か」を言って断る。
- 関連: `surface_control`, `poles`, `set_poles`, `surface_type`

---

### `surface_control(grid)` — 格子を**制御点**として自由曲面 {#surface-control}
`値` · → `2D`

**実装**: `occt.so` · 型 `oc-face3d`(BRP2) ·

行×列に並べた点を **Bezier 曲面の制御点**として使う。手で形を引っぱる道具。

**入力**
- `grid` 制御点の格子 — `[[[x,y,z], …], …]`。**行数・列数とも 2 以上**

**出力** Bezier 曲面の面 — `2D`（`oc-face3d`）

- ⚠⚠ **面は与えた点を通らない**（通るのは四隅の 4 点だけ）。`surface_through` と**同じ綴りを受け取り、
  点の意味だけが違う**ので、取り違えると黙って別の面が出る。同じ格子を渡したとき、
  `surface_through` は点を通り（差は数値誤差の桁）、`surface_control` は**桁違いに離れる**
  — 制御点は「引っぱる重り」であって通過点ではないため。

- ⚠ Bezier の次数は `(行数-1)×(列数-1)` で、OCCT の上限は 25。超えたら**明示エラー**にする
  （黙って B-spline に切り替えない）。密な格子は `surface_through` を使う。
- 関連: `surface_through`, `poles`, `set_poles`

---

### `poles(f)` / `set_poles(f, grid)` — 制御網の**取り出しと差し替え** {#poles}
`2D` · → `値` ／ `2D`,`値` · → `2D`

**実装**: `occt.so` · 型 `oc-face3d`(BRP2) ·

`poles` は Bezier / B-spline 面の制御網を `[[[x,y,z], …], …]` で返し、`set_poles` は差し替えた面を返す。

★ **狙いは「通過点で当てて、制御点で整える」を合成で書けるようにすること。**

```
var S = surface_through(測った点の格子);   // 点を通る面を当てる
var P = poles(S);                          // その制御網を見る
P[1][1][2] = 2;                            // 1 点だけ持ち上げる
var T = set_poles(S, P);                   // 差し替える
```

- ★ 「通過点と制御点を 1 つの op で混在させる」（一部の制御点を固定して残りを通過点条件で解く）は
  定式化としては成立するが **OCCT に口が無く**、未知数と式の数・条件数を自前で見る研究項目になる。
  ⇒ **持たない**。上の合成で足りる。
- ⚠ 解析曲面（平面・球・円柱…）は制御網を持たないので `poles` は断る。`surface_type` で先に訊ける。
- ⚠⚠ `set_poles` は**トリムを保たない** — 差し替えた曲面の自然境界で面を建て直す
  （OCCT に「既存の輪郭を新しい曲面に載せ直す」口が無い）。⇒ 輪郭が 1 本でない面（穴あき等）は
  **黙って落とさず明示エラー**にする。1 本でも自然境界とは限らない点は承知して使うこと。
- ⚠ 格子の行数・列数が面と違えば断る（`poles` が返した形を直して渡す使い方が前提）。
- 関連: `surface_through`, `surface_control`, `surface_type`

---

### `project_flatten(area2d)` — 空間に置かれた 2D を **z=0 へ落とす** {#project-flatten}
`2D` · → `2D`

**実装**: `cgal.so` / `manifold.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 `cg-cross2d` / `mf-cross2d` / `oc-cross2d`

**world 座標の `(x,y)` をそのまま取り、`z` を捨てる**（＝ `z=0` 平面への直投影＝影）。

```
project_flatten(rotate(rect(2,3),"x",45))   面積 6 → 6·cos45 = 4.2426406871
project_flatten(rect(2,3))                  既に平ら ⇒ 恒等（面積 6）
```

**入力** `area2d` 対象 — `2D`（`*-face3d` / `*-cross2d` のどちらも受ける）

**出力** `z=0` へ落ちた 2D — `2D`（`cg-cross2d` / `mf-cross2d` / `oc-cross2d`）

⚠ 以前ここには「occt だけ `oc-face3d` のまま」と書いてあった。 で `oc-cross2d` が
型として立ったので**3 カーネルで揃った**。

- ★ **世界での姿だけで決まる**（＝経路非依存）。同じ図形なら、どんな式で作っても同じ結果。
  `rotate(R,"x",180)` と `rotate(rotate(R,"x",90),"x",90)` は world では同じ図形なので、
  `project_flatten` の結果も同じ `bbox` になる。
- ★ 平面が **XY と平行**なら**等長**（面積・周長は厳密に不変）。傾いていれば `cosθ` で縮む。
- ★ **冪等**。2 回当てても 1 回と同じ。
- ★ 出力は `*-cross2d` なので **SVG に書ける**ようになる。傾いた断面を図面にする唯一の道
  （`export` の SVG は `*-face3d` を断る）。
  ⚠ `manifold` は 2D の SVG/DXF を**持たない**（`cgal` で書く）。
  ★ `occt` は  から自分で書く（**曲線を曲線のまま** — `hlr` の項）。
- ⚠⚠ **`cast` とは別物**。`cast("cg-cross2d", x)` は**名前だけ降ろす**（幾何は 1 ミリも動かず、
  平面が `z=0` でなければ明示エラー）。`project_flatten` は**幾何を動かす**。
- ⚠⚠ **`project` とも別物**（上の項）。`project` は曲面へ投影して切るので**形が変わる**。
  紛れないよう複合語にしてある。
- ⚠ 平面が **world +Z を含む**と影が線に潰れるので**明示エラー**（黙って面積 0 を返さない）。
  判定は法線の `z` 成分で、`extrude` の「掃引方向が面の中を向いている」検査と同じ式。
- ★ **「実形のまま寝かせる」op は無い**（意図的）。同じ図形の表裏は図形自体からは決まらず、
  法線から枠を決める*連続な*規約も存在しない（毛玉の定理）ので、どんな規約にも
  「わずかに傾けただけで結果が跳ぶ場所」ができる。実形が要るなら
  **先に `transform` で XY と平行にしてから**当てる（表裏もそのとき利用者が決める）。
- ⚠ `occt` は**直線の境界なら厳密**だが、**曲線の境界は近似**になる。`BRepAlgoAPI_Common` が
  平面 ∩ 円柱 を楕円と認識せず BSpline に落とすため（45 度に傾けた単位円の影なら、
  厳密値は閉形式 `π·cos45`。近似の分だけ上にずれる）。
- 関連: `cast`, `project`, `extrude`, `section`

### `project(drawing, target, [dx,dy,dz])` — 平面図形を曲面へ**投影して切る**
`2D` · → `2D`

**実装**: `occt.so` · 型 `oc-face3d`(BRP2)

`drawing` を方向 `[dx,dy,dz]` へ投影し、`target` のうち投影に入る部分を返す。
曲面種は保たれる（円筒面へ投影すれば円筒面が返る）。

**入力**
- `drawing` 投影する平面図形 — `2D`（`oc-cross2d` / `oc-face3d` のどちらも受ける）
- `target` 投影先の面 — `2D`（同上・`face` / `face_at` で取り出したもの）
- 方向 — `3D ベクトル`（`[0,0,0]` はエラー）

**出力** 切り取られた面 — `2D`（`oc-face3d`）

- ⚠⚠ **直線投影は閉曲面を複数回当たる**。`project` が返すのは
  **外向き法線が投影方向と逆を向いている面**（＝投影元に顔を向けている面）すべてで、
  ★ **遮蔽は見ない** — 手前の面の陰に隠れていても、こちらを向いていれば返る。
  縦置きのトーラスを下から投影した例: 当たるのは 6 面、返るのは **3 面**
  （下の管の外側 2 枚 + **上の管の内側 1 枚**＝下の管に隠れている面）。
  「いちばん手前の 1 枚だけ」が要るなら遮蔽の判定が要り、それには**投影先の面だけでなく
  立体が要る**（この op は面しか受け取らないので原理的に決められない）。
- ⚠⚠ **結果は 1 枚とは限らない**。凹んだ立体では 1 枚の面の**2 か所以上**に当たる
  （U 字の手前面に帯を投影すると塔 2 本で 2 枚）。束から 1 枚取り出すには `face` / `face_at`。
- ⚠⚠ **面の枚数は幾何だけでは決まらない**。曲面の**パラメータの継ぎ目（seam）**をまたぐ領域は、
  ひと続きの帯でも 2 枚として報告される。例（縦置きトーラスを下から投影）: 幾何としては
  4 本の帯だが `nfaces` は **6**。外側の赤道が `v=0` の継ぎ目にあたり、そこをまたぐ帯が
  `v=[0,0.41]` と `v=[5.87,6.28]` に割れる。★ 継ぎ目の置き方は OCCT の版で変わりうるので、
  **枚数を前提にした式を書かない**（面積や位置で判断する）。
- ⚠ 1 枚の面が手前と奥の**両方**を向くとき（輪郭線をまたぐ輪になる場合・例: トーラスの
  下面に帯を投影）は**明示エラー**。面の粒度では手前だけを取り出せないため。
  面を先に切るか、`cross2d &&& brep3d` で投影が通り抜ける領域全体を取る。
- 例: 円柱（`r=1`）の側面に幅 1・高さ 2 の窓を側方から投影 → `2*asin(0.5)*2` が返る
- ⚠ 相手と交わらない投影は**空の 2D**（エラーではない）。
- 関連: `face_at`, `intersection`, `surface_type`, `hlr`

---

### `hlr(solid, dir[, up][, mode])` — 陰線処理（立体から 2D 図面を起こす）{#hlr}
`3D` · → `2D`

**実装**: `occt.so` · 型 `oc-cross2d`(BRP2) ·

立体を平行投影し、**遮蔽を解いた 2D 図面**を返す。結果は投影面（`z=0`）の上に乗る。

```
hlr(box(2,2,2), [-1,-1,-1])                  // [1,1,1] の頂点から見た図。可視の稜 9 本
hlr(box(2,2,2), [-1,-1,-1], "hidden")        // 隠れた稜 3 本（破線に使う）
hlr(sphere(1.5,16), [0,0,-1], [0,1,0])       // 輪郭の円 1 本（真上から）
export("plan.dxf", hlr(part, [0,-1,0]), "mm")
```

**入力**
- `solid` 対象 — `3D`（`oc-brep3d`）
- `dir` **見る向き** — `3D ベクトル`（視点 → 形。`[0,0,0]` はエラー）
- `up` 図面の上方向 — `3D ベクトル`（省略時は world の `+Z`）
- `mode` — `文字列`（`"visible"`（既定）/ `"hidden"`）

**出力** 図面 — `2D`（`oc-cross2d`・**面を持たない**線の値）

★★ **`project` / `project_flatten` と 3 つとも別物**。名前が似ているので並べる:

| op | 何をするか | 遮蔽 |
|----|-----------|------|
| `project(drawing, target, dir)` | 平面図形を**曲面へ**投影して切る（形が変わる） | **解けない**（面しか受けないので原理的に） |
| `project_flatten(area2d)` | **既にある 2D** を `z=0` へ寝かせる（影） | — （立体を受けない） |
| **`hlr(solid, dir)`** | **立体から**図面を起こす | **解く** |

- ★ 載せるのは **稜（sharp edge）と輪郭（silhouette）**の 2 つ。
  ⚠ 輪郭を載せないと**球が図面から消える**（曲面には稜が無いため）。
  ⚠ 継ぎ目（表現上の seam）と等パラメータ線は**載せない** — 図面の線ではないので。
- ★ 厳密に解く（`HLRBRep_Algo`）。⇒ **円は円のまま**出る。三角形に落とす速い算法もあるが、
  *速くもないのに輪郭が折れ線になる*（円 1 本が多数の線分に化ける）ので持たない。
- ⚠ `up` が `dir` と**平行なら明示エラー**。図面の回転が決まらないため。
  黙って別の向きに差し替えない（図面が黙って回るくらいなら断る）。
  ⇒ 真上から見る図（`dir=[0,0,-1]`）は `up` を明示すること。
- ⚠ 省略できる 2 つ（`up` / `mode`）は**書いたものの種類**で見分ける（3 要素配列 = `up` /
  文字列 = `mode`）。同じ種類を 2 つ渡したら断る。
- ⚠ 出てくる稜は **3D 曲線を持たない**（投影面上の 2D 曲線だけ）。`occt` の内側では
  `BRepAdaptor_Curve` 経由で素性が取れるが、3D 曲線を前提にする道具に渡すと落ちうる。
- ★ 図面は**面積を持たない**（`area` は 0）。領域にしたいときだけ `polygonize(図面, たわみ)`。
  ⚠ 開いた線だけの図面は `polygonize` が断る（`mf-cross2d` に線の置き場所が無い）。
- ⚠ HLR 自体は**中断できない**（進捗の口が無い）。大きな形では `Ctrl+C` を押しても
  その 1 回は走り切る。
- 関連: `project`, `project_flatten`, `nedges`, `export`

---

### `complement(m)` — 補集合
`3D` · → `mesh`

**実装**: `nef_snc.so` / `nef_hybrid.so` · 型 `nf-mesh3d`(NEF3) / `nfb-mesh3d`(NEFB)

内外を入れ替える。**Nef 多面体だから書ける op** で、結果は**非有界**になる。

★ **メッシュ表現では原理的に不可能**。境界を三角形で持つ表現は
「どちらが内側か」を面の向きで表すので、*無限に広がる領域*を値として持てない。
Nef（SNC）は空間を面・稜・頂点の**局所的な入れ方**で分割して各セルに内外の印を付けるので、
非有界なセルにも印を付けられる ＝ 補集合が値になる。⇒ これは「nef が高機能だから」ではなく
**表現に縛られた能力**で、ほかのカーネルに移植する話ではない。

**入力** `m` 対象 — `mesh`（`nf-` のほか `cg-` / `mf-` / `gg-mesh3d` も受け、Nef へ昇格して解く）

**出力** 補集合 — `mesh`（`nf-mesh3d`）

- ⚠ 非有界な値は **体積や書き出しでエラー**になる（`is_simple()` は有界性の判定ではない）。
  有界な形に戻してから測ること
  （例: `intersection(complement(a), box(...))`）。
- 関連: `difference`, `unify`, `part`

---

### `nparts(m)` ／ `part(m, i)` ／ `part_at(m, p)` — 塊の数・n 番目の塊・点の在る塊
`3D` / `2D` · → `値` ／ `mesh` ／ `2D`

**実装**: `nparts` は `cgal.so` / `manifold.so` / `geogram.so` / `nef_snc.so` / `nef_hybrid.so` /
`geomutils.so`。`part` は `cgal.so`（`cg-*`）／ `nef_snc.so` / `nef_hybrid.so`（`nf-*`）／
`geomutils.so`（`mf-` / `gg-` / `ch-` / `gu-*` → `gu-*`）。`part_at` は `cgal.so`（`cg-*`）／ `geomutils.so`（`mf-` / `gg-` / `ch-` / `gu-*`）。

mesh に含まれる**塊（part）の数**と、**i 番目の塊**（0 始まり）。塊 = 独立した立体で、
**空洞は塊に数えない**（空洞つき立体は 1 つの塊。ただしその塊の**境界には空洞の殻も含まれる**）。

- ★**mesh の配列を返す仕組みが無い**ので、「数を返す op」と「n 番目を返す op」の 2 本で塊を扱う
- ★★ **`Σ |volume(part(m,i))| = |volume(m)|`**（符号なし）。`shell` の
  `Σ volume(shell(m,i)) = volume(m)`（**符号つき**）と**別の式**で、この 2 つが別であることが
  `part` と `shell` が別物である理由そのもの。
- ★ 塊を取り出すには**殻の入れ子**（どの空洞がどの塊のものか）を解く必要がある。
  空洞つきの塊で外殻 1 枚だけを返すと*中身の詰まった立体* = **別のもの**になり、体積が黙って増える。
- ★ **2D も同じ相似形**。2D の塊 = 符号つき面積が正のリングとその直接の穴（外周 CCW / 穴 CW）。
  穴の中にまた島がある形では、その島は**別の塊**（3D で空洞の中の立体が別の塊なのと同じ）。
- ★★ `part_at(m, p)` は点 `p` を**含む**塊を返す。`shell_at` が「いちばん近い殻」なのと
  **意味がわざと違う** — **立体は内側を持ち、曲面は持たない**から。
  ⇒ **空洞（穴）の中や立体の外は「そこに材料は無い」と明示エラー**になる。最近傍で代用しない。
  ⚠ 2D では点は**その 2D の平面上**になければならない（面外は断る。黙って射影しない）。
- `convex_decomposition(m)` の結果に使えば**凸片を 1 つずつ**取り出せる（物理エンジンの凸コリジョン
  形状など、片ごとに使いたい用途向け）
- 範囲外の `i` は明示エラー
- ⚠ 索引は走査順（実装依存）。番号は「列挙のため」のもので、版やセッションを跨いで同じ塊を
  指す保証は無い ⇒ 指し続けたいなら `part_at`。⚠⚠ manifold が作った値では**三角形の並び自体が
  走ごとに変わりうる**ので、`i` は同じセッションの中でしか同じ塊を指さない（仕様・。
  [言語リファレンス §10](srava_language_reference.html#mf-tri-order)）。
- 例:
  ```
  module("nef_hybrid.so");
  var d = convex_decomposition(m);
  var n = nparts(d);            // 片の数
  export("piece0.stl", part(d, 0));
  ```
  ```
  module("manifold.so"); module("geomutils.so",{});
  var H = box(4,4,4) --- translate(box(2,2,2),[1,1,1]);   // 中空の箱
  print("V", volume(part(H,0)));          // → 56  (空洞を抱えた塊。殻だけなら 64 になる)
  print("V", volume(part_at(H,[0.5,0.5,0.5])));  // → 56
  // part_at(H,[2,2,2]) は空洞の中なので明示エラー
  ```
- 関連: `nshells`, `shell`, `shell_at`, `convex_decomposition`, `unify`

### `unify(m)` — 内壁除去
`3D` · → `mesh`

**実装**: `nef_snc.so` / `nef_hybrid.so` · 型 `nf-mesh3d`(NEF3) / `nfb-mesh3d`(NEFB)

内部に残った**仕切り面（内壁）を消して**ソリッドを作り直す。接している複数の立体は 1 つに溶ける。

**入力** `m` 対象 — `mesh`（3D・有界）

**出力** 内壁を消した形状 — `mesh`（`nf` 系）

- ★**`repair` とは別物**。`repair` は自己交差を幾何的に解消するだけで**形を変えない**が、`unify` は
  内壁と低次元の破片が落ちるので**体積が変わりうる**。同じ名前にすると「直すつもりが形が変わっていた」
  が起きるので分けてある
- ★**空洞は保たれる**（空洞の境界は片側が立体でないので本物の境界）
- ★**自動ではやらない**（黙って形を変えない・Nef の再構築は重い・部品が残っているなら `union` で書けばよい）
- ★**自己交差した 1 枚のメッシュ**（自分自身を貫く tube など）はこれでは直らない。`unify` は
  正則化（内壁の除去）であって、面どうしの交差を解くものではない。それは `solidify` の仕事
- 例: `unify(convex_decomposition(m))` は分解を元の形へ戻す（内壁だけ消える）
- 関連: `repair`, `solidify`, `convex_decomposition`

### `solidify(m)` — 壊れた境界からソリッドを組み直す
`3D` · → `mesh`

**実装**: `nef_snc.so` / `nef_hybrid.so` / `geogram.so`（→ [対応表](#module-matrix)） · 型 `nf-mesh3d`(NEF3) / `nfb-mesh3d`(NEFB)
／ `geogram.so` · 型 `gg-mesh3d`(MFM3)

**自分自身を貫くメッシュ**（とぐろを巻いた tube など）から、面が囲む領域を正しいソリッドとして
作り直す。**同じ答えを出す実装が 2 つある**:

| モジュール | やり方 |
|---|---|
| `nef_*` | 面 1 枚ごとに Nef を作って n 項 union し、**有界セルを「中身」として塗る**（`CGAL::Mark_bounded_volumes`）。面どうしの交差線は union の過程で実エッジになる |
| `geogram` | mesh arrangement で交差を解き（`MeshSurfaceIntersection`）、radial sort して**外側のシェルだけ残す** |

★ 2 つは**独立実装で、自己交差した掃引でも下位桁まで同じ値を出す**。片方のバグなら
一致しないので、この一致自体が答えの裏取りになっている。

★★ **どちらが実行するかは入力の精度クラスで決まる**（2026-08-25 にこう整理した）:

| 入力の型 | 実行 | |
|---|---|---|
| `cg-mesh3d` / `nf-mesh3d`（厳密な有理数） | `nef_*` | 厳密のまま |
| `mf-mesh3d` / `gg-mesh3d`（double） | `geogram` | double のまま・**桁で速い** |

**昇格も降格も起きない**のがこの振り分けの狙い。効かせ方は priority の梯子で、`geogram` を
`nef` の上（6 > 5）に置いてある。⚠ `geogram` は既定 OFF のビルドオプションなので、**積んでいない
ビルドでは `mf-mesh3d` も `nef` が拾う**（`nef` 側の `(mf-mesh3d)` 行を残してある = 後退させない）。

double の mesh を**厳密に**解き直したいときは、落とす向きを明示するのと同じ作法で書く:

```
solidify(cast("cg-mesh3d", m))     # 厳密へ上げてから nef で解く
```

**入力** `m` 対象 — `mesh`（`nf` 系 / `cg` / `mf`。自己交差したまま入っていてよい）

**出力** 組み直したソリッド — `mesh`（`nf` 系）

- ★**なぜ必要か**: 自己交差した閉メッシュは cgal / manifold / nef のどれも**エラーにせず、
  重なりを二重に数えた体積を黙って返す**（自分を貫く tube では、交差部が二重に数えられる）。
  Nef 構築は面どうしの交差を検査しない（局所の接続だけから SNC を組む）ので素通りする
- ⚠ **自動では検査しない**（設計判断）。`does_self_intersect` は正常なメッシュにも O(n log n) が
  乗るので、変換のたびに払う形にはしない（`modules/nef/c++/nfMesh.cpp` の `nf_try_build`）。
  ★ 自己交差は **`valid(m)` で判る**（3D は 閉 ∧ 自己交差なし）。疑わしい形——掃引で作った管、
  外部から読み込んだ mesh、生成器を自作した形——は、`valid` を通してから使うか、
  `solidify` を明示的に噛ませる。★ **`valid` が `0` を返しても op はエラーにならない**ので、
  「落ちなかった＝正しい」とは読めないことに注意
- ★**cg / mf の mesh もそのまま渡せる**（`nef_*` の all-foreign sig）。実体は codec が nf へ
  昇格読みする。自己交差したメッシュも cg→nf / mf→nf の変換自体は**通る**（Nef 構築は面
  どうしの交差を検査しないので、壊れた形のまま入り、面は保たれる）。cg 入力でも mf 入力でも
  同じ答えになる（mf を `nef` で解くには上表のとおり `geogram` を
  積んでいないか、`module("nef_snc.so",{priority:120})` のように明示的に上げる）
- ★**`repair` を前段に置かないこと**。`repair`（`autorefine`）の出力は**開いた**メッシュなので
  Nef にできず、そこでエラーになる。`solidify` は細分を必要としない（生でも細分済みでも同じ答え）
- ★**空洞は保たれる**。`Mark_bounded_volumes` は有界セルを無差別に塗るので単純に適用すると
  空洞が埋まるが、`solidify` は**連結成分ごとに**組み直し、成分どうしを**入れ子の深さ**で
  合成する（外殻は和・空洞は差）
- ★**重い**。面 1 枚ごとに Nef を作って union するので、**面数に比例して**コストが伸びる
  （通常の Nef 構築より桁で重い）。だから**既定の変換経路には無く**、明示的に呼んだときだけ払う
- 健全な立体に対しては**不変**（箱も中空箱も体積が変わらない）
- ★**geogram 版はコスト特性が違う**。Nef 版が面数に比例して重いのに対し、geogram 版は
  arrangement を 1 回作るだけで済む。★ **`mf-mesh3d` はそのまま渡せる**
  （`gg` と 4CC が同じ `MFM3` = 変換すら起きない）。cgal で作った厳密な mesh を geogram で解きたい
  ときだけ `cast("gg-mesh3d", …)` を明示する — ⚠ これは**厳密 → double の降格**なので、
  `geogram` の sig には書いていない（表現力の高い型から低い型へ落とすのは `cast` だけ）
- 関連: `repair`, `unify`, `valid`, `cast`

### `refine(m, len)` — 形を変えずに面密度だけ上げる
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

**形をまったく変えずに**、すべての辺が `len` 以下になるまで三角形を割る。

**入力** `m` 対象 — `mesh`（3D・三角形メッシュ）／ `len` 目標の辺長（> 0）

**出力** 細分した `mesh`

- ★★**何のためにあるか** — 面数の違うモデルを比べていない、と言える**公平なベンチ**を組むため。
  同じ形のまま面密度だけ動かせるので、「入力の面数を揃えていない」という批判に数値で答えられる
- ★**形は変わらない**。両実装とも体積・面積が動かない（単位箱で 1 / 6）。
  `cgal` 版は **EPECK のまま**回るので**有理数として厳密に**一致する（新しい頂点が
  `A + (B-A)i/n + (C-A)j/n` = 有理数の重心座標で書けるため。`remesh` / `simplify` が
  EPICK コピーへ落ちるのとはここが違う）
- ★**質には手を触れない**。`cgal` 版は全三角形を**同じ n で相似分割**するので、最小角は
  **一切変わらない**（針状の板は針状のまま）。
  ⚠ 三角形の質を*上げたい*なら `remesh`、面数を*落としたい*なら `simplify`
- ⚠ **同じ約束だが作り方が違う**ので、**面数は実装間で一致しない**。manifold は辺ごとに
  分割数を選んで**内部頂点も足す**ので少ない面数で済むかわり、三角形の形は変わる
- ⚠⚠ `manifold` 版は上流に **「`len` 以下」の保証が無い**。理由が 2 つ重なっている:
  ① `RefineToLength` は分割数を**切り捨て**で決める（要求より長い辺が残る）
  ② 辺を割った後に**内部頂点を足して**三角形分割を揃えるので、辺の分割数からは決まらない
  内部の辺ができる。⇒ 最長辺は要求を超えて散る。
  ⇒ srava 側で**作った結果を測り、超えていたら詰めて作り直して**いる。
  *面数ではなく op の約束の方を揃える*、という判断
- ⚠ **三角形メッシュ専用**／**2D には無い**（面を持たない）
- ★ `manifold` 版は**中断できる**（`RefineToLength` は上流の eager op で `ExecutionContext` を見る）
- 関連: `remesh`, `simplify`, `nfaces`

### `remesh(m, len[, iter[, sharp]])` — 辺長を揃えて三角形を張り直す
`3D` · → `mesh`

**実装**: `cgal.so` · 型 `cg-mesh3d`(MESH)

**形は変えずに**、辺の長さが `len` くらいに揃うよう三角形を張り直す（等方リメッシュ）。
★ **面数を減らす op ではない**（減らすのは `simplify`）。むしろ増える — 単位箱を `len=0.25` で
回すと 12 面 → 258 面になる。変わるのは**三角形の質**で、形（体積・面積）は動かない。

**入力** `m` 対象 — `mesh`（3D・三角形メッシュ）／ `len` 目標の辺長（> 0）／
`iter` 反復回数（省略時 3）／ `sharp` 鋭角とみなす二面角の度数（省略時 60）

**出力** 張り直した `mesh`（`cg-mesh3d`）

- ★**何に効くか** — 用途は 2 つ:
  ① **針状三角形（sliver）を潰す**。sliver はブール演算の脆さの主因なので、渡す前に潰せる。
     極端に薄い板のように最小角が潰れたメッシュでも、`len` を細かく取れば**最小角が桁で上がり**、
     体積・面積は動かない
  ② **入力の面密度を揃える**。面数の違うモデルを比べていない、と言えるベンチが組める
- ★**鋭角エッジは既定で保護する**（`sharp` 度を超える二面角のエッジを拘束する）。
  ⚠ 保護しないと箱の角が削れて体積・面積が痩せる。
  **黙って形が変わる**のを避けるためにこちらを既定にした。`sharp` に 180 以上を渡すと
  1 本も拘束しない＝素の等方リメッシュになる（一様さは上がるが角は落ちる）
- ⚠ **三角形メッシュ専用**。4 辺以上の面を持つ mesh は明示エラーになる
- ⚠ **2D（`cg-cross2d`）には無い** — 等方リメッシュは曲面の三角形分割の話で、多角形領域に
  対応物が無い
- ★**中身は EPICK（double）のコピー**で解いて EPECK へ戻す（戻しは無損失）。理由は 2 つあって
  片方は原理的: どちらの op も**新しい頂点位置を決める**ので厳密な答えというものが無く、
  CGAL 側も浮動小数前提（`isotropic_remeshing` は EPECK でも通るが**桁違いに遅く**、
  結果も違った）。詳細は `modules/cgal/c++/cgRemesh.cpp` の冒頭
- 関連: `simplify`, `repair`, `solidify`, `valid`

### `simplify(m, n)` — 形を保ったまま面数を落とす
`3D` · → `mesh`

**実装**: `cgal.so` · 型 `cg-mesh3d`(MESH)

面数が `n` **以下**になるまでエッジを潰して簡約する（`Surface_mesh_simplification`・
LindstromTurk の cost / placement）。

**入力** `m` 対象 — `mesh`（3D・三角形メッシュ）／ `n` 目標の面数（>= 4）

**出力** 簡約した `mesh`（`cg-mesh3d`）

- ★★**「面数」と「形」を分離して測れるようになる**のがこの op の本題。従来、面数を振る唯一の
  口は `sphere(r, seg)` の `seg` だったが、それは**球の族そのものを変えて**しまう（面数と一緒に
  囲む体積も動く）。`simplify` なら **1 つの形から面数だけ違う族**を作れる
- ★**体積が保たれる**。LindstromTurk は体積保存の制約を解いて頂点位置を決めるので、
  細かく張り直した箱を桁で面数を落としても **体積・面積は箱のまま**
- ⚠ **三角形の質は落ちる**（最小角は桁で悪化する）。質が要るなら後段に `remesh` を
  噛ませる ＝ 2 つは**対で使う**
- ⚠ 目標面数は **「以下」で止まる**（CGAL の stop predicate が下から抜けるため）。2000 を頼むと
  1998 が返る、という程度のずれは普通に出る
- ★ 目標が現在の面数以上なら**何もせず素通しする**（エラーにしない）。面数を掃引するとき、
  上限側で黙って通る方が書きやすいため
- ⚠ **2D（`cg-cross2d`）には無い** — 面を持たないので落とす対象が無い
- ★ `remesh` と同じく **EPICK コピー上**で解く。⚠ `LindstromTurk_cost` は `sqrt` を要求するので
  EPECK では**コンパイルすら通らない**（`Lazy_exact_nt` の `Sqrt` が `Null_functor`）
- 関連: `remesh`, `nfaces`, `repair`

### `simplify_cleanup(m, tol)` — 形のずれを `tol` 未満に抑えて潰せるだけ潰す
`3D` · → `mesh`

**実装**: `manifold.so` · 型 `mf-mesh3d`(MFM3)

**面が `tol` 未満しか動かない**範囲で頂点を間引く。ブール演算のあとに残る極小辺や針状三角形の
**掃除**に使う。

**入力** `m` 対象 — `mesh`（3D）／ `tol` 許容差（>= 0・0 は「メッシュ自身の許容差」を意味する）

**出力** 掃除した `mesh`

- ★**`simplify(m, n)` とは約束が逆向き**なので名前を分けてある（→ [op 名の付け方](#name-suffix)）:
  `simplify` は**面数**を指定して形を守り、`simplify_cleanup` は**形のずれの上限**を指定して
  面数は成り行きに任せる
- ★**新しい頂点を作らない**。結果は元の頂点の**部分集合**で、どの面も `tol` 未満しか動かない
  （上流 `Manifold::Simplify` の約束）
- ⚠ **効き始めると形は動く**。`tol` を上げるほど面数は落ちるが、**体積も一緒に落ちる**
  （`tol` は「どれだけ動いてよいか」であって「どれだけ残すか」ではない）。
  ⇒ **面数だけを振りたい測定には使えない**（それは `simplify`）。用途は掃除
- ⚠ `tol` が小さすぎる（または 0）と**ほとんど何も起きない**。厳密に組んだ形はメッシュ自身の
  許容差が 0 なので、`tol=0` は実質そのまま返る
- 関連: `simplify`, `refine`, `remesh`, `repair`

### `color(m, c)` — 面に色をつける
`3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` · 型 `cg-mesh3d`(MESH) / `mf-mesh3d`(MFM3)

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

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `geomutils.so` / `occt.so`（occt の中だけ）／ 橋渡しの `nef_cg.so` / `nef_mf.so`（→ [対応表](#module-matrix)）
· 目標型を産出できるモジュールへ routing する（下の**型相互変換表**）

`mesh` を目標**型** `target_type` へ明示的に移す（rev4 型ディスパッチ）。無損失方向（Manifold→CGAL の昇格）は
自動でも `cast` でも起こせる。損失方向（CGAL→Manifold のダウングレード）は `cast` で明示する。

> ## ★ `cast` に置けるのは「精度を変えない変換」だけ
>
> **`cast` は精度パラメータを取らない。** 粒度・分割数・許容誤差が要る変換は、
> **橋渡しモジュールの明示 op** として書く（ひさ指示・2026-09-01）。
>
> | | 書き方 |
> |---|---|
> | 精度を変えない | `cast("cg-mesh3d", mfMesh)` — double→EPECK の昇格 |
> | 精度が要る | `triangulate(brep, defl)` — B-rep → 三角形（`occt_mf.so`）<br>`voxelize(mesh, dx)` — メッシュ → ボクセル（`openvdb_cg.so` 他）<br>`isosurface(grid, iso)` — ボクセル → メッシュ（同上） |
>
> ## ⚠⚠ `cast` は**置き場所 (枠) を落とさない**
>
> 平面に置かれた 2D (`*-face3d`) をカーネル間で移しても、**載っている平面は保たれる**。
> 2026-09-17 までは `cg-face3d` を `mf` / `gu` へ渡すと**枠が黙って捨てられ z=0 へ戻っていた**
> (で修正・`manifold` の `cache_version` 6→7)。 の規約②「表現力の高→低の
> 落下は `cast` のみ」を、`cast` 自身が*黙って*破っていた形。
>
> ⚠ `line(...)` のガイド (開いた折れ線) を含む 2D は `mf` / `gu` の表現に載らないので
> **明示エラー**になる。黙って捨てると `nverts` が `cgal` と食い違うため。
>
> 理由: `cast(T, x)` の第 1 引数は**目標型名**で、routing の入口。ここに粒度を足すと `cast` が
> 「型変換」と「精度指定」の 2 つの意味を持つ。また粒度は「同じ入力から違う結果」を生むので、
> **op の名前と引数として表に出し、キャッシュキーに現れさせる**のが正しい。
> 詳細は[モジュールリファレンス §型変換の規約](srava_module_reference.html#conversion)。

**入力**
- `target_type` 目標型 — `string`（`"cg-mesh3d"`=CGAL 厳密 3D / `"mf-mesh3d"`=Manifold 3D /
  `"cg-cross2d"`=CGAL 2D / `"mf-cross2d"`=Manifold 2D）
- `mesh` 対象 — `mesh`(3D)または `poly`(2D)

**出力** 同じ形状を `target_type` で表した mesh — `mesh`

- ★**成立条件**: `cast(T, mesh)` は「**`mesh` が書き出す 4CC を、目標型 `T` のモジュールが読める**」ときだけ成立する。
  - cgal.so は `MESH`/`PLY2`（読み書き）に加え `MFM3`/`MFC2` を **readonly 昇格**で読む → `cast("cg-mesh3d", …)` / `cast("cg-cross2d", …)` は cg でも mf 入力でも可（mf→cg は無損失）。
  - manifold.so は `MFM3`/`MFC2`（読み書き）に加え、**`MESH`（CGAL 3D）/ `PLY2`（CGAL 2D）を readonly ダウングレードで読む**（有理数→double・損失）→ `cast("mf-mesh3d", cg_mesh)` / `cast("mf-cross2d", cg_cross2d)` とも**成立（損失）**。
- **型相互変換表**（目標型ごとに「受けられる入力型」・記述子の `sig` から生成）:

| 目標型 | 受けられる入力型（`=` は自型＝実質 no-op） | 受けるモジュール |
|---|---|---|
| `cg-mesh3d` | `cg-mesh3d`= / `mf-` / `gg-` / `ch-` / `gu-` / `nfb-mesh3d` ／ `nf-mesh3d` | `cgal.so` ／ `nef_cg.so`（`nf-` から） |
| `mf-mesh3d` | `mf-mesh3d`= / `cg-` / `gg-` / `ch-` / `gu-` / `nfb-mesh3d` ／ `nf-mesh3d` | `manifold.so` ／ `nef_mf.so`（`nf-` から） |
| `gg-mesh3d` | `gg-mesh3d`= / `cg-` / `mf-` / `ch-` / `gu-mesh3d` | `geogram.so` |
| `ch-mesh3d` | `ch-mesh3d`= / `cg-` / `mf-` / `gg-` / `gu-mesh3d` | `cherchi.so` |
| `nf-mesh3d` / `nfb-mesh3d` | 自型= / `cg-` / `mf-` / `gg-` / `ch-` / `gu-mesh3d` | `nef_snc.so` / `nef_hybrid.so`（各々の自型へ） |
| `gu-mesh3d` | `gu-mesh3d`= / `cg-` / `mf-` / `gg-` / `ch-mesh3d` | `geomutils.so` |
| `cg-cross2d` | `cg-cross2d`= / `mf-` / `gu-cross2d` ／ `cg-` / `mf-` / `gu-face3d`（**枠を落とす**） | `cgal.so` |
| `mf-cross2d` | `mf-cross2d`= / `cg-` / `gu-cross2d` ／ `cg-` / `mf-` / `gu-face3d`（同上） | `manifold.so` |
| `gu-cross2d` | `gu-cross2d`= / `cg-` / `mf-cross2d` ／ `cg-` / `mf-` / `gu-face3d`（同上） | `geomutils.so` |
| `cg-face3d` | `cg-face3d`= / `mf-` / `gu-face3d` | `cgal.so` |
| `mf-face3d` | `mf-face3d`= / `cg-` / `gu-face3d` | `manifold.so` |
| `gu-face3d` | `gu-face3d`= / `cg-` / `mf-face3d` | `geomutils.so` |
| `oc-face3d` / `oc-cross2d` | `oc-face3d` / `oc-cross2d`（**occt の中だけ**） | `occt.so` |

  - ⚠ **occt とメッシュ系の間に `cast` は無い**。B-rep ⇄ メッシュは粒度が要るので
    `triangulate` / `polygonize`（[occt_mf.so](srava_module_reference.html#occt_mf)）という**別 op**。
    ボリューム（`vd-grid3d`）も同じ理由で `voxelize` / `isosurface`。
  - ⚠ **`*-face3d` → `*-cross2d` は「枠を落とす」降格**。枠が既定（z=0）でなければ
    **明示エラー**にする（幾何は 1 ミリも動かさない）。寝かせたいなら
    [`project_flatten`](#project-flatten) を書く。

  - **昇格**（mf→cg）: `cg-mf-upgrade` codec。double は 2 進有理数なので**無損失**で EPECK 厳密化。自動（sig routing）でも `cast` でも起こる。
  - **降格**（cg→mf）: `mf-cg-downgrade` codec。有理数→double 化で**損失**。損失を伴うため `cast` による**明示**のみ。
  - `—`: **次元（3d↔2d）は跨げない**。次元を変えるのは `extrude`（2D→3D）/ `section`（3D→2D）等の op であって `cast` ではない。
- `"cg-…"`: → CGAL(厳密)。Manifold(double)入力は**無損失で厳密化**（double は 2 進有理数）。
- 目標型は**次元を含む**（3d/2d）ので曖昧さがない。
- 既に目標型なら実質 no-op（再エンコードのみ）。
- 例: `var m = cast("cg-mesh3d", box(40,40,40) ||| sphere(20));  // Manifold で速く作り無損失で厳密化`
- 例: `export("fast.stl", cast("mf-mesh3d", box(2,2,2) ||| box(1,1,3)));  // 厳密 3D→高速へ（損失）`
- 関連: `valid`, `volume`

### `module(so|列[, opts])` — モジュールのロード / 設定の上書き
文 · → `文字列` または `配列`

**実装**: 組み込み · 型 -

モジュールを**ロード**し、必要なら**選択優先度・実行方式を上書き**する planner 側関数。

★ **モジュールは `module()` で名指したときにロードされる**（起動時の一括ロードは無い）。一度も
呼ばなければ、そのモジュールの op は `no module can execute op` または `undefined variable` で
明示エラーになる。実カーネル一式をまとめて欲しいときは `include "module/all.sra";`
（→ [モジュールリファレンス](srava_module_reference.html)）。

★ **`module` はロード順を変えない**。既にロード済みなら記述子の内容を書き換えるだけで、
`.so` そのものの差し替えにはならない（差し替えは下記 `module_reload`）。

**入力**
- `so` モジュール — `文字列`（`"cgal.so"` / `"manifold.so"` / `"nef_snc.so"` 等。ファイル名だけなら探索路から解決）
  または **`配列`**（下記「列でまとめて読む」）
- `opts` 上書き設定 — 省略可
  - `"off"` — `文字列`。**アンロードする**（`dlclose`）。以後 `module(so, {})` で読み直せる。
    ★ 文字列オプションは `"off"` **だけ**（`"on"` は無い。ロード / 再ロードは `module(so)` か `module(so, {…})`）。
    ★ 未ロードへの `"off"`、および**一度でも op を実行した**モジュールへの `"off"` は、黙って無視せず
    **明示エラー**になる（前者は名前の打ち間違いに気づけるように、後者はその `.so` 由来のメッシュや
    agent が生きている可能性があるため）。落とせるか先に確かめるなら `module_loaded`
  - `{priority, exec_default, arity}` — `ハッシュ`
    - `priority` 選択優先度 — `整数`。同じ型/op を複数モジュールが提供するとき、どれを既定に寄せるか（大きいほど優先）。
      ★**同点の勝敗は不定**（ロード順 = ディレクトリ走査順に依存）。確実に切り替えるなら既存の最大値より大きい値を指定する
    - `exec_default` 実行方式 — `文字列`（`"process"` 別プロセス / `"thread"` 同一プロセス内スレッド）。op ごとの重さに応じて上書き
    - `arity` 多オペランド評価の項数 — `2 以上の整数`。`union(a,b,c,…)` を木に分解するとき、
      **1 ノードあたり何項まとめるか**の上限（既定 2 = 従来どおりの二項の木）。大きくすると
      中間ノードが減り、カーネルによっては n 個をまとめて 1 回の交差計算にできる
      （geogram は最大 32・manifold / OCCT は上限なし・cgal は二項のみ）。
      ★ **モジュールが受けられる上限を超えては効かない**（cgal に `arity: 8` を書いても二項のまま）
  - **省略** — `module(so, {})` と同じ（ロードのみ・記述子は触らない）

**出力** — `文字列`（モジュール名。`"cgal.so"` を渡すと `cgal`）。列を渡した場合は `配列`

#### 列でまとめて読む — `module(配列, opts)` {#module-list}

引数が**配列**なら、要素は**ファイル名ではなく[記述子名（モジュール名）](srava_module_reference.html)**として読む。
列は[候補列](srava_language_reference.html#module-qualified)と**同じ構造規則**（入れ子は平坦化・
穴は `null` / `0` / `""`・ハッシュは[擬似モジュール](srava_pseudo_module_reference.html)）で読み、
**平坦化後の列と 1 : 1 の配列**を返す。

| 要素 | すること | 返るもの |
|---|---|---|
| 記述子名（ロード済み） | 何もしない | その記述子名 |
| 記述子名（未ロード） | `<名前>` + `.so`（Windows / Cygwin は `.dll`。⚠ **mac も `.so`**）を**探索路から**引いてロード | その記述子名 |
| ハッシュ（擬似モジュール） | 何もしない | そのハッシュ（同じ位置） |
| 穴（`null` / `0` / `""`） | 何もしない | `null`（同じ位置） |

★★★ **不変式**: `use module(L, {})` の候補列は `use L` と**完全に同一**。違いは
**ロードという副作用だけ**。穴とハッシュを「そのまま同じ位置へ」流すのはこのため。

```
use module(["occt", "cgal"], {});      // 2 本読んで、その順で解く
                                       // ≡ module("occt.so",{}); module("cgal.so",{}); use ["occt","cgal"];
```

- `opts` は**列の全部に同じものを適用**する。ロード順は**書いた順**に進む
  （priority 同点の tie-break が列の順と揃う）。
- `{optional: 1}` の**不在**は、その位置に**穴（`null`）**を置く（`use` から見て「書かなかった」と等価）。
- 列に対する `"off"` は**明示エラー**。アンロードは 1 本ずつ `module("<名前>.so", "off")` で行う。
- ⚠ 要素が**記述子名でない**ときは、特別扱いせず**素の「不在」**になる。推測生成は
  **探索路だけ**を見るので、`module(["cgal.so"], {})` は `cgal.so.so` を探して不在、
  `module(["/opt/x/mymod"], {})` は「記述子名にパス区切りは入らない」で不在になる。
  エラー文には**探した綴り**が出る。
- ⇒ **探索路の外の `.so`**、および**記述子名とファイル名が違う `.so`** は、列では読めない。
  あらかじめ `module("<パス>", {})`（文字列形）で明示的にロードしておく
  （ロード済みなら列の側は何もしない）。

- 例: `module("manifold.so", {priority: 99});`（既定幾何カーネルを Manifold へ）
- 例: `module("cgal.so", {exec_default: "thread"});`（cgal の op を同一プロセスで実行）
- 例: `module("cgal.so", "off"); … module("cgal.so", {});`（落としてから読み直す）
- 例: `use module(["occt", "cgal"], {});`（列をまとめて読み、そのまま候補列として敷く）
- 例: `module(["cgal", "nosuch"], {optional: 1});` → `["cgal", null]`（入っていないものは穴）
- 例: `module("geogram.so", {arity: 8}); var m = union([b1, b2, …, bN]);`
  （8 個ずつまとめて畳む。★ **式は書き換えずに** 1 パラメータだけで分解の粒度が変わる。
  ⚠ `arity` を変えると中間ノードのキャッシュキーも変わる（別の木になるため）。切り替えて比べるときはキャッシュ dir を分ける）
- 詳細な引数仕様・記述子 ABI は[**モジュールリファレンス**](srava_module_reference.html)へ。
- 関連: `cast` · `module_loaded` · `module_reload`

---

### `module_loaded(so)` — ロード済みか
文 · → `値`

**実装**: 組み込み · 型 -

その `.so` が**いまロードされているか**を返す。引数の書き方は `module()` と同じで、ファイル名だけなら
探索路から解決される。

`module(so, "off")` が実アンロードになり、未ロードへの `"off"` が明示エラーになったので、
**落とす前に確かめる**手段として要る。

**入力**
- `so` モジュール — `文字列`

**出力** — `値`（ロード済みなら `1`・未ロードなら `0`）

---

### `modules([形])` — 載っているモジュール {#modules}
文 · → `配列` または `文字列`

**実装**: 組み込み · 型 -

引数で **2 つの顔**を持つ。どちらも並びは **priority 降順**（= ディスパッチが
候補を見る順）・同点は登録順。

| 書き方 | 返り値 | 用途 |
|---|---|---|
| `modules()` | **名前の配列** `["manifold","cgal","occt"]`（op を持つものだけ） | [候補列](srava_language_reference.html#module-qualified)へ渡す（`use modules();`） |
| `modules("priority")` | `"manifold:99 cgal:20 …"` の文字列 | `module()` の priority 指定が効いているかを目で見る |

★★ **`use modules();` は振る舞いを変えない**。配列が priority 降順なので、候補列の
「先勝ち」が従来の「priority 最大」と同じ結論になる（同点の扱いも両方登録順）。
これを足かかりにして `use concat(["occt"], modules());` のように **先頭だけ差し替える**と書ける。

⚠ **2 つの顔は同じ並びの別表現ではない**。文字列版は「**載っているものを全部見せる**」道具なので、
番兵 `delayed`（id 0 =「モジュール無し」を表す内部の値）も組み込みの `pig` も隠さない—隠すと
`which()` の答えと食い違うため。配列版は「**候補列へ渡す値**」なので、**op を 1 つでも宣言している
モジュール**だけを返す。

★ 組み込みの `pig`（`.so` 由来でなく D_REF の codec だけを提供する記述子）が配列に**出ない**のは、
op 行を持たない記述子は**ディスパッチが構造的に選べない**から。候補列から抜いても解決結果は
1 つも変わらない（codec の検索は候補列を通らないので `export` もキャッシュも無傷）。
⚠ **逆に、手で `"pig"` を列に書くと害がある**: op 表を持たない記述子は「その op を持つか」に
**不明**を返すため、「どれもその op を持たない」という正確なエラーが出なくなる。

**入力** `形` 省略可 — `"priority"` のみ。他の文字列は**エラー**（黙って配列に倒さない）

**出力** — `配列`（既定）または `文字列`（`"priority"`）

- 例: `print(modules());` → `["manifold","cgal","occt"]`
- 例: `use modules();` — いまの順を **明示的に固定**する（以降の `module()` は候補列を動かさない）
- 関連: `which` · `type_of` · `module` · `module_loaded` · [`use` 文](srava_language_reference.html#use-modules)

---

### `mod_only([a, ]b)` — 候補列の**積**（順はそのまま絞る） {#mod_only}
文 · → `配列`

**実装**: 組み込み · 型 -

`a` のうち **`b` に在る名前**と、**`a` 側の[擬似モジュール](srava_pseudo_module_reference.html)**を、
**`a` の順のまま**返す。候補列は優先順位表なので、*順序を変えずに絞る*ことがこの op の役目である。
（擬似を通すのは**呼び手が選んだ粒度を関数の境界で落とさない**ため。詳しくは下の ★★。
擬似も落としたいときは [`mod_only_names`](#mod_only_names)。）

```
use ["occt", "cgal"];
mod_only(USE_MODULES, ["manifold", "cgal"]);   // → ["cgal"]        (occt は b に無いので落ちる)

use [ pm_cgal({seg:64}), "cgal" ];
mod_only(USE_MODULES, ["cgal"]);               // → [<pm_cgal>, "cgal"]  ★ 擬似は残る
```

#### 1 引数形 `mod_only(sup)` — 左辺を**補う** {#mod_only-1}

第 1 引数を省くと「**いま解こうとしている列**」が使われる:

| 呼び手の状態 | 左辺になるもの |
|---|---|
| `use` で候補列を宣言している | その列（`USE_MODULES`） |
| **何も宣言していない**（`""` / `null` / `0` / `[]` / 全部穴） | **`modules()`**（載っているもの・dispatch 順） |

⇒ ライブラリ関数の宣言はこの 1 行で書ける
（→ [言語リファレンス §ライブラリ関数の宣言](srava_language_reference.html#lib-use-decl)）:

```
var sup = ["cgal", "manifold"];       // この関数が対応するカーネル
use mod_only(sup);                    // ★ これだけ
```

契約は 3 つ:

- 呼び手が選んだカーネルが `sup` に在る → **それで解く**（選択を殺さない）
- 呼び手が**何も言っていない** → **載っているもの** ∩ `sup` から選ぶ（既存のスクリプトはそのまま動く）
- 呼び手の選択が `sup` と**交差しない** → **エラー**（黙って別のカーネルで解かない）。
  文言は「空の列を書いた」ではなく **`use mod_only(...): none of the modules in effect is
  supported here`** になり、ライブラリ側の宣言の行番号が出る

**入力**
- `a` 絞られる側 — `配列` または `文字列`（候補列と同じ規則で読む。入れ子は平坦化）。**省略可**
- `b` 残す名前 — `配列` または `文字列`（同上）

**出力** — `配列`（文字列と[擬似モジュール](srava_pseudo_module_reference.html)）

- ★★ **`a` 側の擬似モジュール（ハッシュ）は、`a` の位置のまま通す**（2026-09-25）。
  擬似は *呼び手が選んだ粒度* であって、どの実モジュールが生き残るかとは**別の軸**だから。
  ⚠ これが無いと**粒度の既定値がライブラリ関数の境界で消える**（`use [pm_cgal({seg:64})]` の下で、
  op を直に呼ぶと 384 面・ライブラリ関数経由だと 192 面 = 擬似なしと同じ、という形になる）。
  **落ちずに値が返る**ので気づけない。擬似を落としたいときは下の `mod_only_names`。
- ⚠ `b` 側の擬似は**落とす** — 名前が無いので「許す名前」になれないため。
- ⚠ [穴](srava_language_reference.html#cand-hole)（`null` / `0` / `""`）は**両辺とも落とす**。
- `a` の**重複はそのまま残す**（列は集合ではなく優先順位表）。
- 一致は**文字列の完全一致**。ロードされているかは**見ない**（載っていない名前は
  [`use`](srava_language_reference.html#use-modules) 側が従来どおり飛ばす）。
- 例: `mod_only("", ["cgal"])` → `[]`（`""` は穴）/ `mod_only(["a",["b"]], ["b"])` → `["b"]`（平坦化）
- 関連: `mod_only_names` · `modules` · `module` · `use`

---

### `mod_only_names([a, ]b)` — 積のうち**名前だけ**を返す {#mod_only_names}
文 · → `配列`

**実装**: 組み込み · 型 -

[`mod_only`](#mod_only) と**積の取り方は同じ**で、違いは **`a` 側の擬似モジュールも落とす**ことだけ。
⇒ 返るのは**文字列だけ**の配列。1 引数形 `mod_only_names(sup)` の**左辺の補い方も `mod_only` と同一**
（宣言があればその列・無ければ `modules()`）。

```
use [ pm_cgal({seg:64}), "cgal" ];
mod_only(["cgal"]);         // → [<pm_cgal の擬似>, "cgal"]   呼び手の粒度を引き継ぐ
mod_only_names(["cgal"]);   // → ["cgal"]                     粒度を **意図的に無視する**
```

- ★ 用途は「**ライブラリ関数が呼び手の粒度を意図的に無視して、自分で制御したい**」場合。
  既定は `mod_only`（呼び手の選んだ粒度を尊重する）で、こちらは**明示的に降りる**口である。
- ⚠ 落ちたときの文言も**書いた op 名**で出る（`use mod_only_names(...): none of the modules
  in effect is supported here …`）。
- 関連: `mod_only` · `modules` · `use`

---

### `type_of(x)` — 幾何型名
文 · → `文字列`

**実装**: 組み込み · 型 -

`x` の**幾何型名**を返す（`cg-mesh3d` / `mf-mesh3d` / `oc-brep3d` / `vd-grid3d` …）。
スカラ・文字列・配列は `"value"`。**式の途中でカーネルが変わったこと**を目で確認するための op。

★ 判断の材料は**型スタンプ 1 つ**。planner の型ディスパッチ（`sig_dispatch`）が決めた
*勝った sig 行の出力型*がそれで、**継続に載る文字列とキャッシュハンドルに載る文字列は同一**。
⇒ cold（計算した）と warm（キャッシュに当たった）で答えが変わらない。
★ 値も参照も**型を持つ**（`"value"` = 値キャッシュ / `"ref"` = `export` の戻り）。
「型の無い出力」は存在しない。

★ **計算の完了は待たない**。型スタンプは継続（promise）に載っているので、値を待たずに読める。
⇒ `type_of` を挿しても**同期点にならない**（挿しただけで並列度が変わる、ということが起きない）。

⚠ ただし **`x` の評価結果がエラーならエラーを返す**（2026-09-21 変更）。以前は
`type_of(nosuchvar)` が `"value"` を返し、エラー表示も無く正常終了していた
— エラーは作られていたのに読み捨てられていた。

⚠ **agent の中で失敗した計算には、宣言された型を答える**（`type_of(points3d("not an array"))` →
`"pt-cloud3d"`）。完了を待たない以上、失敗をまだ観測できないため。これは承知のうえの割り切りで、
`x` が「何になるはずか」を答える op だと読むこと。

**入力** `x` 調べる対象 — `mesh` または `値`

**出力** — `文字列`（型名。複数候補なら CSV）

- 例: `print(type_of(box(2,2,2)));` → `cg-mesh3d`
- ⚠ **値の種別は答えない** — `3` も `3.0` も `"abc"` も `[1,2]` も一様に `"value"`。
  そちらは [`kind_of(x)`](#kind_of)（軸が違う）。
- 関連: `kind_of` · `which` · `modules` · `cast` · `describe`

---

### `kind_of(x)` — 値の種別 {#kind_of}
文 · → `文字列`

**実装**: 組み込み · 型 -

`x` の**値の種別**を返す。返る語は次の 8 つ:

| 返る語 | 何か |
|---|---|
| `"int"` | 整数として書かれた数（`3`） |
| `"float"` | 浮動小数点として書かれた数（`3.0`） |
| `"string"` | 文字列 |
| `"array"` | 配列 |
| `"hash"` | ハッシュ |
| `"function"` | ラムダ `\(…){…}` |
| `"null"` | `null`（配列の穴埋め等） |
| `"cache"` | **計算結果のハンドル**（mesh・点群・ボリューム・B-rep・`export` の戻り `ref`） |

★★ **`type_of` と軸が直交する。** `type_of` は*幾何型*の軸で、非幾何はすべて `"value"` に潰れる。
`kind_of` は*値の種別*の軸で、幾何はすべて `"cache"` に潰れる。両方訊くと両軸が決まる:

```
kind_of(3)           "int"      type_of(3)           "value"
kind_of(3.0)         "float"    type_of(3.0)         "value"
kind_of(box(1,1,1))  "cache"    type_of(box(1,1,1))  "mf-mesh3d"
kind_of(points3d(…)) "cache"    type_of(points3d(…)) "pt-cloud3d"
```

★★ 幾何が `"mesh"` ではなく **`"cache"`** なのは、そのハンドルが持つのが*計算結果への参照*で
あって mesh とは限らないため（点群も B-rep もボリュームも `ref` も、種別としては同じハンドル）。
**何のキャッシュか**は `type_of` が答える。

- ★ **`"int"` / `"float"` は「どう書かれたか」**で、値の大きさではない。`3.0` は `"float"`。
  この区別は wire を越えて保たれる（`serialize()` が float に必ず小数点を付ける）ので、
  in-process でもプロセス経路でも同じ答えになる。
- ★ `agent` の op が**値**を返す場合は、その中身の種別になる（`kind_of(nverts(p))` → `"int"`、
  `kind_of(bbox(p))` → `"array"`）。`"cache"` になるのは幾何そのものを指しているとき。
- ⚠ **幾何は待たない**（`type_of` と同じ）。種別は型スタンプだけで決まるので、実値を待っても
  `"cache"` は `"cache"` のまま。値は評価する（種別を答えるには評価するしかない）。
  `x` がエラーならエラーがそのまま伝播する。
- ⚠ 該当が無ければ `"unknown"`。黙って `"value"` に寄せない（寄せると*種別が増えたこと*が
  見えなくなる）。
- 例: `if (kind_of(v) == "array") { … }`
- 関連: `type_of` · `length` · `int` · `float` · `which`

---

### `which(op[, intype…])` — その op を受けるモジュール
文 · → `文字列`

**実装**: 組み込み · 型 -

`op` を宣言しているモジュールを **priority 順に全部**、`"name:priority:sig"` の空白区切りで返す。

★ **op 名だけでは答えが 1 つに決まらない** — 同じ op 名でも**引数の型でディスパッチ先が変わる**。
`union` が実例で、`cg-mesh3d` が混じると `manifold` は候補から外れる（cgal は mf を食えるが
manifold は cg を食えない）。そこで「勝者 1 つ」ではなく**候補を全部**返し、`sig` も併記して
**なぜそれが選ばれたか**まで読めるようにしてある。

入力型を続けて渡すと、その型を**すべて**受理できる候補だけに絞る。`type_of` と組み合わせて
「型を見て → その型で誰が受けるかを見る」で追える。

**入力**
- `op` op 名 — `文字列`
- `intype…` 絞り込む入力型 — `文字列`（省略可・複数可）

**出力** — `文字列`（候補が無ければ空文字列）

- 例: `print(which("union", type_of(m)));`
- 関連: `type_of` · `modules` · `describe`

```
print("F1", module_loaded("cgal.so"));      // → F1 0
module("cgal.so", {});
print("F2", module_loaded("cgal.so"));      // → F2 1
```

- 関連: `module` · `module_reload`

---

### `module_reload(path, opts)` — 落として読み直す  〔stdlib: module/reload〕 {#module_reload}
文 · → `文字列`

**実装**: stdlib（`include "module/reload.sra";`）· 型 -

`module(path, "off")` してから `module(path, opts)` する定型を 1 行にまとめたヘルパ。
未ロードなら単にロードする。

1 つのモジュール名につき `dlopen` は 1 回なので、`module(so, {…})` では**ロード済みの `.so` 自体を
差し替えられない**（できるのは記述子の上書きだけ）。落としてから読み直すことで差し替えになる。

**入力**
- `path` モジュール — `文字列`（探索路の外の絶対パスも可）
- `opts` 上書き設定 — `ハッシュ`（`module()` と同じ）

**出力** — `文字列`（モジュール名）

```
include "module/reload.sra";
module_reload("cgal.so", {priority:99});
module_reload("/tmp/experimental/cgal.so", {});   // 開発中の .so に差し替える
```

⚠ **差し替えはそのモジュールで op を実行する前に行うこと**。一度でも使われた `.so` は落とせない。
⚠ `reload.sra` は `module/all.sra` には**入っていない**（ヘルパを使うためだけに 6 本ロードされるのを避けるため）。
⚠ 落とさずに同名で別パスを指すと「同名で別ファイル」の明示エラーになる。

- 関連: `module` · `module_loaded` · `pm_*`

---

### `pm_cgal(opts)` ほか — 粒度の既定値を練り込む擬似モジュール  〔stdlib: module/pseudo〕 {#pmod}
文 · → `配列`

**実装**: stdlib（`include "module/pseudo.sra";`）· 型 -

分割数 `seg` / ボクセルサイズ `dx` を**候補列の側で決めて**、呼び出しからは省けるようにする
工場。返り値は **`[擬似モジュール, 落ち先の実名]` の組**なので、そのまま
[候補列](srava_language_reference.html#cand-nested)に並べる。

| 工場 | 覆うモジュール | 取るキー |
|---|---|---|
| `pm_cgal` / `pm_manifold` | `cgal` / `manifold` | `seg` |
| `pm_geogram` / `pm_cherchi` / `pm_nef` | `geogram` / `cherchi` / `nef_hybrid` | `seg` |
| `pm_openvdb` | `openvdb` | `seg`, `dx` |
| `pm_points` ★ | — （点群・下記） | なし（`{}`） |

```
include "module/all.sra";
include "module/pseudo.sra";
use [ pm_cgal({seg:64}), pm_openvdb({seg:64, dx:0.01}) ];

sphere(1.5);           // → "cgal"::sphere(1.5, 64)
tube(path, 24);        // → "cgal"::tube_ruled(path, 24)   ← 名前の橋渡し
```

**入力**
- `opts` 練り込む粒度 — `ハッシュ`（省略不可。既定のままなら `{}`。**知らないキーはエラー**）

**出力** — `配列`（`[擬似モジュール, 実名]`）

- 覆うのは**粒度の引数を持つ生成 op** だけ: `sphere` / `cylinder` / `cone` / `torus` /
  `tube_ruled` / `tube`、2D を持つ `cgal` / `manifold` だけ `circle` / `revolve`。
  `openvdb` はこれに加えて `box` / `boxa` / `prism` / `pyramid` / `tetrahedron` /
  `icosphere` / `empty3d`（どれも `dx` が要る）。
- ⚠ **`pm_occt` は無い**（occt は解析曲面なので `seg` を取らない → 段3）。
- ⚠ **`dx` に既定は無い**（長さなのでモデルの寸法に依る）。`0.05` を置いてあるが明示すること。

**★★ `pm_points` だけは役割が違う — 引数の少ない形を足す** {#pmod-points}

ほかの工場が**既定値を練り込む**のに対し、これは
[`intersection(点群, 形)`](#ptsplit) の **2 引数形**を足して **3 要素配列**にする。

```
include "module/all.sra";
include "module/pseudo.sra";
use [ pm_points({}), "geomutils", "manifold", "points" ];

var p = points3d([[1,1,1],[0.5,0.5,0.5],[0,1,1],[5,5,5],[9,9,9]]);
var s = intersection(p, box(2,2,2));   // ★ p &&& box(2,2,2) でも同じ
print(nverts(s[0]), nverts(s[1]), nverts(s[2]));   // 1 2 2  ＝ 境界 / 内側 / 外側
```

- ★ 不変条件: `nverts(s[0]) + nverts(s[1]) + nverts(s[2]) == nverts(p)`（**分割**なので）
- ★ **`p &&& 形` でも効く**。`&&&` は `mk_meshop` に落ちて構文の糖衣を通らないが、
  擬似モジュールは**候補列**なので演算子経由のノードにも効く。
- ★ 3 引数形 [`intersection(p, m, mode)`](#ptsplit) と [`difference(p, m)`](#ptsplit) は
  **擬似を素通り**して実モジュールが答える（引数の数で外れるため）。
  ⇒ 内外の判定を持つのは `geomutils` / `openvdb` / `occt` で、**落ち先を決め打っていない**。
- ⚠ メッシュどうしの `intersection` / `&&&` は**奪わない**（第 1 引数が点群のときだけ受ける）。
- ⚠⚠ **なぜ構文の糖衣（`section` と同じ手）にしなかったか**: `intersection(a, b)` は
  **メッシュのブール積と同じ綴り**で、パーサは型を知らない（routing は評価時）ので
  判別できない。`section(m,P,N)` が糖衣で書けるのは、3 引数形を持つ op が**他に無い**から。
- 詳細・注意点は[言語リファレンス](srava_language_reference.html#pmod)。
- 関連: `module` · `tube_ruled` · `tube` · [`intersection`（点群）](#ptsplit)

---

### `triangulate(s, deflection)` — B-rep をメッシュへ
`3D` · → `mesh`

**実装**: `occt_mf.so`（occt とメッシュ系の**橋渡しモジュール**） · 型 `oc-brep3d`(BREP) → `mf-mesh3d`(MFM3)

解析曲面（B-rep）を三角形メッシュに落とす。`deflection` は**弦の最大距離**（世界座標の長さ）で、
小さいほど細かい。★ **高→低への降格**なので、この op を通した後は解析曲面の利点（分割数に依存しない
厳密な体積など）は無くなる。

**入力**
- `s` 対象 — `mesh`（`oc-brep3d`）
- `deflection` 弦の最大距離 — `スカラ`（**必須・0 より大**）

**出力** メッシュ — `mesh`（`mf-mesh3d`）

- ⚠ **`occt_mf.so` をロードすること**（`occt.so` 単体には入っていない）。occt 本体を manifold に
  依存させないために分けてある。
- 例: `module("occt.so",{}); module("occt_mf.so",{}); volume(triangulate(sphere(1), 0.01))`
- 関連: `cast`, `voxelize`, `isosurface`

---

### `voxelize(mesh, dx)` — メッシュをボリューム（level set）へ
`3D` · → `mesh`

**実装**: `openvdb_mf.so` / `openvdb_cg.so` / `openvdb_gg.so`（入力のメッシュ型ごとに 1 本）
· 型 `mf-` / `cg-` / `gg-mesh3d` → `vd-grid3d`(`VDB `)

三角形メッシュを **narrow-band level set**（符号付き距離場）へ変換する。

**入力**
- `mesh` 対象 — `mesh`（3D）
- `dx` **ボクセル間隔**（世界座標の長さ） — `スカラ`（**必須・0 より大**）

**出力** ボリューム — `mesh`（`vd-grid3d`）

- ⚠ 第 2 引数は**分割数ではなく間隔**。半分にすると格子は 8 倍になる。
- ⚠ **ボリューム系のブールは 2 つの grid の `dx` が同じであることを前提にする**。
  別の `dx` で作った grid を混ぜない。
- ★ **内部空洞は保たれる**（中空の殻は中空のまま）。入力が「閉じていて向きの揃った曲面」で
  ないときだけ、空洞を埋める従来の変換へ退避する（`stderr` に `WARN` を出す）。
- 例: `module("openvdb_mf.so",{}); var v = voxelize(sphere(8,64), 0.05);`
- 関連: `isosurface`, `renormalize`, `voxels`

---

### `isosurface(v[, iso])` — ボリュームからメッシュへ
`3D` · → `mesh`

**実装**: `openvdb_mf.so` / `openvdb_cg.so` / `openvdb_gg.so` · 型 `vd-grid3d`(`VDB `) → `mf-` / `cg-` / `gg-mesh3d`

等値面 `iso` を三角形メッシュとして取り出す（四角形で取り出してから分割する上流推奨の手順）。

**入力**
- `v` ボリューム — `mesh`（`vd-grid3d`）
- `iso` 等値 — `スカラ`（省略可）

**出力** メッシュ — `mesh`

- 既定: `iso=0`（level set の表面）
- ⚠ 帯（narrow band）の外の等値を指定すると**空になり明示エラー**。
- 例: `isosurface(v)` / `isosurface(v, 0.1)`（0.1 だけ太った面）
- 関連: `voxelize`, `renormalize`

---

### `renormalize(v[, halfWidth])` — level set を張り直す
`3D` · → `mesh`

**実装**: `openvdb.so` · 型 `vd-grid3d`(`VDB `)

符号付き距離場としての性質が崩れた grid（大きな `offset` の後など）を張り直す。
`halfWidth` は帯の半幅（**ボクセル単位**）。

- ★ **内部空洞は保たれる**（`voxelize` と同じ変換を通るため）。

**入力**
- `v` ボリューム — `mesh`（`vd-grid3d`）
- `halfWidth` 帯の半幅 — `スカラ`（省略可・0 以下は既定扱い）

**出力** 張り直した grid — `mesh`（`vd-grid3d`）

- 既定: `halfWidth=3`（OpenVDB の `LEVEL_SET_HALF_WIDTH`）。`offset` を帯より大きく取るときに広げる。
- 関連: `offset`, `voxelize`, `isosurface`

---

## 計測・検査・修復

「値返し op」は結果を式で観測できる（`if (valid(m)==1){…}`・`area(a)+area(b)`）。配列返しは添字可。

### `area(m)` — 面積
`2D・3D` · → `スカラ`

**実装**: `cgal.so` / `nef_snc.so` / `nef_hybrid.so` / `occt.so` / `openvdb.so` / `geomutils.so`（`mf-` / `gg-` / `ch-` の 3D と `gu-*`・**要ロード**） / `manifold.so`（**2D のみ**）（→ [対応表](#module-matrix)） · 型 value

2D＝囲み面積（外周−穴）/ 3D＝表面積。

- ⚠ `occt` は **B-rep のまま**積むので、球なら `4πr²` がそのまま出る。メッシュ系は内接多面体
  なので構造的に小さい値になる（`volume` と同じ事情）。`openvdb` は解像度依存の近似値。
- ★ **内部空洞の面も数える**（中空の殻は外側＋内側の面積）。

**入力** `m` 対象 — `mesh`

**出力** 面積 — `スカラ`

- 例: `area(rect(4,5))` → `20`

### `volume(m)` — 体積
`3D` · → `スカラ`

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so` / `openvdb.so`（→ [対応表](#module-matrix)） · 型 value

囲む体積（閉メッシュ・発散定理）。2D はエラー。

- ⚠ `occt` は **B-rep のまま**積むので球なら `4/3πr³` がそのまま出る。メッシュ系は内接多面体
  なので構造的に小さい値になる。`openvdb` は**メッシュを作らずに格子から**出す
  （符号つきボクセル積分）ので解像度依存の近似値。
- ★ **内部空洞は差し引かれる**（中空の殻は殻の体積になる）。`area` も同様に内側の面を数える。

**入力** `m` 対象 — `mesh`（3D）

**出力** 体積 — `スカラ`

### `perimeter(m)` — 周長
`2D` · → `スカラ`

**実装**: `cgal.so` · 型 value

境界長（外周＋穴の周長）。3D はエラー。

**入力** `m` 対象 — `mesh`（2D）

**出力** 周長 — `スカラ`

### `centroid(m)` — 重心
`2D・3D` · → `ベクトル`

**実装**: `cgal.so` / `nef_snc.so` / `nef_hybrid.so` / `occt.so` / `openvdb.so` / `geomutils.so`（`mf-` / `gg-` / `ch-` の 3D と `gu-*`・**要ロード**） / `manifold.so`（**2D のみ**）／ `points.so`（点群）（→ [対応表](#module-matrix)） · 型 value

面積/体積重心。

**入力** `m` 対象 — `mesh`

**出力** 重心 — `2D/3D ベクトル`（2D＝`[x,y]` / 3D＝`[x,y,z]`）

- 例: `centroid(m)[0]`（x 成分）
- ★★ **2D は型で答えの形が変わる**（`bbox` と同じ・[2D 領域の 2 つの型](srava_language_reference.html#two-2d-types)）:
  `*-cross2d` は**局所の 2 成分**・`*-face3d` と `oc-face3d` は **world の 3 成分**。
  ```
  centroid(rect(2,3))                   // [1,1.5]
  centroid(rotate(rect(2,3),"x",180))   // [1,-1.5,0]
  ```
  ★ world で答えるのは、`rotate(U,"x",180)` と `rotate(rotate(U,"x",90),"x",90)`
  （world では同じ図形）が違う値を返さないようにするため。

### `bbox(m)` — バウンディングボックス
`2D・3D` · → `配列`

**実装**: `cgal.so` / `nef_snc.so` / `nef_hybrid.so` / `occt.so` / `openvdb.so` / `geomutils.so`（`mf-` / `gg-` / `ch-` の 3D と `gu-*`・**要ロード**） / `manifold.so`（**2D のみ**）／ `points.so`（点群）（→ [対応表](#module-matrix)） · 型 value

軸平行 AABB を `[min隅, max隅]` で返す。

**入力** `m` 対象 — `mesh`

**出力** 範囲 — `点列`（`[min, max]` の 2 要素・各隅は `2D/3D ベクトル`）

- 例: `var bb = bbox(m); var sz = bb[1] - bb[0];`（サイズ）
- ★★ **2D は型で答えの形が変わる**（[2D 領域の 2 つの型](srava_language_reference.html#two-2d-types)）:
  `*-cross2d`（z=0 の簡易表現）は**局所の 2 成分**・`*-face3d`（空間に置かれた一般表現）と
  `oc-face3d` は **world の 3 成分**。
  ```
  bbox(rect(2,3))                  // [[0,0],[2,3]]
  bbox(rotate(rect(2,3),"x",90))   // [[0,0,0],[2,0,3]]
  ```
  ★ world で答えるのは、**同じ図形に 2 つの答えが出ないようにする**ため。局所座標で答えていた
  頃は `rotate(U,"x",180)` と `rotate(rotate(U,"x",90),"x",90)`（world では同じ図形）が
  違う値を返していた。
- ⚠⚠ **occt で `surface_type` が `bezier` / `bspline` の面は、箱が真の箱より広い**。
  OCCT の `BRepBndLib::Add` はその 2 種類では**制御点 (poles) の箱**を返し、面は制御網の凸包に
  収まる（凸包性質）ので、箱は必ず面を含むが、たいてい真に大きい。

  | 面 | `bbox` の z 上端 | 真値 |
  |---|---|---|
  | 中央だけ持ち上げた 3×3 Bezier（`surface_control`） | 1.0 | **0.25** |
  | 同じ格子を通した B-spline（`surface_through`） | 1.778 | **1.0** |

  ★ 影響するのは `surface_control` / `surface_through` が作る面と、NURBS を含む STEP の
  取り込みだけ。`plane` / `sphere` / `cone` / `cylinder` / `torus` は**正確**（`loft` の側面も
  多くは `cone` なので正確）。`surface_type` で先に判別できる。
  ⚠ OCCT の `AddOptimal`（「precise」と謳う口）は **使えない** — B-spline 面で箱を**過小**に返し、
  **箱が面を含まなくなる**。許容差をどれだけ締めても誤った極値へ収束するため、
  過大（安全側）のまま据え置いている。詳細は 。
- 注: 空集合はエラー → `valid` でガード
- 関連: `centroid`, layout 各関数

### `valid(m)` — 検証
`2D・3D` · → `整数`(0/1)

**実装**: `cgal.so` / `nef_snc.so` / `nef_hybrid.so` / `occt.so` / `openvdb.so` / `points.so`（点群）／ `geomutils.so`（`mf-` / `gg-` / `ch-` / `gu-*`・**2D も含めて**・要ロード）（→ [対応表](#module-matrix)） · 型 value

`1`=正常 / `0`=問題。**3D の定義は全カーネル共通で ① 空でない ∧ ② 閉じている（境界辺の無い
2-多様体）∧ ③ 自己交差が無い**（2D＝全リング単純）。

- ★ 答え方はカーネルごとに違ってよい（定義が同じであれば）。`cgal` / `nef` は CGAL の厳密述語、
  `occt` は `BRepAlgoAPI_Check` + 稜の使われ回数、`geogram` / `cherchi` / `manifold` は共通実装
  （`src/h/common/meshprops.h`）で答える。
  ⚠ `occt` で `BRepAlgoAPI_Check` だけでは足りないのは、それが **立体を 1 つずつ**しか見ないため。
  *別々の立体が稜だけで接している*形（2 球の対称差＝交線の円で接する三日月 2 つ、稜だけで接する
  2 箱）を妥当と答えるので、② の「2-多様体」の側は稜が 2 回ちょうど使われることを別に数える
  （2026-09-15）。
- ⚠ `openvdb` だけ **②③ が構造的に恒真**（距離場は境界も自己交差も表現できない）ので、実質
  ①（空でない）だけを見る。自己交差した掃引を voxelize すると `1` になる。
- ★ ②③ は **座標が完全一致する頂点を同じものと見なして**から数える。同じ位置の頂点が別々の
  番号で入っているメッシュ（`manifold` は色の境目で頂点を分裂させる。ブールの結果も継ぎ目を
  分裂させることがある）で、生の番号のまま数えると継ぎ目が見えないため。
  ⚠ 溶接するのは **完全一致だけ**（許容誤差で近い頂点を寄せると本物の交差を見逃す）。
- ★ `valid` は **回転させても変わらない**。2026-09-15まで、`revolve` で作った立体を
  回すと `manifold` が `0` を返していた（同一平面の非隣接な三角形対を誤検出していた）。
- ⚠ 非多様体になるのは **稜で接したとき**であって、**頂点だけで接しても `1`**（② は稜で数える）。
  単位立方体 2 つを `[1,1,0]` ずらすと `0`（稜 1 本を 4 面が使う）、`[1,1,1]` ずらすと `1`。
- ⚠⚠ `valid` を「ブールが正しい形を返したか」の**代理に使ってはいけない**。接する立体の対称差は
  *正しく出来ても* 非多様体なので `0`。形が合っているかは `volume` で見る。

**入力** `m` 対象 — `mesh`

**出力** 正常か — `整数`（`1`/`0`）

- 例: `if (valid(m) == 1) { export("ok.stl", m); }`
- ★ `0` が返っても**下流の op はエラーにならない**。自己交差した閉メッシュは各カーネルの
  ブール演算を素通りして**黙って誤った体積**を返す（自分を貫く tube では交差部が二重に数えられる）。
  面が囲む本当のソリッドが欲しいときは `solidify` を明示的に呼ぶ
- 関連: `repair`, `solidify`

### `repair(m)` — 修復
`2D・3D` · → `mesh`

**実装**: `cgal.so` · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2)

2D＝`Polygon_repair`（even-odd 正規化・**完全**）/ 3D＝`autorefine`（**交差線を実エッジ化するだけ**）。

**入力** `m` 対象 — `mesh`

**出力** 修復後 — `mesh`

- ★**3D の `repair` は自己交差を「直さない」**。`valid()` は自己交差を見るので、自己交差した形状は
  `repair` の後も **`valid()` = 0 のまま**（軽い/深い自己交差 tube・重なる 2 立体の `+++`、
  いずれも 0）。細分は効いている（自己交差 tube で 86v/168f → 726v/792f）が、交わっていた 2 枚の面は
  エッジで接したまま残るため
- 3D で `repair` が役に立つのは「交点を実エッジにした mesh が欲しい」場合。**汚れた mesh を有効な
  ソリッドにする用途には使えない**。それをやるのは `solidify`
  （自己交差した tube の体積は重なりの二重計上になっている。`solidify` で組み直せば直る）
- ★**`solidify` の前段に `repair` を置いてはいけない**。`autorefine` は内部で
  `duplicate_non_manifold_edges_in_polygon_soup` を通すので出力が**閉じていない**（交差線が穴の縁に
  変わる）。閉じていないメッシュは Nef にできないので、そこで明示エラーになる
- ★注意: 「**分割して `union` すれば正しい値になる**」は**誤り**（旧記述を訂正）。分割すると継ぎ目が
  マイター接合から**平らな蓋どうしの重ね合わせ**に変わり、角が太る。自己交差の無い tube でも
  一本物 52.23 に対し、同じ点列を 2 本に割って `union` すると 54.48（差 2.25 は継ぎ目の分で、
  自己交差とは無関係）。自己交差 tube でも 2 本に割った `union` は 50.51 で、面が囲む本当の体積
  48.61 より太い
- 2D は even-odd 正規化なので**完全に修復できる**（3D と非対称なことに注意）
- 関連: `valid`, `polygon`, `unify`

---

### `voxels(v)` — 有効ボクセル数
`3D` · → `整数`

**実装**: `openvdb.so` · 型 `vd-grid3d`(`VDB `)

grid の**有効ボクセル数**（narrow band に値を持つボクセルの数）。ボリュームの「重さ」の目安で、
`dx` を半分にすると面積比でおよそ 4 倍になる。

**入力** `v` ボリューム — `mesh`（`vd-grid3d`）

**出力** 個数 — `整数`

- ⚠ **体積ではない**。体積は `volume(v)`。
- 例: `voxels(voxelize(sphere(8,64), 0.05))`
- 関連: `volume`, `voxelize`

---

## 近接（2 メッシュ間・3D 専用）

いずれも 3D-3D 専用。2D / 次元混在はエラー。

### `distance(a, b)` — 最近接距離
`3D` · → `スカラ`

**実装**: `cgal.so` · 型 value

2 メッシュ間の最近接距離（AABB 近似・頂点↔面の双方向最小）。

**入力** `a, b` 対象 — ともに `mesh`（3D）

**出力** 最近接距離 — `スカラ`

- 注: 辺-辺の谷を取りこぼし得る（密メッシュで真値に収束）
- 関連: `closest`

### `distance_at(m, [x,y,z])` — 点と境界の距離
`2D・3D` · → `値`

**実装**: `cgal.so`（3D）／ `geogram.so`（3D・自前の AABB）／ `occt.so`（3D・2D）／ `openvdb.so`（3D）／ `geomutils.so`（`mf-` / `ch-` / `gu-*`・**2D も**・要ロード）（→ [対応表](#module-matrix)）

点 `[x,y,z]` から `m` の**境界までの最短距離**。**符号なし**（内側の点でも正）。
★ 2D 領域も受ける— そのときは点も `[x,y]`（`*-face3d` は `[x,y,z]`）で、測る相手は**輪郭**になる。

- ★ 閉形式で検定できる: 半径 `r` の球の中心から距離 `d` の点は **`|d - r|`**。
- ⚠ `distance(a,b)` は**立体どうし**の距離で、こちらは**点との**距離。問うているものが違うので別の名前
  （位置で指す `_at` は `face_at` と同じ流儀）。
- ★★ **同じ op でもカーネルによって測る相手が構造的に違う**:

| カーネル | 測る相手 | 半径 2 の球の中心から |
|---|---|---|
| `occt` | **解析曲面のまま**（`BRepExtrema_DistShapeShape`） | **2**（厳密） |
| `cgal` / `geogram` | 三角形メッシュ = **内接多面体** | 2 より**わずかに小さい**（分割数で寄る） |
| `openvdb` | 距離場を**読むだけ**（探索なし） | 帯の外なのでエラー |

  体積・面積と同じ関係で、これは誤差ではなく**表現の違い**。閉形式と突き合わせるなら箱を使う
  （面が平面なのでどの表現でも同じ立体になる）。
- ⚠⚠ `openvdb` は**狭帯域（既定 3 ボクセル）の外では答えない**。場の値が `background` に飽和していて
  「遠い」ことしか分からないため、飽和値を距離として返さず**明示エラー**にする。
- 例:
```
var B = box(2,2,2);
print("D", distance_at(B,[3,3,3]));   // → D 1.7320508075688772  (角の外 = 頂点まで sqrt(3))
print("D", distance_at(B,[1,1,1]));   // → D 1                   (内側 = 面まで 1)
```
- 関連: `distance`, `closest`, `farthest`, `thin_spots`

### `closest(a, b)` — 最近接点対
`3D` · → `配列`

**実装**: `cgal.so` · 型 value

**入力** `a, b` 対象 — ともに `mesh`（3D）

**出力** — `配列` `[距離(スカラ), a上の点(3Dベクトル), b上の点(3Dベクトル)]`

- 例: `closest(a,b)[1]`（a 上の点）
- 関連: `distance`, `farthest`

### `farthest(a, b)` — 最遠点対
`3D` · → `配列`

**実装**: `cgal.so` · 型 value

頂点ペア総当り（距離値は厳密・大メッシュで重い O(|VA|·|VB|)）。

**入力** `a, b` 対象 — ともに `mesh`（3D）

**出力** — `配列` `[距離(スカラ), a上の点(3Dベクトル), b上の点(3Dベクトル)]`

- 関連: `closest`

### `thin_spots(m, t_min [, rays [, cone]])` — 肉厚解析（薄肉検出）
`3D` · → `配列`

**実装**: `cgal.so` · 型 value

**肉厚 SDF（Shape Diameter Function）**で「薄すぎて 3D プリントで割れる箇所」を位置つきで拾う。各面で内向きに錐状のレイ（全角 `cone`°・`rays` 本）を飛ばし、反対側の壁までの距離の加重平均＝その場所の**肉厚**を測り、`t_min` 未満の面だけを返す。値は絶対距離（モデル単位＝mm 等）。入力は閉じた三角形メッシュ前提（`valid` で前段確認可）。

🔑 **`cone`（コーン全角）が肝**。既定 **45°** は「壁にほぼ垂直方向の肉厚」を測るので、ダクトや壁の**角（複数の薄壁が収束する所）でも過小評価しない**。CGAL 既定の **120°** は広角で形状診断向きだが、角で周囲の壁を拾って 1.5mm 壁を 0.6mm 等と**過小評価**する（＝偽陽性）。角の誤検出が気になるなら 30〜45°、なだらかな曲面の薄肉も拾いたいなら広めに。

⚙️ **マルチスレッド並列**（面ごと独立・AABB ツリー共有）。計算量 ≈ 面数 × `rays`。`rays` を下げると速いが取りこぼす。

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

### `pipe_proximity` ／ `pipe_adjust` ／ `pipe_sample` ／ `pipe_scene_proximity` ／ `pipe_scene_adjust` {#pipe-proximity}
`3D` · → `配列` / `ハッシュ`

**実装**: `pipe_proximity.so`（**解析モジュール**・幾何型を持たず値だけをやり取りする） · 型 value

可変太さ配管（tube）の**自己接近の検出**と、接近を解消するための**制御点の調整**。
幾何カーネルに依存しない（CGAL も Manifold も使わない）ので、mesh ではなく
**制御点の配列と半径**を受け取り、値（配列・ハッシュ）を返す。

| op | 役割 |
|---|---|
| `pipe_proximity(ctrl_pts, radius, report_gap)` | 1 本の配管の自己接近を検出して当たりの一覧を返す |
| `pipe_adjust(ctrl_pts, radius, params)` | 接近を解消するように制御点を動かす（`.ctrl` をそのまま `tube` に渡せる） |
| `pipe_sample(...)` | 中心線のサンプリング（半径キーポイントを保つ） |
| `pipe_scene_proximity(bodies, report_gap)` | **N 体**の相互接近を検出 |
| `pipe_scene_adjust(bodies, movableIdx, params)` | **N 体**のうち可動 body の制御点を動かす |

- ★ **引数と `params` の全キー（`dMin` / `maxIter` / `fixEnds` / `solver` …）と返り値の構造は
  [モジュールリファレンスの pipe_proximity の節](srava_module_reference.html#pipe_proximity)**に
  一覧がある（ここに二重には書かない）。
- 例: `var res = pipe_adjust(pts, 0.8, { dMin: 0.6, maxIter: 400, fixEnds: 1 }); tube_ruled(res.ctrl, 12);`
- 関連: `tube`, `distance`, `thin_spots`

---

## 点群 {#pointcloud}

**順序を持たない点の集まり**を値として扱う。実装は `points.so` — **外部ライブラリを
持たないカーネル中立のモジュール**で、法線推定だけ `cgal.so` が持つ。

| 型名 | 4CC | 中身 |
|---|---|---|
| `pt-cloud2d` | `PTC2` | 2D 座標の平坦配列 + 省略可能な法線 |
| `pt-cloud3d` | `PTC3` | 3D 座標の平坦配列 + 省略可能な法線 |

★ **なぜカーネル中立なのか**: 点群を受け取る外部ライブラリは CGAL / geogram / OCCT / OpenVDB /
manifold の 5 つあり、**そのどれもが「平坦な double 配列」で受け取る**（geogram は
`Delaunay::set_vertices(nb, const double*)` にポインタをそのまま渡せる）。メッシュのように
「保存すべきカーネル固有表現」が無いので、型をどれか 1 つのカーネルに置く理由が無い。

★★ **印は 2 つ** — 「法線があるか」と「**向き付けされているか**」は別の事実。
RANSAC（形状認識）は向きなしで足り、Poisson（曲面復元）は向きありが必須。どちらも
キャッシュに載るので cold と warm で答えが変わらない。

⚠⚠ **既定の法線は作らない**。全点を `(0,0,1)` のような既定値で埋めると、Poisson も RANSAC も
**エラーにならずに走り、静かに嘘の形を返す**。法線が要るなら `estimate_normals` を明示的に呼ぶ。

### `points2d(pts)` ／ `points3d(pts)` — 値配列から点群
`2D`/`3D` · → `点群`

**実装**: `points.so` · 型 `pt-cloud2d`(PTC2) / `pt-cloud3d`(PTC3)

```
points3d([[x,y,z], ...])                  法線なし — 各要素が **点そのもの**（`polygon` と同じ形）
points3d([[[x,y,z],[nx,ny,nz]], ...])     法線あり — 各要素が **[点, 法線]**（`tube` と同じ形）
```

- 判定は「**最初の子要素が数かリストか**」だけ（長さを数えない）。⚠ **混在は明示エラー**。
- ⚠ 長さ 0 の法線は明示エラー（黙って通すと上の「静かに嘘の形」になる）。
- ★ **明示的に与えた法線は「向き付けされている」とみなす**（与えた人が向きに意味を持たせている、と読む）。
- ★★ **2D と 3D で名前が分かれている理由**: op は実行時に出力型を変えられない。`points` は leaf で
  引数が inline 値 = 型シグネチャの照合に参加しないので、planner には 2D か 3D かを判別する材料が
  **無い**。⚠ 1 つの op に 2 行（`->pt-cloud2d;->pt-cloud3d`）と書いても**先頭が黙って勝つ**。
  ⇒ `rect`/`box`・`empty2d`/`empty3d` と同じく名前で分ける。
- ⚠ **inline 値配列の道は O(N^1.9)**（`polygon` と同じ制約）。大きな点群は `import` から入れる
  — ファイル経由は線形で、値配列の道より桁で速い。
- 例: `points3d([[0,0,0],[1,0,0],[0,1,0]])` / 関連: `import`, `estimate_normals`

### `rand(a, b, n, seed)` — 擬似乱数（値の配列 / 点群） {#rand}
`値・2D・3D` · → `値の配列` / `点群`

**実装**: `points.so` · 型 - / `pt-cloud2d`(PTC2) / `pt-cloud3d`(PTC3)

```
rand(a, b, n, seed)   a, b = スカラ        → 値の配列（n 個）
rand(a, b, n, seed)   a, b = 2 要素の配列  → pt-cloud2d   （軸ごとの区間）
rand(a, b, n, seed)   a, b = 3 要素の配列  → pt-cloud3d   （軸ごとの区間）
```

★★ **名前は 1 つ**で、**第 1 引数の形**（要素数 0 / 2 / 3）が軸の数を決める。
旧 `rand_pt2d` / `rand_pt3d` は**無くなった**（別名は置いていない）。

★★★ **シードは省略できない**。この op 群の約束は **「同じ引数なら必ず同じ結果」** で、シードは
その引数のひとつだから。省略可能にすると *引数から結果が決まらない* op になり、srava が結果を
キャッシュする前提そのものが崩れる（キャッシュの鍵は引数のハッシュ ⇒ 2 回目は必ず HIT する
⇒ 「毎回違う値」は最初から実現しない）。⇒ 3 引数で呼ぶと `expected 4 argument(s), got 3`。

⚠⚠ 「同じ」の範囲は **機械と OS をまたぐ**（キャッシュは持ち運べるので、Linux で作った値を
macOS が同じ鍵で HIT させる）。⇒ 生成器も区間への写像も **自前**で持つ。`std::uniform_int_distribution`
は規格が写像を決めていないので処理系ごとに違う値を返し、ここでは使えない。

★ **整数か浮動小数点かは、書かれた `a` と `b` の種別が決める**。規則は 1 つで、配列で呼んだ
ときはそれを **軸ごとに**適用する:

| 書いた区間 | 出る値 |
|---|---|
| `rand(0, 10, n, s)` | 整数 `[0,10]` **閉**（`0` も `10` も出る） |
| `rand(0.0, 10, n, s)` | 浮動小数点 `[0,10)` **半開**（右端は出ない） |
| `rand([0,0,0], [4,4,4], n, s)` | 整数格子上の点 |
| `rand([0,0,0.0], [4,4,4], n, s)` | x, y は格子・**z だけ連続** |

⚠ 離散と連続で端の扱いが違うのは意図的。離散の `[a,b]` は候補が `b-a+1` 個という数え方そのもので、
連続の `[a,b)` は区間を割って使うときに端が重ならないという慣例。

**入力** `a`, `b` 区間 — `数`（1 軸）/ `2`・`3` 要素の配列（軸ごと）
／ `n` 個数 — `整数`（`0` 以上）／ `seed` 種 — `整数`

**出力** `a` がスカラなら値の配列 — `配列` ／ `a` が配列なら `点群`

- `n` と `seed` は **整数だけ**。浮動小数点を黙って切り捨てると `1.4` と `1.6` が同じシードになり、
  *違う引数で同じ結果* が静かに起きる ⇒ 明示エラー。
- `a > b` は明示エラー（空の区間）。`n = 0` は空の配列 / 空の点群（`points3d([])` と同じ扱い）。
- ⚠ スカラで呼ぶと **値**を返すので、結果は木の上でテキスト化されて運ばれる。大きな点群が
  欲しいなら**配列で呼ぶ**（点群型は cache を通るので値配列の O(N^1.9) を踏まない）。
- ⚠⚠ 点群を返す形は **法線を付けない**（無い法線を埋めると Poisson も RANSAC もエラーに
  ならずに走って静かに嘘の形を返す）。法線が要るなら `estimate_normals` を明示的に呼ぶ。
- ★★ **`a` と `b` の軸数が食い違えば明示エラー**（`a and b must each be an array of 3 numbers`）。
  行を決めるのは `a` だけで、`b` は行が決まった後に照合される — 両方で行を選ぶと食い違いが
  「どの候補も受けない」= *どこが悪いか言わない* 文言に化けるため。
- ★ 要素数 **1 や 4** の配列は `a must be a number (one axis) or an array of 2 or 3 numbers …
  but it is an array of 4` と**形の話として**断る。
- ⚠⚠ 承知の上の穴: `a` と `b` を **揃えて** 2 要素で書けば、3 軸のつもりでも 2D の点群になる。
  **名前を 1 つにした代償** — 旧 `rand_pt3d` は軸数を *op 名と配列の長さで 2 回* 言っていたので
  食い違いが見えた。宣言が 1 回になると「3 のつもりだった」がどこにも書かれていないので、
  原理的に検出できない（片方だけ間違えたときは上のエラーで止まる）。
  ★ ただし **型は落ちずに運ばれる**ので、`volume` / `export(".xyz")` / `estimate_normals` は
  型名を挙げて断る。素通りするのは `hull` / `delaunay` / `vert` / `bbox` のように **2D にも
  正当な意味がある op** だけ ⇒ [モジュールリファレンス](srava_module_reference.html#points)に表。
- 例: `rand(1, 6, 10, 42)` / `var p = rand([0.0,0.0,0.0],[1.0,1.0,1.0], 1000, 7); hull(p)`
- 関連: `points2d`, `points3d`, `hull`, `delaunay`, `voronoi`

---

### `rand_gaussian(center, sigma, n, seed)` — 正規分布の擬似乱数（値の配列 / 点群） {#rand-gaussian}
`値・2D・3D` · → `値の配列` / `点群`

**実装**: `points.so` · 型 - / `pt-cloud2d`(PTC2) / `pt-cloud3d`(PTC3)

```
rand_gaussian(center,   sigma, n, seed)   center = スカラ        → 値の配列（n 個）
rand_gaussian(center2d, sigma, n, seed)   center = 2 要素の配列  → pt-cloud2d
rand_gaussian(center3d, sigma, n, seed)   center = 3 要素の配列  → pt-cloud3d
rand_gaussian(点群,     sigma, n, seed)   center = **点群**      → 同じ次元の点群
```

★★ **名前は 1 つ**で、**第 1 引数の形**（要素数 0 / 2 / 3）が軸の数を決める。
仕掛けは [`rand`](#rand) と同じで、内部の行は `rand_gaussian` / `rand_gaussian#pt2d` /
`rand_gaussian#pt3d`。

★★★ **`sigma` は「各軸の標準偏差」**。各軸が独立に `N(center[k], sigma)` に従う
＝ 2D なら円形・3D なら球形の対称性を持つ**等方ガウス**。

> ⚠⚠ **「中心からの距離の標準偏差」ではありません。** 2D の等方ガウスでは距離は Rayleigh 分布に
> 従い、その標準偏差は `sigma` に**なりません**（平均 ≈1.253σ・標準偏差 ≈0.655σ）。
> ⚠ **1 次元では両者が一致する**ので、スカラ版だけ見ていると差が出ません。

★★★ **シードは省略できません**（[`rand`](#rand) と同じ設計）。3 引数で呼ぶと
`op 'rand_gaussian' — no candidate takes 3 argument(s) (points: takes 4)`。

⚠⚠ 「同じ引数なら同じ結果」の範囲は **機械と OS をまたぎます**。正規乱数はどの作り方でも
**対数**が要りますが、`std::log` は**規格が正しい丸めを要求していない**ので処理系で最後の
1 bit が違いえます。⇒ **対数も自前**（IEEE 754 が丸めを規定している演算と `frexp` だけ）。
`sqrt` は IEEE が正しい丸めを要求しているのでそのまま使っています。

**入力**
- `center` 中心 — `数`（1 軸）/ `2`・`3` 要素の配列（軸ごと）
- `sigma` **各軸の**標準偏差 — `スカラ`（`0` 以上。負は**エラー**）
- `n` 個数 — `整数`（`0` 以上）／ `seed` 種 — `整数`

**出力** `center` がスカラなら値の配列 — `配列` ／ `center` が配列なら `点群`

- ⚠ 返る値は **常に浮動小数点**。[`rand`](#rand) の「`a` と `b` が両方整数なら整数」という規則は
  ここには**ありません**（正規分布は連続分布なので整数へ丸める意味が無い）。
- `sigma = 0` は **全点が中心**（退化ですが σ→0 の極限と一致するので通します）。`n = 0` は空。
- ★ 要素数 **1 や 4** の配列は `the center must be a number (one axis) or an array of 2 or 3
  numbers (one per axis), but it is an array of 4` と**形の話として**断ります。
- ⚠⚠ 点群を返す形は **法線を付けません**（[`rand`](#rand) と同じ理由）。
- ⚠ スカラで呼ぶと**値**を返すので木の上でテキスト化されて運ばれます。大きな点群が欲しいなら
  **中心を配列で書く**（点群型は cache を通るので値配列の O(N^1.9) を踏まない）。
- 例: `rand_gaussian(0, 1, 10, 42)` /
  `var p = rand_gaussian([0,0,0], 2.0, 5000, 7); hull(p)`

#### 第 1 引数が**点群**のとき — 重ね合わせた分布から引く {#rand-gaussian-mix}

点群を渡すと、**その各点を中心とする等方ガウスを均等に重ね合わせた分布**から `n` 点を引く。

```
var c = points3d([[0,0,0],[100,0,0]]);
var p = rand_gaussian(c, 1.0, 2000, 11);   // → pt-cloud3d（2000 点）
                                            //   2 つの山にほぼ半々で分かれる
```

> ★★★ **「各入力点に中心版を施したもの」ではありません。** 重ね合わせた分布からの標本なので、
> 実装としては「`n` 回：中心を**一様に 1 つ選び**、そこから `N(0, sigma)` を 1 点」と等価です。

- ★ 出力の点数は **`n`**。入力の点数 `K` とは**無関係**（「各点に `n` 点ずつ」でも「`n/K` 点ずつ」でもない）。
- ⚠ `K` が大きく `n` が小さいと **点を 1 つも貰わない中心**が出ます（混合分布として正しい）。
- ⚠ **入力点群の順序が結果に効きます**（順序は「格納順＝入力の順」と定義済み → `vert`）。
- ⚠ 入力が**空**（`K=0`）は明示エラー — 中心が無いと分布が定義できないため。
- ⚠ `K=1` でも**中心版とは列が違います**（中心のくじを必ず 1 回引くため）。**分布は同じ**です。
  ⇒ 消費順の規約を `K` で条件分けしないため（一様版 `rand` も退化した区間で同じように消費します）。
- ⚠ 入力の**法線は引き継ぎません**（新しい点なので）。
- ★ この形だけ **`sig` が行を選びます**（第 1 引数が幾何なので）。他の 3 形は第 1 引数の
  *値の形* で選ばれる ⇒ **同じ op 名で選ばれ方が 2 通り**あります。

- 関連: `rand`, `points2d`, `points3d`, `hull`, `delaunay`, `voronoi`

### `estimate_normals(p [, k])` — 法線を推定する
`3D` · → `点群`

**実装**: `cgal.so` / `geogram.so` · 型 `pt-cloud3d`(PTC3)

`k` 近傍の接平面から法線を推定し、**向きを揃える**。`k` は省略可（既定 18・両実装で同じ）。

★★ **1 つの op 名に実装が 2 つ**ある（`nverts` などと同じで、srava では普通の形）:

| 名指し | 中身 |
|---|---|
| `"cgal"::estimate_normals(p)` | `pca_estimate_normals`（向きなし）+ `mst_orient_normals`（向き付け） |
| `"geogram"::estimate_normals(p)` | `Co3Ne_compute_normals(M, k, reorient=true)` |

- 両方ロードしていれば **priority で `cgal`（20 > 6）** が受ける。`"geogram"::` で名指しできる。
- ⇒ **cgal（GPL）を入れない構成でも法線推定ができる**。
- ★ キャッシュキーのソルトに**モジュール名 + cache_version** が入るので、2 実装の結果は混ざらない。
- ⚠ **印の根拠の強さが違う**: CGAL の `mst_orient_normals` は向き付けできなかった点を**返す**ので
  「全点を向き付けられた」ことを確かめてから印を立てられる。geogram の `reorient_normals` は
  取り消し以外では常に成功を返し、連結成分ごとに向きを伝播するだけで**成分間の相対符号を報告しない**。
  ⇒ kNN グラフが複数成分に割れる点群（離れた塊が複数ある／点が疎すぎる）では、
  geogram 版は**成分ごとの符号が揃わないまま印が立ちうる**。

- ★ 独立した op なのは、推定が重く、**利用者が見られる / 差し替えられる**値であるべきだから
  （`convex_decomposition` → `part` と同じ構造）。
- ⚠ 向き付けに失敗した点が残った場合、**点は捨てずに**「向き付けあり」の印だけ下ろす。
  法線自体は向きなしとして有効なので RANSAC には使える。
  （CGAL の作法は向き付かなかった点を erase するが、それは**黙って点が減る**）。
- ⚠ 点の**並びは変わりうる**（点群に並びの約束は無い）。
- ⚠ カーネルは **EPICK (double)**。点集合処理は厳密数と相性が悪く、点群の座標は測った値なので
  EPECK へ上げる意味が無い。
- ⚠ 3 点未満はエラー（接平面が決まらない）。2D もエラー。
- 例: `estimate_normals(import("scan.xyz"))` / `estimate_normals(p, 30)`

### `delaunay(p)` — Delaunay 三角形分割／四面体分割 {#delaunay}
`2D・3D` · → `mesh`

**実装**: `cgal.so`（→ [対応表](#module-matrix)）· 型 `pt-cloud2d`→`cg-cross2d` / `pt-cloud3d`→`cg-mesh3d`

点群 `p` の Delaunay 分割。**2D は三角形・3D は四面体**を、1 つの値の中に**片として並べて**返す
（`nparts` で数え、`part(d, i)` で取り出す）。計算は **EPECK で厳密**。

```
var p = points2d([[0,0],[4,0],[0,4],[4,4],[2,2]]);
nparts(delaunay(p))                 // 4  — 三角形の枚数
area(delaunay(p))                   // 16 — 凸包を覆う（隙間も重なりも無い）
```

- ⚠⚠ **三角形／四面体の番号は実装依存**（`voronoi` のセル番号が定義で決まるのと対照的）。
  しかも番号は**キャッシュに焼き付く**ので、上流の版が変われば同じ式が別の片を返しても
  値としては正常に見える。⇒ **数える・全部回す**のは安全、**`i` 番目を名指す**のは危うい。
- ⚠ 入力は**点群型だけ**（値の配列は受けない）。値から来るなら `points2d(...)` / `points3d(...)` を通す。
- ⚠ 3D は四面体が 0 個になることがある（全点が同一平面など）。これは**失敗ではない**。

**入力** `p` 点群 — `pt-cloud2d` / `pt-cloud3d`

### `voronoi(p, box)` — Voronoi 図（箱で切る） {#voronoi}
`2D・3D` · → `mesh`

**実装**: `cgal.so`（→ [対応表](#module-matrix)）· 型 `pt-cloud2d`→`cg-cross2d` / `pt-cloud3d`→`cg-mesh3d`

点群 `p` の Voronoi 図を `box` で切って返す。セルは**片として並ぶ**ので `part(v, i)` で取り出せる。

```
var p = points2d([[0,0],[4,0],[0,4],[4,4],[2,2]]);
var v = voronoi(p, [[-1,-1],[5,5]]);
nparts(v)                           // 5  — サイトの数と同じ
area(v)                             // 36 — 箱を覆い尽くす（6x6）
```

- ★★ **`part(v, i)` は サイト `i` のセル**（`delaunay` と違って**番号が定義で決まる**）。
  サイトごとに独立に箱を半平面で削るので、並べ替えが 1 回も起きない。
- ★ **Delaunay を経由しない**。セルの定義（垂直二等分線による半平面の共通部分）どおりに
  箱を削る（2D は Sutherland–Hodgman・3D は `PMP::clip`）ので、双対から作るときの
  「外心が遠方へ飛ぶ／退化で組合せが一意でない」が**まとめて起きない**。計算は **EPECK で厳密**。
- ⚠ **`box` は省略できない**。① セルは無限に伸びうるので有界な値として返すのに要る
  ② 暗黙の箱を使うと**箱の大きさで面積が黙って変わる**。書き方は対角 2 点
  （2D は `[[x0,y0],[x1,y1]]` / 3D は `[[x0,y0,z0],[x1,y1,z1]]`）。
- ⚠⚠ **サイトは箱の中に無ければならない**（外は明示エラー）。外のサイトを許すと*空のセル*が
  出て、「`part(v,i)` = サイト `i` のセル」が「空をどう表すか」という別の問題に化けるため。

**入力** `p` 点群 — `pt-cloud2d` / `pt-cloud3d` ／ `box` 対角 2 点 — `点列`

### `translate` / `rotate` / `scale` / `mirror` / `transform`（点群）— アフィン変換 {#pointcloud-transform}
`2D・3D` · → `点群`

**実装**: `points.so` · 型 `pt-cloud2d`(PTC2) / `pt-cloud3d`(PTC3)

引数の書き方と拒否の理由は**メッシュ版とまったく同じ**（[アフィン変換](#アフィン変換)参照 —
解釈は 7 カーネル共通の 1 か所にある）。点群では位相が無いので、**各点に行列を当てるだけ**。

**★★ 面外へ出す変換は `pt-cloud3d` を返す**

| 入力 | 変換 | 出力 |
|---|---|---|
| `pt-cloud2d` | z=0 平面を保つ | `pt-cloud2d` |
| `pt-cloud2d` | 面外へ出す | **`pt-cloud3d`** |
| `pt-cloud3d` | 何でも | `pt-cloud3d` |

- ⚠ 点群には**枠（平面）が無い**。`cgal` / `manifold` の `cg-face3d`（= 空間に置かれた 2D）に
  あたるものは無く、平面を出れば即 3 次元の点群になる。
- ⚠⚠ **`rotate` だけは軸が `"z"` のときしか 2D に留まらない**。どの行を選ぶかを決める仕掛けは
  引数を 1 個ずつしか見られないので、*軸と角度の両方*で決まる性質（`rotate(p,"x",180)` は
  平面を保つ）を判定できない。⇒ 保守的に軸だけを見る。これは `cgal` / `manifold` / `occt` の
  2D と**同じ扱い**。
- ★ `scale` の z 倍率は 2D では効かない（z=0 に掛かるため）。`mirror(p,"z")` も 2D では恒等。
  **エラーではない**。
- ★ 閾値は**相対 1e-12**。`rotate("x",180)` の行列に混じる `sin(π)=1.2e-16` は*丸めの残り*
  なので平面→平面と読み、意図した面外回転は **1e-9 度でも** 3D になる。
- 例:
  ```
  module("points.so", {});
  var p = points2d([[0,0],[1,0],[0,1]]);
  print(type_of(translate(p, [1,2])));       // pt-cloud2d
  print(type_of(translate(p, [0,0,1])));     // pt-cloud3d  ← 面外
  print(type_of(rotate(p, "z", 30)));        // pt-cloud2d
  print(type_of(rotate(p, "x", 180)));       // pt-cloud3d  ← ⚠ 平面は保つが軸が z でない
  ```
- ★★ **法線は余ベクトルとして運ばれる** — 点と同じ行列ではなく**逆転置**を当てる
  （3D）。2D の面内法線は*像の平面の中で*変換後の接線に直交する向きへ写る。
  ⇒ `scale(p,[2,1,1])` で法線が傾かない。反射（`mirror`）でも裏返らない。
- 関連: [`union`（点群）](#pointcloud-union), [アフィン変換](#アフィン変換)

### `union(a, b)`（点群）— 単純に混ぜる {#pointcloud-union}
`2D・3D` · → `点群`

**実装**: `points.so` · 型 `pt-cloud2d`(PTC2) / `pt-cloud3d`(PTC3)

2 つの点群を**単純に混ぜる**。⚠ メッシュの `union`（ブール和・重なりを解消する）とは
**別の計算**で、**重複は落とさない**。

**入力** `a`, `b` — `点群`　**出力** 混ぜた点群

- ★ 不変条件: `nverts(union(a,b)) == nverts(a) + nverts(b)`
- ★ `union(pt-cloud2d, pt-cloud3d)` → `pt-cloud3d`（2D 側は z=0 とみなす）
- ⚠⚠ **可換ではない**。並びは「格納順 = 入力の順」（[`vert`](#点群で使える既存-op) の約束）
  なので、`union(a,b)` と `union(b,a)` は*同じ点集合で違う点群*になる。
- ★ **n 項も書ける** — `union(a,b,c)` / `union([a,b,c])` / `a ||| b ||| c`。
  `sig` が `fold` 形なので、3 項以上は**二項の木へ分解**されて通る。
  ★ **畳む順で答えは動かない**（昇格が `max(次元)` = 結合的かつ単調なので、3D がどの位置に
  在っても結果は 3D）。⚠ 分解しても**並びは崩れない**（非可換なので左 `fold` になる）。
- ⚠ `union([])` は点群ではなく **`fold` の単位元 `{}`** を返す（`type_of` は `value`）。
  ★ これは**全カーネル共通の既存の振る舞い**で、点群に限った話ではない。
- ★ 法線は**空でない側がどちらも持っているときだけ**運ぶ。片方にしか無ければ**落とす**
  （無い方を埋めると下流の `estimate_normals` / Poisson が静かに嘘の形を返すため）。
  ⚠ 空の点群は法線の有無を問わない（`union(p, points3d([]))` は恒等）。
- 例:
  ```
  module("points.so", {});
  var a = points2d([[0,0],[1,0]]);
  print(nverts(a ||| points2d([[5,5]])));               // 3
  print(type_of(union(a, points3d([[0,0,9]]))));        // pt-cloud3d
  ```
- 関連: [アフィン変換（点群）](#pointcloud-transform) ／ メッシュ側の [`union`](#uniona-b-uniona-a-b-和) ／ [点群を形で切る](#ptsplit)

### `intersection(p, m, mode)` ／ `difference(p, m)` — 点群を形で切る {#ptsplit}

`点群 · メッシュ/2D領域 · 整数` · → `点群`

**実装**: `geomutils.so` · 型 `pt-cloud2d` / `pt-cloud3d`

点群を形 `m` に対して **3 つに分ける**。出力は入力点群の **部分集合**で、点は 1 つも
動かさず、**入力の順**をそのまま保つ。

| `mode` | 返るもの |
|---|---|
| `-1` | `m` の **内側**（開集合・境界を含まない）。⚠ `mode` を省いたときの既定 |
| `0` | `m` の **境界ちょうど**に載っている点 |
| `1` | `m` の **外側**。`difference(p, m)` はこれと同じもの |

⚠ 単項プラスは書けないので `+1` ではなく **`1`** と書く。

**★★★ なぜ「境界」が独立した 3 つ目なのか。** 境界ちょうどの点を内と外のどちらへ入れるかは
**一意に決まらない**。決め打ちは机上の心配ではなく、`rand` の整数格子では境界に載る点が
普通に出る（実測で 200 点中 194 点＝97% という例がある）。黙って片側へ倒すと、その 97% が
利用者に見えないまま片方に混ざる。⇒ 判定器に答えさせず、**第 3 の集合として切り出す**。
これは `section` の共面（[§断面](#sectionmesh-p-n-断面3-要素配列)）・`shell_at` の同距離・
`openvdb` の零交差の帯と
同じ約束で、`mode` の `0 / -1 / +1` も `section` と揃えてある。

**★ 厳密な分割**なので、次が常に成り立つ:

```
nverts(intersection(p,m,0)) + nverts(intersection(p,m,-1)) + nverts(intersection(p,m,1))
    == nverts(p)
```

- 「境界も含めた閉集合」が欲しいときは `union(intersection(p,m,0), intersection(p,m,-1))` と
  書く ⇒ **何を含めたかが式に出る**。
- **空洞（2D なら穴）の中の点は「外側」**。`m` が中空の箱なら、空洞の中の点は `mode 1` に出る。
- ★ **境界の「厚み」はモジュールごとに違ってよい**（`geomutils` は相対許容差 1e-12）。
  ⚠ 逆に内側 / 外側は **どのモジュールでも一致する**（十分離れた点しか入らないため）。

**受け付ける組み合わせ**（`X <= Y` が条件）:

| 点群 | 形 | 実装 | 判定 |
|---|---|---|---|
| `pt-cloud3d` | 3D メッシュ | `geomutils.so` | 巻き数 |
| `pt-cloud2d` | 3D メッシュ | `geomutils.so` | 巻き数。⚠ 2D 点群は **z=0 平面上の点**とみなす（暗黙の昇格） |
| `pt-cloud2d` | 2D 領域 | `geomutils.so` | point-in-polygon |
| `pt-cloud3d` | 距離場 `vd-grid3d` | `openvdb.so` | **距離場の符号** |
| `pt-cloud2d` | 距離場 `vd-grid3d` | `openvdb.so` | 同上。⚠ こちらも z=0 とみなす |
| `pt-cloud3d` | B-rep `oc-brep3d` | `occt.so` | **`BRepClass3d_SolidClassifier`** |
| `pt-cloud2d` | B-rep `oc-brep3d` | `occt.so` | 同上。⚠ こちらも z=0 とみなす |
| `pt-cloud2d` | `oc-cross2d` / `oc-face3d` | `occt.so` | 面までの距離 |

★ **`openvdb` の境界の帯は `0.75` ボクセル**（= `0.75 × dx`）で、`geomutils` の相対許容差
1e-12 よりずっと厚い。これは欠陥ではなく**距離場という表現の精度そのもの**で、`openvdb` が
`voxelize` の内外判定で零交差の近傍を切り離しているのと同じ考え方。**厳密が要るなら
`geomutils` か `occt` へ**。⚠ 逆に内側 / 外側は**どの実装でも一致する**（境界から十分離れた
点しか入らないため）。

★ `openvdb` は **2D 型を持たない**ので `pt-cloud3d` × 2D の組み合わせは存在しない。

★ **`occt` が 3 つの中で一番正確**。解析曲面のまま判定するので、球や円柱でメッシュ近似の
誤差が無く、**半径ちょうどの点**が正しく境界に出る（メッシュ系は内接多角形なので、それが
構造的に書けない）。境界の厚みは `Precision::Confusion`。

⚠ `oc-face3d`（枠が任意の 2D 領域）では、**面の平面に載っていない点は「外側」**。黙って
射影はしない。面は 3D の中の 2 次元の集合なので、面外の点を「含まない」と言うのは集合として
正しく、分割の不変条件も保たれる。⇒ **2D 点群（z=0）× 枠が z=0 でない `oc-face3d` は
全点が外**になる。`part_at` が面外を*断る*のと非対称に見えるが、あちらは「どの片か」を
答える op で射影すると嘘になるのに対し、こちらは「含むか」なので素直に「含まない」と言える。

**★★ 3 つの実装は内側 / 外側で一致する。** 境界から十分離れた点については `geomutils` /
`openvdb` / `occt` が同じ答えを返し、これは回帰テストで固定してある
（`test/srava_ptsplit_occt.sh` ⑤）。⚠ 一致するのは `mode -1` と `mode 1` だけで、
`mode 0`（境界）の個数は**厚みが違うので一致しない** — これは欠陥ではなく、各表現の精度の違い。

- ⚠ `pt-cloud3d` × 2D 領域（`X > Y`）は **明示エラー**。平面へ射影して答えると
  「面外の高さを黙って捨てた答え」になるため。
- ⚠ **可換ではない**。`intersection(メッシュ, 点群)` の順は受け付けない
  （型が非対称なので、可換にすると fold 分解が引数を組み替えて壊れる）。順で出力型が決まる。
- ⚠ `mode` が `0 / -1 / 1` 以外なら明示エラー。

### 点群で使える既存 op

| op | 意味 |
|---|---|
| `nverts(p)` | 点の数 |
| `vert(p, i)` | `i` 番目の点の座標（`[x,y]` / `[x,y,z]`） |
| `bbox(p)` | 軸平行 AABB（⚠ 空の点群はエラー） |
| `centroid(p)` | **点の平均**（メッシュの面積/体積重心とは別物だが「重心」の約束は同じ） |
| `valid(p)` | 共通定義のうち **①「空でない」だけ**が意味を持つ（②③ は点群では構造的に恒真） |
| `import("a.xyz")` | 3 列（`x y z`）または **6 列（`x y z nx ny nz`）**。2 列は z=0 として読む |
| `export("a.xyz", p)` | **2D・3D とも書ける**。列数は 2 / 3 / 6（下記）。⚠ 往復で 3D になる |

- ★ `hull(p)` / `delaunay(p)` / `voronoi(p, box)` / `distance(p, m)` / `closest` / `farthest` も
  点群をそのまま受ける（→ 各項）。
- ⚠ `area(p)` / `volume(p)` は点群では定義できない ⇒ **明示エラー**
  （`no module can execute op 'area' on input types (pt-cloud3d)`）。
- ⚠ 形式は当面 **`xyz` だけ**。`ply` は「メッシュにも純粋な点群にもなりうる」のに型は拡張子で
  決まる（中身を見てから選べない）ので割り当てない。`off` も面 0 個なら同じ問題。
- ★ **2D 点群も `.xyz` に書ける**（2026-09-22 にひさ判断で解禁）。列数は `.xyz` の
  約束どおり **2 / 3 / 6**:

  | 入力 | 列数 | 内容 |
  |---|---|---|
  | 法線なし `pt-cloud2d` | 2 | `x y` |
  | 法線あり `pt-cloud2d` | 6 | `x y 0 nx ny 0` |
  | 法線なし `pt-cloud3d` | 3 | `x y z` |
  | 法線あり `pt-cloud3d` | 6 | `x y z nx ny nz` |

  ⚠⚠ **往復で型が変わる** — `export` して `import` し直すと `pt-cloud2d` は
  **`pt-cloud3d`（z=0）**で戻る。`.xyz` に「2D である」と書く場所が無いため。
  ★ 黙って起きるわけではない（ここに書いてある・回帰でも明示的に検定している）。
  ★ 法線つき 2D を 6 列にするのは**情報を落とさない**ため。4 列（`x y nx ny`）は
  読む側が受け付けないので、これ以外に法線を残す書き方が無い。
- ★ `translate` / `rotate` / `scale` / `mirror` / `transform` / `union` も点群を受ける
  （→ [アフィン変換（点群）](#pointcloud-transform) / [`union`（点群）](#pointcloud-union)）。

---

## 配列・数値ユーティリティ

planner 側 op（agent 不要・CGAL に触れない）。

⚠ **擬似乱数 `rand` はここには無い** → [点群](#rand)。`points.so` の op なので planner 側では
なく、シードが省略できない（同じ引数なら必ず同じ結果）という別の約束を持つ。

### `length(x)` — 要素数
`配列・文字列` · → `整数`

**実装**: 組み込み · 型 -

配列 / ハッシュの要素数。それ以外はエラー。

**入力** `x` 対象 — `配列` または `ハッシュ`

**出力** 要素数 — `整数`

- 例: `length([1,2,3])` → `3`
- ⚠ **「配列か?」の判定には使えない** — スカラを渡すと述語ではなく**エラー**になる
  (`length: argument is not an array or hash`)。種別を訊くなら [`kind_of(x)`](#kind_of)。

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
- ★ `print(…, volume(m), bbox(m))` のように独立な計測を並べると、引数は**一斉に起動**されてから解決される。⚠ **2026-08-24 より前のビルドでは直列**だった（左から 1 つずつ評価）。

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

- **名前を付けて並列に受けたい**ときは**分割代入** `var [a, b] = [volume(m1), volume(m2)];`（1 文なので並列。→ 言語リファレンス「分割代入」）。
- **約束**：制御文（`{}`/`if`/`while`/`for`）と**文の並び**は直列。1 つの文の中に書いた独立な計算は並列（`async` / `map` / 配列リテラル / 演算子 / `print`・`concat` の引数）。⚠ **値の計算を別々の文に分けると直列**（mesh は文を分けても並列）。→ 言語リファレンス「並列に走るもの・走らないもの」。
- `sync:` は**順序付き出力**であって順序付き実行ではない。`body` の計算は並列、`sync:` 文の**出力**だけが順序付く。
- `sync:` 文は `body` の**後**に走るので、`async { export("a.stl", m); sync: print("saved a"); }` の "saved a" は **a.stl 書き込み完了後**に出る（`export_async`+`gate` の「計算完了 ≠ 書込完了」問題が無い）。
- **エラーは continue-and-collect**：`body`/`sync:` でエラーが出ても他の `async` は止まらず、エラーは末尾でまとめて報告され終了コードに反映（当該 `async` の `sync:` はスキップ）。チェーンは必ず前進するのでデッドロックしない。
- 例（並列計算・順序表示）: `async { var v = volume(m); sync: print("vol", v); }` を複数並べる。
- 例（`par` の代替）: `var ab = [volume(part), bbox(part)]; print("vol", ab[0], "bbox", ab[1]);`（`[..]` が並列評価）
- 関連: `print_async`, `export_async`, `flush`, `gate`

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

**実装**: `cgal.so` / `manifold.so` / `nef_snc.so` / `nef_hybrid.so` / `geogram.so` / `cherchi.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 value（cgal は cg-/mf- 全型を引受＝universal reader）

mesh をファイルへ書き出し（形式は拡張子で自動判別）。引数 1 個は passthrough（書き出さず継続を値化）。

**入力**
- `path` 出力パス — `文字列`
- `mesh` 対象 — `mesh`
- `unit` 単位 — `文字列`（`"mm"`/`"cm"`/`"m"`/`"in"`/`"ft"`/`"micron"`…・**AMF/3MF/SVG/DXF/IGES が使用**・OFF/STL/OBJ/PLY/STEP/.brep は無視・省略可）

manifold.so が直接書けるのは `stl` / `off` / **`3mf` / `amf`**（3MF/AMF は cgal.so と同じ共通ライタ `src/h/common/mesh3mf.h`・色と単位を保持）。それ以外（obj/ply/svg/dxf…）は cgal.so が引き受ける。

**出力** — 引数 1 個版は `mesh`（passthrough）、書き出し版は `null`

- 形式: 3D＝`.off`/`.stl`/`.obj`/`.ply`/`.amf`/`.3mf` / 2D＝`.svg`(入出力)/`.dxf`(入出力)。次元と拡張子の不一致はエラー
- ★★ **2D の `.dxf` / `.svg` は 2 つのカーネルが別々に書く**。同じ拡張子だが**中身の語彙が違う**:

  | 書き手 | 語彙 | 置き場所 |
  |--------|------|----------|
  | `cgal.so` | **折れ線だけ**（閉 `LWPOLYLINE` / `<path>` の `M`・`L`）＋ ガイド層 | DXF は **OCS** で空間の平面も書ける |
  | `occt.so` | **曲線のまま**（`LINE` / `CIRCLE` / `ARC` / `ELLIPSE` / `SPLINE`） | **`z=0` の図面だけ** |

  ⇒ 円を円のまま外へ出したいなら `occt` に書かせる（`hlr` の図面・`circle` など）。
  ⚠ `occt` の SVG は**円弧まで**厳密（`A` コマンド）。**B-spline は SVG の語彙に無い**ので
  そこだけ折れ線に落ちる。曲線を保ちたいなら DXF か STEP。
  ⚠ `occt` に平面の外の 2D を渡すと**理由つきで断る**（`project_flatten` を案内する）。
  黙って `z=0` へ潰さない。
- ★ **`occt.so` だけが `.step`/`.stp` ・ `.iges`/`.igs` ・ `.brep` を書ける**。
  ここが「メッシュの道具」と「CAD の道具」を分ける一線で、**解析曲面（平面/円柱/球/NURBS）と
  位相をそのまま運ぶ**。メッシュ形式へ書いた瞬間に軸と半径は失われる（後から正確なフィレットも
  設計公差での加工もできない）。⇒ 新規に選ぶなら **STEP**、レガシー資産の受け渡しなら **IGES**
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

**実装**: `openvdb_cg.so` · 型 value（HDF5 vox.h5）
⚠ 2026-09-01に `cgal.so` から移設した。`module("cgal.so",{})` だけを書いている
スクリプトは **`module("openvdb_cg.so",{})` の追加が要る**（`include "module/all.sra";` なら不要）。

複数の領域メッシュを共通の Cartesian 格子へボクセル化し、各領域を名前付きマスクとして中立フォーマット **vox.h5**（格子 + マスク）へ書き出す。k-Wave 等の格子ソルバ連携用（→ [シミュレーション（k-Wave）](srava_kwave.html)）。

**入力**
- `path` 出力パス（`.h5`）— `文字列`
- `params` — `ハッシュ`：`{ dx, pad, regions }`
- `mesh…` 領域（可変個）— **3D メッシュ**（`cg-mesh3d` / `mf-mesh3d` / `gg-mesh3d` /
  `ch-mesh3d` / `nfb-mesh3d`）**または ボリューム格子**（`vd-grid3d`）。 で混在可になった。

> ★ **内外判定の規則が入力の種類で違う**（同じ答えを速く出すのではなく **別の答え**）:
>
> | | メッシュ入力 | `vd-grid3d` 入力 |
> |---|---|---|
> | 内外判定 | **厳密 z-パリティ**（EPECK + symbolic perturbation） | level set の**符号**（world 空間で補間サンプル） |
> | 決定性 | メッシュの幾何だけの関数 | **grid の dx で既に離散化済み** |
>
> 精度は「出力格子の `dx`」と「grid 自身の `dx`」の**粗いほう**で決まる。
> `vd-grid3d` の価値は「openvdb で作った形をそのまま h5 に落とせる」ことで、速さの話ではない。
>
> ⚠ 複数の `vd-grid3d` が**同じ格子に乗っている必要は無い**（`dx` も原点も違ってよい）。
> 各入力を出力格子へ独立にラスタライズするため。
  - `dx`（必須）格子ピッチ（mesh と同じ単位）
  - `pad`（既定 8）形状の bounding box 外側に足すボクセル数（PML 余白用）
  - `regions` 領域メタ配列 `[{name, side}, …]`。`name`=マスク名、`side`=`"inside"`（メッシュ内部）/`"outside"`（外部）。`regions[i]` が `i` 番目のメッシュに対応
- `mesh…` 領域メッシュ（**可変個の位置引数**・`regions` と同順）— `mesh`

**出力** — `null`（副作用で vox.h5 を書く）

- 格子は全メッシュの**共通 bbox + pad·dx**、ピッチ `dx` で自動決定（`origin`=セル(0,0,0)中心）。各セル中心を内外判定（z-パリティ法）して `side` で焼く。
- **内外判定は厳密（v1.1.0-rc2 以降）**。判定・交点は厳密有理数で行い、サンプル点が面の辺・頂点・鉛直面にちょうど載る縮退は記号的摂動で一貫して解く。したがって**結果はメッシュの幾何だけで決まり**、実行のたびに変わったり、同じ形状でも内部表現の違いで変わったりしない。
  - サンプル点は**セル中心** `origin + (i+1/2)·dx`。格子に整列した設計値（`dx` の整数倍の半径・平面）と縮退しにくく、格子面上に軸を持つ対称形状では**鏡像対称が保たれる**。
  - z 方向の充填は半開区間 `[z_lo, z_hi)`。接する立体どうしが**隙間なく・二重なく**敷き詰まる。
- **メッシュは位置引数**（`combine` と同じく）。srava ではメッシュ（cache）と値（ハッシュ/配列）が別経路で渡るため、`regions` の中にメッシュは入れられない。
- vox.h5 スキーマ: `Nx/Ny/Nz`(int64)・`dx/dy/dz`(float64)・`origin`(float64[3])・`masks/<name>`(uint8[Nx,Ny,Nz])。属性 `format="srava-vox"`。
- 例:
  ```
  export_vox("vox.h5",
     { dx: 1.0, pad: 8, regions: [ {name:"air", side:"inside"}, {name:"wall", side:"outside"} ] },
     air_mesh, wall_mesh);
  ```
- 関連: [シミュレーション（k-Wave）](srava_kwave.html), `tube`, `export`

### `flush()` — その `try` の待ちが空になるまでのバリア
— · → `null`

**実装**: 組み込み · 型 -

**その地点を囲む `try` で起動した計算**（幾何 op も `async` も）が全部終わるまで待つ。
出力ファイルを読む `system` / `import` の直前に置く。

**入力** なし

**出力** — `null`

- **`try` 本体の中でも `catch` の中でも意味は同じ**。`try` を書かなかった場所（= 見えない `try` の下）
  で呼べば従来どおり「全部」を待つ。
- ⚠ **エラーは報告しない**。エラーは `try` の列に残り `catch` の `error()` が読む
  （`try` を書かなかった場所の分は末尾でまとめて報告される）。
- `flush()` の後の `async` は**独立した発行順**になる（`sync:` の整列チェーンが張り直される）。
- 関連: `async`, `try`, `error`

### `import(path)` — 読み込み
`2D・3D` · → `mesh`

**実装**: `cgal.so` / `manifold.so` / `occt.so`（→ [対応表](#module-matrix)） · 型 `cg-mesh3d`(MESH)・`cg-cross2d`(PLY2) / `mf-mesh3d`(MFM3)

★ 読める形式は **カーネルごとに違う**（`import_exts` の申告どおりに振り分けられる）:
`cgal.so` = OFF / STL / OBJ / PLY ＋ 2D の SVG / DXF、`occt.so` = **STEP / IGES / BREP ＋ DXF**、
それ以外（`manifold` / `nef` / `geogram` / `cherchi` / `openvdb`）= **STL / OFF**
（共通の読み手 `src/h/common/meshio.h`）。

★★ **`.dxf` は 2 つのカーネルが読める。読める語彙が違う**:

| 読み手 | 読めるもの | 読めないもの |
|--------|-----------|-------------|
| `cgal.so` | 閉 `LWPOLYLINE`/`POLYLINE`（領域）・開いたもの（ガイド層）・**OCS**（置き場所） | 曲線（`LINE`/`CIRCLE`/`ARC`/`ELLIPSE`/`SPLINE`/bulge）⇒ **明示エラー** |
| `occt.so` | `LINE`/`CIRCLE`/`ARC`/`ELLIPSE`/`SPLINE`/`LWPOLYLINE`/`POLYLINE` を**曲線のまま** | OCS つき（置かれた図面）⇒ **明示エラー** |

⚠⚠ **`cgal` は曲線を折れ線に落として読んだりしない**（黙って形を変えないため）。
`cgal` の 2D は折れ線の世界なので、曲線を勝手に刻む約束をこちら側に持たせない
—— *どれくらい細かく刻むか* は `polygonize(2d, たわみ)` が既に持っている約束だから。
⇒ **円が `cgal` で欲しいときは `occt` を通す**:

```
cast("cg-cross2d", polygonize("occt"::import("plan.dxf"), 0.001))
```

⚠ `TEXT` / `DIMENSION` など**幾何でない実体**は従来どおり無視する（断るのは
「図形の一部なのに落ちるもの」だけ）。
★ `openvdb.so` は末尾に `dx` が要る: `import(path, dx)`。

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

### `try { … } catch { … }` — エラーを捕まえる
文 · → —

**実装**: 組み込み · 型 -

`try` 本体でエラーが起きたら `catch` 本体を実行する。**本体は両方とも `{}` 必須**（`async` と同じ）。
`catch` を省くと、起きたエラーをそのまま上へ返す（握り潰さない）。

- **コントロール系（`break` / `continue` / `return` / `exit`）は捕まえず素通し**する。
  `try` が持つのは制御の分岐ではなく「`{}` で囲った範囲」だから。
- **撤収するかどうかは `catch` が決める**：`catch` の最後の値がエラーでなければ try/catch は正常終了し、
  後続の文へ進む。`catch` がエラーを返せばそれが try/catch の値になる。
- **try は自分のスコープで起動した計算を見送る**：`try { … }` の中で起動した幾何 op はその try の
  待ちリストに入り、**どの終わり方でも全部終わるまで try を抜けない**（＝暗黙のバリア）。
  終わり方で変わるのは **`destroy` を送るかどうかだけ**（`catch` がエラー/制御信号を返したときと
  `catch` が無いときは送る・それ以外は送らず生かして待つ）。
- **どの try が捕まえるかは呼び出し元で決まる**（動的）。try の外で定義したヘルパ lambda を
  try の中で呼べば、その中のエラーもこの try が捕まえる（C++ の例外と同じ直感）。
- **`async` 本体のエラーも捕まる**：`async` は起動した時点で try の待ちリストに入るので、
  値を見る者が居なくても `catch` に届く。⚠ 本体が終わってから届いたエラーでも `catch` は走る。
  トップレベル（`try` を書かなかった場所）の `async` の失敗は従来どおり末尾でまとめて報告される。
- 例:
  ```
  try {
      export("out.stl", union(a, b));
  } catch {
      print("union に失敗:", error().message);
      export("out.stl", a);              // 代替で畳む
  }
  ```
- 関連: `error`, `throw`, `destroy`, `exit`, `async`

### `error()` — 捕まえたエラーの中身を取り出す
値 · → ハッシュ or 整数

**実装**: 組み込み · 型 -

`catch` 本体で、起きたエラーの**中身をハッシュで**発生順に 1 件ずつ返す。もう無ければ **`0`**。

**入力** なし

**出力** エラーの中身 — `ハッシュ`／もう無ければ `整数` `0`

| 鍵 | 中身 | 例 |
|---|---|---|
| `message` | 文言（モジュール名/op を含む・属性タグは含まない） | `cgal/box: sizes must be > 0` |
| `class` | エラークラス | `normal` / `fatal` / `derived` / `panic` |
| `file` | ソース名（無ければ空文字） | `<source>` |
| `line` | 行番号（無ければ 0） | `2` |

- **`catch` 本体の中でだけ**呼べる。外に書くと **パース時にエラー**になる（実行前に分かる）。
- **何度でも呼べる**。「もう無い」は `0` で分かるので `if (e != 0) { … }` で判定する。
- ★ **待つのはこの関数**。待ちリストに計算が残っていれば、次のエラーが出るか全部終わるまで
  ブロックする（全部終われば `0`）。
- 返るのは**中身のハッシュであってエラー値ではない**。⚠ エラー値はあらゆる演算を吸収して上方伝播
  するので、変数に入れた時点で `catch` 自身がそのエラーで抜けてしまう。
- この 4 つで元のエラーは**完全に復元できる** → `throw`。
- 例: `if (e.class == "fatal") { throw error(); } print("復旧:", e.message);`
- 関連: `try`, `throw`, `print`

### `throw 式;` — エラーを復元して発生させる
文 · → —

**実装**: 組み込み · 型 -

`error()` が返したハッシュから **元のエラーを復元**して発生させる。位置・モジュール名・エラークラスまで
そのまま再現するので、表示される行は `throw` を書いた行ではなく**元々落ちた行**になる。

**入力** `式` エラーの中身 — `ハッシュ`（`error()` の戻り値）

**出力** なし（エラーが発生して上方へ伝播する）

- ⚠ 復元できない値（`0`・ハッシュでない値・`message` 鍵が無い・`class` が未知）を渡すと
  **「復元できない」というエラー**になる。黙って無視はしない。
- ★ これにより `try { s }` と `try { s } catch { throw error(); }` が**厳密に同じ意味**になる。
- 関連: `error`, `try`, `exit`

### `destroy()` — 走っている計算を畳む
値 · → 整数

**実装**: 組み込み · 型 -

囲む `try` の待ちリスト（そのスコープで起動した幾何 op）へ**撤収を送る**。**終了は待たない**
（待つのは `error()` と try の出口）。

**入力** なし

**出力** 送った数 — `整数`

- **トップレベルでも呼べる**：プログラム全体は**見えない `try`** に囲まれていて、そこで起動した
  計算はその待ちリストに入る（何も走っていなければ `0`）。
- ★ **内側で走っているものにも届く**：内側の `try` の中・`async` の中・`map` の中・`system()` の
  子プロセスまで畳まれる（撤収は式の木を辿って伝わる）。戻り値の「送った数」は**直接の送り先の数**で、
  木を伝って畳まれたぶんは含まない。
- ★ **畳まれた計算はプログラムの失敗にならない**：`try` が畳んだ分の「aborted」は「畳まれた跡」
  （`class` が `derived`）として扱われ、末尾の報告にも終了コードにも出ない。畳む前に自分で
  起きていた本物の失敗はそのまま報告される。
- 典型: `catch { destroy(); print("打ち切りました"); }` — 最初の 1 件で全部畳む。
  逆に `catch` がエラーを返さず `destroy()` も呼ばなければ、残りは**走り切る**。
- 関連: `try`, `error`, `throw`

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

### `vcross(a, b)` — 外積  〔stdlib: math〕
`配列` · → `配列`

**実装**: `include "std/math.sra"` · 型 -

**入力** `a, b` — `3D ベクトル`

**出力** `a × b` — `3D ベクトル`

- ⚠ **3D 専用**（2D の外積は擬スカラで型が違う）。
- 関連: `vdot`, `rotmat_2v`

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

### `rotmat_2v(v1, v2)` — v1 を v2 の向きへ持っていく回転  〔stdlib: math〕
`配列`

**実装**: `include "std/math.sra"` · 型 -

回転軸は `v1 × v2`。`c = vnorm(v1)·vnorm(v2)` が `-1` でなければ三角関数を使わずに
`R = I + K + K²/(1+c)`（`K` は `v1 × v2` の歪対称行列）で書ける。

**入力** `v1`, `v2` 向き — `3D ベクトル`（長さは問わない）

**出力** 3×3 回転行列 — `行列`

- 例: `rotate_pts(ps, rotmat_2v([1,0,0],[0,0,1]))` / `transform(m, mat34(rotmat_2v(a,b)))`
- 退化の扱い:
  - `v1` と `v2` が**同じ向き** → 単位行列
  - **逆向き** → `v1` に直交する軸まわりの 180°。軸は数学的に一意でないが**実装は決定的に
    1 つ選ぶ**（`|v1|` の成分が最小の座標軸との外積）。実行のたびに違う軸を選ぶと同じ
    スクリプトが違う結果を出すため
  - どちらかが**零ベクトル** → 向きが定義できないので **NaN 行列**（黙って単位行列を返さない。
    stdlib からエラーを起こす手段が言語に無いため、使った瞬間に座標が NaN になる形にしてある）
- 関連: `vcross`, `mat34`, `rotate_v`

### `mat34(M)` ／ `mat34_t(M, t)` — 3×3 → transform 用の平坦 12 要素  〔stdlib: math〕
`配列`

**実装**: `include "std/math.sra"` · 型 -

`rotmat_*` / `rotmat_2v` が返す **3×3 の入れ子**を、`transform(m, matrix)` が要求する
**行優先 12 要素（3×4）**へ直す。`mat34_t` は平行移動 `t = [tx,ty,tz]` つき。

**入力** `M` — `3×3 行列` ／ `t` 平行移動 — `3D ベクトル`

**出力** 12 要素 — `配列`

- ★ これが無いと `rotmat_*` は事実上 **点列専用**（`rotate_pts`）で、メッシュへ当てる道が無い。
- 例: `transform(m, mat34(rotmat_z(rad(30))))` / `transform(m, mat34_t(R, [5,0,0]))`
- ⚠ 引数の省略は言語が持たないので 2 本に分かれている。
- 関連: `transform`, `rotmat_2v`

### `rotate_v(m, v1, v2)` — mesh を「v1 の向き→v2 の向き」へ回す  〔stdlib: math〕
`3D` · → `mesh`

**実装**: `include "std/math.sra"` · 型 入力を保存

`transform(m, mat34(rotmat_2v(v1, v2)))` の 1 行ラッパ。原点まわり。

**入力** `m` 対象 — `mesh` ／ `v1`, `v2` 向き — `3D ベクトル`

**出力** 回転後 — `mesh`

- 例: `rotate_v(box(3,1,1), [1,0,0], [0,0,1])`（x 方向の棒を z 方向へ立てる）
- ⚠ std/math で唯一 mesh op（`transform`）に触れる関数。呼ばなければ幾何カーネルは要らない。
- 関連: `rotate`, `transform`, `rotmat_2v`

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

- 例: `tube_ruled(map(bezier([[0,0,0],[2,2,0],[4,0,2]], 16), \(p){ [p, 0.3]; }))`
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

### `tube_fw_ruled(pts, w)` — 定幅の掃引（`fw` = fixed width）  〔stdlib: curve〕
`2D・3D` · → `mesh`

**実装**: `include "std/curve.sra"` · 型 -

折れ線を一定幅 `w` で太らせる（丸ジョイント/丸キャップ）。`tube_ruled` に半幅 `w*0.5` を渡す薄いラッパ。
可変幅が要るときは `tube_ruled` を直接（各頂点の半径 `r` を自分で付ける）。

★ **次元は `pts` の位置の次元に従う** — 2D 点列なら帯、3D 点列なら管。名前に `2d` が入らないのはそのため
（旧名は `ribbon2d` だったが、実装は最初から次元非依存だった）。

⚠ **`w` は「幅」であって半径ではない**（半径は `w/2`）。

**入力** `pts` 折れ線（`点列`＝2D/3D ベクトルの配列・半径なし）, `w` 全幅（`スカラ`）

**出力** 帯（2D）／管（3D） — `mesh`

- 例: `export("trace.svg", tube_fw_ruled(bezier([[0,0],[20,20],[40,0]], 16), 5), "mm")`
- 関連: `tube_ruled`, `tube_fw`（なめらか版）, `tube`

### `tube_fw(pts, w)` — 定幅の掃引・**なめらか版**  〔stdlib: curve〕
`3D` · → `mesh`

**実装**: `include "std/curve.sra"` · 型 -

折れ線を一定幅 `w` で太らせる `tube_fw_ruled` の**なめらか版**。`tube` に半幅 `w*0.5` を渡す薄いラッパ。
op の対（`tube` / `tube_ruled`）を stdlib へそのまま写した名前で、分かれ目は**背骨** —
`tube` は点を**通る** C2 の B-spline、`tube_ruled` は点を直線で結ぶ折れ線。

⚠ **`tube` は `occt` だけが持つ op** なので `module("occt.so", {})` が要る。ロードしていなければ
`no module can execute op 'tube'` で落ちる（**黙って折れ線版に化けることはない**）。

⚠⚠ **次元の扱いが `tube_fw_ruled` と違う**。`tube` は `occt` 単独の op で突き合わせる相手が
居ないため、2D の点列を **z=0 の 3D として**受ける ⇒ 返るのは*帯ではなく平たい立体*。
帯が欲しいときは `tube_fw_ruled` を使うこと。

| | 呼ぶ op | 要るモジュール | 2D 点列を渡すと |
|---|---|---|---|
| `tube_fw_ruled(pts, w)` | `tube_ruled` | `tube_ruled` を持つ 8 本のどれか | **2D の帯**（メッシュ系のみ。`occt` は明示エラー） |
| `tube_fw(pts, w)` | `tube` | **`occt` だけ** | **平たい 3D の立体** |

⚠ **`w` は「幅」であって半径ではない**（半径は `w/2`）。

**入力** `pts` 折れ線（`点列`＝3D ベクトルの配列・半径なし）, `w` 全幅（`スカラ`）

**出力** 管（3D） — `mesh`

- 例: `module("occt.so",{}); export("pipe.step", tube_fw(bezier([[0,0,0],[20,20,0],[40,0,10]], 16), 5))`
- 関連: `tube_fw_ruled`（線織版）, `tube`, `tube_ruled`

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
- 使い方（肉厚 d のシェル）: `difference(tube_ruled(tube_wall(path, d)), tube_ruled(path))`。可変肉厚は `tube_wall_var(path, ds)`。
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

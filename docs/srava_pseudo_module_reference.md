# srava 擬似モジュールリファレンス

**擬似モジュール**は、`.so` を持たない**値だけのモジュール**。候補列の要素にハッシュを書くと、
その op は **srava の式（ラムダ）として実行される**。このページは同梱の擬似モジュール集
`module/pseudo.sra` の**利用者向け仕様**を扱う。

- 仕組みそのもの（記述子の形・候補列・`mod::` 指名）は
  [言語リファレンス §擬似モジュール](srava_language_reference.html#pseudo-module)。
- `.so` のモジュールは [モジュールリファレンス](srava_module_reference.html)。
- 各 `pm_*` の呼び出し形は [関数リファレンス §`pm_cgal(opts)` ほか](srava_function_reference.html#pmod)。

---

## なぜ要るか — **粒度の既定値を誰かが決めなければならない** {#why}

分割数 `seg` / ボクセルサイズ `dx` は**近似の細かさ**なので、op ごとに引数で渡すのが原則。
だが「この計算は全部 `seg=64` で」と決めたい場面では、同じ数を**全部の呼び出しに書いて回る**
ことになる。⚠ そして**書き漏らしは静かに既定値（32）で通る** — 落ちないので気づけない。

⇒ 擬似モジュールは、その埋め合わせを**呼び出しの手前**で行う。

★ `cast` ではできない。`cast` は型を変えるだけで、**粒度という「どこにも書いていない数」を
決められない**から。擬似モジュールは*式の中で*それを決める道具になる。

---

## 使い方 {#usage}

`use [ … ]` の候補列に置く。`pm_*` は **[擬似, 実モジュール名] の組**を返すので、**1 個書けば
両方**が候補列に入る（擬似が受けなかった呼び出しは、そのまま実モジュールへ降りる）。

```
module("cgal.so", {});               // ← 使う .so だけを名指しでロードする
include "module/pseudo.sra";         // 擬似モジュール集

use [ pm_cgal({seg:64}) ];
var a = sphere(1.5);                 // ← seg を書いていないが 64 で作られる
var b = sphere(1.5, 8);              // ← 明示した 8 が勝つ（省略形と明示形の両方を受ける）
```

⚠ **`include` はロードではない。** `.so` を載せるのは `module()` / `module/all.sra` の仕事で、
`pseudo.sra` は**候補列に書く値**を定義するだけ。両方要る。

### ★ 使う `.so` が決まっているなら `module()` で名指しする {#name-the-modules}

`include "module/all.sra";` は同梱のカーネルを一式ロードする。**手軽だが無料ではない。**
同梱モジュールは増え続けるので、全ロードの負荷はこの先も増える。加えて、候補列を書かずに
一式を載せると、**その op がどの `.so` に当たるかが読んだだけでは決まらない**。

使うものが決まっているなら、`module()` で名指しする方が**常駐が小さく、配線が読んで分かる**。
実際に、全ロードから 2 本の名指しへ移して**出力がバイト単位で変わらないまま常駐が目に見えて
減った**利用例がある（外れた十数本は、その用途では最初から使われていなかった）。

```
module("cgal.so", {});
module("pipe_proximity.so", {});
include "module/pseudo.sra";
use [ pm_cgal({}), "pipe_proximity" ];
```

⚠ `pm_cgal({})` のように **`{}` を書く**（`pm_cgal()` は不可）。`seg` を省けば各カーネルの既定値になる。
⚠ 実モジュール名は **引用符つき**（`"pipe_proximity"`）。引用符が無いと `undefined variable` になる。

★ どの op がどの `.so` に在るかは [`srava --module-info`](srava_install_guide.html#module-info)
で引ける（`--modules` が**在庫一覧**、`--module-info` が**中身**）。
[op × モジュールの表](srava_function_reference.html#module-matrix) も同じことを一覧で見せる。

### ⚠⚠ `use` は候補列を**置き換える** — 並べ忘れた op は落ちる {#use-replaces}

`use [ … ]` を書いた瞬間、**そこに並べたものだけ**が候補になる。`module/all.sra` で
ロード済みでも、候補列に居なければ選ばれない。落ち方はこの形:

```
*** USE_MODULES ('pm_cgal','cgal'): none of them implements op 'export_vox'
    (see `srava --module-info <name>`) ***
```

**この形で落ちたら、足りない実モジュールを引用符つきで候補列に併記する。**

```
use [ pm_cgal({}), "openvdb_cg" ];       // ← export_vox を持つモジュールを足した
```

⚠ 踏みやすいのは、**生成 op と、それ以外の op（I/O・ソルバ）が同じスクリプトに同居している**
ときである。粒度の既定値を入れたいのは生成 op だけでも、`use` は**スクリプト全体**に効く。

#### 候補列を絞ると、**暗黙のキャストも止まる**

落ちるのは「op が無い」ときだけではない。op は在っても、**入力の型を合わせる変換が
自動では挟まらなくなる**:

```
use [ pm_cgal({seg:32}) ];
volume(offset(box(1,1,1), 0.1))
*** USE_MODULES: op 'offset' does not accept input type(s) cg-mesh3d in any candidate ...
    ★ qualification narrows the candidates, it does not insert a cast
       — convert explicitly with cast(<target type>, ...) ***
```

（`use` を書かなければ、同じ式はそのまま通る。）

★ **これは欠陥ではなく、そういう仕様である。** 裏返せば、**使っていないモジュールへ勝手に
配線されて意図しない動きをすることが無くなる**。候補列が明示されていれば、
外したものが効いていなかったことを**利用者自身が検算できる**。
黙って別のものに当たるより、明示して落ちる方がよい、という設計である。

⚠ 維持コストは「**op を足すたびに候補列を見直す**」。ただし静かに壊れることはなく、
必ず上の形で落ちるので、気づけないコストではない。

---

## 一覧 {#list}

| 名前 | 受け取る `opts` | 実モジュール | 何をするか |
|---|---|---|---|
| `pm_cgal(h)` | `seg` | `cgal` | 分割数の既定値を練り込む |
| `pm_manifold(h)` | `seg` | `manifold` | 同上 |
| `pm_geogram(h)` | `seg` | `geogram` | 同上（2D は無い） |
| `pm_cherchi(h)` | `seg` | `cherchi` | 同上（2D は無い） |
| `pm_nef(h)` | `seg` | `nef_hybrid` | 同上（2D は無い） |
| `pm_openvdb(h)` | `seg` / `dx` | `openvdb` | **`dx` が必須**のカーネル向け。`seg` を持つ op と持たない op が混ざる |
| **`pm_points(h)`** | （なし） | `points` | ★ **役割が違う** — 既定値ではなく**引数の少ない形を足す**（[下記](#pm-points)） |

⚠ 知らないキーを渡すと**明示エラー**になる（`pm_cgal: 知らないキーがある`）。黙って無視しない。

---

## A 群 — メッシュ系（`seg` を埋める） {#mesh-group}

`pm_cgal` / `pm_manifold` / `pm_geogram` / `pm_cherchi` / `pm_nef`。

**定義している op**（`引数` の列は `in[]` のスロット・`必須` は `nreq`）:

| op | 引数 | 必須 | 埋めるもの | 本体が呼ぶもの |
|---|---|---|---|---|
| `sphere(r[, seg])` | 値 / 値 | 1 | `seg` | `mod::sphere(r, seg)` |
| `cylinder(r, h[, seg])` | 値 / 値 / 値 | 2 | `seg` | `mod::cylinder(r, h, seg)` |
| `cone(r, h[, seg])` | 値 / 値 / 値 | 2 | `seg` | `mod::cone(r, h, seg)` |
| `torus(rr, r[, seg])` | 値 / 値 / 値 | 2 | `seg` | `mod::torus(rr, r, seg)` |
| `tube_ruled(path[, seg])` | 値 / 値 | 1 | `seg` | `mod::tube_ruled(path, seg)` |
| `tube(path[, seg])` | 値 / **述語** | 1 | `seg` ＋ **名前の橋渡し** | `mod::tube_ruled(path, seg)` |
| `circle(r[, seg])` ※2D | 値 / 値 | 1 | `seg` | `mod::circle(r, seg)` |
| `revolve(g[, deg][, seg])` ※2D | 幾何 / 値 / 値 | 1 | `deg`（既定 360）＋ `seg` | `mod::revolve(g, deg, seg)` |

- ※2D の 2 行（`circle` / `revolve`）は **`pm_cgal` / `pm_manifold` にしか無い**。
  ⚠ 2D を持たないカーネルにこの行を足すと、擬似が受けてから「そんな op は無い」で落ちる
  ＝ **隣へ降りる道を塞ぐ**。
- ★ `tube` は**名前の橋渡し**。`tube` は occt だけの op になったが、occt は第 2 引数が
  ハッシュのときだけ成立する。⇒ `tube(path, 24)` では occt が候補から外れるので、擬似が
  受けてメッシュ系の `tube_ruled` が答える。
  ⚠ **第 2 引数がハッシュなら受けない**（述語スロット）。`tube(path, {closed:1})` は occt の形
  なので、ここで掴むと occt へ降りる道を塞ぐ。
- ★ どの行も `nreq` で**省略形と明示形の両方**を受ける。明示した値が勝ち、省略した分だけ埋まる。

## B 群 — `pm_openvdb`（`dx` が要る） {#openvdb-group}

距離場は**ボクセルサイズ `dx`** が粒度を決める。`seg` を持つ op と持たない op が混ざるので、
行を分けてある。`opts` は `{seg: …, dx: …}`（`dx` の既定は `0.05`）。

**定義している op**:

| op | 引数 | 必須 | 埋めるもの | 本体が呼ぶもの |
|---|---|---|---|---|
| `sphere(r)` | 値 | 1 | `dx` | `mod::sphere(r, dx)` |
| `tetrahedron(a)` | 値 | 1 | `dx` | `mod::tetrahedron(a, dx)` |
| `boxa(a)` | 値 | 1 | `dx` | `mod::boxa(a, dx)` |
| `box(x, y, z)` | 値 / 値 / 値 | 3 | `dx` | `mod::box(x, y, z, dx)` |
| `prism(a, b, c)` | 値 / 値 / 値 | 3 | `dx` | `mod::prism(a, b, c, dx)` |
| `pyramid(a, b, c)` | 値 / 値 / 値 | 3 | `dx` | `mod::pyramid(a, b, c, dx)` |
| `empty3d()` | — | 0 | `dx` | `mod::empty3d(dx)` |
| `icosphere(r[, subdiv])` | 値 / 値 | 1 | **細分回数**（既定 0）＋ `dx` | `mod::icosphere(r, subdiv, dx)` |
| `cylinder(r, h[, seg])` | 値 / 値 / 値 | 2 | `seg` ＋ `dx` | `mod::cylinder(r, h, seg, dx)` |
| `cone(r, h[, seg])` | 値 / 値 / 値 | 2 | `seg` ＋ `dx` | `mod::cone(r, h, seg, dx)` |
| `torus(rr, r[, seg])` | 値 / 値 / 値 | 2 | `seg` ＋ `dx` | `mod::torus(rr, r, seg, dx)` |
| `tube_ruled(path[, seg])` | 値 / 値 | 1 | `seg` ＋ `dx` | `mod::tube_ruled(path, seg, dx)` |
| `tube(path[, seg])` | 値 / **述語** | 1 | `seg` ＋ `dx` ＋ 名前の橋渡し | `mod::tube_ruled(path, seg, dx)` |

⚠ `icosphere` の第 2 引数は **`seg` ではなく細分回数**。同じ「細かさ」でも別の量なので、
`seg` では埋めない。

⚠⚠ **`seg` を持たない op に `seg` を渡す行は置いていない。** `sphere` は距離場を直に書くので
分割数という概念が無く、粒度は `dx` だけが決める。ここで `sphere(r, seg)` を受けて `seg` を
捨てる行を足すと「**書いた分割数が黙って消える**」ので、受けない ＝ 候補から外れる
（隣のカーネルへ降りる）ままにしてある。

## ★★ `pm_points` だけは役割が違う — **引数の少ない形を足す** {#pm-points}

A / B 群が「既定値を練り込む」のに対し、これは**呼び方そのものを足す**。

```
intersection(点群, 形)        →  [境界ちょうど, 内側 (開), 外側]   ★ 3 要素配列 ← 擬似が作る
intersection(点群, 形, mode)  →  1 つ  (mode 0 / -1 / +1)          ← モジュールが持つ
difference  (点群, 形)        →  外側と同じもの                    ← モジュールが持つ
```

**定義している op**（1 本だけ）:

| op | 引数 | 必須 | 何をするか |
|---|---|---|---|
| `intersection(点群, 形)` | **述語** / 幾何 | 2 | 3 引数の呼び分けを **3 回**行い、結果を 3 要素配列にまとめる |

★ 第 1 引数の述語は「**点群か**」（`type_of(v)` が `pt-cloud2d` / `pt-cloud3d`）。
メッシュどうしの `intersection` / `&&&` はここで**外れて**、隣の実カーネルへ降りる。

⚠ **`difference` の行は置いていない。** あちらは元から 2 引数なので、関数形も `---` も
そのまま実モジュールへ通る。擬似が受けて捨てると「隣へ降りる道を塞ぐ」だけになる。

★ **厳密な分割**なので次が常に成り立つ:

```
nverts(s[0]) + nverts(s[1]) + nverts(s[2]) == nverts(点群)
```

★ 境界ちょうどの点を「内側」「外側」のどちらに入れるかは**一意に決まらない**ので、判定器に
答えさせず**第 3 の集合**として切り出す（`section` の共面と同じ形）。詳細は
[関数リファレンス §点群を形で切る](srava_function_reference.html#ptsplit)。

### ⚠⚠ なぜ**パーサの糖衣にしなかったか**（実測して決めた） {#why-not-sugar}

`section(m,P,N)` の 3 要素配列は**パーサの糖衣**で作っている。同じ手が使えなかった:

| | op の引数 | 糖衣の条件 | |
|---|---|---|---|
| `section` | `nin=4` のみ | `na==3` | **競合しない** |
| `intersection` | `nin=2`（メッシュのブール積） | `na==2` | ⚠ **op を覆い隠す** |

パーサは**型を知らない**（routing は eval 時）ので「点群か」を判別できない。

- ⚠ `union` の糖衣を 2 引数へ広げて測ったら、`union(a,b)` が第 2 引数を**黙って捨てて** `a` を
  返した（エラーにならない）。
- ⚠⚠ しかも `a &&& b` は `mk_meshop` に落ちて **`mk_call` を通らない**ので、糖衣だと
  **綴りで意味が割れる**（関数形だけ 3 要素配列になる）。

⇒ **擬似モジュールなら eval 時に値を見られる**ので「点群のときだけ受ける」が書け、しかも
`&&&` にも効く（routing の候補列は演算子経由のノードも通るため）。

```
use [ pm_points({}), "geomutils", "manifold", "points" ];
var s = p &&& m;                     // ← 関数形と同じ 3 要素配列
```

⚠ 内外の判定は **geomutils / openvdb / occt** に分かれている。⇒ 本体は `mod::` で指名せず
**素の 3 引数呼び**にしてあり、**利用者がロードしている実装が答える**。落ち先を決め打っていない。

---

## 書く人向け — 踏みやすい穴 {#pitfalls}

自分で擬似モジュールを書くときの注意。仕組みの詳細は
[言語リファレンス §擬似モジュール](srava_language_reference.html#pseudo-module)。

- ⚠⚠ **本体は `mod::` で指名する。** 指名を省くと候補列の先頭 ＝ その擬似自身へ戻ってきて
  **無限に回る**。
  ★ 例外は `pm_points` のように**引数の数が違う**呼び方をする場合。3 引数は擬似の行
  （`nreq:2`）に **arity で当たらない**ので戻ってこない（実測で確かめてある）。
- ⚠ **受けてはいけないものを受けない。** 擬似が受けた時点で、隣の実カーネルへ降りる道が
  閉じる。「受けて捨てる」行は書かない（書いた値が黙って消える）。
- ⚠ **持たない op の行を足さない。** 擬似が受けてから「そんな op は無い」で落ちる。
- ★ **述語スロット**（`in:[ \(v){ … }, "cache" ]`）で値を見て選べる。パーサにできない
  「点群のときだけ」「ハッシュでないときだけ」はここで書く。
- ★ `nreq` で**省略形と明示形の両方**を受ける。省略された引数は `null` で届くので、そこだけ
  埋めて実モジュールへ素通しする。

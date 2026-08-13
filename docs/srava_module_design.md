# srava モジュール設計ガイド — サードパーティ .so の作り方

srava は本体（planner / agent host）を再ビルドせずに機能を足せる **モジュール機構**を持つ。モジュールは
**ダイナミックリンクの単位**（`.so`）で、`extern "C"` の記述子を 1 個だけ公開する。host 実行体が起動時に
`.so` を dlopen して記述子を読み、そのモジュールが申告した **op**（`box` / `pipe_proximity` / 自作 op …）を
srava プログラムから普通の関数のように呼べるようにする。本書は「**自分のモジュールを書きたい開発者**」向けに、
記述子 ABI・op と型の申告方法・実行方式・モジュール間の型変換・ビルドと配置を、同梱モジュール
（`cgal` / `manifold` / `pipe_proximity` / `demo`）の実物から引いて説明する。host 側のコードは一切改修不要で、
`.so` を探索路に置くだけで新しい op が使える。

---

## 1. モジュールとは何か

- モジュール = **1 個の `.so`**。`modules/<name>/` をビルドして `<name>.so` を生成し、host が dlopen する。
- host 実行体は 2 つある。どちらも同じ `.so` を dlopen する:
  - **planner**（`srava`）: srava プログラムを解釈しプランを組む。in-proc（thread）実行では、planner が
    自プロセス内でモジュールの実行体を起こす。
  - **agent host**（`srava_agent`）: process 実行時に、planner が `srava_agent <so> ...` を fork し、agent
    側が `.so` を dlopen してモジュールの実行体を起こす。**単一の agent host** がすべてのモジュールを担う
    （幾何カーネルごとに別バイナリを持つ旧体制は廃止）。
- `.so` は起動時に自動ロードされる。記述子ファイル（`.plugin` 等の行ベース manifest）や、起動する外部 `bin`
  の指定は無い — **dlopen するだけ**。
- 実行方式（in-proc thread / 別 process）はモジュールが記述子で申告し、srava 側の `module(...)` で上書きできる
  （§5）。どちらの方式でも **同じ記述子・同じ generic 経路**（`pigfModuleAgent`）を通る。
- 1 個の `.so` が **複数の op** を serve してよい（`pipe_proximity.so` は 1 個で 5 op を提供する）。

### 用語（本書で厳密に使い分ける 3 語）

- **幾何カーネル**（geometric kernel）= 幾何コア・幾何表現そのもの（CGAL の EPECK 厳密演算 / Manifold の
  float halfedge）。**モジュールが知っていて、planner は知らない**。
- **型**（type）= mesh の型。**4CC の cache タグと 1:1** で対応する（`cg-mesh3d`↔`MESH` /
  `mf-mesh3d`↔`MFM3` 等）。`cast` は型から型への変換。
- **モジュール**（module）= ダイナミックリンクの単位（`.so`）。planner は op が要求する**型**を見て、その型を
  サポートするモジュールへ**ディスパッチ**する。**モジュール ↔ 幾何カーネルは 1:1 ではない**: 幾何カーネルを
  1 つも持たないモジュール（`pipe_proximity` = 純解析）もある。

---

## 2. モジュール記述子 ABI

`.so` が公開する唯一の C エクスポートは `srava_module()` で、`srava_module_descriptor` へのポインタを返す。
host はこの 1 個の記述子だけを読んでモジュールを配線する。定義は host と `.so` が共有する唯一の公開ヘッダ
`src/h/pig/c++/pigModule.h` にある。

```c++
#include "pig/c++/pigModule.h"

struct srava_module_descriptor {
    int               abi_version;   // = SRAVA_MODULE_ABI。不一致は dlopen を捨ててエラー
    const char*       name;          // "cgal" / "manifold" / "pipe_proximity" …
    int               priority;      // 既定カーネル選択順 (大=優先・同値は後勝ち)。解析モジュールは 0

    // --- 実行体 ---
    pigAgentFactory   make_agent;    // in-proc 実行体 (ptsAgent 派生) 生成子。PROCESS 専用でも 0 不可
    unsigned          exec_caps;     // 実行可能な起動方式の bitmask (EXEC_THREAD | EXEC_PROCESS …)
    int               exec_default;  // 既定の起動方式 (EXEC_THREAD / EXEC_PROCESS)

    // --- I/O 能力 (空/0 可) ---
    const pigOpEntry* ops;    int n_ops;   // 対応 op 表 (§3)
    const char*       import_exts;   // import で読める拡張子 CSV。0 可
    const char*       export_exts;   // export で書ける拡張子 CSV。0 可

    // --- キャッシュ 4CC タグ / codec (§4・幾何モジュールのみ) ---
    const char*           codec_tags; // このモジュールがサポートする（読み書きする）キャッシュ 4CC の CSV。0 可
    const pigModuleCodec* codecs;     // reader/writer factory (name==0 番兵終端の配列)。0 可
};

extern "C" const srava_module_descriptor* srava_module(void);   // .so の唯一の export
```

- **現行 ABI バージョン = `SRAVA_MODULE_ABI`（= 5）**。`abi_version` フィールドに必ず `SRAVA_MODULE_ABI` を
  入れる。host は dlopen 時にこれを検査し、不一致の `.so` は拒否する（構造体レイアウトの取り違えを防ぐため
  version は文字列でなく C++ 構造体で固定）。
- **エントリシンボルは `srava_module`**（`SRAVA_MODULE_SYM`）。ローダは `dlsym(so, "srava_module")` でこの関数を
  引く。慣例として、記述子本体は実行体クラスの `.cpp`（`cgatsAgent.cpp` 等）に置き、`manifest.cpp` はそれを
  `extern` 参照して `srava_module()` から返すだけにする。`manifest.cpp` を `.so` にだけリンクすれば、
  `srava_module` シンボルが実行体本体に現れず衝突しない。

`manifest.cpp` の最小形（同梱モジュール共通）:

```c++
// modules/<name>/manifest.cpp
#include "pig/c++/pigModule.h"
extern const srava_module_descriptor <name>_descriptor;   // 実行体 .cpp が定義
extern "C" const srava_module_descriptor* srava_module(void) { return &<name>_descriptor; }
```

---

## 3. op の申告（`ops` 配列）

各モジュールは自分が受理する op を `pigOpEntry`（`src/h/pig/c++/pigOpEntry.h`）の配列で申告する。planner は
op 名と**引数の型**をこの表と照合してモジュールを選ぶ。1 行は次のフィールドを持つ:

```c++
struct pigOpEntry {
    const char*       op;        // op 名 (キー)
    const pigArgKind* in;        // 入力の受け取り種別リスト (先頭 nin 個)
    int               nin;       // 固定入力数
    pigArgKind        out;       // 出力の受け取り種別
    pigCalcFactory    mkCalc;    // 計算本体 (ptsCalcBody 派生) の生成子
    int               variadic;  // 1 = nin 個の後ろに AK_CACHE(mesh) を可変個
    const char*       sig;       // 幾何型シグネチャ "(in型,...)->out型"。値 op は 0 可
};
```

- **引数種別 `pigArgKind`**: `AK_INLINE`（値リテラル・構造値をそのまま受ける）/ `AK_CACHE`（上流 op の結果を
  指す cache ハンドル = mesh などの重い本体を reader で読む）。`out` も同じ 2 値で、mesh を produce する op は
  `AK_CACHE`、数値・配列など**値**を返す op は `AK_INLINE`。
- **`sig`（幾何型シグネチャ）** = planner の型ディスパッチが使う中核。書式は `"(in1,in2,...)->out"`、複数
  シグネチャは `;` 区切り。列挙するのは**幾何型（mesh）入力のみ**（スカラ/値の inline 引数は型を持たないので省略）。
  出力が値（体積・面積・bool 等）の op は `->value`。planner は `(op, 入力型[])` を直接この表に照合して
  実行モジュールを決めるので、次元・厳密度の分岐に host 側の名指しコードは要らない。

### 例 1: 解析モジュール（値のみ・`pipe_proximity`）

mesh も cache も扱わず値だけをやり取りするモジュールは、`in`/`nin`/`mkCalc`/`sig` を使わず、op 名の申告
だけで足りる（`variadic=1` で任意 arity を許容し、実 arity 検査は計算本体側で行う）:

```c++
static const pigOpEntry PP_OPS[] = {
    { "pipe_proximity",       0, 0, AK_INLINE, 0, 1 },   // out=value (AK_INLINE)
    { "pipe_adjust",          0, 0, AK_INLINE, 0, 1 },
    { "pipe_scene_proximity", 0, 0, AK_INLINE, 0, 1 },
    { "pipe_scene_adjust",    0, 0, AK_INLINE, 0, 1 },
    { "pipe_sample",          0, 0, AK_INLINE, 0, 1 },
};
```

### 例 2: 幾何モジュール（mesh 入出力・型シグネチャ付き・`cgal`）

mesh を入出力する op は、入力の受け取り種別（`AK_CACHE` = 上流 mesh）と `sig`（型シグネチャ）を書く:

```c++
static const ArgKind SHAPE3_IN[]  = { AK_INLINE, AK_INLINE, AK_INLINE };  // box(x,y,z)
static const ArgKind BINMESH_IN[] = { AK_CACHE, AK_CACHE };               // union(a, b)
static const ArgKind MEASURE_IN[] = { AK_CACHE };                         // volume(m)

static const pigOpEntry OPS[] = {
    // leaf 生成: mesh 入力なし → 出力型だけ ("->cg-mesh3d")
    { "box",     SHAPE3_IN,  3, AK_CACHE,  &mkCalcT<cgaBox>,    0, "->cg-mesh3d" },
    // 二項 mesh op: 自型 + 他モジュール型 (mf-mesh3d) の受理も sig に列挙
    { "union",   BINMESH_IN, 2, AK_CACHE,  &mkCalcT<cgaUnion>,  0,
        "(cg-mesh3d,cg-mesh3d)->cg-mesh3d;(cg-mesh3d,mf-mesh3d)->cg-mesh3d;(mf-mesh3d,cg-mesh3d)->cg-mesh3d" },
    // 次元で分岐する op: 2D と 3D を別シグネチャで (host 側の次元例外コードは不要)
    { "offset",  ROTATE_IN,  3, AK_CACHE,  &mkCalcT<cgaOffset>, 0,
        "(cg-cross2d)->cg-cross2d;(cg-mesh3d)->cg-mesh3d" },
    // 値を返す計測 op: out=AK_INLINE / sig の出力型は "value"
    { "volume",  MEASURE_IN, 1, AK_INLINE, &mkCalcT<cgaVolume>, 0, "(cg-mesh3d)->value" },
};
```

- `mkCalcT<T>` は計算本体 `T`（`ptsCalcBody` 派生）を `thNEW` する薄い thunk。op 実行時に planner が生成子を
  呼び、本体が入力を読んで結果を produce する。
- 別モジュールが作った mesh を受理したい場合は、その型を `sig` の入力側に**明示列挙**する（上の
  `(mf-mesh3d,cg-mesh3d)->cg-mesh3d`）。これがクロスモジュール受理の宣言になる（§6）。

---

## 4. 型と 4CC の登録（幾何モジュール向け）

mesh のような**キャッシュ本体**を produce するモジュールは、その型を型レジストリへ登録し、cache への
read/write を codec で提供する。値だけのモジュール（`pipe_proximity` / `demo`）はこの節を丸ごと省略できる。

### 4.1 型 ↔ 4CC の登録

型名（実装型・タグと 1:1）を 4CC cache タグへ結びつける。これは `.so` の manifest 静的初期化で行い、pig 層に
ハードコードしない（型は `.so` が自己申告する）:

```c++
#include "pig/c++/pigTypeRegistry.h"
namespace {
struct MyTypeReg {
    MyTypeReg() {
        pigTypeRegistry::register_type("cg-mesh3d",  "MESH");   // 3D 立体 ↔ 4CC "MESH"
        pigTypeRegistry::register_type("cg-cross2d", "PLY2");   // 2D 多角形 ↔ 4CC "PLY2"
    }
} g_my_type_reg;
}
```

- 型名は **実装型**（`cg-mesh3d` / `mf-mesh3d` … タグと 1:1）を基本にする。
- 記述子の `codec_tags` に**自分がサポートする（読み書きする）** 4CC を CSV で書く（cgal は `"MESH,PLY2"`）。

### 4.2 codec（read/write factory）

`pigModuleCodec` の配列（`name==0` を番兵とする終端）で、cache の reader/writer factory を申告する:

```c++
struct pigModuleCodec {
    const char*       name;      // dedup キー ("cg-mesh")。0 = 配列終端
    const char*       tags;      // 対応 4CC の CSV ("MESH,PLY2")
    const char*       out_types; // tags と位置対応した出力型名 CSV ("cg-mesh3d,cg-cross2d")
    pigCacheMatchFn   match;     // writer 選択 (本文型判定)。読取専用 codec は never (0 返し)
    pigCacheReaderFn  mkReader;
    pigCacheWriterFn  mkWriter;
};

const pigModuleCodec cgal_codecs[] = {
    // 自型: 書ける / 読める
    { "cg-mesh",       "MESH,PLY2", "cg-mesh3d,cg-cross2d", &cg_match,       &cg_mk_reader, &cg_mk_writer },
    // foreign 昇格読み: 他モジュールの 4CC (MFM3/MFC2) を自型へ変換して読む (書きは自型モジュールが担うので writer=0)
    { "cg-mf-upgrade", "MFM3,MFC2", "cg-mesh3d,cg-cross2d", &cg_match_never, &cg_mk_reader, 0             },
    { 0, 0, 0, 0, 0, 0 },
};
```

- `out_types` は `tags` と**位置対応**した出力型名。reader 選択は「file のタグ × 求める出力型」の 2 キーで
  行われる（§6）。
- foreign タグを自型へ**昇格読み**する codec を足せば、他モジュールが作った mesh を自分の型として読める
  （`match` は `never` = 書きは相手の自型モジュールに任せ、読みだけ提供）。

---

## 5. 実行方式（in-proc thread / 別 process）

記述子は起動方式の能力と既定を宣言する:

```c++
enum {
    EXEC_THREAD  = 1 << 0,   // planner 内 thread (in-proc・ptsMediatorInternal)
    EXEC_PROCESS = 1 << 1,   // srava_agent プロセス (ptsMediatorExternal)
    EXEC_REMOTE  = 1 << 2,   // 将来: リモートノード (枠のみ)
};
```

- `exec_caps` = そのモジュールが**動ける方式**の bitmask、`exec_default` = **既定**の方式。
- 起動方式の決定は**後が勝つ**: `exec_default` ← srava 言語の `module(so, {exec_default:...})` 指定。選ばれた
  方式が `exec_caps` に無ければエラー。

| モジュール | exec_caps | exec_default | 理由 |
|---|---|---|---|
| cgal | `EXEC_PROCESS` | PROCESS | CGAL は in-proc thread 不可 |
| manifold | `EXEC_THREAD \| EXEC_PROCESS` | THREAD | in-proc が速い |
| pipe_proximity | `EXEC_THREAD \| EXEC_PROCESS` | THREAD | 解析・thread 化が主目的 |

srava プログラム側からの上書き:

```
load("pipe_proximity.so");                          // ロードのみ (冪等)
module("cgal.so",     { exec_default: "process" });  // 実行方式を上書き
module("manifold.so", { priority: 20 });             // 既定カーネルを切替 (後述)
module("cgal.so", "off");                            // ロードしたまま routing 候補から外す
module("cgal.so", "on");                             // 候補へ戻す (既定は on)
```

- 第 2 引数のハッシュで上書きできるのは **`exec_default`** と **`priority`** の 2 項目のみ（`make_agent` /
  `ops` / `codecs` 等は `.so` の実体そのものなので上書き不可）。
- `priority` は **既定カーネル**（leaf 生成 op がどのモジュールへ落ちるか）の選択順。`module(so,{priority:N})`
  は so の priority を N にし、同時に so を「今ロードした」扱いにする（priority 最大・同値は後勝ち）。
- 第 2 引数の文字列 `"off"` / `"on"` = 実行時の有効/無効切替。`"off"` はモジュールを**ロードしたまま** routing
  候補から外す（codec は生きたままなので、そのモジュールが作った mesh の読み書きは引き続き可能）。

### 5.1 引数の読み取り — 配列/ハッシュは `obt_array()` / `obt_hash()` で取る {#obt}

op の実装で「この引数は配列か？ハッシュか？」を判定するとき、**`sPtr<pigDataArray>::d_cast(v)` を
使ってはいけない**。素の RTTI キャストなので、`v` が**遅延ノード**（`pigDataDelay`）だと中身が配列でも
`null` になる。代わりに pigData のゲートウェイを使う:

```c++
sPtr<pigDataArray> a = v->obt_array();   // 配列なら本体、違えば thNULL
sPtr<pigDataHash>  h = v->obt_hash();    // ハッシュなら本体、違えば thNULL
```

`pigDataDelay::obt_array()` が `compact()` に委譲するので、**呼び側が compact を忘れられない**
（`get_int` / `get_flt` / `is_error` と同じ作法）。

なぜ効くか: **配列とハッシュは要素を eager 解決しない**（`pigData.h` の設計注記）。`map` や lambda で
作られた配列の要素は**遅延ノードのまま**入っている。process 実行では inline 引数がテキストに直列化され
`pig_value_parse` で素の値に再構築されるので `d_cast` でも通ってしまうが、**in-proc 実行では遅延ノードが
そのまま届く**。つまり `d_cast` を使うと「in-proc でだけ、`map` 由来の入力でだけ、嘘のエラーが出る」
という見つけにくい壊れ方をする（2026-08-13 に `tube` / `polygon` / `pipe_scene_*` で実際に起きた）。

`v` が `thNULL` になり得る場所（省略可能な引数・`hash->get_ix` の空返し）では、呼ぶ前に
`v.is_notNull()` を確かめること（ゲートウェイはメンバ関数なので null では呼べない）。

---

## 6. モジュール間の型変換（cross-module conversion）

あるモジュールの op が**別モジュールが作った mesh** を消費する場面（`cgal.union(box, manifold_box)` 等）の
仕組み。要点は「**入力は所属モジュールを持たず型を持つ**。その型を受理する op を持つモジュールを見つけるのが
routing」という型軸ディスパッチ。

- **受理の宣言**は §3 の `sig` に foreign 入力型を明示列挙する（例 `(mf-mesh3d)->cg-mesh3d`）。planner の
  `decide_executor(op, 入力型[])` がこれを見て消費側モジュールへ振る。
- **実際の変換**は cache の reader が担う。消費側の実行体は入力 cache から `get_body("<自分の型名>")` で
  「欲しい型」を宣言する。cache は:
  - その型の本体が in-memory に在れば即返し（**同型 in-proc の fast path**・変換ゼロ）、
  - 無ければ file のヘッダ 4CC と目標型で `reader_for(file_tag, target_type)` を引き、変換 reader を回して
    目標型の本体を作り、型別リストに載せる（**per-type single-flight** = 同一 mesh を複数 op が消費しても
    変換は 1 回）。
- したがって foreign mesh を in-proc 消費したいモジュールは、その foreign 4CC → 自型の**昇格読み codec**
  （§4.2 の `cg-mf-upgrade` 型）を 1 本用意し、消費 op の `sig` に foreign 入力型を書けばよい。file は生産者
  形式のみで書かれ、派生型は on-demand 再生成される。

（in-proc の生産側は set_body と同時に実 cache file も書くので、消費側の変換 reader は in-proc でも file を
読める。詳細は `cross_module_conversion_design.md`。）

---

## 7. ビルドと配置

### 7.1 CMake — モジュールを `.so` としてビルド

モジュールは `MODULE` ライブラリとしてビルドし、`PREFIX ""` で `lib<name>.so` でなく `<name>.so` を出力する。
CGAL/Manifold のような重い依存はモジュール内に静的リンクし、pig/pts/tinyState は host が `-rdynamic` で供給
するので `.so` には bundle しない（undefined のまま dlopen 時に解決）。

値のみモジュール（`demo`）の最小例:

```cmake
add_library(demo_module MODULE
  modules/demo/dematsAgent.cpp
  modules/demo/demo_compute.cpp
  modules/demo/manifest.cpp)
set_target_properties(demo_module PROPERTIES PREFIX "" OUTPUT_NAME "demo")   # → demo.so
add_dependencies(srava       demo_module)   # planner が起動時に dlopen (op 受理・型登録)
add_dependencies(srava_agent demo_module)   # agent host が process 実行時に dlopen (make_agent)
install(TARGETS demo_module LIBRARY DESTINATION lib/srava/modules)
```

mesh を出力するモジュール（`d3`）は、これに codec / wire-cache stream の reader/writer とメッシュ本体クラスを
足すだけで、host 無改修で mesh の cache 往復が成立する。

### 7.2 `.so` の探索路（自動ロード）

host は起動時に次を順に走査し、見つかった `*.so` をすべて dlopen する。**後にロードしたものが勝つ**（後勝ち）:

1. **実行体と同じディレクトリ**（ビルドツリーでは `cgal.so` 等が planner と同居）
2. `/usr/local/lib/srava/modules`（install 既定 = `SRAVA_MODULE_SYSDIR`・CMake で上書き可）
3. `~/.config/srava/modules`
4. **`$SRAVA_MODULE_PATH`**（`:` 区切り・最後にロード = **最優先**）

実装は `src/classes/pig/c++/pigModuleLoader.cpp`。開発中は `SRAVA_MODULE_PATH` で自作 `.so` のディレクトリを
指すのが手軽:

```sh
SRAVA_MODULE_PATH="/path/to/my/modules" srava my.sra
```

---

## 8. 最小のモジュール雛形（擬似コード）

値のみの解析モジュールを 3 ファイルで作る例（`modules/hello/`）。

```c++
// hello_compute.h — 計算本体 (host 非依存・pigData のみ)
#include "pig/c++/pigData.h"
#include "ts2/c++/sArray.h"
sPtr<pigData> hello_compute(const char *op, sArray<sPtr<pigData> >& args);

// hello_compute.cpp
sPtr<pigData> hello_compute(const char *op, sArray<sPtr<pigData> >& args) {
    if (!strcmp(op, "hello_add")) {
        // args から値を取り出し、pigData の値 (数/配列/文字列/ハッシュ) を返す
        return /* pigDataInt(a + b) 相当 */;
    }
    return /* pigDataError("unknown op") 相当 */;
}
```

```c++
// helatsAgent.cpp — 実行体 (ptsAgent 派生) + 記述子
#include "pig/c++/pigOpEntry.h"
#include "pig/c++/pigModule.h"
// ... CLASS_TINYSTATE(hello/c++/helatsAgent, pig/c++/ptsAgent) で状態機械を継承し、
//     STARTCALC で hello_compute(op, argv) を呼んで結果を outCache へ set する ...

static const pigOpEntry HELLO_OPS[] = {
    { "hello_add",   0, 0, AK_INLINE, 0, 1 },   // 値 op: in/sig 省略・variadic
    { "hello_greet", 0, 0, AK_INLINE, 0, 1 },
};
static const int HELLO_N_OPS = (int)(sizeof(HELLO_OPS)/sizeof(HELLO_OPS[0]));

static sPtr<ptsAgent> mk_helatsAgent(sPtr<ptsObject> med) { return thNEW(helatsAgent,(med)); }
static const int helatsAgent_registered =
    (pigAgentRegistry::register_agent("hello", &mk_helatsAgent), 0);

extern const srava_module_descriptor helatsAgent_descriptor;
extern const srava_module_descriptor helatsAgent_descriptor = {
    SRAVA_MODULE_ABI, "hello", 0,                       // ABI / name / priority
    &mk_helatsAgent, (unsigned)(EXEC_THREAD|EXEC_PROCESS), EXEC_THREAD,
    HELLO_OPS, HELLO_N_OPS, 0, 0,                        // ops / import_exts / export_exts
    0,                                                  // codec_tags (値のみ)
    0,                                                  // codecs   (値のみ)
};
static const int helatsAgent_desc_registered =
    (pigModuleRegistry::register_descriptor(&helatsAgent_descriptor), 0);
```

```c++
// manifest.cpp — 唯一の C エクスポート
#include "pig/c++/pigModule.h"
extern const srava_module_descriptor helatsAgent_descriptor;
extern "C" const srava_module_descriptor* srava_module(void) { return &helatsAgent_descriptor; }
```

CMake で `hello.so` を作って探索路に置けば、srava プログラムから `hello_add(2, 3)` がそのまま呼べる（host
無改修）。mesh を扱うモジュールにするには、§4 の型登録と codec、mesh 本体クラス、`ops` の `sig`、
wire-cache stream の reader/writer を足す（`modules/d3/` が最小の mesh モジュール実例）。

---

## 9. 関連ドキュメント

- [srava モジュールリファレンス](srava_module_reference.html) — 同梱モジュール（pipe_proximity 等）の
  インストール・op 仕様・利用側の手順。
- [srava 言語リファレンス §10 幾何カーネル — 型ディスパッチと型変換](srava_language_reference.html#kernel) —
  幾何カーネル / 型 / モジュールの語法、`load` / `module` の言語仕様、型ディスパッチと `cast`。
- [srava 関数リファレンス](srava_function_reference.html) — 組込 op と各モジュール op の一覧・引数仕様。
- `docs/cross_module_conversion_design.md` — モジュール間の型変換（`get_body(型名)` / `reader_for` /
  per-type single-flight）の詳細設計。

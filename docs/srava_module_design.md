# srava モジュール設計ガイド — サードパーティ .so の作り方

srava は本体（planner / agent host）を再ビルドせずに機能を足せる **モジュール機構**を持つ。モジュールは
**ダイナミックリンクの単位**（`.so`）で、`SRAVA_MODULE_EXPORT` を付けた記述子を 1 個だけ公開する。host 実行体が起動時に
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
    側が `.so` を dlopen してモジュールの実行体を起こす。**単一の agent host** がすべてのモジュールを担う。
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
    int               priority;      // 既定カーネル選択順 (大=優先・★同値の勝敗は不定)

    // --- 実行体 ---
    pigAgentFactory   make_agent;    // in-proc 実行体 (ptsAgent 派生) 生成子。PROCESS 専用でも 0 不可
    unsigned          exec_caps;     // 実行可能な起動方式の bitmask (EXEC_THREAD | EXEC_PROCESS …)
    int               exec_default;  // 既定の起動方式 (EXEC_THREAD / EXEC_PROCESS)

    // --- I/O 能力 (空/0 可) ---
    const pigOpEntry* ops;    int n_ops;   // 対応 op 表 (§3)
    const char*       import_exts;   // import で読める拡張子 CSV。0 可
    const char*       export_exts;   // export で書ける拡張子 CSV。0 可

    // --- 型と本体クラス (§4・幾何モジュールのみ) ---
    const pigModuleType*       provides; // 階層 × 型名 × 4CC (wire==0 番兵終端)。0 可

    // --- キャッシュキーソルト (#3427) ---
    const char*       hash_salt;     // 出力キャッシュのキーに混ぜる弁別バイト列 (manifold="\x01MFM")。
                                     // 基準カーネル cgal は 0 = ソルト無し (既存キーを byte 不変に保つ)

    // --- 項数ポリシー (v9・#3436 P4・§5.2) ---
    int               arity;         // N' = 1 ノードあたり受け取りたい**最大**項数。0 = 未指定 = 2 (二項)。
                                     // 実項数 k = min(N', op の sig が申告する N, 群の執行者が許す最大)。
                                     // module(so,{arity:k}) で上書き可。capability (op ごと・正しさ) と
                                     // policy (モジュールごと・つまみ) の分離がこの設計の核

    // --- フック (0 可) ---
    void (*initialize)(void);        // §7。**そのモジュールの最初の agent が起きるときに 1 回だけ**。
                                     // TBB の global_control のような「プロセスに 1 度だけ」を置く。
                                     // 呼ぶのは ptsMediator で TS_STATE 内なので排他は不要
    void (*configure)(sPtr<pigData> opts);  // v10・#3441。module(so,{opts}) のハッシュ全体を受け取る。
                                     // initialize と違い **opts が設定/更新されるたび**呼ばれる →
                                     // ★冪等に実装すること。opts は thNULL のことがある
                                     // (module(so,"on"/"off") 等)。稼働中 agent への再配線はしない
};

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void);   // .so の唯一の export
```

- **現行 ABI バージョン = `SRAVA_MODULE_ABI`（= 5）**。`abi_version` フィールドに必ず `SRAVA_MODULE_ABI` を
  入れる。host は dlopen 時にこれを検査し、不一致の `.so` は拒否する（構造体レイアウトの取り違えを防ぐため
  version は文字列でなく C++ 構造体で固定）。
- **エントリシンボルは `srava_module`**（`SRAVA_MODULE_SYM`）。ローダは `dlsym(so, "srava_module")` でこの関数を
  引く。慣例として、記述子本体は実行体クラスの `.cpp`（`cgatsAgent.cpp` 等）に置き、`manifest.cpp` はそれを
  `extern` 参照して `srava_module()` から返すだけにする。`manifest.cpp` を `.so` にだけリンクすれば、
  `srava_module` シンボルが実行体本体に現れず衝突しない。
- ⚠⚠ **必ず `SRAVA_MODULE_EXPORT` を使う**（`extern "C"` だけでは Windows で壊れる）。`pigModule.h` が
  OS ごとに定義しており、Windows/Cygwin では `__declspec(dllexport)`、それ以外では
  `visibility("default")` に展開される。
  **なぜ必要か**: PE はエクスポート表に載ったものだけが `GetProcAddress` で引ける。MinGW の `ld` は
  救済として `--export-all-symbols` を暗黙に効かせるが、これは **DLL 内に明示的な `dllexport` が
  1 つも無いとき限定**の挙動である。⇒ OCCT のように `dllexport` を撒くライブラリをリンクした瞬間、
  自動エクスポートが切れて `srava_module` だけが表から漏れ、`GetProcAddress failed: srava_module`
  で dlopen に失敗する（2026-08-27 に occt.dll で実際に踏んだ。2213 個エクスポートしているのに
  `srava_module` だけが無い、という形で発覚）。**依存の変化で遠隔から壊れる**ので、属性は常に明示する。

`manifest.cpp` の最小形（同梱モジュール共通）:

```c++
// modules/<name>/manifest.cpp
#include "pig/c++/pigModule.h"
extern const srava_module_descriptor <name>_descriptor;   // 実行体 .cpp が定義
SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void) { return &<name>_descriptor; }
```

⚠ `extern "C"` を直接書かないこと（上記の理由で Windows のみ壊れる）。`SRAVA_MODULE_EXPORT` が
`extern "C"` を含んでいる。

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
    int               variadic;  // 1 = nin 個の後ろに引数を可変個 (種別は vtail_value)
    const char*       sig;       // 幾何型シグネチャ。値 op は 0 可 (書式は下の §3.1)
    int               commutative;  // 1 = 可換 (union/intersection/combine)。木の形とキー正規化が見る
    int               vtail_value;  // 可変部の種別: 0 = 幾何 (既定) / 1 = 値
};
```

⚠ **`OPS[]` は位置指定の初期化子**なので、フィールドは必ず**末尾に足す**こと。途中に挿げると
`int`/`0` 互換で静かにずれ、コンパイラも ABI 版番号も止めない（occt の記述子で実際に踏んだ）。

- **引数種別 `pigArgKind`**: `AK_INLINE`（値リテラル・構造値をそのまま受ける）/ `AK_CACHE`（上流 op の結果を
  指す cache ハンドル = mesh などの重い本体を reader で読む）。`out` も同じ 2 値で、mesh を produce する op は
  `AK_CACHE`、数値・配列など**値**を返す op は `AK_INLINE`。
- **`sig`（幾何型シグネチャ）** = planner の型ディスパッチが使う中核。複数シグネチャは `;` 区切り。
  列挙するのは**幾何型（mesh）入力のみ**（スカラ/値の inline 引数は型を持たないので省略）。
  出力が値（体積・面積・bool 等）の op は `->value`。planner は `(op, 入力型[])` を直接この表に照合して
  実行モジュールを決めるので、次元・厳密度の分岐に host 側の名指しコードは要らない。

### 3.1 sig の 3 つの形

| 形 | 書き方 | 意味 |
|---|---|---|
| 固定形 | `"(a,b)->c"` | 位置と個数が確定 |
| 繰り返し形 | `"({a,b,c}...)->ref"` | 末尾スロットが 1 個以上。**分解も昇格もしない** |
| fold 形 | `"[a,b,c](N)->a"` | 2〜N 項。先頭 `a` = **主型**。木に分解してよい |

- **旧記法 `"T..."` は `"{T}..."` の糖衣**なので、既存の sig はそのまま有効。
- 可変部が単独なら外側の `( )` を省ける（`"[a,b](8)->a"`）。固定部を前置するときだけ括弧が要る
  （`"(x,[a,b](8))->a"`）。⚠ **固定部を前置した op は木に分解しない**（固定引数を各群へ複製すると
  意味が変わるため）。値引数も固定部に数える。
- fold 形の **`N` は性能のつまみではなく正しさの上限**。`*` は「このカーネルに上限が無い」の意
  （manifold の `BatchBoolean` / OCCT の `BOPAlgo_Builder`）。geogram の 32 のように**超えると黙って
  壊れる**値があるので、実装が二項しか受けられないあいだは `(2)` と書く。
- fold 形は「**引数のどれか 1 個が主型**」を要求する。これが「cgal は `(mf,mf)` の行を*書かない*」という
  運用（disjoint 原則）を規則にしたもので、`union(mf,mf)` は cgal にマッチせず manifold が取る。
- **`{…}…` と `[…](N)` は統合できない**。似ているのは構文だけで、分解の可否・主型の有無・昇格を
  宣言するか・出力が集合の中かが全部違う。

### 3.2 何項で投げるか — capability と policy

- op の `sig` の `N` = **capability**（このモジュールが正しく処理できる上限）
- 記述子の **`arity`** = **policy**（1 ノードあたり受け取りたい最大項数・`0` = 未指定 = `2`）

実際の項数は `k = min(N', N, 群の執行者が許す最大)`。srava スクリプト側から
`module("geogram.so", {arity: 8})` で `N'` を上書きできる（2 以上の有限整数）。
★ 既定が 2 なので、fold 形へ書き換えただけでは挙動は変わらない。
★ capability が policy を頭打ちにするので、`sig` に `(2)` と書いてある cgal は `arity: 8` を
指定しても二項の木のままになる。

### 例 1: 解析モジュール（値のみ・`pipe_proximity`）

mesh も cache も扱わず値だけをやり取りするモジュールは、`in`/`nin`/`mkCalc`/`sig` を使わず、op 名の申告
だけで足りる（`variadic=1` で任意 arity を許容し、実 arity 検査は計算本体側で行う）。
⚠ **可変部が値なら `vtail_value=1` を書く** — 既定の 0 は「可変部は幾何」の意味なので、
書かないと planner の引数種別検査が正しい呼び出しを弾く:

```c++
static const pigOpEntry PP_OPS[] = {                    //              sig  comm vtail
    { "pipe_proximity",       0, 0, AK_INLINE, 0, 1, "->value", 0, 1 },   // 可変部は値
    { "pipe_adjust",          0, 0, AK_INLINE, 0, 1, "->value", 0, 1 },
    { "pipe_scene_proximity", 0, 0, AK_INLINE, 0, 1, "->value", 0, 1 },
    { "pipe_scene_adjust",    0, 0, AK_INLINE, 0, 1, "->value", 0, 1 },
    { "pipe_sample",          0, 0, AK_INLINE, 0, 1, "->value", 0, 1 },
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
    // fold 形: 自型 (= 主型) + 昇格して読める他モジュール型を 1 つの集合に。末尾の 1 = 可換
    // (2) = この実装は二項まで。n 項で受けられるカーネルは (32) や (*) と書き variadic=1 にする
    { "union",   BINMESH_IN, 2, AK_CACHE,  &mkCalcT<cgaUnion>,  0,
        "[cg-mesh3d,mf-mesh3d,gg-mesh3d](2)->cg-mesh3d;[cg-cross2d,mf-cross2d](2)->cg-cross2d", 1 },
    // 次元で分岐する op: 2D と 3D を別シグネチャで (host 側の次元例外コードは不要)
    { "offset",  ROTATE_IN,  3, AK_CACHE,  &mkCalcT<cgaOffset>, 0,
        "(cg-cross2d)->cg-cross2d;(cg-mesh3d)->cg-mesh3d" },
    // 値を返す計測 op: out=AK_INLINE / sig の出力型は "value"
    { "volume",  MEASURE_IN, 1, AK_INLINE, &mkCalcT<cgaVolume>, 0, "(cg-mesh3d)->value" },
};
```

- `mkCalcT<T>` は計算本体 `T`（`ptsCalcBody` 派生）を `thNEW` する薄い thunk。op 実行時に planner が生成子を
  呼び、本体が入力を読んで結果を produce する。
- 別モジュールが作った mesh を受理したい場合は、その型を `sig` の入力側に**明示列挙**する（fold 形なら
  主型と同じ bracket に並べる）。これがクロスモジュール受理の宣言になる（§6）。
  ⚠ bracket に書いてよいのは「主型と、主型へ昇格して読める型」だけ。ついでに foreign 型を足すと
  複数モジュールがマッチして**決着の根拠が「列挙の有無」から priority へ静かに移る**。

### 3.3 ロード時に弾かれる申告

記述子は dlopen 時に自己矛盾を検査され、引っかかるとそのモジュールは**ロードされない**（fail-fast）:

- `sig` が文法違反（`[a,b](1)` のように `N < 2`・`(N)` の欠落・可変部が末尾でない 等）
- fold 形の型集合に同じ型が 2 度出る
- fold 形の出力が主型と違う（畳む op でないなら繰り返し形 `{…}…` を使う）
- `sig` に可変部があるのに `variadic=0`（planner は n 項を振るが agent が受け取れない）
- `sig` に幾何の可変部があるのに `vtail_value=1`

⚠ 逆（`variadic=1` なのに sig に可変部が無い）は**正常**。`sig` は幾何引数への射影でしかないので、
値引数が可変長の op はこの形になる。

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

- 型名は **実装型**（`cg-mesh3d` / `mf-mesh3d` …）を基本にする。

### 4.2 provides（階層 × 型名 × 4CC）

記述子は `pigModuleType` の配列（`wire==0` を番兵とする終端）を 1 本だけ持つ。
**1 行 = 「この本体クラス階層について、このモジュールはこの型名を名乗り、この 4CC を扱う」**:

```c++
struct pigModuleType {
    const pigWireClass* wire;    // 本体クラス階層 (Cls::WIRE)。0 = 配列終端
    const char*         types;   // この階層について名乗る型名の CSV
    const char*         tags;    // 扱う 4CC の CSV。★診断専用
};

const pigModuleType occt_mf_provides[] = {
    { &ocGeom::WIRE, "oc-brep3d", "BREP" },
    { &mfGeom::WIRE, "mf-mesh3d", "MFM3" },
    { 0, 0, 0 },
};
```

- 行の識別は **`wire`（クラスそのもの）** が担う。自由文字列の名前は持たない（ABI v16 で撤去 —
  型名と紛らわしいだけで、番兵と表示ラベルにしか使われていなかった）。
- 型名が**階層ではなくモジュールに属する**のは必要な非対称。`mfGeom` について `manifold` は
  `"mf-mesh3d,mf-cross2d"` を名乗り、`occt_mf` は `"mf-mesh3d"` だけを名乗る。
- ⚠ **`types` と `tags` は位置対応しない。** 独立した 2 本のリストで、個数も一致しない。
  cgal は `types` が 2 個（`cg-mesh3d,cg-cross2d`）に対し `tags` は 5 個（`MESH,PLY2,MFM3,MFC2,NEFB`）で、
  実際の対応は多対少（`MESH`/`MFM3`/`NEFB` → `cg-mesh3d`、`PLY2`/`MFC2` → `cg-cross2d`）。
  **どのタグがどの型になるかは申告しない** — 答えるのは `wire->create` に通して返ってきた具象の
  `type_name()` だけで、`srava --module-info` が出しているのはその実測結果。
  ★ ABI v10〜v13 の `tags`/`types` は**位置対応だった**ので、当時を知っているほど誤読しやすい。
- ⚠ **`tags` は診断専用。実行時の判断に使ってはならない。** 読めるかを答えるのは
  `wire->create`（`create_for_meta`）一本で、`tags` はその答えを**列挙するための候補**にすぎない。
  ABI v10〜v13 の `tags` は reader 選択を駆動しており、申告と実装がずれると**黙って**誤った
  reader が選ばれた（v14 で撤去した理由）。今回は判断経路から切り離したうえで戻している。
- `srava --module-info` が各 `tags` を `wire->create` に通して**実際に受理するか確かめる**ので、
  ずれは黙って残らず `(NOT accepted ...)` として表に出る。
  （この検証機構は導入直後に組込 `pig` モジュールの 1 件を実際に検出した。）
- **他モジュールが作った値を自型として読めるようにするのに、行を足す必要は無い。**
  `create_for_meta` にその 4CC を受理させ、`tags` に足せば診断にも出る。

### 4.3 wires（本体クラス階層）

`create_for_meta` / stream reader / stream writer / `d_cast` 述語は、すべて **本体クラス階層**に
帰属する。各階層の根が `static const pigWireClass WIRE` を持ち、記述子はそれを並べる:

```c++
// cgMesh.h
class cgMesh : public pigDataWireTyped {
    static sPtr<cgMesh> create_for_meta(const uint8_t *meta, int len);
    static const pigWireClass WIRE;
};
// cgCacheCodec.cpp
const pigWireClass cgMesh::WIRE = { &pig_wire_factory<cgMesh>, &cg_mk_reader, &cg_mk_writer,
                                    &pig_wire_match<cgMesh> };
const pigWireClass* const cgal_wires[] = { &cgMesh::WIRE, 0 };
```

- 自前のクラスを作らず他モジュールのクラスを使うモジュール（`occt_mf` / `openvdb_mf` 等）は、
  **借りている側のクラスをそのまま並べる**。新しいクラスも wire 形式も作らない。
- op の引数がどのクラスとして実体化されるかは、OPS 行の `OPWIRE(Calc, In...)` が宣言する（§3）。

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
module("cgal.so",     { exec_default: "process" });  // 実行方式を上書き
module("manifold.so", { priority: 99 });              // 既定カーネルを切替 (後述)
module("geogram.so",  { arity: 8 });                  // N 項評価の上限 (#3436 P4・後述)
module("geogram.so",  { threads: 4 });                // モジュール固有の設定 (下記 5.2)
module("cgal.so", "off");                            // ★ アンロード (dlclose)。以後 module() で読み直せる
module_loaded("cgal.so");                            // いま載っているか (1/0)。★ロードはしない
module("cgal.so");                                   // 未ロードなら読み込む (第 2 引数省略の糖衣)
```

- **`module` はロードとは別物**。ロードは起動時の探索路走査で済んでおり、`module` が行うのは
  **記述子の内容の上書き**だけ（`exec_default` / `priority` / `arity` / 有効・無効 / 任意 opts）。
  したがって **`module` は「ロード順」を変えない**。
- ただし指定されたモジュールが **まだロードされていない場合は新たに読み込む**。探索路の外に置いた
  `.so` をスクリプトから明示的に持ち込めるのはこの経路（これは正真正銘のロードなので順序も進む）。
- 第 2 引数のハッシュで**個別に上書き**できるのは **`exec_default`** / **`priority`** /
  **`arity`**（§3.2）の 3 項目（`make_agent` / `ops` / `codecs` 等は `.so` の実体そのものなので
  上書き不可）。★ これら以外のキーも**ハッシュ全体としてはモジュールに渡せる**（§5.2 の `configure`）。
- 第 2 引数を省いた `module(so)` は「**未ロードなら読み込む**」。
- `priority` は **既定カーネル**（leaf 生成 op がどのモジュールへ落ちるか）の選択順で、**大きいほど優先**。
  ★**同点になった場合の勝敗は不定**（ロード順で決まり、ロード順はディレクトリの走査順に依存するため）。
  確実に切り替えたいなら、同点ではなく**既存の最大値より大きい値**を指定する。
  同梱モジュールの既定 priority は互いに重複しないようにしてある（同時に使わない `nef_snc` /
  `nef_hybrid` だけが同値）:
  `cgal` 20 > `manifold` 10 > `nef_snc` = `nef_hybrid` 5 > `pipe_proximity` 4 >
  `demo` -1 > `d3` -2 > `d2` -3 > `d4` -4 > `d5` -5（負値はテスト専用モジュール）。
- 第 2 引数の文字列は **`"off"` だけ**で、意味は **アンロード（`dlclose`）**（2026-08-28・ひさ設計）。
  以前は有効フラグを倒すだけで `.so` はプロセスに載ったままだったが、明示的にモジュールを落としたり
  入れ替えたりできるように本当に落とす。以後 `module(so, {...})` で読み直せる。
  - **一度でも使われたモジュールは落とせない**。`.so` の中身を指すオブジェクト（メッシュ本体・agent）が
    生きている可能性があるため、`"cannot unload ...: already used by this program"` の明示エラーになる。
    差し替えはそのモジュールで op を実行する**前**に行う。
  - **未ロードへの `"off"` も明示エラー**（名前を間違えたときに気づけるように）。載っているかは
    `module_loaded(so)` で判定できる（この op は**ロードという副作用を持たない**）。
  - 旧 `"on"` は**撤去**した。`"off"` がフラグでなく実アンロードになった以上、「戻す」は再ロードであり
    `module(so, {...})` そのものだから。
  - 落としてから読み直す定型は `lib/module/reload.sra` の `module_reload(path, opts)` にある。

### 5.2 モジュール固有の設定 — `configure` フック (#3441)

`exec_default` / `priority` / `arity` は srava 側が意味を知っている汎用の上書きだが、
モジュールが**自分だけの設定項目**を持ちたいことがある（例: geogram の内部スレッド数）。
記述子の `configure` フィールドがそのための入口:

```c++
void            (*configure)(sPtr<pigData> opts);   /* 0 可 (使わない) */
```

```
module("geogram.so", { priority: 99, threads: 4 });   // threads は geogram だけが知るキー
```

- `module(so,{opts})` は `exec_default`/`priority`/`arity` を読んだあと、**ハッシュ全体**を
  レジストリに保持し、そのモジュールの `configure(opts)` を呼ぶ。モジュール側は自分が使う
  キー（例 `"threads"`）だけを `opts->get_ix(...)` で読み、他は無視してよい。
- **`initialize`（§2 ABI）との違い**: `initialize` は「そのモジュールの最初の agent が起きる
  ときに 1 回だけ」。`configure` は **`module()` が呼ばれるたびに**（同じ実行で複数回呼ばれれば
  複数回）呼ばれる ⇒ **冪等に実装すること**。
- **in-proc（thread）と process の両方に届く**。in-proc は planner と同一プロセス・同一
  descriptor なので `module()` 実行時に直接 `configure` が呼ばれて完結する。process は
  **agent 起動直後に 1 回だけ**、旧 `C_ENV`（pigwire レコード種別 10）で opts のハッシュを
  丸ごと（`serialize()` の srava 文法テキスト）送り、agent 側が同じ `configure` を呼ぶ。
  ⚠ **稼働中の agent への再配線はしない** — 次にその agent が (再) 起動されたときに反映される。
- geogram の実装例（`modules/geogram/c++/ggMesh.cpp`）:
  ```c++
  static int g_geoInit = 0, g_maxThreads = 0;   // 0 = 未設定 = 既定 (nproc)
  void ggMesh::configure(sPtr<pigData> opts) {
      if (opts.is_notNull()) {
          sPtr<pigData> t = opts->get_ix(thNEW(pigDataString,("threads")));
          if (t.is_notNull() && !t->is_error() && (int)t->get_int() > 0)
              g_maxThreads = (int)t->get_int();
      }
      if (g_maxThreads > 0 && g_geoInit)
          GEO::Process::set_max_threads((GEO::index_t)g_maxThreads);   // 未初期化ならまだ (ensure_init が拾う)
  }
  ```
  ★ 「初期化は 1 回」(`g_geoInit`) と「スレッド数の適用」を分けてあるので、`configure` が
  何度呼ばれても最新値がそのつど反映される。⚠ **既定は変えない** — `threads` を指定しなければ
  geogram は従来どおり `GEO::initialize()` 内で nproc をそのまま使う（**絞れば速くなるとは
  限らない**ので、絞ることを既定にはしない。絞りたい人が明示するための口という位置づけ）。
  - `threads:N` (N>0) は上限・`threads:0`（または負値・未指定）は**既定へ明示的に戻す**
    (`GEO::Process::maximum_concurrent_threads()` へ)。一度絞った後、同一プロセス内で
    `module()` を再度呼んで緩める/戻す、という使い方ができる (2026-08-26 追記・bench 指摘)。

★★ **2 モジュール目 (openvdb) の実装で踏んだ罠 — `global_control` から `task_arena` への
移行はつまみの守備範囲を狭める** (2026-08-26・bench 報告):

```
tbb::global_control   プロセス全体に効く → 何もしなくても全 TBB 呼び出しを覆う
tbb::task_arena       包んだ範囲だけ効く → 包み忘れた所は無制限のまま
```

openvdb は最初 `vdGrid.cpp` のブール演算と `volume()` だけを `task_arena` で包み、
「`threads:1` を指定しても効いていない」と誤診した。実際は重い op が **4 モジュール
11 ファイル 26 箇所**に散っていて、包み忘れた所が無制限のまま残っていた。最終的に「`ptsCalcBody` 派生クラスの `compute()` 単位で
一律に包む」形に直し、「全 `compute()` が包まれているか」を grep で機械検査できるようにした。

★ **教訓**: `configure()` でモジュールに opt-in の設定を渡す機構自体は正しく動いても、
**モジュール側が「その設定を実際にどこで消費するか」を網羅できていなければ効かない**。
`global_control` のような「プロセス全体に暗黙で効く」道具から、`task_arena` のような
「明示的に包んだ範囲だけに効く」道具へ移すときは特に、**呼び出し箇所の網羅性**を
(理想的には機械的に) 確認すること。geogram の `GEO::Process::set_max_threads` は
geogram 側のグローバル設定なのでこの問題は起きないが、将来 in-proc 対応する場合は
「プロセス全体 vs op ごと」の同じ論点に直面する見込み。

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

### 5.3 モジュールに **static を置かない** — 状態は registry に預ける (2026-08-26)

★★ **モジュールの file-scope な可変 static は禁止**（ひさ指示）。in-proc 実行では 1 プロセスに
**同じモジュールの op が複数同居しうる**ので、モジュール大域に状態を溜めると op どうしで混線する。
「そのモジュールにひとつ」で正しい状態は、**registry のモジュール専用スロット**に預ける:

```cpp
#include "pig/c++/pigModuleRegistry.h"

class myModuleData : public stdObject { public: int maxThreads = 0; };

static sPtr<myModuleData> my_data()
{
    sPtr<pigModuleRegistry> reg = pig_current_registry();   // 呼び出し文脈から辿る (TLS)
    if ( reg == thNULL ) return sPtr<myModuleData>();       // 文脈が無い = 設定なし扱い
    int id = reg->id_of_name("mymodule");                   // 自分の名前で引く
    if ( id < 0 ) return sPtr<myModuleData>();
    sPtr<myModuleData> d = sPtr<myModuleData>::d_cast(reg->module_data(id));
    if ( d == thNULL ) { d = thNEW(myModuleData,()); reg->set_module_data(id, d); }
    return d;
}
```

- `pig_current_registry()` は `sCallSection` の TLS から辿るので、**ABI を変えずに**使える。
- agent プロセス側は `.so` が 1 本しかないので、名前を知らなくても `pig_current_module_id()` で引ける。
- ★ 「初期化したか」のガード用 static も**要らないことが多い**。上流の初期化関数が冪等なら
  （`GEO::initialize()` / `openvdb::initialize()` はそうだった）**毎回呼んでよい**。

### 5.4 例外とエラー — **モジュール側で捕まえ、理由を呼び手のバッファへ**

```
規約   幾何カーネルが投げる例外は **モジュール側で catch** して srava のエラー (pigDataError) にする
       (cgal / nef / geogram / occt / cherchi はすべてこの作法)。
安全網 それでも漏れた例外は **ホスト側が受け止める** (2026-08-26)。ただしこれは最後の砦であって、
       モジュールが catch しない理由にはならない (どの op で何が起きたかはモジュールしか知らない)。
理由の置き場 ⚠ **static に溜めない**。呼び手が用意したバッファへ書く (§5.3 と同じ理由)。
限界   ⚠ **ワーカースレッドから投げられた例外は捕まえられない** (geogram が実際にそう投げる)。
       この場合 agent は terminate → SIGABRT で死ぬが、**理由は読める** — 下記のとおり
       agent の stderr を拾ってエラー文に載せているため。
```

★ **agent の stderr はエラー文に出る**。`ptsErrSink` が溜め、mediator が「子の終了状態 + stderr + wire」を総合して理由を組み立てる。
⇒ **モジュールが黙って落ちても、リンクしたライブラリの言い分がそのまま利用者に届く**（モジュール非依存）。

★ **利用者に出る文字列は英語で書く**（コメントは日本語のままでよい）。→ `CONTRIBUTING.md`

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
読める。）

---

## 7. ビルドと配置

### 7.1 CMake — モジュールを `.so` としてビルド

モジュールは **`srava_add_module()`** で定義する（ルート `CMakeLists.txt` が定義しているヘルパ）。
`MODULE` ライブラリ・`PREFIX ""`（`lib<name>.so` でなく `<name>.so`）・**`INSTALL_RPATH`**・`pig` への
リンク・host との実行時依存は関数が面倒を見るので、**書き忘れが起きない**。
CGAL/Manifold のような重い依存はモジュール内に静的リンクし、pig/pts/tinyState は host が `-rdynamic` で供給
するので `.so` には bundle しない（undefined のまま dlopen 時に解決）。

値のみモジュール（`demo`）の最小例:

```cmake
srava_add_module(demo_module
  OUTPUT   demo                      # → demo.so
  SOURCES  modules/demo/dematsAgent.cpp
           modules/demo/demo_compute.cpp
           modules/demo/manifest.cpp
  DEPENDS  pigts_codegen demo_ts_codegen   # 生成物への依存 (あれば)
  INCLUDES modules/demo)
# INSTALL を付けると $PREFIX/lib/srava/modules へ install される (省略 = テスト専用モジュール)
```

外部ライブラリを使う場合は `LINK` に足す（`LINK CGAL::CGAL` / `LINK manifold::manifold`）。
同一ソースから 2 つの `.so` を作るときは `HIDDEN` を付ける（記述子などのグローバルシンボルが
衝突して、後から読まれた方が**先に読まれた方の記述子**を返す事故が実際に起きた）。


mesh を出力するモジュール（`d3`）は、これに codec / wire-cache stream の reader/writer とメッシュ本体クラスを
足すだけで、host 無改修で mesh の cache 往復が成立する。

### 7.2 `.so` の探索路と明示ロード

★ 起動時にやるのは**列挙だけ**で、実際に読み込むのは
**`module("<name>.so", {...})` が呼ばれたとき**（唯一のロード入口）。
モジュールを 1 本も使わないスクリプトは、モジュールのロードコストを一切払わない。
⚠ したがって **自作モジュールも `module()` で名指さないと使われない**。開発中に
「op が見つからない」と言われたら、まずこれを疑う（`include "module/all.sra";` でも可）。

列挙の順序は次のとおり。**下に行くほど強い**（「より具体的な場所が勝つ」）:

1. `/usr/local/lib/srava/modules`（configure 時の prefix を焼き込んだ install 既定 = `SRAVA_MODULE_SYSDIR`・最弱）
2. **実行体から見た `../lib/srava/modules`**（= 自分と同じ install ツリー。§7.4 の再配置可能性の要）
3. `~/.config/srava/modules`（ユーザ個人の上書き）
4. **実行体と同じディレクトリ**（ビルドツリーでは `cgal.so` 等が planner と同居）
5. **`$SRAVA_MODULE_PATH`**（`:` 区切り・Windows は `;`・**最優先**）

★ **同じファイル名の候補が複数あるときは、勝者 1 つだけを dlopen する**。全部 dlopen すると、
記述子は後勝ち・実行体と codec は先勝ちで登録されるため「**新しい ops 表で振り分けて古いコードを実行する**」
混成ができ、`srava --modules` は新しい方を勝者と表示するので気づけない（実際に古い vendored ソルバが
走ってヒープ破壊した）。読まれなかった候補は shadowed として `srava --modules` に出る。

★ `module("nef_snc.so")` のように **ファイル名だけ**を書いたときも、この探索路の強い順に解決される
（dlopen に素で渡すと ld.so の探索に行ってしまい、srava の探索路には当たらない）。

実装は `src/classes/pig/c++/pigModuleLoader.cpp`。開発中は `SRAVA_MODULE_PATH` で自作 `.so` のディレクトリを
指すのが手軽:

```sh
SRAVA_MODULE_PATH="/path/to/my/modules" srava my.sra
```

### 7.3 外部依存の取り込み規約

新しい幾何カーネルはたいてい外部ライブラリを連れてくる（geogram / OpenVDB / OCCT）。
取り込み方は **3 つだけ**とし、次の基準で選ぶ。迷ったら C → B → A の順に検討する（下に行くほど
srava 側の管理責任が重い）。

| | 方式 | 選ぶ条件 | 実例 | 実行時の共有依存 |
|---|---|---|---|---|
| **A** | **vendoring**（ソースを repo に置く） | 小さい・**自分たちが改修する**・上流が実質同じ担当 | pipeProximity | 増えない（静的） |
| **B** | **FetchContent + 静的リンク** | 中規模・上流を version pin できる・改修しない | Manifold（v3.5.2 に pin） | 増えない（静的） |
| **C** | **`find_package` でシステムから検出** | 巨大・distro が配っている・自前ビルドが現実的でない | CGAL / GMP / MPFR / Boost / HDF5 | 増える（system の `.so`） |

共通の約束:

- **A を選んだら `VENDORED.md` を必ず置く** — 由来（元 repo・分岐点の commit / tag・取り込み日）、
  上流追随するのかしないのか、ライセンス、改修時に守る制約を書く。
  「上流追随しない」と決めたならそう明記する（曖昧なまま放置すると次の担当が同期作業を探し始める）。
- **B は必ず `EXCLUDE_FROM_ALL` を付ける** — `FetchContent_MakeAvailable` は内部で `add_subdirectory`
  するので、これが無いと**上流の `install()` が srava の install に相乗り**して、ユーザが自前で入れた
  同じライブラリを `$PREFIX` 上書きして壊しうる。
- **C は「無い環境ではそのモジュールごと外れる」ことを保証する** — `option(SRAVA_MODULE_xxx)` の
  `if()` の**中**で `find_package` を呼ぶ。OFF にしたとき `find_package` すら走らないのが正しい形
  （ctest `srava_build_nodeps` が、全モジュール OFF 構成のビルドと実行を常時検証している）。
- **ライセンスの影響を `THIRD_PARTY.md` に反映する** — 特に GPL 系を新たに引く場合。
- **モジュールが「私物の共有ライブラリ」を持ち込む場合**（A/B で静的にできない、C でもない場合）は、
  その `.so` を **`$PREFIX/lib` へ install** する。モジュールの `INSTALL_RPATH`（`srava_add_module()` が
  付ける `$ORIGIN/../..`）がそこを指しているので、追加の RPATH 設定は要らない。
  install 規則を書き忘れると「ビルドツリーでは動くが install したら agent が起動しない」になる。

### 7.4 install レイアウトと再配置可能性

```
$PREFIX/bin/srava, srava_agent
$PREFIX/lib/libpig.so                    ← host もモジュールも参照する共有ライブラリ
$PREFIX/lib/srava/modules/*.so           ← モジュール (探索路の install 既定)
$PREFIX/share/srava/lib/std/*.sra        ← 標準ライブラリ
```

このツリーは **丸ごと別の場所へ移しても動く**。根拠は 3 つで、どれか 1 つでも欠けると壊れる:

1. モジュール探索路に **実行体から見た `../lib/srava/modules`** が入っている（§7.2 の ②）。
2. `srava_agent` の解決が **実行体と同じ dir** を見る（`SRAVA_AGENT` → 兄弟 → configure 時の prefix）。
3. `libpig.so` を **`DT_RPATH`**（`$ORIGIN/../lib`）で辿る。`DT_RUNPATH` ではなく `DT_RPATH` なのは、
   RUNPATH だと **`LD_LIBRARY_PATH` の方が先**に探されるため。`LD_LIBRARY_PATH` に空成分
   （末尾の `:` = カレントディレクトリ）が入っている機械は珍しくなく、その状態で別ビルドの
   ディレクトリを cwd にして走らせると**隣のビルドの `libpig.so`** を掴んで沈黙ハングする（実際に踏んだ）。

ctest **`srava_install_tree`** が、staging prefix へ install したツリーを **env を落として**走らせ、
上の 3 つと「外部依存を持つモジュールが in-proc / 別プロセスの両方で動くこと」を常時検証している。
新しいカーネルを足したら、このテストの対象リスト（`test/srava_install.sh`）にも 1 行足すこと。

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
    0,                                                  // provides (値のみ)
    0,                                                  // hash_salt (値だけなので不要)
    0,                                                  // arity (0 = 未指定 = 二項)
    0, 0,                                               // initialize / configure (使わない)
};
static const int helatsAgent_desc_registered =
    (pigModuleRegistry::register_descriptor(&helatsAgent_descriptor), 0);
```

```c++
// manifest.cpp — 唯一の C エクスポート
#include "pig/c++/pigModule.h"
extern const srava_module_descriptor helatsAgent_descriptor;
SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void) { return &helatsAgent_descriptor; }
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


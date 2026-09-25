# srava モジュールリファレンス

srava は、本体を再ビルドせずに機能を足せる **モジュール機構**を持つ。モジュールは
**ダイナミックリンクの単位**（`.so`）で、登録した **op 名**を srava プログラムから普通の関数のように
呼べる。実行体（host）は **`module()` が呼ばれた時点で** `.so` を **dlopen** して記述子を読み、どのモジュールが
どの op・どの **型**（mesh の型・4CC と 1:1）を扱うかを登録する（下記）。同名 op が複数モジュールにあるとき、
host は入力 mesh の**型でディスパッチ**先を決める（例: `union` の入力が `mf-mesh3d` なら manifold.so、
`cg-mesh3d` なら cgal.so）。pipe_proximity のような**解析モジュール**は値（数・配列・文字列・ハッシュ）
だけをやり取りし、型も**幾何カーネル**（CGAL/Manifold などの幾何コア・モジュールだけが知る）も知らない。

このリファレンスは同梱モジュールの**利用者向け仕様**を扱う。
⚠ `.so` を持たない **擬似モジュール**（候補列に置く値）は別ページ →
[擬似モジュールリファレンス](srava_pseudo_module_reference.html)。
**同梱は全 22 モジュール**:

| 分類 | モジュール | 既定 |
|---|---|---|
| 幾何カーネル | [cgal.so](#cgal) / [manifold.so](#manifold) | **ON** |
| 幾何カーネル | [nef_hybrid.so](#nef) | **ON** |
| 幾何カーネル | [nef_snc.so](#nef) | **ON**（`-DSRAVA_MODULE_NEF_SNC=OFF` で外せる） |
| 幾何カーネル | [geogram.so](#geogram) / [cherchi.so](#cherchi) | **ON** |
| ボリューム | [openvdb.so](#openvdb) | **ON** |
| ボリューム橋渡し | [openvdb_mf.so / openvdb_cg.so / openvdb_gg.so](#openvdb_bridge) | **ON**（`openvdb_cg` は CGAL をリンクするので **GPL**・`-DSRAVA_MODULE_OPENVDB_CG_GPL=OFF` で外せる） |
| Nef 橋渡し | [nef_cg.so / nef_mf.so](#nef_bridge) | `nef_snc.so` と連動（既定 **OFF**） |
| B-rep | [occt.so](#occt) / [occt_mf.so](#occt_mf) | **ON**（system の OpenCASCADE が要る） |
| メッシュ共通 | [geomutils.so](#geomutils) | **ON**（外部ライブラリ非依存なので常にビルドされる） |
| 点群 | [points.so](#points) | **ON**（外部ライブラリ非依存なので常にビルドされる） |
| 解析 | [pipe_proximity.so](#pipe_proximity) | **ON** |
| デモ／テスト | [demo.so / d2.so / d3.so / d4.so / d5.so](#demo) | **ON** |

★ **`geomutils.so` / `points.so` は「幾何カーネル」ではない**。外部の幾何ライブラリを持たず、
**他のモジュールが作った値をそのまま受けて**答える *カーネル中立* のモジュールで、
`geomutils.so` はメッシュ系（`manifold` / `geogram` / `cherchi` / 自型 `gu-*`）の
**計測・位相・片の取り出し・頂点読み**を 1 本の実装で引き受ける。
⚠ **ロードは自動ではない** — `valid(mfMesh)` / `genus(...)` / `part(...)` の類は
`module("geomutils.so", {})` を書いていないと「no module can execute op」になる。

**2026-08-31 以降、同梱モジュールは既定で全部ビルドされる**（`nef_snc.so` も 2026-09-16 に既定 ON へ戻った）。
不要なものは `-DSRAVA_MODULE_<名前>=OFF` で外せる — `geogram` / `openvdb` / `cherchi` は
FetchContent で取得してビルドするため時間がかかり、`occt` は system の OpenCASCADE を要求する。
⚠ **Cygwin では TBB / OpenCASCADE / abseil の都合で `geogram` / `openvdb` / `occt` / `cherchi` が
自動 OFF** になる（`manifold` の op 内並列も同様）。詳細は[Cygwin ビルド手順](cygwin_build.html)。

記述子 ABI・レジストリ・型ディスパッチ・モジュール間の型変換といった**内部設計**は
[モジュール設計](srava_module_design.html)を参照。

---

## モジュールの構造

| 層 | 役割 |
|---|---|
| **pig**（framework） | モジュールレジストリ・エージェントノード・値コーデック・SDK |
| **srava**（言語） | パーサが登録 op 名を内部の（generic な）モジュールエージェントノードへ繋ぐ |
| **module**（`.so`） | 便利機能の実体。自分の op を C++ 記述子で申告。host を知らない |

- モジュール = **ダイナミックリンクの単位**（`.so`）。`modules/<name>/` をビルドして `<name>.so` を生成し、
  実行体 `srava_agent`（および planner 本体 `srava`）が **dlopen** して記述子を読む。
- 実行方式は記述子と `module(...)` で決まる。既定は **planner 内 thread**（in-proc, EXEC_THREAD）。
  `module("<name>.so",{exec_default:"process"})` と明示したときだけ別プロセス実行する。どちらでも
  **同じ記述子・同じ generic 経路**（`pigfModuleAgent`）を通る（旧「プラグイン=独立プロセスで pigwire
  で planner と 1 往復」という機構は無い）。
- op は記述子の **C++ 配列**が申告する（例: pipe_proximity の `PP_OPS[]`）。`.plugin` のような
  行ベースのマニフェストファイルや、起動する外部 `bin` の指定は無い（`.so` を dlopen するだけ）。
- 解析モジュールの値の受け渡しは pig 値（null / 整数 / 小数 / 文字列 / 配列 `[...]` / ハッシュ `{"k":v,...}`）。
- 各呼び出しは自己完結（グローバル状態なし）。結果は op 名 + 引数ハッシュで **内容アドレスキャッシュ**に乗る。

### `.so` の探索と **明示ロード**

モジュールは **`module(...)` を呼んだときにロードされる**。

- 起動時にやるのは **探索ディレクトリの列挙だけ**（dlopen はしない）。`module()` の名前解決に使う。
- **`module(...)` が唯一のロード入口**。呼ばれた時点で、まだロードされていなければそこで読み込む。
- 何もロードしないスクリプトは **モジュールのロードコストを一切払わない**。

探索ディレクトリの順序（**後に見つかったものが勝つ = 後勝ち**）:

1. `/usr/local/lib/srava/modules`（`cmake --install` の配置先 = install 既定。CMake の
   `SRAVA_MODULE_SYSDIR` で上書き可）
2. `~/.config/srava/modules`（ユーザ個人の上書き）
3. **実行体と同じディレクトリ**（ビルドツリーでは各 `.so` が planner と同居）
4. `$SRAVA_MODULE_PATH`（`:` 区切りで複数ディレクトリ可・**最後が最優先**）

つまり優先度は **install 済み ＜ ユーザ設定 ＜ ビルドツリー ＜ `$SRAVA_MODULE_PATH`** で、
「より具体的な場所が勝つ」。ビルドツリーで `srava` を叩けば、install 済みがあっても
**そのビルドツリーの `.so` が使われる**。

**まとめてロードしたいとき**:

```
include "module/all.sra";        // 同梱 16 本を optional で一括ロード (lib/module/all.sra)
include "module/demo.sra";       // デモ／テスト用 5 本 (demo/d2/d3/d4/d5)。通常は不要
```
同梱スクリプトは `lib/module/` に 4 本あり、残る 2 本は **.so をロードしない**（値やヘルパを
定義するだけなので、上の表の「収録」には出てこない）:

| ファイル | 何をするか | 詳細 |
|---|---|---|
| `module/pseudo.sra` | 擬似モジュール `pm_*` を定義し、**粒度の既定値**（`seg` / `dx`）を候補列で練り込む | [言語リファレンス §擬似モジュール集](srava_language_reference.html#pmod) |
| `module/reload.sra` | `module_reload(path, opts)` — `"off"` してから読み直して **.so を差し替える** | [関数リファレンス `module_reload`](srava_function_reference.html#module_reload) |

⚠ どちらも `all.sra` には入っていない。`pseudo.sra` が**トップレベルに** `use` を書かないのと同じ理由で、
**既定として敷くと利用者の指定（`module(so,{priority:N})` や自前の候補列）が黙って効かなくなる**。
★ 効くのは *誰が* 書くかではなく ***どこに* 書くか**。読み込まれるファイルの**トップレベル**は ✗（利用者の
env に残る）だが、**関数の内側**は ✓ — `use` は DEF なので抜ければ外の値へ戻り、外へ漏れない。
⇒ ライブラリ関数は**自分の候補列を関数の頭で宣言する**のがよい（必須は `module([…])` で末尾）。
→ [言語リファレンス §ライブラリ関数の宣言](srava_language_reference.html#lib-use-decl)
```sh
SRAVA_MODULE_ALL=1 srava foo.sra   # 環境変数でも同じことができる
```
> ⚠ `SRAVA_MODULE_ALL=1` は**ソース先頭に `include "module/all.sra";` を差し込むだけ**なので、
> `include` と同じ解決規則に従う。**`all.sra` が見つかる場所に居ることが前提**で、具体的には
> `$SRAVA_PATH`(コロン区切り)か、install 済みの `$PREFIX/share/srava/lib`(コンパイル時に焼かれる)。
> **ビルドツリーには `lib/` が置かれない**ので、install せずにビルドツリーで使うなら
> `SRAVA_PATH=<ソースツリー>/lib` を明示する。見つからなければ **`include: cannot find` で明示エラー**に
> なる(探した場所がメッセージに出る)。黙って無視はしない。
> ⚠ **収録範囲**（2026-09-01 に見直し・2026-09-07 に Nef 橋渡しを追加・2026-09-19 に `geomutils` を追加）:
> `all.sra` は同梱 22 本のうち **16 本**を並べる。
>
> | ファイル | 収録 |
> |---|---|
> | `module/all.sra` | cgal / manifold / nef_hybrid / geogram / cherchi / geomutils / openvdb / openvdb_mf / openvdb_cg / openvdb_gg / nef_cg / nef_mf / occt / occt_mf / points / pipe_proximity（16 本） |
> | `module/demo.sra` | demo / d2 / d3 / d4 / d5（5 本・デモ／テスト専用。**install されない**ので install 済みツリーでは全部スキップされる） |
> | どちらにも無い | `nef_snc.so`（1 本） |
>
> ⚠ `nef_snc.so` はどちらにも入れない。実カーネルの変種なので、ビルドはされていても
> `module("nef_snc.so",{});` と **明示的に書く**必要がある — 入れずに呼ぶと
> `cannot open shared object file` になる。
> ★ 2026-09-16 に**既定 ON へ戻した**（2026-08-31〜09-16 は既定 OFF だった）。
> その期間に建てた木では `-DSRAVA_MODULE_NEF_SNC=ON` を足さないと `.so` 自体が無い。
>
> ⚠ ロードは無料ではない。モジュール本数は srava の起動固定費に効くので、使う `.so` が
> 決まっているなら `all.sra` ではなく個別に `module()` するほうが速い。
> ⚠ サードパーティのプラグイン（`pipe_proximity`）と依存ゼロのトイ実装（`d2`-`d5` / `demo`）も
> 入っていない。`pipe_scene_adjust` 等が `undefined variable` になるのはこのため（仕様）。
> ⚠ ビルド構成によって存在しない `.so` があるので、`all.sra` の各行は `{optional: 1}` で書かれている。

そのモジュールが**何を申告しているか**（op ごとの `sig` 全リスト・`provides`（階層 × 型名 × 4CC）・
`exec_caps` / `exec_default` / `arity` / 拡張子 / フック）は **`srava --module-info [名前 ...]`** で出る。名前を省くと全モジュール
（500 行を超える）、名前を与えるとそれだけに絞られる。`--modules` とは**別コマンド**で、問いが違う
（`--modules` = 「どの `.so` が効いているか」= 配置 / `--module-info` = 「何を申告しているか」= 中身）。

> **この 2 つの診断コマンドの詳細（出力例つき）はインストールガイドにある**:
> [`srava --modules`](srava_install_guide.html#modules) ／
> [`srava --module-info`](srava_install_guide.html#module-info)
> （コマンドラインのフラグ一覧は [§9](srava_install_guide.html#cli)）。
> **モジュールが効かないときは、まず `--modules` を見る。**

```
$ srava --module-info occt_mf
occt_mf  (abi=31 prio=0 /usr/local/lib/srava/modules/occt_mf.so)
    exec_caps=process(0x2)  exec_default=process  make_agent=yes
    grace=0(kill at once)  panic=off
    arity=0  cache_version=1  import=-  export=-  initialize=no  configure=no
    cache_salt=|occt_mf|v1
    ops (2):
      triangulate        nin=2 wire=[ocGeom]
        sig = (oc-brep3d)->mf-mesh3d
      polygonize         nin=2 wire=[ocGeom]
        sig = (oc-face3d)->mf-cross2d
    provides (hierarchy / declared type names / tags probed against create):
      ocGeom             types = oc-brep3d
                         create=yes reader=yes writer=yes match=yes
                         tag 'BREP' -> oc-brep3d
      mfGeom             types = mf-mesh3d
                         create=yes reader=yes writer=yes match=yes
                         tag 'MFM3' -> mf-mesh3d
```

どの `.so` が見えているかは **`srava --modules`** で確認できる（この診断だけは
**全部を列挙して dlopen する**）。実装は `src/classes/pig/c++/pigModuleLoader.cpp` /
`pigModuleRegistry.cpp`。1 つの `.so` が **複数の op** を serve してよい
（pipe_proximity.so は 1 つの `.so` で **5 op**（後述）を提供する）。

### `module(...)` — モジュールをロードし、記述子の内容を上書きする

`module` は、モジュールの**ロード**と、記述子の内容（優先度・実行方式など）の**上書き**を行う関数である。
**ロードの唯一の入口**でもあるので、**使うモジュールは必ず一度は `module()` で名指す**
（`include "module/all.sra";` で一括でもよい）。構文は `src/classes/cg/c++/ns_sravaParser.y`・意味は
`src/classes/pig/c++/pigData.cpp`（`pigDataOperatorModule`）で実装される。

```
module("cgal.so");                             // ① 未ロードなら読み込む（第 2 引数省略の糖衣）
module("manifold.so", { priority: 99 });       // ② 既定幾何カーネルの優先度を上げる（大きいほど優先）
module("cgal.so",     { exec_default: "process" });  // ③ 実行方式を別プロセスに（重い op 向け）
module("manifold.so", { exec_default: "thread" });   // ③ in-proc（planner 内スレッド）に固定
module("cgal.so", "off");                      // ④ アンロード (dlclose)。module() で読み直せる
module_loaded("cgal.so");                      // ④ いま載っているか (1/0)。★ロードはしない
module("geogram.so", { optional: 1 });         // ⑤ 無ければ静かに諦める (ビルド構成差の吸収)
module("openvdb.so", { arity: 16 });           // ⑥ n 項ブールを何項まで 1 回で渡すか
module("geogram.so", { threads: 4 });          // ⑦ op 内並列の上限 (受け取るのはモジュール側)
module(["occt", "cgal"], {});                  // ⑧ **列でまとめて読む**（要素は記述子名）
```

第 1 引数は **`.so` のファイル名（文字列）** または **記述子名の列（配列）**。
列形は要素を[候補列](srava_language_reference.html#module-qualified)と同じ構造規則で読み、
平坦化後の列と **1 : 1** の配列を返す（★ 不変式 **`use module(L,{})` の候補列は `use L` と完全に同一**）。
詳細は[関数リファレンス §列でまとめて読む](srava_function_reference.html#module-list)。
⚠ 列形が引くのは **探索路だけ**なので、探索路の外の `.so` や、**記述子名とファイル名が違う `.so`** は
文字列形であらかじめ読んでおくこと。

第 2 引数は**ハッシュ**または**文字列**（省略可）。戻り値は解決したモジュール名（文字列。列形なら配列）:

| 第 2 引数 | 効果 |
|---|---|
| （なし） | 未ロードなら読み込む（糖衣）|
| `{priority: N}` | **既定幾何カーネルの優先度**（整数）を上書き。型が定まらない leaf op（例: 引数だけの `box`）で、どのモジュールを既定にするかを決める。**大きいほど優先**。★**同点の勝敗は不定**（ロード順 = 走査順に依存）なので、確実に切り替えるなら既存の最大値より大きい値にする |
| `{exec_default: "thread"｜"process"}` | **実行方式**。`"thread"`=planner 内 in-proc / `"process"`=別プロセス `srava_agent`。省略時は記述子の既定（cgal=process 相当・manifold=thread） |
| `"off"` | **アンロード（`dlclose`）**（2026-08-28）。★ 列形には**使えない**（明示エラー。落とすのは 1 本ずつ）。そのモジュールの型・4CC・codec・実行体・拡張子は**どこからも見えなくなる**。以後 `module(so,{...})` で読み直せる。★**一度でも使われたモジュールは落とせない**（`.so` 由来のオブジェクトが生きうるため明示エラー）。★**未ロードへの `"off"` も明示エラー** — 載っているかは `module_loaded(so)` で判定する。旧 `"on"` は撤去（再ロードは `module(so,{...})`）|
| `{optional: 1}` | **入っていなければ**エラーにしない（静かに諦める）。ビルド構成によって存在しない `.so` があるため、便宜スクリプト（`module/all.sra`）はこれで書かれている。⚠⚠ **「入っていない」と「拒んだ」は別**： ABI 不一致・記述子違反・`srava_module` を持たない `.so`・ファイルは在るのに dlopen が失敗した場合・同名で別の実ファイル は、`optional` でも**明示エラー**（飲み込むと *別のモジュールが答えたまま先へ進む*） |
| `{arity: N}` | **n 項ブールを何項まで 1 回の呼び出しで渡すか**。記述子の申告（そのモジュールが受けられる最大項数）とは別で、こちらは **policy**（実際に何項で渡すか）。「木の段数を減らす」ことと「1 回の呼び出しを重くする」ことのトレードオフを、スクリプトを変えずに切り替えられる。★ 受けられる上限はモジュールが決める（例: geogram / cherchi は 32）ので、それを超える指定は上限で頭打ちになる |
| `{threads: N}` | **op 内並列の上限**。受け取るのは記述子の `configure` フックを持つモジュールだけ（現在は geogram / openvdb / cherchi）。`N<=0` は「制限を解除して既定へ戻す」。⚠ 絞れば速くなるとは限らない（同上） |

- 選択そのものは型でディスパッチされるので、`union(mfBody, mfBody)` は `module` 無しでも manifold.so に行く。
  `module(..., {priority})` が効くのは**型が決まらない生成 op の既定**（どの幾何カーネルで `box` を作るか等）。

### ★★ **引数の数も行の成立条件**です（2026-09-21）

同じ op 名でも、モジュールによって取る引数が違います。**その個数を受けられない行は候補から
外れ、隣のモジュールへ降ります** — C++ のオーバーロード解決に近い形です。

```srava
module("cgal.so",{}); module("openvdb.so",{});
volume(box(2,2,2));          // 8            ← cgal（厳密）
volume(box(2,2,2, 0.05));    // 8.0201…      ← openvdb（末尾は dx = ボクセルサイズ）
```

⇒ `priority` を上げていても、**その個数を受けられなければ選ばれません**。

```srava
module("openvdb.so",{priority:99});
type_of(sphere(1));          // cg-mesh3d    ← dx が無いので openvdb は候補から外れる
type_of(sphere(1, 0.05));    // vd-grid3d    ← openvdb だけが受ける
```

⚠ **指名（`"openvdb"::` / `use`）したときは降りられません** — その候補だけで解けなければ
エラーになります。どれも受けられないときは、候補ごとの取れる個数を並べて言います:

```
*** op 'sphere' — no candidate takes 3 argument(s)
    (cgal: takes 1 to 2; manifold: takes 1 to 2; openvdb: takes 2) ***
```

★ 「必要のない引数は撤去し、必要なものは引数として見せる」が原則です。
隠したい場合は**擬似モジュール**（[言語リファレンス §擬似モジュール](srava_language_reference.html#pseudo-module)・
既定の粒度を練り込む定型は [`module/pseudo.sra`](srava_language_reference.html#pmod)）で包んでください。
- `{exec_default}` は機能は変えず**実行方式だけ**を変える（in-proc は速い・process は重い op を分離できる。
  詳細は[モジュール設計](srava_module_design.html)）。

---

## 型 × モジュール一覧 {#type-matrix}

srava の値の **型**（`cg-mesh3d` のような綴り）と、それを扱う `.so` の対応。
★ **記述子から機械生成**（`srava --module-info` の `provides` と各 op の `sig`）。

- **4CC** … その型を**書き出す**ときの形式。`読み:` はそのモジュールが**読める**ほかの 4CC
  （＝昇格読み・降格読み。→ [型変換の規約](#conversion)）。
- **値を作る** … その型を `sig` の**出力**に書く `.so`（`->cg-mesh3d` のように）。
- **値を受ける** … その型を `sig` の**入力**に名乗る `.so`。
  ★ ここには**その型を所有していない**モジュールが並ぶ — 他カーネルの値をそのまま受ける口で、
  これが「混ぜて書ける」の実体。

| 型 | 4CC | 中身 | 値を**作る** | 値を**受ける** |
|---|---|---|---|---|
| **3D ソリッド / ボリューム** | | | | |
| `cg-mesh3d` | **`MESH`**<br><small>読み: `MFM3`, `NEFB`</small> | 3D ソリッド（三角形メッシュ）<br>CGAL EPECK・**厳密**（有理数） | `cgal` / `nef_cg` / `openvdb_cg` | `cgal` / `cherchi` / `geogram` / `geomutils` / `manifold` / `nef_hybrid` / `nef_snc` / `openvdb_cg` |
| `mf-mesh3d` | **`MFM3`**<br><small>読み: `MESH`, `NEFB`</small> | 3D ソリッド（三角形メッシュ）<br>Manifold・double | `manifold` / `nef_mf` / `occt_mf` / `openvdb_mf` | `cgal` / `cherchi` / `d4` / `d5` / `geogram` / `geomutils` / `manifold` / `nef_hybrid` / `nef_snc` / `openvdb_cg` / `openvdb_mf` |
| `gg-mesh3d` | **`MFM3`**<br><small>読み: `MESH`</small> | 3D ソリッド（三角形メッシュ）<br>geogram・演算は厳密／座標は double | `geogram` / `openvdb_gg` | `cgal` / `cherchi` / `geogram` / `geomutils` / `manifold` / `nef_hybrid` / `nef_snc` / `openvdb_cg` / `openvdb_gg` |
| `ch-mesh3d` | **`MFM3`**<br><small>読み: `MESH`</small> | 3D ソリッド（三角形メッシュ）<br>cherchi（間接述語）・double | `cherchi` | `cgal` / `cherchi` / `geogram` / `geomutils` / `manifold` / `nef_hybrid` / `nef_snc` / `openvdb_cg` |
| `gu-mesh3d` | **`MFM3`** | 3D ソリッド（三角形メッシュ）<br>**カーネル中立**・double。片の取り出しの返り型 | `geomutils` | `cgal` / `cherchi` / `geogram` / `geomutils` / `manifold` / `nef_hybrid` / `nef_snc` |
| `nf-mesh3d` | **`NEF3`**<br><small>読み: `NEFB`, `MESH`, `MFM3`</small> | 3D ソリッド（**Nef SNC**）<br>CGAL Nef・厳密・常に SNC 表現 | `nef_snc` | `nef_cg` / `nef_mf` / `nef_snc` |
| `nfb-mesh3d` | **`NEFB`**<br><small>読み: `NEF3`, `MESH`, `MFM3`</small> | 3D ソリッド（**Nef 境界ハイブリッド**）<br>CGAL Nef・厳密・有界立体は境界表現 | `nef_hybrid` | `cgal` / `manifold` / `nef_hybrid` / `openvdb_cg` |
| `oc-brep3d` | **`BREP`** | 3D ソリッド（**B-rep**）<br>OCCT・解析曲面 / NURBS を**近似せずに**持つ | `occt` | `occt` / `occt_mf` |
| `vd-grid3d` | **`VDB`** | 3D ボリューム（**符号付き距離場**）<br>OpenVDB・格子。表面は値 0 の等値面 | `openvdb` / `openvdb_cg` / `openvdb_gg` / `openvdb_mf` | `openvdb` / `openvdb_cg` / `openvdb_gg` / `openvdb_mf` |
| **2D 領域 — `z=0` の簡易表現** | | | | |
| `cg-cross2d` | **`PLY2`**<br><small>読み: `MFC2`</small> | 2D 領域（**z=0** の簡易表現）<br>CGAL EPECK・厳密 | `cgal` | `cgal` / `geomutils` / `manifold` |
| `mf-cross2d` | **`MFC2`**<br><small>読み: `PLY2`</small> | 2D 領域（**z=0** の簡易表現）<br>Manifold / clipper2・double | `manifold` / `occt_mf` | `cgal` / `geomutils` / `manifold` |
| `gu-cross2d` | **`MFC2`** | 2D 領域（**z=0** の簡易表現）<br>カーネル中立・double | `geomutils` | `cgal` / `geomutils` / `manifold` |
| `oc-cross2d` | **`BRP2`** | 2D **図面**（面を持たない＝稜だけ）<br>OCCT・`hlr` の出力がこれ | `occt` | `occt` / `occt_mf` |
| **2D 領域 — 空間に置かれた一般表現** | | | | |
| `cg-face3d` | **`PLY2`** ※2 | 2D 領域（**空間に置かれた**一般表現・枠つき）<br>CGAL EPECK・厳密 | `cgal` | `cgal` / `geomutils` / `manifold` |
| `mf-face3d` | **`MFC2`** ※2 | 2D 領域（**空間に置かれた**一般表現・枠つき）<br>Manifold・double | `manifold` / `occt_mf` | `cgal` / `geomutils` / `manifold` |
| `gu-face3d` | **`MFC2`** ※2 | 2D 領域（**空間に置かれた**一般表現・枠つき）<br>カーネル中立・double | `geomutils` | `cgal` / `geomutils` / `manifold` |
| `oc-face3d` | **`BRP2`** ※2 | 2D **面**（平面に限らず**曲面上**に切り取られた面も）<br>OCCT・輪郭は Bezier / B-spline のまま | `occt` | `occt` / `occt_mf` |
| **点群** | | | | |
| `pt-cloud2d` | **`PTC2`** | **点群**（2D・法線なし）<br>カーネル中立（`libsrava_pt`） | `cgal` / `geomutils` / `occt` / `points` | `cgal` / `points` |
| `pt-cloud3d` | **`PTC3`** | **点群**（3D・法線を持てる）<br>カーネル中立（`libsrava_pt`） | `cgal` / `geogram` / `geomutils` / `occt` / `points` | `cgal` / `geogram` / `points` |
| **デモ／テスト用** | | | | |
| `d2-shape2d` | **`D2S2`** | デモ／テスト用の 2D 値<br>幾何カーネルではない | `d2` | `d2` |
| `d3-mesh3d` | **`D3M3`** | デモ／テスト用の 3D 値<br>mesh を**出力する**モジュールの例 | `d3` | `d3` |
| `d4-mesh3d` | **`D4M3`**<br><small>読み: `MFM3`</small> | デモ／テスト用の 3D 値<br>in-proc で mesh を**消費する**例 | `d4` | `d4` |
| `d5-mesh3d` | **`D5M3`**<br><small>読み: `MFM3`</small> | デモ／テスト用の 3D 値<br>d4 と同型・**別の自型**（多型共存） | `d5` | `d5` |

- ※1 **4CC は形式の名前であって型の名前ではない**。形式が同じなら**共有する**
  （`MFM3` は `mf-` / `gg-` / `ch-` / `gu-` と d4 / d5 が共有）。型の区別は記述子の
  `types` 申告と**型スタンプ**が担い、キャッシュの弁別は**キャッシュソルト**
  （モジュール名 + `cache_version`）が担うので衝突しない。
- ※2 **`*-face3d` は兄弟の `*-cross2d` と同じ 4CC に書き出す**。4CC だけからは 2 つを区別できず、
  **型は planner が載せるスタンプが持つ**（blob を分ける必要がないため）。
  ⇒ 2D の 2 型の規約は [2D 領域の 2 つの型](srava_language_reference.html#two-2d-types)。
- ★ **点群型（`pt-cloud2d` / `pt-cloud3d`）の本家は [points.so](#points)**（実体は中立の
  `libsrava_pt`）。`cgal` / `geogram` / `occt` / `geomutils` は**借りている**だけで
  （`<mod>_provides` に `&ptCloud::WIRE` を 1 行足す）、点群そのものを所有してはいない。
- ★ **同じ型を複数のモジュールが名乗るときの勝者は `priority`**（大きいほうが勝つ）:

  | priority | モジュール |
  |---|---|
  | 20 | `cgal` |
  | 10 | `manifold` |
  | 7 | `geomutils` |
  | 6 | `geogram` |
  | 5 | `nef_snc` / `nef_hybrid` |
  | 4 | `pipe_proximity` |
  | 3 | `cherchi` |
  | 2 | `occt` |
  | 1 | `openvdb` |
  | 0 | 橋渡し（`nef_cg` / `nef_mf` / `occt_mf` / `openvdb_*`）・`points` |
  | 負 | デモ／テスト（`demo` / `d2`〜`d5`） |

  ⚠ **型が決まる呼び出しでは priority は効かない**（入力型でモジュールが決まる）。効くのは
  `box()` のような**型が決まらない生成 op** と、*同じ型を複数が受けられる*とき。
  名指ししたいときは `"nef_snc"::part(m, i)` と書く。
- ⚠ 表に無い `value` / `ref` は**幾何型ではない**（数・配列・ハッシュ・ファイル参照）。
  `volume` のような計測 op の `sig` の出力がこれ。

---

## 型変換の規約 — `cast` と橋渡しモジュールの分担 {#conversion}

> ★ ひさ指示（2026-09-01）。**どちらに置くかの唯一の基準は「精度パラメータが要るか」。**

| | 置き場所 | 例 |
|---|---|---|
| **精度を変えない変換** | 各カーネルの `cast` | `cast("cg-mesh3d", mfMesh)`（double→EPECK 昇格）<br>`cast("mf-mesh3d", cgMesh)`（EPECK→double 降格） |
| **精度パラメータを要する変換** | **橋渡しモジュールの明示 op** | `triangulate(brep, defl)`（`occt_mf.so`）<br>`voxelize(mesh, dx)`（`openvdb_cg.so` 他）<br>`isosurface(grid, iso)`（同上） |

### なぜ `cast` に精度を持たせないか

**① `cast` の引数は「目標型名」であって、そこに粒度の居場所が無い。**
`cast(T, x)` は「`T` を産出できるモジュール」へ振られる **型軸ディスパッチの入口**
（ 最後の段 2/5 以降、判定は routing の cast 専用ブロックではなく **行のマッチ関数**
`pig_match_cast_target` が持つ ＝ 目標型が *その行の sig の出力型か*。⇒ `cast` の行は
目標型ごとに 1 本で、cgal なら `cast#cg-mesh3d` / `cast#cg-cross2d` / `cast#cg-face3d`）。
⇒ `import` / `export` も同じ形へ移り（3/5・4/5）、**routing に op 名の特例は 1 つも残っていない**。
★ 4/5 で `export` の「入力型の home カーネルを優先する」規約を撤去した — 行き先は
**priority × sig × 拡張子**だけで決まる（`export("a.stl", <mf-mesh3d>)` は cgal が書く）。ここに第 3 引数として粒度を足すと、`cast` が「型変換」と「精度指定」の
2 つの意味を持つ。実際 `cast` は 2026-08-28 に「出力型だけ見て sig の入力申告を無視し、**黙って
誤った値を返す**」穴を踏んでおり、意味を増やす方向は同じ穴を広げる。

**② 精度が要る変換は「同じ入力から違う結果」が出る。** `defl` を変えれば三角形の数が変わり、
`dx` を変えればボクセルの数が変わる。値ベースの DAG では、その値が**キャッシュキーに現れなければ
ならない**。`cast` の引数に紛れ込ませるより、**op の名前と引数として表に出す**ほうが正しい。

**③ 前例がそうなっている。** B-rep → メッシュ（3D で「曲面を三角形に落とす」= 粒度が要る）は
`cast` ではなく `occt_mf.so` の `triangulate(s, defl)` として実装されている。

```c
static const pigArgKind TRI_IN[] = { AK_CACHE, AK_INLINE };   /* triangulate(s, defl) */
{ "triangulate", TRI_IN, 2, AK_CACHE, ..., "(oc-brep3d)->mf-mesh3d" }
```

### 帰結

- ⚠ **`cast(oc-face3d, …) -> cg-cross2d` のような「曲線を折れ線に落とす」変換は `cast` に置けない。**
  2D で「曲線 → 折れ線」に粒度が要るのは、3D で「曲面 → 三角形」に粒度が要るのと同じ問題。
  橋渡しモジュールの明示 op として置く（→ [occt.so の 2D](#occt) の注記）
- `cast` で書けるのは、**同じものを別の表現で持ち直すだけ**の変換に限る

---

## モジュールごとの並列性 — thread agent と process agent {#exec}

srava は op を 2 通りの方式で実行する。どちらになるかは**記述子の `exec_caps` / `exec_default`**
（`module(..., {exec_default})` で上書き可）で決まる。

| 方式 | 実体 | 特徴 |
|---|---|---|
| `"thread"` (in-proc) | planner 内のスレッド（`ptsMediatorInternal`） | プロセス起動もシリアライズ往復も無い。**速い** |
| `"process"` | 別プロセス `srava_agent` | アドレス空間が別。**隔離される** |

同梱モジュールの `exec_caps`（取りうる方式）と既定は次のとおり。**全 18 モジュールを網羅**する。

| モジュール | `exec_caps` | 既定 | 理由 |
|---|---|---|---|
| `manifold` | `THREAD｜PROCESS` | **thread** | 値が共有に耐える |
| `pipe_proximity` | `THREAD｜PROCESS` | **thread** | 幾何型を持たず値だけをやり取りする |
| `cgal` | `PROCESS` のみ | process | EPECK の値が共有に耐えない |
| `nef_snc` / `nef_hybrid` / `nef_cg` / `nef_mf` | `PROCESS` のみ | process | 同上（Nef も EPECK 上に構築される） |
| `geogram` / `cherchi` / `occt` / `occt_mf` | `PROCESS` のみ | process | プロセス全体のグローバル初期化を持つ。in-proc の安全性は未検証 |
| `openvdb` / `openvdb_mf` / `openvdb_cg` / `openvdb_gg` | `THREAD｜PROCESS` | process | ⚠ **thread も可能**だが既定は process のまま（下記） |
| `demo` | `PROCESS` のみ | process | デモ／テスト用 |
| `d2` / `d3` | `THREAD｜PROCESS` | process | デモ／テスト用 |
| `d4` / `d5` | `THREAD｜PROCESS` | **thread** | デモ／テスト用（値のみ） |

> ⚠ **openvdb 系の `THREAD` は実験用の口**。`exec_caps` に `THREAD` が立っているのは、in-proc agent が
> planner と同一アドレス空間にいて**メモリ会計に遅延なく含まれる**（process agent の pid 登録遅れが
> 原理的に無い）ことを測るため。**既定は `process` のまま**で、試すときは明示する:
>
> ```
> module("openvdb.so", {exec_default:"thread"});
> ```
>
> ⚠ 安全性は未検証（openvdb のグローバル初期化と TBB を planner プロセスへ持ち込む）。

### プロセス分離が持つ、並列性以外の利点

`EXEC_PROCESS` は「in-proc にできないから仕方なく」ではなく、積極的な利点もある。

- **障害隔離**: CGAL は不正な入力（非閉・自己交差）で **segfault しうる**。別プロセスなら
  agent が死ぬだけで planner はエラーとして受け取れる
- **メモリ隔離**: 厳密カーネルは入力規模に対して RSS が大きく伸びる。OOM kill の巻き添えを防ぐ
- **決定性**: CGAL corefinement は内部のポインタ／ハッシュ順序に依存する箇所があり、
  ヒープ配置（スレッドのタイミングで揺れる）によって**幾何的には等価だがテッセレーションが異なる**
  出力を返すことがある。スレッド数が増えるほどこの揺れは出やすい

## インストール

モジュールは本体と一緒に `cmake --install` で配置される。ビルドするモジュールは configure
オプションで選ぶ（pipe_proximity はコアもソース同梱・MIT・CGAL 非依存で、既定 ON）:

```sh
cmake -S . -B build -DSRAVA_MODULE_PIPEPROX=ON   # 既定 ON。OFF で除外(取り込み済みソース・外部取得なし)
cmake --build build -j
sudo cmake --install build                        # srava/srava_agent + モジュール一式を $PREFIX へ
```

- `cmake --install` が配置するもの:
  - モジュール → `$PREFIX/lib/srava/modules/`（探索路 ② に載る）: `cgal.so` / `manifold.so` /
    `pipe_proximity.so`（有効化したもの）
  - 実行体 → `$PREFIX/bin/srava`, `$PREFIX/bin/srava_agent`
- → install 後は**環境変数なしで** 各モジュールの op が使える。
- 開発中（install せず）は、ビルドツリーの `.so` が planner と同居する（探索路 ①）ので、
  そのまま `srava my.sra` で使える。別ディレクトリの `.so` を足すなら `SRAVA_MODULE_PATH` で指す:
  ```sh
  SRAVA_MODULE_PATH="/path/to/modules" srava my.sra
  ```

---

## cgal.so {#cgal}

**概要**: srava の**厳密（exact）幾何**モジュール。CGAL の Nef/多面体を EPECK（厳密構成）で駆動し、
3D ソリッドと 2D 断面のブール・生成・変換・計測を担う。全 op を持つ**上位互換**の基準実装で、
manifold.so の高速結果を無損失に読み込んで検算・精密加工にも使える（速度は manifold に劣る）。

**前提とする外部ライブラリ**:

| ライブラリ | バージョン / 取得 | 用途 |
|---|---|---|
| CGAL | **6.x**（`find_package(CGAL REQUIRED)`） | 幾何コア（Nef_polyhedron_3 / Polygon_with_holes_2 等） |
| GMP / MPFR | システム提供 | EPECK の厳密有理数演算 |
| Boost | システム提供 | CGAL 依存 |
| HDF5 | `find_package(HDF5 COMPONENTS C)`（`export_vox` 用のみ・**`openvdb_cg.so` が使う**） | ボクセル書き出し |

**サポートする型**（この 3 型を読み書き・ほかに点群型を [points.so](#points) から借りる）:

| 型名 | 4CC | 4CC(readonly) | 意味 | 幾何カーネル |
|---|---|---|---|---|
| `cg-mesh3d` | `MESH` | `MFM3` | 3D ソリッド mesh | CGAL EPECK |
| `cg-cross2d` | `PLY2` | `MFC2` | **z=0 の** 2D 領域（`Polygon_with_holes_2`） | CGAL EPECK |
| `cg-face3d` | `PLY2` | `MFC2` | **空間に置かれた** 2D 領域（枠つき） | CGAL EPECK |

★ **2D の型は 2 つある**。`cg-cross2d` は z=0 の簡易表現、`cg-face3d` は
**平面（枠）を持って空間に置かれた**同じ形。★ 変換 5 op は **z=0 平面を平面へ写すと分かるときは
`cg-cross2d` のまま**返り、面の外へ出るときだけ `cg-face3d` になる（2026-09-19）:

```
rotate(rect(2,3), "z", 90)   → cg-cross2d      translate(rect(2,3), [5,1,0]) → cg-cross2d
rotate(rect(2,3), "x", 45)   → cg-face3d       translate(rect(2,3), [0,0,2]) → cg-face3d
```

空間に置かれた 2D を `z=0` へ落とすには [`project_flatten`](srava_function_reference.html#project-flatten)
または `cast("cg-cross2d", …)` を書く（→ [2D 領域の 2 つの型](srava_language_reference.html#two-2d-types)）。

- **4CC** = 読み書きできる 4CC。**4CC(readonly)** = 読めるが書けない 4CC（`cg-mf-upgrade` codec が
  manifold の double→EPECK へ**無損失昇格**して読むのみ・書き出しは自 4CC のみ）。よって混成パイプで
  manifold 出力を cgal に流し込める。

**サポートする cast**（cgal.so の型を**目標**にする `cast(T, …)` が受けられる入力）:

| 目標型 | 入力 (4CC) | 経路 | 損失 |
|---|---|---|---|
| `cg-mesh3d` | `MESH`（自型） | 再エンコードのみ（実質 no-op） | なし |
| `cg-mesh3d` | `MFM3`（manifold 3D） | `cg-mf-upgrade`（double→EPECK 昇格） | **無損失** |
| `cg-cross2d` | `PLY2`（自型） | 再エンコードのみ（実質 no-op） | なし |
| `cg-cross2d` | `MFC2`（manifold 2D） | `cg-mf-upgrade`（double→EPECK 昇格） | **無損失** |

- 昇格は無損失なので `cast` 明示のほか**自動**（sig routing）でも起こる。次元（3d↔2d）は跨げない。
- 全型の相互変換表は[関数リファレンスの `cast`](srava_function_reference.html#cast)。

**サポートする op**（2D/3D 両対応・全 op を持つ。**太字**は cgal.so だけが持つ）:

```
box, boxa, import, prism, pyramid, sphere, icosphere,
cylinder, cone, torus, tetrahedron, empty2d, empty3d,
union, combine, intersection, difference,
export, translate, rotate, mirror, scale, transform, project_flatten,
color, rect, ngon, circle, polygon, line, extrude, tube_ruled, revolve, offset(2D のみ), hull, loft_ruled,
area, valid, repair, refine, remesh, simplify, section, volume, perimeter, centroid, bbox,
nverts, nfaces, nparts, nshells, genus,
part, part_at, shell, shell_at, vert, verts, face_verts,
distance, distance_at, closest, farthest, thin_spots, cast,
estimate_normals, delaunay, voronoi
```

★ **位相と片**: `nparts` / `nshells` / `genus` は **Nef へ変換せずに**数える
（定義は `src/h/common/meshprops.h`・掃引規模では Nef が 100GB 級になるため）。
`part` / `shell` は塊・殻を取り出し、`vert` / `verts` / `face_verts` は頂点を読む。
⚠ 空洞（シェルの入れ子）のように **nef にしか出せない形**は明示エラーで `nef` を名指す。

★ **点群から面を起こす 2 本**: `delaunay(p)`（2D 三角形 / 3D 四面体）と
`voronoi(p, box)`（セル）。どちらも EPECK で厳密に計算し、**片として並べて**返す
（`part(v, i)` が `voronoi` では *サイト `i` のセル* と定義で決まるのに対し、
`delaunay` の三角形番号は**実装依存**）。

★ **`estimate_normals(p[,k])`は点群の op** で、cgal の型（`cg-mesh3d` / `cg-cross2d`）とは
無関係。型 `pt-cloud3d` は中立の [points.so](#points) / `libsrava_pt` が持ち、cgal は
**そのクラスをそのまま借りている**（`cgal_provides` に `&ptCloud::WIRE` を 1 行足すだけ・
`occt_mf` が `mfGeom` を借りるのと同じ作法）。
cgal が持つ理由は、法線を要求する当の相手（Poisson / RANSAC）が CGAL にあり、
**向きなし推定（`pca_estimate_normals`）と向き付け（`mst_orient_normals`）が両方そろっている**ため
＝ 点群型の 2 つの印を 1 モジュールで正しく立てられるから。
⚠ カーネルは **EPICK（double）** — 点集合処理は厳密数と相性が悪く、点群の座標は測った値なので
EPECK へ上げる意味が無い。★ **`geogram.so` にも同じ op 名で実装がある**（`Co3Ne_compute_normals`）ので、
**非 GPL 構成でも法線推定はできる**。両方ロードしていれば priority で cgal（20 > 6）が受け、
`"geogram"::estimate_normals(p)` で名指しできる。
⚠ 印の根拠は cgal 版のほうが強い — `mst_orient_normals` は向き付けできなかった点を**返す**ので、
「全点を向き付けられた」ことを確かめてから印を立てられる。

cgal.so 固有の op: **line, repair, remesh, simplify, perimeter, distance,
closest, farthest, thin_spots**（manifold.so には無い）。
★ **`refine` / `remesh` / `simplify`**は三角形の張り方を作り直す 3 op で、どれも**形を保つ**。
`refine` は形も三角形の形も変えずに面密度だけ上げる（**EPECK のまま**回るので体積が有理数として
厳密に一致する・`manifold.so` にも同じ約束の実装がある）。残り 2 つは cgal 固有:
`remesh` は辺長を揃えて三角形の質を上げ（面数は増える）、`simplify` は面数だけを落とす
（三角形の質は下がる）。⚠ 中身は **EPICK（double）のコピー**で解いて EPECK へ戻す — 新しい頂点
位置を決める op なので厳密な答えというものが無く、CGAL の実装も浮動小数前提
（`LindstromTurk_cost` は EPECK では**コンパイルが通らない**）。
⚠ `export_vox` は 2026-09-01に **`openvdb_cg.so` へ移設**した（cgal の幾何とは無関係な
「メッシュ全般 → vox.h5」の仕事だったため）。さらに  で **`vd-grid3d` も受ける**ようになり、
1 つの h5 にカーネル混在レイヤを書ける。`tube_ruled` と `color` は  で manifold.so にも
移植済み（掃引の幾何は `src/h/common/tube.h`、色指定の解釈は `src/h/common/colorspec.h`、
色つき 3MF/AMF のライタは `src/h/common/mesh3mf.h` を両モジュールが共有する）。

各 op のシグネチャ・引数・例は[関数リファレンス](srava_function_reference.html)を参照（各項に実装モジュール・型を明記）。

---

## manifold.so {#manifold}

**概要**: srava の**高速（近似 double）幾何**モジュール。elalish/manifold を用い、多くの生成・ブール・
計測 op を CGAL より桁で速く処理する。cgal.so の op の**サブセット**を持つ。既定では in-proc（planner 内
スレッド）で走り、型が決まらない生成 op（`box` 等）の既定幾何カーネルにもなり得る。

**前提とする外部ライブラリ**:

| ライブラリ | バージョン / 取得 | 用途 |
|---|---|---|
| Manifold | **v3.5.2**（FetchContent `github.com/elalish/manifold`） | 幾何コア（3D mesh・double ベース） |
| clipper2 | Manifold 経由で取得 | 2D 断面（`mf-cross2d`）のブール |

**サポートする型**（この 3 型を読み書き）:

| 型名 | 4CC | 4CC(readonly) | 意味 | 幾何カーネル |
|---|---|---|---|---|
| `mf-mesh3d` | `MFM3` | `MESH` | 3D mesh | Manifold（double） |
| `mf-cross2d` | `MFC2` | `PLY2` | **z=0 の** 2D 領域 | Manifold / clipper2 |
| `mf-face3d` | `MFC2` | `PLY2` | **空間に置かれた** 2D 領域（枠つき） | Manifold / clipper2 |

- **4CC(readonly)**: `cast` 時のみ `MESH`（CGAL 3D）/ `PLY2`（CGAL 2D）を **readonly ダウングレード**で読む（`mf-cg-downgrade` codec が有理数→double 化・**損失**。有理数文字列のパーサは `src/h/common/exact_wire.h` に置いて **geogram.so と実体を共有**する）→ `cast("mf-mesh3d", cgMesh)` / `cast("mf-cross2d", cgCross)` とも成立。書き出しは自 4CC のみ。

**サポートする cast**（manifold.so の型を**目標**にする `cast(T, …)` が受けられる入力）:

| 目標型 | 入力 (4CC) | 経路 | 損失 |
|---|---|---|---|
| `mf-mesh3d` | `MFM3`（自型） | 再エンコードのみ（実質 no-op） | なし |
| `mf-mesh3d` | `MESH`（cgal 3D） | `mf-cg-downgrade`（有理数→double 降格） | **損失** |
| `mf-cross2d` | `MFC2`（自型） | 再エンコードのみ（実質 no-op） | なし |
| `mf-cross2d` | `PLY2`（cgal 2D） | `mf-cg-downgrade`（有理数→double 降格） | **損失** |

- 降格は損失を伴うため**自動では起こらず** `cast` の明示のみ。次元（3d↔2d）は跨げない。
- 全型の相互変換表は[関数リファレンスの `cast`](srava_function_reference.html#cast)。

**サポートする op**（cgal.so のサブセット・`offset` は 2D 専用）:

```
box, boxa, sphere, icosphere, prism, pyramid,
cylinder, cone, torus, tetrahedron, empty2d, empty3d,
union, intersection, difference, combine,
export, cast, polygon, revolve,
translate, rotate, scale, mirror, transform, project_flatten,
volume, area(2D), bbox(2D), centroid(2D), nverts(2D), nfaces(2D),
import, rect, circle, ngon, extrude, section, offset, tube_ruled, color,
hull, minkowski(3D・自型どうしのみ), loft_ruled, refine, simplify_cleanup
```

⚠⚠ **3D の計測・位相は `geomutils.so` へ移した**。`mf-mesh3d` に対する
`area` / `bbox` / `centroid` / `nverts` / `nfaces` と、`valid` / `nparts` / `nshells` / `genus` /
`part` / `shell` / `vert` / `verts` は [geomutils.so](#geomutils) が答えるので、
**`module("geomutils.so", {})` を併せて書く**（`include "module/all.sra";` にも入っている）。
`volume` だけは manifold 自身が持つ（`Manifold::Volume()`）。
⇒ 答えは移す前と**変わらない**（もともと共通実装 `src/h/common/meshprops.h` を通していた）。

★ **`refine`**は cgal.so と**同じ約束**の op（形は変えず全辺を `len` 以下にする）。
★ **`simplify_cleanup(m, tol)`** は manifold.so **固有**で、cgal の `simplify(m, n)` とは
*約束が逆向き*（面数ではなく**形のずれの上限**を指定する）。名前を分けてあるのはそのため
（→ [op 名の付け方](srava_function_reference.html#name-suffix)）。

**書き出せる形式**: `stl` / `off` / **`3mf` / `amf`**（3MF/AMF は cgal.so と同じ共通ライタで、
`color` の色と `unit` を保持する）。それ以外の形式（obj/ply/svg/dxf…）は cgal.so が引き受ける。
`color` の色は **頂点プロパティ ch3..5 (RGB 0-255)** として持つ（cgal の per-face `f:color` とは持ち方が
違うが、全体を一様に塗るので見え方は同じ）。`combine` で成分ごとの色が残るのも cgal と同じ
（片方だけが色を持つ場合は無色側を灰 180 にしてから合成する）。

⚠ **`combine`（`+++`）の意味論は cgal と違う**: manifold は重なりを**解消する**（実質 `union`）。
Manifold の値は常に妥当な 2-manifold 立体であることが型の不変条件で、「自己交差した 2 枚の殻を
そのまま持つ」表現が存在しないため（`Manifold::Compose` は v3.5.2 では `BatchBoolean(OpType::Add)`
そのもので deprecated）。**カーネルの不変条件による差なので manifold 側では埋められない**。
他方に完全に埋まった成分は吸収されて消えるので、可視化マーカは表面からはみ出す位置に置くこと。
詳細と数値例は[関数リファレンスの `combine`](srava_function_reference.html#combine)。

⚠ **`solidify` / `remesh` / `simplify` を持たない**（3 つとも上流を確かめた。理由は別々）:

- **`solidify`** — Manifold の型の不変条件が「常に妥当な 2-manifold 立体」なので、**自己交差した
  スープを受け取る入口が公開 API に無い**。⚠ ただし**原理的に不可能なわけではない**
  （arrangement + 巻き数で内外を決め直す手法は確立していて、現に geogram と nef がそれをやっている）。
  *「実装されていない」のであって「不可能」ではない*。
  ⚠ 自己交差した mesh を渡すと **エラーにならず、重なりを二重に数えた体積を返す**
  （半分重ねた 2 つの箱は、重なりを 2 回数えた体積になる。geogram の `solidify` は正しい答えを返す）。
  ★ `valid(m)` は 0 を返すので**検出はできる**。
- **`remesh`** — `Refine` / `RefineToLength` は**細分**であって等方リメッシュではない。頂点を動かさず
  辺も潰さないので、悪い三角形は分割されても悪いまま。薄い板を細かく割っても**最小角は変わらず**
  （下位 10% は改善する）、箱では**かえって悪化**する。
  ★ 一方で**形はまったく変わらない**（体積・面積が厳密に不変）ので、「同じ形のまま面密度だけ上げる」
  道具としては素直。⇒ そこだけを **`refine(m, len)`** として配線した（cgal にも同じ約束の実装がある。
  ⚠ 上流に「`len` 以下」の保証は無い — 分割数が**切り捨て**なうえ、**内部頂点**の追加で辺の分割数から
  決まらない辺ができるため、最長辺が要求を超えて散る。srava 側で*作った結果を測り、
  超えていたら詰めて作り直して*いる。⇒ 面数は cgal 版と一致しないが、**op の約束の方を揃えた**）。
  **`remesh` としては配線しない**。
- **`simplify`** — `Simplify(tolerance)` は許容差以下のエッジを潰すもので、**目標面数を指定できない**。
  srava の `simplify(m, n)`（面数を指定・形は保つ）の約束は満たさない。
  ★ ただし上流の約束自体は明快で、*「結果は元の頂点の部分集合で、どの面も `tol` 未満しか動かない」*
  = **有界誤差の掃除**として筋が通っている。⇒ **`simplify_cleanup(m, tol)` として拾った**
  （ブールのあとに残る極小辺・針状三角形を落とす用途）。
  ⚠ `tol` を上げるほど面数は落ちるが**体積も一緒に落ちる**ので、面数だけ振る測定軸には使えない。

`tube_ruled` は cgal.so と**同じ共通ヘッダ**（`src/h/common/tube.h`）で掃引を生成するので、頂点座標・三角形の
並びが両モジュールで一致する（`sphere`/`icosphere` の `geodesic.h` と同じ方針）。体積は cgal が厳密有理数で
積んでから丸めるのに対し manifold は double で積むため最下位 1 ulp 程度ずれる（2D は合併エンジンが
Polygon_set_2 と clipper2 で異なるのでもう少し緩い）。

各 op のシグネチャ・引数・例は[関数リファレンス](srava_function_reference.html)を参照（各項に実装モジュール・型を明記）。

---

## nef_snc.so / nef_hybrid.so {#nef}

**概要**: CGAL の **Nef_polyhedron_3** を土台にした厳密幾何モジュール。**同一ソースから作る 2 変種**で、
ワイヤ表現（内部の Nef 構築方式）だけが違う。要件は **Nef 型を維持したまま op を連鎖させる**こと —
ブール op は Nef のまま結果を返し、境界表現へ戻すのは `volume` / `export` / キャッシュ書き出しだけ。
そのため `complement`（補集合）や `minkowski`、`convex_decomposition` のような **Nef でしか素直に書けない
op** を持つ。**3D の `offset` を引き受ける唯一のカーネル**でもある。

> ⚠ 2 変種は**同一ソース**なので、記述子シンボルの衝突を避けるため `HIDDEN`（可視性 hidden）で
> ビルドされる。どちらを使うかは `module()` で選ぶ。

**前提とする外部ライブラリ**:

| ライブラリ | バージョン / 取得 | 用途 |
|---|---|---|
| CGAL | **6.x**（`find_package(CGAL REQUIRED)`・`SRAVA_MODULE_CGAL=ON` が前提） | `Nef_polyhedron_3` |
| GMP / MPFR | システム提供 | EPECK の厳密有理数演算 |

**サポートする型**:

| モジュール | 型名 | 4CC | 4CC(readonly) | 意味 |
|---|---|---|---|---|
| `nef_snc` | `nf-mesh3d` | `NEF3` | `NEFB` | SNC 表現の Nef 多面体 |
| `nef_hybrid` | `nfb-mesh3d` | `NEFB` | `NEF3` | hybrid 表現の Nef 多面体 |

- 互いの 4CC を **readonly で読める**ので、片方が書いた結果をもう片方に流せる。
- **他カーネルへも出せる**（2026-09-06 拡張）。`nef_hybrid` は有界・2-多様体の値を
  厳密境界形式だけで書き、それ以外でも**境界が取れる限り** SNC の後ろに厳密境界を併記する。
  ⇒ `cast("mf-mesh3d", x)` / `cast("cg-mesh3d", x)` は行き先がその境界を読んで通る。
  `nef_snc` は**常に SNC だけ**を書く（併記はやめた。書き出しのたびに境界を
  作らせるので代償が大きい）。⇒ こちらの `cast` は橋渡しモジュール
  [`nef_cg.so` / `nef_mf.so`](#nef_bridge) が担い、境界を作る代償は **`cast` のときだけ**払う。
  - ★ 併記の条件は「2-多様体か」ではなく **「境界表現を取れるか」**。`nef` は marked volume
    ごとの**全シェル**から境界を作れるので、稜だけで接する 2 立体の和や `convex_decomposition`
    の結果（どちらも 2-多様体ではない）も出せる。境界では各塊が**別の連結成分**として出る。
  - ⚠ 通らないのは **非有界**（`complement` の結果など）だけ。行き先の型に表現が無いためで、
    明示エラーになり理由も出る。
  - ⚠ 2-多様体でない値を `nef_hybrid` が**境界だけ**で書くことはない。境界だけを書き戻すと
    内部の仕切り面が消え、`convex_decomposition` の結果が 1 塊に化ける（点集合は同じでも
    `nparts` / `part` の答えが変わる）ため、SNC を本体・境界を付録として両方書く。
  `manifold.so` / `cgal.so` が読んでいるのは併記された境界であって **SNC ではない**
  （どちらも SNC のパーサを持たない）。
  - ⚠  で前提が 1 つ変わった。「`cgal.so` を CGAL Nef 非依存に保つ」方針は
    畳まれ、`cgal.so` が使う幾何ライブラリは CGAL Nef を含むようになった（上流が
    corefinement と Nef をはっきり分けられないため）。**振る舞いは変わっていない** ——
    `cgal.so` は依然 SNC を読まず、`nef_snc` からの変換は橋渡しモジュールが担う。
    `manifold.so` は引き続き **CGAL 非依存**（GPL 非汚染）。

**サポートする op**:

| 分類 | op |
|---|---|
| 生成 | `box` / `boxa` / `sphere` / `icosphere` / `prism` / `pyramid` / `cylinder` / `cone` / `torus` / `tetrahedron` / `tube_ruled` / `empty3d` |
| 入力 | `import`（`stl` / `off`） |
| ブール | `union` / `intersection` / `difference` / `complement` |
| 凸包 | `hull` |
| Nef 固有 | `minkowski` ※ / `offset`（**3D**）/ `convex_decomposition` / `nparts` ※2 / `part` / `unify` / `solidify` |
| 変換 | `translate` / `rotate` / `scale` / `mirror` / `transform` / `cast` |
| 計測 | `nverts` / `nfaces` / `volume` / `bbox` / `centroid` / `area` / `valid` |
| I/O | `export` |

※2 `nparts` は 2026-09-13以降 **`cgal` / `manifold` / `geogram` も持つ**（塊 = SNC の marked volume
という約束はそのまま。メッシュ系は符号つき体積が正のシェルを数えて同じ数を出す）。
**Nef へ変換せずに数えられる**ことが要点で、掃引規模のメッシュでは変換が 100GB 級になり事実上使えなかった。
なお `part`（取り出し）はシェルの入れ子関係が要るので **nef だけ**のまま。

※ `minkowski` は 2026-09-12以降 **`manifold.so` も持つ**（`mf` どうしだけ）。nef が受け持つのは
自型・`cg` / `gg` 由来・異カーネル混成で、そこは変わっていない。中身は別物で、nef は凸分解 + 厳密有理数、
manifold は三角形ごとの凸包 + `BatchBoolean`。

**実行方式**: `PROCESS` のみ（`exec_default=process`）。Nef も EPECK 上に構築されるので値が共有に耐えない。

## geogram.so {#geogram}

**概要**: srava の **3 つめの厳密幾何**モジュール。Bruno Lévy の
[geogram](https://github.com/BrunoLevy/geogram)（BSD-3）を用い、**mesh arrangement + 厳密述語 / 厳密構成**で
二項ブールを行う（arXiv:2405.12949 "Exact predicates, exact constructions and combinatorics for mesh CSG"）。
CGAL corefinement と**同じ厳密解**を出しながら、多重ブールで**桁違いに速い**のが特徴。

> **既定 ON**（2026-08-31 以降）。`cmake -DSRAVA_MODULE_GEOGRAM=OFF` で外せる。
> ⚠ 外部から取得してビルドするので**取得 + ビルドに時間がかかる** — 軽く済ませたいときは OFF。

**前提とする外部ライブラリ**:

| ライブラリ | バージョン / 取得 | 用途 |
|---|---|---|
| geogram | **v1.10.0**（FetchContent `github.com/BrunoLevy/geogram`・**静的リンク**） | 幾何コア（arrangement・厳密述語） |
| OpenMP | システム（`find_package(OpenMP)`） | geogram 内部のスレッド管理 |

> ⚠ geogram 同梱の **TetGen（AGPL）** と **Triangle（非商用限定）** は既定 ON なので、srava 側で明示的に
> OFF にしている（srava は GPLv3 なので AGPL を引き込むと配布条件が変わる）。使うのは BSD-3 の本体だけ。

**サポートする型**:

| 型名 | 4CC | 意味 | 幾何カーネル |
|---|---|---|---|
| `gg-mesh3d` | `MFM3`（manifold と共有） | 3D mesh | geogram（厳密演算・座標は double） |

- **wire 形式は manifold と完全に同一**（`[u32 nv][u32 nt]` + 頂点 double×3 + 三角形 u32×3）。
  geogram のブールは厳密だが**結果メッシュの頂点は double に落ちる**（EPECK のように有理数を持ち回らない）ため。
- ★**4CC は形式の名前であって型の名前ではない**ので、形式が同じなら**同じ 4CC を共有する**。
  型の区別（`gg-mesh3d` / `mf-mesh3d`）は
  codec 行の `types` の申告と**型スタンプ**が担い、キャッシュの弁別は
  **キャッシュソルト**(モジュール名 + `cache_version`・`--module-info` の `cache_salt` 行)が担うので衝突しない。
  おかげで、cgal が geogram の値を読む経路（`cg-mf-upgrade`）も manifold と共通の 1 本で済む。
  > 逆に、4CC を分けたままにすると「同じ形式に 2 つの名前がある」状態が残り、読み側の codec が
  > モジュールの数だけ増える。
- ★**4CC(readonly)**: `MESH`（cgal の厳密有理数テキスト）を **readonly の昇格読み**で受ける
  （`gg-cg-upgrade` codec・2026-08-19）。有理数文字列 → double へ落とすので**損失**変換だが、
  座標がもともと double だった値（共通生成器 `common/geodesic.h` の球など）は往復しても
  **bit 一致**する。パーサは `src/h/common/exact_wire.h` に切り出して manifold と**同じ実体を共有**
  しているので、geogram.so は **CGAL をリンクしない**。書き出しは自 4CC（`MFM3`）のみ。
  - これにより `cast("gg-mesh3d", cgMesh)` と `solidify(cgMesh)` が成立する。
  - `PLY2`（cgal 2D）と `NEFB`（nef 境界）は申告しない — geogram モジュールに 2D 型は無く、
    NEFB は必要になってから（読めないものを申告しない）。

**サポートする op**:

| 分類 | op |
|---|---|
| 生成 | `box` / `boxa` / `sphere` / `icosphere` / `prism` / `pyramid` / `cylinder` / `cone` / `torus` / `tetrahedron` / `tube_ruled` / `empty3d` |
| 入力 | `import`（`stl` / `off`） |
| ブール（二項） | `union` / `intersection` / `difference` |
| 凸包 | `hull` |
| 変換 | `translate` / `rotate` / `scale` / `mirror` / `transform` / `cast` |
| 計測 | `volume` / `distance_at`<br>⚠⚠ **それ以外（`nverts` / `nfaces` / `bbox` / `centroid` / `area` / `valid` / `nshells` / `nparts` / `genus`）は [geomutils.so](#geomutils) へ移した**。答えは移す前と変わらない（もともと共通実装 `src/h/common/meshprops.h` を通していた）が、**`module("geomutils.so", {})` を併せて書く**必要がある<br>★ : 位相の 3 本は **Nef へ変換せずに**数える<br>★ `distance_at` は geogram が `MeshFacetsAABB` で自前に持つので geomutils は名乗らない（速い実装を奪わない） |
| 出力 | `export`（`off` / `stl` / `obj` / `ply`） |
| 点群 | `estimate_normals`（型 `pt-cloud3d` は [points.so](#points) から借りる） |
| ★固有 | **`solidify`** |

- 基本立体は共通生成器（`common/geodesic.h` / `common/solids.h`）を使うので、頂点と面の並びが
  cgal / manifold / nef / cherchi と一致する。掃引管 `tube_ruled` も共通（`common/tube.h`）。
  ただし**体積は最下位桁がずれる**（geogram は発散定理を double で積む・cgal は厳密有理数を積んで最後に丸める）。
- ★**`estimate_normals(p[,k])`**は **cgal.so と同じ op 名の別実装**
  （`Co3Ne_compute_normals(M, k, reorient=true)`）。両方ロードしていれば priority で cgal（20 > 6）が
  受け、`"geogram"::estimate_normals(p)` で名指しできる。
  ⇒ **cgal（GPL）を入れない構成でも法線推定ができる**。型 `pt-cloud3d` は geogram のものではなく
  中立の `libsrava_pt` が持ち、`geogram_provides` に `&ptCloud::WIRE` を 1 行足して借りているだけ。
  ⚠ 印の根拠は cgal 版より弱い — `reorient_normals` は向き付けできなかった点を報告しないので、
  kNN グラフが複数成分に割れる点群では**成分ごとの符号が揃わないまま印が立ちうる**。
  ⚠⚠ Co3Ne は **CmdLine の変数を読む**（`co3ne:*` / `algo:nn_search` / `log:*`）。宣言されていない
  変数を引くと geogram は **assert で落ちる**ので、呼ぶ前に `import_arg_group("standard"/"algo"/"co3ne")`
  が要る（実際に踏んだ: `environment.cpp:217 "Assertion failed: variable_exists"`）。
- ★**`solidify(m)`** — 自己交差した閉メッシュから**内外を決め直して**ソリッドにする。arrangement で交差を解き、
  radial sort で外側シェルだけを残す。**cgal は自己交差を素通りして誤った体積を返し、manifold も同じ誤値、
  nef は SNC を組めない**ので、これは geogram を入れる質的な理由のひとつ（→）。
  nef の同名 op（別実装）と独立に同じ値を出すことを回帰テストで固定している。
- ★**多オペランドに対応済み**。geogram は facet 属性 `operand_bit` で **N 項ブール**を
  持っており、`module("geogram.so", {arity: N})` で「何項を 1 回の arrangement に渡すか」を選べる
  （上限 32 = `operand_bit` の幅）。既定は二項。
- ⚠ **`remesh` / `simplify` は持たない**（上流を確かめた）。ライブラリには近いものが入っているが、
  srava の同名 op の約束（**形を変えない**）を満たさない:
  - `GEO::remesh_smooth` は CVT（Lloyd + Newton）による**再標本化**で、**特徴を保護しない**。
    箱にかけると角が落ちて体積・面積が痩せ、薄い板では**体積が 2 割ほど失われる**。
    最小角は確かに良くなるが、**形が変わってしまう**ので cgal の `remesh`（箱なら体積・面積が
    ちょうど保たれる）とは同じ約束にできない。加えて引数が**辺長ではなく目標点数**。
  - `GEO::mesh_decimate_vertex_clustering` は**格子ビン数**を指定する粗い間引きで、**面数を指定できない**。
    ビン数を粗くしていくと、ある点までまったく効かず、効き始めると**体積ごと落ちる**。
    `simplify(m, n)` の約束（面数を指定・体積は保つ）とは別物。

---

## cherchi.so {#cherchi}

**概要**: srava の **4 つめの厳密幾何**モジュール。Cherchi らの
[Interactive and Robust Mesh Booleans](https://github.com/gcherchi/InteractiveAndRobustMeshBooleans)（MIT・以下 IRMB）を用いる。
geogram と同じ **mesh arrangement** 系だが、厳密性の作り方が違う — 交点の座標を明示的に構成せず、
**indirect predicates**（「どの 3 平面の交わりか」という間接表現）のまま厳密述語を評価するので、
有理数展開を避けたまま判定できる（SIGGRAPH Asia 2020 / ACM TOG 2022）。

> **既定 ON**（2026-08-31 以降）。`cmake -DSRAVA_MODULE_CHERCHI=OFF` で外せる。
> ⚠ **Cygwin では自動 OFF**（依存の abseil が拒否する）。

**前提とする外部ライブラリ**（すべて GPLv3 と両立・詳細は THIRD_PARTY.md）:

| ライブラリ | バージョン / 取得 | 用途 |
|---|---|---|
| IRMB + arrangements | commit `7bd6c26`（FetchContent・**ヘッダのみ**） | 幾何コア（arrangement・ブール分類） |
| Indirect_Predicates | 同梱 submodule（LGPL-2.1・ヘッダのみ） | 間接表現の厳密述語 |
| Cinolib | 同梱 submodule（MIT・ヘッダのみ。Eigen を同梱している） | octree・ベクトル型 |
| Shewchuk predicates | 同梱（Cinolib の external・**K&R C**） | 浮動小数の厳密述語 |
| oneTBB | **システム**（`find_package(TBB CONFIG)`） | op 内並列 |

> ★ 上流の `CMakeLists.txt` は **add_subdirectory しない**（`SOURCE_SUBDIR` に CMakeLists.txt の無い
> ディレクトリを指して取得だけさせる）。上流のそれは実行体を 5 本と **oneTBB を自前で建てる**ので、
> そのまま取り込むと 1 プロセスに TBB が 2 つ入る（OpenVDB / OCCT と同じ「TBB はシステムから 1 つ」の原則違反）。
> IRMB 自体はヘッダだけなので、include パスと `shewchuk.c` 1 本だけを取ってこちらでターゲットを組む。

> ★ コンパイル条件: `-frounding-math`（述語が IEEE 754 の丸め方向に依存する）と
> `-ffp-contract=off`（FMA の縮約で誤差なし変換が壊れるのを防ぐ）。AVX2 は有れば使う。

**サポートする型**:

| 型名 | 4CC | 意味 | 幾何カーネル |
|---|---|---|---|
| `ch-mesh3d` | `MFM3`（manifold / geogram と共有） | 3D mesh | cherchi（厳密述語・座標は double） |

- wire 形式は manifold / geogram と同一。結果メッシュの頂点は **double に落ちる**（geogram と同じ精度クラス）。
- **4CC(readonly)**: `MESH`（cgal の厳密有理数テキスト）を昇格読みで受ける（`ch-cg-upgrade`）。
  パーサは `src/h/common/exact_wire.h` を共有するので、cherchi.so は **CGAL をリンクしない**。

**op**: `box` / `boxa` / `sphere` / `icosphere` / `prism` / `pyramid` / `cylinder` / `cone` / `torus` /
`tetrahedron` / `tube_ruled` / `empty3d` / `import`（stl,off）/ `union` / `intersection` / `difference` /
`volume` / `export`（off,stl,obj）/ `cast` /
`translate` / `rotate` / `scale` / `mirror` / `transform`。

⚠⚠ **計測・位相は [geomutils.so](#geomutils) が引き受ける**。`nverts` / `nfaces` /
`bbox` / `centroid` / `area` / `valid` / `nparts` / `nshells` / `genus` / `part` / `shell` /
`vert` / `verts` / `distance_at` を `ch-mesh3d` に対して呼ぶには
**`module("geomutils.so", {})` を併せて書く**（`include "module/all.sra";` にも入っている）。
★  でいちばん大きく埋まったのが cherchi — それまで位相 op が 1 つも無かった。

- 基本立体と掃引管は共通生成器（`common/solids.h` / `geodesic.h` / `tube.h`）なので、頂点と面の並びが
  他カーネルと一致する。
- ★ ブールの**オペランドが空**（面を 1 つも持たない）のときは、arrangement へ渡す前に
  **集合演算として畳む**（`A ∩ ∅ = ∅` など）。cherchi は全オペランドを 1 本のソウプへ連結して
  三角形ごとに label を振る方式なので、そのまま渡すと「そのオペランドは最初から無かった」ことになり、
  空集合が `{}`（fold の中立元）と区別できなくなる。

**⚠ 既知の限界 — オペランドの配置が退化していると壊れる**:

| # | 配置 | 何が起きるか | srava 側の扱い |
|---|---|---|---|
| ① | **面でちょうど接する**（体積の重なりが 0） | 誤った値になる（共有壁が両側から残り、体積が過大になる） | ⚠ **検出できない**（境界辺が残らない）→ 既知の限界。`contact` テストで可視化 |
| ② | **多重に重なる**（3 重以上が同じ領域に） | 結果が壊れる（Release ビルドでは静かに進む） | ★ 結果に必ず**境界辺**（逆向きの相手がいない有向辺）が残るので `ch_has_no_boundary()` が **エラー**にする |

- 限界は「**測度 0 の接触**」に局在している。ごくわずかにずらせばどちらも正しく解ける。
  各オペランドは上流の入力要件（manifold / watertight / 自己交差なし / 向き付き）を満たしていても
  起きるので、**入力の不正ではなく配置**の問題。
- ⇒ **priority は 3**（既定 routing に入れない）。CAD 的な使い方では「面で接する立体の和」は
  普通に出てくるので、既定にすると黙って誤る。**明示して使うカーネル**として置いてある。
- ⇒ モデルは **一般の位置**で書く（接触ちょうどを避ける）。

**op 内並列**: `module("cherchi.so", { threads: N })` で 1 op あたりの上限を絞れる（`N<=0` で解除）。
IRMB のブールは `tbb::parallel_for` を直に呼んでおり、**上流にコンパイル時スイッチが無い**
（srava が渡す `TBB_PARALLEL` は Cinolib の octree にしか効かない）。そこで呼び出し側を
`tbb::task_arena` で囲んで絞っている（openvdb と同じ手法）。
⚠ `task_arena` は**スレッドプールを縮めない**ので、効いたかは**スレッド数や wall ではなく
CPU 時間**で見ること。

**⚠ `solidify`は持たない**。IRMB の分類は「**他の label の内側か**」で決まるため、
自己交差した *1 枚の* メッシュには効かない（重なる 2 箱を 1 ラベルで union させても内側の面が落ちない）。
**汚い入力を食えるのは arrangement までで、内外の決め直しは label 側の話**である。
連結成分ごとに label を振れば成分どうしの自己交差は解けるが、 の tube_ruled は 1 成分なので救えない。
`solidify` は geogram / nef が持つ。

**テスト**: `srava_cherchi_{bool,arity,contact,guard,mfcross,cgcross}` と、カーネル一致
`srava_agree_cherchi_{box,sphere,union,difference}`（基準は cgal・leaf は 1e-12 / ブールは 1e-9）。

## openvdb.so {#openvdb}

**`-DSRAVA_MODULE_OPENVDB=ON`（既定 ON・2026-08-31 以降。⚠ ビルドが重い / Cygwin では自動 OFF）**。OpenVDB（AcademySoftwareFoundation・Apache-2.0）を
FetchContent で取得して静的リンクする（規約 B）。

> ★ **TBB だけは共有ライブラリ**として `$PREFIX/lib` へ install する。TBB は**スケジューラの状態を
> 持つランタイム**なので、静的リンクして複数の `.so` に埋めると 1 プロセスに**スレッドプールが
> 2 つ**できる（`nef_snc` / `nef_hybrid` で踏んだシンボル衝突の裏返し）。将来 Manifold `PAR=ON`
> も TBB を使うため、**ビルド全体で 1 インスタンス**を規則とした。
> OpenVDB core の `find_package(TBB REQUIRED)` は切れない（トップレベルの `USE_TBB` は
> NanoVDB 専用）。Boost は `OPENVDB_USE_DELAYED_LOADING=OFF` で外し、Blosc も OFF にしている。

**これは第 3 の表現クラス**。三角形メッシュでも B-rep でもなく、格子点に**符号付き距離**を持ち、
**表面は値 0 の等値面として暗黙に定義される**。ブールは**点ごとの min/max だけ**なので
**位相の場合分けが存在しない** — 自己交差・非多様体・汚い入力でも必ず答えが出る。代償は
**解像度が全て**であること（格子間隔以下の薄板や鋭いエッジは消える）。

**サポートする型**:

| 型名 | 4CC | 意味 |
|---|---|---|
| `vd-grid3d` | `VDB ` | 疎な符号付き距離場（OpenVDB ネイティブのシリアライズ） |
| `mf-mesh3d` | `MFM3` | 出入り口のメッシュ（`voxelize` の入力 / `isosurface` の出力） |

- wire 形式は `[u64 len]` + `openvdb::io::Stream` の出力。中立形式を自前定義しなかったのは
  **読み手が居ない**ため — 暗黙 cast を持たない以上、`vd` のキャッシュを読むのは `vd` だけ。

**サポートする op**:

| 分類 | op |
|---|---|
| 生成 | `box` / `boxa` / `sphere` / `icosphere` / `prism` / `pyramid` / `cylinder` / `cone` / `torus` / `tetrahedron` / `tube_ruled` / `empty3d`<br>★ **どれも末尾に `dx`（ボクセルサイズ）を取る**（`sphere(r, dx)` / `prism(n, h, r, dx)` …）。ボリューム表現に分割数は意味を持たず、合成は transform の一致を要求するので **省略できない** |
| 入力 | `import(path, dx)`（`stl` / `off`） |
| 出入り | `voxelize(mesh, dx)` / `isosurface(v, iso)` |
| ブール（二項） | `union` / `intersection` / `difference` |
| **点群を切る** | `intersection(点群, 距離場, mode)` / `difference(点群, 距離場)`<br>★ 判定は **距離場の符号**。境界の帯は **0.75 ボクセル**（`geomutils` の相対許容差 1e-12 より厚い ＝ この表現の精度そのもの）<br>⚠ **可換ではない**（`intersection(距離場, 点群)` の順は受け付けない）。約束の全文は [関数リファレンス §点群を形で切る](srava_function_reference.md#ptsplit)<br>⚠ `distance_at` は使えない（符号を落とし、狭帯域の外で明示エラーになるため）。帯の外でも **符号は生きている**ので内外は言える |
| 加工 | `offset(v, d)` / `renormalize(v[, halfWidth])` |
| アフィン変換 | `translate` / `rotate` / `scale` / `mirror` / `transform`（引数の解釈はメッシュ系と同一）<br>★ **格子は元のまま・ボクセルを焼き直す**（`transform` を差し替えた shallow copy を作り、元の格子へ `resampleToMatch`）。⇒ ブールの前提（2 つの level set が**同じ transform** を持つ）と等方 voxel の前提が破れない<br>★ 非等方な `scale` や縮小でも正しく焼き直される（狭帯域が薄くなる懸念は、`resampleToMatch` が帯の幅を**出力側から決め直す**ので起きない） |
| 計測 | `volume` / `voxels` / `bbox` / `centroid` / `area` / `valid` / `distance_at`<br>★ : **距離が場そのもの**なので探索が要らない。⚠ 狭帯域の外は `background` に飽和するので明示エラー<br>⚠ `valid` は共通定義の ①（空でない）だけを見る。②（閉じている）③（自己交差が無い）は **距離場では構造的に恒真** |

- ★`voxelize` の第 2 引数は**分割数ではなくボクセル間隔 `dx`**。OpenVDB の CSG は 2 つの
  level set が**同じ transform を持つことを要求する**ので、形ごとに `dx` がばらつくと
  resample が要る。格子が違う 2 つのブールは**黙って計算せず明示エラー**にしている。
- ★`offset` の第 3 引数（近似球の細分化）は**無視する**。メッシュ系の 3D offset が球との
  Minkowski 和で実装されているためのパラメータで、距離場には近似球が無い。

### op 内並列を絞る — `module(so, {threads: N})` と `SRAVA_OP_THREADS`

op 内並列の上限は **2 通り**で指定できる。どちらも「**1 つの op が使ってよい上限**」で、
プロセス全体の上限ではない。

| 指定 | 効き方 |
|---|---|
| `module("openvdb.so", { threads: N })` | 記述子の `configure` フック経由。`N<=0` は制限を解除して既定へ戻す |
| 環境変数 `SRAVA_OP_THREADS` | 起動時に既定値として読む（oneTBB には公式の環境変数が無いのでモジュール側で受ける） |

### 形態 (dilate / erode / open / close) — **専用 op は作らない**（2026-09-12）

⚠  の表の「形態 = `lib`（未配線）」は **弱すぎる主張**だった。`offset(v, d)` の実体が
`LevelSetFilter::offset` で、これが **dilate / erode そのもの**（`d>0` で膨張・`d<0` で収縮）。
⇒ 無かったのは **open / close の合成**だけで、それは srava の式として書ける:

<pre>
open(v, r)   = offset(offset(v, -r), r)     細い橋・突起を落とす
close(v, r)  = offset(offset(v,  r), -r)    細い隙間・穴を埋める
</pre>

★ **「閾値より細い特徴だけが消える」ことを確かめてある**（2 つの箱を細い橋で繋いだ形で、
橋の厚みを `2r` の上下に振る）:

| 入力 | `open` の結果 |
|---|---|
| 箱 2 個だけ | 基準 |
| ＋橋が `2r` より薄い | 箱だけと**一致** ⇒ 橋が消えた |
| ＋橋が `2r` より厚い | 箱だけより**大きい** ⇒ 橋は残った |

閉形式（Steiner の公式で「箱を `r` で open した体積」）との差はボクセル誤差の範囲に収まる。
⇒ *式で十分*。

- ⚠ **`renormalize` を挟まないこと**。`offset` の出力は真の距離場ではない（`|∇φ|=1` が崩れる）ので
  「2 回目の `offset` の前に距離場へ戻すべき」と考えたが、**かえって閉形式から離れた**。
  帯の再構築で界面がわずかに動くため。⇒ 素直に 2 回 `offset` する方が近い
- ★ **`dilateActiveValues` / `erodeActiveValues` とは別物**。あちらは*格子の活性ボクセルの管理*
  （帯の広げ縮め）で、形の膨張収縮ではない。名前が似ているので混同しないこと
- ⚠ コストは `|d| / (0.5·dx)` に比例する（`LevelSetFilter::offset` が半ボクセルずつ進める）。
  ⇒ open/close は `offset` 2 回ぶん。`r` を大きく取ると効いてくる

### ⚠ `solidify` を持たない — 配線しない判断（2026-09-12）

 では「メッシュ → 距離場 → 再メッシュで `solidify` を配線するか判断する」が課題だった。
**意味論としては成立するが、配線はしない**。

- ★ **意味は合っている**。 で voxelize の入口に入れた**巻き数の interiorTest** は
  「0 でなければ内側」で符号を決めるので、*自己交差した閉曲面では「2 回巻いていれば内側」*
  = `solidify` がやっている判定とまったく同じ規則になる。半分重ねた 2 つの箱（1 枚のスープ）で
  確かめると、素の `volume` は二重に数えるのに対し、`solidify` と `voxelize` はどちらも
  重なりを 1 回だけ数えた値を返す。

- ⚠ しかし **近似**である（`dx` の誤差が乗る）。`solidify` を持つ 2 実装（nef / geogram）は
  **独立実装で下位桁まで一致**しており、そこへ桁の粗い 3 つめを同じ op 名で入れると、
  答えの質が**入力型で変わる**ことになる。
- ⚠ しかも **`dx` が要る**ので `solidify(m)` のシグネチャに収まらない。
- ⇒ 配線しない。**この経路は既に `voxelize(m, dx)` として明示的に書ける**（上で確かめたのはまさにそれ）ので、
  ボクセル側で解きたい人は*そう書けばよい*。隠して呼ばせる理由が無い。

## openvdb_mf.so / openvdb_cg.so / openvdb_gg.so {#openvdb_bridge}

**概要**: **openvdb とメッシュ系の橋渡しモジュール**。ボリューム（`vd-grid3d`）とメッシュの間を渡す
`voxelize` / `isosurface` の 2 op だけを持ち、相手側のメッシュ型ごとに 1 本ずつ用意されている。

| モジュール | 相手のメッシュ型 |
|---|---|
| `openvdb_mf.so` | `mf-mesh3d`（manifold） |
| `openvdb_cg.so` | `cg-mesh3d`（cgal） |
| `openvdb_gg.so` | `gg-mesh3d`（geogram） |

> ★ **なぜ本体から分けてあるか**: 型変換は**両側の本物のクラスを知っていなければならない**。
> この 3 本は変換だけを担い、両側の本物のクラス（`mfMesh` / `vdGrid`）を直接使う。

> ⚠ 幾何クラス `vdGrid` は共有ライブラリ **`libsrava_vd.so`** に置かれ、`openvdb.so` と
> この 3 本が**同じ実体**を共有する。使うときは `module("openvdb.so", {})` と橋渡しの
> **両方**をロードする。

**サポートする型**: `vd-grid3d`(`VDB `) ⇄ `mf-mesh3d`(MFM3) / `cg-mesh3d`(MESH) / `gg-mesh3d`

**サポートする op**: `voxelize`（メッシュ → ボリューム）/ `isosurface`（ボリューム → メッシュ）
／ **`openvdb_cg.so` だけ `export_vox`** も持つ（メッシュ全般・`vd-grid3d` → `vox.h5`。 で
`cgal.so` からここへ移した — cgal の幾何とは無関係な仕事だったため。 で `vd-grid3d` も受ける
ようになり、1 つの h5 にカーネル混在レイヤを書ける。⚠ HDF5 が要る）

**実行方式**: `THREAD｜PROCESS`・既定 `process`（本体 openvdb.so と同じ。→ [並列性](#exec)）

## occt.so {#occt}

> ### ⚠ 2026-09-12 より前に測った `occt` の「欠測（`rc != 0`）の数」は、これ以降のものと比較できない
>
> それまでの `occt` は、BOPAlgo が融合に失敗しても `IsDone()` を真にしたまま、
> **材料を落とした形を普通の値として返して**いた。 がこれを bbox 包含で捕まえて
> 明示エラーにするので、**これまで成功と数えていた run の一部が `rc != 0` になる**。
>
> ★ **劣化ではない。黙っていたものが出てきただけ**である。両者の欠測数を並べて
> 「退行した」と読まないこと。

> ## ★ `occt` の 2D — `oc-face3d`（2026-09-01）
>
> `occt.so` は **3 つの型**を名乗る（ほかに点群型を [points.so](#points) から借りる）:
>
> | 型 | 4CC | 中身 |
> |---|---|---|
> | `oc-brep3d` | `BREP` | B-rep ソリッド |
> | **`oc-face3d`** | **`BRP2`** | **面**（`TopoDS_Face`・輪郭は Bezier / B-spline のまま）。 以降は平面に限らず、**曲面上に切り取られた面**も持てる＝*空間に置かれた 2D*（一般表現） |
> | **`oc-cross2d`** | `BRP2` | **z=0 の簡易表現**。 で型名として戻した — 陰線処理（`hlr`）の出力は**投影面の上の平らな図面**で、その状態は確かに存在するため |
>
> ⚠ **4CC は `BRP2` 1 つを 2 型で共有する**（型は planner が載せるスタンプが持つので blob を
> 分ける必要がない）。cgal が `PLY2` を `cg-cross2d` / `cg-face3d` で共有しているのと同じ形。
> 2D の 4 規約は cg / mf と共通（→ [2D 領域の 2 つの型](srava_language_reference.html#two-2d-types)）。
>
> ⚠ 「OCCT に 2D が無い」は正確には **2D の位相が無い**という意味。曲線側
> （`Geom2d_BezierCurve` / `Geom2d_BSplineCurve` / `GCE2d_*` / `Geom2dAPI_*` / `BRepBuilderAPI_MakeEdge2d`）
> は揃っているので、「2D 領域」は平面上の Face として表せる。
>
> 入口は `text(fontPath, str[, size])`（TrueType の字形）と 2D プリミティブ
> （`rect` / `ngon` / `circle` / `polygon`）。出口は `extrude` / `revolve`。
> ★ フォントは **パス必須**で `import` と同じ D_REF 扱い（詳細は
> [関数リファレンス §text](srava_function_reference.html)）。
>
> ⚠ `occt.so` は `TKV3d` をリンクする（`StdPrs_BRepFont` がそこに居るため）。名前のとおり
> 可視化側のライブラリだが、`RenderGlyph` は表示に触らないので**ヘッドレスで動く**。
>
> ★ 2D 領域を折れ線へ落とすのは **`occt_mf.so` の `polygonize(cross2d, defl)`**。
>
> ### ★★ 2D は **z=0 平面の外へも置ける**（`oc-face3d` は / メッシュ系は）
>
> 変換 5 op（`translate` / `rotate` / `scale` / `mirror` / `transform`）は 2D も受ける。
> ⚠ **置き方の仕組みがカーネルで違う**:
>
> | | 2D の持ち方 | `rotate(rect(2,3), "x", 45)` | `translate(rect(2,3), [0,0,5])` |
> |---|---|---|---|
> | `cgal` / `manifold` | **平面（枠）+ 局所座標**。枠は正規直交・歪みは局所座標が持つ | **面積は不変**・枠が傾く | 枠が **z=5 へ上がる** |
> | **`occt`** | **面そのもの**（`TopoDS_Face` を動かす） | **面積は不変**・面が傾く | **z=5 へ上がる** |
>
> ⚠ メッシュ系は 2026-09-12 まで面外成分を**黙って捨てて**いた（射影した領域を返す）。同じ式に
> 2 通りの値が出るので一度**明示エラー**にし、 で **枠へ渡す**ように変えた。
>
> ★ **同じ平面に載っていれば、軸の取り方が違っても混ぜられる**。ブール・`combine`・
> `hull` は **先頭の被演算子の枠で表し直してから**計算する（幾何は 1 ミリも動かさず、局所座標の
> 読み方だけを揃える）。例: `rotate(rect(2,3),"x",90)` と `rotate(rect(2,3),"x",-90)` はどちらも
> `y=0` 平面に居るので `union` できる。
> ⚠ **本当に別の平面**なら明示エラー（別の平面の交わりは線分以下に落ちて 2D で表せない）。
> ★ `revolve` は**空間に置いた断面でも回せる**。軸は `extrude` と同じ考え方で
> **world Y に固定**（3 カーネル共通）。⚠ 断面が軸をまたぐと明示エラー。
> ★ `extrude` は **world +Z のまま**（枠の法線に固定すると斜めに押し出す手段が無くなる）。
> 体積は「射影面積 × 長さ」になる。平面が +Z を含むときだけ明示エラー。★ 平面を平面へ写す変換（`rotate(…, "x", 180)`・`mirror(…, "z")`・
> `scale(…, [1,1,-1])` など）は **通る** — 判定は「行列の z 行が (0,0,\*,0) か」であって、
> 軸の名前ではない。
>
> `oc-face3d` の実体は「任意の曲面 + (u,v) を切り取るワイヤ」なので、平面に縛られていない。
> ⇒ **断面を空間に置ける**のは `occt` / `manifold` / `cgal` の 3 つ（2D 型を持つカーネル全部）で、
> これが `loft_ruled`（断面を並べて張る）の前提になる。なめらかな `loft` は解析曲面が要るので
> `occt` だけ。
>
> ★★ 面外へ出た 2D も `polygonize` へ渡せる。面が載っている平面がそのまま
> `mf-cross2d` の **枠**になるので、面積も輪の形も変わらない（射影ではない）。
> ⚠ 〜 の間はここが明示エラーだった — 行き先の `mf-cross2d` が z=0 に縛られていて、
> 受ければ黙って XY へ射影する以外に手が無かったため。 で枠を持つようになって解消した。
> ⚠ **曲面上に切り取られた面は従来どおり断る**（折れ線にできないのは平面かどうかとは別の理由）。
>
> ⚠ **OCCT のバージョンに注意**: 7.8.1 には「境界だけで接する立体の融合が接触面を消してしまい、
> `xor` が `union` の値を返す」上流欠陥がある（7.9 系では解消）。ctest の `srava_occt_contact` が
> これを検出する。⚠ ただし**このテストが赤い理由は版だけとは限らない**（MinGW では 7.9.3 でも
> 別の検査で落ちる）→
> [インストールガイド §3](srava_install_guide.html)。
> **`cast` には置けない** — 曲線を折れ線に落とすには**粒度の指定が要る**ため
> （3D で `triangulate(s, defl)` が `cast` でないのと同じ理由。`defl` の単位も揃えてある）。
> → [型変換の規約](#conversion) / [関数リファレンス §polygonize](srava_function_reference.html)

> ### ★★ 面を取り出す — `face(solid, i)` / `face_at(solid, [x,y,z])`（2026-09-12）
>
> 立体から面を 1 枚取り出して `oc-face3d` にする。★ 取り出した面は**平面とは限らない**
> （円柱の側面は円筒面のまま出る）。`oc-face3d` が `TopoDS_Face` である意味そのもので、
> **メッシュ系が原理的に持てない**能力。
>
> **指し方を 2 通りに分けてある**。どちらが要るかは「何が変わりうるか」で決まる:
>
> | | 指し方 | 変わらないもの | 変わると困るもの |
> |---|---|---|---|
> | `face(s, i)` | **索引**（`TopExp_Explorer` の順） | 同じ面集合なら並びは決定的 | 立体の**作り方**を変えると面集合が変わる |
> | `face_at(s, [x,y,z])` | **位置**（その点に最も近い面） | 面の割れ方が変わっても同じ場所を指す | 稜・角の真上は同距離で決まらない |
>
> ★ 巡回順が「くじ引き」でないことは**着手前に測ってある**（4 条件）:
> ①同じ形を 2 回作る ②同じブールを 2 回 ③`BinTools` で往復（= キャッシュ相当・2 往復も）
> ④同じ面集合を作る別の式（`(box-A)-B` と `box-(A+B)`）— **すべて同じ並び**。
>
> ⚠ ただし `box(2,3,4)`（6 面）と「2 つの箱を積んだ同じ形」（10 面）は**面集合そのものが違う**。
> 同じ `i` は別の面を指す。★ これは順序規約をどう決めても直らないので `face_at` がある。
>
> ⚠ `face_at` は同距離の面が複数あるとき**明示エラー**にする。黙って片方を選ぶと
> 「同じ式に 2 通りの値」になる（1 と同じ筋）。
>
> ⚠ 取り出した面を `polygonize` へ渡すと、**曲面上の面だけ**が断られる（以降、面外の
> *平面* は枠として引き継がれて通る）。検証は `area` で行う。

> ### ★★ 曲面上の 2D を作る 3 手 — 取り出す / 切る / 投影する
>
> | やること | op | 検証（閉形式） |
> |---|---|---|
> | 面を**取り出す** | `face(s,i)` / `face_at(s,[x,y,z])` | 円柱側面 `2πrh`・底面 `πr²` |
> | 面を立体で**切る** | `cross2d &&& brep3d` / `cross2d --- brep3d` | 面積の加法性 `18.8496 = 3.1416 + 15.7080` |
> | 平面図形を**投影**する | `project(drawing, target, [dx,dy,dz])` | 弧長×高さ `2·asin(0.5)·2 = 2.0943951024` |
>
> どれも**曲面種を保つ**（円筒面を切っても投影しても円筒面）。素性は `surface_type(f)` で訊ける。
>
> ⚠⚠ **直線投影は閉曲面を複数回当たる**。`project` が返すのは**外向き法線が投影方向と逆を向く面**
> すべて（＝投影元に顔を向けている面）で、★ **遮蔽は見ない**。
> 円柱を横から投影すれば手前の半面だけが残る（選別しないと面積はちょうど 2 倍）が、
> **縦置きのトーラスを下から投影すると当たる 6 面のうち 3 面が返る** — 下の管の外側 2 枚に加え、
> その陰に隠れた**上の管の内側 1 枚**も「下を向いている」ので返る。
>
> ⇒ 「いちばん手前の 1 枚」が要るなら遮蔽の判定が要る。★ それには投影先の面だけでなく
> **立体**が要るので、`project(drawing, target_face, dir)` の形では**原理的に決められない**。
> 別の口（立体を受ける版）にするかどうかは未決。
>
> ⚠⚠ **投影の結果は 1 枚とは限らない**。凹んだ立体では 1 枚の面の 2 か所以上に当たる
> （U 字の手前面に帯を投影 → 塔 2 本で 2 枚）。⇒ `face(cross2d, i)` / `face_at(cross2d, [x,y,z])`
> で束から 1 枚を取り出せる（**2D も受ける**のはこのため）。
>
> ⚠⚠ **面の枚数は幾何だけでは決まらない — パラメータの継ぎ目（seam）で割れる**。
> トーラスは外側の赤道が `v=0` の継ぎ目なので、そこをまたぐ帯はひと続きでも 2 枚として報告される
> （縦置きトーラスを下から投影すると、幾何としては 4 本の帯でも `nfaces` は 6 になる）。
> 円柱も同じで、角度 0 の母線が継ぎ目。
> ★ 継ぎ目の置き方は OCCT の版で変わりうる ⇒ **枚数を前提にした式やテストを書かない**。
> 面積・位置・`surface_type` で判断する。
>
> ⇒ 枚数を意味のある数にしたいときは **`unify_faces(s)`** を通す（明示 op）。同じ曲面に載る
> 隣り合う面だけを 1 枚に畳み、**形は変えない**。畳み方はパラメータの原点をずらすのではなく、
> 周期曲面なので **2π を超える範囲をそのまま使う**（継ぎ目で割れた 2 枚が、`2π` をまたぐ
> 1 つの区間として 1 枚になる）。曲面は元のままなので他の面との共有関係が壊れない。
> ⚠ `fillet(box)` の 26 面は**畳まれない** — 平面と円筒は接していても別の曲面だから。
>
> ⚠ 1 枚の面が手前と奥の**両方**を向くとき（輪郭線をまたぐ輪。例: トーラスの下面に帯を投影）は
> **明示エラー**。★ 判定は面の中央 1 点ではなく **UV の格子でサンプル**する — 中央 1 点は
> ちょうど輪郭線の上（`dot = 0`）に来ることがあり、そこで誤って全部捨てると
> 「交わらなかった」と区別のつかない空が返る（2026-09-13 に実際に踏んだ）。
>
> ⚠ 2D×3D で**書けない**組み合わせは sig に載せていない: 立体 − 面（体積 0 の面で立体を切っても
> 変わらない = 黙って no-op）/ 2D ∪ 3D（次元の違う和を表現できる型が無い）。
> ★ 断る場所を op の中でなく **sig** に置くと、「どう書けるか」が記述子 1 箇所に集まる。
>
> ⚠ `extrude` / `revolve` は**掃引が単調でないと明示エラー**になる。符号つき体積が厳密に 0 に
> 打ち消し合う場合で、「潰れている」のではなく **`BRepPrimAPI` が組む境界表現の方が壊れている**
> （掃引された領域自体はミンコフスキー和として体積を持つ）。

> ## ★ `tube` と `tube_ruled` は **別の op**（`loft` / `loft_ruled` と同じ対）
>
> 2026-09-20 まで **どちらも `tube` という 1 つの名前**で、*どちらが走るかがロード構成で決まって*
> いた。`loft` / `loft_ruled` と同じ対に名前を分けたので、**いまは名前で決まる**。
>
> 分かれ目は **背骨**（なめらかに通すか、直線で結ぶか）。`loft` / `loft_ruled` で断面の置き方が
> 変わらないのと同じで、**断面の表現は op では変わらない** — 変わるのはカーネルである。
>
> | | 背骨 | 断面 | `segs` |
> |---|---|---|---|
> | `tube_ruled`（`cgal` / `manifold` / `geogram` / `cherchi` / `nef_hybrid` / `nef_snc` / `openvdb`） | 点を直線で結ぶ**折れ線** | `segs` 角形近似の円 | 効く |
> | **`tube_ruled`（`occt`）** | 点を直線で結ぶ**折れ線** | **厳密な円** | **無視** |
> | **`tube`（`occt`）** | 点を**通る C2 の B-spline** | **厳密な円** | **無視** |
>
> ★ `occt` の `tube_ruled` は「折れ線に沿って置いた円の `loft_ruled`」そのもので、メッシュ系と
> **同じ構成**である（`segs` を上げるとメッシュ系の値が `occt` の値へ収束することで確かめられる）。
> ⚠ それでも**厳密に一致はしない** — 断面が円か多角形かの違いが残るため。精度の問題ではなく
> **表現の違い**なので、許容誤差を緩めても一致しない（`kernel_agree` の表にも入れていない。
> `occt` の `sphere` を入れられないのと同じ理由）。
>
> ⚠ 角のあるパスで `"occt"::tube` を `tube_ruled` の代わりに使うことは**できない** —
> B-spline の背骨は角を丸めるので、体積がはっきり違う形になる。角を尖らせたいなら
> `"occt"::tube_ruled(…)` と書く。
>
> ★ `closed:1` で**半径を変えられる**のは `tube_ruled` だけ（`tube` は明示エラー）。
> 組み立てる機構が違うため。
>
> ### ⚠ `occt` の `tube_ruled` は **3D のパスだけ**を受ける
>
> `tube_ruled(path)` は、位置が `[x,y]` なら結果が **2D の領域（帯）**になる — これは
> ほかの 7 本で確定している意味である。`occt` は常に `oc-brep3d` を名乗るので、
> ここで `[x,y]` を z=0 と読んで掃くと**同じ式がカーネルによって「帯」と「平たい立体」に
> 化ける**。⇒ `occt` は 2D のパスを**明示エラー**にする（帯が要るなら
> `"cgal"::tube_ruled` / `"manifold"::tube_ruled`、立体が要るなら位置を `[x,y,z]` で書く）。
>
> ⚠ **`tube` は従来どおり `[x,y]` を z=0 として受ける**。`tube` は `occt` 単独の op で、
> 突き合わせる相手が居ないため。
>
> ★ これは **`occt` に帯が作れないという意味ではない**。`occt` は 2D の領域を持てる
> （`oc-cross2d` / `oc-face3d` 型があり `rect` / `circle` / `polygon` / `text` / 2D の `offset`
> を実装している）。**この op がまだ 3D 専用**だ、という範囲の話である。
>
> occt 版を使う価値は「解析曲面として持てる」こと — `offset` が厳密（Steiner の公式と一致）・
> `fillet`/`chamfer` が効く・STEP に実物の曲面が載る。詳細は
> [関数リファレンス §tube](srava_function_reference.html) を参照。

**`-DSRAVA_MODULE_OCCT=ON`（既定 ON・2026-08-31 以降。**system の OpenCASCADE が要る** / Cygwin では自動 OFF）**。Open CASCADE Technology（LGPL-2.1 + 例外）を
`find_package` でシステムから使う（**規約 C**。Debian が `libocct-*` を配っており、自前ビルドは
非常に重い）。OCCT 自体が素の `tbb;tbbmalloc` をリンク要求に持つため、TBB は
**ビルド全体で 1 インスタンス**の規則（openvdb 節を参照）の対象になる。

**これは第 4 の表現クラス = B-rep**。三角形メッシュでも距離場でもなく、**解析曲面**
（平面・球面・円筒・トーラス…）を境界として持つ。**近似が入らない**のが特徴で、その代わり
扱える曲面は**カタログ**であって任意の `f(x,y,z)=0` ではない（ACIS / Parasolid と同じ系譜）。

**サポートする型**:

| 型名 | 4CC | 意味 |
|---|---|---|
| `oc-brep3d` | `BREP` | OCCT の `TopoDS_Shape`（`BRepTools` のシリアライズ） |

**サポートする op**:

| 分類 | op |
|---|---|
| 生成（3D） | `box(w,h,d)` / `boxa` / `sphere(r[, seg])` / `cylinder(r, h[, seg])` / `torus(R, r[, seg])` / `cone(r, h[, seg])` / `prism(n,h,r)` / `pyramid(n,h,r)` / `tetrahedron(r)` / `icosphere(r[, subdiv])` / `tube_ruled` / `empty3d` |
| 生成（2D） | `rect(w,h)` / `ngon(n,r)` / `polygon(pts)` / `circle(r[, segs])` / `text` / `empty2d` |
| ブール（二項） | `union` / `intersection` / `difference` |
| **点群を切る** | `intersection(点群, 形, mode)` / `difference(点群, 形)`<br>★ 判定は **`BRepClass3d_SolidClassifier`**（3D）/ 面までの距離（2D）。**解析曲面のまま**解くので球や円柱でメッシュ近似の誤差が無く、**3 実装の中で一番正確**（境界の厚みは `Precision::Confusion`）<br>⚠ `oc-face3d` では **面の平面に載っていない点は外側**（黙って射影しない）<br>⚠ **可換ではない**。約束の全文は [関数リファレンス §点群を形で切る](srava_function_reference.md#ptsplit) |
| 加工 | `offset(s, d)`（3D ／ 2D は**面の中で**輪郭を動かす）/ **`offset_thicken(area2d, d)`**（面に厚みを付けて立体に）/ **`fillet(s, r)`** / **`chamfer(s, d)`** / `unify_faces` |
| 自由曲面 | **`surface_through(grid[, mode])`**（通過点を通す）/ **`surface_control(grid[, mode])`**（制御点で与える）/ **`poles(f)`** / **`set_poles(f, grid)`** / **`surface_type(f)`** |
| 3D→2D 図面 | **`hlr(solid, dir[, up][, mode])`** — 陰線処理。遮蔽を解いた図面を `oc-cross2d` で返す（`mode` で可視／隠線を選ぶ） |
| 2D の置き直し | `project_flatten(area2d)` — 空間に置かれた面を **z=0 へ落とす**（3 カーネル共通）/ `project` |
| 3D→2D | **`section(s, P, N[, mode])`**<br>★ : **解析曲面のまま切る**ので球の断面は真円 (`Geom_Circle`)。面積が `π(r²-h²)` に丸め誤差の範囲で一致する (メッシュ系は内接多角形で構造的に小さい)。3 要素配列の規約は cgal / manifold と同じ |
| 入口 | `import(path)` … **STEP / IGES / .brep** |
| 出口 | `export(path, s)` … **STEP / IGES / .brep**（`triangulate` は別モジュール → [occt_mf.so](#occt_mf)） |
| 計測 | `volume` / `nfaces` / `nverts` / **`nedges`** / `bbox` / `centroid` / `area` / `valid` / `distance_at`（**2D も受ける**）/ `vert` / `verts` / `face_verts`<br>★ occt の「頂点」は **稜の端点** — 立方体 8・円筒 2（継ぎ目）・円 1。**`nedges` は occt だけ**が持つ（B-rep の稜は「曲線 1 本」なので円は **1 本**。面を持たない図面（`hlr` の出力）は稜の本数だけが中身を語る）<br>★ : `distance_at` も **B-rep のまま** (`BRepExtrema_DistShapeShape`)。⚠⚠ `TopoDS_Solid` をそのまま渡すと「中身の詰まった領域」扱いで**内側が 0** になるので、Face の compound に対して測っている<br>★ **B-rep のまま**積むので、球の表面積は `4πr²` ちょうど（メッシュ系の内接多面体とは構造的に違う値）。`valid` は `BRepAlgoAPI_Check`（妥当性 + 自己交差）**＋稜の使われ回数**（★ : `BRepAlgoAPI_Check` は立体を 1 つずつしか見ないので、稜だけで接する 2 立体を妥当と答える。共通定義 ② の「2-多様体」はこちらで数える） |

#### ★ 生成 — **解析曲面**の組と、**平面多面体**の組がある

曲面を持つものは厳密で、分割数（`seg`）は**そもそも取りません**（2026-09-21）。
近似しないので意味を持たないためです。⇒ `sphere(r)` / `cylinder(r,h)` / `cone(r,h)` /
`torus(R,r)` / `circle(r)` / `revolve(cross2d[,deg])`。

⚠ 2026-09-21 以前は「受け取るが無視する」形で引数の個数をメッシュ系と揃えていました。
いまは **個数が routing の条件**なので、`sphere(1,32)` と書けば
*分割数を持つカーネル* が選ばれ、`"occt"::sphere(1,32)` はエラーになります。

| op | 体積 | Face 数 |
|---|---|---|
| `sphere(r[, seg])` | 4/3·π·r³（**`seg` に依存しない** — 球面 1 枚として持つ） | 1 |
| `cylinder(r, h[, seg])` | π·r²·h | **3**（円筒 1 + 平面 2） |
| `cone(r, h[, seg])` | π·r²·h/3 | **2**（円錐面 1 + 平面 1） |
| `torus(R, r[, seg])` | 2π²·R·r² | **1** |
| `circle(r[, segs])`（2D） | π·r²（面積） | 1 |

一方 **`prism` / `pyramid` / `tetrahedron` / `icosphere` / `rect` / `ngon` / `polygon`** は
**平面の集まり**なので、メッシュ系と**厳密に一致する**（`box` と同じ理由）。`icosphere` が
ここに入るのは、それが近似球ではなく**測地多面体そのもの**だから。
⇒ カーネル一致の検査表には、この平面多面体の組だけを入れている。

★ `nfaces` は **三角形数ではなく Face 数**。トーラスが「1 面」なのがこの表現の要点で、
同じ形をメッシュ系に持たせれば数千面になる。`torus` は「メッシュでは必ず近似になるが
B-rep では厳密に持てる」形の代表であり、**`fillet` が稜に作る曲面そのもの**でもある。

#### ★ `fillet` / `chamfer` — B-rep でしか厳密に書けない加工

**全ての稜**に一律に適用する（「この稜だけ」を指す語彙が srava に無いため。部分適用は将来）。

★ **メッシュ系にこの op が無いのは偶然ではない。** 転がり球の接触軌跡は解析曲面
（平面どうしの稜なら円筒、頂点なら球）であって、三角形分割の上では**定義そのものが近似になる**。

**どちらも真値と突き合わせられる**:

- **`fillet`**: 直方体（辺 a）の全稜を半径 r で丸めた形は、**内側の直方体 (a−2r) を半径 r の
  ボールで Minkowski 和したもの**とちょうど一致する。「OCCT の fillet が転がり球の軌跡である」ことは
  回帰テストで固定してある。
- **`chamfer`**: ★ **角の扱いに二つの流儀がある**。

  | 流儀 | 立方体（辺 a・距離 d）の体積 | a=2, d=0.3 |
  |---|---|---|
  | (a) 稜の平面 3 枚がそのまま交わる | a³ − 6ad² + 6d³ | 7.082 |
  | (b) **角にも平面を立てる** | a³ − 6ad² + (16/3)d³ | **7.064** |

  **OCCT は (b)**。立方体の Face 数が 26（元 6 + 稜 12 + **角 8**）になることがその裏づけで、
  (a) を仮定した式と突き合わせると合わない。⚠ **「chamfer」という言葉だけでは形が決まらない**ので、
  他のカーネルと突き合わせるときは流儀を確認すること。

#### ★ `import` / `export` — STEP は「表現力を落とさない出口」

★ **`import` は「mesh → B-rep」ではない。** STEP も `.brep` も**解析曲面をそのまま持っている**
形式なので、読むだけで B-rep が手に入る（復元も推定もしない）。三角形群から解析曲面を復元する
reverse engineering の入口は、依然として**作らない**。

★ **STEP 往復は表現を落とさない** — 書いて読み直しても解析曲面のまま（Face 数も保たれる）。
メッシュ形式（STL/OBJ…）へ書くと三角形に落ちるので、B-rep を保ったまま外へ出す出口は
STEP / IGES / `.brep`。

#### IGES（2026-09-12）

`.iges` / `.igs` を読み書きする（`IGESControl_Writer` / `IGESControl_Reader`・toolkit は
**`TKDEIGES`**。STEP の `TKDESTEP` とは別なので、ビルドフラグも別に `SRAVA_OCCT_IGES` がある）。

- ★ **新規に選ぶ形式ではない**。IGES は STEP の前世代で、曲面は運べるが**位相（殻・向き）が弱い**。
  レガシー資産の受け渡し用と考えて、新しく出すなら STEP
- ★★ 書き出しは **BRep モード**（`IGESControl_Writer(unit, 1)`）を使う。⚠ OCCT の既定は
  Faces モード（`0`）で、面を**ばらばらの IGES エンティティ**として並べるので、読み戻しても
  殻にならず **体積が出ない**。ここを踏むと「書けているのに使えない」ファイルになる
- ★ **単位はファイル自身が持つ** — STEP と違い writer の引数なので、`export(path, s, unit)` の
  `unit` をそのまま渡している（`mm` / `cm` / `m` / `in` / `ft` / `km` / `mi` / `mil` / `um`。
  大文字小文字は問わない）。⚠ 知らない綴りは **`MM` に落とす**（黙って別の単位で書かない）
- ⚠ `IGESControl_Controller::Init()` を呼ばないと writer が**空のモデルを書く**
- ★ 中断できる（`AddShape` / `TransferRoots` が `Message_ProgressRange` を取る。STEP と同じ作法で、
  ファイルへの書き出し自体は始まったら最後まで走る）
- ★ **精度**: 球を 3 形式へ出して読み戻しても、Face 数は **1**（解析球面 1 枚）のままで、
  体積は閉形式 `4/3·π·r³` と倍精度の丸めの桁でしか違わない。
  ⇒ *三角形化されていないことが値で言える*（メッシュ経由なら内接多面体の体積になるので
  上位の桁で落ちる）。回帰は `test/srava_occt.sh iges`

### ⚠ `solidify` を持たない — 配線しない判断（2026-09-12）

 では「`ShapeFix_Shape` で `solidify` を配線するか判断する」が課題だったが、**試して見送った**。

- ★ `ShapeFix_Shape` は**別の仕事**だった。半分重ねた 2 つの箱を 1 枚の殻として与えると
  **ソリッドが 1 つもできない**。これは B-rep の壊れ（wire の向き・
  公差・微小エッジ）を直すもので、**面どうしの交差を解いて内外を決め直す機構ではない**。
- ★ 正しい道具立ては `BOPAlgo_Builder`（General Fuse）で交差を解き、`BOPAlgo_BuilderSolid` で
  シェルを Growth / Hole に分類する経路。`BOPAlgo_BuilderSolid` の前提が
  *"The given faces should be non-intersecting"* なので、**2 段で組む**必要がある。
- ⚠ ただし素直に 2 段を繋いだだけでは **答えが合わなかった**（同じ入力で geogram の
  `solidify` と違う体積になる）。分類の段に、nef が
  `Mark_bounded_volumes` + 入れ子の深さで、geogram が radial sort でやっているのと同等の
  詰めが要る。**20 行では載らない**。
- ⇒ いま `solidify` を持つ 2 実装（nef / geogram）は**独立実装で下位桁まで一致**している。
  そこへ公差ベースの 3 つめを入れると、同じ op 名の答えが**入力型で質的に変わる**。
  ⇒ 見送り。必要になったら「メッシュのスープではなく **B-rep の殻**を直す」用途として起票し直す。

## nef_cg.so / nef_mf.so {#nef_bridge}

**概要**: **`nef_snc.so` と他カーネルの橋渡しモジュール**。`cast` 1 op だけを持ち、
Nef の値（`nf-mesh3d`）から**本物の `cgMesh3D`**（`cg-mesh3d`）／**本物の `mfMesh`**（`mf-mesh3d`）を作る。

`nef_snc.so` はキャッシュに **SNC（Nef 本来の表現）だけ**を書く。SNC の読み取りには CGAL Nef が要るが、
`cgal.so` も `manifold.so` も SNC のパーサを持たないので、その cache を直接読むことはできない。
変換をこの 2 本に切り出すことで、**`cast` を書いたときだけ**境界表現への変換コストを払う。

> ⚠ `nef_hybrid.so` には橋渡しが要らない。あちらは普通の立体を厳密境界の形式で書くので、
> `cgal.so` / `manifold.so` がそのまま読める。橋が受けるのは `nf-mesh3d` だけ。

> ⚠ `SRAVA_MODULE_NEF_SNC`（既定 ON）のときにビルドされる（`nef_mf.so` はさらに manifold が要る）。
> `module/all.sra` には入っているので、`nef_snc.so` を明示ロードすれば併せて使える。

**サポートする型**: `nf-mesh3d`(NEF3) を読み、`nef_cg` は `cg-mesh3d`(MESH)、`nef_mf` は `mf-mesh3d`(MFM3) を書く。

**サポートする op**: `cast(型名, s)` のみ。

変換できないのは**境界表現を取れない値**（非有界 — `complement` の結果など）で、そのときは明示エラーになる。

## occt_mf.so {#occt_mf}

**概要**: **occt とメッシュ系の橋渡しモジュール**。`triangulate`（3D）と `polygonize`（2D）の
2 op を持ち、B-rep（`oc-brep3d`）から**本物の `mfMesh`**（`mf-mesh3d`）を、
occt の 2D（`oc-face3d` / `oc-cross2d`）から `mf-face3d` / `mf-cross2d` を作る。

> ⚠ `-DSRAVA_MODULE_OCCT=ON` かつ manifold が有効なときにビルドされる。使うときは
> `module("occt.so", {}); module("occt_mf.so", {});` の**両方**をロードする。

**サポートする型**: `oc-brep3d`(BREP) / `oc-face3d` / `oc-cross2d`(BRP2) を読み、
`mf-mesh3d`(MFM3) / `mf-face3d` / `mf-cross2d`(MFC2) を書く。

**サポートする op**（2 本）:

| op | 何をするか |
|---|---|
| `triangulate(s, deflection)` | B-rep → 三角形メッシュ。`deflection` は弦の最大距離（世界座標の長さ） |
| `polygonize(area2d, deflection)` | occt の 2D（曲線のままの輪郭）→ **折れ線**の 2D。単位は `triangulate` と揃えてある |

- ★ **`cast` には置けない** — 曲線を折れ線へ落とすには**粒度の指定**が要るため
  （型変換は情報を捨てる方向でも「引数なし」が約束）。→ [型変換の規約](#conversion)
- ★★ **面外へ出た 2D も渡せる**。面が載っている平面がそのまま `mf-face3d` の**枠**に
  なるので、面積も輪の形も変わらない（射影ではない）。⚠ **曲面上に切り取られた面は断る**。

## geomutils.so {#geomutils}

**概要**: **メッシュ系に共通の「素性を訊く」op** を 1 本の実装で引き受ける、**カーネル中立**の
モジュール。外部の幾何ライブラリを持たず、`manifold` / `geogram` / `cherchi`
（と自分の型 `gu-*`）が作った値を**そのまま受けて**、計測・位相・片の取り出し・頂点読みに答える。

> ★ なぜ 1 本に寄せたか: 計算の中身はもともと `src/h/common/meshprops.h`（3D）と
> `ringprops.h`（2D）に**定義ごと 1 本**で書かれていて、3 つのカーネルはどれも内部表現から
> 素の配列へ写してそこを通っていた。⇒ **実質「同じ型」を 3 者が名乗らずに作っていた**ので、
> 型として名乗らせ、1 モジュールに所有させた（ひさ設計 2026-09-17）。
> **答えは寄せる前と 1 ビットも変わらない**（回帰は「寄せる前 / 後で同値」＋ codec を壊すと
> 赤くなる陽性対照と対で回す）。

**前提とする外部ライブラリ**: **なし**（`points.so` と同じ。したがって**常にビルドされる**）。

⚠⚠ **ロードは自動ではない**。`module("geomutils.so", {})` を書いていないと、
`valid(mfMesh)` / `genus(...)` / `nparts(...)` / `part(...)` は
`no module can execute op ...` になる（`include "module/all.sra";` にも含まれる）。

**サポートする型**:

| 型名 | 4CC | 意味 | 座標 |
|---|---|---|---|
| `gu-mesh3d` | `MFM3`（manifold / geogram と共有） | 3D 三角形メッシュ | double |
| `gu-cross2d` | `MFC2`（manifold と共有） | z=0 の 2D 領域 | double |
| `gu-face3d` | `MFC2` | 空間に置かれた 2D 領域（枠つき） | double |

- 点群型（`pt-cloud2d` / `pt-cloud3d`）は [points.so](#points) から**借りている**（`verts` の返り値の器）。
- **`cast` が型の入口**。`mf-` / `gg-` / `ch-` / `cg-` の 3D・2D をすべて受ける
  （⚠ `cg-*` の**厳密有理数は double へ落ちる = 不可逆**。厳密が要る問いは cgal 側で訊く）。

**サポートする op**（21 本）:

| 分類 | op |
|---|---|
| 型の入口 | `cast` |
| 計測 | `volume`（自型のみ）/ `area` / `bbox` / `centroid` / `distance_at` |
| 位相・数える | `nverts` / `nfaces` / `nparts` / `nshells` / `genus` / `valid` |
| 片の取り出し | `part` / `part_at` / `shell` / `shell_at` |
| 頂点を読む | `vert` / `verts` / `face_verts` |
| **点群を切る** | `intersection`（点群 × 形 × mode）/ `difference`（点群 × 形） |

- ★ **点群を切る 2 本**は向きが逆で、`points.so` の型を**受けて**返す。
  境界ちょうどの点を第 3 の集合として切り出す約束は
  [関数リファレンス §点群を形で切る](srava_function_reference.md#ptsplit) に全文。
  ⚠ **可換ではない**（`intersection(メッシュ, 点群)` の順は受け付けない）。
  ⚠ `pt-cloud3d` × 2D 領域は明示エラー（`X <= Y` が条件）。

- ★ 受ける型は op ごとに違う。素性 op は `gu-*` に加えて **`mf-mesh3d` / `gg-mesh3d` / `ch-mesh3d`**
  を名乗り、2D は `gu-cross2d` / `gu-face3d`（`valid` / `nparts` は `mf-cross2d` / `mf-face3d` も——
  manifold がその 2 本を持たないため）。
- ⚠ **`cg-*` は名乗らない**（据え置き）。名乗っても priority 20 に負けて**死に行**になる。
- ⚠ **`gg-mesh3d` の `distance_at` は名乗らない** — geogram が `MeshFacetsAABB` で自前に持っており、
  ここが priority で勝つと*速い実装を総当たりで置き換える*ことになる。
  ⇒ **歯抜けを埋めるモジュールが、既にあるものを奪ってはいけない**。
- ⚠⚠ **`part(mf-mesh3d, i)` / `part(gg-mesh3d, i)` の返り型は `gu-mesh3d`**（従来は `nf-mesh3d`）。
  厳密な答えが要るなら `"nef_snc"::part(...)` と**名指す**。

**priority = 7**（上下の両側に根拠がある）:

- **5 以下にできない** — `mf` / `gg` から 3D の素性 op を落とした瞬間、いま priority に負けて
  死んでいる `nef` の `nparts` の行が生き返り、**黙って Nef 変換経路へ落ちる**
  （掃引規模では SNC が 100GB 級になる）。
- **8 以上にしない** — 押さえる対象は増えず、`mf`（10）を超えると 2D まで奪って
  「2D は manifold のまま」という決定と食い違う。
- **`cgal`（20）は超えない** ⇒ 既定カーネルにはならない（立体を作る op を 1 つも名乗っていない）。

```
module("manifold.so", {}); module("geomutils.so", {});
var m = cast("mf-mesh3d", box(2,2,2));
valid(m);        // 1      ← geomutils が答える
genus(m);        // 0
nparts(m);       // 1
volume(m);       // 8      ← これは manifold 自身（自前の Volume()）
area(m);         // 24     ← 3D は geomutils・2D は manifold
```

各 op のシグネチャ・引数・例は[関数リファレンス](srava_function_reference.html)を参照。

---

## points.so {#points}

**概要**: **点群**（順序を持たない点の集まり。法線を持てる）を値として扱うモジュール。
幾何カーネルを持たない点では `pipe_proximity` と同じ**非カーネル**モジュールだが、こちらは
**自分の型を持つ**（`pt-cloud2d` / `pt-cloud3d`）。

**前提とする外部ライブラリ**: **なし**。中身は平坦な double 配列の走査と stdio だけ。

### ★★ なぜカーネル中立なのか（設計の核）

点群を受け取る外部ライブラリは 5 つあり、**そのどれもが「平坦な double 配列」で受け取る**:

| モジュール | 点群を受ける API | 受け取る形 |
|---|---|---|
| cgal | `pca/jet_estimate_normals` `mst_orient_normals` `poisson_surface_reconstruction_delaunay` `Efficient_RANSAC` `Delaunay_triangulation_2/3` | レンジ + property map（コンテナ自由・EPICK） |
| geogram | `Delaunay::set_vertices` `Co3Ne_compute_normals` `Co3Ne_reconstruct` | `const double*`（**0 コピー**） |
| occt | `GeomAPI_PointsToBSpline` / `GeomAPI_PointsToBSplineSurface` | `TColgp_Array1OfPnt` / ⚠ `Array2OfPnt` = 行×列の**格子**が要る |
| openvdb | `tools/ParticlesToLevelSet.h` / `points/`（PointDataGrid） | 粒子リスト |
| manifold | `Manifold::Hull(const std::vector<vec3>&)` | `vec3` の連続配列 |

⇒ メッシュ（EPECK `Surface_mesh` ⇄ manifold の半辺構造）と違い、点群には**保存すべきカーネル固有
表現が無い**。変換は 0 コピーか線形 1 パスで済む。型をどれか 1 つのカーネルに置くと残り 4 つが
そこへ依存することになり、cgal に置けば geogram / manifold / openvdb（いずれも非 GPL）が
**GPL を引き込む**。⇒ 本体クラス `ptCloud` と codec / reader / writer を中立の
**`libsrava_pt`** に置き、このモジュールが型を名乗る。

★ 点群を消費するモジュールは、自分の `provides` に `&ptCloud::WIRE` を並べ、`LINK` に `srava_pt` を
足すだけでよい（`occt_mf` が `mfGeom` を借りるのと同じ作法）。新しいクラスも wire 形式も作らない。
実例が `cgal.so` の `estimate_normals`。

**サポートする型**:

| 型名 | 4CC | 意味 |
|---|---|---|
| `pt-cloud2d` | `PTC2` | 2D 点群（座標の平坦配列 + 省略可能な法線） |
| `pt-cloud3d` | `PTC3` | 3D 点群（同上） |

cache 形式（`D_META` の 4CC に続く `D_CHUNK`・little-endian）:

<pre>
[u32 np][u32 flags] 座標×np(double × dim) [法線×np(double × dim)]
  flags bit0 = 法線あり / bit1 = 向き付けあり
</pre>

★ **次元は 4CC が持つ**（`PTC2`=2D / `PTC3`=3D）。payload には書かない — cgal が `MESH`/`PLY2` で
次元をタグに畳んでいるのと同じで、二重に持つと必ずずれる。

### ★★ 印は 2 つ — 法線の有無 / 向き付けの有無

| 使う側 | 要求 |
|---|---|
| CGAL Shape_detection（RANSAC） | **向きなし**で足りる |
| CGAL Poisson / geogram `Co3Ne_reconstruct` | **向きあり**が必須 |

「法線があるか」と「向き付けされているか」は**別の事実**なので両方を持ち、**両方ともキャッシュに
載せる**（載せないと cold と warm で答えが変わる。前例は openvdb の `is_normalized`）。

⚠⚠ **既定の法線ベクトルは作らない**。全点を `(0,0,1)` などで埋めると Poisson も RANSAC も
**エラーにならずに走り、静かに嘘の形を返す**。要るのは既定値ではなく「法線が無いときの既定の
振る舞い」で、それは ① 推定を独立した op（`estimate_normals`）にする ② 法線を要求する op は
無ければ**明示エラー**にする（黙って推定しない）、の 2 つ。

★ 規約: **明示的に与えられた法線は向き付けされているとみなす**（`points3d` の入れ子形・`xyz` の
6 列）。与えた人が向きに意味を持たせている、と読む。推定した法線は `estimate_normals` が
**実際に向き付けできたときだけ**印を立てる。

**サポートする op**（17 op）:

```
points2d, points3d, rand, rand_gaussian,
import, export, nverts, vert, bbox, centroid, valid,
translate, rotate, scale, mirror, transform, union       ←
```

- ★ `vert(p, i)` は `i` 番目の点の座標（の「頂点を読む 3 つ組」と同じ綴り）。
  逆向きの `verts(m)` は**メッシュ側**が持ち、返り値の器として点群型を借りる。

- `import` / `export` に**新しい op 名は作らない**。記述子の `import_exts` が型つき CSV
  （`"xyz:pt-cloud3d"`）なので、申告するだけで routing がここへ来る。
- ⚠⚠ **形式は当面 `xyz` だけ**。`ply` は「メッシュにも純粋な点群にもなりうる」のに、型は
  routing の段（`ext_type_in_csv`）で拡張子から決まるので**中身を見てから選べない**。しかも
  `ply` は既に cgal が `ply:cg-mesh3d` と申告済み。`off` も面 0 個なら同じ問題。
  ⇒ 曖昧でない `xyz` だけを点群に割り当てる。
- `xyz` の列数は **2 / 3 / 6**（2 は z=0・6 は `x y z nx ny nz`）。geogram の XYZIOHandler・
  CGAL `read_xyz_points` と同じ並び。⚠ ファイル内で列数が変わったら明示エラー。
- ★ **2D 点群も `.xyz` に書ける**（ひさ判断 2026-09-22）。`export` の `sig` は
  `(pt-cloud3d)->ref;(pt-cloud2d)->ref`（出力は常に `ref` なので行は 1 本のまま）。
  列数は `.xyz` の約束どおり **2 / 3 / 6**:

```
法線なし 2D → 2 列  "x y"            法線なし 3D → 3 列  "x y z"
法線あり 2D → 6 列  "x y 0 nx ny 0"  法線あり 3D → 6 列  "x y z nx ny nz"
```

  ⚠⚠ **往復で型が変わる**（`pt-cloud2d` → `pt-cloud3d`）。`.xyz` に「2D である」と書く場所が
  無いため。⇒ 2026-09-22 まではこれを理由に **書けない**仕様にしていたが、
  「往復で型が変わることは*文書に書けば読み手が判断できる*」という判断で解禁した（ の案 (a)）。
  ★ 代案 (b)「2 列なら `import` も `pt-cloud2d` を返す」は採らなかった — `import` の行は
  **拡張子が産む型**で選ばれる規約なので、同じ `.xyz` が 2 つの出力型を持つと
  *ファイルを開かないと型が決まらない* ことになる。
  ★ 法線つき 2D を 6 列にするのは**情報を落とさない**ため（4 列は読む側が受けない）。
- ⚠ `area` / `volume` は**申告していない**。それが明示エラーになる
  （`no module can execute op 'area' on input types (pt-cloud3d)` と受理する型まで添えて断る）。
- `valid(p)` は共通定義のうち **①「空でない」だけ**が意味を持つ（②③ は点群では構造的に恒真。
  openvdb の距離場と同じ扱い）。

### ★★ transform 一族 と `union`（2026-09-22）

点群にも `translate` / `rotate` / `scale` / `mirror` / `transform` / `union` がある。
引数の書き方と拒否の理由はメッシュ版と**同じ 1 か所**（`common/affine.h`）なので、
受け付ける形もエラー文も 7 カーネルと揃っている。

```
(pt-cloud2d)->pt-cloud2d    z=0 平面を保つ変換     行 translate#xy / rotate#z / scale#xy / mirror#xy / transform#xy
(pt-cloud2d)->pt-cloud3d    面外へ出す変換         行 translate / rotate / scale / mirror / transform
(pt-cloud3d)->pt-cloud3d
```

★★ 行を選ぶのは **cgal / manifold / occt の 2D が /5 で使っているのと同じ共通述語**
（`pig/c++/pigOpMatch.h` の `pig_val_keeps_xy` ほか）。⇒「平面を保つか」の判定が 8 本目として
割れない。**新しい述語は 1 つも作っていない**。

⚠⚠ **routing の述語と計算本体の判定は同じ関数でなければならない**。別々に書くと
*値は正しいのに型だけ違う*（`sig` は `pt-cloud2d` と言っているのに 3 次元が返る）という
一番見つけにくい壊れ方になる。⇒ 対応表は `pt/c++/ptAffine.h` の 1 か所にあり、行のマッチ関数も
計算本体もそこを呼ぶ。
★ 2026-09-22 に較正で確かめたこと: **`type_of` は routing の申告を答える**ので、本体だけを
壊しても型の検定は緑のまま通る。捕まえたのは値の検定（`vert` が 2 成分のはずのところで 3 成分）
だった。⇒ 型と値の両方を検定に置くこと。

⚠⚠ **`rotate` だけは軸が `"z"` のときしか 2D に留まらない**。マッチ関数は引数を 1 個ずつしか
見られないので、*軸と角度の両方*で決まる性質（`rotate(p,"x",180)` は平面を保つ）を判定できない。
⇒ 保守的に軸だけを見る（`cgal` / `manifold` / `occt` の 2D と同じ扱い）。

★ 点群には**枠（平面）が無い**ので、`cg-cross2d` / `cg-face3d` のような「置かれた 2D」は
在り得ない。平面を出れば即 3 次元の点群になる。

**法線**（★ ここが「静かに嘘をつかない」ことの本体）:

- 法線は**ベクトルではなく余ベクトル**。点と同じ行列を当てると `scale([2,1,1])` で面に対して
  傾く。⇒ 3D は**逆転置** `n' = cof(M)·n / det(M)`。⚠ **`det` で割る**（割らないと反射で裏返る）。
- 2D の面内法線は、*像の平面の中で*変換後の接線に直交する向きへ写す
  （`n' ∝ B(-J G J n)` ・ `B` = 線形部の第 1・2 列 ・ `G = BᵀB`）。平面を保つ変換では
  これは 2x2 の逆転置と**厳密に一致する**（乱数行列 2000 本で確認）。
- 向き付けの印（`oriented`）はそのまま運ぶ。

**`union`**:

- **単純に混ぜる**。⚠ メッシュのブール和とは別の計算で、**重複は落とさない**
  ⇒ 不変条件 `nverts(union(a,b)) == nverts(a)+nverts(b)`。
  （落とすべき重複の定義が点群には無い。与えるならそれは「間引き」という別の op。）
- `sig` は **`fold` 形 2 本**:

```
[pt-cloud3d,pt-cloud2d](2)->pt-cloud3d    主型 3d が 1 つでも在れば 3D
[pt-cloud2d](2)->pt-cloud2d               全部 2D のときだけ 2D
```

  ★ 主型の規則（可変部に `set[0]` が最低 1 個）が「**どちらかが 3D なら 3D**」をそのまま書いた
  形になっている ⇒ 昇格の規則が `sig` **だけ**で決まり、op の中と二重帳簿にならない。
  ★ `(2)` は「一度に 2 項まで」という capability。3 項以上は
  `pigfModuleAgent::try_decompose` が**二項の木へ分解**する（cgal の `union` と同じ）。
  ⚠ 配列形 `union([a,b,c])` は `pigfArrayFold` が **n 項ノード 1 つ**に畳むだけなので、
  **`fold` 形の行が無いと分解されずに arity エラーになる**（当初そうなっていた）。
- ★★  の懸念「2D+3D 混在を許すと畳む順で主型が動く」は **`union` には当たらない** —
  昇格が `max(次元)` で結合的かつ単調だから。⚠ 当たるのは  の (8)(9)（型が非対称で、
  分解すると意味が変わるもの）の方。
- ⚠ `union([])` は **`fold` の単位元 `{}`**（`type_of` は `value`）。全カーネル共通の既存の振る舞い。
- ⚠⚠ **可換の印を立てていない**。並びは「格納順 = 入力の順」が約束なので、
  印を立てると ① 分解が均衡木になり ② **二項ノードのキャッシュキーが正規化されて**
  `union(a,b)` と `union(b,a)` が同じ結果を返す ⇒ 索引の約束が静かに破れる。
  ★ 2026-09-22 の較正で分かったこと: **この害は「同じ走の中で両方を評価」しないと見えない**。
  別々の走（別キャッシュ dir）だと鍵が衝突していても双方が自分で計算してしまい、
  正しい答えが返る。⇒「同じ cache dir で 2 回流すのは検定にならない」の**逆向きの罠**。
- 法線は**空でない側がどちらも持っているときだけ**運ぶ。片方にしか無ければ落とす
  （既定の法線を作らないのと同じ理由）。⚠ 空の点群は問わない（`union(p, 空)` は恒等）。

### ★★★ 擬似乱数 — `rand`（2026-09-21 ・ で 1 つの名前へ 2026-09-22）

外部ライブラリを持たない計算なので、点群型と同じ理由でここに居る（どの幾何カーネルにも
属さない・`priority` が 0 なので同名 op を持つカーネルを押しのけない）。

```
rand(a, b, n, seed)            a, b = スカラ        → 値の配列（n 個）
rand(a, b, n, seed)            a, b = 2 要素の配列  → pt-cloud2d
rand(a, b, n, seed)            a, b = 3 要素の配列  → pt-cloud3d
```

★★ **名前は 1 つ**で、**第 1 引数の形**が軸の数を決める。内部では
`rand` / `rand#pt2d` / `rand#pt3d` の 3 行で、行を選ぶのは
[マッチ関数](srava_language_reference.html#pseudo-module)（`a` の要素数 0 / 2 / 3）。
行名はキャッシュキー・`ps`・診断に**そのまま出る**ので、どの行が走ったかは外から確かめられる。

★★★ **シードは省略できない**。この op 群の約束は **「同じ引数なら必ず同じ結果」** で、
シードはその引数のひとつだから。省略可能にすると *引数から結果が決まらない* op になり、
srava が結果をキャッシュする前提そのものが崩れる（キャッシュの鍵は引数のハッシュなので、
2 回目は必ず HIT する ⇒ 「毎回違う」は最初から実現しない）。

⚠⚠ 「同じ」の範囲は **機械と OS をまたぐ**。キャッシュは機械の間で持ち運べるので、
Linux で作った値を macOS が HIT させた瞬間に *別の列* が同じ鍵で通ってはいけない。
⇒ 標準ライブラリの分布器は使えない（`std::uniform_int_distribution` は **規格が写像を
決めていない**ので libstdc++ と libc++ で違う値を返す）。生成器（xoshiro256\*\*、種は
splitmix64 で展開）も区間への写像も自前で持ち、`uint64` の算術だけで書いてある。

★ **整数か浮動小数点かは、書かれた `a` と `b` の種別が決める**。規則は 1 つで、
配列を渡したときはそれを **軸ごとに**適用する:

| 書いた区間 | 出る値 |
|---|---|
| `rand(0, 10, …)` | 整数 `[0,10]` **閉**（`0` も `10` も出る） |
| `rand(0.0, 10, …)` | 浮動小数点 `[0,10)` **半開**（右端は出ない） |
| `rand([0,0,0], [4,4,4], …)` | 整数格子上の点 |
| `rand([0,0,0.0], [4,4,4], …)` | x, y は格子・**z だけ連続** |

⚠ 離散と連続で端の扱いが違う（閉と半開）のは意図的。離散の `[a,b]` は候補が `b-a+1` 個
という数え方そのもので、連続の `[a,b)` は区間を割って使うときに端が重ならないという慣例。

- `n` と `seed` は **整数だけ**。浮動小数点を黙って切り捨てると `1.4` と `1.6` が同じシードになり、
  「同じ引数なら同じ結果」の裏返し（*違う引数で同じ結果*）が静かに起きる ⇒ 明示エラー。
- `a > b` は明示エラー（空の区間）。`n = 0` は空の配列 / 空の点群（`points3d([])` と同じ扱い）。
- ⚠ スカラで呼ぶと **値**を返すので、結果は木の上でテキスト化されて運ばれる。10^5 点が
  欲しいなら**配列で呼ぶ**（点群型は cache を通るので値配列の O(N^1.9) を踏まない）。
- ⚠⚠ 点群を返す形は **法線を付けない**。無い法線を埋めると Poisson も RANSAC も
  エラーにならずに走って静かに嘘の形を返す（上の「既定の法線ベクトルは作らない」と同じ理由）。
- ★★ **`a` と `b` の軸数が食い違えば明示エラー**（`a and b must each be an array of 3 numbers`）。
  行を決めるのは `a` だけで、`b` は行が決まった後に照合される。⇒ 両方をマッチで見ると
  食い違いが「どの行も成立しない」= *どこが悪いか言わない* 文言に化けるため（ひさ 2026-09-22）。
- ★ 要素数が **1 や 4** の配列は軸 1 本の行が引き取り、`a must be a number (one axis) or an
  array of 2 or 3 numbers … but it is an array of 4` と**形の話として**断る。
- ⚠⚠ 承知の上の穴: `a` と `b` を **揃えて** 2 要素で書けば、3 軸のつもりでも 2D の点群に
  なる。⇒ **名前を 1 つにした代償** — 旧 `rand_pt3d` は軸数を *op 名と配列の長さで 2 回*
  言っていたので食い違いが見えた。宣言が 1 回になると「3 のつもりだった」がどこにも
  書かれていないので、原理的に検出できない（片方だけ間違えたときは、`a` と `b` が
  2 回言っているので上のエラーで止まる）。
  ★ ただし **型は落ちずに運ばれる**ので、3D 専用の op は型名を挙げて断る（実測）:

  | 下流 | どうなるか |
  |---|---|
  | `volume(hull(p))` | 明示エラー `input types (cg-cross2d)` |
  | `export("*.xyz", p)` | 明示エラー `入力の型 'pt-cloud2d' を受け取れるモジュールが無い` |
  | `estimate_normals(p)` | 明示エラー `accepted: (pt-cloud3d)->pt-cloud3d` |
  | `hull(p)` / `delaunay(p)` | **通る** → `cg-cross2d`（2D の答え） |
  | `vert(p,0)` / `bbox(p)` | **通る** → 成分が 2 つ |

  ⇒ 素通りするのは **2D にも正当な意味がある op** だけ。`type_of(p)` か成分数で確かめられる。

### ★★ 正規分布 — `rand_gaussian`（2026-09-22）

```
rand_gaussian(center,   sigma, n, seed)   center = スカラ        → 値の配列（n 個）
rand_gaussian(center2d, sigma, n, seed)   center = 2 要素の配列  → pt-cloud2d
rand_gaussian(center3d, sigma, n, seed)   center = 3 要素の配列  → pt-cloud3d
```

`rand` と同じく **第 1 引数の形**が軸の数を決める（行は `rand_gaussian` /
`rand_gaussian#pt2d` / `rand_gaussian#pt3d`）。シードは省略できない。

★★★ **`sigma` は「各軸の標準偏差」**。各軸が独立に `N(center[k], sigma)` ＝ 等方ガウス。
⚠⚠ 「**距離の**標準偏差」ではない（2D では距離が Rayleigh 分布になり σ にならない）。
⚠ 1 次元では両者が一致するので、スカラ版だけ見ていると差が出ない。

★★ **対数も自前で持っている**。正規乱数はどの作り方でも対数が要るが、`std::log` は
**規格が正しい丸めを要求していない**ので glibc と Apple libm で最後の 1 bit が違いうる。
⇒ `uniform_int_distribution` を使わないのと同じ理由で、`pt_log_det` を自前で置いた
（IEEE 754 が丸めを規定している演算と `frexp` だけ）。`sqrt` は IEEE が正しい丸めを
要求しているのでそのまま。⇒ **「同じ引数なら同じ結果」が機と OS をまたいで保たれる**。

⚠ Marsaglia polar は 1 回で 2 値できるが、**2 つ目は捨てている**。持ち越すと
「前に何回呼ばれたか」で消費順が変わり、*同じ引数が呼び出し文脈で違う結果*になるため。

- ⚠ 返る値は **常に浮動小数点**（`rand` の「両方整数なら整数」の規則は無い）。
- `sigma = 0` は全点が中心。`sigma < 0` は明示エラー。
- ⚠ 点群を返す形は **法線を付けない**（`rand` と同じ）。

★★ **第 1 引数が点群のとき**は、その各点を中心とする等方ガウスを**均等に重ね合わせた分布**から
`n` 点を引く。「各入力点に中心版を施したもの」とは**違う**。

```
rand_gaussian(pt-cloud2d, sigma, n, seed) → pt-cloud2d
rand_gaussian(pt-cloud3d, sigma, n, seed) → pt-cloud3d
```

- 出力の点数は **n**（入力の K とは無関係）。⚠ K が大きく n が小さいと点を貰わない中心が出る。
- ⚠ **入力点群の順序が結果に効く**。⚠ 空の点群は明示エラー（中心が無いと分布が定義できない）。
- ⚠ K=1 でも中心版とは**列が違う**（中心のくじを必ず引く）。分布は同じ。
- ★ この行だけ **sig が選ぶ**（第 1 引数が幾何なので insets が立つ）。他の 3 行は
  マッチ関数が値の形を見る ⇒ **同じ op 名で選ばれ方が 2 通り**ある。

⚠ `estimate_normals` は**このモジュールには無い**。外部ライブラリを持たないので推定できない。
→ **[cgal.so](#cgal)**（`pca_estimate_normals` + `mst_orient_normals`）と
**[geogram.so](#geogram)**（`Co3Ne_compute_normals`）が、*1 つの op 名で 2 実装*を持つ。
両方ロードしていれば priority で cgal が受け、`"geogram"::estimate_normals(p)` で名指しできる。

⚠ **inline 値配列の道は O(N^1.9)**（`polygon` と同じ制約）。大きな点群は `import` から入れる
（ファイル経由は線形で、値配列の道より桁で速い）。これが点群型を作った動機そのもの。

## pipe_proximity.so {#pipe_proximity}

**概要**: 可変太さ配管の**自己接近検出・距離調整**モジュール。**幾何カーネルにもメッシュ型にも依存しない**
解析モジュールの例で、入出力はすべて値（数・配列・ハッシュ）。中心線 + 半径プロファイルを受け、BVH ベースの
近接計算で最小隙間や制御点の調整結果を返す。

**前提とする外部ライブラリ**: **なし**（コアもソース同梱で、外部取得は発生しない）。

| ライブラリ | バージョン / 取得 | 用途 |
|---|---|---|
| pipeProximity | **同梱**（`modules/pipe_proximity/vendor/pipeProximity/`・MIT・GLOBALBASE UMUT） | BVH 近接計算・距離調整ソルバ |

**サポートする型**: **なし**。幾何カーネルを持たず、mesh の型も扱わない。全 op は
`out=value`（INLINE・数/配列/ハッシュを返す）。

**サポートする op**（5 op・詳細は下記）:

```
pipe_proximity, pipe_adjust, pipe_scene_proximity, pipe_scene_adjust, pipe_sample
```

中心線は **制御点列**で与える: `[[x,y,z], ...]`（先頭 = 始点 S / 末尾 = 終点 E / 中間 = off-curve
制御点 C）。内部では中点法 2 次ベジエ鎖（通過点 = S, mid(Cᵢ,Cᵢ₊₁)…, E）として扱う。

### 半径プロファイル（`radius`）

すべての op で `radius` 引数は**形で自動判別**される（弧長 s → 半径）:

| `radius` | プロファイル |
|---|---|
| スカラ `r` | 一定半径 `r` |
| フラット `[r0, m]` | 指数 `r(s) = r0·exp(m·s)`（`m>0` 太る / `m<0` テーパ・**無限に伸びる**） |
| フラット `[r0, m, p1, r1]` | 指数を**弧長 p1 以降は `r1` に固定**（p1 で不連続）。指数の伸びを途中で止める |
| フラット `[r0, m, p1, p2, r2]` | 指数を `p1`→`p2` で**線形に `r2` へ移行**し、`p2` 以降 `r2` 固定（不連続を避けたい時） |
| ネスト `[[s,r],…]` | 弧長キーポイントの**線形補間**（s 昇順に整列・範囲外は端値クランプ） |

- 4/5 要素の弧長クランプは**指数形にだけ**効く（ネスト線形補間形には効かない）。用途: コイルの**尾＝引っ張り部**は太さが邪魔になるので、途中から細く固定する等。スカラやネスト形で同じことをしたいときは、`r1`/`r2` を細くした区間をネストで直接書けばよい。
- `p1`/`p2` は**弧長**（始点からの距離）。`[r0, m, p1, r1]` は p1 で指数値 `r0·exp(m·p1)` から `r1` へ飛ぶ。連続にしたいなら `[r0, m, p1, p2, r2]` で `p1`→`p2` の傾斜区間を設ける。

### `pipe_proximity(ctrl_pts, radius, report_gap)` — 自己接近検出

`report_gap` 以下の自己接近を gap 昇順で返す。

```
var pts  = [[0,0,0],[12,0,0],[10,2,0],[12,4,0],[0,4,0]];
var hits = pipe_proximity(pts, 0.8, 1.0);
// hits = [[gap, pA, pB, normal, sA, sB, rA, rB], ...]   (無ければ [])
//   gap=表面間隙, pA/pB=両壁の接近点, normal=法線, sA/sB=弧長, rA/rB=その点の半径
```

### `pipe_adjust(ctrl_pts, radius, params)` — 距離調整コントローラ

自己接近する設計を、一様クリアランス `gap >= dMin` を満たすよう制御点を動かす
（ペナルティ/拡張ラグランジュ法の勾配降下）。

```
var res = pipe_adjust(pts, 0.8, { dMin: 0.6, maxIter: 400, fixEnds: 1 });
// res = { ctrl:[[x,y,z],...], iters, energy, clearViolation, feasible }
//   ctrl = 調整後の制御点(入力と同じ並び)。そのまま tube_ruled / pipe_proximity に渡せる。
//   clearViolation = max(0, dMin-gap) 残差(0 に近いほど達成)。feasible = 硬拘束が両立したか。
var moved = pipe_adjust(pts, 0.8, {dMin:0.6}).ctrl;
```

`params` ハッシュのキー（すべて任意・省略時は既定値）:

| キー | 既定 | 意味 |
|---|---|---|
| `dMin` | 0.5 | 目標クリアランス（`gap >= dMin`） |
| `maxIter` | 200 | 反復上限 |
| `solver` | `"grad"` | ソルバ選択。`"grad"`=勾配降下（全点同時にフル勾配方向）/ `"cd"`（または `cd:1`）=**座標降下**（各制御点を軸並行に1点ずつ line search）。下記 |
| `cdPitch0` | 8.0 | 座標降下の初期 pitch（1点1軸の試行移動量。半減しながら探索） |
| `cdPitchMin` | 0.01 | 座標降下の最小 pitch（これ未満で各点を打ち切り） |
| `sweep` | `"forward"` | 座標降下の点スイープ方向。`"forward"`=DOF 0→末尾（固定根 c0 側から）/ `"reverse"`（または `"tail"` / `cdReverse:1`）=末尾→0（**可動な尾側から**）。`solver:"cd"` 時のみ有効。下記 |
| `parallel` | `0` | 座標降下の並列化。`0`=直列 Gauss-Seidel（既定）/ `1`（または `"jacobi"`）=**接触グラフ彩色のブロック Jacobi**（非干渉な制御点を同時更新）。`solver:"cd"` 時のみ。下記 |
| `threads` | `0` | スレッド上限。`0`=自動（コア数−2）/ `1`=直列（スレッド不使用）。`parallel:0` でも `threads>1` なら各点の試行評価を並列化（結果は直列と一致） |
| `fixEnds` | 1 | 端点 S,E を固定（配管の取り合いを保つ） |
| `wBend` | 0.1 | 曲げ正則化の重み（大きいほど滑らか） |
| `wSpace` | 0 | **制御点間隔の均一化**の重み。隣接する区間長の差の二乗を罰し、接触の無い区間で制御点が密集／崩壊するのを防ぐ。`0`=無効（従来）。下記 |
| `fixed` | `[]` | 固定する**制御点**の設計点 index（`0=S, 1..m=C, m+1=E`） |
| `pins` | `[]` | **通過点**ピン。各 `{joint, at:[x,y,z], hard}`（中点 Mⱼ=(Cⱼ+Cⱼ₊₁)/2 を `at` へ。`hard`=厳密 / 省略=ソフト） |
| `fZ` | 0 | 外力: z 並行（**負で重力下向き**）。`U=-fZ·z` |
| `fAxis` | 0 | 外力: z 軸へ（正で束ねる）。`U=fAxis·ρ` |
| `fOrigin` | 0 | 外力: 原点へ（正で集める）。`U=fOrigin·r` |
| `separate` | 1 | energy 後段の**射影的分離パス**を行うか（重なりを押し広げる。下記） |
| `sepGain` | 0.25 | 分離の押し離しゲイン |
| `sepIter` | 1000 | 分離反復上限 |
| `sepLambda` | 0.15 | 分離の移動量平滑化（ジグザグ抑制。0=切る） |

> **射影的分離パス**: energy 勾配法は「ほぼ足りた隙間を詰める」のは得意だが「重なりを**押し広げる**」のは
> 苦手（接触の再検出で非平滑になり局所平衡で停止、gap≈0 では接触法線が不安定）。そこで energy の後に、
> 検出した接触を**中心線間の安定な方向**へ**食い込み量 `dMin−gap` だけ**押し離す射影的緩和を回す
> （`segDesignWeights` で制御点へ厳密分配・移動量を平滑化してキンク抑制・固定 DOF は除外）。
> これにより**ピッチ≈2r で隣接ターンが接触するコイル等でも、設定そのままで gap≥dMin の有効メッシュに開く**。
> 既定で ON。energy 法の挙動が良いケースでは違反ゼロなので即 no-op。`separate:0` で切れる。
> 後方互換として位置引数 `pipe_adjust(ctrl, radius, dMin, maxIter, fixEnds, wBend)` も受け付ける。

> **ソルバ選択（`solver`）**: 同じエネルギー `wLen·L + wBend·∫κ² + wPenalty·[dMin−gap]₊²` を最小化するが、
> 探索の進め方が違う。
>
> - `"grad"`（既定）= **勾配降下**。全制御点を一度にフル勾配方向へ動かし、グローバルな1ステップ
>   `α=stepMax/|g|` を line search。滑らかで速いが、**全点同時の歩幅が最大勾配に支配される**ため、
>   深く重なった自己接触（太い管が斜めに交差・密巻きコイル等）では局所平衡に嵌り、`clearViolation`
>   が残ることがある。
> - `"cd"` = **座標降下**（非線形 Gauss-Seidel）。各制御点を**1点ずつ・軸並行に** `cdPitch0→cdPitchMin`
>   と pitch を半減しながら個別に line search する。1点ずつなので**他点の巨大勾配に歩幅を奪われず**、
>   勾配降下が解けない**深い重なりを `gap≥dMin` まで押し切れる**ことが多い（クリアランスを厳密化したい
>   ケースで有効）。代償として**計算は重い**（1 sweep で全点×全軸を試行評価）。
>
> 迷ったら既定の `"grad"` で試し、`clearViolation` が残る／重なりが解けないときに `solver:"cd"` へ。
> なお N 体 `pipe_scene_adjust` も同じ `solver` キーを受ける。
>
> **スイープ方向（`sweep`・cd 専用）**: 座標降下は Gauss-Seidel なので**点を回す順序で収束経路が変わる**。
> 既定 `"forward"` は DOF 0（＝固定根 `c0`）側から末尾へ。固定端の隣＝最も動きにくい側から始まるため、
> 全体が動き出すまでが遅いことがある。`sweep:"reverse"` は**自由な尾側から**回す。コイルを根本固定で
> 引っ張るような構成では、可動端から順に動かす方が**1 sweep あたりの全体移動が大きく**、同じ反復数で
> より低いエネルギー／違反へ届くことがある（固定点指定 `fixed`/`pins` はどちらの向きでもそのまま尊重される。
> 配列を反転させる必要はない）。勾配降下（`solver:"grad"`）は全点同時更新なので順序非依存＝この指定は無視。
>
> **並列化（`parallel`/`threads`・cd 専用）**: cd は各点で「6方向×pitch段」の全エネルギー評価を回すため重い。
> 2 段の並列化を選べる（既定は完全直列で従来と厳密一致）:
 - **`threads>1`（`parallel:0` のまま）= L1**: 各点の **6 試行を並列評価**。点の更新順は直列のままなので**結果は直列と完全一致**（安全な高速化）。ただし並列度は**最大 6**（軸×符号の試行数）で頭打ち。
> - **`parallel:1` = L2（接触グラフ彩色ブロック Jacobi）**: 「隣接でも接触でもない＝非干渉」な制御点を**点×6試行の細粒度で一括並列**更新する（並列度 = 6×活性点・最低 6・6 の倍数）。L1 の 6 倍上限を超えて多コアを使える。
>   **結果は直列とは異なる**（色順で点を回す＝順序依存の別最適化＋長距離の弧長カップリング無視。`sweep` の向き違いと同様に別の最適点に到達する）。deterministic（再現性あり）。
>   弧長が大きく動く初期や厳密一致が要る場面は L1/直列、位置が固まった**微調整段や接触が疎な構成で L2** が効く。彩色は外ループ毎に再計算。
>
> スレッド数は `threads` で制御する。⚠ **L1 の並列度は 6（軸×符号の試行数）で頭打ち**、
> L2 の並列度は **6 ×（同じ色に入る活性点の数）**なので、`threads` を増やしても彩色の結果より上には行かない。
>
> **制御点間隔の均一化（`wSpace`）**: 最適化エネルギー `wLen·L + wBend·∫κ² + wPenalty·[dMin−gap]₊²` は、
> 制御点を曲線に沿って**接線方向へスライド**させる変形にほぼ不感（弧長も曲率もほぼ不変）＝ヌルモードを持つ。
> このため接触に拘束されない区間（マンドレルから離れて空中に張り出した尾部など）では、`solver:"cd"` の
> line search が点をこの方向へ流し、**制御点が始点側へ寄って一部の区間長が極端に狭くなる（隣接点がほぼ
> 重なる）**ことがある。`wSpace>0` は全 DOF 折れ線 `[S, C…, E]` の**隣接区間長の差** `Σ(|eᵢ|−|eᵢ₋₁|)²` を
> 罰する「間隔を揃えるばね」で、このヌルモードを抑えて制御点を均一に分布させる。局所差分なので、巻きつき部と
> 尾部のような**自然な間隔差は緩やかに許容**し、崩壊だけを防ぐ。
>
> **目安 0.05〜0.2**（大きいほど均一）。接触ペナルティより十分小さく効くので `gap≥dMin` の達成をほとんど
> 乱さない（崩壊した区間長が是正され、`clearViolation` はほぼ変わらない）。既定 `0`＝従来と完全一致。
> `pipe_adjust` / `pipe_scene_adjust` 共通のキー。

### `pipe_scene_proximity(bodies, report_gap)` — N 体近接検出

複数配管をまとめて検出する。`bodies` は body の配列、各 body = `{ctrl, radius, movable}`。

```
var bodies = [
  {ctrl: [[0,2,0],[14,2,0],[0,2.2,0]], radius: 0.8, movable: 1},
  {ctrl: [[0,4,0],[14,4,0]],           radius: 0.8, movable: 0}
];
var hits = pipe_scene_proximity(bodies, 8.0);
// hits = [[gap, pA, pB, normal, sA, sB, rA, rB, bodyA, bodyB], ...]
//   単一 op より末尾に bodyA/bodyB(Body 番号) が付く。
//   可動 body の自己接近 + 異 body 間の交差を返す(固定–固定ペアはスキップ)。
```

### `pipe_scene_adjust(bodies, movableIdx, params)` — N 体距離調整

`movableIdx` の body を、他の**固定 body 群を障害物**として `gap >= dMin` へ調整する（adjustScene・
可動 1 本モデル）。`params` は `pipe_adjust` と同じハッシュ。`fixed` / `pins` は可動 body の DOF に効く。

```
var res = pipe_scene_adjust(bodies, 0, { dMin: 0.5, maxIter: 400, fixEnds: 1, fZ: -0.3 });
// res = { ctrl, iters, energy, clearViolation, feasible }  (= 可動 body の調整後 ctrl)
```

### `pipe_sample(ctrl_pts, radius, pitch)` — 弧長等間隔サンプル(tube_ruled 化用)

中心線を**弧長等間隔ピッチ** `pitch`(mm)でサンプルし、各点に半径 `R(s)` を付けて返す。可変太さ管を
`tube_ruled` で可視化するときに使う(弧長 s と半径の対応をライブラリの正確な弧長で評価するので、srava 側で
弧長計算も r(s) 評価も不要)。

```
var samples = pipe_sample(ctrl, [[0,1.0],[26,0.35]], 0.6);  // テーパ管・ピッチ 0.6mm
// samples = [[ [x,y,z], r ], ...]   → そのまま tube_ruled に渡せる
var solid = tube_ruled(samples, 24);
```

- `pitch` 省略/`<=0` で `Smax/64`。点列は**弧長等間隔**(`arcAt` の単調逆引き)。
- **端点(s=0, Smax)と半径キーポイント(`[[s,r]]` の各 s)を強制的に含める** → 管端とテーパの折れがクッキリ出る。
- `radius` は他の op と同じ 3 形態。`pipe_adjust` の戻り `ctrl` をそのまま渡せば調整後の管が描ける。

---

## デモ／テスト用モジュール — demo / d2 / d3 / d4 / d5 {#demo}

**概要**: モジュール機構そのものを検証・実演するための小さなモジュール群。**幾何カーネルではなく**、
CGAL / Manifold / srava 言語を一切参照しない。既定でビルドされ、ctest が使う。

> ⚠ **実用の op は持たない**。`priority` が**負値**なので既定の幾何カーネルとして選ばれることはない
> （最下位群・同点回避のため互いに違う値を持つ）。

| モジュール | priority | 型（4CC） | op | 実行方式 | 何を実演するか |
|---|---|---|---|---|---|
| `demo.so` | `-1` | なし（値のみ） | `demo_add` / `demo_range` / `demo_spin`（★ 中断できる「重い op」・graceful teardown の検証用）<br>`demo_pick` / `demo_only` / `demo_xy`（★ **`op#変種` の検定台**・下の注） | `PROCESS` のみ | 最小のモジュール。value op だけ・入力は全て inline・出力は value でキャッシュ読み書きが無い |
| `d2.so` | `-3` | `d2-shape2d`(`D2S2`) | `d2_square` / `dcount` | `THREAD｜PROCESS`・既定 process | d3 と**同じ op 名 `dcount`** を自分の次元型で申告し、入力型（次元）でのディスパッチを示す |
| `d3.so` | `-2` | `d3-mesh3d`(`D3M3`) | `d3_cube` / `d3_merge` / `d3_nfaces` / `d3_nverts` / `dcount` | `THREAD｜PROCESS`・既定 process | **mesh を出力する**モジュールを、host 無改修で（`.so` を探索路に置くだけで）走らせる |
| `d4.so` | `-4` | `d4-mesh3d`(`D4M3`) | `d4_cube` / `d4_merge` / `d4_nfaces` / `d4_nverts` / `d4_wedge`（★ **わざと居座る** op・中断要求を一切見ない in-proc 実行体を作るテスト用フック） | `THREAD｜PROCESS`・**既定 thread** | **in-proc で mesh を消費**する。別モジュール（in-proc の manifold）が作った mesh を受け取れるか＝モジュール間の型変換 |
| `d5.so` | `-5` | `d5-mesh3d`(`D5M3`) | `d5_cube` / `d5_merge` / `d5_nfaces` / `d5_nverts` | `THREAD｜PROCESS`・**既定 thread** | d4 と同型だが**別の自型**を持つ。同一の mesh を d4 / d5 が各々の自型で受ける＝変換の**多型共存** |

> ★★ **`op#変種` の検定台** (2026-09-19): `demo.so` の後半 3 本は、記述子が
> *同じ op 名で複数の行*を持てる仕組み（`op#変種`）を、幾何を一切持ち込まずに固定するためにある。
> 行の選び方が壊れても幾何の結果は変わらないので、**カーネルのテストでは暴けない**。
>
> | op | 行 | 何を固定するか |
> |---|---|---|
> | `demo_pick` | `#a`（マッチ関数つき・第 1 引数が `1` のときだけ成立）／`#b`（無条件） | **1 行目が外れて 2 行目が選ばれる**こと。`demo_pick(1)` は `#a`・`demo_pick(9)` は `#b` |
> | `demo_only` | `#b` だけ（基底行を持たない） | **基底行が無いモジュールでも成立する**こと |
> | `demo_xy` | `#flat`（行列が `z=0` 平面を平面へ写すときだけ成立・→ `1`）／`#any`（無条件・→ `2`） | 共通のマッチ述語が効くこと。★ 肝は `rotate("x",180)` 相当（`sin(π)` = 1.2e-16）が **`1` を返す**こと — 厳密な `0` と比べる実装だと正当な変換を面外と誤判定する |
>
> ⚠ **変種の行を先に書く**。無条件の行（`#` の無い行）を前に置くと変種が永久に選ばれないので、
> 記述子のロード時検査（`pig_descriptor_violation`）が弾く。

## 例（同梱 pipe_proximity）

| ファイル | 内容 |
|---|---|
| `examples/pipe_clearance.sra` | 自己接近の検出 + 接近点の球マーカ可視化（検出のみ） |
| `examples/pipe_adjust.sra` | 詰まった折り返しを `pipe_adjust` で開く（調整前/後を 3MF に） |
| `examples/pipe_scene.sra` | 固定障害物配管を避けて可動配管を `pipe_scene_adjust` で調整（N 体） |
| `examples/pipe_taper.sra` | 可変太さ(テーパ)管を調整し `pipe_sample` で per-vertex 半径つき tube_ruled 化 |
| `examples/pipe_variable.sra` | 太さが弧長に沿って変わる管(指数フレア / キーポイント紡錘形)を `pipe_sample` で生成 |

可視化は 3MF（面色保持）に出力。定数太さのデモは中心線を srava 側でサンプルして `tube_ruled` するが、
**可変太さは `pipe_sample`** を使うと弧長↔半径の対応をライブラリの正確な弧長で評価でき、解析と一致する。

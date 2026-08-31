# srava モジュールリファレンス

srava は、本体を再ビルドせずに機能を足せる **モジュール機構**を持つ。モジュールは
**ダイナミックリンクの単位**（`.so`）で、登録した **op 名**を srava プログラムから普通の関数のように
呼べる。実行体（host）は **`module()` が呼ばれた時点で** `.so` を **dlopen** して記述子を読み、どのモジュールが
どの op・どの **型**（mesh の型・4CC と 1:1）を扱うかを登録する（下記）。同名 op が複数モジュールにあるとき、
host は入力 mesh の**型でディスパッチ**先を決める（例: `union` の入力が `mf-mesh3d` なら manifold.so、
`cg-mesh3d` なら cgal.so）。pipe_proximity のような**解析モジュール**は値（数・配列・文字列・ハッシュ）
だけをやり取りし、型も**幾何カーネル**（CGAL/Manifold などの幾何コア・モジュールだけが知る）も知らない。

このリファレンスは同梱モジュールの**利用者向け仕様**を扱う。**同梱は全 18 モジュール**:

| 分類 | モジュール | 既定 |
|---|---|---|
| 幾何カーネル | [cgal.so](#cgal) / [manifold.so](#manifold) | **ON** |
| 幾何カーネル | [nef_hybrid.so](#nef) | **ON** |
| 幾何カーネル | [nef_snc.so](#nef) | **OFF**（`-DSRAVA_MODULE_NEF_SNC=ON` で追加） |
| 幾何カーネル | [geogram.so](#geogram) / [cherchi.so](#cherchi) | **ON** |
| ボリューム | [openvdb.so](#openvdb) | **ON** |
| ボリューム橋渡し | [openvdb_mf.so / openvdb_cg.so / openvdb_gg.so](#openvdb_bridge) | **ON**（`openvdb_cg` は CGAL をリンクするので **GPL**・`-DSRAVA_MODULE_OPENVDB_CG_GPL=OFF` で外せる） |
| B-rep | [occt.so](#occt) / [occt_mf.so](#occt_mf) | **ON**（system の OpenCASCADE が要る） |
| 解析 | [pipe_proximity.so](#pipe_proximity) | **ON** |
| デモ／テスト | [demo.so / d2.so / d3.so / d4.so / d5.so](#demo) | **ON** |

**2026-08-31 以降、同梱モジュールは既定で全部ビルドされる**（`nef_snc.so` を除く）。
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
include "module/all.sra";        // 実カーネル一式を optional で一括ロード (lib/module/all.sra)
```
```sh
SRAVA_MODULE_ALL=1 srava foo.sra   # 環境変数でも同じことができる
```
> ⚠ `SRAVA_MODULE_ALL=1` は**ソース先頭に `include "module/all.sra";` を差し込むだけ**なので、
> `include` と同じ解決規則に従う。**`all.sra` が見つかる場所に居ることが前提**で、具体的には
> `$SRAVA_PATH`(コロン区切り)か、install 済みの `$PREFIX/share/srava/lib`(コンパイル時に焼かれる)。
> **ビルドツリーには `lib/` が置かれない**ので、install せずにビルドツリーで使うなら
> `SRAVA_PATH=<ソースツリー>/lib` を明示する。見つからなければ **`include: cannot find` で明示エラー**に
> なる(探した場所がメッセージに出る)。黙って無視はしない。
> ⚠ `all.sra` に入るのは cgal / geogram / manifold / occt / openvdb / **nef_hybrid**。
> `nef_snc.so` は同一プロセスに 2 変種を同居させる特殊用途なので入っていない。
> ⚠ さらに `nef_snc.so` は **既定でビルドもされない**（2026-08-31 以降）。使うには
> `-DSRAVA_MODULE_NEF_SNC=ON` でビルドし直す必要がある — 入れずに `module("nef_snc.so",{})` を
> 呼ぶと `cannot open shared object file` になる。
> ⚠ 橋渡しモジュール（`openvdb_mf` / `openvdb_cg` / `openvdb_gg`）も入っていない。
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
occt_mf  (abi=16 prio=0 /usr/local/lib/srava/modules/occt_mf.so)
    exec_caps=process(0x2)  exec_default=process  make_agent=yes
    arity=0  import=-  export=-  hash_salt=yes  initialize=no  configure=no
    ops (1):
      triangulate        nin=2 wire=1
        sig = (oc-brep3d)->mf-mesh3d
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
module("openvdb.so", { arity: 16 });           // ⑥ n 項ブールを何項まで 1 回で渡すか (#3436 P4)
module("geogram.so", { threads: 4 });          // ⑦ op 内並列の上限 (受け取るのはモジュール側)
```

第 2 引数は**ハッシュ**または**文字列**（省略可）。戻り値は解決したモジュール名（文字列）:

| 第 2 引数 | 効果 |
|---|---|
| （なし） | 未ロードなら読み込む（糖衣）|
| `{priority: N}` | **既定幾何カーネルの優先度**（整数）を上書き。型が定まらない leaf op（例: 引数だけの `box`）で、どのモジュールを既定にするかを決める。**大きいほど優先**。★**同点の勝敗は不定**（ロード順 = 走査順に依存）なので、確実に切り替えるなら既存の最大値より大きい値にする |
| `{exec_default: "thread"｜"process"}` | **実行方式**。`"thread"`=planner 内 in-proc / `"process"`=別プロセス `srava_agent`。省略時は記述子の既定（cgal=process 相当・manifold=thread） |
| `"off"` | **アンロード（`dlclose`）**（2026-08-28）。そのモジュールの型・4CC・codec・実行体・拡張子は**どこからも見えなくなる**（#3439）。以後 `module(so,{...})` で読み直せる。★**一度でも使われたモジュールは落とせない**（`.so` 由来のオブジェクトが生きうるため明示エラー）。★**未ロードへの `"off"` も明示エラー** — 載っているかは `module_loaded(so)` で判定する。旧 `"on"` は撤去（再ロードは `module(so,{...})`）|
| `{optional: 1}` | **見つからない / ロードできなくてもエラーにしない**（静かに諦める）。ビルド構成によって存在しない `.so` があるため、便宜スクリプト（`module/all.sra`）はこれで書かれている |
| `{arity: N}` | **n 項ブールを何項まで 1 回の呼び出しで渡すか**（#3436 P4）。記述子の申告（そのモジュールが受けられる最大項数）とは別で、こちらは **policy**（実際に何項で渡すか）。「木の段数を減らす」ことと「1 回の呼び出しを重くする」ことのトレードオフを、スクリプトを変えずに切り替えられる。★ 受けられる上限はモジュールが決める（例: geogram / cherchi は 32）ので、それを超える指定は上限で頭打ちになる |
| `{threads: N}` | **op 内並列の上限**。受け取るのは記述子の `configure` フックを持つモジュールだけ（現在は geogram / openvdb）。`N<=0` は「制限を解除して既定へ戻す」。⚠ 絞れば速くなるとは限らない（同上） |

- 選択そのものは型でディスパッチされるので、`union(mfBody, mfBody)` は `module` 無しでも manifold.so に行く。
  `module(..., {priority})` が効くのは**型が決まらない生成 op の既定**（どの幾何カーネルで `box` を作るか等）。
- `{exec_default}` は機能は変えず**実行方式だけ**を変える（in-proc は速い・process は重い op を分離できる。
  詳細は[モジュール設計](srava_module_design.html)）。

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
| `nef_snc` / `nef_hybrid` | `PROCESS` のみ | process | 同上（Nef も EPECK 上に構築される） |
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
| HDF5 | `find_package(HDF5 COMPONENTS C)`（`export_vox` 用のみ） | ボクセル書き出し |

**サポートする型**（この 2 型を読み書き）:

| 型名 | 4CC | 4CC(readonly) | 意味 | 幾何カーネル |
|---|---|---|---|---|
| `cg-mesh3d` | `MESH` | `MFM3` | 3D ソリッド mesh | CGAL EPECK |
| `cg-cross2d` | `PLY2` | `MFC2` | 2D 断面（`Polygon_with_holes_2`） | CGAL EPECK |

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
union, combine, intersection, difference,
export, export_vox, translate, rotate, mirror, scale, transform,
color, rect, ngon, circle, polygon, line, extrude, tube, revolve, offset(2D のみ),
area, valid, repair, section, volume, perimeter, centroid, bbox,
distance, closest, farthest, thin_spots, cast
```

cgal.so 固有の op: **pyramid, line, export_vox, repair, perimeter, distance,
closest, farthest, thin_spots**（manifold.so には無い）。`tube` と `color` は #3415 で manifold.so にも
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

**サポートする型**（この 2 型を読み書き）:

| 型名 | 4CC | 4CC(readonly) | 意味 | 幾何カーネル |
|---|---|---|---|---|
| `mf-mesh3d` | `MFM3` | `MESH` | 3D mesh | Manifold（double） |
| `mf-cross2d` | `MFC2` | `PLY2` | 2D 断面 | Manifold / clipper2 |

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
box, boxa, sphere, icosphere,
union, intersection, difference, combine,
export, cast, polygon, prism, revolve,
translate, rotate, scale, mirror, transform,
area, valid, volume, bbox, centroid,
import, rect, circle, ngon, extrude, section, offset, tube, color
```

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

`tube` は cgal.so と**同じ共通ヘッダ**（`src/h/common/tube.h`）で掃引を生成するので、頂点座標・三角形の
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

**サポートする op**:

| 分類 | op |
|---|---|
| 生成 | `box` / `boxa` / `sphere` |
| ブール | `union` / `intersection` / `difference` / `complement` |
| Nef 固有 | `minkowski` / `offset`（**3D**）/ `convex_decomposition` / `nparts` / `part` / `unify` / `solidify` |
| 変換 | `translate` / `cast` |
| 計測 | `nverts` / `nfaces` / `volume` |
| I/O | `export` |

**実行方式**: `PROCESS` のみ（`exec_default=process`）。Nef も EPECK 上に構築されるので値が共有に耐えない。

## geogram.so {#geogram}

**概要**: srava の **3 つめの厳密幾何**モジュール（#3435）。Bruno Lévy の
[geogram](https://github.com/BrunoLevy/geogram)（BSD-3）を用い、**mesh arrangement + 厳密述語 / 厳密構成**で
二項ブールを行う（arXiv:2405.12949 "Exact predicates, exact constructions and combinatorics for mesh CSG"）。
CGAL corefinement と**同じ厳密解**を出しながら、多重ブールで**桁違いに速い**のが特徴。

> **既定では OFF**。`cmake -DSRAVA_MODULE_GEOGRAM=ON` で有効化する（外部から取得してビルドする依存で、
> 比較のためのカーネルなので既定経路には要らない）。

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
  codec 行の `types` の申告と**型スタンプ**が担い、キャッシュの弁別は `hash_salt` が担うので衝突しない。
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
| 生成 | `box` / `boxa` / `sphere` |
| ブール（二項） | `union` / `intersection` / `difference` |
| 変換 | `translate` / `cast` |
| 計測 | `volume` / `nverts` / `nfaces` |
| 出力 | `export`（`off` / `stl` / `obj` / `ply`） |
| ★固有 | **`solidify`** |

- `sphere` は共通生成器 `common/geodesic.h` を使うので、頂点と面の並びが cgal / manifold / nef と一致する。
  ただし**体積は最下位桁がずれる**（geogram は発散定理を double で積む・cgal は厳密有理数を積んで最後に丸める）。
- ★**`solidify(m)`** — 自己交差した閉メッシュから**内外を決め直して**ソリッドにする。arrangement で交差を解き、
  radial sort で外側シェルだけを残す。**cgal は自己交差を素通りして誤った体積を返し、manifold も同じ誤値、
  nef は SNC を組めない**ので、これは geogram を入れる質的な理由のひとつ（→ #3445）。
  nef の同名 op（別実装）と独立に同じ値を出すことを回帰テストで固定している。
- ★**多オペランドに対応済み**（#3436 P4）。geogram は facet 属性 `operand_bit` で **N 項ブール**を
  持っており、`module("geogram.so", {arity: N})` で「何項を 1 回の arrangement に渡すか」を選べる
  （上限 32 = `operand_bit` の幅）。既定は二項。

---

## cherchi.so {#cherchi}

**概要**: srava の **4 つめの厳密幾何**モジュール（#3438 P6）。Cherchi らの
[Interactive and Robust Mesh Booleans](https://github.com/gcherchi/InteractiveAndRobustMeshBooleans)（MIT・以下 IRMB）を用いる。
geogram と同じ **mesh arrangement** 系だが、厳密性の作り方が違う — 交点の座標を明示的に構成せず、
**indirect predicates**（「どの 3 平面の交わりか」という間接表現）のまま厳密述語を評価するので、
有理数展開を避けたまま判定できる（SIGGRAPH Asia 2020 / ACM TOG 2022）。

> **既定では OFF**。`cmake -DSRAVA_MODULE_CHERCHI=ON` で有効化する。

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

**op**: `box` / `boxa` / `sphere` / `union` / `intersection` / `difference` / `volume` / `nverts` / `nfaces` /
`export`（off,stl,obj）/ `cast` / `translate`。

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

**⚠ `solidify`（#3445）は持たない**。IRMB の分類は「**他の label の内側か**」で決まるため、
自己交差した *1 枚の* メッシュには効かない（重なる 2 箱を 1 ラベルで union させても内側の面が落ちない）。
**汚い入力を食えるのは arrangement までで、内外の決め直しは label 側の話**である。
連結成分ごとに label を振れば成分どうしの自己交差は解けるが、#3445 の tube は 1 成分なので救えない。
`solidify` は geogram / nef が持つ。

**テスト**: `srava_cherchi_{bool,arity,contact,guard,mfcross,cgcross}` と、カーネル一致
`srava_agree_cherchi_{box,sphere,union,difference}`（基準は cgal・leaf は 1e-12 / ブールは 1e-9）。

## openvdb.so {#openvdb}

**`-DSRAVA_MODULE_OPENVDB=ON`（既定 OFF）**。OpenVDB（AcademySoftwareFoundation・Apache-2.0）を
FetchContent で取得して静的リンクする（規約 B）。

> ★ **TBB だけは共有ライブラリ**として `$PREFIX/lib` へ install する。TBB は**スケジューラの状態を
> 持つランタイム**なので、静的リンクして複数の `.so` に埋めると 1 プロセスに**スレッドプールが
> 2 つ**できる（`nef_snc` / `nef_hybrid` で踏んだシンボル衝突の裏返し）。将来 Manifold `PAR=ON`
> （#3419）も TBB を使うため、**ビルド全体で 1 インスタンス**を規則とした。
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
| 出入り | `voxelize(mesh, dx)` / `isosurface(v, iso)` |
| ブール（二項） | `union` / `intersection` / `difference` |
| 加工 | `offset(v, d)` / `renormalize(v[, halfWidth])` |
| 計測 | `volume` / `voxels` |

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

**実行方式**: `THREAD｜PROCESS`・既定 `process`（本体 openvdb.so と同じ。→ [並列性](#exec)）

## occt.so {#occt}

**`-DSRAVA_MODULE_OCCT=ON`（既定 OFF）**。Open CASCADE Technology（LGPL-2.1 + 例外）を
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
| 生成 | `box(w,h,d)` / `sphere(r, seg)` / `cylinder(r, h)` / `torus(R, r)` |
| ブール（二項） | `union` / `intersection` / `difference` |
| 加工 | `offset(s, d)` / **`fillet(s, r)`** / **`chamfer(s, d)`** |
| 入口 | `import(path)` … **STEP / .brep** |
| 出口 | `export(path, s)` … **STEP / .brep**（`triangulate` は別モジュール → [occt_mf.so](#occt_mf)） |
| 計測 | `volume` / `nfaces` |

#### ★ 生成 — どれも厳密で、分割数を持たない

| op | 体積 | Face 数 |
|---|---|---|
| `sphere(r[, seg])` | 4/3·π·r³（**`seg` に依存しない** — 球面 1 枚として持つ） | 1 |
| `cylinder(r, h)` | π·r²·h | **3**（円筒 1 + 平面 2） |
| `torus(R, r)` | 2π²·R·r² | **1** |

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
メッシュ形式（STL/OBJ…）へ書くと三角形に落ちるので、B-rep を保ったまま外へ出す出口は STEP / `.brep`。

## occt_mf.so {#occt_mf}

**概要**: **occt とメッシュ系の橋渡しモジュール**。`triangulate` 1 op だけを持ち、
B-rep（`oc-brep3d`）から**本物の `mfMesh`**（`mf-mesh3d`）を作る。

> ⚠ `-DSRAVA_MODULE_OCCT=ON` かつ manifold が有効なときにビルドされる。使うときは
> `module("occt.so", {}); module("occt_mf.so", {});` の**両方**をロードする。

**サポートする型**: `oc-brep3d`(BREP) を読み、`mf-mesh3d`(MFM3) を書く。

**サポートする op**: `triangulate(s, deflection)` — `deflection` は弦の最大距離（世界座標の長さ）。

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
//   ctrl = 調整後の制御点(入力と同じ並び)。そのまま tube / pipe_proximity に渡せる。
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

### `pipe_sample(ctrl_pts, radius, pitch)` — 弧長等間隔サンプル(tube 化用)

中心線を**弧長等間隔ピッチ** `pitch`(mm)でサンプルし、各点に半径 `R(s)` を付けて返す。可変太さ管を
`tube` で可視化するときに使う(弧長 s と半径の対応をライブラリの正確な弧長で評価するので、srava 側で
弧長計算も r(s) 評価も不要)。

```
var samples = pipe_sample(ctrl, [[0,1.0],[26,0.35]], 0.6);  // テーパ管・ピッチ 0.6mm
// samples = [[ [x,y,z], r ], ...]   → そのまま tube に渡せる
var solid = tube(samples, 24);
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
| `demo.so` | `-1` | なし（値のみ） | `demo_add` / `demo_range` | `PROCESS` のみ | 最小のモジュール。value op だけ・入力は全て inline・出力は value でキャッシュ読み書きが無い |
| `d2.so` | `-3` | `d2-shape2d`(`D2S2`) | `d2_square` / `dcount` | `THREAD｜PROCESS`・既定 process | d3 と**同じ op 名 `dcount`** を自分の次元型で申告し、入力型（次元）でのディスパッチを示す |
| `d3.so` | `-2` | `d3-mesh3d`(`D3M3`) | `d3_cube` / `d3_merge` / `d3_nfaces` / `d3_nverts` / `dcount` | `THREAD｜PROCESS`・既定 process | **mesh を出力する**モジュールを、host 無改修で（`.so` を探索路に置くだけで）走らせる |
| `d4.so` | `-4` | `d4-mesh3d`(`D4M3`) | `d4_cube` / `d4_merge` / `d4_nfaces` / `d4_nverts` | `THREAD｜PROCESS`・**既定 thread** | **in-proc で mesh を消費**する。別モジュール（in-proc の manifold）が作った mesh を受け取れるか＝モジュール間の型変換 |
| `d5.so` | `-5` | `d5-mesh3d`(`D5M3`) | `d5_cube` / `d5_merge` / `d5_nfaces` / `d5_nverts` | `THREAD｜PROCESS`・**既定 thread** | d4 と同型だが**別の自型**を持つ。同一の mesh を d4 / d5 が各々の自型で受ける＝変換の**多型共存** |

## 例（同梱 pipe_proximity）

| ファイル | 内容 |
|---|---|
| `examples/pipe_clearance.sra` | 自己接近の検出 + 接近点の球マーカ可視化（検出のみ） |
| `examples/pipe_adjust.sra` | 詰まった折り返しを `pipe_adjust` で開く（調整前/後を 3MF に） |
| `examples/pipe_scene.sra` | 固定障害物配管を避けて可動配管を `pipe_scene_adjust` で調整（N 体） |
| `examples/pipe_taper.sra` | 可変太さ(テーパ)管を調整し `pipe_sample` で per-vertex 半径つき tube 化 |
| `examples/pipe_variable.sra` | 太さが弧長に沿って変わる管(指数フレア / キーポイント紡錘形)を `pipe_sample` で生成 |

可視化は 3MF（面色保持）に出力。定数太さのデモは中心線を srava 側でサンプルして `tube` するが、
**可変太さは `pipe_sample`** を使うと弧長↔半径の対応をライブラリの正確な弧長で評価でき、解析と一致する。

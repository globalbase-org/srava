# srava モジュールリファレンス

srava は、本体を再ビルドせずに機能を足せる **モジュール機構**を持つ。モジュールは
**ダイナミックリンクの単位**（`.so`）で、登録した **op 名**を srava プログラムから普通の関数のように
呼べる。実行体（host）は起動時に `.so` を **dlopen** して各モジュールの記述子を読み、どのモジュールが
どの op・どの **型**（mesh の型・4CC と 1:1）を扱うかを登録する。同名 op が複数モジュールにあるとき、
host は入力 mesh の**型でディスパッチ**先を決める（例: `union` の入力が `mf-mesh3d` なら manifold.so、
`cg-mesh3d` なら cgal.so）。pipe_proximity のような**解析モジュール**は値（数・配列・文字列・ハッシュ）
だけをやり取りし、型も**幾何カーネル**（CGAL/Manifold などの幾何コア・モジュールだけが知る）も知らない。

このリファレンスは同梱 3 モジュール（[cgal.so](#cgal) / [manifold.so](#manifold) /
[pipe_proximity](#pipe_proximity)）の**利用者向け仕様**を扱う。記述子 ABI・レジストリ・型ディスパッチ・
モジュール間の型変換といった**内部設計**は[モジュール設計](srava_module_design.html)
（`docs/agent_so_design.md` / `docs/cross_module_conversion_design.md`）を参照。

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

### `.so` の探索（自動ロード）

モジュールは記述子ファイルではなく **`*.so` を自動ロード**して登録する。実行体は起動時に以下の
ディレクトリを順に走査し、見つかった `*.so` をすべて dlopen する（**後にロードしたものが勝つ = 後勝ち**）:

1. `/usr/local/lib/srava/modules`（`cmake --install` の配置先 = install 既定。CMake の
   `SRAVA_MODULE_SYSDIR` で上書き可）
2. `~/.config/srava/modules`（ユーザ個人の上書き）
3. **実行体と同じディレクトリ**（ビルドツリーでは `cgal.so` / `manifold.so` / `pipe_proximity.so` が
   planner と同居）
4. `$SRAVA_MODULE_PATH`（`:` 区切りで複数ディレクトリ可・**最後にロード＝最優先**）

つまり優先度は **install 済み ＜ ユーザ設定 ＜ ビルドツリー ＜ `$SRAVA_MODULE_PATH`** で、
「より具体的な場所が勝つ」。ビルドツリーで `srava` を叩けば、install 済みがあっても
**そのビルドツリーの `.so` が使われる**（2026-08-12 にこの順序へ変更。以前は逆で、
ビルドし直しても古い install が使われ続ける取り違えが起きていた）。

どの `.so` が実際に有効になっているかは **`srava --modules`** で確認できる（後述）。

環境変数は **`SRAVA_MODULE_PATH`**、探索ディレクトリは **`.../modules`**。実装は
`src/classes/pig/c++/pigModuleLoader.cpp`。1 つの `.so` が **複数の op** を serve してよい
（pipe_proximity.so は 1 つの `.so` で **5 op**（後述）を提供する）。

### `module(...)` — モジュールの選択・設定を上書きする（省略可能）

`module` は、host のモジュール選択や実行方式を**プログラム側から上書き**する省略可能な関数である。
**呼ばなくても既定で動く**（自動ロード + 型ディスパッチ + 既定の優先度・実行方式）。明示したいときだけ
プログラム冒頭で呼ぶ。構文は `src/classes/cg/c++/ns_sravaParser.y`・意味は
`src/classes/pig/c++/pigData.cpp`（`pigDataOperatorModule`）で実装される。

```
module("cgal.so");                             // ① ロードのみ（明示ロード。既定探索で見つかるものは不要）
module("manifold.so", { priority: 10 });       // ② 既定幾何カーネルの優先度を上げる（後勝ち・大きいほど優先）
module("cgal.so",     { exec_default: "process" });  // ③ 実行方式を別プロセスに（重い op 向け）
module("manifold.so", { exec_default: "thread" });   // ③ in-proc（planner 内スレッド）に固定
module("cgal.so", "off");   module("cgal.so", "on");  // ④ 実行時にルーティング候補から外す/戻す
```

第 2 引数は**ハッシュ**または**文字列**（省略可）。戻り値は解決したモジュール名（文字列）:

| 第 2 引数 | 効果 |
|---|---|
| （なし） | `.so` をロードするだけ |
| `{priority: N}` | **既定幾何カーネルの優先度**（整数）を上書き。型が定まらない leaf op（例: 引数だけの `box`）で、どのモジュールを既定にするかを決める。**大きいほど優先・後勝ち** |
| `{exec_default: "thread"｜"process"}` | **実行方式**。`"thread"`=planner 内 in-proc / `"process"`=別プロセス `srava_agent`。省略時は記述子の既定（cgal=process 相当・manifold=thread） |
| `"off"` / `"on"` | 実行時に当該モジュールを**ルーティング候補から除外／復帰**（ロードと codec は生かしたまま。無効カーネルが既に作った mesh の読みは可能） |

- 選択そのものは型でディスパッチされるので、`union(mfBody, mfBody)` は `module` 無しでも manifold.so に行く。
  `module(..., {priority})` が効くのは**型が決まらない生成 op の既定**（どの幾何カーネルで `box` を作るか等）。
- `{exec_default}` は機能は変えず**実行方式だけ**を変える（in-proc は速い・process は重い op を分離できる。
  詳細は[モジュール設計](srava_module_design.html)）。

---

## インストール（同梱 pipe_proximity モジュール）

pipe_proximity（可変太さ配管の自己接近検出・別 repo・MIT・CGAL 非依存）は **opt-in** で同梱できる:

```sh
cmake -S . -B build -DSRAVA_MODULE_PIPEPROX=ON   # 既定 ON。OFF で除外。ON で FetchContent 取得(v0.1.6 pin)
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
color, rect, ngon, circle, polygon, line, extrude, tube, revolve, offset,
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

- **4CC(readonly)**: `cast` 時のみ `MESH`（CGAL 3D）/ `PLY2`（CGAL 2D）を **readonly ダウングレード**で読む（`mf-cg-downgrade` codec が有理数→double 化・**損失**）→ `cast("mf-mesh3d", cgMesh)` / `cast("mf-cross2d", cgCross)` とも成立。書き出しは自 4CC のみ。

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

## pipe_proximity.so {#pipe_proximity}

**概要**: 可変太さ配管の**自己接近検出・距離調整**モジュール。**幾何カーネルにもメッシュ型にも依存しない**
解析モジュールの例で、入出力はすべて値（数・配列・ハッシュ）。中心線 + 半径プロファイルを受け、BVH ベースの
近接計算で最小隙間や制御点の調整結果を返す。

**前提とする外部ライブラリ**:

| ライブラリ | バージョン / 取得 | 用途 |
|---|---|---|
| pipeProximity | **v0.1.6**（FetchContent `project.globalbase.org`・pin） | BVH 近接計算・距離調整ソルバ |

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
> 実測（密巻きコイル 27 点・maxIter 25）: 直列 **351s** → L1 約 5〜6 コア / **L2 50.7s（約6.9倍）**。この例では L2 が energy/clearViol とも直列より低い値に到達（2768/0.0016 vs 2820/0.0052）。スレッド数は `threads` で制御（密コイルの L2 彩色は 7 色 size[6,6,6,2,2,2,1] なので `threads` を増やしても色サイズ×6 が上限）。
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
> 乱さない（実測: 一様尾部が最適化で `segMin 0.125`／最大最小比 1342 倍まで崩壊 → `wSpace:0.05` で `segMin 93.7`
> ／比 1.41 倍に是正、`clearViolation` は 0.0012→0.0021 とほぼ不変）。既定 `0`＝従来と完全一致。
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

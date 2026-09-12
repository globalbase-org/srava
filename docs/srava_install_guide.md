---
title: srava インストールガイド (Linux / macOS / Windows)
---

# srava インストールガイド

Linux・macOS・Windows(MSYS2/MINGW64・Cygwin)での **ビルド + インストール** 手順。
モジュールの依存関係と、`export_vox` 用の任意依存 HDF5 も扱う。
幾何カーネルの概念は [言語リファレンス §10](srava_language_reference.html#kernel) を参照。

## 0. モジュールはどうやってインストールする？

**個別の手動インストールは不要。** 幾何モジュール(`cgal.so` / `manifold.so` / `nef_*.so` /
`geogram.so` / `openvdb.so` と橋渡し 3 本 / `occt.so` / `occt_mf.so` / `cherchi.so` /
`pipe_proximity.so`)はすべて srava のソースからビルドされる。**既定でほぼ全部が ON** なので、
軽く済ませたいときだけ §7 の `-DSRAVA_MODULE_*=OFF` で外す。各モジュールが依存する外部ライブラリの
入手経路は 3 種類ある:

1. **ソースを取り込み済み(vendoring)** — srava のリポジトリにソースが入っており、外部から取得しない。対象:
   - pipeProximity(MIT・`modules/pipe_proximity/vendor/pipeProximity/`)。**srava 管理下**で上流追随はしない
     (経緯は同ディレクトリの `VENDORED.md`)。ネットワークも git も要らない。
2. **FetchContent で自動取得** — srava のビルド時に CMake の FetchContent が git clone してその場でビルドし、
   モジュールに**静的に埋め込む**(実行時の共有ライブラリ依存なし=自己完結)。対象:
   - Manifold([elalish/manifold](https://github.com/elalish/manifold), Apache-2.0, v3.5.2 に pin) — その 2D 依存 **clipper2** も Manifold 経由で取得
   - geogram([BrunoLevy/geogram](https://github.com/BrunoLevy/geogram), BSD-3, v1.10.0 に pin)
   - OpenVDB(Apache-2.0) — **TBB が要る**
   - Cherchi(indirect predicates, MIT)
   - どれも **既定 ON**。必要なのは **`git`** だけで、外すときは対応する configure オプション
     (`-DSRAVA_MODULE_MANIFOLD=OFF` 等・§7)を渡す。
3. **システムから検出** — configure 時に `find_package` で既存のインストールを拾う。対象:
   - CGAL 6.x + GMP + MPFR + Boost(`cgal.so` / `nef_snc.so` / `nef_hybrid.so` / `nef_cg.so` 用)
   - Open CASCADE (OCCT)(`occt.so` / `occt_mf.so` 用・**既定 ON なので事前に入れておく**)
   - TBB(`manifold.so` の op 内並列と、OpenVDB / geogram が要求する)
   - HDF5(`export_vox` 用・任意)
   - jemalloc(Linux の既定アロケータ・任意。無ければ system malloc に落ちる → §7 `SRAVA_ALLOCATOR`)

どれを選ぶかの判断基準は[モジュール設計ガイド §7.3](srava_module_design.html)にある(新しい幾何カーネルを
足すときはそちらを先に読む)。

各モジュールの詳細(依存・扱う型・提供 op)は
[モジュールリファレンス](srava_module_reference.html)を参照。

> **git が無い環境**(例: MSYS2 MINGW64 で git 未導入)は clone できないので、別マシンで取得済みの
> `manifold-src` / `clipper2-src` を持ち込み、`-DFETCHCONTENT_SOURCE_DIR_MANIFOLD=<path>`
> `-DFETCHCONTENT_SOURCE_DIR_CLIPPER2=<path>` で指す(下記 Windows 節)。

## 1. 依存関係の全体像

| 依存 | 何に要る | 必須? |
|------|---------|-------|
| C++17 コンパイラ(gcc/clang) | 全体 | 必須 |
| CMake ≥ 3.16(推奨 3.24+) | ビルド | 必須 |
| git | FetchContent 取得モジュール(Manifold)の clone | 当該モジュール有効時に必須 |
| **tinyState**(globalbase-org/tinyState, BSD) | 状態機械ランタイム基盤 | **必須**(find_package) |
| CGAL 6.x + GMP + MPFR + Boost | [`cgal.so`](srava_module_reference.html#cgal)(厳密幾何カーネルのモジュール) | 厳密カーネルに必須(`-DSRAVA_MODULE_CGAL=OFF` で非依存化) |
| Manifold + clipper2 | [`manifold.so`](srava_module_reference.html#manifold)(高速幾何カーネルのモジュール) | FetchContent で自動(git のみ) |
| **TBB**(oneTBB) | `manifold.so` の op 内並列(`SRAVA_MANIFOLD_PAR`・**既定 ON**)。`geogram` / `openvdb` も要求する | **既定で必須**。要らなければ `-DSRAVA_MANIFOLD_PAR=OFF`(Manifold はシリアルで建つ)。Cygwin では自動 OFF |
| geogram | [`geogram.so`](srava_module_reference.html#geogram)(厳密 mesh arrangement のモジュール) | **既定 ON**。FetchContent で自動(git のみ)。⚠ 取得 + ビルドに時間がかかるので、軽く済ませたいときは `-DSRAVA_MODULE_GEOGRAM=OFF` |
| OpenVDB | [`openvdb.so`](srava_module_reference.html#openvdb) と橋渡し 3 本(`openvdb_cg` / `openvdb_gg` / `openvdb_mf`)。voxel 化・等値面・オフセット | **既定 ON**。FetchContent で自動(git のみ)・**TBB が要る**。⚠ ビルドが重い。`-DSRAVA_MODULE_OPENVDB=OFF` で除外 |
| Open CASCADE (OCCT) | [`occt.so`](srava_module_reference.html#occt) / `occt_mf.so`(B-rep・NURBS/解析曲面) | **既定 ON**。**system から `find_package` で検出**するので、事前に入れておく必要がある。Cygwin では自動 OFF |
| Cherchi (indirect predicates) | `cherchi.so`(厳密ブール) | **既定 ON**。FetchContent で自動(git のみ)。Cygwin では自動 OFF(abseil が拒否) |
| pipeProximity | [`pipe_proximity.so`](srava_module_reference.html#pipe_proximity) | **不要**(ソース取り込み済み) |
| HDF5 | `export_vox`(voxel 化 → k-Wave) | 任意(無ければ export_vox のみ無効) |
| jemalloc | Linux の既定アロケータ(実行体 2 本にだけリンク) | 任意(無ければ system malloc へ落ちて警告・`-DSRAVA_ALLOCATOR=system` で明示的に切れる) |

生成される実行体(`cmake --install` で `bin/` に):

- `srava` — プランナ(言語/実行エンジン・幾何カーネル非依存)
- `srava_agent` — 幾何カーネル非依存の agent host

## 2. tinyState(共通の前提)

srava は `find_package(tinyState)` で tinyState を探す。先に tinyState をビルド・インストールする。
**要 tinyState v2.0.0-rc16 以上**。理由は 2 つ:

- rc13 で並列ワーカの親付けが修正された(旧版だと cold cache 実行で `pig_value_parse: malformed value`)。
- rc16 で `stdLimitSemaphore::insNeq()` が入った。**ワーカーゲートの既定の入場順**
  (`GATE_ORDER=delta` → [言語リファレンス §9.0.1](srava_language_reference.html#環境変数-実行テスト用))が
  「同点を LIFO に倒す口」を必要とし、それがこの API になる。

⚠ 後者が無い tinyState では **configure が `FATAL_ERROR` で止まる**。黙って古い入場順へ落とさないのは、
既定の挙動が toolchain の版で静かに割れると、あとから「どちらで動いていたのか」が分からなくなるため。
メッセージは次の形で出る:

```
tinyState at '<inc>' has no stdLimitSemaphore::insNeq().
srava needs it for the worker gate admission order. Install tinyState v2.0.0-rc16 or later, ...
```

入っている版は install 済みのヘッダで確認できる:

```sh
grep TS_REVISION /usr/local/include/std2/tinyState_config.h
#   → #define TS_REVISION       "v2.0.0-rc16-0-g070e615"      ← 公開 rc16 を素で建てた場合
```

⚠ `TS_REVISION` は `git describe` の出力を焼いたものなので、**建て方で文字列が揺れる**
(タグから進んだコミットの `-<n>-g<hash>`、作業ツリーが汚れていれば `-dirty`、
同じコミットに別名のタグが同居していればその名前)。版の見当をつけるには使えるが、
**完全一致で判定しない**こと。判定が要るなら、srava が実際に見ているものを直接見る
— これが空なら configure は止まる:

```sh
grep -l insNeq /usr/local/include/ts2/c++/stdLimitSemaphore.h
```

⚠ `find_package(tinyState)` が立てる `PACKAGE_VERSION` は `2.0.0` 固定で **rc 番号を持たない**ので、
版の判別にはこちらを見る。C++ からは `#include "std2/tinyState_config.h"` で `TS_REVISION` /
`TS_VERSION` を参照できる。

```sh
git clone https://github.com/globalbase-org/tinyState.git
cd tinyState
cmake -B build .
cmake --build build -j
sudo cmake --install build          # /usr/local に配置(config も入る)
```

インストール後、srava の configure は `/usr/local` から自動で拾う(prefix を変えた場合は
`-DCMAKE_PREFIX_PATH=<prefix>`)。

## 3. Linux(Debian / Ubuntu)

```sh
# 依存(Debian/Ubuntu)
sudo apt install build-essential cmake ninja-build git \
     libcgal-dev libgmp-dev libmpfr-dev libboost-dev libhdf5-dev libtbb-dev \
     libocct-foundation-dev libocct-modeling-data-dev libocct-modeling-algorithms-dev \
     libocct-data-exchange-dev
# (Arch: sudo pacman -S base-devel cmake ninja git cgal gmp mpfr boost hdf5 onetbb opencascade)

# tinyState は §2 で導入済みとする

git clone https://github.com/globalbase-org/srava.git
cd srava
cmake -B build -G Ninja -DSRAVA_MODULE_MANIFOLD=ON .   # Manifold は git で自動取得
cmake --build build -j
sudo cmake --install build                              # /usr/local/bin へ
```

- 検証実績: Debian 13(trixie)/ g++ 14.2 / CMake 3.31・**既定構成**(§7 の既定のまま = `nef_snc` だけ OFF)で
  **`srava_occt_contact` 1 本を除いて green**(2026-09-12)。その 1 本は **OCCT 7.8.1 の上流欠陥**で、
  srava 側の回帰ではない(下記)。
  ⚠ **ctest の本数は構成で変わる**ので、他所の数字と突き合わせるときは option を揃えること
  (例: `-DSRAVA_MODULE_NEF_SNC=ON` は `nef_snc` と橋渡し 2 本のぶんテストが増える)。

> ### ⚠ Debian 13 の OCCT 7.8.1 では `srava_occt_contact` が落ちる
>
> **境界だけで接する立体の融合**(2 球の対称差 = 三日月 2 つが交線の円だけで接する形)で、OCCT が
> その接触面を内部面とみなして消してしまい、レンズが材料に化けて **`xor` が `union` の値を返す**。
> **OCCT 7.9.3 では 7 ケースすべて正しく、このテストも通る**(macOS で確認済み)。
> ⇒ 落ちるかどうかは **OCCT の版だけ**で決まる。
>
> ⚠ **面積では検出できない** — `xor` の境界は元の 2 球の境界そのものなので、union に化けても
> 面積は正しく見える。体積で見る必要がある。
>
> このテストは**上流の欠陥を静かな誤値ではなく赤い失敗にするため**に置いてある。落ちたときは
> まず OCCT のバージョンを疑うこと。`occt` を使わないなら `-DSRAVA_MODULE_OCCT=OFF` で外せる。

## 4. macOS(Apple Silicon / Intel)

```sh
# コンパイラ + git(Xcode Command Line Tools)
xcode-select --install

# CMake と CGAL(cgal が boost/gmp/mpfr を連れてくる)・TBB・OpenCASCADE
brew install cmake cgal tbb opencascade
# 任意: export_vox を使うなら
brew install hdf5

# tinyState は §2 で導入済みとする(/usr/local か /opt/homebrew)

git clone https://github.com/globalbase-org/srava.git
cd srava
cmake -B build -DSRAVA_MODULE_MANIFOLD=ON .   # Manifold は git で自動取得
cmake --build build -j
sudo cmake --install build
```

- **CGAL が無い状態**では `cgal.so` モジュールがビルドできない(`find_package(CGAL)` 失敗)。
  `brew install cgal` で解決するか、`-DSRAVA_MODULE_CGAL=OFF` で当モジュールを除外する。
- HDF5 を入れない場合、configure は「HDF5 not found → export_vox 無効」と表示して**本体は通常ビルド**
  (`export_vox` だけ使えない)。
- ⚠ **`geogram` を有効にするなら `OpenMP_ROOT` の指定が要る**。Homebrew の `libomp` は keg-only で、
  無指定だと `Could NOT find OpenMP` になり geogram が OpenMP 無しで建つ:

  ```sh
  cmake -B build -DOpenMP_ROOT=/opt/homebrew/opt/libomp -DSRAVA_MODULE_GEOGRAM=ON .
  ```

  `CMAKE_PREFIX_PATH=/opt/homebrew` を渡しても効かない(keg-only は prefix の外に居るため)。
- 検証実績: macOS 14.8(arm64・OCCT 7.9.3)/ Apple clang / `-DSRAVA_MODULE_NEF_SNC=ON` を足した構成で
  **full ctest が 100% green**(2026-09-12)。★ この構成では `srava_occt_contact` も**通る** —
  Linux(§3)で落ちるのは OCCT 7.8.1 の欠陥で、7.9.3 では出ないため。
  ⚠ **このリリースの最終ツリーそのものではなく、その 3 コミット手前で取った値**(差分は docs と
  テストの数え方の修正のみ)。

## 5. Windows — MSYS2 / MINGW64

MSYS2 の **MINGW64** シェルでビルドする(Cygwin とは別物・下記 §6)。

```sh
# MINGW64 シェルで(pacman)
pacman -S --needed git \
  mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-cgal mingw-w64-x86_64-gmp mingw-w64-x86_64-mpfr \
  mingw-w64-x86_64-boost mingw-w64-x86_64-hdf5 mingw-w64-x86_64-tbb \
  mingw-w64-x86_64-opencascade

# tinyState は §2 の手順で /usr/local(=C:\msys64\usr\local)へ

cd /c/Users/<you>/srava
cmake -B build -G Ninja -DSRAVA_MODULE_MANIFOLD=ON .
cmake --build build -j
cmake --install build --prefix /usr/local
```

- ⚠ **`git` を必ず入れる**(`pacman -S git`)。Manifold/モジュールの FetchContent が git clone するため。
  git を入れられない場合は、別マシンで取得済みのソースを持ち込んで指す:

  ```sh
  cmake -B build -G Ninja -DSRAVA_MODULE_MANIFOLD=ON \
    -DFETCHCONTENT_SOURCE_DIR_MANIFOLD=/path/to/manifold-src \
    -DFETCHCONTENT_SOURCE_DIR_CLIPPER2=/path/to/clipper2-src .
  ```

- **PowerShell から直接ではなく MINGW64 シェル**でビルドする(`C:\msys64\usr\bin\bash.exe -lc` +
  `export MSYSTEM=MINGW64; source /etc/profile`)。多段クォートが要る操作はスクリプトファイルにして実行。
- Windows ではモジュールの拡張子は **`.dll`**(`cgal.dll` / `manifold.dll` / …)。ソース中の
  `module("cgal.so")` は実行 OS の拡張子へ自動で正規化されるので、スクリプトはそのまま可搬。
- 検証実績: Windows 11 + MSYS2/MINGW64(g++ 16.1)/ Ninja / CMake 4.4 で **full ctest が 100% green**
  (2026-08-28。共有 libpig.dll + 全モジュール .dll 構成)。
  ⚠ Windows では `ctest -j2` で回すこと。
  ⚠⚠ **これは前リリース(v1.1.0-rc5)時点の記録**で、このリリースでは取り直していない。
  本リリースで入った**走行中の中断**(Ctrl+C / `grace` / `panic`)は、シグナル・IOCP・`select` という
  **プラットフォームごとに実装が分かれる**層に触るので、Windows で使うなら手元で ctest を回して
  確認してほしい。
- ★ **Windows の主経路は MinGW**。Cygwin(§6)は使えるモジュールが限られる。

## 6. Windows — Cygwin

Cygwin は **CGAL をパッケージしておらず Boost も古い**ため、CGAL 6.x と新しめ Boost の手当てが要る。
詳細な確立手順は別ページ **[Cygwin ビルド手順](cygwin_build.html)** を参照(header-only CGAL の配置・
Boost 差し替え・`-DSRAVA_ENABLE_HDF5` の扱い)。要点:

- `setup-x86_64.exe -q -P libgmp-devel,libmpfr-devel,libboost-devel,ninja,git`
- CGAL 6.x は header-only リリースを展開して `-DCGAL_DIR=` で指す。
- HDF5 は Cygwin の cmake config が壊れているため**既定で無効**。使うなら config を直して
  `-DSRAVA_ENABLE_HDF5=ON`。
- ⚠ **使えるモジュールが限られる**: `cgal` / `nef_hybrid` / `manifold` / `pipe_proximity` のみ
  (`nef_snc` は既定 OFF・`-DSRAVA_MODULE_NEF_SNC=ON` で追加できる)。
  **他は既定 ON だが Cygwin では自動 OFF に倒れる**: `geogram` と `openvdb` は TBB、`occt` は OpenCASCADE を要するが
  Cygwin はどちらもパッケージしておらず、`cherchi` は依存の abseil が Cygwin を明示的に拒否する。
  `CMakeLists.txt` が `if(CYGWIN)` で自動的に OFF にする。**`manifold.so` の op 内並列
  (`SRAVA_MANIFOLD_PAR`・他プラットフォームでは既定 ON)も同じ理由で自動 OFF になる**ので、
  Cygwin に TBB を用意する必要はない(Manifold 自体はシリアルで建って動く)。
- ⚠ **ビルド後に `rebase -b 0x600000000 -v *.dll` が要る**。Cygwin の `fork()` は子で DLL を親と
  同じアドレスに載せ直すため、多数の `.dll` を dlopen する srava では再配置に失敗する。
  失敗時のエラーはワーカーゲートの上限を指すが**それは真因ではない**。詳細は別ページ。
- 検証実績: Cygwin 3.6(g++ 11.5 / CMake 4.2)で **full ctest が 100% green**(2026-08-28。
  上記 5 モジュール構成・`rebase` 実施後・`-DCMAKE_BUILD_TYPE=Release`)。
  ⚠⚠ **これも前リリース(v1.1.0-rc5)時点の記録**で、このリリースでは取り直していない(§5 と同じ理由)。
- ⚠ **`-DCMAKE_BUILD_TYPE=Release` を必ず指定する**。無指定だと最適化が一切かからず、
  数値最適化を回す op(`pipe_adjust` 等)が桁違いに遅くなり、テストが TIMEOUT する。
- Manifold を使うなら Cygwin の `git` が要る(`-DSRAVA_MODULE_MANIFOLD=ON`)。
- ⚠ **Windows では MinGW(§5)を推奨する**。Cygwin は上記のとおり使えるモジュールが限られ、
  `rebase` という追加手順も要る。

## 7. configure オプション早見表

| オプション | 既定 | 効果 |
|-----------|------|------|
| `-DSRAVA_MODULE_CGAL=ON` | **ON** | CGAL 幾何モジュール(`cgal.so`)をビルド。**OFF で CGAL/GMP/MPFR 非依存ビルド**(`cgal.so` と CGAL 専用テストを除外・既定カーネルは次点へ) |
| `-DSRAVA_MODULE_MANIFOLD=ON` | **ON** | Manifold 幾何モジュール(`manifold.so`)をビルド(要 git・FetchContent 自動取得)。OFF で除外 |
| `-DSRAVA_MODULE_NEF=ON` | **ON** | CGAL Nef モジュール群の傘(`SRAVA_MODULE_CGAL=ON` が前提)。OFF で 2 変種とも除外 |
| `-DSRAVA_MODULE_NEF_HYBRID=ON` | **ON** | `nef_hybrid.so`(有界立体は厳密境界・型 `nfb-mesh3d`) |
| `-DSRAVA_MODULE_NEF_SNC=ON` | **OFF** | `nef_snc.so`(常に SNC 表現・型 `nf-mesh3d`)。**既定 OFF**。橋渡しの `nef_cg.so`(→ CGAL)/ `nef_mf.so`(→ Manifold・要 `SRAVA_MODULE_MANIFOLD`)も**これと連動して**建つ |
| `-DSRAVA_MODULE_GEOGRAM=ON` | **ON** | geogram モジュール(`geogram.so`)をビルド(要 git・FetchContent 自動取得)。⚠ 取得 + ビルドに時間がかかるので、軽く済ませたいときは OFF |
| `-DSRAVA_MODULE_PIPEPROX=ON` | **ON** | pipe_proximity モジュール(`pipe_proximity.so`)をビルド(取り込み済みソース・外部取得なし)。OFF で除外 |
| `-DSRAVA_MODULE_OPENVDB=ON` | **ON** | OpenVDB ボリュームモジュール(`openvdb.so` と橋渡し 3 本)をビルド(FetchContent・要 TBB)。⚠ ビルドが重い。**Cygwin では自動 OFF** |
| `-DSRAVA_MODULE_OPENVDB_CG_GPL=ON` | **ON** | 橋渡しの `openvdb_cg.so` を建てる。**CGAL をリンクするのでこのモジュールは GPL**(`openvdb.so` / `openvdb_mf.so` は Apache-2.0 のみ) |
| `-DSRAVA_MODULE_OCCT=ON` | **ON** | Open CASCADE B-rep モジュール(`occt.so` / `occt_mf.so`)をビルド(**system の OpenCASCADE が要る**)。`occt_mf` は `SRAVA_MODULE_MANIFOLD=ON` のときだけ建つ。**Cygwin では自動 OFF** |
| `-DSRAVA_MODULE_CHERCHI=ON` | **ON** | Cherchi(indirect predicates)ブールモジュールをビルド(FetchContent)。**Cygwin では自動 OFF**(依存の abseil が拒否) |
| `-DSRAVA_OCCT_TEXT=ON` | **ON** | `occt` の `text` op(TrueType 取り込み)。**OCCT の Visualization(TKV3d → TKService)が要る**ので、ヘッドレスの幾何モジュールに freetype / fontconfig / X11 が乗る。最小 OCCT ならこれを `OFF` にする |
| `-DSRAVA_OCCT_STEP=ON` | **ON** | `occt` の STEP 入出力(`.step` / `.stp`)。**OCCT の DataExchange(TKDESTEP / TKXSBase)が要る** |
| `-DSRAVA_ALLOCATOR=auto` | **auto** | 実行体 2 本(`srava` / `srava_agent`)が使う malloc 実装。`auto` は **Linux で jemalloc**・他 OS で system malloc。`jemalloc` / `system` で明示指定できる。⚠ モジュールには付けない(プロセスに 1 つ) |
| `-DSRAVA_MANIFOLD_PAR=ON` | **ON** | Manifold を op 内並列(TBB)で建てる。**system の TBB が要る**。`OFF` でシリアル。**Cygwin では自動 OFF**。切替には Manifold の全再コンパイルが要る |
| `-DSRAVA_SLOW_TESTS=ON` | **OFF** | 時間のかかる回帰テスト(`std/roll.sra` 等)も ctest に登録する |
| `-DSRAVA_ENABLE_HDF5=ON` | Cygwin で OFF | `export_vox`(HDF5)を有効化。**Cygwin 専用の分岐**でのみ参照(非 Cygwin では HDF5 は無条件に auto 検出=このオプションは無視) |
| `-DFETCHCONTENT_SOURCE_DIR_MANIFOLD=<dir>` | — | git を使わず取得済み Manifold ソースを指す |
| `-DFETCHCONTENT_SOURCE_DIR_CLIPPER2=<dir>` | — | 同 clipper2 |
| `-DCMAKE_PREFIX_PATH=<prefix>` | — | tinyState 等を非標準 prefix から探す |

- ⚠ `SRAVA_OCCT_TEXT` / `SRAVA_OCCT_STEP` は **無ければ自動 OFF、ではなく configure を止める**。
  自動で落とすと OCCT を差し替えた日に op が黙って歯抜けになり、その op を含む式全体の行き先カーネルが
  裏返る(テストは落ちずに意味だけ変わる)。落とすときは人が明示的に `OFF` を書く。

- 旧名 `-DSRAVA_KERNEL_CGAL` / `-DSRAVA_KERNEL_MANIFOLD` / `-DSRAVA_PLUGIN_PIPEPROX` は `set()` で
  現行の `SRAVA_MODULE_*` へ転送され受理されるが **deprecated**(kernel/plugin → module リネーム)。新規は現行名を使う。

## 8. 動作確認

```sh
# 既定(CGAL・厳密)
echo 'export("box.stl", box(20,20,20));' | SRAVA_SOURCE=/dev/stdin srava
# または
srava model.sra

# テスト一式
ctest --test-dir build -j
```

- **モジュールの優先度切替はソース内で行う**。特定のモジュールを優先させるには:

  ```
  module("manifold.so", {priority:1});
  // ★ **使うモジュールは module() で名指す** (または include "module/all.sra"; /
  //   SRAVA_MODULE_ALL=1 で一括ロード)
  export("box.stl", box(20,20,20));
  ```

  のように `module(...)` で当該モジュールの優先度を上げる。複数の幾何モジュールが読まれているとき、
  planner は「優先モジュールで扱える op はそれで処理し、未対応 op や別の型が要る所は他モジュールへ自動で回す」
  ハイブリッド動作になる(→ [言語リファレンス §10](srava_language_reference.html#kernel))。
- インストール後は `srava` が `srava_agent` を自動で使う。解決順は
  **① env `SRAVA_AGENT` → ② 実行体と同じ dir の `srava_agent` → ③ configure 時の `$PREFIX/bin/srava_agent`**。
  ② があるので、install 先を後から別の場所へ移してもビルドツリーで直に走らせても、**自分と同じ版の
  agent** が起動する(版が食い違うと沈黙ハングや的外れなエラーになるため、突き合わせで即エラーになる)。
- 幾何モジュール(`cgal.so` / `manifold.so` / `nef_snc.so` / `pipe_proximity.so` …)は host が次の
  **探索路**を順に走査して dlopen する(**後勝ち** = 下に行くほど優先。「より具体的な場所が勝つ」):
  1. `/usr/local/lib/srava/modules`(configure 時の prefix を焼き込んだ install 既定・最弱),
  2. **実行体から見た `../lib/srava/modules`**(= 自分と同じ install ツリーのモジュール),
  3. `~/.config/srava/modules`(ユーザ個人の上書き),
  4. 実行体と同居する dir(**ビルドツリーではここで解決される**),
  5. `$SRAVA_MODULE_PATH`(`:` 区切り・Windows は `;`・最優先)。

  ② があるので、**install した prefix ごと別の場所へ移しても動く**(1 は configure 時の絶対パスなので、
  それしか無かった頃は移設した install が「その機械の `/usr/local` にある別世代のモジュール」を
  読みに行っていた)。`module("nef_snc.so")` のように **ファイル名だけ**を書いた場合も、この探索路の
  強い順に解決される。

  **同名(同ファイル名)のモジュールは勝者 1 つだけが dlopen される**(負けた候補は読まれない)。
  install 済みとビルドツリーが混ざって新旧が取り違わるのを防ぐため。
- **`srava --module-info [名前]`** で、そのモジュールが申告している op の `sig` 全リスト・`provides` が出る（中身の確認用。`--modules` は配置の確認用で別物 → [§9](#cli)）
- **`srava --modules`** で診断ダンプが出る: 走査した探索路(存在しない dir も表示)・ロード済み
  モジュール(priority / 実行方式 / op 数 / パス)・同名で読まれなかった候補(shadowed)・
  ロード失敗(理由付き)。モジュールが効かないときはまずこれを見る。
- `cmake --install` は幾何モジュールも `$PREFIX/lib/srava/modules` へ配置する:
  `cgal.so` / `manifold.so` / `nef_*.so` / `pipe_proximity.so`(有効化したもの)が探索路 ①② に載る。
  開発中は build ツリーの `.so` が実行体と同居する(探索路 ④ = install より強い)ので install なしでも動く。
- **install ツリーは丸ごと移動できる**(`bin/` と `lib/` の相対関係さえ保てばよい)。`libpig.so` は
  `$ORIGIN/../lib` を指す **RPATH**(`DT_RUNPATH` ではなく `DT_RPATH`)で解決するので、
  `LD_LIBRARY_PATH` に別の srava が入っていても自分のものが勝つ。
  この 2 つ(移設可能性と in-proc / 別プロセス両方での動作)は ctest `srava_install_tree` が常時検証している。

## 9. コマンドラインの形式とフラグ {#cli}

```
srava <スクリプト> [引数...]     スクリプトを実行する。第 2 引数以降は srava プログラムの ARGV
srava -  [引数...]                ソースを**標準入力**から読む
srava                             ソースは env SRAVA_SOURCE（未設定なら組み込みの既定ソース）
srava --modules                   モジュールの診断ダンプを出して終了(配置)
srava --module-info [名前...]     モジュールの申告ダンプを出して終了(中身)
srava --count-cache <dir>         キャッシュ dir の内訳を 1 行で出して終了
```

- **シェバング対応**: ソース先頭行が `#!` なら読み飛ばすので、スクリプトを直接実行できる。
- **終了コード**は既定 0。srava プログラムから予約変数で変えられる
  （→ [言語リファレンス](srava_language_reference.html#環境変数-実行テスト用)）。

### `srava --modules` — モジュール診断 {#modules}

走査した探索路（**存在しない dir も表示**）・ロード済みモジュール（priority / 実行方式 / op 数 / パス）・
同名で読まれなかった候補（shadowed）・ロード失敗（理由付き）を出す。**モジュールが効かないときはまず
これを見る**。

> ★ 通常の実行では `.so` は **`module()` で名指しされた時点で**はじめて dlopen される（遅延ロード）。
> `--modules` はこの例外で、診断が目的なので探索路を**全部**読みに行く。読むのは診断専用の
> 使い捨てレジストリで、実行系と同じ経路を通るため**実走時と同じ勝者・同じ失敗が見える**。

### `srava --module-info [名前 ...]` — モジュール申告ダンプ {#module-info}

`--modules` とは**別コマンド**で、問いが違う。

| | 問い | 量 |
|---|---|---|
| `--modules` | **どの `.so` が効いているか**(探索路・勝者・shadowed・失敗) | 数十行 |
| `--module-info` | **そのモジュールが何を申告しているか**(op の `sig` 全リスト・`provides`) | 全モジュールで 500 行超 |

配置の切り分けに使う `--modules` の表が申告に埋もれないよう、分けてある。名前を与えるとそれだけに
絞られる(複数可)。名前を間違えたときは黙って空で終わらず、明示的にそう言う。

```
$ srava --module-info d3
d3  (abi=24 prio=-2 <build>/d3.so)
    exec_caps=thread|process(0x3)  exec_default=process  make_agent=yes
    grace=0(kill at once)  panic=off
    arity=0  cache_version=1  import=-  export=-  initialize=no  configure=no
    cache_salt=|d3|v1
    ops (5):
      d3_cube            nin=1 wire=-
        sig = ->d3-mesh3d
      d3_merge           nin=2 wire=[d3Mesh][d3Mesh]
        sig = (d3-mesh3d,d3-mesh3d)->d3-mesh3d
      ...
    provides (hierarchy / declared type names / tags probed against create):
      d3Mesh             types = d3-mesh3d
                         create=yes reader=yes writer=yes match=yes
                         tag 'D3M3' -> d3-mesh3d

$ srava --module-info nosuch
no such module is loaded (see `srava --modules` for the names)
```

- `wire=` は **cache 引数のスロット列**で、`[本体クラス]` を並べたもの。`-` は幾何 cache 引数なし。
  1 スロットが複数の型を受ける op は候補列になる(`export_vox` の `[cgMesh|vdGrid]`)。
  ⚠ 数えるのは `AK_CACHE` の引数だけ(値引数は位置に入らない)。可変長 op の尾部は
  **最後のスロットが繰り返す**。
- `cache_salt=` は出力キャッシュキーに混ざる弁別バイト列で、**モジュール名 + `cache_version`** から
  レジストリが組む(`|` は表示できない区切り `\x01` の代用)。`.so` を 1 本だけ差し替えたときに
  ここが動いているかで切り分けられる。
- `grace` / `panic` は**撤収の猶予**の申告(#3503)。`grace` は **process 実行**で SIGKILL までの
  猶予(`0(kill at once)` / `Nms` / `graceful-only` = kill しない)、`panic` は **in-proc 実行**で
  居座ったときに planner を abort するまでの猶予(`off` / `Nms`)。同じモジュールでも実行方式で
  使う口が変わるので 2 本ある。
  - ⚠ **ここに出るのは `.so` が申告している値**であって実行時の実効値ではない。
    `module(so,{grace:N})` や `SRAVA_AGENT_GRACE_MS` / `SRAVA_INPROC_PANIC_MS` での上書きは
    この表示に現れない(実効値は env > `module()` > 記述子 の順で解決される)。
  - ⚠ `graceful-only` を名乗れるのは **全 op・全経路が中断要求を見る**モジュールだけ。止まらない
    経路が 1 つでもあると Ctrl+C で永久に終わらない。迷ったら `Nms` にする(申告が間違っていても
    代償は遅延だけで済む)。

- `sig` は長いものがあるが**折り返さない**。揃って見えることより `grep` で拾えることを優先している。
- `provides` の `tag` 行は申告をそのまま出すのではなく、**実際に `create_for_meta` へ通して検証**した
  結果が出る。申告と実装がずれていれば `(NOT accepted - declaration does not match create_for_meta)`
  と表示される。
- `types` と `tags` は**位置対応しない**(独立した 2 本で個数も一致しない)。どのタグがどの型になるかは
  申告ではなく、上記の probe が答える。

```
search path (走査順・後にロードしたものが優先):
  sysdir             /usr/local/lib/srava/modules
  exe-rel install    <prefix>/lib/srava/modules                (無し)
  user config        ~/.config/srava/modules                   (無し)
  exe dir            <build>/

loaded:
  name              prio exec       ops  path
  cgal                20 process     45  <build>/cgal.so
  manifold            10 thread      36  <build>/manifold.so
  pig                  0 -            0  (組込)

shadowed (同名の勝者が在るため読み込まなかったもの):
  /usr/local/lib/srava/modules/cgal.so                 → <build>/cgal.so

(モジュールでない .so を 4 個スキップ)
```

- `prio` / `exec` / `ops` は記述子の申告値そのもの。
  → [モジュールリファレンス](srava_module_reference.html#exec)の並列性の表と突き合わせられる。
- **shadowed は事故検出用**。install 済みとビルドツリーが混ざると、意図しない世代が走る。

### `srava --count-cache <dir>` — キャッシュの内訳

```
complete=<n> incomplete=<n> broken=<n>
```

1 行・機械可読。完成判定は sweep と同じ基準（番兵まで歩く）を使うので、ファイル数を数えるだけの
外部スクリプトのように**書きかけを完成品と数えてしまうことがない**。

### ⚠ 受け付けるフラグはこの 3 つだけ

`--modules` / `--module-info` / `--count-cache` 以外は無く、`--help` / `--version` も**無い**。それ以外の `--…` は**スクリプトのファイル名として扱われる**ので、
`srava --help` は次のように失敗する:

```
srava: cannot open '--help': No such file or directory
```

⚠ 単数形の `--module` も同様（正しくは `--modules`）。

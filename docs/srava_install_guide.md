---
title: srava インストールガイド (Linux / macOS / Windows)
---

# srava インストールガイド

Linux・macOS・Windows(MSYS2/MINGW64・Cygwin)での **ビルド + インストール** 手順。
モジュールの依存関係と、`export_vox` 用の任意依存 HDF5 も扱う。
幾何カーネルの概念は [言語リファレンス §10](srava_language_reference.html#kernel) を参照。

## 0. モジュールはどうやってインストールする？

**個別の手動インストールは不要。** 幾何モジュール(`cgal.so` / `manifold.so` / `pipe_proximity.so`)は
すべて srava のソースからビルドされる。各モジュールが依存する外部ライブラリの入手経路は 2 種類ある:

1. **FetchContent で自動取得** — srava のビルド時に CMake の FetchContent が git clone してその場でビルドし、
   モジュールに**静的に埋め込む**(実行時の共有ライブラリ依存なし=自己完結)。対象:
   - Manifold([elalish/manifold](https://github.com/elalish/manifold), Apache-2.0, v3.5.2 に pin) — その 2D 依存 **clipper2** も Manifold 経由で取得
   - pipeProximity(v0.1.6)
   - 必要なのは **`git`** と、対応する configure オプション(`-DSRAVA_MODULE_MANIFOLD=ON` 等・§7)だけ。
2. **システムから検出** — configure 時に `find_package` で既存のインストールを拾う。対象:
   - CGAL 6.x + GMP + MPFR + Boost(`cgal.so` 用)
   - HDF5(`export_vox` 用・任意)

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
| git | FetchContent 取得モジュール(Manifold / pipeProximity)の clone | 当該モジュール有効時に必須 |
| **tinyState**(globalbase-org/tinyState, BSD) | 状態機械ランタイム基盤 | **必須**(find_package) |
| CGAL 6.x + GMP + MPFR + Boost | [`cgal.so`](srava_module_reference.html#cgal)(厳密幾何カーネルのモジュール) | 厳密カーネルに必須(`-DSRAVA_MODULE_CGAL=OFF` で非依存化) |
| Manifold + clipper2 | [`manifold.so`](srava_module_reference.html#manifold)(高速幾何カーネルのモジュール) | FetchContent で自動(git のみ) |
| pipeProximity | [`pipe_proximity.so`](srava_module_reference.html#pipe_proximity) | FetchContent で自動(git のみ) |
| HDF5 | `export_vox`(voxel 化 → k-Wave) | 任意(無ければ export_vox のみ無効) |

生成される実行体(`cmake --install` で `bin/` に):

- `srava` — プランナ(言語/実行エンジン・幾何カーネル非依存)
- `srava_agent` — 幾何カーネル非依存の agent host

幾何は 3 バイナリ構成ではなくなった(`srava_agent_mf` は廃止)。厳密/高速の幾何は
`cgal.so` / `manifold.so` / `pipe_proximity.so` などの **モジュール(.so)** として供給され、
`srava` / `srava_agent` が**ロード時に dlopen** する(探索路は §8 参照)。

## 2. tinyState(共通の前提)

srava は `find_package(tinyState)` で tinyState を探す。先に tinyState をビルド・インストールする。
**要 tinyState v2.0.0-rc12 以上**。rc11 以前は不可: Windows で必須の修正(スレッドローカルの
イメージ跨ぎ単一化)が rc12 で入ったほか、rc12 で内部レイアウト(ABI)が変わっている。

> ⚠ **tinyState を rc11 以前から入れ替えた場合、srava は再リンクではなく**クリーン再ビルド**が必要**
> (`rm -rf build` からやり直す)。レイアウト変更を跨ぐ incremental ビルドは、エラーにならずに
> 壊れたバイナリを作ることがある。

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
     libcgal-dev libgmp-dev libmpfr-dev libboost-dev libhdf5-dev
# (Arch: sudo pacman -S base-devel cmake ninja git cgal gmp mpfr boost hdf5)

# tinyState は §2 で導入済みとする

git clone https://github.com/globalbase-org/srava.git
cd srava
cmake -B build -G Ninja -DSRAVA_MODULE_MANIFOLD=ON .   # Manifold は git で自動取得
cmake --build build -j
sudo cmake --install build                              # /usr/local/bin へ
```

## 4. macOS(Apple Silicon / Intel)

```sh
# コンパイラ + git(Xcode Command Line Tools)
xcode-select --install

# CMake と CGAL(cgal が boost/gmp/mpfr を連れてくる)
brew install cmake cgal
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
- 検証実績: macOS 14(arm64)/ Apple clang 16 / CMake 4.4 で **ctest 約 221 本(全オプション ON 時)green**、
  各幾何モジュール動作確認済み。

## 5. Windows — MSYS2 / MINGW64

MSYS2 の **MINGW64** シェルでビルドする(Cygwin とは別物・下記 §6)。

```sh
# MINGW64 シェルで(pacman)
pacman -S --needed git \
  mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-cgal mingw-w64-x86_64-gmp mingw-w64-x86_64-mpfr \
  mingw-w64-x86_64-boost mingw-w64-x86_64-hdf5

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
- 検証実績: Windows 11 + MSYS2/MINGW64(gcc 15)/ Ninja で **ビルド全通・ctest 230/230 green**
  (2026-08-13。共有 libpig.dll + 全モジュール .dll 構成)。

## 6. Windows — Cygwin

Cygwin は **CGAL をパッケージしておらず Boost も古い**ため、CGAL 6.x と新しめ Boost の手当てが要る。
詳細な確立手順は別ページ **[Cygwin ビルド手順](cygwin_build.html)** を参照(header-only CGAL の配置・
Boost 差し替え・`-DSRAVA_ENABLE_HDF5` の扱い)。要点:

- `setup-x86_64.exe -q -P libgmp-devel,libmpfr-devel,libboost-devel,ninja,git`
- CGAL 6.x は header-only リリースを展開して `-DCGAL_DIR=` で指す。
- HDF5 は Cygwin の cmake config が壊れているため**既定で無効**。使うなら config を直して
  `-DSRAVA_ENABLE_HDF5=ON`。
- Manifold を使うなら Cygwin の `git` が要る(`-DSRAVA_MODULE_MANIFOLD=ON`)。
- ⚠ **現世代(モジュール .dll 化・共有 libpig 化後)の Cygwin ビルドは未検証**。上記は前世代で
  確立した環境手当てで、依存の入手方法としては現在も有効。MinGW(§5)は検証済みなので、
  Windows では MinGW を推奨する。

## 7. configure オプション早見表

| オプション | 既定 | 効果 |
|-----------|------|------|
| `-DSRAVA_MODULE_CGAL=ON` | **ON** | CGAL 幾何モジュール(`cgal.so`)をビルド。**OFF で CGAL/GMP/MPFR 非依存ビルド**(`cgal.so` と CGAL 専用テストを除外・既定カーネルは次点へ) |
| `-DSRAVA_MODULE_MANIFOLD=ON` | **ON** | Manifold 幾何モジュール(`manifold.so`)をビルド(要 git・FetchContent 自動取得)。OFF で除外 |
| `-DSRAVA_MODULE_PIPEPROX=ON` | **ON** | pipe_proximity モジュール(`pipe_proximity.so`)をビルド(要 git・FetchContent 自動取得)。OFF で除外 |
| `-DSRAVA_ENABLE_HDF5=ON` | Cygwin で OFF | `export_vox`(HDF5)を有効化。**Cygwin 専用の分岐**でのみ参照(非 Cygwin では HDF5 は無条件に auto 検出=このオプションは無視) |
| `-DFETCHCONTENT_SOURCE_DIR_MANIFOLD=<dir>` | — | git を使わず取得済み Manifold ソースを指す |
| `-DFETCHCONTENT_SOURCE_DIR_CLIPPER2=<dir>` | — | 同 clipper2 |
| `-DFETCHCONTENT_SOURCE_DIR_PIPEPROX=<dir>` | — | 同 pipe_proximity |
| `-DCMAKE_PREFIX_PATH=<prefix>` | — | tinyState 等を非標準 prefix から探す |

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

- **モジュールの優先度切替は env ではなくソース内で行う**(`SRAVA_DEFAULT_OUTPUT` 等の env による
  既定切替は撤去済み)。特定のモジュールを優先させるにはソース側で:

  ```
  module("manifold.so", {priority:1});
  export("box.stl", box(20,20,20));
  ```

  のように `module(...)` で当該モジュールの優先度を上げる。複数の幾何モジュールが読まれているとき、
  planner は「優先モジュールで扱える op はそれで処理し、未対応 op や別の型が要る所は他モジュールへ自動で回す」
  ハイブリッド動作になる(→ [言語リファレンス §10](srava_language_reference.html#kernel))。
- インストール後は `srava` が `/usr/local/bin/srava_agent` を自動で使う(env `SRAVA_AGENT` で差し替え可)。
- 幾何モジュール(`cgal.so` / `manifold.so` / `pipe_proximity.so`)は host が次の**探索路**を順に走査して
  dlopen する(**後勝ち** = 下に行くほど優先。「より具体的な場所が勝つ」):
  1. `/usr/local/lib/srava/modules`(install 既定・最弱),
  2. `~/.config/srava/modules`(ユーザ個人の上書き),
  3. 実行体と同居する dir(**ビルドツリーではここで解決される**),
  4. `$SRAVA_MODULE_PATH`(`:` 区切り・Windows は `;`・最優先)。

  **同名(同ファイル名)のモジュールは勝者 1 つだけが dlopen される**(負けた候補は読まれない)。
  install 済みとビルドツリーが混ざって新旧が取り違わるのを防ぐため。
- **`srava --modules`** で診断ダンプが出る: 走査した探索路(存在しない dir も表示)・ロード済み
  モジュール(priority / 実行方式 / op 数 / パス)・同名で読まれなかった候補(shadowed)・
  ロード失敗(理由付き)。モジュールが効かないときはまずこれを見る。
- `cmake --install` は幾何モジュールも SYSDIR(`/usr/local/lib/srava/modules`)へ配置する:
  `cgal.so` / `manifold.so` / `pipe_proximity.so`(有効化したもの)が探索路 ① に載る。開発中は build ツリー
  の `.so` が実行体と同居する(探索路 ③ = install より強い)ので install なしでも動く。

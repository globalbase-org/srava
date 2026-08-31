# srava を Cygwin でビルドする

Cygwin は POSIX 層なので、MinGW 固有の事情(ts2System の起動方式・IOCP・O_BINARY・
`/tmp` パス・named pipe)に縛られず、実質 Linux 相当でビルド/実行できる。ただし Cygwin は
**CGAL をパッケージしておらず**、**Boost が古い(1.66)**ため、CGAL 6.x と新しめ Boost を手当てする
必要がある。以下はその手順。

## 1. Cygwin パッケージ(setup-x86_64.exe)

```
setup-x86_64.exe -q -P libgmp-devel,libmpfr-devel,libhdf5-devel,libboost-devel,ninja
```
- gmp/mpfr は CGAL(EPECK)の厳密数、ninja はビルド、hdf5 は voxelize 用(下記参照)。
- ⚠ **CGAL パッケージは Cygwin に存在しない** → 手順 2 で用意。
- ⚠ Cygwin の Boost は **1.66**(古い)。CGAL 6 は Boost >= 1.72 を要求するので手順 3 で差し替える。

## 2. CGAL 6.x(header-only)を配置

srava は `CGAL/Polygon_repair/repair.h` 等 **CGAL 6.0 の新規ヘッダ**を使うため CGAL 6.x が必須
(5.6 では不足・かつ Boost 1.66 と boost_mp 非互換)。header-only リリースを展開するだけでよい:

```
cd ~
curl -sSL -O https://github.com/CGAL/cgal/releases/download/v6.0.1/CGAL-6.0.1-library.tar.xz
tar xf CGAL-6.0.1-library.tar.xz
# → CGAL_DIR = ~/CGAL-6.0.1/lib/cmake/CGAL
```

## 3. Boost >= 1.72 を用意(boost/ だけを露出)

Cygwin の Boost 1.66 は古すぎる(`cpp_int_backend has no member 'limbs'` で CGAL がコンパイル不能)。
同機の MinGW(MSYS2)側に新しい Boost があればその **boost/ ディレクトリだけ**を symlink で露出する
(MinGW の include ディレクトリ全体をパスに載せると Cygwin のシステムヘッダを shadow して
`#error Only Win32 target is supported!` になるので、必ず boost/ のみにする):

```
mkdir -p ~/boostinc
ln -sfn /cygdrive/c/msys64/mingw64/include/boost ~/boostinc/boost
# → BOOST_INCLUDE = ~/boostinc  (Boost 1.9x)
```
MinGW 側 Boost が無い/古い場合は、Boost 1.8x のソースを展開して `~/boostinc/boost` を指す。

## 4. Cygwin で使えるモジュール

Cygwin は **TBB と OpenCASCADE をパッケージしていない**(`cygcheck -p libtbb` / `-p opencascade` とも
0 件)。これが有効化できるモジュールの線引きになる。

| モジュール | Cygwin | 理由 |
|---|---|---|
| `cgal` / `nef_snc` / `nef_hybrid` / `manifold` / `pipe_proximity` | **可** | 既定 ON のまま使える |
| `geogram` / `openvdb`(+ 橋渡し 3 本) | **不可** | TBB が要る(下記) |
| `occt` / `occt_mf` | **不可** | OpenCASCADE が無い |
| `cherchi` | **不可** | 依存の abseil が Cygwin を明示的に拒否する |
| `manifold` の **op 内並列** (`SRAVA_MANIFOLD_PAR`) | **不可** | TBB が要る(他プラットフォームでは既定 ON・Cygwin では自動 OFF でシリアルに建つ) |

`CMakeLists.txt` がこれらを **`if(CYGWIN)` で自動的に OFF にする**(理由つきの `message` を出す)ので、
`-DSRAVA_MODULE_...=ON` を渡しても黙って壊れることはない。

> ★ **なぜ TBB を自前で入れても駄目なのか**(2026-08-28 に実機で確認)
>
> * **共有で建てると**: TBB のヘッダは `_WIN32` の有無で `__declspec(dllimport)` を出すか決めるが、
>   **Cygwin は `_WIN32` を定義しない**。消費側は ELF 流の直接シンボル `_ZN3tbb…` を要求するのに、
>   PE の import library は `__imp__ZN3tbb…` しか持たず、リンクが通らない。
> * **静的で建てると**: ld の「アーカイブは一度しか走査しない」規則に当たる。リンク行で TBB より
>   後ろに `libgeogram.a` が再度現れるため未定義参照が大量に出る。
> * さらに静的 TBB は srava の「**TBB は 1 プロセス 1 インスタンス**」という原則にも反する。
>
> ⚠ `cherchi` は abseil の `policy_checks.h` が `#if defined(__CYGWIN__) / #error "Cygwin is not
> supported."` と書いており、これは**上流のサポート方針**であって除外リストの漏れではない。回避しない。

## 5. ビルド

tinyState は Cygwin の `/usr/local` に install されたものを `find_package` が自動で引く。

```
cd <srava>
CGAL_DIR=~/CGAL-6.0.1/lib/cmake/CGAL BOOST_INCLUDE=~/boostinc bash cygbuild.sh
```

ビルド後、**テストを走らせる前に下記の `rebase` を行う**。

### ⚠ ビルド後に `rebase` が要る

**これをやらないとテストの多くが落ちる。** Cygwin の `fork()` は「子プロセスで DLL を**親と同じ
アドレス**に載せ直す」ことで実装されている。srava は `.dll` を 10 本ほど dlopen してから agent を
fork するので、既定の配置のままだと再配置に失敗する:

```sh
cd build-cyg
rebase -b 0x600000000 -v *.dll
```

⚠ **エラーメッセージが真因を隠す。** 失敗すると srava は次のように報告するが、
ゲートの上限をいくら下げても直らない:

```
*** ERROR fork failed (process limit): the worker gate is open to 16 agents, which
    exceeds this machine's fork/process limit. Lower it with SRAVA_LOAD_CPU=<percent>
    (or SRAVA_LOAD_CPU=0 SRAVA_LOAD_AGENT=<count>) and re-run. ***
```

本当の原因は Cygwin 自身が stderr に出す次の行である:

```
child_info_fork::abort: unable to remap .../nef_hybrid.dll to same address as
parent (0x2150000) - try running rebaseall
```

⚠ 共有機では `rebaseall`(システム全体の DLL を触る)ではなく、**自分のビルドツリーの `.dll` だけ**を
上のように再配置する。`nef_hybrid.dll` は CGAL Nef を含むため非常に大きく、衝突しやすい。

`CMakeLists.txt` が `if(CYGWIN)` で自動処理する Cygwin 固有事項:
- **`-Wa,-mbig-obj`**: CGAL/EPECK の大量テンプレート実体化で `cgMesh2D/3D.cpp.o` が COFF の
  `.text` 上限を超え `file too big` になるため big-obj 形式を有効化(MinGW の gas は不要)。
- **HDF5 の既定無効化**: Cygwin の `libhdf5-devel` は cmake config(`/usr/cmake/hdf5-config.cmake`)が
  壊れており `find_package(HDF5)` が FATAL する。voxelize(export_vox)は任意機能なので Cygwin では
  既定で無効化する。config を直して有効化する場合は `-DSRAVA_ENABLE_HDF5=ON`。
- **geogram のパッチ**: OS 判定と FPE 制御の 2 箇所に Cygwin を足す(→ §4)。冪等なので再 configure
  しても二重に当たらない。

## テスト状況

**常用の検証プラットフォームは Linux / macOS / Windows(MinGW) の 3 つで、いずれも full ctest が
100% 通る**(Windows は `ctest -j2` で回すこと)。

Cygwin 固有だった不具合は**いずれも解消済み**:

- **teardown のランダム SEGFAULT** — `select()` が `-1` / `errno==0`(スプリアス)を返すのを
  `fwIO` の error 分岐が拾えず、負値を「完了」と取り違えて app を早期 finalize していた。
  tinyState 側で修正済み。
- **`srava_plugin_echo`** — `PIG_PLUGIN_PATH` の `':'` 分割が `C:/…` の**ドライブレターの colon**を
  区切りと誤認していた。ドライブ colon を区切りから除外。
- **`srava_value_roundtrip`** — VALUE モードが flush を通らずに終了し、パイプ時のフルバッファで
  出力が消えていた。出力直後に flush する。
- **`async_err`**(実際は全エラー共通) — planner が終了コードを書いても、Cygwin では通常の
  `return` 経路(atexit + 静的デストラクタ)がそれを 0 に落としていた。
- **`srava_export_formats`** — 3MF 自体は正しく書けており、**Cygwin に `unzip` が無い**だけだった。
  srava の 3MF ZIP は無圧縮(STORE)なので、`unzip` 不在時は raw grep へフォールバックする。

> ⚠ **Cygwin は定常の検証対象には入っていない**。上記の修正時点では full ctest が通っていたが、
> その後テストは増えているので、最新の木で通るかは実機で確かめること。

### ⚠ `-DCMAKE_BUILD_TYPE=Release` を必ず指定する

`cygbuild.sh` はビルドタイプを指定しないので、**無指定だと最適化が一切かからない**。数値最適化を
回す op(`pipe_adjust` / `pipe_scene_adjust` 等)がこれで桁違いに遅くなり、テストが TIMEOUT する:

```sh
CGAL_DIR=... BOOST_INCLUDE=... bash cygbuild.sh -DCMAKE_BUILD_TYPE=Release
```

⚠ `-Wa,-mbig-obj` は最適化とは無関係(COFF の `.text` 上限対策)なので、`Release` と併用してよい。

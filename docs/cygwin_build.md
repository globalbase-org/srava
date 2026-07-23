# srava を Cygwin でビルドする

Cygwin は POSIX 層なので、MinGW 特有の問題(ts2System の `#` 直接 exec・IOCP・O_BINARY・
`/tmp` パス・named pipe)は当てはまらず、実質 Linux 相当でビルド/実行できる。ただし Cygwin は
**CGAL をパッケージしておらず**、**Boost が古い(1.66)**ため、CGAL 6.x と新しめ Boost を手当てする
必要がある。以下は NucBox7 実機(`CYGWIN_NT` / g++ 11.5)での確立手順。

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

## 4. ビルド

tinyState は Cygwin `/usr/local` の install(tinyState あきら管理)を find_package が自動で引く。

```
cd <srava>
CGAL_DIR=~/CGAL-6.0.1/lib/cmake/CGAL BOOST_INCLUDE=~/boostinc bash cygbuild.sh
cd build-cyg && ctest -j4
```

`CMakeLists.txt` が `if(CYGWIN)` で自動処理する Cygwin 固有事項:
- **`-Wa,-mbig-obj`**: CGAL/EPECK の大量テンプレート実体化で `cgMesh2D/3D.cpp.o` が COFF の
  `.text` 上限を超え `file too big` になるため big-obj 形式を有効化(MinGW の gas は不要)。
- **HDF5 の既定無効化**: Cygwin の `libhdf5-devel` は cmake config(`/usr/cmake/hdf5-config.cmake`)が
  壊れており `find_package(HDF5)` が FATAL する。voxelize(export_vox)は任意機能なので Cygwin では
  既定で無効化する。config を直して有効化する場合は `-DSRAVA_ENABLE_HDF5=ON`。

## 既知のテスト状況(2026-07-20 時点)

full ctest は **96%(169/176)**。srava 本体・幾何・大半の言語機能は動作する。残る失敗:

- **ランダムな SEGFAULT ~3件/回**(毎回別テスト・`-j1` でも発生)= **tinyState Cygwin の
  teardown/リソース競合フレーク**(特定テストのバグではない・tinyState 側)。切り分け repro は
  別途(MinGW の #3393 と同系統)。
- **srava_export_formats**(`tmf=0`)= **3MF の大 boolean mesh 書き出しの既知バグ**(MinGW #141 と同一・
  Cygwin 固有でない)。AMF は成功。
- **srava_plugin_echo / async_err / value_roundtrip** = Cygwin 個別調査中。

Linux/MinGW と最適化レベルを揃えるため、Cygwin でも `-O2` は付けない(`-Wa,-mbig-obj` のみで
`file too big` は解消する)。

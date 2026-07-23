#!/bin/bash
# srava を Cygwin でビルドするヘルパ。前提・セットアップ手順は docs/cygwin_build.md 参照。
#
# 必須 env:
#   CGAL_DIR      : CGAL 6.x(header-only)の <prefix>/lib/cmake/CGAL
#   BOOST_INCLUDE : boost/ を含むディレクトリ(Boost >= 1.72)
# 任意 env:
#   BUILD         : ビルドディレクトリ名(既定 build-cyg)
#
# tinyState は Cygwin /usr/local の install を find_package が自動で引く(別 prefix は
# -DCMAKE_PREFIX_PATH= を後ろに足す)。-Wa,-mbig-obj(COFF big-obj)と HDF5 の無効化は
# CMakeLists が if(CYGWIN) で自動処理するのでここでは指定不要。
#
# 例:
#   CGAL_DIR=~/CGAL-6.0.1/lib/cmake/CGAL BOOST_INCLUDE=~/boostinc bash cygbuild.sh
set -e
: "${CGAL_DIR:?export CGAL_DIR=<CGAL-6.x>/lib/cmake/CGAL (docs/cygwin_build.md 参照)}"
: "${BOOST_INCLUDE:?export BOOST_INCLUDE=<boost/ を含む dir> (Boost>=1.72)}"
BUILD="${BUILD:-build-cyg}"
here="$(cd "$(dirname "$0")" && pwd)"
cmake -S "$here" -B "$here/$BUILD" -G Ninja \
  -DCGAL_DIR="$CGAL_DIR" \
  -DBoost_INCLUDE_DIR="$BOOST_INCLUDE" -DBoost_NO_SYSTEM_PATHS=ON \
  "$@"
cmake --build "$here/$BUILD"
echo "OK: built in $here/$BUILD  (ctest: cd $BUILD && ctest -j4)"

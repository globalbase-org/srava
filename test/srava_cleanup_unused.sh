#!/bin/sh
# 終了時クリーンアップ(1.2.5)の回帰テスト: この run で使われなかった完了キャッシュが消えること。
#   1) srava を一度走らせ used キャッシュを生成
#   2) その 1 つを未使用 hash 名でコピー(= 別プログラムの残骸を模した stray 完了キャッシュ)
#   3) もう一度 srava を走らせる → stray は削除、used は残存
# 引数 $1 = srava 実行体パス。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
set -e
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
rm -rf "$D"

"$SRAVA" >/dev/null 2>&1                       # warm: used キャッシュ生成
one=$(ls "$D"/*.cache | head -1)
cp "$one" "$D/ffffffffffffffff.cache"          # stray(未使用 hash)

"$SRAVA" >/dev/null 2>&1                        # run2: HIT → stray は未使用で削除

if [ -e "$D/ffffffffffffffff.cache" ]; then
	echo "FAIL: unused cache was not removed"; exit 1
fi
if [ ! -e "$one" ]; then
	echo "FAIL: used cache was wrongly removed"; exit 1
fi
echo "CLEANUP_OK"

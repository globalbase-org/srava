#!/bin/sh
# ファイル export(2.2)の回帰テスト: export(path, mesh) が拡張子に応じた形式で書くか。
# $1 = srava 実行体。$2 = 形式(off|stl, 既定 off)。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
SRAVA="$1"
FMT="${2:-off}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
OUT="$D.$FMT"
# native srava と MSYS sh で /tmp の解決先が食い違う(C:\tmp vs C:\msys64\tmp)。export パスは
# SRAVA_SOURCE 内リテラルとして native srava に渡るので、Windows では cygpath で両者一致の native 形へ。
# Linux は cygpath 不在 → 変換なし(そのまま)。
command -v cygpath >/dev/null 2>&1 && OUT=$(cygpath -m "$OUT")
rm -rf "$D"; rm -f "$OUT"
SRAVA_SOURCE="export(\"$OUT\", box(2,2,2) ||| box(1,1,3));" "$SRAVA" >/dev/null 2>&1
if [ ! -f "$OUT" ]; then echo "FAIL: output not written"; exit 1; fi
case "$FMT" in
off)
	head -1 "$OUT" | grep -q '^OFF' || { echo "FAIL: not an OFF file"; exit 1; }
	# 2 行目 = "V F E"。union(box(2,2,2),box(1,1,3)) = 25 46 0
	echo "OFF-HEADER $(sed -n '2p' "$OUT")" ;;
stl)
	# binary STL: offset 80 の u32 = 三角形数(union の面数 46)。
	n=$(python3 -c 'import struct,sys; b=open(sys.argv[1],"rb").read(); print(struct.unpack_from("<I",b,80)[0])' "$OUT")
	echo "STL-TRIS $n" ;;
esac

#!/bin/sh
# 1.2.3 可変ソートの回帰: 可換 op(union/intersection)は引数順によらず同一キャッシュキー、
# 非可換(difference)は順序で別キーになることを、result cache のハッシュ比較で検証する。
# $1=srava 実行体。SRAVA_AGENT / SRAVA_CACHE_DIR は cmake が注入。
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"

hashof() {   # $1 = srava ソース → result cache の 16hex を返す。dir を毎回リセットして
	# 他 run のキャッシュ掃除ログ("swept unused cache: <hash>")の混入を防ぐ。result 行限定で抽出。
	rm -rf "$D"
	SRAVA_SOURCE="$1" "$SRAVA" 2>&1 | grep 'result cache=' | grep -oE '[0-9a-f]{16}\.cache' | head -1
}

u1=$(hashof 'export(box(2,2,2) ||| box(1,1,3));')
u2=$(hashof 'export(box(1,1,3) ||| box(2,2,2));')
i1=$(hashof 'export(box(2,2,2) &&& box(1,1,3));')
i2=$(hashof 'export(box(1,1,3) &&& box(2,2,2));')
d1=$(hashof 'export(box(2,2,2) --- box(1,1,3));')
d2=$(hashof 'export(box(1,1,3) --- box(2,2,2));')
# 実行木分解(n-ary): 可換 3 引数は引数順によらず同一キー、difference n-ary は中置左結合と一致。
n1=$(hashof 'export(union(box(2,2,2), box(1,1,3), box(5,5,5)));')
n2=$(hashof 'export(union(box(5,5,5), box(1,1,3), box(2,2,2)));')
f1=$(hashof 'export(difference(box(5,5,5), box(1,1,9), box(2,2,9)));')
f2=$(hashof 'export((box(5,5,5) --- box(1,1,9)) --- box(2,2,9));')
echo "union $u1 $u2 / inter $i1 $i2 / diff $d1 $d2 / nary $n1 $n2 / dfold $f1 $f2"

# 可換は一致、非可換は不一致、n-ary 可換は引数順非依存、difference n-ary=中置左結合(全て取得できている)
if [ -n "$u1" ] && [ "$u1" = "$u2" ] \
   && [ -n "$i1" ] && [ "$i1" = "$i2" ] \
   && [ -n "$d1" ] && [ -n "$d2" ] && [ "$d1" != "$d2" ] \
   && [ -n "$n1" ] && [ "$n1" = "$n2" ] \
   && [ -n "$f1" ] && [ "$f1" = "$f2" ]; then
	echo "COMMSORT_OK"
	exit 0
fi
echo "COMMSORT_FAIL"
exit 1

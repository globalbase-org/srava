#!/bin/sh
# 1.2.3 可換 op の回帰: 二項の可換 op(union/intersection)は引数順によらず同一キャッシュキー、
# 非可換(difference)は順序で別キーになることを、result cache のハッシュ比較で検証する。
# ★ #3500: n 項の可換 op は **引数を並べ替えない** ので、書いた順が違えば木の形が変わり
#   別キーになる。残っているのは二項ノードのキー正規化だけ — つまり「木の形を変えない
#   入れ替え」(均衡木で同じ節点に入る 2 つの交換) は今も同一キーになる。この 2 つを
#   両方とも見る (片方だけだとソートを戻しても気付けない)。
# $1=srava 実行体。SRAVA_AGENT / SRAVA_CACHE_DIR は cmake が注入。
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
# ★ #3569: 要るのは **cgal 1 本だけ** — box / union / intersection / difference / export は
#   すべて cgal (priority 20) が答える。#3452 の互換スイッチ (all.sra 16 本) は外した。
#   ⚠ この検定はキャッシュキーの一致/不一致を見るので、**読むモジュールが変わると鍵も変わる**。
#     比較は同じ run の中で閉じているので、1 本に減らしても関係は保たれる。
MCG='module("cgal.so",{});'

hashof() {   # $1 = srava ソース → result cache の 16hex を返す。dir を毎回リセットして
	# 他 run のキャッシュ掃除ログ("swept unused cache: <hash>")の混入を防ぐ。result 行限定で抽出。
	rm -rf "$D"
	SRAVA_SOURCE="$MCG$1" "$SRAVA" 2>&1 | grep 'result cache=' | grep -oE '[0-9a-f]{16}\.cache' | head -1
}

u1=$(hashof 'export(box(2,2,2) ||| box(1,1,3));')
u2=$(hashof 'export(box(1,1,3) ||| box(2,2,2));')
i1=$(hashof 'export(box(2,2,2) &&& box(1,1,3));')
i2=$(hashof 'export(box(1,1,3) &&& box(2,2,2));')
d1=$(hashof 'export(box(2,2,2) --- box(1,1,3));')
d2=$(hashof 'export(box(1,1,3) --- box(2,2,2));')
# 実行木分解(n-ary)。3 引数 (k=2) の均衡木は ((A|||B)|||C) の形になるので:
#   n1 と n2 … A と C を入れ替えた = 木の形が変わる            → 別キー (#3500: 並べ替えない)
#   n1 と n3 … A と B を入れ替えた = 同じ節点の中の交換だけ    → 同一キー (二項の正規化は健在)
# difference の n-ary は中置左結合と一致する (従来どおり)。
n1=$(hashof 'export(union(box(2,2,2), box(1,1,3), box(5,5,5)));')
n2=$(hashof 'export(union(box(5,5,5), box(1,1,3), box(2,2,2)));')
n3=$(hashof 'export(union(box(1,1,3), box(2,2,2), box(5,5,5)));')
f1=$(hashof 'export(difference(box(5,5,5), box(1,1,9), box(2,2,9)));')
f2=$(hashof 'export((box(5,5,5) --- box(1,1,9)) --- box(2,2,9));')
echo "union $u1 $u2 / inter $i1 $i2 / diff $d1 $d2 / nary $n1 $n2 $n3 / dfold $f1 $f2"

# 二項可換は一致、非可換は不一致、n-ary は木の形が変われば別キー・変わらなければ同一キー、
# difference n-ary = 中置左結合 (すべて取得できていること込みで判定)
if [ -n "$u1" ] && [ "$u1" = "$u2" ] \
   && [ -n "$i1" ] && [ "$i1" = "$i2" ] \
   && [ -n "$d1" ] && [ -n "$d2" ] && [ "$d1" != "$d2" ] \
   && [ -n "$n1" ] && [ -n "$n2" ] && [ "$n1" != "$n2" ] \
   && [ -n "$n3" ] && [ "$n1" = "$n3" ] \
   && [ -n "$f1" ] && [ "$f1" = "$f2" ]; then
	echo "COMMSORT_OK"
	exit 0
fi
echo "COMMSORT_FAIL"
exit 1

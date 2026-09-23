#!/bin/sh
# ★★ #3559: **変種で建てる TU が具体クラス名を直に書いていないこと**を数える。$1 = ソース木。
#
# ---- なぜ要るか (2026-09-19 に実際に踏んだ) ----
# #3559 で nef の幾何クラスを nfMesh (hybrid) / nfMeshSnc (snc) の 2 型に割った。op のモジュールは
# 従来どおり同一ソースを 2 度ビルドし、-D (NF_WIRE_SNC / NF_WIRE_HYBRID) が **どちらの型か**を
# 選ぶ (綴りは @NF_MESH@)。
# ⚠⚠ ここで具体クラス名を直に書くと、**もう一方の変種の値を作ってもコンパイルは通る** —
#   両方の型が同じヘッダに居て、どちらも完全な型だから。落ちるのは実行時、それも *その op* では
#   なく **下流の materialize** で:
#     *** ERROR area: input 1: cannot materialize a value (format 'TEXT') … ***
#   (実例: nfTriSink.h の @finish()@ が @thNEW(nfMesh,())@ のままだったため、nef_snc の
#    prism / tube / icosphere など **9 本の生成 op** が nfb-mesh3d を作っていた。
#    ⚠ nfTriSink.h は op TU が *推移的に* 読むヘッダで、op 側の字面には現れない。)
# ⇒ 型を割った以上、**変種で建てる TU に具体クラス名を書かない**が規約になる。規約は申し送りでは
#   守られないので、数える検査を置く。
#
# ---- 何を見ないか ----
# ・幾何ライブラリ側の TU (nfMesh.cpp / nfWire.cpp) は **-D なしで** 2 型ともコンパイルするので、
#   具体クラス名を書いて**正しい**。⇒ 対象外 (むしろ下の陽性対照に使う)。
# ・橋 (nef_cg / nef_mf) は変種で建てない。受けるのは nf-mesh3d だけなので @nfMeshSnc@ を
#   名指しするのが**正しい** (それが「hybrid を受けない」の実装そのもの)。⇒ 対象外。
D="${1:?source dir not given}"

# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

NFDIR="$D/modules/nef"
[ -d "$NFDIR" ] || { echo "NEFVAR-SKIP $NFDIR が無い"; exit 77; }

# 具体クラス名の出現を数える。
#   ⚠⚠ **コメントと include 行を先に落とす** — 落とさないと @#include "nf/c++/nfMesh.h"@ と
#     解説文が全部当たって「41 本が違反」になる (2026-09-19 に実際にそう出た)。
#     ⇒ ブロックコメントは awk の状態機械で跨いで落とす (行頭 * を消すだけでは足りない)。
#   ⚠ @nfMesh.h@ / @nfMesh.cpp@ は **ファイル名**なので違反ではない ⇒ 直後が @.@ なら除く。
#   ⚠ @nfNefMesh@ (実装クラス) は変種に依らないので当たってはいけない ⇒ 語境界で切る。
#   ★ @NF_MESH@ 経由は **マクロ名しか書かれていない**ので、この綴りには当たらない (それが狙い)。
strip_comments() {
	awk '
	{
		line = $0
		out = ""
		while ( length(line) > 0 ) {
			if ( inc ) {
				i = index(line, "*/")
				if ( i == 0 ) { line = ""; break }
				inc = 0; line = substr(line, i + 2); continue
			}
			i = index(line, "/*"); j = index(line, "//")
			if ( j > 0 && ( i == 0 || j < i ) ) { out = out substr(line, 1, j - 1); line = ""; break }
			if ( i == 0 ) { out = out line; line = ""; break }
			out = out substr(line, 1, i - 1); inc = 1; line = substr(line, i + 2)
		}
		print out
	}' "$1"
}

hits() {   # hits <ファイル> → 具体クラス名を書いている行 (行番号つき)
	strip_comments "$1" | grep -vE '^[[:space:]]*#[[:space:]]*include' |
	  grep -nE '\bnfMeshSnc\b|\bnfMesh\b[^.]'
}
count_names() { hits "$1" | grep -c ''; }

# ---- ★★ 陽性対照: 数え方が **当たることを見てから** 0 を報告する ----
# ⚠ 「0 件でした」は何も見せない — 正規表現が壊れていても 0 になる。
#   幾何ライブラリ側の nfWire.cpp は 2 型とも名指ししていて **正しい**。そこが 0 に見えたら
#   壊れているのは検出器の方。
CTL="$NFDIR/c++/nfWire.cpp"
if [ ! -f "$CTL" ]; then
	echo "NEFVAR-SKIP 陽性対照 (nfWire.cpp) が無い"; exit 77
fi
ctl=$(count_names "$CTL")
echo "--- 陽性対照 nfWire.cpp (2 型を名指ししていて正しい): $ctl 行"
if [ "$ctl" = "0" ]; then
	echo "FAIL: 陽性対照が当たらない — **数え方の方が壊れている**"
	echo "  ⇒ nfWire.cpp は PIG_WIRE_DEF(nfMesh,…) / PIG_WIRE_DEF(nfMeshSnc,…) を書いているので 0 はありえない"
	exit 1
fi

# ---- 本題: 変種で建てる TU ----
# = nef モジュールのソース一式 (op・記述子) と、それらが読むヘッダ。
fails=0
checked=0
for f in "$NFDIR"/c++/nfa*.cpp "$NFDIR"/c++/nfts*.cpp "$NFDIR"/c++/nfCacheCodec.cpp \
         "$NFDIR"/h/nf/c++/nfa*.h "$NFDIR"/h/nf/c++/nfTriSink.h "$NFDIR"/manifest.cpp
do
	[ -f "$f" ] || continue
	checked=$((checked+1))
	n=$(count_names "$f")
	[ "$n" = "0" ] && continue
	echo "FAIL: $(basename "$f") が具体クラス名を $n 行で直に書いている"
	hits "$f" | sed 's/^/       /' | head -5
	fails=$((fails+1))
done

if [ "$checked" = "0" ]; then
	echo "NEFVAR-SKIP 対象の TU が 1 本も無い"; exit 77
fi
echo "--- 変種で建てる TU: $checked 本を検べた"
if [ "$fails" != "0" ]; then
	echo "FAIL: 具体クラス名を直に書いている TU が $fails 本"
	echo "  ⇒ 直し方: @NF_MESH@ と書く (nfMesh.h 末尾の -D が自分の変種へ解決する)"
	echo "  ⇒ 受け皿の型は @nfNefMesh@ (実装クラス・変種に依らない)"
	exit 1
fi
echo "NEFVAR-OK 変種で建てる TU は具体クラス名を書いていない"

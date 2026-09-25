#!/bin/sh
# #3595 段3 — 同梱モジュールは「**.so の幹 == 記述子名**」であること。
# $1 = srava 実行体。env: SRAVA_CACHE_DIR (作業ディレクトリを作る場所として使う)。
#
# ★ なぜ機械で留めるか (ひさ 2026-09-24)
#   `module(配列, opts)` は要素を **記述子名**として受け、未ロードなら
#   `<記述子名> + <拡張子>` を探索路から引いてロードする。この推測生成は
#   「同梱の .so は記述子名と同じ幹を持つ」という前提の上に立っている。
#   ⇒ ずれた .so が入ると `module(["<記述子名>"],{})` が「そんなファイルは無い」で落ちる。
#     *名前は合っているのに* 落ちるので原因が読みにくい。申し送りにせず検査で留める。
#
# ⚠ 対象は **探索路に置かれた同梱モジュールだけ**。探索路の外のものを
#   `module("<path>", {})` で明示的に読む場合は名前が違ってよい (確定仕様の「逃げ道」)。
#
# ★★ 較正 (負の対照) を同梱してある — 「0 件でした」は、**当たる例が 1 つ通る**ことを
#   示して初めて主張になる。幹の違う .so を探索路に足し、この検査が **1 件見つける**ことを見る。
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
NG=0
say() { echo "$1"; }
bad() { echo "FAIL($1)"; NG=1; }

# 探索路に載っているモジュールを列挙し、幹と記述子名がずれている行だけを出す。
# ⚠ 材料は `srava --modules` の **loaded 節** (実際に dlopen できたものだけが出る)。
#   組込 "pig" は path が "(組込)" なので自然に外れる。
mismatches() {   # mismatches [SRAVA_MODULE_PATH] → "MISMATCH <名前> <path>" を 0 行以上
	SRAVA_MODULE_PATH="$1" "$SRAVA" --modules 2>&1 | awk '
		/^loaded:/        { f = 1; next }
		f && $1 == "name" { next }
		f && NF >= 5 {
			p = $NF
			if ( p !~ /\.so$/ && p !~ /\.dll$/ && p !~ /\.dylib$/ ) next
			stem = p
			sub(/.*\//, "", stem)
			sub(/\.[^.]*$/, "", stem)
			if ( stem != $1 ) printf "MISMATCH %s %s\n", $1, p
		}'
}
paths() {        # 探索路に載っている .so の実パス
	"$SRAVA" --modules 2>&1 | awk '
		/^loaded:/        { f = 1; next }
		f && $1 == "name" { next }
		f && NF >= 5 && $NF ~ /\.(so|dll|dylib)$/ { print $NF }'
}

# ---- ① 同梱は全部一致している ----
N=$(paths | wc -l | tr -d ' ')
[ "$N" -ge 2 ] || bad "モジュールが $N 本しか見えない (探索路が空? 検査が空振りしている)"
M=$(mismatches "" | wc -l | tr -d ' ')
if [ "$M" -ne 0 ]; then
	bad "幹と記述子名がずれた同梱モジュールが $M 本ある"
	mismatches "" | sed 's/^/    /'
fi
say "  1) 同梱 $N 本すべて .so の幹 == 記述子名"

# ---- ② 較正 — 幹を変えた .so を足すと **見つかる** ----
#   ★ これが出ないなら、上の「0 件」は検査が動いていないだけかもしれない。
W="$D-namecheck"
rm -rf "$W"; mkdir -p "$W" || bad "作業ディレクトリが作れない $W"
#   いちばん小さいモジュールを選ぶ (複製の費用を抑えるだけの理由)。
SRC=""; SZ=0
for f in $(paths); do
	[ -r "$f" ] || continue
	s=$(wc -c < "$f" 2>/dev/null || echo 0)
	if [ -z "$SRC" ] || [ "$s" -lt "$SZ" ]; then SRC="$f"; SZ="$s"; fi
done
if [ -z "$SRC" ]; then
	bad "較正に使える .so が拾えない"
else
	#   ⚠ 拡張子は **実物から取る**。Windows のモジュールは .dll なので、`.so` を置いても
	#     探索路が拾わず **較正が armed にならない** (2026-09-25 に box で実測)。
	#     ⇒ 「幹だけ変えて拡張子は同じ」にするのが、この較正の意図どおりの形。
	CEXT=$(printf '%s\n' "$SRC" | sed -n 's/.*\(\.[A-Za-z0-9]*\)$/\1/p')
	[ -n "$CEXT" ] || bad "モジュールの拡張子が実パスから取れない ($SRC)"
	CDST="$W/zz_calib_renamed$CEXT"
	ln -s "$SRC" "$CDST" 2>/dev/null || cp "$SRC" "$CDST"
	C=$(mismatches "$W" | wc -l | tr -d ' ')
	if [ "$C" -lt 1 ]; then
		bad "較正が armed でない — 幹を変えた .so を足しても検査が反応しない"
		mismatches "$W" | sed 's/^/    /'
	else
		say "  2) 較正: 幹を変えた .so ($(basename "$SRC") → $(basename "$CDST")) を足すと $C 件見つかる"
	fi
	rm -rf "$W"
fi

[ "$NG" -eq 0 ] && echo "MODULE-NAME-MATCHES-SO-OK"
exit "$NG"

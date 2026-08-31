#!/bin/sh
# キャッシュの死体 (書きかけ) の後始末の回帰。$1 = srava 実行体。$2 = モード。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★なぜ要るか (2026-08-26): agent が SIGABRT 等で **書きかけのまま死ぬ**ことは実際に起きる
#   (モジュールがリンクしたライブラリの致命エラー)。そのとき残った .cache が
#   **黙って有効扱いされる**と、答えが静かに壊れる = いちばん質の悪い壊れ方になる。
#   機構は pigCacheManager にあった (W_END 番兵の有無 + writer_pid の生存で死体を判定) が、
#   **回帰テストが 1 本も無かった**ので固定する。
#
# モード:
#   recycled … ★ pid の使い回しで沈黙ハングしないこと (2026-08-26)。書きかけ + **生きている pid**
#           だが起動時刻が違う → 「writer ではない」と判定して掃く。
#           ⚠ 直す前はここで **reader が永久にポーリングして返らなかった** (実測)。
#           ⚠ 起動時刻を取れない OS (osglue_pid_starttime が 0) では pid だけの判定に落ちるので
#             このモードは **skip する** (でないとテストがハングする)。
#   sweep … ★本題。書きかけ (番兵なし) + writer が死んでいる → 起動時 sweep が消し、再計算する。
#           固定するのは 3 点: ① sweep が実際に消したと言う ② 値が壊れない
#           ③ **ファイルが作り直されている** (= 切り詰めたものを読んでいない)。
#           ⚠ ③ が無いと「たまたま参照されなかった」だけでも通ってしまう。
SRAVA="$1"
MODE="${2:-sweep}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
SRC='print(volume(sphere(1.2, 60)));'

# writer_pid (ストリームヘッダの offset 8・LE u32) を書き換える。
# ★ 元の pid をそのまま使わないのは、**pid の使い回し**で「生きている」と判定されると
#   sweep されず、reader が writer を待って**沈黙ハング**するため (実測で確認済み)。
#   確実に死んでいる pid を使ってテストを決定的にする。
put_pid() {
	_f="$1"; _p="$2"
	printf "$(printf '\\%03o\\%03o\\%03o\\%03o' \
		$((_p % 256)) $(((_p / 256) % 256)) $(((_p / 65536) % 256)) $(((_p / 16777216) % 256)))" \
	| dd of="$_f" bs=1 seek=8 conv=notrunc status=none 2>/dev/null
}

# streamhdr (offset 12・LE u64) の writer 起動時刻を読む。0 = この OS では取れていない。
get_start() {
	od -An -tu8 -j12 -N8 "$1" 2>/dev/null | tr -d ' \n'
}

case "$MODE" in
recycled)
	rm -rf "$D"
	OUT1=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="$SRC" "$SRAVA" 2>&1)
	V1=$(echo "$OUT1" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')
	[ -n "$V1" ] || { echo "FAIL: 1 回目で値が出ていない"; echo "$OUT1"; exit 1; }

	F=""
	for C in "$D"/*.cache; do
		[ -f "$C" ] || continue
		S=$(wc -c < "$C")
		[ "$S" -gt 100 ] || continue
		F="$C"; break
	done
	[ -n "$F" ] || { echo "FAIL: 壊す対象の .cache が無い"; ls -la "$D"; exit 1; }

	ST=$(get_start "$F")
	if [ -z "$ST" ] || [ "$ST" = "0" ]; then
		rm -rf "$D"
		echo "CORPSE-RECYCLED-SKIP (この OS では writer の起動時刻を取れない = pid だけの判定)"
		exit 0
	fi

	S=$(wc -c < "$F")
	dd if="$F" of="$F.t" bs=1 count=$((S * 2 / 3)) status=none 2>/dev/null
	mv "$F.t" "$F"
	# ★ pid だけ **生きているもの (自分)** に差し替える。起動時刻は元の (死んだ agent の) まま
	#   = OS が pid を使い回した状態そのもの。
	put_pid "$F" "$$"
	echo "  writer pid=$$ (生存) / 起動時刻=$ST (元のまま) = 使い回しの再現"

	OUT2=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="$SRC" "$SRAVA" 2>&1)
	V2=$(echo "$OUT2" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')
	# ① 返ってくること (直す前はここで永久ハング → ctest の TIMEOUT で落ちた)
	[ -n "$V2" ] || { echo "FAIL: 値が返らない (writer を待ち続けている疑い)"; echo "$OUT2"; exit 1; }
	# ② 掃かれていること
	SW=$(echo "$OUT2" | sed -n 's/.*startup sweep: \([0-9]*\) corpse.*/\1/p')
	[ -n "$SW" ] && [ "$SW" -ge 1 ] || {
		echo "FAIL: 使い回し pid の死体が掃かれていない (sweep=${SW:-なし})"; echo "$OUT2"; exit 1; }
	# ③ 値が壊れていないこと
	[ "$V1" = "$V2" ] || { echo "FAIL: 値が変わった ($V1 -> $V2)"; echo "$OUT2"; exit 1; }
	rm -rf "$D"
	echo "CORPSE-RECYCLED-OK swept=$SW value=$V2" ;;
sweep)
	rm -rf "$D"
	OUT1=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="$SRC" "$SRAVA" 2>&1)
	V1=$(echo "$OUT1" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')
	[ -n "$V1" ] || { echo "FAIL: 1 回目で値が出ていない"; echo "$OUT1"; exit 1; }

	# 確実に死んでいる pid を 1 つ作る (起動して即終了させ、回収まで待つ)。
	( exit 0 ) & DEADPID=$!
	wait $DEADPID 2>/dev/null

	# 100 バイト超の .cache を全部「書きかけ」にし、writer は死んでいることにする。
	N=0
	for F in "$D"/*.cache; do
		[ -f "$F" ] || continue
		S=$(wc -c < "$F")
		[ "$S" -gt 100 ] || continue
		echo "$F $S" >> "$D/.orig"
		dd if="$F" of="$F.t" bs=1 count=$((S * 2 / 3)) status=none 2>/dev/null
		mv "$F.t" "$F"
		put_pid "$F" "$DEADPID"
		N=$((N+1))
	done
	[ "$N" -gt 0 ] || { echo "FAIL: 壊す対象の .cache が無い"; ls -la "$D"; exit 1; }
	echo "  $N 個の .cache を書きかけにした (writer pid=$DEADPID = 死亡済み)"

	OUT2=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="$SRC" "$SRAVA" 2>&1)
	V2=$(echo "$OUT2" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')

	# ① sweep が実際に消したと言っているか
	SW=$(echo "$OUT2" | sed -n 's/.*startup sweep: \([0-9]*\) corpse.*/\1/p')
	[ -n "$SW" ] && [ "$SW" -ge 1 ] || {
		echo "FAIL: 死体が掃かれていない (startup sweep=${SW:-なし}・期待 >= 1)"; echo "$OUT2"; exit 1; }

	# ② 値が壊れていないか
	[ "$V1" = "$V2" ] || { echo "FAIL: 値が変わった ($V1 -> ${V2:-なし})"; echo "$OUT2"; exit 1; }

	# ③ ★ ファイルが作り直されているか = 切り詰めたものを読んでいない証拠。
	#    ⚠ これが無いと「その cache がたまたま参照されなかった」だけでも通ってしまう。
	REBUILT=0
	while read -r F S; do
		[ -f "$F" ] || continue
		NOW=$(wc -c < "$F")
		# ⚠ macOS の wc -c は数値の前に空白を詰めるので **文字列比較にしない**
		#   (Linux の GNU wc は詰めないので、そちらでは表面化しなかった)。
		[ "$NOW" -eq "$S" ] 2>/dev/null && REBUILT=$((REBUILT+1))
	done < "$D/.orig"
	[ "$REBUILT" -ge 1 ] || {
		echo "FAIL: 作り直された .cache が 1 つも無い (切り詰めたものを読んでいる疑い)"
		echo "$OUT2"; ls -la "$D"; exit 1; }

	rm -rf "$D"
	echo "CORPSE-SWEEP-OK swept=$SW rebuilt=$REBUILT value=$V2" ;;
*)
	echo "unknown mode: $MODE"; exit 1 ;;
esac

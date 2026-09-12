#!/bin/sh
# ★ #3503: **in-proc の居座り panic** の回帰。$1 = srava 実行体。$2 = モード。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR, SRAVA_MODULE_PATH。
#
# ★ 何を見ているか
#   in-proc の実行体が destroy に応じないと、planner は TSE_RETURN を永久に待つ
#   (**殺せる子プロセスが無い**ので、抜ける道は planner ごと abort する以外に無い)。
#   ⚠ ただし in-proc と process は **同居する**。abort が早すぎると、そのとき生きている
#     agent プロセスが全部迷子になる (子は setpgid で別プロセスグループに居るので端末の
#     シグナルも届かない) = #3417 が潰した居残りに戻る。
#   ⇒ 「子が居なくなってから撃つ」ことまで含めて固定する。
#
#   ⚠ 居座りは **d4_wedge** で作る (中断要求を一切見ない in-proc op・テスト用フック)。
#     実カーネルはどれも中断に応じるか process 専用かで、これを意図的に作れない。
SRAVA="${1:?srava binary not given}"
MODE="${2:-off}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"

# ★ 生きている agent プロセスの数。⚠ pgrep -f srava_agent は **自分のシェルのコマンド文字列**
#   (env 指定に srava_agent を含む) にも当たるので使わない。zombie も除く。
count_agents() {
	ps -A -o stat,command 2>/dev/null |
	awk '$2 ~ /srava_agent$/ && $1 !~ /^Z/ {n++} END{print n+0}'
}

case "$MODE" in
off)
	# 既定 (SRAVA_INPROC_PANIC_MS 未設定) では panic しない = 居座ったら終わらない。
	# ⇒ **ハングすること**を確かめる (これが panic の存在理由)。
	rm -rf "$D-off"
	SRAVA_CACHE_DIR="$D-off" SRAVA_SOURCE='module("d4.so",{priority:99}); print("W", d4_wedge(30.0));' \
	    "$SRAVA" > "$D-off.out" 2>&1 &
	P=$!
	sleep 2
	kill -INT "$P" 2>/dev/null
	sleep 3
	if kill -0 "$P" 2>/dev/null; then
		kill -9 "$P" 2>/dev/null; wait "$P" 2>/dev/null
		echo "INPROC-PANIC-OFF-OK 既定では panic せず居座る (期待どおり)"
	else
		wait "$P" 2>/dev/null
		echo "FAIL: 既定なのに終了した (panic が既定で有効になっていないか?)"; exit 1
	fi
	rm -rf "$D-off" ;;
on)
	# panic 有効。★ 子プロセス (demo=process 専用) と in-proc の居座りを **同時に**走らせ、
	#   (a) abort すること (b) そのとき生きている agent プロセスが残らないこと を見る。
	rm -rf "$D-on"
	before=$(count_agents)
	SRAVA_CACHE_DIR="$D-on" SRAVA_INPROC_PANIC_MS=800 \
	  SRAVA_SOURCE='module("d4.so",{priority:99}); module("demo.so",{priority:50});
	                print("X", demo_spin(30.0) + d4_wedge(60.0));' \
	    "$SRAVA" > "$D-on.out" 2>&1 &
	P=$!
	sleep 4
	mid=$(count_agents)
	[ "$mid" -gt "$before" ] || { echo "FAIL: 子プロセスが走っていない (before=$before mid=$mid) = 検定になっていない";
	                              kill -9 "$P" 2>/dev/null; wait "$P" 2>/dev/null; exit 1; }
	kill -INT "$P" 2>/dev/null
	( sleep 20; kill -9 "$P" 2>/dev/null ) &
	W=$!
	wait "$P"; RC=$?
	kill "$W" 2>/dev/null
	after=$(count_agents)
	# (a) abort で終わったか (128+SIGABRT=134)
	[ "$RC" = "134" ] || { echo "FAIL: 終了コードが $RC (期待 134 = SIGABRT = panic)"; tail -5 "$D-on.out"; exit 1; }
	# (b) ★ 迷子が居ないこと
	[ "$after" -le "$before" ] || { echo "FAIL: agent プロセスが $after 残った (開始前 $before) = 迷子"; exit 1; }
	# (c) 理由が出ていること (黙って落ちない)
	grep -q "did not fold after the abort request" "$D-on.out" || {
		echo "FAIL: panic の理由が出ていない"; tail -5 "$D-on.out"; exit 1; }
	grep -q "No agent processes remain" "$D-on.out" || {
		echo "FAIL: 「子が残っていない」の確認が出ていない"; tail -5 "$D-on.out"; exit 1; }
	echo "INPROC-PANIC-ON-OK abort (rc=$RC)・agent 残 $after (開始前 $before)"
	rm -rf "$D-on" ;;
*)
	echo "unknown mode: $MODE"; exit 1 ;;
esac

#!/bin/sh
# #3417: 撤収 (Ctrl+C 等) の回帰。$1 = srava 実行体。$2 = モード。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ 何を見ているか
#   sigint … 計算中の agent に SIGINT を撃って、(a) 短時間で終わる (b) agent が 1 つも残らない
#            (c) 終了コードが 128+SIGINT = 130 になる、の 3 つ。
#
#   ⚠ **SIGINT は agent には届かない**。ts2System が setpgid で agent を別プロセスグループに
#     置くので、端末の Ctrl+C も kill -INT <planner> も planner にしか当たらない。
#     agent を止められるのは planner が駆動する撤収だけで、この経路が壊れると
#     「planner だけ消えて agent が計算を続ける」= 居残りになる (#3417 の元々の動機)。
#
#   ⚠ 遅い op には demo モジュールの demo_spin を使う。実カーネルに触らずに
#     「計算中の agent」を作れる唯一の手段 (demo_spin は 10ms 刻みで is_destroyed() を見る)。
SRAVA="${1:?srava binary not given}"
MODE="${2:-sigint}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"

# ★ 生きている agent プロセスの数 (srava_inproc_panic.sh と同じ数え方)。
#   ⚠⚠ **`pgrep -f srava_agent` を使ってはいけない** — コマンド行の *どこかに* その文字列が
#     あるプロセスすべてに当たる。同じ機体で別のビルドが走っていると、コンパイラの
#     `-DSRAVA_AGENT_DEFAULT="/usr/local/bin/srava_agent"` に当たって **14〜24 個の "残存 agent"**
#     を報告する (2026-09-12 に実際に踏んだ。ピアのクローンのビルドが原因で、こちらの
#     撤収は正常だった)。⇒ **実行体名で照合する**。zombie も除く。
count_agents() {
	ps -A -o stat,command 2>/dev/null |
	awk '$2 ~ /srava_agent$/ && $1 !~ /^Z/ {n++} END{print n+0}'
}

case "$MODE" in
sigint)
	rm -rf "$D"
	SRAVA_SOURCE='module("demo.so",{priority:99}); print("R", demo_spin(30.0));' \
	  "$SRAVA" > "$D.out" 2>&1 &
	P=$!
	sleep 3
	# 走り出していること (すぐ死んでいたら検査になっていない)
	kill -0 "$P" 2>/dev/null || { echo "FAIL: 3 秒待つ前に planner が終了した"; cat "$D.out"; exit 1; }
	T0=$(date +%s)
	kill -INT "$P" 2>/dev/null
	wait "$P"; RC=$?
	T1=$(date +%s)
	EL=$((T1 - T0))
	# (a) 短時間で終わる。demo_spin は残り 27 秒ぶん回るはずなので、5 秒以内なら撤収が効いている
	[ "$EL" -le 5 ] || { echo "FAIL: SIGINT から終了まで $EL 秒 (5 秒以内であるべき = 計算を最後まで走らせている)"; exit 1; }
	# (b) agent が残っていない
	N=$(count_agents)
	[ "$N" = "0" ] || { echo "FAIL: srava_agent が $N 個残っている"; exit 1; }
	# (c) 終了コード 128+SIGINT
	[ "$RC" = "130" ] || { echo "FAIL: 終了コードが $RC (期待 130 = 128+SIGINT)"; cat "$D.out"; exit 1; }
	echo "TEARDOWN-SIGINT-OK ${EL}秒で終了・agent 残存 0・exit $RC" ;;
*)
	echo "unknown mode: $MODE"; exit 1 ;;
esac

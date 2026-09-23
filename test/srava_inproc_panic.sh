#!/bin/sh
# ★ #3503: **in-proc の居座り panic** の回帰。$1 = srava 実行体。$2 = モード。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR, SRAVA_MODULE_PATH, WIN_SIGBREAK (Windows のみ)。
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
#
# ★★ 撃ち方・数え方・終了コードはすべて OS で違う (#3521・2026-09-15 に移植)
#   撃ち方   POSIX   = kill -INT <planner>
#            Windows = win_sigbreak 経由で **新プロセスグループへ CTRL_BREAK**
#            ⚠ MSYS の kill は **native プロセスに届かない。しかも rc=0 を返す**。
#              旧版はここで届かないシグナルを撃っており、Windows では
#              **off が「居座った」と誤って合格し、on は数え方で落ちていた**。
#   数え方   test/srava_count_agents.sh (共通)。⚠ ここにコピーを作らないこと
#   終了コード POSIX = 134 (128+SIGABRT) / Windows = **3** (MSVCRT の abort・実測)
SRAVA="${1:?srava binary not given}"
MODE="${2:-off}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
# ★ 生きている agent プロセスの数 (共通実装)。⚠ **コピーを作らないこと** — 片方だけ
#   直して取り残す事故が実際に起きた (#3521)。
. "$(dirname "$0")/srava_count_agents.sh"

# ★ モジュールの拡張子は OS 依存 (Linux/macOS=.so / MinGW・Cygwin=.dll)。実物を見て決める
#   (srava_teardown.sh・srava_parse.sh と同じ作法)。
SODIR="$(dirname "$SRAVA")"
SOEXT=.so; [ -f "$SODIR/d4.so" ] || SOEXT=.dll

# ★ abort の終了コード。POSIX は 128+SIGABRT、Windows の MSVCRT は 3 (2026-09-15 実測)。
ABORT_RC=134
[ -n "$WIN_SIGBREAK" ] && [ -x "$WIN_SIGBREAK" ] && ABORT_RC=3

case "$MODE" in
off)
	# 既定 (SRAVA_INPROC_PANIC_MS 未設定) では panic しない = 居座ったら終わらない。
	# ⇒ **ハングすること**を確かめる (これが panic の存在理由)。
	rm -rf "$D-off"
	SRC="module(\"d4$SOEXT\",{priority:99}); print(\"W\", d4_wedge(30.0));"
	if [ -n "$WIN_SIGBREAK" ] && [ -x "$WIN_SIGBREAK" ]; then
		# ---- Windows ----
		# ⚠⚠ ここを kill -INT のままにすると **検定にならない** — 届かないので必ず
		#   居座り、「期待どおり居座った」と **空振りで合格**する (2026-09-15 に実測。
		#   box では本当に Passed していた)。⇒ CTRL_BREAK を実際に届けてから見る。
		# ★ win_sigbreak は撃った後 15 秒待ち、終わらなければ TerminateProcess して
		#   **rc=3 (HUNG)** を返す。この "HUNG" こそが off の期待結果。
		SRAVA_CACHE_DIR="$D-off" SRAVA_SOURCE="$SRC" \
		  "$WIN_SIGBREAK" "$SRAVA" 2000 > "$D-off.out" 2>&1
		WRC=$?
		[ "$WRC" = "3" ] || {
			echo "FAIL: CTRL_BREAK で終了した (win_sigbreak rc=$WRC) = panic が既定で有効になっていないか?"
			cat "$D-off.out"; exit 1; }
		echo "INPROC-PANIC-OFF-OK 既定では panic せず居座る (期待どおり・CTRL_BREAK 到達を確認)"
	else
		# ---- POSIX ----
		SRAVA_CACHE_DIR="$D-off" SRAVA_SOURCE="$SRC" \
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
	fi
	rm -rf "$D-off" ;;
on)
	# panic 有効。★ 子プロセス (demo=process 専用) と in-proc の居座りを **同時に**走らせ、
	#   (a) abort すること (b) そのとき生きている agent プロセスが残らないこと を見る。
	rm -rf "$D-on"
	SRC="module(\"d4$SOEXT\",{priority:99}); module(\"demo$SOEXT\",{priority:50});
	     print(\"X\", demo_spin(30.0) + d4_wedge(60.0));"
	before=$(count_agents)
	if [ -n "$WIN_SIGBREAK" ] && [ -x "$WIN_SIGBREAK" ]; then
		# ---- Windows ----
		# ★ win_sigbreak を **背後で**走らせ、撃つ前 (4 秒) に mid を数える。
		#   ⇒ 5 秒で CTRL_BREAK が飛ぶので、数える時点ではまだ子が生きている。
		SRAVA_CACHE_DIR="$D-on" SRAVA_INPROC_PANIC_MS=800 SRAVA_SOURCE="$SRC" \
		  "$WIN_SIGBREAK" "$SRAVA" 5000 > "$D-on.out" 2>&1 &
		P=$!
		sleep 4
		mid=$(count_agents)
		[ "$mid" -gt "$before" ] || { echo "FAIL: 子プロセスが走っていない (before=$before mid=$mid) = 検定になっていない";
		                              kill -9 "$P" 2>/dev/null; wait "$P" 2>/dev/null; exit 1; }
		wait "$P"; WRC=$?
		[ "$WRC" = "3" ] && { echo "FAIL: srava が畳まれなかった (win_sigbreak HUNG)"; tail -5 "$D-on.out"; exit 1; }
		# srava の終了コードは win_sigbreak が stderr に出す (自分の rc には載せない)。
		RC=$(sed -n 's/.*srava exit code=\([0-9][0-9]*\).*/\1/p' "$D-on.out" | tail -1)
		[ -n "$RC" ] || { echo "FAIL: win_sigbreak が srava の終了コードを報告しなかった"; tail -5 "$D-on.out"; exit 1; }
	else
		# ---- POSIX ----
		SRAVA_CACHE_DIR="$D-on" SRAVA_INPROC_PANIC_MS=800 SRAVA_SOURCE="$SRC" \
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
	fi
	after=$(count_agents)
	# (a) abort で終わったか (POSIX 134 = 128+SIGABRT / Windows 3 = MSVCRT abort)
	[ "$RC" = "$ABORT_RC" ] || { echo "FAIL: 終了コードが $RC (期待 $ABORT_RC = abort = panic)"; tail -5 "$D-on.out"; exit 1; }
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

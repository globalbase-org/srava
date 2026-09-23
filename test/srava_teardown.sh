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
#
# ★★ 撃ち方は OS で違う (#3520・2026-09-15)
#   POSIX    kill -INT <planner>
#   Windows  win_sigbreak 経由で **新プロセスグループへ CTRL_BREAK** (env WIN_SIGBREAK で渡す)
#            ⚠ MSYS の kill は native プロセスに届かない。**しかも rc=0 を返す**
#   ⇒ 「撃てたか」を kill の戻り値で判断してはいけない。**効果 (終了コード 130) で見る**。
SRAVA="${1:?srava binary not given}"
MODE="${2:-sigint}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

# ★ 生きている agent プロセスの数 — **共通実装を source する** (#3521・2026-09-15)。
#   ⚠⚠ かつてここに実体のコピーがあり、Windows 対応を **こちらにだけ**入れて
#     srava_inproc_panic.sh の同じ関数が取り残された。チケットには「移植済み」と
#     残ったが、報告されていたテストの方は直っていなかった。**コピーを作らないこと**。
. "$(dirname "$0")/srava_count_agents.sh"

# ---- 撃ち方を 1 か所に (⚠ モードごとに複製しない — 冒頭の count_agents の事故と同じ型) ----
# shoot <sra ソース> → RC (srava の終了コード) ・ EL (撃ってから終わるまでの秒) を設定する。
#   ★ 撃ち方は OS で違う (上の ★★ 参照)。**違うのは配送経路だけ**なので、判定は呼び手が持つ。
shoot() {
	_src="$1"
	rm -rf "$D"
	if [ -n "$WIN_SIGBREAK" ] && [ -x "$WIN_SIGBREAK" ]; then
		# ---- Windows ------------------------------------------------------
		BRK_MS=3000
		T0=$(date +%s)
		SRAVA_SOURCE="$_src" "$WIN_SIGBREAK" "$SRAVA" "$BRK_MS" > "$D.out" 2>&1
		WRC=$?
		T1=$(date +%s)
		[ "$WRC" = "3" ] && { echo "FAIL: srava がグレースフルに終了しなかった (win_sigbreak HUNG)"; cat "$D.out"; exit 1; }
		# srava の終了コードは win_sigbreak が stderr に出す (自分の rc には載せない)。
		RC=$(sed -n 's/.*srava exit code=\([0-9][0-9]*\).*/\1/p' "$D.out" | tail -1)
		[ -n "$RC" ] || { echo "FAIL: win_sigbreak が srava の終了コードを報告しなかった"; cat "$D.out"; exit 1; }
		# 撃った時点からの経過 = 全体 - 撃つまでの待ち
		EL=$(( T1 - T0 - BRK_MS / 1000 ))
		[ "$EL" -lt 0 ] && EL=0
	else
		# ---- POSIX --------------------------------------------------------
		SRAVA_SOURCE="$_src" "$SRAVA" > "$D.out" 2>&1 &
		P=$!
		sleep 3
		# 走り出していること (すぐ死んでいたら検査になっていない)
		kill -0 "$P" 2>/dev/null || { echo "FAIL: 3 秒待つ前に planner が終了した"; cat "$D.out"; exit 1; }
		T0=$(date +%s)
		kill -INT "$P" 2>/dev/null
		wait "$P"; RC=$?
		T1=$(date +%s)
		EL=$((T1 - T0))
	fi
}

# 撤収が効いたかの判定 (3 つ・全モード共通)。$1 = 表示名
check_folded() {
	# (a) 短時間で終わる。demo_spin は残り 27 秒ぶん回るはずなので、5 秒以内なら撤収が効いている
	[ "$EL" -le 5 ] || { echo "FAIL: $1 — SIGINT から終了まで $EL 秒 (5 秒以内であるべき = 計算を最後まで走らせている)"; cat "$D.out"; exit 1; }
	# (b) agent が残っていない
	N=$(count_agents)
	[ "$N" = "0" ] || { echo "FAIL: $1 — srava_agent が $N 個残っている"; exit 1; }
	# (c) 終了コード 128+SIGINT
	[ "$RC" = "130" ] || { echo "FAIL: $1 — 終了コードが $RC (期待 130 = 128+SIGINT)"; cat "$D.out"; exit 1; }
}

# ★ モジュールの拡張子は OS 依存 (Linux/macOS=.so / MinGW・Cygwin=.dll)。実物を見て決める
#   (srava_parse.sh と同じ作法)。
SODIR="$(dirname "$SRAVA")"
SOEXT=.so; [ -f "$SODIR/demo.so" ] || SOEXT=.dll

case "$MODE" in
sigint)
	shoot "module(\"demo$SOEXT\",{priority:99}); print(\"R\", demo_spin(30.0));"
	check_folded "try の外"
	echo "TEARDOWN-SIGINT-OK ${EL}秒で終了・agent 残存 0・exit $RC" ;;
tryasync)
	# ★★ #3482 (2026-09-20): **利用者が書いた try の中で、statement1 より長生きする計算**に
	#   撤収が届くか。
	#
	# ---- なぜこの形でないと検定にならないか (陰性対照で確かめた) ----
	# agent の登録先は **囲む最も内側の try 1 つだけ**で、根の台帳へは登録しない。
	# 外側から内側を畳むのは **destroy が pigData の木を伝わる**方が担う
	# (planner → 文 → try ノード → その待ちリスト → async → agent)。
	# ⚠ ところが @try { print(demo_spin(30)) }@ のように **statement1 の中で走っているだけ**の
	#   agent は、try が statement1 へ送る destroy (ACT_START の args[0]->destroy()) だけで畳まれる。
	#   ⇒ **待ちリスト経由の転送を殺しても緑のまま**だった (2026-09-20 実測)。
	# ★ 判別するのは @async@ — statement1 が値を返した後も走り続けるので、届く経路が
	#   **try の待ちリストしかない**。陰性対照 (pigfTryCatch の destroy_agents 転送を殺す) で
	#   **27 秒 = spin 完走**になることを確認済み。対照 (try の外の async) は 0 秒のまま。
	shoot "module(\"demo$SOEXT\",{priority:99}); try { async { print(\"R\", demo_spin(30.0)); } } catch { print(\"C\"); }"
	check_folded "try の中の async"
	echo "TEARDOWN-TRYASYNC-OK ${EL}秒で終了・agent 残存 0・exit $RC" ;;
sysint)
	# ★ #3541: **system() の子まで撤収が届くか**。#3520 (agent 経路) と対になる検定。
	#
	# ⚠⚠ **`x 15` を直接見てはいけない** — 15 は POSIX の raw waitpid status (SIGTERM で
	#   殺された) の表現で、**Windows は raw の終了コードを返す**ので 15 にならない
	#   (実測: POSIX `exit 5` → 1280 / Windows → 5)。⇒ 値を決め打ちすると移植できない。
	# ★ 代わりに **対照との差**で見る。これなら OS の表現を知らなくても成立する:
	#     撃たない → 子が完走した status (= A)
	#     撃つ     → A と **違う** status  かつ **早く終わる**
	#   ⚠ 対照が無いと「撃ったら早く終わった」が「子が元々早かった」と区別できない。
	#
	# ★★ 2026-09-15: #3541 ② が直った (演算子ノードが引数へ destroy を転送するようになった)
	#   ので、**形 A と形 B の両方**を回す。
	#     形 A  var x = system(...); print("x", x);   assign ノードを経由して届く (元から通った)
	#     形 B  print("x", system(...));              **演算子の引数位置** — ②で直した経路
	#   ⚠ 形 B を落とすと、②の退行が **形 A だけでは見えない** (経路が違うため)。
	rm -rf "$D"
	# 子は「そこそこ長く走る」だけでよい。⚠ 実行体の探し方は OS で違う:
	#   POSIX   sh -c 経由なので `sleep` でよい
	#   Windows 直接 exec なので **絶対パス**が要る (cygpath -m で Windows 形式へ)
	if ps -W >/dev/null 2>&1; then
		_sl=$(command -v sleep 2>/dev/null)
		[ -n "$_sl" ] && command -v cygpath >/dev/null 2>&1 && _sl=$(cygpath -m "$_sl")
		[ -n "$_sl" ] || { echo "FAIL: sleep が見つからない"; exit 1; }
	else
		_sl=sleep
	fi

	_sysint_note=""
	for FORM in A B; do
		case "$FORM" in
		A) SRC="var x = system(\"$_sl 12\"); print(\"x\", x);" ;;
		B) SRC="print(\"x\", system(\"$_sl 12\"));" ;;
		esac

		# --- 対照: 撃たずに完走させる ---
		rm -rf "$D"
		T0=$(date +%s)
		SRAVA_SOURCE="$SRC" "$SRAVA" > "$D.ctl" 2>&1
		EL_C=$(( $(date +%s) - T0 ))
		X_C=$(sed -n 's/^x \([0-9-][0-9]*\).*/\1/p' "$D.ctl" | head -1)
		[ -n "$X_C" ] || { echo "FAIL: 形 $FORM の対照で x が取れなかった"; cat "$D.ctl"; exit 1; }
		[ "$EL_C" -ge 9 ] || { echo "FAIL: 形 $FORM の対照が $EL_C 秒で終わった (12 秒走るはず = 検定になっていない)"; exit 1; }

		# --- 本番: 3 秒後に撃つ ---
		rm -rf "$D"
		if [ -n "$WIN_SIGBREAK" ] && [ -x "$WIN_SIGBREAK" ]; then
			BRK_MS=3000
			T0=$(date +%s)
			SRAVA_SOURCE="$SRC" "$WIN_SIGBREAK" "$SRAVA" "$BRK_MS" > "$D.out" 2>&1
			EL=$(( $(date +%s) - T0 - BRK_MS / 1000 ))
		else
			SRAVA_SOURCE="$SRC" "$SRAVA" > "$D.out" 2>&1 &
			P=$!
			sleep 3
			kill -0 "$P" 2>/dev/null || { echo "FAIL: 形 $FORM で 3 秒待つ前に planner が終了した"; cat "$D.out"; exit 1; }
			T0=$(date +%s)
			kill -INT "$P" 2>/dev/null
			wait "$P" 2>/dev/null
			RC=$?
			EL=$(( $(date +%s) - T0 ))
		fi
		[ "$EL" -lt 0 ] && EL=0
		X=$(sed -n 's/^x \([0-9-][0-9]*\).*/\1/p' "$D.out" | head -1)

		# (a) 早く終わる (子は残り 9 秒ぶん走るはずなので、5 秒以内なら撤収が効いている)
		[ "$EL" -le 5 ] || { echo "FAIL: 形 $FORM で中断から終了まで $EL 秒 (5 秒以内であるべき)"; cat "$D.out"; exit 1; }
		# (b) ★ 子が **完走していない** = status が対照と違う
		[ -n "$X" ] || { echo "FAIL: 形 $FORM で x が取れなかった (system() の戻り値が印字されていない)"; cat "$D.out"; exit 1; }
		[ "$X" != "$X_C" ] || {
			echo "FAIL: 形 $FORM で x=$X が対照と同じ = **子まで撤収が届いていない** (#3541 ②)"; cat "$D.out"; exit 1; }
		# (c) ★★ #3541①: **終了コードが 128+signum になる**。
		#   ⚠ 「評価は成功したが中断された」が同時に成り立つ経路で、成功の 0 が 128+signum を
		#     上書きして **130 が 1 に化けて**いた。⇒ agent 経路 (srava_teardown_sigint) では
		#     評価がエラーに落ちるので隠れており、**system 経路でしか見えない**。
		#   ⚠ WIN_SIGBREAK 経路は CTRL_BREAK なので signum が違う ⇒ POSIX の kill 経路だけ見る。
		if [ -z "$WIN_SIGBREAK" ] || [ ! -x "$WIN_SIGBREAK" ]; then
			[ "$RC" = "130" ] || {
				echo "FAIL: 形 $FORM の終了コードが $RC (SIGINT なので 130 = 128+2 であるべき・#3541 ①)"
				cat "$D.out"; exit 1; }
		fi
		_sysint_note="$_sysint_note 形$FORM(${EL}秒/x=$X/対照x=$X_C/rc=${RC:-n/a})"
	done
	echo "TEARDOWN-SYSINT-OK$_sysint_note" ;;
ctrlc)
	# ★★ #3542 の退行検出**専用** (Windows/MinGW のみ・2026-09-15)。
	#
	# ⚠ なぜ別モードが要るか: 上の sigint は移植性のため **CTRL_BREAK** で撃っている
	#   (「Ctrl+C を無視する」状態の対象外なので #3542 の修正前後どちらでも通る)。
	#   ⇒ 裏返すと **Ctrl+C の入口は 1 度も検定されない**。#3542 が退行しても sigint は
	#     緑のままになる。⇒ ここだけが Ctrl+C 経路を踏む。
	#
	# ★ 判定の原理: `CREATE_NEW_PROCESS_GROUP` で起こした子は **Ctrl+C が無効**。
	#   子が `SetConsoleCtrlHandler(NULL, FALSE)` で解除して初めて CTRL_C_EVENT が届く。
	#   ⇒ **CTRL_C が通ること自体が、その解除が入っている証拠**になる。
	#   ⚠ srava 側にはその呼び出しが 1 つも無い (確認済み) ので、解除するのは tinyState。
	#
	# ⚠⚠ **古い tinyState では必ず赤くなる** — 修正は develop-v2 `74722f5` で、
	#   公開版 rc16 以前には入っていない (rc17 は未 tag)。赤が出たら
	#   「テストが壊れた」ではなく **リンクしている tinyState が古い**を先に疑うこと。
	[ -n "$WIN_SIGBREAK" ] && [ -x "$WIN_SIGBREAK" ] || {
		echo "FAIL: WIN_SIGBREAK が無い (このモードは Windows 専用)"; exit 1; }
	rm -rf "$D"
	SODIR="$(dirname "$SRAVA")"
	SOEXT=.so; [ -f "$SODIR/demo.so" ] || SOEXT=.dll
	SRC="module(\"demo$SOEXT\",{priority:99}); print(\"R\", demo_spin(30.0));"
	BRK_MS=3000
	T0=$(date +%s)
	SRAVA_SOURCE="$SRC" "$WIN_SIGBREAK" "$SRAVA" "$BRK_MS" c > "$D.out" 2>&1
	WRC=$?
	EL=$(( $(date +%s) - T0 - BRK_MS / 1000 ))
	[ "$EL" -lt 0 ] && EL=0
	# ★ 対照は撃ち方だけ違う同じ仕事 = srava_teardown_sigint (CTRL_BREAK)。あちらが緑で
	#   こちらだけ赤なら、違いは **イベント種別だけ**なので原因は一意に絞れる。
	[ "$WRC" = "3" ] && {
		echo "FAIL: CTRL_C が握り潰された (win_sigbreak HUNG) = tinyState が継承した"
		echo "      「Ctrl+C を無視する」状態を解除していない (#3542)。"
		echo "      ⇒ リンクしている tinyState が develop-v2 74722f5 より古い可能性。"
		echo "      ⚠ srava_teardown_sigint (CTRL_BREAK) が緑ならテスト側の問題ではない。"
		cat "$D.out"; exit 1; }
	RC=$(sed -n 's/.*srava exit code=\([0-9][0-9]*\).*/\1/p' "$D.out" | tail -1)
	[ -n "$RC" ] || { echo "FAIL: win_sigbreak が srava の終了コードを報告しなかった"; cat "$D.out"; exit 1; }
	# (a) 早く終わる (demo_spin は残り 27 秒ぶん回るはず)
	[ "$EL" -le 5 ] || { echo "FAIL: CTRL_C から終了まで $EL 秒 (5 秒以内であるべき)"; cat "$D.out"; exit 1; }
	# (b) ★ 130 = 128+SIGINT。届いていなければここまで来ない (上の HUNG で落ちる)
	[ "$RC" = "130" ] || { echo "FAIL: 終了コードが $RC (SIGINT なので 130 であるべき)"; cat "$D.out"; exit 1; }
	# (c) 迷子が残っていない
	N=$(count_agents)
	[ "$N" = "0" ] || { echo "FAIL: agent プロセスが $N 残った"; exit 1; }
	echo "TEARDOWN-CTRLC-OK ${EL}秒で終了・agent 残存 0・exit $RC (Ctrl+C の入口が生きている)" ;;
*)
	echo "unknown mode: $MODE"; exit 1 ;;
esac

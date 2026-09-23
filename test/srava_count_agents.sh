# ★ 生きている agent プロセスを数える — **teardown と inproc_panic の共通実装**。
#
# ⚠⚠ **1 箇所にしてあるのは、片方だけ直す事故が実際に起きたから** (#3521・2026-09-15)。
#   b1048a0 で Windows 対応を srava_teardown.sh にだけ入れ、srava_inproc_panic.sh には
#   同じ関数のコピーが残っていた。チケットには「数え方は移植済み」と記録されたが、
#   **報告されていたテストの方は直っていなかった**。⇒ **コピーを作らず、この 1 本を source する**。
#
# ⚠⚠ **`pgrep -f srava_agent` を使ってはいけない** — コマンド行の *どこかに* その文字列が
#   あるプロセスすべてに当たる。同じ機体で別のビルドが走っていると、コンパイラの
#   `-DSRAVA_AGENT_DEFAULT="/usr/local/bin/srava_agent"` に当たって **14〜24 個の "残存 agent"**
#   を報告する (2026-09-12 に実際に踏んだ)。⇒ **実行体のパスで照合する**。zombie も除く。
# ⚠⚠ 実行体名 (srava_agent) だけで数えると **他のクローンの agent まで数える**。
#   ⇒ **$SRAVA_AGENT の絶対パスに一致するものだけ**数える (別クローンは別パス)。
#   ★ SRAVA_AGENT が無い / 相対パスのときだけ従来の名前一致に落とす。
#
# ⚠⚠⚠ **同じクローンで ctest を 2 本同時に回すと、これでも防げない** (2026-09-19 実測)。
#   パスが同一なので *他方の走の agent* を数え、teardown が
#       FAIL: srava_agent が 6 個残っている
#   と報告する。⇒ **撤収の検定は 1 本ずつ**。並列に回すなら別クローンで。
#   ★ 切り分けの手掛かり: 同時走では `nodeps` ラベルの時間が 3 倍に伸びる (180 秒 → 608 秒)。
#     ⚠ 原因が割れるまで「フレーク」と呼ばないこと — *テスト側の揺れ*という含みが入るが、
#       これは **測り方が他人を数えている**という別の話である。
#
# ⚠⚠ **`ps -A -o` は Windows に無い** (#3521・2026-09-15 実測)。MSYS/Cygwin の ps は
#   cygwin 3.6.9 の最小実装で **-A も -o も持たない**:
#       ps -A                  rc=1  "ps: unknown option -- A"
#       ps -o stat,command     rc=1  "ps: unknown option -- o"
#   ⇒ パイプの左が **0 行**になり、正規表現を何に直しても結果は 0。
# ★ 方言の分岐は **`ps -W` が通るか** で切る (Linux では NG・Windows では OK)。
#
# ⚠⚠⚠ **`ps -W` は 1 つの表の中でプロセスごとに違う表記を出す** (2026-09-15 実測。
#   b1048a0 の Windows 分岐が **常に 0 を返していた**真因):
#       /c/Users/.../srava              ← MSYS 由来 (POSIX 表記・.exe 無し)
#       C:\Users\...\srava_agent.exe    ← native   (Windows 表記・.exe 付き)
#   srava が起こす agent は **後者**なので、`cygpath -u` した POSIX パスと直接比べると
#   **永久に一致しない**。⇒ 期待値を **両表記に展開**して行末一致で見る。
#   ⚠ 行の側で "C:/" → "/c/" は畳めない (行頭には PID が来るのでアンカーが効かない)。
#   ⚠ Windows のパスは大文字小文字を区別しないので tolower してから比べる。
# ⚠ STIME 欄は "17:25:12" (1 語) と "Sep  9" (2 語) で **列数が変わる**。
#   ⇒ 列番号で COMMAND を取らず **行末一致**で見る (パスに空白があっても壊れない)。

count_agents() {
	if ps -W >/dev/null 2>&1; then
		# --- Windows (MSYS/Cygwin) ---
		_ca_a="$SRAVA_AGENT"
		if command -v cygpath >/dev/null 2>&1 && [ -n "$_ca_a" ]; then
			_ca_a=$(cygpath -u "$_ca_a" 2>/dev/null || printf '%s' "$SRAVA_AGENT")
		fi
		case "$_ca_a" in
		/*)	ps -W 2>/dev/null | awk -v a="$_ca_a" '
			function norm(p) { gsub(/\\/, "/", p); sub(/\.[Ee][Xx][Ee]$/, "", p); return tolower(p) }
			function ends(l, n) { return (length(l) >= length(n) &&
			                              substr(l, length(l) - length(n) + 1) == n) }
			BEGIN {
				n1 = norm(a)                          # /c/users/.../srava_agent
				n2 = n1
				if (n2 ~ /^\/[a-z]\//)                # c:/users/.../srava_agent
					n2 = substr(n2, 2, 1) ":" substr(n2, 3)
			}
			{ l = norm($0) }
			ends(l, n1) || ends(l, n2) { c++ }
			END { print c+0 }' ;;
		*)	ps -W 2>/dev/null | awk '
			function norm(p) { gsub(/\\/, "/", p); sub(/\.[Ee][Xx][Ee]$/, "", p); return tolower(p) }
			{ l = norm($0) }
			l ~ /\/srava_agent$/ { c++ }
			END { print c+0 }' ;;
		esac
	else
		# --- POSIX ---
		case "$SRAVA_AGENT" in
		/*)	ps -A -o stat,command 2>/dev/null |
			awk -v a="$SRAVA_AGENT" '$2 == a && $1 !~ /^Z/ {n++} END{print n+0}' ;;
		*)	ps -A -o stat,command 2>/dev/null |
			awk '$2 ~ /srava_agent$/ && $1 !~ /^Z/ {n++} END{print n+0}' ;;
		esac
	fi
}

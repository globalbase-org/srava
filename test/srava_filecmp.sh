# srava_filecmp.sh — 2 つのファイルが同一かを、移植性のある形で問う共通部品。
#
# ⚠⚠ **MinGW (MSYS2) には既定で cmp / diff が無い** (diffutils が同梱されない)。
#   `if cmp -s a b; then 同じ; else 違う; fi` と書くと、**cmp が無いときも else に落ちる**ので
#   「比較した結果ちがった」と報告してしまう。2026-09-15 に box で実際に踏んだ:
#
#     srava_points          → 「法線の往復でファイルが変わった」   ← 一度も比較していない
#     srava_estimate_normals → 「既定が cgal に解決されていない」   ← 直前の出力は既定と cgal が
#                                                                    同値だと示していた
#
#   ★ どちらも **原因を名指しするメッセージ**なので、読んだ人はその原因を追ってしまう。
#     「比較できなかった」と「比較したら違った」は**別の結果**として扱うこと。
#
#   file_same A B   →  0 = 同一 / 1 = 相違 / 2 = ★ 比較手段が無い / 3 = ★ ファイルが読めない
#
# ⚠⚠ **2 と 3 を分ける** (bench・2026-09-16)。初版は両方 2 で、呼び手が
#   「比較手段が無い (cmp / sha256sum / md5sum のいずれも不在)」と *原因を名指し* していた。
#   ⇒ ファイルが無いだけのときに **確かめていない原因を名乗る**ので、この部品が直そうとしている
#     欠陥そのものを、1 段ずらして再現することになる。
#   ★ @cmp -s a b@ は **ファイルが無いときも 2** を返す (実測) ので、cmp に任せきりにできない。
#
# ⚠ **コピーを作らないこと。** 直すときはこの 1 本を直す (srava_count_agents.sh と同じ約束)。
#
# ⚠⚠ 呼び手は **0 以外を «違う» と読まないこと**。
#     @if file_same A B; then …@ と書くと 2 / 3 が «違う» に化けて、**一度も比較せずに通る**。
#     ⇒ 必ず rc を受けて 0 / 1 / それ以外 の 3 分岐にする。

file_same()
{
	# ★ 先に「読めるか」を見る — cmp は「無い」と「手段が無い」を同じ 2 で返すので分けられない。
	{ [ -r "$1" ] && [ -r "$2" ] ; } || return 3
	if command -v cmp >/dev/null 2>&1; then
		cmp -s "$1" "$2"; return $?
	fi
	# cmp が無い環境 (MinGW) 用の代替。中身の同一性だけを見る点で cmp と等価。
	if command -v sha256sum >/dev/null 2>&1; then
		_a=`sha256sum < "$1" 2>/dev/null` || return 2
		_b=`sha256sum < "$2" 2>/dev/null` || return 2
		[ "$_a" = "$_b" ]; return $?
	fi
	if command -v md5sum >/dev/null 2>&1; then
		_a=`md5sum < "$1" 2>/dev/null` || return 2
		_b=`md5sum < "$2" 2>/dev/null` || return 2
		[ "$_a" = "$_b" ]; return $?
	fi
	return 2
}

# 相違の中身を見せる。diff が無ければ od で先頭だけ並べる (無いより良い)。
file_show_diff()
{
	if command -v diff >/dev/null 2>&1; then
		diff "$1" "$2"
	else
		echo "  (diff が無いので先頭 8 行ずつ)"
		echo "  --- $1"; head -8 "$1" 2>/dev/null | sed 's/^/    /'
		echo "  --- $2"; head -8 "$2" 2>/dev/null | sed 's/^/    /'
	fi
}

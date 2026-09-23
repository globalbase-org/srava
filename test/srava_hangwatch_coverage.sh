#!/bin/sh
# ★★ #3522: **番犬の歯抜けを機械で数える**。$1 = test ディレクトリ。
#
# 番犬 (test/srava_hangwatch.sh) は 20 秒 / 30 秒の TIMEOUT を捕まえてスタックを
# /tmp/srava-hang/ へ撮るが、各テストが 1 行 source しないとカバーされない。
# ⚠ 入れ忘れは **目視では見えない**。実際 2026-09-14 に 47 本中 7 本が漏れていた
#   (番犬は bench 側・漏れた 7 本は dev-macmini-1 側で独立に書かれ、合流時に約束が落ちた)。
# ⇒ 「表にして数えるまで見えない」歯抜けは **数える検査を置く**。
#   同型の先例: srava_openvdb_guard が CMake の if() の外に居た件 (c542192)。
#
# ★ 対象は test/srava_*.sh のうち **テストとして起動されるもの全部**。テストとして登録
#   されているかどうかは見ない — 登録されていない時期があっても、書いた時点で入っているのが正しい。
#
# ⚠⚠ **除外は名前の列挙ではなく構造で切る** (2026-09-15)。source 専用の共通部品
#   (srava_hangwatch.sh / srava_count_agents.sh) は **shebang を持たない** のでそれで判別する。
#   名前を並べる方式だと、共通部品を 1 本増やすたびにこの検査が落ちる
#   (srava_count_agents.sh を足して実際に落ちた)。⇒ 部品は shebang を付けないこと。
D="${1:?test dir not given}"
n=0
miss=""
for f in "$D"/srava_*.sh; do
	b=$(basename "$f")
	# source 専用の共通部品 (shebang 無し) は対象外 — 単独では走らない。
	case "$(head -1 "$f")" in '#!'*) ;; *) continue ;; esac
	[ "$b" = "srava_hangwatch_coverage.sh" ] && continue
	n=$((n+1))
	grep -q 'srava_hangwatch\.sh' "$f" || miss="$miss $b"
done
if [ -n "$miss" ]; then
	echo "FAIL: 番犬を source していないテストがある:$miss"
	echo "  直し方: 引数を読み終えた直後へ 1 行入れる"
	echo '    . "$(dirname "$0")/srava_hangwatch.sh"'
	exit 1
fi
echo "HWCOV-OK $n scripts"

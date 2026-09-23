#!/bin/sh
# ★★ #3530: **segs / n の検査の歯抜けを機械で数える**。$1 = リポジトリのルート。
#
# #3530 の本体は「同じ `if ( segs < 3 )` を実装ごとに逆の意味で読んでいた」こと。共通検査
# (src/h/common/segs.h) を 1 本置いても、**新しい op を足した人がそこを通し忘れたら元に戻る**。
# ⚠ しかも戻ったことは目視では見えない — 起票時に 4 通りの規約が併存していたのが証拠。
# ⇒ 「表にして数えるまで見えない」歯抜けは **数える検査を置く** (srava_hangwatch_coverage と同型)。
#
# 見るのは 4 つ:
#   ① 検査を通している op が、回帰 (srava_segs.sh) の表に載っているか
#   ② ある op を検査しているカーネルがあるのに、**同じ op を持つ別のカーネルが検査していない**
#      ★ これがいちばん起きる (#3516 の歯抜けと同型・配り忘れ)
#   ③ args から seg / segs / nseg を読んでいるのに共通検査を呼んでいない実装が無いか
#   ④ 共通検査を呼ぶ実装が segs.h を **include** しているか
#      ⚠ ④ は実際に踏んだ: include 済みかの判定を「ファイル中に common/segs.h という文字列が
#         あるか」で書いたら、直前に挿入した **コメントの中の文字列**にマッチして 34 本が
#         素通りした (= 代理を見ると黙って誤答する型)。⇒ 判定は #include 行そのもので行う。
R="${1:?repo root not given}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。⚠ この検査は srava を起動しない (grep するだけ) が、
#   番犬の coverage は **例外を作らない**方針 — 「登録されているか」「起動するか」で除外を
#   始めると、判断が要る条件が増えて歯抜けが戻る。⇒ 無条件に 1 行入れる。
. "$(dirname "$0")/srava_hangwatch.sh"
cd "$R" || exit 1
fail=0
n=0

# ---- 検査を通している op の一覧 (実装から取る = 実体) ----
segs_ops=$(grep -rhoE 'srava_geo::segs_error\("[a-z_]+"\)' modules/ | sed 's/.*("\(.*\)")/\1/' | sort -u)
n_ops=$(grep -rhoE 'srava_geo::sides_error\("[a-z_]+"\)' modules/ | sed 's/.*("\(.*\)")/\1/' | sort -u)

# ---- ① 回帰の表に載っているか ----
for op in $segs_ops; do
	grep -qE "^	$op\)" test/srava_segs.sh || {
		echo "FAIL: op '$op' は共通検査を通しているが test/srava_segs.sh の expr_for に無い"
		echo "  直し方: expr_for() へ 1 行足す —   $op)   echo \"volume($op(...\$2\$TAIL))\";;"
		fail=1; }
	n=$((n+1))
done
for op in $n_ops; do
	grep -qE "^	$op\)" test/srava_segs.sh || {
		echo "FAIL: op '$op' は共通検査を通しているが test/srava_segs.sh の nexpr_for に無い"; fail=1; }
	n=$((n+1))
done

# ---- ② 同じ op を持つのに検査していないカーネルが無いか ----
#   op テーブル (*tsAgent.cpp) の行から OPWIRE(実装名) を拾い、その実装が検査を呼ぶか見る
for tbl in modules/*/c++/*tsAgent.cpp; do
	dir=$(dirname "$tbl")
	for op in $segs_ops $n_ops; do
		impl=$(sed -n "s/^[[:space:]]*{[[:space:]]*\"$op\",.*OPWIRE(\([A-Za-z0-9_]*\).*/\1/p" "$tbl" | head -1)
		[ -n "$impl" ] || continue                 # このカーネルはこの op を持たない
		f="$dir/$impl.cpp"
		[ -f "$f" ] || { echo "FAIL: $tbl の op '$op' の実装 $f が見つからない"; fail=1; continue; }
		# ---- ★★ #3570 段3: **その引数を取らないカーネルは対象外** ----
		#   ② の前提は「同じ op を持つ *だけでなく* 同じ引数を取る」こと。occt は解析曲面
		#   なので segs という引数そのものを記述子から撤去した ⇒ 検査する対象が存在しない。
		#   ⚠ これは「検査をサボっている」ではない。現に srava_segs_occt が
		#     **渡すとエラーになること**を見ている (HONORS=none)。
		#
		#   ★ 除外を **手で並べない** — 「segs が何番目の引数か」は srava_segs.sh の
		#     expr_for から導ける (sphere(1$2) なら 2 番目・cylinder(1,1$2) なら 3 番目)。
		#     記述子の nin がその位置に届かなければ、このカーネルはその引数を持たない。
		#     ⇒ 表が 1 つ増えない = 新しい op を足しても両方を直す必要が無い。
		segpos=$(awk -v op="$op" '
			$0 ~ ("^\t" op "\\)") {
				line = $0
				i = index(line, op "(")
				if (i == 0) next
				rest = substr(line, i + length(op) + 1)
				d = 0; nc = 0
				for (k = 1; k <= length(rest); k++) {
					c = substr(rest, k, 1)
					if (c == "(" || c == "[") d++
					else if (c == ")" || c == "]") { if (d == 0) break; d-- }
					else if (c == "$") break        # ★ $2 = segs の位置に着いた
					else if (c == "," && d == 0) nc++
				}
				#   nc+1 = **$2 の前に在る固定引数の数**。segs はその次 ⇒ nc+2 番目。
				print nc + 2; exit
			}' test/srava_segs.sh)
		nin=$(sed -n "s/^[[:space:]]*{[[:space:]]*\"$op\",[[:space:]]*[A-Za-z0-9_]*,[[:space:]]*\([0-9][0-9]*\),.*/\1/p" "$tbl" | head -1)
		if [ -n "$segpos" ] && [ -n "$nin" ] && [ "$nin" -lt "$segpos" ]; then
			n=$((n+1))
			continue                      # このカーネルはその引数を取らない
		fi
		# ---- ★ 例外: **同じ op 名で引数の意味が違う**もの ----
		#   openvdb の sphere は sphere(r, dx) — 第 2 引数は分割数ではなく **ボクセルサイズ**
		#   (ひさ指示 2026-08-31・voxelize(mesh,dx) の書き方を踏襲)。ボクセル表現に分割数は
		#   意味を持たないので segs 自体が無い。⇒ 検査する対象が存在しない。
		#   ⚠ 例外は **理由つきでここに 1 行**。理由の無い除外を足すと、この検査が
		#     「歯抜けを数える」ものから「歯抜けを隠す」ものに変わる。
		case "$f/$op" in
			*/vdaSphere.cpp/sphere) continue;;
		esac
		grep -q 'srava_geo::check_segs\|srava_geo::check_sides' "$f" || {
			echo "FAIL: $f ($op) が共通検査を呼んでいない"
			echo "  ⚠ 同じ op を他のカーネルは検査している ⇒ **同じ式がカーネルによって落ちたり通ったり**する"
			echo "  直し方: src/h/common/segs.h の check_segs() / check_sides() を通す"
			fail=1; }
		n=$((n+1))
	done
done

# ---- ③ seg を args から読んでいるのに検査していない実装 ----
for f in $(grep -rlE '(int[[:space:]]+(seg|segs|nseg)(_in)?[[:space:]]*=[[:space:]]*\([[:space:]]*na[[:space:]]*>)' modules/*/c++/*.cpp); do
	grep -q 'srava_geo::check_segs' "$f" || {
		echo "FAIL: $f は args から分割数を読んでいるが共通検査を呼んでいない"
		echo "  ⇒ #3530 の食い違い (0 が既定値か 3 かがカーネルで違う) がここから戻る"
		fail=1; }
	n=$((n+1))
done

# ---- ④ 呼ぶなら include していること (★ 判定は #include 行そのもの) ----
for f in $(grep -rl 'srava_geo::check_segs\|srava_geo::check_sides' modules/*/c++/*.cpp); do
	grep -qE '^#include[[:space:]]+"common/segs\.h"' "$f" || {
		echo "FAIL: $f は共通検査を呼ぶが #include \"common/segs.h\" が無い"; fail=1; }
	n=$((n+1))
done

[ "$fail" = "0" ] || { echo "FAIL: 上記のとおり"; exit 1; }
echo "SEGSCOV-OK $n checks (segs ops: $(echo $segs_ops | tr '\n' ' ')/ n ops: $(echo $n_ops | tr '\n' ' '))"

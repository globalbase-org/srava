#!/bin/sh
# 正規分布の擬似乱数 rand_gaussian の回帰 (#3576 ・ 2026-09-22)。
#
# $1 = srava 実行体 / env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- 何を検定しているか ------------------------------------------------------------
# ① 行の選び方  第 1 引数の形 (スカラ / 2 要素 / 3 要素) が軸数を決める。出力型がその証拠。
#               ★ 仕掛けは #3572 の rand と同じ (pt_match_rand_dim が sig の出力型から引く)。
# ② 決定性      同じ引数を **別のキャッシュ dir で 2 回**。★ 同じ dir では HIT で必ず一致し検定にならない。
# ③ 黄金値      ハードコードした期待列。★★ これが **OS と標準ライブラリをまたぐ**唯一の検定。
#               ⚠⚠ 正規乱数は対数が要るが `std::log` は規格が正しい丸めを要求していない。
#                 ⇒ 自前の pt_log_det を使っている。ここが機に依っていれば **この行が落ちる**。
# ④ 分布        標本 2000 で 平均 / 標準偏差 / ±1σ ±2σ の割合 が理論値に寄ること。
#               ⚠ 8000 にすると **値配列の経路**が間欠的に parse error を出す (別件)。
#               ★ 許容幅は標本誤差から決める (きつすぎると偶然で落ちる)。
# ⑤ sigma は **各軸の**標準偏差   ⇒ 3D の centroid が中心へ寄る。⚠ 「距離の標準偏差」ではない。
# ⑥ 退化と明示エラー  sigma=0 は全点が中心 / sigma<0 ・ 要素数 1,4 ・ シード省略 ・ n が浮動 はエラー。
# ⑦ 負の対照   シードを変えたら違う / 中心を変えたら違う。
# ⑧ 混合分布   点群を中心の集合として **重ね合わせた分布**から n 点 (#3577)。
#              ★ 「各入力点に中心版を施したもの」とは違う ⇒ 中心 2 つで **ほぼ半々**が証拠。
#              ⚠ 点数は n (K と無関係) / 入力の順序が効く / 空はエラー
#              ⚠ K=1 でも **中心版とは列が違う** (中心のくじを必ず引く)。分布は同じ。
#              ⚠ ①〜⑤ だけだと「常に同じ定数を返す実装」でも緑になる。
LC_ALL=C
export LC_ALL
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

run() {   # run <cache-suffix> <source>  → 出力全部
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"points.so\",{});
$2" "$SRAVA" 2>&1
}
v() {     # v <cache-suffix> <source>  → "V " 行の中身だけ (空白は落とす)
	# ⚠⚠ エラーは "V " で始まらないので、素の sed だと **空文字列**になり
	#   「値が一致するか」が *空 = 空* で通る = **壊れているのに緑**。
	#   ⇒ #3572 で実際に 2 件踏んだ形なので、最初からここで止める (srava_rand.sh と同じ番)。
	_out=$(run "$1" "$2")
	case "$_out" in
	*"*** ERROR"*)
		# ⚠⚠ **印にどの検定かを書く**。stderr だけに出すと、v が `$( )` の中の
		#   パイプラインで呼ばれた場合に文言が拾えず、末尾で「どれか分からない赤」になる
		#   (2026-09-22 に実際に踏んだ — 印だけ立って理由が消えた)。
		#   ⇒ 印そのものを読めば分かるようにする。
		{ echo "GAUSS_FAIL: v $1 がエラーになった (検定が空振りするので止める):"
		  echo "$_out" ; } >> "$D-verror"
		echo "GAUSS_FAIL: v $1 がエラーになった (詳細は $D-verror)" >&2 ;;
	esac
	echo "$_out" | sed -n 's/^V //p' | tr -d ' '
}
# ⚠ 前の走の印が残っていると **理由のない赤**になる (D を使い回したとき)。
rm -f "$D-verror"
n=0
chk() {   # chk <name> <got> <expected>
	[ "$2" = "$3" ] || { echo "GAUSS_FAIL: $1 が [$2] (期待 [$3])"; exit 1; }
	n=$((n+1))
}
chk_ne() {
	[ "$2" != "$3" ] || { echo "GAUSS_FAIL: $1 が [$2] と一致してしまった (違うはず)"; exit 1; }
	n=$((n+1))
}
chk_err() {   # chk_err <name> <出力> <期待する部分文字列>
	case "$2" in
	*"$3"*) n=$((n+1)) ;;
	*) echo "GAUSS_FAIL: $1 がエラーにならない/文言が違う: $2" ; exit 1 ;;
	esac
}

# ---- ① 行の選び方 — 第 1 引数の形が軸数を決める ------------------------------------
chk "スカラ → 値の配列"   "$(v r1 'print("V", type_of(rand_gaussian(0, 1, 4, 1)));')"          "value"
chk "2 要素 → pt-cloud2d" "$(v r2 'print("V", type_of(rand_gaussian([0,0], 1, 4, 1)));')"      "pt-cloud2d"
chk "3 要素 → pt-cloud3d" "$(v r3 'print("V", type_of(rand_gaussian([0,0,0], 1, 4, 1)));')"    "pt-cloud3d"
chk "点数は n"            "$(v r4 'print("V", nverts(rand_gaussian([0,0,0], 1, 137, 2)));')"   "137"
# ★ 第 1 引数が **上流 op の結果**でも待って読んで正しい行へ行く (cold/warm で割れない)。
chk "第 1 引数が上流の値" \
	"$(v r5 'print("V", type_of(rand_gaussian(rand(0,0,2,7), 1, 4, 1)));')"                "pt-cloud2d"
echo "  1) 第 1 引数の形が軸数を決める (5 件)"

# ---- ② 決定性 (別キャッシュ dir で 2 回) --------------------------------------------
A=$(v d1 'print("V", rand_gaussian(0, 1, 6, 12345));')
B=$(v d2 'print("V", rand_gaussian(0, 1, 6, 12345));')
chk "同じ引数は同じ結果 (値)" "$A" "$B"
PA=$(v d3 'print("V", vert(rand_gaussian([0.0,0.0,0.0], 1.0, 4, 999), 2));')
PB=$(v d4 'print("V", vert(rand_gaussian([0.0,0.0,0.0], 1.0, 4, 999), 2));')
chk "同じ引数は同じ点群"      "$PA" "$PB"
echo "  2) 決定性 (別 dir で 2 回・値と点群)"

# ---- ③ ★★ 黄金値 — OS と標準ライブラリをまたぐ唯一の検定 --------------------------
# ⚠⚠ ここが落ちたら容疑者は **2 つ**ある。順に潰すこと:
#   ① **FMA 融合** (2026-09-22 に実際に踏んだ・bench)。@a*b+c@ を 1 命令に融合すると
#      丸めが 2 回 → 1 回に減って値が変わる。融合するかは *コンパイラと -march の裁量*
#      なので、**同じソースが機で違う値を出す**。⇒ CMakeLists が ptRandom.cpp にだけ
#      @-ffp-contract=off@ を付けてある。**その行が消えていないか先に見る**。
#      ★ 実測: Linux で @-mfma -ffp-contract=fast@ を付けると mac (arm64/clang) の値が
#        そのまま再現した ⇒ 機の差ではなく融合の差と確定した。
#      ⚠⚠ ソースを自前にしても塞がらない穴 — pt_log_det が 13 か所 / next_gauss が 1 か所 /
#        next_flt が 1 か所 融合されていた。「libm を避けた」だけでは足りない。
#   ② それでも違うなら **自前の対数 pt_log_det** が可搬でない。
#   ★ sqrt は IEEE 754 が正しい丸めを要求しているので容疑者ではない。
# ★ 期待列は **融合なし**の値 = 両機が出せる側。⚠ 融合ありの値へ書き換えないこと
#   (片方の機でしか出せない値を黄金値にすると、もう片方が永久に赤になる)。
chk "黄金値 (値・center=0 sigma=1 seed=42)" \
	"$(v g1 'print("V", rand_gaussian(0, 1, 6, 42));')" \
	"[-0.72621913824478568,0.22162270150359337,0.46417731016247366,1.4762494610184149,1.0078198992420604,1.0272109607595328]"
echo "  3) 黄金値 (機と標準ライブラリをまたぐ)"

# ---- ④ 分布 — 平均 / 標準偏差 / ±1σ ±2σ ---------------------------------------------
# ★ n=2000 ・ center=5 ・ sigma=2。許容は標本誤差から: 平均の標準誤差 = 2/√2000 ≈ 0.0447。
#   ⇒ 平均は ±4 標準誤差 (0.18) ・ 標準偏差は ±0.15 ・ 割合は ±3 ポイント で見る。
# ⚠⚠ **8000 にしない**。値配列で 8000 個を運ぶと *間欠的に* こうなる (2026-09-22 実測・6 走中 2 回):
#     *** ERROR[<source>,1] parse error near '[' ... [8.7703725064880835,4.3532141660511705,...
#   ⇒ 大きな値配列の経路そのものの問題で、rand_gaussian の側ではない
#     (点群を返す形では起きない)。⚠ 別件として記録済み。
#   ★ 検定としては 2000 で足りる — 許容幅を標本誤差から決め直せばよいだけ。
v g2 'print("V", rand_gaussian(5.0, 2.0, 2000, 20260922));' | tr -d '[]' | tr ',' '\n' > "$D-samp"
awk '
	{ x[NR] = $1; s += $1 }
	END {
		N = NR
		if (N != 2000) { print "GAUSS_FAIL: 標本が " N " 個 (2000 のはず)"; exit 1 }
		m = s / N
		for (i = 1; i <= N; ++i) { d = x[i] - m; q += d * d }
		sd = sqrt(q / (N - 1))
		for (i = 1; i <= N; ++i) {
			a = x[i] - 5.0; if (a < 0) a = -a
			if (a <= 2.0) w1++
			if (a <= 4.0) w2++
		}
		w1 = w1 * 100.0 / N;  w2 = w2 * 100.0 / N
		bad = 0
		if (m - 5.0 >  0.18 || m - 5.0 < -0.18) { print "GAUSS_FAIL: 平均 " m " (期待 5±0.18)"; bad = 1 }
		if (sd - 2.0 >  0.15 || sd - 2.0 < -0.15) { print "GAUSS_FAIL: 標準偏差 " sd " (期待 2±0.15)"; bad = 1 }
		if (w1 - 68.27 >  3.0 || w1 - 68.27 < -3.0) { print "GAUSS_FAIL: ±1σ 内 " w1 "% (理論 68.27)"; bad = 1 }
		if (w2 - 95.45 >  2.0 || w2 - 95.45 < -2.0) { print "GAUSS_FAIL: ±2σ 内 " w2 "% (理論 95.45)"; bad = 1 }
		printf "  4) 分布: 平均 %.4f / 標準偏差 %.4f / ±1σ %.2f%% / ±2σ %.2f%%\n", m, sd, w1, w2
		exit bad
	}' "$D-samp" || exit 1
n=$((n+4))
rm -f "$D-samp"

# ---- ⑤ sigma は **各軸の**標準偏差 ---------------------------------------------------
# ★ 中心を軸ごとに離して置き、centroid が各軸の中心へ寄ることを見る。
#   ⚠ 「距離の標準偏差」だと解釈していたら、この検定は通るが ④ の標準偏差が合わなくなる。
echo "$(v g3 'print("V", centroid(rand_gaussian([10,-5,100], 1.0, 20000, 3)));')" | \
	tr -d '[]' | tr ',' '\n' | awk '
	NR==1 { c[1] = $1 } NR==2 { c[2] = $1 } NR==3 { c[3] = $1 }
	END {
		want[1] = 10; want[2] = -5; want[3] = 100
		# 標準誤差 = 1/√20000 ≈ 0.00707 ⇒ ±5 標準誤差 = 0.036 で見る
		for (i = 1; i <= 3; ++i) {
			d = c[i] - want[i]; if (d < 0) d = -d
			if (d > 0.036) { print "GAUSS_FAIL: 軸 " i-1 " の centroid " c[i] " (期待 " want[i] ")"; bad = 1 }
		}
		exit bad
	}' || exit 1
n=$((n+1))
echo "  5) sigma は各軸の標準偏差 (3D の centroid が各軸の中心へ)"

# ---- ⑥ 退化と明示エラー --------------------------------------------------------------
# sigma=0 は **全点が中心**。退化だが定義でき、σ→0 の極限と一致するので通す。
chk "sigma=0 は全点が中心" "$(v e0 'print("V", bbox(rand_gaussian([1,2,3], 0, 10, 1)));')" \
	"[[1,2,3],[1,2,3]]"
chk_err "sigma が負"     "$(run e1 'print("V", rand_gaussian(0, -1, 3, 1));')"  "sigma must not be negative"
chk_err "要素数 1"       "$(run e2 'print("V", rand_gaussian([0], 1, 3, 1));')" \
                         "an array of 2 or 3 numbers (one per axis), but it is an array of 1"
chk_err "要素数 4"       "$(run e3 'print("V", rand_gaussian([0,0,0,0], 1, 3, 1));')" "but it is an array of 4"
# ★★★ シードは省略できない (rand と同じ設計)。3 引数は **routing が弾く**。
chk_err "シード省略"     "$(run e4 'print("V", rand_gaussian(0, 1, 3));')" \
                         "op 'rand_gaussian' — no candidate takes 3 argument(s) (points: takes 4)"
chk_err "n が浮動"       "$(run e5 'print("V", rand_gaussian(0, 1, 3.0, 1));')" "n must be an integer"
chk_err "シードが浮動"   "$(run e6 'print("V", rand_gaussian(0, 1, 3, 1.5));')" "seed must be an integer"
chk_err "n が負"         "$(run e7 'print("V", rand_gaussian(0, 1, -1, 1));')"  "must not be negative"
chk_err "sigma が文字列" "$(run e8 'print("V", rand_gaussian(0, "x", 3, 1));')" "sigma must be a number"
chk_err "中心が文字列"   "$(run e9 'print("V", rand_gaussian("x", 1, 3, 1));')" "center must be a number"
chk "n=0 は空配列"       "$(v e10 'print("V", rand_gaussian(0, 1, 0, 1));')"    "[]"
echo "  6) 退化 (sigma=0 / n=0) と明示エラー (9 件)"

# ---- ⑦ 負の対照 ----------------------------------------------------------------------
# ⚠ ①〜⑤ だけなら「常に同じ定数を返す実装」でも緑になる。
S1=$(v n1 'print("V", rand_gaussian(0, 1, 4, 1));')
S2=$(v n2 'print("V", rand_gaussian(0, 1, 4, 2));')
chk_ne "シードを変えたら違う" "$S1" "$S2"
C1=$(v n3 'print("V", rand_gaussian(0, 1, 4, 1));')
C2=$(v n4 'print("V", rand_gaussian(100, 1, 4, 1));')
chk_ne "中心を変えたら違う"   "$C1" "$C2"
G1=$(v n5 'print("V", rand_gaussian(0, 1, 4, 1));')
G2=$(v n6 'print("V", rand(0, 1, 4, 1));')
chk_ne "一様 rand とは違う列" "$G1" "$G2"
echo "  7) 負の対照: 種 / 中心 / 一様版 と違う (3 件)"

# ---- ⑧ 混合分布 (#3577) — 点群を中心の集合として重ね合わせる --------------------------
# ★★ 「各入力点に中心版を施したもの」とは **違う**。重ね合わせた分布から n 点を引く。
chk "3D 混合 → pt-cloud3d" \
	"$(v m1 'var c = points3d([[0,0,0],[10,0,0]]); print("V", type_of(rand_gaussian(c, 1.0, 8, 5)));')" \
	"pt-cloud3d"
chk "2D 混合 → pt-cloud2d" \
	"$(v m2 'var c = points2d([[0,0],[10,0]]); print("V", type_of(rand_gaussian(c, 1.0, 8, 5)));')" \
	"pt-cloud2d"
# ★ 点数は **n**。入力の K とは無関係 (「各点に n 点ずつ」でも「n/K 点ずつ」でもない)。
chk "点数は n (K と無関係)" \
	"$(v m3 'var c = points3d([[0,0,0],[1,1,1],[2,2,2],[3,3,3],[4,4,4]]);
print("V", nverts(rand_gaussian(c, 0.1, 7, 1)));')" "7"
# ★★ 混合であることの直接の証拠: 十分離れた中心 2 つに **ほぼ半々**で分かれる。
#   ★ 数えるのに **centroid を使う** — 中心が 0 と 100 なら centroid.x = 100 * (右の割合)。
#     ⚠ srava の for で 2000 点を vert() で舐めると **48.6 秒**かかる (実測)。centroid は 0.05 秒。
#       ⇒ 同じ情報が O(1) で取れるときにループを書かない (番犬の予算を無駄に食う)。
#   ⚠ 許容は二項分布から: hi ~ B(2000, 0.5) の標準偏差 22.4 ⇒ 割合の sd = 0.0112
#     ⇒ centroid.x の sd = 1.12 ⇒ ±5σ = 5.6 で見る。
echo "$(v m4 'var c = points3d([[0,0,0],[100,0,0]]);
print("V", centroid(rand_gaussian(c, 1.0, 2000, 11)));')" | tr -d '[]' | tr ',' '\n' | awk '
	NR==1 { cx = $1 + 0 }
	END {
		if (cx < 50 - 5.6 || cx > 50 + 5.6) {
			printf "GAUSS_FAIL: 混合が半々にならない (centroid.x %.2f / 期待 50±5.6)\n", cx; exit 1
		}
		printf "  8) 混合分布: 中心 2 つに %.0f / %.0f 点で分かれた (centroid.x %.2f)\n", \
		       2000 * (1 - cx / 100), 2000 * cx / 100, cx
	}' || exit 1
n=$((n+1))
# ⚠ **入力点群の順序が結果に効く** (順序は #3527 で「格納順 = 入力の順」と定義済み)。
#   ★ sigma を極小にすると、引いた点がどの中心の近くかで順序の影響が目に見える。
O1=$(v m5 'var c = points3d([[0,0,0],[100,0,0]]); print("V", vert(rand_gaussian(c,0.001,3,9),0));')
O2=$(v m6 'var c = points3d([[100,0,0],[0,0,0]]); print("V", vert(rand_gaussian(c,0.001,3,9),0));')
chk_ne "入力の順序が結果に効く" "$O1" "$O2"
# ⚠ 入力が空 (K=0) は **明示エラー** — 中心が無いと分布が定義できない。
chk_err "空の点群はエラー" \
	"$(run m7 'var c = points3d([]); print("V", nverts(rand_gaussian(c, 1.0, 4, 1)));')" \
	"there is no center to draw from"
# ★★ K=1 でも **中心版とは列が違う** (中心のくじを必ず 1 回引くため)。
#   ⚠ これは意図した形。⇒ 消費順の規約を「K で条件分け」しないため
#     (一様版 rand も退化した区間で同じように消費する)。分布は同じ。
K1=$(v m8 'var c = points3d([[0,0,0]]); print("V", vert(rand_gaussian(c, 1.0, 3, 5), 0));')
C1=$(v m9 'print("V", vert(rand_gaussian([0,0,0], 1.0, 3, 5), 0));')
chk_ne "K=1 でも中心版とは列が違う (くじを引くため)" "$K1" "$C1"
#   ★ ただし **分布は一致する**: K=1 の大標本で centroid が中心へ寄る。
echo "$(v m10 'var c = points3d([[7,-3,2]]);
print("V", centroid(rand_gaussian(c, 1.0, 20000, 4)));')" | tr -d '[]' | tr ',' '\n' | awk '
	NR==1 { c[1] = $1 } NR==2 { c[2] = $1 } NR==3 { c[3] = $1 }
	END {
		want[1] = 7; want[2] = -3; want[3] = 2
		for (i = 1; i <= 3; ++i) {
			d = c[i] - want[i]; if (d < 0) d = -d
			if (d > 0.036) { print "GAUSS_FAIL: K=1 の軸 " i-1 " の centroid " c[i] " (期待 " want[i] ")"; bad = 1 }
		}
		exit bad
	}' || exit 1
n=$((n+1))
echo "  9) 混合: 順序が効く / 空はエラー / K=1 は列は違うが分布は同じ (4 件)"

# ★★ v() がエラーを飲み込んだ検定が 1 つでもあれば赤にする (v() の番の受け)。
if [ -f "$D-verror" ]; then
	echo "GAUSS_FAIL: v がエラーを返した検定がある:"
	cat "$D-verror"
	rm -f "$D-verror"
	exit 1
fi
echo "GAUSS_OK ($n checks)"

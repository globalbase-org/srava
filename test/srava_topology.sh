#!/bin/sh
# 位相を **直接** 数える op (nshells / nparts / genus) の回帰 (#3514)。
#
# $1 = srava 実行体 / $2 = モジュール名 (.so) / $3 = 球の分割数
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- 何を固定するのか ------------------------------------------------------------
# ★ 3 つの数はそれぞれ **違う問い**に答える。取り違えると「同じ op がカーネルごとに別の数を
#   答える」ことになるので、区別が出る形 (中空の箱・入れ子シェル) を必ず入れる:
#
#     nshells(m)  境界シェル = 面の連結成分の枚数    球 1 ・中空の箱 2 ・入れ子 N 球 N
#     nparts(m)   塊 = 立体の連結成分の数            球 1 ・中空の箱 1 ・入れ子 N 球 N/2
#     genus(m)    種数 = 取っ手の総数                球 0 ・トーラス 1 ・中空の箱 0
#
# ★ 期待値はすべて **閉形式で手で書ける整数**。カーネル同士の合議ではない (#3470 の教訓)。
# ★ nparts は **nef の nparts と同じ約束** (SNC の marked volume = 塊)。同じ入力に対して
#   nef / cgal / manifold / geogram が同じ数を返すことまで固定したいが、この本は 1 カーネルずつ
#   走らせる形なので、各本が同じ期待値表を見ることで揃えている。
#
# ---- ⚠ 「xor の N 球 = 成分 N/2」が成り立つのは **同心** のときだけ ----------------
# ⚠⚠ #3514 の起票時の検証案は「xor の N 球 → 成分 N/2」だったが、掃引の模型
#   (R=1.5 の球を線分 (0,0,0)-(1,1,1) 上に N 個) では **成分数は 1 になる**。
#   xor の破片どうしは球面の交線の円で **接している** (N=2 の三日月 2 つが交線の円だけで
#   接するのと同じ形 = #3490 の症例) ので、面の連結成分としては 1 枚に繋がっており、
#   しかも非多様体なので genus は定義できない (valid も 0)。実測で中心の間隔を不等にしても
#   変わらない ⇒ 対称性ではなく **xor の構造そのもの**。
#   ⇒ 位相で数えたいなら **同心の球**を使う。こちらなら破片は本当に離れていて
#     nshells=N ・ nparts=N/2 ・ genus=0 ・ valid=1 が出る (このテストが固定しているのはこれ)。
SRAVA="$1"
SO="${2:?module .so not given}"
SEG="${3:-64}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"

# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

run() {   # run <cache-suffix> <source>  → VAL 行だけ拾う
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
$2" "$SRAVA" 2>&1
}
n=0
chk() {   # chk <name> <got> <expected>   (整数の完全一致)
	[ "$2" = "$3" ] || { echo "FAIL: $1 が $2 (期待 $3)"; exit 1; }
	n=$((n+1))
}

# ---- ① 単一の立体: 球 / 箱 / トーラス -------------------------------------------
#   ★ トーラスが **genus を体積から区別する**唯一の形。ここが 0 になると
#     「Euler 標数を数えていない (面数だけ見ている)」たぐいの壊れ方が素通りする。
O=$(run t1 "
var S = sphere(1,$SEG);
var B = box(2,3,4);
var T = torus(3,1,$SEG);
print(\"VAL\", nshells(S)); print(\"VAL\", nparts(S)); print(\"VAL\", genus(S));
print(\"VAL\", nshells(B)); print(\"VAL\", nparts(B)); print(\"VAL\", genus(B));
print(\"VAL\", nshells(T)); print(\"VAL\", nparts(T)); print(\"VAL\", genus(T));")
# ⚠⚠ **値の分割だけ glob を止める** (2026-09-22)。
#   `set -- $(…)` の $( ) は **クォートできない** (単語分割が要る) のでパス名展開に掛かる。
#   比べる値は `[1,1,0]` 形 = glob の **文字クラス**なので、カレント (ビルド dir) に `1` という
#   名前のファイルが 1 つあるだけで `1` に化け、**「値が違う」という、それらしい文言で赤くなる**
#   (2026-09-21 に実際に踏んだ。⚠ PIG_TIMING は *ファイルパス* を取る env なので
#   `PIG_TIMING=1` と打つと `1` というファイルができる)。
#   ★★ **実行と分割を分ける**のが肝。`set -f; set -- $(cmd)` と囲うと -f が $( ) の中まで効き、
#     cmd がシェル関数だと *その中の glob* まで殺す (実測: count_tus → objs_of の
#     `CMakeFiles/*/link.txt` が読めず「全 0 本」になった)。⇒ 先に変数へ取ってから分割する。
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "球 nshells"      "$1" 1
chk "球 nparts"       "$2" 1
chk "球 genus"        "$3" 0
chk "箱 nshells"      "$4" 1
chk "箱 nparts"       "$5" 1
chk "箱 genus"        "$6" 0
chk "トーラス nshells" "$7" 1
chk "トーラス nparts"  "$8" 1
chk "トーラス genus"   "$9" 1

# ---- ② ★★ シェルと塊が **分かれる** 形 ------------------------------------------
#   中空の箱 = 箱から内側の箱をくり抜いたもの。シェル 2 枚だが塊は 1 個。
#   ⇒ ここで nshells と nparts が同じ数を返したら、どちらかが他方の別名になっている。
O=$(run t2 '
var H = box(3,3,3) --- translate(box(1,1,1),[1,1,1]);
var D = box(1,1,1) ||| translate(box(1,1,1),[5,0,0]);
print("VAL", nshells(H)); print("VAL", nparts(H)); print("VAL", genus(H)); print("VAL", volume(H));
print("VAL", nshells(D)); print("VAL", nparts(D)); print("VAL", genus(D)); print("VAL", volume(D));')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "中空の箱 nshells" "$1" 2
chk "中空の箱 nparts"  "$2" 1
chk "中空の箱 genus"   "$3" 0
#   ⚠ 体積は **空洞が残っていること**の裏取り。埋まっていれば 27 になる (occt #3490 の壊れ方)。
ok=$(awk -v v="$4" 'BEGIN{ d=v-26; if(d<0)d=-d; print (d<1e-6)?1:0 }')
chk "中空の箱 volume=26 (空洞が残っている)" "$ok" 1
chk "離れた 2 箱 nshells" "$5" 2
chk "離れた 2 箱 nparts"  "$6" 2
chk "離れた 2 箱 genus"   "$7" 0
ok=$(awk -v v="$8" 'BEGIN{ d=v-2; if(d<0)d=-d; print (d<1e-6)?1:0 }')
chk "離れた 2 箱 volume=2" "$ok" 1

# ---- ③ ★★ 入れ子シェル: 同心の球の xor ------------------------------------------
#   半径 1..N の球を xor すると、被覆数が奇数の殻だけが残る ⇒ **N/2 枚の離れた殻**。
#   シェルは 1 枚の殻につき外側と内側の 2 枚なので N 枚。種数は 0 (どれも球面)。
#   ★ これが #3514 の狙い — 「シェルを 1 枚失えば体積が 100 倍ずれる」という
#     *体積を位相の代理に使う* 判定を、**数そのもの**に置き換える点。
for N in 2 4 6; do
	SRC="var x0 = sphere(1,$SEG);"
	i=1
	while [ $i -lt $N ]; do
		r=$((i+1))
		SRC="$SRC
var a$i = sphere($r,$SEG);
var x$i = (x$((i-1)) --- a$i) ||| (a$i --- x$((i-1)));"
		i=$((i+1))
	done
	L=$((N-1))
	O=$(run "t3-$N" "$SRC
print(\"VAL\", nshells(x$L)); print(\"VAL\", nparts(x$L));
print(\"VAL\", genus(x$L));   print(\"VAL\", valid(x$L));")
	_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
	set -f
	set -- $_SPLIT_
	set +f
	chk "同心 xor N=$N nshells" "$1" "$N"
	chk "同心 xor N=$N nparts"  "$2" "$((N/2))"
	chk "同心 xor N=$N genus"   "$3" 0
	#   ⚠ valid=1 を一緒に見る。0 なら破片が接してしまっており、上の 3 つは
	#     「離れた殻を数えた」ことにならない (下の ④ がその形)。
	chk "同心 xor N=$N valid"   "$4" 1
done

# ---- ④ 閉じていないメッシュでは genus を **断る** --------------------------------
#   ★ chi = 2-2g は閉じた向きづけ可能な曲面でしか成り立たない。非多様体に整数を返すのは
#     「黙って意味の無い値を出す」ことなので、明示エラーにする。
#   ⚠ 模型は掃引の xor そのもの (交線の円で接する三日月 2 つ)。**成分数は 1** で、
#     #3514 の起票時の想定 (N/2) と違う — この本の冒頭の但し書きの根拠。
#   ★★ #3537 (2026-09-15): この @valid=0@ が **どこから来るかが変わった**。
#     以前は self_intersects の *誤検出*で 0 になっていた (厳密な有理数で確かめると、
#     疑われていた三角形対は交差していない = 答えだけ合っていた)。誤検出を止めたところ
#     @is_closed@ が **生の頂点番号**で辺を数えていて継ぎ目を見ていないことが出た。
#     いまは溶接して数えるので、正しい理由 (溶接後に 4 回使われる辺が 108 本 = 非多様体)
#     で 0 になる。⇒ 期待値は同じだが、根拠は入れ替わっている。
XOR='var a = sphere(1.5,32); var b = translate(sphere(1.5,32),[1,1,1]);
     var x = (a --- b) ||| (b --- a);'
O=$(run t4 "$XOR
print(\"VAL\", valid(x)); print(\"VAL\", nshells(x)); print(\"VAL\", nparts(x));")
if echo "$O" | grep -q "boolean failed"; then
	#   ⚠ cgal は **接する立体の融合そのものを断る** (掃引でも N=24 で rc=1)。
	#     位相を数える前に止まるので、このカーネルではこの模型を作れない = 検査しない。
	echo "  note: $SO は接する xor を作れない (boolean failed) ので ④ は飛ばす"
else
	_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
	set -f
	set -- $_SPLIT_
	set +f
	chk "接する xor valid"   "$1" 0
	chk "接する xor nshells" "$2" 1
	chk "接する xor nparts"  "$3" 1
	echo "$(run t5 "$XOR print(\"VAL\", genus(x));")" | grep -q "not a closed 2-manifold" || {
		echo "FAIL: 閉じていないメッシュに genus が答えた"; exit 1; }
	n=$((n+1))
fi

# ---- ⑤ 型: 2D に **曲面の量は無い** / 片の数だけは在る ------------------------------
#   ⚠ 2D 型を持たないカーネル (geogram) では rect そのものが無いので飛ばす。
#
#   ★★ #3525 でここの約束が 1 つ動いた (ひさ判断 2026-09-15):
#     nshells / genus … **2D には無い**。曲面の量 (面の連結成分・取っ手) なので定義できない
#                        ⇒ 従来どおり断る。
#     nparts          … **cgal では 2D も答える**。約束は「*その値が構造として持っている片の数*」で、
#                        3D で既にそうだった (nef = marked volume の数 ⇒ convex_decomposition の
#                        結果は 1 つの連結な立体でも凸片の数 / cgal = 符号つき体積が正のシェルの数
#                        ⇒ 接しているだけの 2 立体は 2)。⇒ 2D をこれに揃えた。
#                        ⚠ 「点集合の連結成分」と読むと *Voronoi のようにセルが互いに接する分割*が
#                          1 になってしまう。既存の約束は元からその読みではなかった。
#     ⇒ 他のカーネルは 2D の nparts を宣言していないので、従来どおり断る (routing が言う)。
O=$(run t6 'print("VAL", nparts(rect(2,3)));')
if echo "$O" | grep -q "op 'rect'"; then
	echo "  note: $SO は 2D 型を持たない (rect が無い) ので ⑤ は飛ばす"
else
	case "$SO" in
	cgal.so|manifold.so)
		# ★★ #3527 段 3: **manifold の 2D も答えるようになった**。以前はここが cgal 専用で、
		#   他カーネルは「断ること」を検定していた (#3525 の時点では 2D の nparts が cgal に
		#   しか無かったため)。素性 op を geomutils へ寄せたときに、mf が持っていなかった
		#   2D の valid / nparts / part / vert / verts を gu が埋めた ⇒ 歯抜けが消えた。
		#   ⚠ 「断る」を検定していた行は **歯抜けを仕様として固定していた**ことになる。
		#     埋めたら更新する — 答えは cgal と一致することを実測で確かめてある (1 と 2)。
		chk "2D の nparts (片は 1 つ)" "$(echo "$O" | sed -n 's/^VAL //p')" 1
		# ★ 片が複数ある 2D でも数が合うこと (離れた 2 枚を combine = 仕切りを実体として持つ値)。
		chk "2D の nparts (片が 2 つ)" \
		    "$(run t6b 'print("VAL", nparts(combine(rect(2,3), translate(rect(2,3),[9,0,0]))));' \
		       | sed -n 's/^VAL //p')" 2
		;;
	*)
		# ⚠ geogram / cherchi は **2D 型そのものを持たない** ので、ここは「型が無い」側。
		echo "$O" | grep -q "no module can execute op 'nparts'\|needs a mesh" || {
			echo "FAIL: nparts が 2D を受けた"; exit 1; }
		n=$((n+1))
		;;
	esac
	# ⚠ nshells / genus は **どのカーネルでも** 2D を受けない (曲面の量なので 2D では定義できない)。
	echo "$(run t6c 'print("VAL", nshells(rect(2,3)));')" \
	    | grep -q "no module can execute op 'nshells'\|needs a mesh" || {
		echo "FAIL: nshells が 2D を受けた"; exit 1; }
	n=$((n+1))
	echo "$(run t6d 'print("VAL", genus(rect(2,3)));')" \
	    | grep -q "no module can execute op 'genus'\|needs a mesh\|not a closed 2-manifold" || {
		echo "FAIL: genus が 2D を受けた"; exit 1; }
	n=$((n+1))
fi

echo "TOPOLOGY-OK $SO ($n checks)"

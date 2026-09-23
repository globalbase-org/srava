#!/bin/sh
# ★★ #3511: **loft_ruled** — 断面の列を **直線で結ぶ** 立体 (メッシュ系カーネル)。
#
# $1 = srava 実行体 / $2 = モジュール名 (module() に渡す .so) / $3 = 許容相対誤差
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- ★★ なぜ occt の srava_occt_loft.sh と別に要るのか ----
# ★ メッシュ系は **@loft@ (なめらか) を持たない** — 解析曲面が要るので線織だけ。
#   ⇒ 期待値の表がそもそも別。「両方あるか」を見る occt の回帰をそのまま当てられない。
# ★ そして **ねじれ (面内回転) の答えが occt と違う** (下の ⑧)。ここを検査に書いておかないと、
#   誰かが「一致させよう」として *細分を黙って足す* 変更を入れたとき、それが正しいのか
#   仕様違反なのか分からなくなる。
#
# ---- ★★ 許容誤差が厳密一致でないのはなぜか ----
# ⚠ manifold の 2D は **cache から decode するとき Clipper2 の整数格子に載る**
#   (mfCross.cpp の decode は CrossSection(ps,NonZero) で作り直す)。⇒ *同じ式でも
#   経路によって断面の座標が下位桁で動く* (area で既にそうなっている)。
#   ⇒ loft の体積も同じ桁で揺れる。**これは loft の欠陥ではなく 2D 型の性質**なので、
#     ここは相対許容で受ける (判定に効く桁は数桁余らせてある)。
#
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
SRAVA="$1"
SO="${2:?module .so not given}"
TOL="${3:-1e-7}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
. "$(dirname "$0")/srava_hangwatch.sh"

run() { rm -rf "$D-$1"; SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
$2" "$SRAVA" 2>&1; }
chk() {
	ok=$(awk -v g="$2" -v e="$3" -v t="$TOL" 'BEGIN{
		d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1; print (g!="" && d/s <= t) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: $1 が $2 (期待 $3)"; exit 1; }
	n=$((n+1))
}
n=0

# ---- ① 閉形式 (32 角形 / 6 角形の多角形面積で立てる) ----
#   ★ 断面は **多角形**なので厳密な円ではない。k(n) = (1/2) n sin(2pi/n) を使う:
#       k(32) = 3.121445152258052   k(6) = 2.598076211353316
#   円柱    k(32) r^2 h                      = k32 * 1 * 4     = 12.485780609032208
#   円錐台  (h/3)(A1 + A2 + sqrt(A1 A2))      = (4/3) k32 * 7   = 29.133488087741817
#   角柱    w d h                             = 2*3*5           = 30
#   3 断面  円錐台 2 つ (1->2 と 2->3・h=2)    = (2/3) k32 * 26  = 54.10504930580623
#   ⚠ 円は **分割数を明示する** (@circle(1,32)@)。@circle(r,0)@ の既定はカーネルで違う
#     (cgal は 3・manifold は 32) ので、省略すると同じスクリプトが別の形を測ることになる。
#   ★★ 3 断面は **半径を 1->2->3 にしてある**。1->2->1 にすると値が円錐台と同じ
#     ((4/3)k*7 = (2/3)k*14) になり、*断面の枚数が効いていなくても通ってしまう*。
O=$(run l1 '
var C = circle(1,32); var R = rect(2,3);
print("VAL", volume(loft_ruled(C, translate(C,[0,0,4]))));
print("VAL", volume(loft_ruled(C, translate(circle(2,32),[0,0,4]))));
print("VAL", volume(loft_ruled(R, translate(R,[0,0,5]))));
print("VAL", volume(loft_ruled(C, translate(circle(2,32),[0,0,2]), translate(circle(3,32),[0,0,4]))));
print("VAL", valid(loft_ruled(R, translate(R,[0,0,5]))));
print("VAL", nfaces(loft_ruled(R, translate(R,[0,0,5]))));
print("VAL", nfaces(loft_ruled(C, translate(circle(2,32),[0,0,2]), translate(circle(3,32),[0,0,4]))));
')
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
chk "円柱 (k32 r^2 h)"            "$1" 12.485780609032208
chk "円錐台 ((h/3)(A1+A2+sqrt))"  "$2" 29.133488087741817
chk "角柱 (2*3*5)"                "$3" 30
chk "3 断面 (円錐台 2 つ)"         "$4" 54.10504930580623
chk "立体として妥当"               "$5" 1
chk "角柱は三角形 12 枚"           "$6" 12
chk "3 断面は三角形 188 枚"        "$7" 188

# ---- ② ★★ occt と **厳密に一致する** (独立実装の一致 = 対応づけの規約の証拠) ----
# ★★ ここが対応づけ (弧長で正規化 + 全断面の頂点の和集合で標本化 + 最近点で始点合わせ) の
#   唯一の外部検証。どちらも厳密な多面体になる組だけを並べてある:
#     ngon6 -> ngon6         occt 10.392304845413264
#     ngon4 -> ngon8         occt  9.6568542494923797  ← ★ **頂点数が違っても**合う
#     rect を x 20 度傾け     occt 26.354492739361916   ← ★ #3526 の枠が効いている
#   ⚠ この 3 つが落ちたら、対応づけを変えた変更が入っている (値を書き換える前に理由を書くこと)。
O=$(run l2 '
var R = rect(2,3); var G = ngon(6,1);
print("VAL", volume(loft_ruled(G, translate(G,[0,0,4]))));
print("VAL", volume(loft_ruled(ngon(4,1), translate(ngon(8,1),[0,0,4]))));
print("VAL", volume(loft_ruled(R, translate(rotate(R,"x",20),[0,0,4]))));
print("VAL", volume(loft_ruled(R, translate(R,[0,0,4]))));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "occt 一致: ngon6->ngon6"        "$1" 10.392304845413264
chk "occt 一致: ngon4->ngon8"        "$2" 9.6568542494923797
chk "occt 一致: rect を x20 傾け"    "$3" 26.354492739361916
chk "傾けない角柱 (2*3*4)"           "$4" 24
# ★ 傾けた値が傾けない値と違うこと = 枠が黙って捨てられていない (空振り検査)
A=$(awk -v a="$3" -v b="$4" 'BEGIN{d=a-b; if(d<0)d=-d; print (d>1) ? 1 : 0}')
[ "$A" = "1" ] || { echo "FAIL: 傾けても値が変わらない (枠が無視されている: $3 / $4)"; exit 1; }
n=$((n+1))

# ---- ③ ⚠⚠ **ねじれ (面内回転) は occt と一致しない** (ひさ判断: そのまま受ける・2026-09-13) ----
# 対応する稜が同一平面に無いと、その四角形は **双線形パッチ (双曲放物面)** になり
# 三角形 2 枚では表せない。⇒ 立場は @circle(r,segs)@ の segs と同じで、細かくしたい人は
# **中間断面を足す** (2 次で収束する)。
#   rect を z10 ねじる   occt 23.878462024097665 / メッシュ 22.373511150984267
#   中間断面 (z5 を挟む)  メッシュ 23.214207814254262  ← occt に近づく
# ⚠ この項は「不一致が **本物である**」ことを固定する。誰かが黙って細分を足したら落ちる。
O=$(run l3 '
var R = rect(2,3);
print("VAL", volume(loft_ruled(R, translate(rotate(R,"z",10),[0,0,4]))));
print("VAL", volume(loft_ruled(R, translate(rotate(R,"z",5),[0,0,2]), translate(rotate(R,"z",10),[0,0,4]))));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "ねじれ z10 (メッシュの値)"      "$1" 22.373511150984267
chk "ねじれ z10 + 中間断面"          "$2" 23.214207814254262
# occt (23.878462024097665) から 1% 以上離れていること = 近似であることが本物
A=$(awk -v a="$1" 'BEGIN{d=a-23.878462024097665; if(d<0)d=-d; print (d/23.878462024097665 > 0.01) ? 1 : 0}')
[ "$A" = "1" ] || { echo "FAIL: ねじれが occt と一致してしまった ($1)。細分が入ったなら仕様を直すこと"; exit 1; }
# 中間断面を足すと occt に **近づく** こと (刻めば収束する)
A=$(awk -v a="$1" -v b="$2" 'BEGIN{e=23.878462024097665; da=a-e; if(da<0)da=-da; db=b-e; if(db<0)db=-db; print (db<da)?1:0}')
[ "$A" = "1" ] || { echo "FAIL: 中間断面を足しても occt に近づかない ($1 -> $2)"; exit 1; }
n=$((n+2))

# ---- ④ ★ **なめらかな loft は置かない** (メッシュ系には解析曲面が無い) ----
# ⚠ 黙って線織で代用したら「どのカーネルがどちらを持つか」(#3510 の表) が嘘になる。
run l4 'print("VAL", volume(loft(rect(2,3), translate(rect(2,3),[0,0,4]))));' |
	grep -qE "ERROR|error" || {
	echo "FAIL: メッシュ系に loft (なめらか) が在ることになっている"; exit 1; }
n=$((n+1))

# ---- ⑤ ⚠ 退化を **黙って通さない** ----
#   同じ位置の 2 断面 → 体積 0 を返さず明示エラー (#3518 の 5 と同じ網)
#   ★ 文言が **原因を名指しする**こと。後段の「閉じた曲面にならない」でも落ちるが、それでは
#     利用者は「重なっている」と分からない (実測でそうなったので前段の検査を足した)。
run l5 'var R = rect(2,3); print("VAL", volume(loft_ruled(R, R)));' |
	grep -q "at the same place" || {
	echo "FAIL: 同じ位置の断面で体積 0 を黙って返した"; exit 1; }
n=$((n+1))
#   穴あき断面 (輪が 2 本) → どの輪をどの輪につなぐか決まらないので明示エラー
run l6 'var A = difference(rect(4,4), translate(rect(2,2),[1,1,0]));
print("VAL", volume(loft_ruled(A, translate(A,[0,0,4]))));' |
	grep -q "one closed outline per section" || {
	echo "FAIL: 穴あき断面を受けてしまった"; exit 1; }
n=$((n+1))
#   断面 1 枚 → エラー
run l7 'print("VAL", volume(loft_ruled(rect(2,3))));' | grep -qE "ERROR|error" || {
	echo "FAIL: 断面 1 枚を受けてしまった"; exit 1; }
n=$((n+1))

# ---- ⑥ ★ sig の "[]": 配列 1 個でも渡せて、並べた形と **完全に同じ** (#3511) ----
O=$(run l8 '
var A = [rect(2,3), translate(rect(2,3),[0,0,2]), translate(rect(2,3),[0,0,5])];
print("VAL", volume(loft_ruled(A)));
print("VAL", volume(loft_ruled(rect(2,3), translate(rect(2,3),[0,0,2]), translate(rect(2,3),[0,0,5]))));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "配列で loft_ruled" "$1" 30
[ "$1" = "$2" ] || { echo "FAIL: 配列形と並べた形で値が違う ($1 / $2)"; exit 1; }
n=$((n+1))
#   ★ キャッシュキーも一致する = **配列形で作った実体を並べた形が HIT で拾う**
rm -rf "$D-l9"
SRAVA_CACHE_DIR="$D-l9" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
var A = [rect(2,3), translate(rect(2,3),[0,0,2]), translate(rect(2,3),[0,0,5])];
print(\"V\", volume(loft_ruled(A)));" "$SRAVA" >/dev/null 2>&1
H=$(SRAVA_CACHE_DIR="$D-l9" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
print(\"V\", volume(loft_ruled(rect(2,3), translate(rect(2,3),[0,0,2]), translate(rect(2,3),[0,0,5]))));" "$SRAVA" 2>&1 |
	sed -n 's/.*cache: \([0-9]*\) hit(s), \([0-9]*\) miss(es).*/\1 \2/p')
set -- $H
[ "$2" = "0" ] && [ "$1" -gt 0 ] || {
	echo "FAIL: 配列形と並べた形でキャッシュキーが違う (hit=$1 miss=$2)"; exit 1; }
n=$((n+1))

# ---- ⑦ ★★ 入れ子の配列は **1 段しか展開しない** (2026-09-18) ----
#   見た目は「書き間違い (@loft_ruled([[a,b]])@) を拒否する」検定だが、**本当に見ているのは
#   @try_shortcircuit@ が再入可能か**である。
#   ⚠⚠ 配列 1 個の n 項展開 (⑥) は @pigfModuleAgent::try_shortcircuit@ (ACT_START の 1.5) が
#     **args を書き換えて**行う。ところが *この状態は compact ゲートの yield で頭から再走する*
#     (実測 2026-09-18: ctest 1 周で ACT_START に 2 回以上入った agent が **341 / 11221**・
#      try_shortcircuit は **338**)。展開を 1 度に抑える印 (`scArrayExpanded`) が無いと
#     再走のたびに 1 段ずつ展開が進み:
#         loft_ruled([[a,b]])   1 回目 → args=[[a,b]] (要素 1) ⇒ 型が合わずエラー
#                               2 回目 → args=[a,b]           ⇒ **通ってしまう**
#     = 同じ式が「何回再走したか」で別の意味になる。
#   ★ この形の検定が 1 本も無かったので、#3511 で [] 書式を入れてから今日まで気づかれなかった
#     (テストをいくら回しても、**入れ子を渡す式が無ければ何も暴けない**)。
#   ★★ この検定は **「正しければエラー・壊れていれば値が返る」** 形になっている。
#     ⇒ *再入が起きるようになった瞬間に必ず捕まえる*。判定に閾値も許容誤差も要らない。
#   ⚠ いま印 (`scArrayExpanded`) を外しても緑のまま通る — **バグが発火しないだけ**で、
#     検定が効かないのではない。発火しないのは @loft_ruled@ の ACT_START が *1.5 より後で
#     yield する経路を持たない* から:
#         1)   is_error の待ちは **1.5 の前** (展開前なので無害)
#         1.6  try_decompose が compact するのは `module::op` の指名式だけ (定数なら即解決)
#         1.7  decide_out_module が compact するのは cast / import / export の専用ブロックだけ
#     ⇒ 「配列展開を持つ op (sig に [])」と「1.5 より後で待つ op」が **いまは交わらない**。
#     ⚠⚠ #3554 の AK_MATCH は **1.7 に待ちを置く**ので、その交わりができた瞬間にここが効く。
O=$(run n1 '
var C = [rect(2,3), translate(rect(2,3),[0,0,2])];
print("VAL", volume(loft_ruled([C])));
')
echo "$O" | grep -q "^VAL " && {
	echo "FAIL: 入れ子の配列 loft_ruled([[a,b]]) が通ってしまった (展開が 2 段進んでいる =
	      try_shortcircuit が再入可能でない)"; echo "$O"; exit 1; }
#   ★ **意図したエラーで落ちている**ことまで見る (別の理由で落ちても緑になるのを防ぐ)。
echo "$O" | grep -q "loft_ruled" || {
	echo "FAIL: 入れ子配列が loft_ruled のエラーになっていない (別の理由で落ちた?)"; echo "$O"; exit 1; }
n=$((n+1))

echo "LOFT-RULED-OK $SO ($n checks)"

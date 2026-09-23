#!/bin/sh
# ★★ #3518 の 2: **面を取り出す** — face(solid,i) / face_at(solid,[x,y,z])。
#
# $1 = srava 実行体 / $2 = モジュール名 (.so) / $3 = 許容誤差
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- なぜ occt だけなのか ----
# ★ oc-face3d の実体は TopoDS_Face = **任意の曲面上のトリム面**なので、円柱の側面を
#   「2D 領域」として取り出せる。メッシュ系の 2D は平面に生きているので同じ物を持てない
#   (あちらの「面」は三角形でもある) ⇒ fillet / chamfer と同じ **occt 固有**の op。
#
# ---- 何で検証するか ----
# ★ 取り出した面は多くが曲面なので polygonize は断る (7c80fbd)。
#   ⚠ #3536 以降、**z=0 でない平面は断らない** — 平面を mf-cross2d の枠として引き継ぐ。
#     断るのは曲面上の面だけ (折れ線にできないのは平面かどうかとは別の理由)。
#   ⇒ 検証は **area と閉形式**で行う。円柱: 側面 2πrh・底面 πr²。
# ★★ **順序の再現性そのものを検査する** — face(B,i) の i が同じ面を指し続けることが
#   キャッシュの前提なので、ここが崩れたら黙って別の値になる ([[nary-tree-order-lottery]])。
SRAVA="$1"
SO="${2:?module .so not given}"
TOL="${3:-1e-9}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

run() {   # run <cache-suffix> <source>  → VAL 行だけ拾う
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});
$2" "$SRAVA" 2>&1
}
chk() {   # chk <name> <got> <expected>
	ok=$(awk -v g="$2" -v e="$3" -v t="$TOL" 'BEGIN{
		d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1;
		print (g!="" && d/s <= t) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: $1 が $2 (期待 $3)"; exit 1; }
	n=$((n+1))
}
n=0

# ---- ① 索引で取る: box(2,3,4) の 6 面。面積の **多重集合**が {6,6,8,8,12,12} ----
#   ★ 並べ替えてから比べる — **巡回順そのものを期待値に焼き付けない**。
#     OCCT の内部順は版で変わりうるので、焼き付けると 7.9.3 (mac) で落ちる検査になる。
O=$(run i1 '
var B = box(2,3,4);
print("NF", nfaces(B));
print("VAL", area(face(B,0)));
print("VAL", area(face(B,1)));
print("VAL", area(face(B,2)));
print("VAL", area(face(B,3)));
print("VAL", area(face(B,4)));
print("VAL", area(face(B,5)));')
chk "nfaces(box)" "$(echo "$O" | sed -n 's/^NF //p')" 6
# ⚠⚠ **値の分割だけ glob を止める** (2026-09-22)。
#   `set -- $(…)` の $( ) は **クォートできない** (単語分割が要る) のでパス名展開に掛かる。
#   比べる値は `[1,1,0]` 形 = glob の **文字クラス**なので、カレント (ビルド dir) に `1` という
#   名前のファイルが 1 つあるだけで `1` に化け、**「値が違う」という、それらしい文言で赤くなる**
#   (2026-09-21 に実際に踏んだ。⚠ PIG_TIMING は *ファイルパス* を取る env なので
#   `PIG_TIMING=1` と打つと `1` というファイルができる)。
#   ★★ **実行と分割を分ける**のが肝。`set -f; set -- $(cmd)` と囲うと -f が $( ) の中まで効き、
#     cmd がシェル関数だと *その中の glob* まで殺す (実測: count_tus → objs_of の
#     `CMakeFiles/*/link.txt` が読めず「全 0 本」になった)。⇒ 先に変数へ取ってから分割する。
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p' | sort -g)
set -f
set -- $_SPLIT_
set +f
chk "面積の小さい順 1" "$1" 6
chk "面積の小さい順 2" "$2" 6
chk "面積の小さい順 3" "$3" 8
chk "面積の小さい順 4" "$4" 8
chk "面積の小さい順 5" "$5" 12
chk "面積の小さい順 6" "$6" 12

# ---- ② ★★ 索引の再現性。同じ式の同じ i が同じ面を指すこと ----
#   ⚠ ここが崩れるとキャッシュに焼き付いた値が版ごとに別物になる。
O=$(run i2 '
var B = box(2,3,4);
print("VAL", area(face(B,2)));
print("VAL", area(face(B,2)));            // 同じ式・2 回
print("VAL", area(face(box(2,3,4),2)));   // 別に書いた同じ立体
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
[ "$1" = "$2" ] && [ "$1" = "$3" ] || { echo "FAIL: face(B,2) が呼ぶたびに違う面を指す ($1 / $2 / $3)"; exit 1; }
n=$((n+2))

# ---- ③ 範囲外・索引無し・非整数は明示エラー (黙って 0 番を使わない) ----
for T in 'face(box(1,1,1),6)' 'face(box(1,1,1),-1)'; do
	echo "$(run i3 "print(\"VAL\", area($T));")" | grep -q "out of range" || {
		echo "FAIL: 範囲外の索引 ($T) が通った"; exit 1; }
	n=$((n+1))
done
#   ⚠ 索引を省いた呼び方は **routing が先に答える** (op の中までは来ない)。
#   ★ #3570 段4: 個数は行の成立条件になったので、文言は「どれも受けない」を候補ごとに
#     並べる形になった (以前の "expected 2 argument(s), got 1" は *勝った行* の文言)。
echo "$(run i4 'print("VAL", area(face(box(1,1,1))));')" | grep -q "no candidate takes 1 argument(s)" || {
	echo "FAIL: 索引無しの face() が通った"; exit 1; }
n=$((n+1))
echo "$(run i5 'print("VAL", area(face(box(1,1,1),1.5)));')" | grep -q "whole number" || {
	echo "FAIL: 非整数の索引が通った"; exit 1; }
n=$((n+1))

# ---- ④ ★ 曲面の面が取れる: 円柱の側面は **円筒面 1 枚**。閉形式 2*pi*r*h ----
#   ⚠ srava の cylinder は **原点中心** (z = -h/2 .. +h/2)。
O=$(run c1 '
var C = cylinder(0.5,4);
print("VAL", nfaces(C));
print("VAL", area(face_at(C,[2,0,0])));     // 側面 2*pi*0.5*4
print("VAL", area(face_at(C,[0,0,9])));     // 上面 pi*0.25
print("VAL", area(face_at(C,[0,0,-9])));    // 下面
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "nfaces(cylinder)" "$1" 3
chk "側面 (2*pi*r*h)"  "$2" 12.566370614359172
chk "上面 (pi*r^2)"    "$3" 0.7853981633974483
chk "下面 (pi*r^2)"    "$4" 0.7853981633974483

# ---- ⑤ 幾何で指す: 位置で選べること。面の **割れ方が変わっても**同じ場所を指す ----
#   ★ box(2,3,4) と「2 つ積んだ箱」は同じ立体だが面集合が違う (継ぎ目が残る)。
#     索引では別の面を指すが、face_at は **どちらでも天面**を指す。
O=$(run g1 '
var A = box(2,3,4);
var B = box(2,3,2) ||| translate(box(2,3,2),[0,0,2]);
print("VAL", nfaces(A));
print("VAL", nfaces(B));
print("VAL", area(face_at(A,[1,1.5,9])));   // 天面 = 2*3
print("VAL", area(face_at(B,[1,1.5,9])));   // 面集合が違っても天面 = 2*3
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "nfaces(box)"        "$1" 6
[ "$2" = "6" ] && echo "  note: 積んだ箱も 6 面 (OCCT が継ぎ目を消した)" || n=$((n+1))
chk "A の天面"           "$3" 6
chk "B の天面 (面集合が違っても同じ場所)" "$4" 6

# ---- ⑥ ★ 同距離は断る (角・稜の真上で黙って片方を選ばない) ----
for T in 'face_at(box(2,2,2),[5,5,5])' 'face_at(cylinder(0.5,4),[2,0,2])'; do
	echo "$(run e1 "print(\"VAL\", area($T));")" | grep -q "same distance" || {
		echo "FAIL: 同距離の点 ($T) を黙って受けた"; exit 1; }
	n=$((n+1))
done

# ---- ⑦ 型: 2D も受ける (2026-09-13) / 点が無ければ断る ----
#   ⚠ この項は 2026-09-13 に **意味が変わった**。それまでは「2D を断ること」を見ていたが、
#     project / ブールの結果が 1 枚とは限らないと分かったので、束から 1 枚取り出せる方が
#     正しい約束になった。⇒ 1 面の 2D に当てると **その面がそのまま**返る。
O=$(run t1 'print("VAL", area(face(rect(2,3),0)));')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "1 面の 2D に face を当てるとその面" "$1" 6
echo "$(run t2 'print("VAL", area(face_at(box(1,1,1),5)));')" | grep -q "needs a point" || {
	echo "FAIL: face_at が点でない引数を受けた"; exit 1; }
n=$((n+1))

# ---- ⑧ ★★ #3518 の 4: **2D x 3D のブール** — 面を立体で切り取る ----
#   ★ 検証は閉形式。円柱 (r=1,h=3) の側面 = 2πrh。半空間で切ると半分 = πrh。
#     高さ 1 の帯で切ると 2πr*1。⇒ **面積の加法性**が成り立つ (18.8496 = 9.4248 + 9.4248)。
O=$(run b1 '
var C = cylinder(1,3);
var S = face_at(C,[5,0,0]);                        // 側面 (円筒面のまま)
var HB = translate(box(9,9,9),[-4.5,0,-4.5]);      // y>=0 の半空間
var Z1 = translate(box(9,9,1),[-4.5,-4.5,-0.5]);   // z の帯 1
print("VAL", area(S));
print("VAL", area(S &&& HB));
print("VAL", area(S --- HB));
print("VAL", area(HB &&& S));                      // ★ intersection は可換 (両向き)
print("VAL", area(S &&& Z1));
print("VAL", area(S &&& translate(box(1,1,1),[50,50,50])));   // 交わらない = 空 (エラーではない)
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "側面 (2*pi*r*h)"        "$1" 18.849555921538759
chk "∩ 半空間 (pi*r*h)"      "$2" 9.42477796076938
chk "− 半空間 (pi*r*h)"      "$3" 9.42477796076938
chk "逆向きの ∩ (可換)"       "$4" 9.42477796076938
chk "∩ 帯 (2*pi*r*1)"        "$5" 6.283185307179586
chk "交わらない = 空"         "$6" 0

# ---- ⑨ ★ 意図的に **載せていない**組み合わせは planner が断る ----
#   立体 − 面 … 体積 0 の面で立体を切っても変わらない (黙って no-op になる)
#   2D ∪ 3D  … 次元の違う和を表現できる型が無い
for T in 'box(9,9,9) --- face(box(2,2,2),0)' 'face(box(2,2,2),0) ||| box(9,9,9)'; do
	echo "$(run b2 "print(\"VAL\", area($T));")" | grep -q "no module can execute op" || {
		echo "FAIL: 載せていないはずの組み合わせ ($T) が通った"; exit 1; }
	n=$((n+1))
done

# ---- ⑩ ★ #3518 の 6: **面の素性を訊く** (surface_type / nfaces / bbox / centroid が 2D を受ける) ----
#   ★ face / face_at で平面でない 2D が普通に入るようになったので、「cast できるのか /
#     polygonize できるのか / extrude して意味があるのか」を **踏む前に** 判断する手段が要る。
O=$(run q1 '
var C = cylinder(1,3);
print("T", surface_type(face_at(C,[5,0,0])));       // 側面 = 円筒面
print("T", surface_type(face_at(C,[0,0,9])));       // 上面 = 平面
print("T", surface_type(rect(2,3)));
print("T", surface_type(face(sphere(1),0)));
print("T", surface_type(face(torus(2,0.5),0)));
print("T", surface_type(face_at(C,[5,0,0]) &&& translate(box(1,1,1),[50,50,50])));   // 空
print("VAL", nfaces(rect(2,3)));
print("VAL", area(face_at(C,[0,0,9])));
')
_SPLIT_=$(echo "$O" | sed -n 's/^T //p')
set -f
set -- $_SPLIT_
set +f
[ "$1" = "cylinder" ] || { echo "FAIL: 円柱側面の surface_type が $1"; exit 1; }
[ "$2" = "plane" ]    || { echo "FAIL: 円柱上面の surface_type が $2"; exit 1; }
[ "$3" = "plane" ]    || { echo "FAIL: rect の surface_type が $3"; exit 1; }
[ "$4" = "sphere" ]   || { echo "FAIL: 球面の surface_type が $4"; exit 1; }
[ "$5" = "torus" ]    || { echo "FAIL: トーラス面の surface_type が $5"; exit 1; }
[ "$6" = "empty" ]    || { echo "FAIL: 空の 2D の surface_type が $6"; exit 1; }
n=$((n+6))
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "nfaces(2D)" "$1" 1
chk "area(上面)"  "$2" 3.141592653589793

# ---- ⑪ ★★ #3518 の 3: **投影** — 平面図形を曲面へ投影して切る ----
#   閉形式: 円柱 (r=1) の側面に幅 2a・高さ h の窓を側方から投影すると
#           切り取られる面積 = r * 2*asin(a/r) * h。a=0.5, r=1, h=2 なら 2*asin(0.5)*2 = 2.0943951024
#   ★★ **直線投影は閉曲面を 2 回当たる** (手前と奥)。project は *投影元から見える側だけ*
#     を残す。⇒ 選別しない (角柱との ∩ をそのまま取る) と **ちょうど 2 倍**になる。
#     この 2 つを並べて検査することで「選別が効いている」ことまで見る。
O=$(run p1 '
var C = cylinder(1,4);
var S = face_at(C,[5,0,0]);
var D = translate(rect(2,1),[-1,-0.5]);                  // XY で x:-1..1, y:-0.5..0.5
var R = translate(rotate(D,"y",90),[-3,0,0]);            // x=-3 の平面へ立てる (y 幅 1・z 高さ 2)
print("VAL", area(S));
print("VAL", area(R));
print("VAL", area(project(R,S,[1,0,0])));
print("VAL", area(S &&& translate(rotate(extrude(D,9),"y",90),[-3,0,0])));   // 選別なし = 2 倍
print("T", surface_type(project(R,S,[1,0,0])));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "側面 (2*pi*r*h)"          "$1" 25.132741228718345
chk "投影する平面図形 (1x2)"    "$2" 2
chk "★ 投影 (弧長 x 高さ)"      "$3" 2.0943951023931953
chk "選別なし = ちょうど 2 倍"   "$4" 4.1887902047863906
[ "$(echo "$O" | sed -n 's/^T //p')" = "cylinder" ] || {
	echo "FAIL: 投影の結果が円筒面でない"; exit 1; }
n=$((n+1))

# ---- ⑬ ★★ **投影の結果は 1 枚とは限らない** — 凹んだ立体の 2 か所に当たる ----
#   U 字 (箱の上に切り欠き) の手前の面は *U 字型の 1 枚の平面*。そこへ塔の高さの帯を
#   投影すると、**塔 2 本のところで 2 枚**に切り取られる。どちらも投影元を向いている。
#   ⇒ 「手前だけ残す」= 1 枚に絞る、ではない。★ 面数を検査に入れておく。
O=$(run u1 '
var U = box(6,2,4) --- translate(box(4,4,3),[1,-1,1]);
var F = face_at(U,[0.5,-9,2]);
var D = translate(rotate(rect(6,1),"x",90),[0,-9,2]);
var P = project(D,F,[0,1,0]);
print("VAL", volume(U));
print("VAL", nfaces(F));
print("VAL", area(F));
print("VAL", nfaces(P));
print("VAL", area(P));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "U 字の体積 (6*2*4 - 4*2*3)"      "$1" 24
chk "手前の面は 1 枚"                  "$2" 1
chk "手前の面の面積 (6*4 - 4*3)"       "$3" 12
chk "★ 投影は **2 枚**になる"          "$4" 2
chk "★ 投影の面積 (1x1 が 2 枚)"       "$5" 2

#   ★★ 束から 1 枚を取り出せること (face / face_at が **2D も受ける**・2026-09-13)。
#     ⚠ これが無いと「2 枚返る」値を受け取った側が使えない。
O=$(run u1b '
var U = box(6,2,4) --- translate(box(4,4,3),[1,-1,1]);
var P = project(translate(rotate(rect(6,1),"x",90),[0,-9,2]), face_at(U,[0.5,-9,2]), [0,1,0]);
print("VAL", area(face(P,0)));
print("VAL", area(face(P,1)));
print("VAL", area(face_at(P,[0.5,-9,2.5])));    // 左の塔
print("VAL", area(face_at(P,[5.5,-9,2.5])));    // 右の塔
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "束の 0 枚目"          "$1" 1
chk "束の 1 枚目"          "$2" 1
chk "位置で左を指す"        "$3" 1
chk "位置で右を指す"        "$4" 1
#   ⚠ 左右が **別の面**であること (同じ面を 2 回指していたら検査になっていない)
O2=$(run u1c '
var U = box(6,2,4) --- translate(box(4,4,3),[1,-1,1]);
var P = project(translate(rotate(rect(6,1),"x",90),[0,-9,2]), face_at(U,[0.5,-9,2]), [0,1,0]);
print("B", bbox(face_at(P,[0.5,-9,2.5])));
print("B", bbox(face_at(P,[5.5,-9,2.5])));')
L=$(echo "$O2" | sed -n 's/^B //p' | head -1)
R=$(echo "$O2" | sed -n 's/^B //p' | tail -1)
[ "$L" != "$R" ] || { echo "FAIL: face_at が左右で同じ面を返している ($L)"; exit 1; }
n=$((n+1))
#   範囲外の索引は 2D でも断る
echo "$(run u1d '
var U = box(6,2,4) --- translate(box(4,4,3),[1,-1,1]);
var P = project(translate(rotate(rect(6,1),"x",90),[0,-9,2]), face_at(U,[0.5,-9,2]), [0,1,0]);
print("VAL", area(face(P,2)));')" | grep -q "2D region has 2 faces" || {
	echo "FAIL: 2D の範囲外索引が通った"; exit 1; }
n=$((n+1))

# ---- ⑭ ★★ 1 枚の面が手前と奥の **両方**を向くときは明示エラー ----
#   トーラスの下面に帯を投影すると、切り取られた面は *管の断面を一周するひと続きの輪*
#   になり、1 枚が手前と奥の両方を向く。手前だけを面の粒度で取り出せないので断る。
#   ⚠ 2026-09-13 まではここで黙って空が返っていた (「交わらなかった」と区別がつかない)。
#     判定を面の中央 1 点でやっていたためで、その点はちょうど輪郭線の上 (dot=0) だった。
echo "$(run u2 '
var S = face(torus(2,0.5),0);
var D = translate(translate(rect(18,0.4),[-9,-0.2]),[0,0,-9]);
print("VAL", area(project(D,S,[0,0,1])));')" | grep -q "wraps around the surface" || {
	echo "FAIL: 輪郭線をまたぐ面の投影が黙って通った (空が返っていないか)"; exit 1; }
n=$((n+1))

# ---- ⑮ ★★ **「手前」は「見える」ではない** — 遮蔽は見ていない (約束の固定) ----
#   縦置きのトーラス (車輪) を下から投影すると、垂直な柱は **下の管と上の管の両方**を貫く。
#   当たるのは 6 面だが、project が返すのは *外向き法線が下を向いている* 3 面:
#       z=-2.51..-2.35  2 枚  下の管の外側 (下向き)      ← 本当に見える
#       z=+1.49..+1.66  1 枚  **上の管の内側** (下向き)  ← 下の管の陰。見えないが返る
#   ⇒ 「いちばん手前の 1 枚」を返すには遮蔽の判定が要り、それには **立体**が要る
#     (この op は面しか受け取らないので原理的に決められない)。★ ここを検査で固定しておく。
#   ⚠⚠ **面の枚数は焼き付けない**。当たるのは幾何としては 4 枚の帯だが、OCCT の報告は
#     **6 枚**になる — トーラスの *外側の赤道が v=0 のパラメータ継ぎ目 (seam)* で、
#     そこをまたぐ帯が 2 枚に割れるため (UV を見て確認: v=[0,0.41] と v=[5.87,6.28])。
#     ★ 継ぎ目の置き方は OCCT の版で変わりうるので、**枚数ではなく面積で**判定する。
#     面積なら鋭い: 遮蔽まで見て「本当に見える面」だけを返すなら 0.1648 になるので、
#     0.32987 であること自体が「遮蔽を見ていない」の証拠になる。
O=$(run v1 '
var T = rotate(torus(2,0.5),"x",90);
var S = face(T,0);
var R = translate(rect(0.4,0.4),[-0.2,-0.2]);
var D = translate(R,[0,0,-9]);
print("VAL", area(S &&& translate(extrude(R,20),[0,0,-9])));   // 当たる面積
print("VAL", area(project(D,S,[0,0,1])));                      // 返る面積
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "縦トーラス: 当たる面積"                    "$1" 0.65974847949528925
chk "★ project が返す面積 (遮蔽を見ないので半分)" "$2" 0.32987423974764396

# ---- ⑯ ★★ unify_faces — 同じ曲面に載る隣り合う面を 1 枚に畳む (形は変えない) ----
#   ★ 面の枚数は幾何だけでは決まらない (周期曲面は継ぎ目で割れる) ので、枚数を意味のある
#     数にしたいときに通す。判定は「接しているか」ではなく **同じ曲面に載っているか**。
#   ⚠ 面積・体積は **厳密には一致しない** — pcurve を作り直すので相対 1e-10 くらい動く
#     (実測 0.65974847949528925 → 0.65974847932751912 = 相対 2.5e-10)。
#     ⇒ 「値が変わらない」ではなく「**許容内で変わらない**」を検査する。
O=$(run uf1 '
var T = rotate(torus(2,0.5),"x",90);
var CUT = face(T,0) &&& translate(extrude(translate(rect(0.4,0.4),[-0.2,-0.2]),20),[0,0,-9]);
print("VAL", nfaces(CUT));
print("VAL", nfaces(unify_faces(CUT)));          // 継ぎ目で割れた 2 組が畳まれる
print("VAL", area(unify_faces(CUT)));            // 面積は許容内で不変
print("T", surface_type(unify_faces(CUT)));      // 曲面種は保たれる
var ST = box(2,3,2) ||| translate(box(2,3,2),[0,0,2]);
print("VAL", nfaces(ST));
print("VAL", nfaces(unify_faces(ST)));           // 同一平面の継ぎ目が消える
print("VAL", volume(unify_faces(ST)));           // 体積は不変
print("VAL", nfaces(unify_faces(box(2,2,2))));   // 畳むものが無ければそのまま
print("VAL", nfaces(fillet(box(2,2,2),0.3)));
print("VAL", nfaces(unify_faces(fillet(box(2,2,2),0.3))));   // ★ 接していても曲面が違えば畳まない
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "切り取り (継ぎ目で割れて 6 枚)"      "$1" 6
chk "★ 畳むと 4 枚"                       "$2" 4
chk "面積は許容内で不変"                   "$3" 0.65974847949528925
chk "積んだ箱 (継ぎ目つき)"                "$4" 10
chk "★ 畳むと 6 枚"                       "$5" 6
chk "体積は不変"                           "$6" 24
chk "畳むものが無い箱はそのまま"            "$7" 6
chk "丸めた箱 (平面6+円筒12+球8)"          "$8" 26
chk "★ 接しているだけの面は畳まない"        "$9" 26
[ "$(echo "$O" | sed -n 's/^T //p')" = "torus" ] || {
	echo "FAIL: 畳んだ後の曲面種が torus でない"; exit 1; }
n=$((n+1))

# ---- ⑫ 投影の引数検査 ----
echo "$(run p2 'print("VAL", area(project(rect(2,3),face(box(2,2,2),0),[0,0,0])));')" \
	| grep -q "must not be \[0,0,0\]" || { echo "FAIL: 零ベクトルの投影方向が通った"; exit 1; }
n=$((n+1))

# ---- ⑬ ★★ #3547: **測る op が 2D を受ける** (nverts / valid) ----
# ⚠ 直す前は routing が断っていた ("no module can execute op 'nverts' on (oc-face3d)")。
#   **型の穴であって計算の穴ではない** — TopExp も BRepAlgoAPI_Check も TopoDS_Shape を取るので
#   面でもそのまま動く。#3527 段 4〜6 で mf / gg / ch が 2D まで埋まり occt だけ残っていた。
# ★ valid の定義は共通 (#3487 の 3 条件を 2D へ写したもの・src/h/common/ringprops.h):
#     ① 空でない ∧ ② 面積を持ちうる ∧ ③ 自己交差が無い
#   ⇒ **3 つを 1 つずつ別の入力で落とす**。⚠ 全部 0 になる入力だけを並べると
#     「② で落ちているのに ③ が効いていると思い込む」形の空振りになる (現に bowtie は
#     対称だと面積が相殺して 0 になり、②で落ちて ③ を通らない)。
#   ⇒ 最後の 2 行が対 — **面積 1 (>0) なのに valid 0** で初めて ③ が効いたと言える。
O=$(run m1 '
print("VAL", nverts(face(box(2,2,2),0)));
print("VAL", valid(face(box(2,2,2),0)));
print("VAL", nverts(rect(3,2)));
print("VAL", valid(rect(3,2)));
print("VAL", nverts(circle(1)));
print("VAL", valid(polygon([[0,0],[6,0],[1,3],[6,4],[0,4]])));
print("VAL", valid(empty2d()));
print("VAL", valid(polygon([[0,0],[2,0],[4,0]])));
print("VAL", valid(polygon([[0,0],[4,0],[0,2],[3,2]])));
print("VAL", area(polygon([[0,0],[4,0],[0,2],[3,2]])));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "面 (oc-face3d) の頂点数"               "$1" 4
chk "面は妥当"                              "$2" 1
chk "矩形 (oc-cross2d) の頂点数"            "$3" 4
chk "矩形は妥当"                            "$4" 1
chk "★ 円の頂点は 1 (閉じた稜の継ぎ目)"      "$5" 1
chk "凹でも自己交差が無ければ妥当"           "$6" 1
chk "① 空は妥当でない"                      "$7" 0
chk "② 面積を持てない (共線) は妥当でない"   "$8" 0
chk "③ 自己交差は妥当でない"                "$9" 0
shift 9
chk "★ ③ の入力は面積を持つ (②で落ちていない)" "$1" 1

# ---- ⑭ ★★ #3547 ②: offset (面内) と offset_thicken (厚み) ----
# ★★ **別の操作なので名前を分けてある** (ひさ裁定 2026-09-18):
#     offset(2D,d)          面の *中で* 輪郭を動かす  → 2D のまま (cg / mf と同じ約束)
#     offset_thicken(2D,d)  面の法線側に厚みを付ける  → 3D ・ **片側だけ**
#   ⚠ 同じ名前にすると、*-face3d は「曲面」ではなく「空間に置かれた」の意味なので
#     (規約①・平面も大量に含む)、cg / mf が 2D を返す同じ入力で occt だけ 3D を返すことになる。
# ★ どれも **閉形式で検算できる** ので、カーネル合議を使わずにその場で固定できる。
O=$(run m2 '
print("VAL", area(offset(rect(3,2), 0.2)));
print("VAL", area(offset(rect(3,2), -0.2)));
print("VAL", area(offset(circle(1), 0.5)));
print("VAL", nedges(offset(circle(1), 0.5)));
print("VAL", volume(offset_thicken(rect(3,2), 0.1)));
print("VAL", volume(offset_thicken(circle(1), 0.25)));
print("VAL", volume(offset_thicken(face(cylinder(1,4),0), 0.1)));
print("VAL", volume(offset_thicken(face(cylinder(1,4),0), -0.1)));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
# 外側は角が **円弧で丸まる** (Arc join) ので Steiner の形: 6 + 10d + πd²
chk "矩形を外へ 0.2"              "$1" 8.1256637061435946
# 内側は角が立つ ⇒ ただの縮小 (3-2d)(2-2d)
chk "矩形を内へ 0.2"              "$2" 4.16
chk "円を外へ 0.5 (π1.5²)"        "$3" 7.0685834705770363
# ★★ occt だけの値 — 輪郭が **曲線のまま**なので稜は 1 本。cg / mf は折れ線に落ちる
chk "★ 円は円のまま (稜 1 本)"     "$4" 1
chk "平面の厚み = 面積 x d"        "$5" 0.6
# ⚠ ここから下は曲面を含むので GProp の求積誤差が 1e-8 ほど乗る。⇒ **この 3 行だけ** 緩める
#   (既定の 1e-9 のままだと *正しい値で赤くなる*)。緩めた範囲は閉形式との相対差で 5e-9〜1.3e-8。
TOL_SAVE="$TOL"; TOL=1e-7
chk "円板の厚み = πr²d"           "$6" 0.78539816339744828
chk "円筒側面を外へ (π(1.1²−1²)4)" "$7" 2.638937829015426
chk "円筒側面を内へ (π(1²−0.9²)4)" "$8" 2.3876104167282426
TOL="$TOL_SAVE"

# ---- ⑮ ★ 断るべきものを断る (#3547 ②) ----
# ⚠ 「効いた」を言うには **断る側**が要る。どちらも直す前は *黙って別の値* が返っていた:
#   ・曲面の面内オフセット → OCCT に道具が無く、UV でずらすと距離が保てない
#   ・厚みが凹側の曲率半径を超える → OCCT は IsDone()=true で **軸を越えた環**を返す
#     (r=1 の側面を内へ 1.5 で体積 3π。Geom_OffsetSurface は「自己交差を消さない・検査しない」)
run m3 'print("VAL", area(offset(face(cylinder(1,4),0), 0.1)));' \
	| grep -q "offset_thicken" || { echo "FAIL: 曲面の面内オフセットが通った (案内も出ていない)"; exit 1; }
n=$((n+1))
run m4 'print("VAL", volume(offset_thicken(face(cylinder(1,4),0), -1.5)));' \
	| grep -q "radius of curvature" || { echo "FAIL: 曲率半径を超える厚みが通った"; exit 1; }
n=$((n+1))

# ---- ⑯ ★★ #3547 ④: 頂点を読む 3 つ組 (vert / verts / face_verts) ----
# ⚠⚠ occt の「頂点」は **稜の端点** — 立方体 8 ・ 円筒 2 (継ぎ目) ・ 円 1。
#   mesh 系の vert (三角形の頂点) とは *数え方が違う* が、それがこの表現の要点。
# ★★ いちばん効く検査は **verts(v) の i 番目 == vert(v,i)**。片方だけ順を変えると
#   黙ってずれるので、等式そのものを固定する (#3527 の⚠・geomutils と同じ検査)。
O=$(run m5 '
module("points.so",{});
print("VAL", nverts(box(2,3,4)));
print("VAL", vert(box(2,3,4),0));
print("VAL", face_verts(box(2,3,4),0));
print("VAL", nverts(cylinder(1,4)));
print("VAL", face_verts(cylinder(1,4),0));
print("VAL", nverts(verts(box(2,3,4))));
print("VAL", vert(box(2,3,4),3));
print("VAL", vert(verts(box(2,3,4)),3));
print("VAL", nverts(verts(rect(3,2))));
print("VAL", vert(verts(rect(3,2)),2));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "立方体の頂点数 (稜の端点)"      "$1" 8
[ "$2" = "[0,0,4]" ] || { echo "FAIL: vert(box,0) が $2 (期待 [0,0,4])"; exit 1; }
n=$((n+1))
# ★ B-rep の面は n 頂点 — 箱の面は 4 (cgal の face_verts は三角形なので常に 3)
[ "$3" = "[0,1,2,3]" ] || { echo "FAIL: face_verts(box,0) が $3 (期待 [0,1,2,3])"; exit 1; }
n=$((n+1))
chk "円筒の頂点数 (継ぎ目の 2 つ)"    "$4" 2
[ "$5" = "[0,1]" ] || { echo "FAIL: face_verts(cyl,0) が $5 (期待 [0,1])"; exit 1; }
n=$((n+1))
chk "点群にしても点数は同じ"          "$6" 8
# ★★ 3 つ組の要 — 同じ列を同じ順で指すこと
[ -n "$7" ] && [ "$7" = "$8" ] || { echo "FAIL: vert(v,3)=$7 と vert(verts(v),3)=$8 がずれた"; exit 1; }
n=$((n+1))
chk "2D も点群にできる"               "$9" 4
shift 9
# ★ 成分数は名乗りの規約どおり (cross2d = 2 成分・bbox / centroid と同じ)
[ "$1" = "[3,2]" ] || { echo "FAIL: vert(verts(rect),2) が $1 (期待 [3,2] = 2 成分)"; exit 1; }
n=$((n+1))

# ---- ⑰ ★★ #3553: distance_at が 2D も受ける (定義は 3D と同じ) ----
# ★★ ひさ裁定 (2026-09-18): **新しい意味を作らない**。@*-face3d@ / @*-cross2d@ はどちらも
#   *3D に埋め込まれた 2 次元* なので、既存の定義「p から **面の集合** までの最短距離」が
#   そのまま当てはまる。⇒ 3D 側の実装と **1 本**を共有している (oc_distance_to_faces)。
# ⚠ 「平面へ射影してから 2D で測る」案は採らなかった — 面外の点の高さを黙って捨てるので、
#   #3533 / #3534 で何度も直した *置き場所が落ちる* 事故と同じ形になる。
#   ⇒ その違いを **測って固定する**のが下の 2 行目 (真上 0.7 が 0 になってはいけない)。
O=$(run m6 '
print("VAL", distance_at(rect(3,2),[1.5,1,0]));
print("VAL", distance_at(rect(3,2),[1.5,1,0.7]));
print("VAL", distance_at(rect(3,2),[5,1,0]));
print("VAL", distance_at(rect(3,2),[5,4,0]));
print("VAL", distance_at(face(cylinder(1,4),0),[0,0,2]));
print("VAL", distance_at(sphere(2),[0,0,0]));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
# ★ 面の上の点は 0 — 「点が面を通るか」がそのまま測れる (#3532 の当初の需要)
chk "面上の点は 0"                    "$1" 0
# ★★ ここが射影案との分かれ目。射影すると 0 になる
chk "★ 真上 0.7 は 0.7 (射影しない)"   "$2" 0.7
chk "同一平面で外に 2"                "$3" 2
chk "角の外は 2√2"                    "$4" 2.8284271247461903
chk "円柱の軸上から側面まで r=1"       "$5" 1
chk "球の中心から面まで r (3D は不変)" "$6" 2

echo "OCCT-FACE-OK $SO ($n checks)"

#!/bin/sh
# ★★ #3518: **2D (cross2d) のアフィン変換** — 置き場所と向きの回帰。
#
# $1 = srava 実行体 / $2 = モジュール名 (module() に渡す .so) / $3 = 許容誤差
# $4 = "oc" なら occt 固有の項 (面外へ出す変換・polygonize の拒否) も見る。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- なぜ 3D の srava_affine.sh と別に要るのか ----
# ★ 変換 op は 2D 型も受ける (cgal / manifold は元から・occt は #3518 で足した)。ところが
#   **2D の検査がどこにも無かった** — 3D の回帰は 2D の経路を 1 行も通らない。
#   ⇒ #3516 と同じ「表にして数えるまで見えない歯抜け」。
#
# ---- なぜ area だけでは足りないか ----
# ★★ **面積は剛体変換で不変**なので、translate / rotate / mirror が何もしていなくても
#   area は正しい値を返す。⇒ 2D を **extrude して 3D にし、内部に小箱を置いて切り取る**
#   (srava_affine.sh の型をそのまま 2D へ移した)。probe は一辺 0.6 の箱なので期待値は
#   0.6^3 = 0.216 ちょうど。置き場所が違えば 0 になる (半端な値にならない = 判定が鋭い)。
# ★ 向き (輪の巻き方) は **mirror の後に立体が成立するか**で見る。2D の輪が裏返ったままだと
#   extrude した立体の内外が入れ替わるので、端から差し込んだ probe を引いた値が合わない。
SRAVA="$1"
SO="${2:?module .so not given}"
TOL="${3:-1e-9}"
# ★★ #3526 (2026-09-13): 第 4 引数は **2D が空間に置けるか** の宣言になった。
#   ""      … 置けない (面外へ出す変換は明示エラー)            … cgal
#   "space" … 置ける (面積は不変・extrude は world +Z のまま)   … manifold
#   "oc"    … space の項目 + occt 固有の項 (polygonize の拒否)  … occt
OCCHK="$4"
SPACE=0
[ "$OCCHK" = "space" ] || [ "$OCCHK" = "oc" ] && SPACE=1
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

SRC=$(cat <<'EOF'
var R = rect(2,3);          // [0,2]x[0,3] (角が原点)
var P = box(0.6,0.6,0.6);   // probe (体積 0.216)
var E = 1;                  // 押し出し高さ

// ① 素の面積と体積 (前提: 押し出すと [0,2]x[0,3]x[0,1])
print("VAL", area(R));
print("VAL", volume(extrude(R,E)));
print("VAL", volume(extrude(R,E) &&& translate(P,[0.7,1.2,0.2])));
// ② translate: [5,7]x[1,4] へ移る
print("VAL", volume(extrude(translate(R,[5,1]),E) &&& translate(P,[5.7,2.2,0.2])));
// ③ rotate("z",+90): (x,y)->(-y,x) なので [-3,0]x[0,2]
print("VAL", volume(extrude(rotate(R,"z",90),E) &&& translate(P,[-2.3,0.7,0.2])));
// ④ rotate("z",-90): [0,3]x[-2,0]。**符号**を固定する
print("VAL", volume(extrude(rotate(R,"z",-90),E) &&& translate(P,[0.7,-1.3,0.2])));
// ⑤ scale(スカラ): 面積は 4 倍・[0,4]x[0,6]
print("VAL", area(scale(R,2)));
print("VAL", volume(extrude(scale(R,2),E) &&& translate(P,[3.0,5.0,0.2])));
// ⑥ scale([2,1,1]): 面積は 2 倍・[0,4]x[0,3] (★ 2D でも 3 要素で書ける)
print("VAL", area(scale(R,[2,1,1])));
print("VAL", volume(extrude(scale(R,[2,1,1]),E) &&& translate(P,[3.0,1.2,0.2])));
// ⑦ mirror("x"): [-2,0]x[0,3] / mirror("y"): [0,2]x[-3,0]
print("VAL", volume(extrude(mirror(R,"x"),E) &&& translate(P,[-1.3,1.2,0.2])));
print("VAL", volume(extrude(mirror(R,"y"),E) &&& translate(P,[0.7,-1.8,0.2])));
// ⑧ ★ 向き: 反射した 2D を押し出した立体から probe を **端から差し込んで**引く
//    (6 - 0.4*0.6*0.6 = 5.856)。輪が裏返ったままだと内外が入れ替わってこの値にならない。
print("VAL", volume(difference(extrude(mirror(R,"x"),E), translate(P,[-2.2,0.2,0.2]))));
// ⑨ transform: 行優先 12 要素の平行移動 / 回転行列は rotate("z",90) と同じ
print("VAL", volume(extrude(transform(R,[1,0,0,10, 0,1,0,0, 0,0,1,0]),E) &&& translate(P,[10.7,1.2,0.2])));
print("VAL", volume(extrude(transform(R,[0,-1,0,0, 1,0,0,0, 0,0,1,0]),E) &&& translate(P,[-2.3,0.7,0.2])));
// ⑩ 反射しても立体は成立している (valid=1) / 面内回転で面積は不変
print("VAL", valid(extrude(mirror(R,"x"),E)));
print("VAL", area(rotate(R,"z",37)));
EOF
)

# 期待値 (すべて閉形式。probe は一辺 0.6 の箱 = 0.216)。
EXP="6 6 0.216 0.216 0.216 0.216 24 0.216 12 0.216 0.216 0.216 5.856 0.216 0.216 1 6"

rm -rf "$D-a2"
OUT=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
$SRC" "$SRAVA" 2>&1)
GOT=$(echo "$OUT" | sed -n 's/^VAL //p')

n=0
set -- $EXP
for g in $GOT; do
	n=$((n+1))
	e="$1"; shift
	[ -n "$e" ] || { echo "FAIL: 出力が期待より多い ($n 個目 = $g)"; echo "$OUT"; exit 1; }
	ok=$(awk -v g="$g" -v e="$e" -v t="$TOL" 'BEGIN{
		d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1;
		print (g!="" && d/s <= t) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: $n 個目が $g (期待 $e ・許容 $TOL)"; echo "$OUT"; exit 1; }
done
[ "$#" = 0 ] || { echo "FAIL: 出力が足りない ($n 個しか出ていない・残り $*)"; echo "$OUT"; exit 1; }

# ---- エラー文が 3D と **同じ** であること (2D も common/affine.h を通っている証拠) ----
# ★ 2D の経路だけ別解釈になっていたら、同じ書き間違いに違う説明が返る。
rm -rf "$D-a2e"
E1=$(SRAVA_CACHE_DIR="$D-a2e" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{}); print(\"V\", area(rotate(rect(2,3),\"q\",10)));" "$SRAVA" 2>&1)
echo "$E1" | grep -q "unknown axis 'q'" || { echo "FAIL: 2D の未知の軸のエラー文が違う"; echo "$E1"; exit 1; }
n=$((n+1))
rm -rf "$D-a2e2"
E2=$(SRAVA_CACHE_DIR="$D-a2e2" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{}); print(\"V\", area(transform(rect(2,3),[1,2,3])));" "$SRAVA" 2>&1)
echo "$E2" | grep -q "matrix must have 12 (3x4) or 16 (4x4) elements" || { echo "FAIL: 2D の行列長のエラー文が違う"; echo "$E2"; exit 1; }
n=$((n+1))

# ==================================================================
# ★★ **2D を z=0 平面の外へ置ける** カーネルの項 (#3518 の occt / #3526 の manifold)
#   ⚠ cgal はまだ置けないので当てない (あちらは面外成分を捨てないよう明示エラーにしてある)。
#   ★ 置けると area が **射影されない** (6 のまま) / translate([0,0,5]) が効く /
#     extrude は **world +Z のまま** なので傾けた面は 射影面積 x 長さ になる (ひさ判断 #3526 ①)。
#   ⇒ これが loft (断面を空間に置く) の前提。
# ==================================================================
if [ "$SPACE" = "1" ]; then
	rm -rf "$D-a2oc"
	O=$(SRAVA_CACHE_DIR="$D-a2oc" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
	var R = rect(2,3);
	var P = box(0.6,0.6,0.6);
	// ⑪ 傾けても **面の真の面積は変わらない** (cgal/manifold は射影して 6*cos45 になる)
	print(\"VAL\", area(rotate(R,\"x\",45)));
	// ⑫ 持ち上げた断面は **その高さに居る** (z=5..6 で probe に当たる)
	print(\"VAL\", volume(extrude(translate(R,[0,0,5]),1) &&& translate(P,[0.7,1.2,5.2])));
	// ⑬ 持ち上げた断面は **z=0 には居ない** (元の高さでは probe に当たらない)
	print(\"VAL\", volume(extrude(translate(R,[0,0,5]),1) &&& translate(P,[0.7,1.2,0.2])));
	// ⑭ 傾けた断面を +Z へ押し出した体積 = 射影面積 x 押し出し長 = 6*cos45
	print(\"VAL\", volume(extrude(rotate(R,\"x\",45),1)));
	" "$SRAVA" 2>&1)
	OG=$(echo "$O" | sed -n 's/^VAL //p')
	OEXP="6 0.216 0 4.2426406871192848"
	m=0
	set -- $OEXP
	for g in $OG; do
		m=$((m+1)); e="$1"; shift
		[ -n "$e" ] || { echo "FAIL: occt 固有の出力が多い ($m 個目 = $g)"; echo "$O"; exit 1; }
		ok=$(awk -v g="$g" -v e="$e" -v t="$TOL" 'BEGIN{
			d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1;
			print (g!="" && d/s <= t) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: occt 固有の $m 個目が $g (期待 $e)"; echo "$O"; exit 1; }
	done
	[ "$#" = 0 ] || { echo "FAIL: occt 固有の出力が足りない ($m 個・残り $*)"; echo "$O"; exit 1; }
	n=$((n+m))

	# ---- ★★ #3526: **空間に置いた 2D も revolve できる** (2026-09-13 に実装) ----
	# ★★ 軸は **world Y** (原点まわり)・常に (ひさ判断)。根拠は extrude と同じ論法 —
	#   world 固定なら「先に revolve して立体を動かす」で枠相対の結果も作れるが、枠相対に
	#   固定すると *置いた断面を world 軸で回す手段が無くなる*。occt は元から world Y。
	#
	# ★ 判定に使う **閉形式**: 軸に平行な辺を持つ矩形 x∈[a,b] を面外に h ずらして Y 軸で回すと、
	#   環の内外半径は sqrt(a^2+h^2) / sqrt(b^2+h^2) なので
	#       R_out^2 - R_in^2 = (b^2+h^2) - (a^2+h^2) = b^2 - a^2
	#   ⇒ **体積は h に依らない** (多角形近似でも同じ式が成り立つ)。
	#   ⇒ ① 置いても体積が変わらないこと を見る。
	# ⚠ ただし **体積だけでは「置き場所が無視されている」と区別できない** (同じ値になるので)。
	#   ⇒ ② **bbox が広がること** を併せて見る (x の端が sqrt(b^2+h^2) 相当へ伸びる)。
	#     枠を無視していれば ±b のままなので、ここで落ちる。
	# ⚠ 部分回転は **比較しない** — 刻み幅の決め方が従来経路 (枠が既定) と新経路で違うので、
	#   置いた/置いていないを突き合わせる意味が無い (どちらも正しい近似)。
	# ⚠ valid も見ない — manifold の valid (double の自己交差述語) は *回転した revolve の
	#   立体を誤検出する* (同じメッシュを cgal の厳密述語に渡すと 1)。#3526 とは無関係な既存の
	#   欠陥なので、ここで固定すると別の理由で落ちるテストになる。
	rm -rf "$D-a2rv"
	RV=$(SRAVA_CACHE_DIR="$D-a2rv" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
	var Q = translate(rect(1,2),[3,0,0]);
	print(\"VAL\", volume(revolve(Q,360)));
	print(\"VAL\", volume(revolve(translate(Q,[0,0,5]), 360)));
	print(\"VAL\", bbox(revolve(Q,360)));
	print(\"VAL\", bbox(revolve(translate(Q,[0,0,5]), 360)));
	" "$SRAVA" 2>&1)
	RG=$(echo "$RV" | sed -n 's/^VAL //p')
	V0=$(echo "$RG" | sed -n 1p); V5=$(echo "$RG" | sed -n 2p)
	X0=$(echo "$RG" | sed -n 3p | tr -d '[]' | cut -d, -f4)
	X5=$(echo "$RG" | sed -n 4p | tr -d '[]' | cut -d, -f4)
	# ① 置いても体積は変わらない (閉形式より h に依らない)
	ok=$(awk -v g="$V5" -v e="$V0" -v t="$TOL" 'BEGIN{
		d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1; print (g!="" && e!="" && d/s <= t) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 置いた断面の revolve の体積が $V5 (局所は $V0)"; echo "$RV"; exit 1; }
	n=$((n+1))
	# ② ★ bbox が広がっている = **置き場所が効いている** (体積だけでは分からない)
	#    局所は x 端が 4 付近・z=5 に置くと sqrt(4^2+5^2)=6.40 付近まで伸びる
	ok=$(awk -v a="$X0" -v b="$X5" 'BEGIN{ print (a!="" && b!="" && b > a*1.4) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 置いても bbox が広がらない (局所 $X0 / 置いた $X5) = 枠が無視されている"; echo "$RV"; exit 1; }
	n=$((n+1))

	# ---- ⚠ 軸をまたぐ / 軸が貫く断面は **明示エラー** (黙って体積を返さない) ----
	#   ★ 文言はカーネルで違ってよい (occt は BRep が NotDone で失敗する) が、
	#     **値を返してはいけない**。そこだけを固定する。
	rm -rf "$D-a2rx"
	RX=$(SRAVA_CACHE_DIR="$D-a2rx" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
	print(\"VAL\", volume(revolve(translate(rect(2,2),[-1,0,5]),360)));" "$SRAVA" 2>&1)
	echo "$RX" | grep -qE "ERROR|error" || {
		echo "FAIL: 軸を貫く断面の revolve が黙って通った"; echo "$RX"; exit 1; }
	n=$((n+1))
	#   ★ 枠が既定でも同じ (従来経路)。⚠ manifold はここが黙っていたので 2026-09-13 に揃えた。
	rm -rf "$D-a2ry"
	RY=$(SRAVA_CACHE_DIR="$D-a2ry" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
	print(\"VAL\", volume(revolve(translate(rect(2,2),[-1,0,0]),360)));" "$SRAVA" 2>&1)
	echo "$RY" | grep -qE "ERROR|error" || {
		echo "FAIL: 軸をまたぐ断面の revolve が黙って通った (枠は既定)"; echo "$RY"; exit 1; }
	n=$((n+1))

	# ---- ★ 掃引方向が面の中を向くと **明示エラー** (#3518 の 5) ----
	#   ⚠ 2026-09-12 まではここが「体積 0 の立体」として黙って通っていた。
	#     境界の符号つき体積が厳密に打ち消し合うだけで、掃引された領域自体は体積を持つ
	#     (ミンコフスキー和)。⇒ 0 を返さずに断る。詳しくは test/srava_occt_sweep.sh。
	rm -rf "$D-a2sw"
	SW=$(SRAVA_CACHE_DIR="$D-a2sw" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
	print(\"VAL\", volume(extrude(rotate(rect(2,3),\"x\",90),1)));" "$SRAVA" 2>&1)
	echo "$SW" | grep -q "sweep direction lies inside" || {
		echo "FAIL: 面内方向への押し出しが黙って通った"; echo "$SW"; exit 1; }
	n=$((n+1))

	if [ "$OCCHK" = "space" ]; then
		# ---- ★★ #3526: **section は切った場所に断面を返す** ----
		# ⚠ メッシュ系だけ (この木の occt には section が無い — #3514 は dev-macmini-1 側)。
		#   ★ occt の section が入ったら **同じ項をここへ広げること** (occt の 2D は面そのものなので
		#     元から切った場所に居るはずだが、確かめていない)。
		# ⚠ 2026-09-13 まで断面は **黙って z=0 に戻って**いた (2D が置き場所を持てなかったので)。
		#   ⇒ 切った高さの情報が消え、extrude し直すと原点に戻った立体ができていた。
		# ★ 局所座標は動かさないので **面積は不変** (100)。変わるのは「どこに居るか」だけ。
		#   ⇒ 判定は **extrude した立体の bbox の z 下端が切った高さになること**。
		#     ⚠ 面積だけでは検出できない (置き場所を無視していても 100 になる)。
		rm -rf "$D-a2sc"
		SC=$(SRAVA_CACHE_DIR="$D-a2sc" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
		var b = box(10,10,10);
		var s5 = section(b,[0,0,5],[0,0,1])[0];
		print(\"VAL\", area(s5));
		print(\"VAL\", bbox(extrude(s5,1)));
		" "$SRAVA" 2>&1)
		SCG=$(echo "$SC" | sed -n 's/^VAL //p')
		SA=$(echo "$SCG" | sed -n 1p)
		SZ=$(echo "$SCG" | sed -n 2p | tr -d '[]' | cut -d, -f3)
		ok=$(awk -v g="$SA" -v t="$TOL" 'BEGIN{ d=g-100; if(d<0)d=-d; print (g!="" && d/100 <= t) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: z=5 断面の面積が $SA (期待 100)"; echo "$SC"; exit 1; }
		n=$((n+1))
		ok=$(awk -v g="$SZ" 'BEGIN{ d=g-5; if(d<0)d=-d; print (g!="" && d <= 1e-9) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: z=5 で切った断面を押し出した立体の z 下端が $SZ (期待 5) = 断面が切った場所に無い"; echo "$SC"; exit 1; }
		n=$((n+1))
	fi

	# ---- ★★ #3526: **同じ平面で軸の取り方だけが違う 2D は表し直して計算する** ----
	# ⚠ メッシュ系だけ (occt は 2D のブールをそもそも持たない)。
	#
	# ★ 枠は「原点 O + 軸 U,V」なので、**同じ平面に載っていても軸の取り方が違えば枠は一致しない**。
	#   例: rotate(R,"x",90) と rotate(R,"x",-90) はどちらも y=0 平面に居るが V が逆向き。
	#   ⇒ 2026-09-13 まではこれを **明示エラー**にしていた (枠の一致を要求していた)。
	#   いまは **先頭の被演算子の枠で表し直してから**計算する (幾何は 1 ミリも動かさない)。
	#
	# ★ 期待値の作り: R = [0,2]x[0,3] を x 軸に ±90 度回すと、y=0 平面で z∈[0,3] と z∈[-3,0] に
	#   並ぶ (触れ合うだけで重ならない) ⇒ union は 6+6 = 12。表し直しに失敗して片方が落ちれば
	#   6 になるので、**空振りしない**判定になっている。
	# ★ translate は「同じ平面の中で原点が動く」場合を通す (軸の反転とは別の経路)。
	#
	# ⚠⚠ #3533: **hull はここから外した**。2D の型が 2 つに割れ、規約③で「face3d が混ざれば
	#   答えは立体」になったので、@hull(A,B2)@ はもう *2D のブールの仲間* ではない。
	#   ★ 代わりに下で「**同一平面なら明示エラー**」を固定する。⚠ これは #3533 で見つけた
	#     欠陥の回帰でもある — cgal の退化検査 (面数 + 閉じているか) は *ほぼ共面* の点集合を
	#     取り逃がし、面 8 枚・閉じている・valid=1・体積 1.1e-15 の「立体」を **黙って返して
	#     いた** (area が 24 になる)。manifold だけが内部 ε で偶然捕まえていた。
	if [ "$OCCHK" = "space" ]; then
		rm -rf "$D-a2rp"
		RP=$(SRAVA_CACHE_DIR="$D-a2rp" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
		var R = rect(2,3);
		var A = rotate(R,\"x\",90);
		var B2 = rotate(R,\"x\",-90);
		print(\"VAL\", area(union(A,B2)));
		print(\"VAL\", area(combine(A,B2)));
		print(\"VAL\", area(intersection(A, translate(A,[1,0,0]))));
		print(\"VAL\", area(difference(A, translate(A,[1,0,0]))));
		print(\"VAL\", area(union(A,A)));
		" "$SRAVA" 2>&1)
		RPG=$(echo "$RP" | sed -n 's/^VAL //p')
		RPEXP="12 12 3 3 6"
		m=0
		set -- $RPEXP
		for g in $RPG; do
			m=$((m+1)); e="$1"; shift
			[ -n "$e" ] || { echo "FAIL: 再表現の出力が多い ($m 個目 = $g)"; echo "$RP"; exit 1; }
			ok=$(awk -v g="$g" -v e="$e" -v t="$TOL" 'BEGIN{
				d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1;
				print (g!="" && d/s <= t) ? 1 : 0 }')
			[ "$ok" = "1" ] || { echo "FAIL: 再表現の $m 個目が $g (期待 $e)"; echo "$RP"; exit 1; }
		done
		[ "$#" = 0 ] || { echo "FAIL: 再表現の出力が足りない ($m 個・残り $*)"; echo "$RP"; exit 1; }
		n=$((n+m))

		# ---- ⚠ **本当に別の平面**なら明示エラー。★ 文言が原因を名指しすること ----
		#   ⚠ cgal はここが 2026-09-13 まで「閉じた立体が触れ合っている」という **無関係な説明**を
		#     出していた (2D の枠で断ったのに 3D 用の文言が出ていた)。理由を配線して直した。
		for OPX in union intersection difference combine; do
			rm -rf "$D-a2rq"
			RQ=$(SRAVA_CACHE_DIR="$D-a2rq" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
			print(\"VAL\", area($OPX(rect(2,3), rotate(rect(2,3),\"x\",90))));" "$SRAVA" 2>&1)
			echo "$RQ" | grep -q "different planes" || {
				echo "FAIL: $OPX が別平面の 2D を断らない / 理由を名指ししない"; echo "$RQ"; exit 1; }
			n=$((n+1))
		done

		# ---- ★★ #3533: hull は **同一平面なら明示エラー・別平面なら立体** ----
		#   ⚠ 「黙って極薄の立体を返さない」が本題。負の対照は「別平面は通る」側。
		rm -rf "$D-a2hc"
		HC=$(SRAVA_CACHE_DIR="$D-a2hc" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
		var R = rect(2,3);
		print(\"VAL\", area(hull(rotate(R,\"x\",90), rotate(R,\"x\",-90))));" "$SRAVA" 2>&1)
		echo "$HC" | grep -q "on one plane" || {
			echo "FAIL: 同一平面の 2D の hull が明示エラーにならない (極薄の立体を黙って返した?)"
			echo "$HC"; exit 1; }
		n=$((n+1))
		rm -rf "$D-a2hd"
		HD=$(SRAVA_CACHE_DIR="$D-a2hd" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
		print(\"VAL\", volume(hull(rotate(rect(2,2),\"x\",90), rect(1,4))));" "$SRAVA" 2>&1)
		HDV=$(echo "$HD" | sed -n 's/^VAL //p')
		ok=$(awk -v g="$HDV" 'BEGIN{ d=g-6.6666666666666661; if(d<0)d=-d; print (g!="" && d <= 1e-9) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: 別平面の 2D の hull が立体にならない ($HDV・期待 6.66667)"; echo "$HD"; exit 1; }
		n=$((n+1))
	fi

    if [ "$OCCHK" = "oc" ]; then
	# ---- ★★ 面外へ出た 2D を **黙って射影しない** (#3518) が、**断りもしない** (#3536) ----
	#   #3518 ではここが *明示エラー* だった (行き先の mf-cross2d が z=0 に縛られていたので、
	#   射影する以外に渡す手が無かった)。#3526 で mf-cross2d が枠 (平面) を持つようになり、
	#   #3536 で polygonize が **面の平面を枠として引き継ぐ**ようになった。
	#   ⇒ 検査するのは「断ること」ではなく **射影していないこと**:
	#     ★★ 45 度傾けた rect(2,3) の面積は **6 のまま**。射影していたら 6·cos45 = 4.2426。
	#        ⚠ 面積が 6 であること自体が「射影していない」の証拠になる (#3518 の実測の裏返し)。
	#     ★ 持ち上げた 2D は **z=5 に居る**まま渡る (押し出すと z=5 から立つ)。
	#   ⚠ 曲面上の面は **従来どおり断る** — 折れ線にできないのは平面かどうかとは別の話。
	#   ⚠ polygonize は occt_mf.so が持ち manifold.so も要る。無いビルドでは飛ばす
	#     (「op が無い」と「拒否しない」を混ぜない)。
	#   ⚠⚠ 「op が無い」には **2 つの言い方**がある (2026-09-15 に occt 単独ビルドで踏んだ):
	#       誰かが名前を持っている → "no module can execute op 'polygonize' on (...)"
	#       **誰も持っていない**   → パーサが名前を知らないので "undefined variable: polygonize"
	#     片方だけ書くと、モジュールを絞ったビルドで **飛ばさずに赤くなる**。両方見る。
	rm -rf "$D-a2pg"
	PG="module(\"manifold.so\",{optional:1}); module(\"occt_mf.so\",{optional:1}); module(\"$SO\",{priority:99});module(\"geomutils.so\",{});"
	F=$(SRAVA_CACHE_DIR="$D-a2pg" SRAVA_SOURCE="$PG print(\"VAL\", area(polygonize(rect(2,3),0.01)));" "$SRAVA" 2>&1)
	case "$F" in
	*"no module can execute op 'polygonize'"*|*"undefined variable: polygonize"*)
		echo "  skip polygonize (occt_mf.so / manifold.so が無い)" ;;
	*)
		echo "$F" | grep -q "^VAL 6$" || { echo "FAIL: 平面の polygonize が 6 にならない"; echo "$F"; exit 1; }
		n=$((n+1))
		# ★★ 面外の 2D は **面積を保ったまま**渡る (射影なら 4.2426 / 6 以外なら射影の疑い)。
		for T in 'rotate(rect(2,3),"x",45)' 'translate(rect(2,3),[0,0,5])' \
		         'rotate(translate(rect(2,3),[0,0,5]),"y",30)'; do
			rm -rf "$D-a2pg2"
			G=$(SRAVA_CACHE_DIR="$D-a2pg2" SRAVA_SOURCE="$PG print(\"VAL\", area(polygonize($T,0.01)));" "$SRAVA" 2>&1)
			echo "$G" | grep -q "^VAL 6$" || {
				echo "FAIL: 面外の 2D ($T) の polygonize が 6 にならない (射影の疑い)"
				echo "$G"; exit 1; }
			n=$((n+1))
		done
		# ★ 置き場所そのものを見る — 持ち上げた 2D を渡して押し出すと **z=5 から**立つ。
		#   ⚠ 面積では検出できない (射影しても面積が変わらない置き方があるため)。
		rm -rf "$D-a2pg3"
		G=$(SRAVA_CACHE_DIR="$D-a2pg3" SRAVA_SOURCE="$PG
print(\"BOX\", bbox(extrude(polygonize(translate(rect(2,3),[0,0,5]),0.01),1)));" "$SRAVA" 2>&1)
		echo "$G" | grep -q "^BOX \[\[0,0,5\],\[2,3,6\]\]$" || {
			echo "FAIL: 持ち上げた 2D の polygonize が z=5 に居ない"; echo "$G"; exit 1; }
		n=$((n+1))
		# ⚠ 曲面上の面は **断ったまま** (平面かどうかとは別の理由)。
		rm -rf "$D-a2pg4"
		G=$(SRAVA_CACHE_DIR="$D-a2pg4" SRAVA_SOURCE="$PG
print(\"VAL\", area(polygonize(face_at(cylinder(1,3),[5,0,0]),0.01)));" "$SRAVA" 2>&1)
		echo "$G" | grep -q "curved surface" || {
			echo "FAIL: 曲面上の面を polygonize が受けた"; echo "$G"; exit 1; }
		n=$((n+1)) ;;
	esac
    fi
fi

# ==================================================================
# ★★ cgal / manifold 固有: **面外へ出す変換は断る** (#3518)
#   ⚠ ここは occt には当てない (あちらは面外へ置けるのが能力)。上の "oc" 節と **排他**。
#   あちらの 2D は XY の 2x2 + (tx,ty) しか使わないので、受けてしまうと面外成分を
#   黙って捨てて **射影した領域**を返す。同じ入力に 2 通りの値が出るので明示エラーにした。
# ==================================================================
if [ "$SPACE" != "1" ]; then
	# (a) 面外へ出す変換は **明示エラー**。
	#   ⚠ scale は matrix_scale が対角行列しか作らない (z 行が必ず (0,0,sz,0)) ので
	#     構造上発火しない。ここに並べると **空振りする項**になるので入れない。
	for T in 'rotate(R,"x",45)' 'rotate(R,[1,1,0],30)' 'translate(R,[0,0,5])' \
	         'mirror(R,[0,1,1])' 'transform(R,[1,0,0,0, 0,1,0,0, 0,0,1,5])'; do
		rm -rf "$D-a2pl"
		G=$(SRAVA_CACHE_DIR="$D-a2pl" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
		var R = rect(2,3); print(\"VAL\", area($T));" "$SRAVA" 2>&1)
		echo "$G" | grep -q "out of the z=0 plane" || {
			echo "FAIL: 面外へ出す変換 ($T) を黙って受けた"; echo "$G"; exit 1; }
		n=$((n+1))
	done

	# (b) ★★ 平面を平面へ写すものは **通らないといけない**。
	#   cos/sin は double 近似なので rotate("x",180) の m21 = sin(π) = 1.2246e-16 になる。
	#   ⇒ 検査を **厳密な 0** と比べる実装にすると、この正当な変換がここで落ちる。
	#   ★ 面積は剛体変換で不変なので、動いたことは probe (0.216) で見る。
	rm -rf "$D-a2pl2"
	K=$(SRAVA_CACHE_DIR="$D-a2pl2" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
	var R = rect(2,3);
	var P = box(0.6,0.6,0.6);
	print(\"VAL\", area(rotate(R,\"x\",180)));
	print(\"VAL\", volume(extrude(rotate(R,\"x\",180),1) &&& translate(P,[0.7,-1.8,0.2])));
	print(\"VAL\", area(rotate(R,\"y\",180)));
	print(\"VAL\", volume(extrude(rotate(R,\"y\",180),1) &&& translate(P,[-1.3,1.2,0.2])));
	print(\"VAL\", area(rotate(R,\"x\",360)));
	print(\"VAL\", area(mirror(R,\"z\")));
	print(\"VAL\", area(scale(R,[1,1,-1])));
	print(\"VAL\", area(translate(R,[1,2,0])));" "$SRAVA" 2>&1)
	KG=$(echo "$K" | sed -n 's/^VAL //p')
	KEXP="6 0.216 6 0.216 6 6 6 6"
	m=0
	set -- $KEXP
	for g in $KG; do
		m=$((m+1)); e="$1"; shift
		[ -n "$e" ] || { echo "FAIL: 平面内変換の出力が多い ($m 個目 = $g)"; echo "$K"; exit 1; }
		ok=$(awk -v g="$g" -v e="$e" -v t="$TOL" 'BEGIN{
			d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1;
			print (g!="" && d/s <= t) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: 平面内変換の $m 個目が $g (期待 $e ・丸め残りで断っていないか)"; echo "$K"; exit 1; }
	done
	[ "$#" = 0 ] || { echo "FAIL: 平面内変換の出力が足りない ($m 個・残り $*)"; echo "$K"; exit 1; }
	n=$((n+m))

	# (c) ★ 3D は素通りすること (2D の検査が 3D に漏れていないか)
	rm -rf "$D-a2pl3"
	K3=$(SRAVA_CACHE_DIR="$D-a2pl3" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
	print(\"VAL\", volume(rotate(box(2,3,1),\"x\",45)));
	print(\"VAL\", volume(translate(box(2,3,1),[0,0,5])));" "$SRAVA" 2>&1)
	echo "$K3" | sed -n 's/^VAL //p' | while read g; do
		[ "$(awk -v g="$g" 'BEGIN{d=g-6; if(d<0)d=-d; print (d<=1e-9)?1:0}')" = 1 ] || exit 1
	done || { echo "FAIL: 3D の変換に 2D の検査が漏れている"; echo "$K3"; exit 1; }
	n=$((n+2))
fi

# ---- ★★ #3554 段4: **xy 平面に帰着する transform は cross2d のまま** ----------------
#   規約① (transform 系は常に face3d) の例外。行列が z=0 平面を平面へ写すなら
#   @transform#xy@ の行が選ばれる。⇒ *型が変わったこと*を 2 つの独立な証拠で見る:
#     ① bbox の成分数   cross2d = 2 / face3d = 3
#     ② SVG に書けるか  規約④ で SVG は face3d を断る (型が効いていることの独立な証拠)
#   ⚠ この op だけ。translate / rotate / scale / mirror は段5。
#   ⚠ 判定は routing (マッチ関数) と計算本体で **同じ述語** (srava_affine::keeps_z_plane) を
#     使っている。別々に書くと「sig は cross2d と言っているのに face3d を返す」が起きる。
if [ "$SO" = "cgal.so" ] || [ "$SO" = "manifold.so" ]; then
	rm -rf "$D-x4"
	#   ⚠ 成分数を数える op は無いので **bbox の文字列から数える** ([[x,y]] = 2 / [[x,y,z]] = 3)。
	X4=$(SRAVA_CACHE_DIR="$D-x4" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
		var R = rect(2,3);
		print(\"N\", bbox(transform(R,[1,0,0,10, 0,1,0,0, 0,0,1,0])));
		print(\"N\", bbox(transform(R,[0,-1,0,0, 1,0,0,0, 0,0,1,0])));
		print(\"N\", bbox(transform(R,[1,0,0,0, 0,1,0,0, 0,0,1,5])));
		print(\"N\", bbox(transform(R,[1,0,0,0, 0,0,-1,0, 0,1,0,0])));" "$SRAVA" 2>&1 |
		sed -n 's/^N //p' | sed 's/^\[\[//; s/\].*//' | awk -F, '{printf "%d ", NF}')
	#     xy 平行移動  z 回転   z 移動(面外)  x 回転(面外)
	[ "$X4" = "2 2 3 3 " ] || { echo "FAIL: 段4 の型が [$X4] (期待 [2 2 3 3 ])"; exit 1; }

	#   ② SVG が独立の証拠: xy 内なら書けて、面外なら規約④ が断る
	#   ⚠ **cgal だけ** — manifold は SVG を書けない (export=stl,off,3mf,amf)。
	#     ⇒ ここを全カーネルに当てると「型は効いているのに SVG が無くて赤」になる
	#       (2026-09-19 に実際に踏んだ)。能力の差は *記述子が申告している*ので、それに従う。
	if [ "$SO" = "cgal.so" ]; then
	mkdir -p "$D-x4/out"
	SVG=$(SRAVA_CACHE_DIR="$D-x4" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
		export(\"$D-x4/out/xy.svg\", transform(rect(2,3),[1,0,0,10, 0,1,0,0, 0,0,1,0]));" "$SRAVA" 2>&1)
	[ -s "$D-x4/out/xy.svg" ] || { echo "FAIL: xy 内の transform が SVG に書けない"; echo "$SVG"; exit 1; }
	OUTS=$(SRAVA_CACHE_DIR="$D-x4" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
		export(\"$D-x4/out/z.svg\", transform(rect(2,3),[1,0,0,0, 0,1,0,0, 0,0,1,5]));" "$SRAVA" 2>&1)
	echo "$OUTS" | grep -q "placed in space" || {
		echo "FAIL: 面外の transform が SVG で断られていない (型が face3d になっていない)"; echo "$OUTS"; exit 1; }
	n=$((n+1))
	fi
	rm -rf "$D-x4"
	n=$((n+1))

	# ---- ★★ #3554 段5: translate / rotate / scale / mirror も同じ形 --------------------
	#   ⚠⚠ **routing の判定と計算本体の判定は一致していなければならない**。
	#     一致していないと「sig は face3d と言っているのに cross2d が返る」= 下流の型スタンプが
	#     嘘になる。2026-09-19 に実際に踏んだ: rotate は *軸と角度の両方*で平面を保つかが
	#     決まるのに、マッチ関数は **引数を 1 個ずつしか見られない** ので軸しか見られない。
	#     計算本体だけ行列で判定していたため @rotate(R,"x",180)@ が cross2d を返していた。
	#   ⇒ rotate は **軸 z のときだけ** 2D のまま (保守的だが嘘をつかない)。
	#     ★ 下の "rotate x 180" が **3 成分**であることが、その一致の検定になっている。
	rm -rf "$D-x5"
	X5=$(SRAVA_CACHE_DIR="$D-x5" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
		var R = rect(2,3);
		print(\"N\", bbox(translate(R,[5,1,0])));
		print(\"N\", bbox(translate(R,[0,0,2])));
		print(\"N\", bbox(rotate(R,\"z\",90)));
		print(\"N\", bbox(rotate(R,\"x\",180)));
		print(\"N\", bbox(rotate(R,\"x\",45)));
		print(\"N\", bbox(scale(R,[2,3,1])));
		print(\"N\", bbox(mirror(R,\"y\")));
		print(\"N\", bbox(mirror(R,[0,1,1])));" "$SRAVA" 2>&1 |
		sed -n 's/^N //p' | sed 's/^\[\[//; s/\].*//' | awk -F, '{printf "%d ", NF}')
	#  tr(xy) tr(z) rot(z90) rot(x180) rot(x45) scale mirror(y) mirror(斜め)
	[ "$X5" = "2 3 2 3 3 2 2 3 " ] || { echo "FAIL: 段5 の型が [$X5] (期待 [2 3 2 3 3 2 2 3 ])"; exit 1; }
	rm -rf "$D-x5"
	n=$((n+1))
fi

echo "AFFINE2D-OK $SO ($n checks)"

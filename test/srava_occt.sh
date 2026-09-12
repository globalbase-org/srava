#!/bin/sh
# OCCT (B-rep) モジュール (#3437 P5) の振る舞い回帰。
# $1 = srava 実行体。$2 = モード。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# モードが見ているもの:
#   exact    … ★**球が厳密**であること。OCCT の sphere は解析曲面 1 枚なので volume が
#              4/3*pi*r^3 ちょうどになる。メッシュ系 (内接多面体) とは原理的に一致しない
#              ので、kernel_agree には素で入れない — この差自体が結果。
#   steiner  … ★**offset が Steiner の公式と一致**すること。解析曲面を直接オフセットし、
#              稜に円筒パッチ・頂点に球パッチを生成する = Steiner を構成的に実行している、
#              という主張の検証。box(2,2,2) の厳密値 (V=8, A=24, M=6pi):
#                  V(d) = 8 + 24d + 6*pi*d^2 + (4/3)*pi*d^3
#                  d=0.1 -> 10.5926843494 / d=0.2 -> 13.5874925585
#              解析曲面を直接オフセットするので、真値と**多数桁で一致**する
#              (残差は交線の B-spline 近似とトレランス由来)。
#   tri      … ★出口 triangulate。**箱は無損失** (平面なので 8 ちょうど)、**球は deflection で
#              決まる誤差**を持ち、deflection を 1/10 にすると誤差も約 1/10 (1 次収束)。
#              ★頂点の溶接が要る: OCCT は Face ごとに独立した頂点配列を持つので、溶接しないと
#              閉じていない三角形スープになり下流が volume 0 を返す (実際に踏んだ)。
SRAVA="$1"
MODE="${2:-exact}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
OC='module("occt.so",{priority:99});'

case "$MODE" in
exact)
	rm -rf "$D-e"
	R=$(SRAVA_CACHE_DIR="$D-e" SRAVA_SOURCE="$OC print(\"R\", volume(sphere(1,32)));" "$SRAVA" 2>&1 | sed -n 's/^R //p')
	[ -n "$R" ] || { echo "FAIL: 値が出ない"; exit 1; }
	# 4/3*pi = 4.18879020478639... と 1e-12 以内で一致すること (内接多面体なら 4.09 前後になる)
	ok=$(awk -v r="$R" 'BEGIN{ e=4.1887902047863905; d=r-e; if(d<0)d=-d; print (d<1e-12) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 球の体積が $R (期待 4.18879020478639 = 厳密球)"; exit 1; }
	# 第 2 引数 (分割数) を変えても値が動かないこと = 近似していない証拠
	R2=$(SRAVA_CACHE_DIR="$D-e2" SRAVA_SOURCE="$OC print(\"R\", volume(sphere(1,4)));" "$SRAVA" 2>&1 | sed -n 's/^R //p')
	rm -rf "$D-e2"
	[ "$R" = "$R2" ] || { echo "FAIL: 分割数で値が変わった seg32=$R seg4=$R2 (近似している?)"; exit 1; }
	echo "OCCT-EXACT-OK $R (分割数に依存しない)" ;;
steiner)
	rm -rf "$D-s"
	OUT=$(SRAVA_CACHE_DIR="$D-s" SRAVA_SOURCE="$OC var b = box(2,2,2);
	  print(\"A\", volume(offset(b, 0.1)));
	  print(\"B\", volume(offset(b, 0.2)));" "$SRAVA" 2>&1)
	A=$(echo "$OUT" | sed -n 's/^A //p'); B=$(echo "$OUT" | sed -n 's/^B //p')
	if [ -z "$A" ] || [ -z "$B" ]; then echo "FAIL: 値が出ない A=$A B=$B"; echo "$OUT"; exit 1; fi
	ok=$(awk -v a="$A" -v b="$B" 'BEGIN{
		da=a-10.5926843494; if(da<0)da=-da; db=b-13.5874925585; if(db<0)db=-db;
		print (da<1e-6 && db<1e-6) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: offset が Steiner と一致しない d=0.1 -> $A (期待 10.5926843494) / d=0.2 -> $B (期待 13.5874925585)"; exit 1; }
	echo "OCCT-STEINER-OK d0.1=$A d0.2=$B" ;;
tri)
	# ★ #3452 (module() 明示ロード) 追従: cast("mf-mesh3d", …) の**出口の型を産む**
	#   manifold を明示ロードする (自動ロードが廃止されたため)。
	# ★★ akira-project #3452: triangulate は **occt_mf.so** (境界モジュール) が持つ。
	#   occt.so は B-rep だけを扱い、mesh の型は一切名乗らない。
	rm -rf "$D-t"
	#   ★ **occt を先に**読む順で回す = 順序依存の回帰。かつてこの順だと triangulate の出力
	#     (mf-mesh3d) を manifold が受け取れず "nfaces: needs a mesh" になっていた。
	#     原因は occt の codec が (MFM3, mf-mesh3d) の **reader** も名乗っていたこと
	#     (実体は ocMesh を作るので mf-mesh3d ではない) + reader_for が最初の一致で
	#     打ち切っていたこと。ocMesh ごと撤去したので、どちらの順でも通る。
	OUT=$(SRAVA_CACHE_DIR="$D-t" SRAVA_SOURCE="$OC module(\"manifold.so\",{}); module(\"occt_mf.so\",{});
	  print(\"BOX\",  volume(cast(\"mf-mesh3d\", triangulate(box(2,2,2), 0.01))));
	  print(\"C\",    volume(cast(\"mf-mesh3d\", triangulate(sphere(1,0), 0.01))));
	  print(\"F\",    volume(cast(\"mf-mesh3d\", triangulate(sphere(1,0), 0.001))));" "$SRAVA" 2>&1)
	BX=$(echo "$OUT" | sed -n 's/^BOX //p')
	C=$(echo "$OUT" | sed -n 's/^C //p'); F=$(echo "$OUT" | sed -n 's/^F //p')
	if [ -z "$BX" ] || [ -z "$C" ] || [ -z "$F" ]; then echo "FAIL: 値が出ない BOX=$BX C=$C F=$F"; echo "$OUT"; exit 1; fi
	# ★ 溶接が壊れると下流が「閉じていない」と見なして 0 を返す。まずそこを弾く。
	ok=$(awk -v c="$C" 'BEGIN{ print (c > 1) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 三角形化した球の体積が $C (0 付近なら頂点の溶接が壊れている)"; exit 1; }
	# 箱は平面だけなので三角形化しても厳密 (8)。
	ok=$(awk -v b="$BX" 'BEGIN{ d=b-8; if(d<0)d=-d; print (d<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 箱の三角形化が $BX (期待 8・平面なので無損失のはず)"; exit 1; }
	# 球は内接多面体なので必ず厳密値より小さく、deflection を細かくすると近づく。
	ok=$(awk -v c="$C" -v f="$F" 'BEGIN{ e=4.1887902047863905;
		dc=e-c; df=e-f; print (dc>0 && df>0 && df < dc/3) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 収束していない defl0.01 -> $C / defl0.001 -> $F (厳密 4.18879)"; exit 1; }
	echo "OCCT-TRI-OK box=$BX sphere(0.01)=$C sphere(0.001)=$F" ;;
prim)
	# ★ cylinder / torus も**厳密**であること。どちらも解析曲面なので分割数の概念が無く、
	#   volume は πr²h / 2π²Rr² とちょうど一致する。Face 数も B-rep の要点なので固定する
	#   (cylinder = 円筒 1 + 平面 2 = 3 / torus = トーラス面 1 枚 = 1)。
	rm -rf "$D-p"
	OUT=$(SRAVA_CACHE_DIR="$D-p" SRAVA_SOURCE="$OC
	  print(\"CV\", volume(cylinder(1,2,32)));   print(\"CF\", nfaces(cylinder(1,2,32)));
	  print(\"TV\", volume(torus(2,0.5,32)));    print(\"TF\", nfaces(torus(2,0.5,32)));" "$SRAVA" 2>&1)
	CV=$(echo "$OUT" | sed -n 's/^CV //p'); CF=$(echo "$OUT" | sed -n 's/^CF //p')
	TV=$(echo "$OUT" | sed -n 's/^TV //p'); TF=$(echo "$OUT" | sed -n 's/^TF //p')
	if [ -z "$CV" ] || [ -z "$TV" ]; then echo "FAIL: 値が出ない CV=$CV TV=$TV"; echo "$OUT"; exit 1; fi
	ok=$(awk -v c="$CV" -v t="$TV" 'BEGIN{
		ec = c - 6.283185307179586;  if(ec<0) ec=-ec;    # pi*1*1*2
		et = t - 9.869604401089358;  if(et<0) et=-et;    # 2*pi^2*2*0.5^2
		print (ec<1e-12 && et<1e-12) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 厳密でない cylinder=$CV (期待 6.283185307179586) torus=$TV (期待 9.869604401089358)"; exit 1; }
	# ★ Face 数 = 三角形数ではない。ここが崩れたら「解析曲面のまま持っている」が崩れている。
	[ "$CF" = "3" ] || { echo "FAIL: cylinder の Face 数が $CF (期待 3 = 円筒 1 + 平面 2)"; exit 1; }
	[ "$TF" = "1" ] || { echo "FAIL: torus の Face 数が $TF (期待 1 = トーラス面 1 枚)"; exit 1; }
	echo "OCCT-PRIM-OK cyl=$CV($CF 面) torus=$TV($TF 面)" ;;
edit)
	# ★ fillet / chamfer — **B-rep でしか厳密に書けない加工**。どちらも真値がある。
	#
	#  fillet: 直方体 (辺 a) の全稜を半径 r で丸めた形は、内側の直方体 (a-2r) を半径 r の
	#          ボールで Minkowski 和したものと**ちょうど一致する** → Steiner の公式が使える。
	#          a=2, r=0.3 → V = ai^3 + 6 ai^2 r + 3 pi ai r^2 + (4/3) pi r^3  (ai = a-2r = 1.4)
	#                         = 7.572619358586173
	#  chamfer: ★ 角の扱いに**二つの流儀**がある。
	#          (a) 稜の平面 3 枚がそのまま交わる … V = a^3 - 6 a d^2 + 6 d^3   = 7.082
	#          (b) 角にも平面を立てる           … V = a^3 - 6 a d^2 + (16/3) d^3 = 7.064
	#          **OCCT は (b)**。d=0.2 / 0.3 / 0.5 の 3 点で 15 桁一致することを確認済み
	#          (Face 数 26 = 元 6 + 稜 12 + 角 8 も (b) の裏づけ)。
	rm -rf "$D-ed"
	OUT=$(SRAVA_CACHE_DIR="$D-ed" SRAVA_SOURCE="$OC
	  print(\"FV\", volume(fillet(box(2,2,2), 0.3)));  print(\"FF\", nfaces(fillet(box(2,2,2), 0.3)));
	  print(\"HV\", volume(chamfer(box(2,2,2), 0.3))); print(\"HF\", nfaces(chamfer(box(2,2,2), 0.3)));
	  print(\"H5\", volume(chamfer(box(2,2,2), 0.5)));" "$SRAVA" 2>&1)
	FV=$(echo "$OUT" | sed -n 's/^FV //p'); FF=$(echo "$OUT" | sed -n 's/^FF //p')
	HV=$(echo "$OUT" | sed -n 's/^HV //p'); HF=$(echo "$OUT" | sed -n 's/^HF //p')
	H5=$(echo "$OUT" | sed -n 's/^H5 //p')
	if [ -z "$FV" ] || [ -z "$HV" ]; then echo "FAIL: 値が出ない FV=$FV HV=$HV"; echo "$OUT"; exit 1; fi
	ok=$(awk -v f="$FV" -v h="$HV" -v h5="$H5" 'BEGIN{
		ef = f - 7.572619358586173; if(ef<0) ef=-ef;       # Steiner (a=2, r=0.3)
		eh = h - 7.064;             if(eh<0) eh=-eh;       # a^3-6ad^2+(16/3)d^3, d=0.3
		e5 = h5 - 5.666666666666667; if(e5<0) e5=-e5;      # 同 d=0.5
		print (ef<1e-9 && eh<1e-9 && e5<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 真値と一致しない fillet=$FV (期待 7.572619358586173) chamfer(0.3)=$HV (期待 7.064) chamfer(0.5)=$H5 (期待 5.666666666666667)"; exit 1; }
	# 面の内訳: 元 6 + 稜 12 + 角 8 = 26。ここが変わったら角の流儀が変わった合図。
	[ "$FF" = "26" ] || { echo "FAIL: fillet の Face 数が $FF (期待 26 = 平面 6 + 円筒 12 + 球 8)"; exit 1; }
	[ "$HF" = "26" ] || { echo "FAIL: chamfer の Face 数が $HF (期待 26 = 元 6 + 稜 12 + 角 8)"; exit 1; }
	echo "OCCT-EDIT-OK fillet=$FV($FF 面) chamfer=$HV($HF 面)" ;;
step)
	# ★ STEP / .brep の往復。**解析曲面のまま外へ出せる**ことがこの型の実用価値なので、
	#   往復して volume と Face 数が保たれることを固定する (三角形化していたら両方崩れる)。
	# ★★ 併せて「export が agent を殺さないこと」の回帰でもある。OCCT の STEP ライタは
	#   既定で **stdout** に進捗を出すが、agent の stdout は pigwire そのものなので、
	#   黙らせないと **ファイルは正しく書けているのに agent が死ぬ** (2026-08-20 に踏んだ)。
	rm -rf "$D-st"; F="${D}-rt"
	rm -f "$F.step" "$F.brep"
	OUT=$(SRAVA_CACHE_DIR="$D-st" SRAVA_SOURCE="$OC
	  var f = fillet(box(2,2,2), 0.3);
	  print(\"SRC\", volume(f)); print(\"SRCF\", nfaces(f));
	  export(\"$F.step\", f); export(\"$F.brep\", f);" "$SRAVA" 2>&1)
	SRC=$(echo "$OUT" | sed -n 's/^SRC //p'); SRCF=$(echo "$OUT" | sed -n 's/^SRCF //p')
	if [ -z "$SRC" ]; then echo "FAIL: 元の値が出ない"; echo "$OUT"; exit 1; fi
	case "$OUT" in *"agent closed unexpectedly"*)
		echo "FAIL: export が agent を殺した (OCCT の診断出力が stdout=pigwire を壊している)"; exit 1 ;;
	esac
	[ -s "$F.step" ] || { echo "FAIL: STEP が書けていない"; exit 1; }
	[ -s "$F.brep" ] || { echo "FAIL: BREP が書けていない"; exit 1; }
	rm -rf "$D-st2"
	OUT2=$(SRAVA_CACHE_DIR="$D-st2" SRAVA_SOURCE="$OC
	  print(\"RS\", volume(import(\"$F.step\"))); print(\"RSF\", nfaces(import(\"$F.step\")));
	  print(\"RB\", volume(import(\"$F.brep\")));" "$SRAVA" 2>&1)
	RS=$(echo "$OUT2" | sed -n 's/^RS //p'); RSF=$(echo "$OUT2" | sed -n 's/^RSF //p')
	RB=$(echo "$OUT2" | sed -n 's/^RB //p')
	if [ -z "$RS" ] || [ -z "$RB" ]; then echo "FAIL: 読み戻せない RS=$RS RB=$RB"; echo "$OUT2"; exit 1; fi
	ok=$(awk -v s="$SRC" -v a="$RS" -v b="$RB" 'BEGIN{
		da=(a-s)/s; if(da<0) da=-da; db=(b-s)/s; if(db<0) db=-db;
		print (da<1e-12 && db<1e-12) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 往復で値が変わった 元=$SRC step=$RS brep=$RB"; exit 1; }
	[ "$RSF" = "$SRCF" ] || { echo "FAIL: 往復で Face 数が変わった 元=$SRCF step=$RSF (三角形化された?)"; exit 1; }
	rm -f "$F.step" "$F.brep"
	echo "OCCT-STEP-OK src=$SRC step=$RS brep=$RB faces=$SRCF" ;;
place)
	# ★ #3474 (2026-09-05): occt の基本立体の **置き場所** を閉形式で固定する。
	#   体積は平行移動で変わらないので、素の volume を比べる kernel_agree では
	#   置き場所の食い違いを検出できない。実際に 2 件見逃していた:
	#     ・box   … occt だけ原点中心 (他 5 カーネルは角が原点)
	#     ・prism … occt だけ原点中心 z=-h/2〜h/2 (cgal / manifold は z=0〜h)
	#   ⚠ occt の cylinder / cone / torus / sphere は **解析曲面**なので他カーネルと体積が
	#     構造的に違い、kernel_agree の表に入れられない ⇒ 位置はここで **閉形式**と突き合わせる
	#     (角が原点の box(10,10,10) で第 1 象限 = +++ 八分空間を切り取る)。
	OK=1
	chk() {   # chk <式> <期待値> <説明>。式が area(/volume( で始まらなければ volume() で包む
		rm -rf "$D-pl"
		case "$1" in area\(*|volume\(*) E="$1" ;; *) E="volume($1)" ;; esac
		V=$(SRAVA_CACHE_DIR="$D-pl" SRAVA_SOURCE="module(\"occt.so\",{priority:99});
		     print(\"VAL\", $E);" "$SRAVA" 2>&1 | sed -n 's/^VAL //p')
		ok=$(awk -v a="$V" -v b="$2" 'BEGIN{ if(a==""){print 0; exit} d=a-b; if(d<0)d=-d;
		      r=(b<0?-b:b); if(r<1e-12)r=1; print (d/r < 1e-9) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: $3 が $V (期待 $2)"; OK=0; }
	}
	# 角が原点: box(2,2,2) は [0,2]^3 なので [1,11]^3 との交差は 1^3
	chk 'box(2,2,2) &&& translate(box(10,10,10),[1,1,1])' 1 "box の角が原点"
	# prism は z=0〜h: 正六角形 (外接 1) の面積 3*sqrt(3)/2 の 1/4 × 高さ 2
	chk 'prism(6,2,1) &&& box(10,10,10)' 1.2990381056766580 "prism の底面が z=0"
	chk 'pyramid(6,2,1) &&& box(10,10,10)' 0.4330127018922193 "pyramid の底面が z=0"
	# 以下は原点中心。+++ 八分空間で切ると閉形式になる
	chk 'sphere(1,32) &&& box(10,10,10)'      0.5235987755982988  "sphere が原点中心 (pi/6)"
	chk 'cylinder(1,2,32) &&& box(10,10,10)'  0.7853981633974483  "cylinder が原点中心 (pi/4)"
	chk 'cone(1,2,32) &&& box(10,10,10)'      0.0654498469497874  "cone が原点中心 (pi/48)"
	chk 'torus(2,0.5,32) &&& box(10,10,10)'   1.2337005501361697  "torus が原点中心 (pi^2/8)"
	chk 'tetrahedron(1) &&& box(10,10,10)'    0.0962250448649376  "tetrahedron が原点中心"
	# ★ #3474 続き: 2D プリミティブ。circle は **厳密な円** なので閉形式 πr² と一致する
	#   (メッシュ系の内接正多角形とは構造的に違うので kernel_agree に入れられない)。
	chk 'area(circle(2))'  12.566370614359172  "circle が厳密 (pi*r^2)"
	chk 'area(rect(3,4))'  12                  "rect が角原点で w*h"
	chk 'area(ngon(6,2))'  10.392304845413264  "ngon が原点中心・外接半径 r"
	chk 'area(empty2d())'  0                   "empty2d が空集合"
	# ★ occt **単独で** 2D→3D が書けること (これができないのが元の欠落だった)
	chk 'volume(extrude(rect(3,4), 5))'  60    "occt 単独で 2D を押し出せる"
	[ "$OK" = "1" ] && echo "OCCT-PLACE-OK" ;;
fatal)
	# ★ OCCT が **例外で**失敗する経路が srava のエラーになること (2026-08-26)。
	#   球には seam 稜があるので op_fillet の「稜が 0 本」ガードを素通りし、
	#   BRepFilletAPI_MakeFillet の中で Standard_Failure が飛ぶ。
	#   ⚠⚠ **Standard_Failure は std::exception 派生ではない** (Standard_Transient 派生) ので、
	#     他モジュールと同じ catch (const std::exception&) では **素通りして agent が
	#     terminate → SIGABRT** になる。専用の catch が要る、というのがここの回帰。
	#   ★ 固定するのは OCCT の文言ではなく「**agent ごと死なない・理由がモジュール名 / op 名から
	#     始まる**」こと (OCCT が将来この形を通せるようになったら成功でも合格)。
	#   ★ #3475: 文言が "occt/fillet: ..." になった (どのカーネルが失敗したかを名乗る)。
	OK=1
	for EXPR in "fillet(sphere(1,20), 0.5)" "chamfer(sphere(1,20), 0.3)"; do
		rm -rf "$D-ft"
		OUT=$(SRAVA_CACHE_DIR="$D-ft" SRAVA_SOURCE="module(\"occt.so\",{priority:99});
		      print(volume($EXPR));" "$SRAVA" 2>&1)
		if echo "$OUT" | grep -qE 'closed unexpectedly|died with SIG'; then
			echo "FAIL: $EXPR で agent ごと死んだ (OCCT の例外を捕まえていない)"; echo "$OUT"; OK=0
		elif echo "$OUT" | grep -q 'ERROR'; then
			echo "$OUT" | grep -qE 'ERROR\[[^]]*\] occt/(fillet|chamfer):' || {
				echo "FAIL: $EXPR のエラーが 'occt/<op>:' で始まっていない"; echo "$OUT"; OK=0; }
			echo "  $EXPR -> $(echo "$OUT" | sed -n 's/.*ERROR\[[^]]*\] //p' | cut -c1-72)"
		else
			V=$(echo "$OUT" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')
			[ -n "$V" ] || { echo "FAIL: $EXPR でエラーも値も出ていない"; echo "$OUT"; OK=0; }
			echo "  $EXPR -> 成功 value=$V (OCCT が通せるようになった)"
		fi
	done
	# 正常系が壊れていないこと (ガードで包んだ経路が素通りしていないか)
	rm -rf "$D-fo"
	OUT2=$(SRAVA_CACHE_DIR="$D-fo" SRAVA_SOURCE="module(\"occt.so\",{priority:99});
	       print(volume(fillet(box(2,2,2), 0.3)));" "$SRAVA" 2>&1)
	V2=$(echo "$OUT2" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')
	ok=$(awk -v a="$V2" 'BEGIN{ d=a-7.5726193585861754; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 正常な fillet の体積が $V2 (期待 7.57262)"; echo "$OUT2"; OK=0; }
	rm -rf "$D-ft" "$D-fo"
	[ "$OK" = 1 ] && echo "OCCT-FATAL-OK" ;;
contact)
	# ★ #3490 (2026-09-06): **内部が交わらず境界だけで接する融合**が空洞を埋めないこと。
	#   症例は 2 球の対称差 (a---b) ||| (b---a)。2 つの三日月は **交線の円だけ**で接しており、
	#   融合がその接触面を「内部面」とみなして消すと、レンズが材料に化けて **xor が union の
	#   値になる** (真値 21.765592 に対して 25.019963)。
	#
	#   ⚠⚠ **面積では検出できない**。xor の境界は ∂a ∪ ∂b そのものなので、union に化けても
	#     area は 2*4*pi*r^2 = 56.548668 のまま**正しく見える**。体積かレンズの突きが要る。
	#
	#   ★ このモードは OCCT 側の欠陥を **静かな誤値ではなくテスト失敗**にするためにある。
	#     OCCT 7.9.3 (macOS/Homebrew) では 7 ケース全て正しいことを確認済み (2026-09-06)。
	#     #3490 を報告したビルドでは 1 番が 25.019963 / valid=0 になっていた
	#     ⇒ ここが落ちたら **まず OCCT のバージョンを疑う**。
	#
	#   閉形式 (r=1.5・中心間 d=sqrt(3)):
	#       V(球)  = 4/3*pi*1.5^3                    = 14.137166941154069
	#       V(lens)= pi/12*(4r+d)*(2r-d)^2           =  3.254370742068528
	#       xor    = 2*(V - Vlens)                   = 21.765592372748237  ★ ここが要点
	#       union  = 2V - Vlens                      = 25.019963156449542  ← 壊れると この値
	OK=1
	oc_chk() {   # oc_chk <式> <期待値> <説明> [許容相対誤差 (既定 1e-9)]
		rm -rf "$D-ct"
		T="${4:-1e-9}"
		V=$(SRAVA_CACHE_DIR="$D-ct" SRAVA_SOURCE="$OC $1" "$SRAVA" 2>&1 | sed -n 's/^VAL //p')
		ok=$(awk -v a="$V" -v b="$2" -v t="$T" 'BEGIN{ if(a==""){print 0; exit} d=a-b; if(d<0)d=-d;
		      r=(b<0?-b:b); if(r<1e-9)r=1; print (d/r < t) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: $3 が $V (期待 $2 ± 相対 $T)"; OK=0; }
	}
	# (1) 三日月 2 つ = 交線の円だけで接する ← #3490 の症例そのもの
	XOR='var a = sphere(1.5,32); var b = translate(sphere(1.5,32),[1,1,1]);
	     var x = (a --- b) ||| (b --- a);'
	oc_chk "$XOR print(\"VAL\", volume(x));" 21.765592372748237 "三日月 2 つの融合 (xor)"
	# ★ レンズの中を小球で突く。25.02 に化けているとここが小球まるごとになる
	oc_chk "$XOR print(\"VAL\", volume(x &&& translate(sphere(0.2,16),[0.5,0.5,0.5])));" \
	       0 "xor のレンズ内部が空 (材料になっていない)"
	oc_chk "$XOR print(\"VAL\", valid(x));" 1 "xor が正しい立体"
	# (2)-(4) 面 / 稜 / 頂点だけで接する 2 箱。どれも体積 2
	oc_chk 'print("VAL", volume(box(1,1,1) ||| translate(box(1,1,1),[1,0,0])));' 2 "面で接する 2 箱"
	oc_chk 'print("VAL", volume(box(1,1,1) ||| translate(box(1,1,1),[1,1,0])));' 2 "稜だけで接する 2 箱"
	oc_chk 'print("VAL", volume(box(1,1,1) ||| translate(box(1,1,1),[1,1,1])));' 2 "頂点だけで接する 2 箱"
	# (5) 1 点で外接する 2 球
	oc_chk 'print("VAL", volume(sphere(1,32) ||| translate(sphere(1,32),[2,0,0])));' \
	       8.377580409572782 "1 点で外接する 2 球"
	# (6) 中空の殻 = 内側シェルを void として持てること (表現能力の裏づけ)
	oc_chk 'print("VAL", volume(sphere(1.5,32) --- sphere(1.0,32)));' \
	       9.948376736367678 "中空の殻 (内側シェルが void)"
	# (7) n 項の経路 (SetArguments/SetTools) も同じ答えを出すこと
	# ⚠ n 項 (SetArguments/SetTools を 1 回の BOP へ) は二項の畳み込みと **同じ交差計算では
	#   ない**ので、下位桁が一致しない (実測で相対 1e-9 のずれ)。ここで見たいのは
	#   「レンズが材料に化けていないか」= 25 系の値になっていないかなので許容は 1e-7 で十分。
	oc_chk "$XOR print(\"VAL\", volume(union(a --- b, b --- a, translate(box(1,1,1),[10,10,10]))));" \
	       22.765592372748237 "n 項の融合でも xor が保たれる" 1e-7
	[ "$OK" = "1" ] && echo "OCCT-CONTACT-OK" ;;
*)
	echo "unknown mode: $MODE"; exit 1 ;;
esac

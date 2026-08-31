#!/bin/sh
# geogram モジュール (#3435 P3) の振る舞い回帰。
# $1 = srava 実行体。$2 = モード。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# モードが見ているもの:
#   solidify … ★#3435 の受け入れ条件 (#3445)。**自己交差した閉メッシュから内外を決め直す**。
#              これは cgal (corefinement が素通りして誤値) / manifold (同じ誤値) / nef
#              (SNC を組めない) のどれも持たない能力で、geogram を入れる質的な理由。
#              ⚠ チケット #3435 の note は期待値を 50.51 と書いているが、これは **誤り**
#                (#3445 で判明: 50.51 は「分割して union した」ときの継ぎ目が太った値)。
#                面が囲む本当の体積は **48.6088**。nef の solidify と独立実装で一致する。
#   mfcross  … mf (raw double "MFM3") を gg として読み、混成ブールが純 gg と同値になる。
#   cgcross  … ★cg (厳密有理数 "MESH") を gg として**昇格読み**する (2026-08-19)。
#              パーサを src/h/common/exact_wire.h へ切り出して manifold と共有した回帰。
#   warmroute… ★ GGM3 撤去の回帰。gg と mf が **同じ 4CC "MFM3"** を名乗る状態で、cold と warm で
#              routing が変わらない (HIT キャッシュ経由でも geogram が計算し続ける)。
#   fatal    … ★geogram の **致命エラーが srava のエラーになる** (agent がシグナルで死んで
#              「原因が読めない」形にならない)。2026-08-26 にピア報告のバグを切り分けて判明:
#              geogram の arrangement は radial sort に失敗すると
#              `Did not manage to sort a bundle` → geo_assert_not_reached → std::runtime_error。
#              ★ **並列区間 (ワーカースレッド) から投げるので gg_guard の catch を素通りし**、
#                terminate() → SIGABRT で agent ごと死ぬ。threads:1 なら呼び出しスレッドで
#                投げるので捕まえられ、geogram の理由がそのままエラー文に載る。
#              ⚠ **geogram が将来この形を解けるようになったら成功してよい**。ここが固定したいのは
#                値ではなく「**不透明に死なない**」こと。だから成功も合格とし、
#                "agent closed unexpectedly" だけを不合格にする。
#   arity    … ★#3436 P4。geogram は operand_bit で **本当に n 項**のブールを持つ。
#              module("geogram.so",{arity:k}) を掃引して、① 値が k に依らず同じ
#              ② 節点数 (= キャッシュ MISS 数) が k とともに単調に減る (k=n で中間ノードが 0)
#              を見る。★ 掃引はスクリプトを一切変えない 1 パラメータになっている。
SRAVA="$1"
MODE="${2:-solidify}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
GG='module("geogram.so",{priority:99});'
MF='module("manifold.so",{priority:50});'

case "$MODE" in
solidify)
	# tube は geogram が持たない op なので manifold に作らせ (priority 50 > cgal 20)、
	# cast で gg へ渡す (mf の値を gg として読む。wire 形式 "MFM3" は共有)。
	SELFX='tube([[[0,0,0],0.8],[[10,0,0],0.8],[[10,0,2],0.8],[[0,0,2],0.8],[[0,0,4],0.8],[[5,0,4],0.8],[[5,0,-2],0.8]], 12)'
	rm -rf "$D-a"
	OUT=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="$MF $GG
	      var g = cast(\"gg-mesh3d\", $SELFX);
	      print(\"BEFORE\", volume(g));
	      print(\"AFTER\",  volume(solidify(g)));" "$SRAVA" 2>&1)
	B=$(echo "$OUT" | sed -n 's/^BEFORE //p')
	A=$(echo "$OUT" | sed -n 's/^AFTER //p')
	# ① 前提: 自己交差したままだと重なりを二重に数える (52.0164)。
	ok=$(awk -v a="$B" 'BEGIN{ d=a-52.016415536710625; if(d<0)d=-d; print (a!="" && d<1e-6) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 自己交差のままの体積が $B (期待 52.0164 = 二重計上)"; echo "$OUT"; exit 1; }
	# ② ★本題: 内外を決め直すと 48.6088 (nef の solidify と一致する独立実装)。
	ok=$(awk -v a="$A" 'BEGIN{ d=a-48.608763289596638; if(d<0)d=-d; print (a!="" && d<1e-6) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: solidify 後の体積が $A (期待 48.6088)"; echo "$OUT"; exit 1; }
	# ③ 健全な立体は不変。
	rm -rf "$D-b"
	OUT2=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="$GG print(\"BOX\", volume(solidify(box(2,2,2))));" "$SRAVA" 2>&1)
	V=$(echo "$OUT2" | sed -n 's/^BOX //p')
	ok=$(awk -v a="$V" 'BEGIN{ d=a-8; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 健全な箱の solidify が $V (期待 8)"; echo "$OUT2"; exit 1; }
	echo "GEOGRAM-SOLIDIFY-OK before=$B after=$A" ;;
mfcross)
	# mf が作った mesh ("MFM3") を gg が**昇格読み**し、混成ブールが純 mf と同値になること。
	# ★ mf 側のオペランドは **tube** で作る: geogram は tube を持たないので、geogram が priority 99
	#   でも routing は manifold へ行く (mf の box を作らせようとしても gg が勝ってしまうため)。
	TUBE='tube([[[0,0,0],0.6],[[4,0,0],0.6]], 12)'
	rm -rf "$D-c" "$D-d"
	# 混成: tube=mf-mesh3d / box=gg-mesh3d (形式はどちらも "MFM3") → gg の sig の
	# (mf-mesh3d, gg-mesh3d) 行で gg が計算する。★型が違っても形式は同じ、という状態の回帰でもある
	MIX=$(SRAVA_CACHE_DIR="$D-c" SRAVA_SOURCE="$MF $GG
	      print(\"V\", volume($TUBE ||| box(1,1,1)));" "$SRAVA" 2>&1 | sed -n 's/^V //p')
	# 参照: 同じ形を **純 manifold** で作る
	REF=$(SRAVA_CACHE_DIR="$D-d" SRAVA_SOURCE="module(\"manifold.so\",{priority:99});
	      print(\"V\", volume($TUBE ||| box(1,1,1)));" "$SRAVA" 2>&1 | sed -n 's/^V //p')
	if [ -z "$MIX" ] || [ -z "$REF" ]; then echo "FAIL: 値が出ない mixed=$MIX ref=$REF"; exit 1; fi
	ok=$(awk -v a="$REF" -v b="$MIX" 'BEGIN{ d=a-b; if(d<0)d=-d; s=(a<0?-a:a); if(s<1)s=1;
	                                          print (d <= 1e-9*s) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 混成が純 mf と違う ref=$REF mixed=$MIX"; exit 1; }
	echo "GEOGRAM-MFCROSS-OK $MIX" ;;
cgcross)
	# ★ cg→gg 昇格読み (2026-08-19)。cgal の厳密メッシュ "MESH" (有理数文字列) を geogram が
	#   double 化して読む。パーサは src/h/common/exact_wire.h = manifold と同じ実体なので、
	#   geogram.so は CGAL をリンクしない。
	#   壊れていたときの症状: cast("gg-mesh3d", cgMesh) が
	#     "cannot convert format 'MESH' ... to any of [gg-mesh3d]" で落ちる。
	CG='module("cgal.so",{priority:99});'
	rm -rf "$D-g" "$D-h" "$D-i"
	# ① ★忠実性: 球は共通生成器 (common/geodesic.h) なので cgal も geogram も **同じ double 座標**を
	#   持つ。cgal はそれを厳密有理数として書き、geogram が読み戻す → 往復が無損失なら
	#   体積・頂点数・面数が **純 geogram と完全一致**するはず (許容誤差ではなく一致を要求する)。
	VIA=$(SRAVA_CACHE_DIR="$D-g" SRAVA_SOURCE="$CG $GG
	      var g = cast(\"gg-mesh3d\", sphere(1,32));
	      print(\"R\", volume(g), nverts(g), nfaces(g));" "$SRAVA" 2>&1 | sed -n 's/^R //p')
	PURE=$(SRAVA_CACHE_DIR="$D-h" SRAVA_SOURCE="module(\"geogram.so\",{priority:99});
	      var g = sphere(1,32);
	      print(\"R\", volume(g), nverts(g), nfaces(g));" "$SRAVA" 2>&1 | sed -n 's/^R //p')
	if [ -z "$VIA" ] || [ -z "$PURE" ]; then echo "FAIL: 値が出ない via=$VIA pure=$PURE"; exit 1; fi
	[ "$VIA" = "$PURE" ] || { echo "FAIL: cg 経由 [$VIA] が純 gg [$PURE] と一致しない"; exit 1; }
	# ② ★2026-08-25 (eabd8d3) の設計変更に追従: solidify は当時 sig に (cg-mesh3d) 行を持ち
	#   geogram (double 化) が拾っていたが、「厳密入力の降格は cast のみが担う」方針 (約束②) の
	#   もと geogram の solidify から (cg-mesh3d) 行が削除され、代わり nef に
	#   (cg-mesh3d)->NF_TYPE が追加された (精度クラスを保存する側= nef へ振る)。
	#   自己交差 tube を cgal に作らせ、nef が内外を決め直して 48.6088 (mf 経由・geogram と
	#   独立に一致する値) になることを検証する (旧テストは geogram 経由を見ていたが対象が
	#   nef に変わっただけで、狙い「cg 入力の solidify が二重計上を正しく解消する」は同じ)。
	SELFX2='tube([[[0,0,0],0.8],[[10,0,0],0.8],[[10,0,2],0.8],[[0,0,2],0.8],[[0,0,4],0.8],[[5,0,4],0.8],[[5,0,-2],0.8]], 12)'
	OUT3=$(SRAVA_CACHE_DIR="$D-i" SRAVA_SOURCE="$CG $GG module(\"nef_hybrid.so\",{});
	      var t = $SELFX2;
	      print(\"BEFORE\", volume(t));
	      print(\"AFTER\",  volume(solidify(t)));" "$SRAVA" 2>&1)
	B3=$(echo "$OUT3" | sed -n 's/^BEFORE //p')
	A3=$(echo "$OUT3" | sed -n 's/^AFTER //p')
	# cgal (厳密) は自己交差を素通りして重なりを二重計上する = 52.0164。
	ok=$(awk -v a="$B3" 'BEGIN{ d=a-52.016415536710625; if(d<0)d=-d; print (a!="" && d<1e-6) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: cgal の自己交差体積が $B3 (期待 52.0164)"; echo "$OUT3"; exit 1; }
	ok=$(awk -v a="$A3" 'BEGIN{ d=a-48.608763289596638; if(d<0)d=-d; print (a!="" && d<1e-6) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: cg 入力の solidify が $A3 (期待 48.6088)"; echo "$OUT3"; exit 1; }
	echo "GEOGRAM-CGCROSS-OK sphere=[$VIA] solidify=$A3" ;;
warmroute)
	# ★ 2026-08-19 (GGM3 撤去の回帰): geogram は manifold と **同じ 4CC "MFM3"** を名乗る
	#   (wire レイアウトが同一なので、形式が同じものに別の 4CC を与えない)。この状態で
	#   **cold と warm で routing が変わらない**ことを固定する。
	#   壊れていたときの症状: HIT したキャッシュの型を 4CC から引き直す (先勝ちで mf-mesh3d に
	#   なる) ため、warm では volume が **manifold** へ流れて厳密値ちょうどになる。cold は最終桁がずれる
	#   なので、★答えが正しく見えたまま計算したモジュールだけが変わる。
	#   同じ穴が「モジュールの型リストを codec_tags の 4CC から作る」側にもあった
	#   (pigf_module_type_list → codecs の writer 行から導く形へ・ABI v11)。
	rm -rf "$D-e"
	# ① cold: box を geogram に作らせる (priority 99)。値は使わず cache を残すのが目的。
	C=$(SRAVA_CACHE_DIR="$D-e" SRAVA_SOURCE="$MF $GG print(\"N\", nverts(box(2,2,2)));" "$SRAVA" 2>&1 | sed -n 's/^N //p')
	[ -n "$C" ] || { echo "FAIL: cold が値を返さない"; exit 1; }
	# ② warm: 同じ cache dir で volume を要求 → box は HIT・volume だけ MISS。
	W=$(SRAVA_CACHE_DIR="$D-e" SRAVA_SOURCE="$MF $GG print(\"V\", volume(box(2,2,2)));" "$SRAVA" 2>&1 | sed -n 's/^V //p')
	[ -n "$W" ] || { echo "FAIL: warm が値を返さない"; exit 1; }
	# ③ cold で同じ式を最初から計算した値 (= geogram が計算した値) と一致すること。
	rm -rf "$D-f"
	R=$(SRAVA_CACHE_DIR="$D-f" SRAVA_SOURCE="$MF $GG print(\"V\", volume(box(2,2,2)));" "$SRAVA" 2>&1 | sed -n 's/^V //p')
	[ "$W" = "$R" ] || { echo "FAIL: warm=$W が cold=$R と違う (HIT 経由で別モジュールへ流れている)"; exit 1; }
	# ④ ★ geogram は体積を発散定理の double 和で出すので box(2,2,2) は 8 に**ならない**。
	#   8 ちょうどなら manifold/cgal が計算した = routing が壊れている、と判別できる。
	[ "$W" != "8" ] || { echo "FAIL: warm の体積が厳密に 8 = geogram 以外が計算している"; exit 1; }
	ok=$(awk -v a="$W" 'BEGIN{ d=a-8; if(d<0)d=-d; print (a!="" && d<1e-12) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: warm の体積 $W が 8 から離れすぎ (別の壊れ方)"; exit 1; }
	echo "GEOGRAM-WARMROUTE-OK warm=$W cold=$R" ;;
arity)
	# ★ #3436 P4: n 項ブールの掃引。8 個の箱を union し、arity だけを変える。
	#   期待: 値は不変 / union 節点数は 7 (k=2) → 4 (3) → 3 (4) → 1 (8) と単調減。
	SRC=''
	i=1
	while [ $i -le 8 ]; do
		SRC="$SRC v[$((i-1))] = cast(\"gg-mesh3d\", translate(box(2,2,2),[$i,0,0]));"
		i=$((i+1))
	done
	PREV=999
	OK=1
	for K in 2 3 4 8; do
		rm -rf "$D-k$K"
		OUT=$(SRAVA_CACHE_DIR="$D-k$K" SRAVA_SOURCE="module(\"geogram.so\",{priority:99,arity:$K}); var v=[]; $SRC print(volume(union(v)));" "$SRAVA" 2>&1)
		V=$(echo "$OUT" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')
		M=$(echo "$OUT" | sed -n 's/.*cache: [0-9]* hit(s), \([0-9]*\) miss.*/\1/p')
		echo "  arity=$K value=$V miss=$M"
		case "$V" in 36|36.0|36.000*) ;; *) echo "FAIL: arity=$K の体積が 36 でない ($V)"; OK=0;; esac
		if [ -z "$M" ] || [ "$M" -ge "$PREV" ]; then
			echo "FAIL: arity=$K で節点数が減っていない (miss=$M >= $PREV)"; OK=0
		fi
		PREV=$M
	done
	rm -rf "$D-k2" "$D-k3" "$D-k4" "$D-k8"
	[ "$OK" = 1 ] && echo "GEOGRAM-ARITY-OK" ;;
fatal)
	# ★ geogram の radial sort が解けない既知の形 (2026-08-26 に最小化)。
	#   条件は 3 つそろったとき: ① 2 つのオペランドの箱が **同じ x 幅** = 側面が同一平面
	#   ② 球がその面を貫く (r=1.3 > 0.5) ③ 球の分割が細かい (seg>=50。seg=20 なら通る)。
	#   ⇒ 同一平面上に載った円い継ぎ目のまわりで束を放射順に並べられない。
	BAD='(box(1,4,3) ||| sphere(1.3, 200)) ||| (box(1,2,2) ||| sphere(1.3, 200))'
	OK=1

	# ---- ① threads:1 = 呼び出しスレッドで投げるので **モジュールが捕まえられる** ----
	#   ここが固定したい本体: geogram の理由が srava のエラー文にそのまま載ること。
	rm -rf "$D-f1"
	OUT1=$(SRAVA_CACHE_DIR="$D-f1" SRAVA_SOURCE="module(\"geogram.so\",{priority:99,threads:1});
	       print(volume($BAD));" "$SRAVA" 2>&1)
	if echo "$OUT1" | grep -q 'closed unexpectedly'; then
		echo "FAIL: threads:1 で agent ごと死んだ (モジュールの catch が効いていない)"
		echo "$OUT1"; OK=0
	elif echo "$OUT1" | grep -q 'ERROR'; then
		# エラーなら op の名前で始まり、geogram の言い分が載っていること。
		echo "$OUT1" | grep -q 'ERROR.*union:' || {
			echo "FAIL: threads:1 のエラーが 'union: <geogram の理由>' の形でない"; echo "$OUT1"; OK=0; }
		echo "  threads:1 -> $(echo "$OUT1" | sed -n 's/.*ERROR\[[^]]*\] //p' | cut -c1-70)"
	else
		# ★ geogram が将来この形を解けるようになったら成功でよい (固定したいのは値ではない)。
		V=$(echo "$OUT1" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')
		[ -n "$V" ] || { echo "FAIL: threads:1 でエラーも値も出ていない"; echo "$OUT1"; OK=0; }
		echo "  threads:1 -> 成功 value=$V (geogram が解けるようになった)"
	fi

	# ---- ② 既定 (op 内並列 ON) = geogram は **ワーカースレッドから投げる** ----
	#   モジュール側の catch は素通りし agent は SIGABRT で死ぬ。それでも **理由は読める**:
	#   ptsErrSink が agent の stderr を溜めており、pigfAgent がエラー文に載せるため
	#   (2026-08-26。従来は ts2System の efd が nullptr = ts2IOdevNull へ捨てられていた)。
	#   ★ 固定するのは geogram の文言ではなく「**srava が agent の stderr を出す**」こと。
	rm -rf "$D-fd"
	OUT2=$(SRAVA_CACHE_DIR="$D-fd" SRAVA_SOURCE="module(\"geogram.so\",{priority:99});
	       print(volume($BAD));" "$SRAVA" 2>&1)
	if echo "$OUT2" | grep -q 'closed unexpectedly'; then
		echo "FAIL: 既定で原因不明のまま落ちた (agent closed unexpectedly)"; echo "$OUT2"; OK=0
	elif echo "$OUT2" | grep -q 'ERROR'; then
		echo "$OUT2" | grep -q 'agent stderr:' || {
			echo "FAIL: 既定のエラーに agent の stderr が載っていない (ptsErrSink が効いていない)"
			echo "$OUT2"; OK=0; }
		echo "  既定    -> $(echo "$OUT2" | sed -n 's/.*ERROR\[[^]]*\] //p' | cut -c1-70)"
	else
		V=$(echo "$OUT2" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')
		[ -n "$V" ] || { echo "FAIL: 既定でエラーも値も出ていない (沈黙ハングの疑い)"; echo "$OUT2"; OK=0; }
		echo "  既定    -> 成功 value=$V (geogram が解けるようになった)"
	fi

	rm -rf "$D-f1" "$D-fd"
	[ "$OK" = 1 ] && echo "GEOGRAM-FATAL-OK" ;;
*)
	echo "unknown mode: $MODE"; exit 1 ;;
esac

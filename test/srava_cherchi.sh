#!/bin/sh
# cherchi モジュール (#3438 P6) の振る舞い回帰。
# $1 = srava 実行体。$2 = モード。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# モードが見ているもの:
#   bool     … 二項ブールの値。IRMB は二項も **n 項の 2 ラベル版**として解くので、ここが
#              壊れると n 項も壊れている (共通の道を通る)。
#   contact  … ★ 既知の限界 (面でちょうど接する配置で誤った値) を **可視化**する。
#              ⚠ この形は結果に境界辺が残らないので、下の検査 (guard) では捕まえられない。
#   guard    … ★ 多重に重なった n 項 (上流が壊れる配置) も **エラーになる** こと。
#              以前は **実行のたびに違う誤った値**が返っていた (2026-08-26 に検査を追加)。
#   arity    … ★#3436 P4。cherchi は label (bitset<32>) で **素で n 項**なので、
#              module("cherchi.so",{arity:k}) の掃引で ① 値が k に依らず同じ
#              ② 節点数 (= キャッシュ MISS 数) が k とともに単調に減る、を見る。
#              geogram と同じ検査 = **2 つの独立実装で同じ性質を固定する**。
#   mfcross  … mf ("MFM3") を ch として読み、混成ブールが純 mf と同値になる。
#              ★ wire 形式が同一 (4CC 共有) なので変換は起きない、という状態の回帰。
#   cgcross  … cg (厳密有理数 "MESH") からの **昇格読み** (cast("ch-mesh3d", …))。
#              パーサは src/h/common/exact_wire.h = manifold/geogram と同じ実体なので、
#              cherchi.so は CGAL をリンクしない。
#
# ⚠⚠ **上流の限界 (2026-08-26 に最小再現)**: オペランドの配置が退化していると IRMB は壊れる。
#   ① 面でちょうど接する (体積の重なりがちょうど 0): 箱 [0,2]^3 と [2,4]×[0,2]^2 の union は
#      16 のはずが 18.6667 になっていた (共有壁が両側から残り、閉曲面でなくなる)。
#      ★ 隙間をごくわずかずらすと**どちらも正しい** = 限界は「測度 0 の接触」に局在。
#   ② 多重に重なる n 項: 幅 2 の箱を 0.7 刻みで 8 個 (3 重に重なる領域がある) union すると、
#      デバッグビルドでは上流の assert が落ち、**Release では実行のたびに違う誤った値**が出ていた。
#   ⇒ ★ srava 側で **結果が閉じた向き付き曲面か**を検査し、どちらも **エラー**にした
#      (modules/cherchi/c++/chMesh.cpp の ch_is_closed_oriented)。**黙って誤らない**。
#   ⇒ ベンチのモデルは **一般の位置**で書く。★★ 引き金は「退化した配置」であって入力の質ではない:
#      **同じ形を軸に沿って平行移動する**と、移動方向に平行な面が同一平面になって踏む
#      (上流のバイナリで確認: 軸に沿った 2 球は 8 回中 1 回誤り / 一般の位置へずらすと 8/8 一致)。
#   ⚠ 入力側の要件 (manifold / watertight / 自己交差なし / 向き付き) は各オペランドとも
#     満たしているので、これは「入力が不正」ではなく **オペランドどうしの配置**の問題。
#
# ⚠ solidify は **持たない**。IRMB の分類は label 単位なので、自己交差した 1 枚のメッシュには
#   効かない (重なる 2 箱を 1 ラベルで union させても内側の面が落ちない)。#3445 の能力は
#   geogram / nef が持つ。詳細は modules/cherchi/h/ch/c++/chMesh.h 冒頭。
SRAVA="$1"
MODE="${2:-bool}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
CH='module("cherchi.so",{priority:99});'
MF='module("manifold.so",{priority:50});'

# 期待値との突き合わせ (相対誤差)。$1=実測 $2=期待 $3=許容相対誤差
near() {
	awk -v a="$1" -v b="$2" -v t="$3" 'BEGIN{
		if (a == "") { print 0; exit }
		d = a - b; if (d < 0) d = -d; s = (b < 0 ? -b : b); if (s < 1) s = 1;
		print (d <= t * s) ? 1 : 0 }'
}

case "$MODE" in
bool)
	# 箱 A=[0,2]^3 と B=[1,3]^3 → union 15 / intersection 1 / difference 7。
	rm -rf "$D-b"
	OUT=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="$CH
	      var a = box(2,2,2);
	      var b = translate(box(2,2,2),[1,1,1]);
	      print(\"U\", volume(a ||| b));
	      print(\"I\", volume(a &&& b));
	      print(\"D\", volume(a --- b));" "$SRAVA" 2>&1)
	U=$(echo "$OUT" | sed -n 's/^U //p')
	I=$(echo "$OUT" | sed -n 's/^I //p')
	S=$(echo "$OUT" | sed -n 's/^D //p')
	[ "$(near "$U" 15 1e-9)" = 1 ] || { echo "FAIL: union が $U (期待 15)"; echo "$OUT"; exit 1; }
	[ "$(near "$I" 1  1e-9)" = 1 ] || { echo "FAIL: intersection が $I (期待 1)"; echo "$OUT"; exit 1; }
	[ "$(near "$S" 7  1e-9)" = 1 ] || { echo "FAIL: difference が $S (期待 7)"; echo "$OUT"; exit 1; }
	# ★ export の申告 (export_exts = "off,stl,obj") が**実態と一致している**ことも見る。
	#   「申告はあるが書けない / 申告に無いのに黙って別形式で書く」を防ぐ (記述子の嘘の回帰)。
	mkdir -p "$D-b/out"
	OUT2=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="$CH
	       var m = box(2,2,3);
	       export(\"$D-b/out/a.off\", m); export(\"$D-b/out/a.stl\", m); export(\"$D-b/out/a.obj\", m);" "$SRAVA" 2>&1)
	for e in off stl obj; do
		[ -s "$D-b/out/a.$e" ] || { echo "FAIL: export $e が書けていない"; echo "$OUT2"; exit 1; }
	done
	# OFF の 2 行目は「頂点数 面数 0」= 箱なので 8 12 0。
	H=$(sed -n '2p' "$D-b/out/a.off")
	[ "$H" = "8 12 0" ] || { echo "FAIL: OFF の見出しが [$H] (期待 8 12 0)"; exit 1; }
	# 申告していない拡張子は **黙って別形式で書かず**エラーになること。
	OUT3=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="$CH export(\"$D-b/out/a.ply\", box(1,1,1));" "$SRAVA" 2>&1)
	[ -e "$D-b/out/a.ply" ] && { echo "FAIL: 申告していない .ply を書いてしまった"; exit 1; }
	echo "$OUT3" | grep -qi "ERROR" || { echo "FAIL: 未対応拡張子がエラーにならない"; echo "$OUT3"; exit 1; }
	rm -rf "$D-b"
	echo "CHERCHI-BOOL-OK union=$U inter=$I diff=$S export=off/stl/obj" ;;
arity)
	# ★ #3436 P4: 8 個の箱を union し、arity だけを変える。
	#   期待: ① 値が k に依らず同じ ② 節点数 (= キャッシュ MISS 数) が k とともに単調に減る。
	# ★★ 箱は **一般の位置**に置く (x だけでなく y/z にもずらす)。理由 = 同じ形を **軸に沿って**
	#   並べると、平行移動の向きに平行な面が **ちょうど同一平面**になり、上流の退化条件を踏む
	#   (2026-08-27 に上流のバイナリで確認: 軸に沿った 2 球は 8 回中 1 回誤り / 一般の位置は 8/8 一致)。
	#   ⇒ 絶対値は式で書けなくなるので、**「k を変えても同じ値」**を条件にする
	#     (このテストが本当に見たいのはそれ。値そのものは bool モードが見ている)。
	SRC=''
	i=0
	while [ $i -le 7 ]; do
		SRC="$SRC v[$i] = translate(box(2,2,2),[$(awk -v i=$i 'BEGIN{printf "%g", i*1.5}'),$(awk -v i=$i 'BEGIN{printf "%g", i*0.13}'),$(awk -v i=$i 'BEGIN{printf "%g", i*0.07}')]);"
		i=$((i+1))
	done
	PREV=999
	REF=''
	OK=1
	for K in 2 3 4 8; do
		rm -rf "$D-k$K"
		OUT=$(SRAVA_CACHE_DIR="$D-k$K" SRAVA_SOURCE="module(\"cherchi.so\",{priority:99,arity:$K}); var v=[]; $SRC print(volume(union(v)));" "$SRAVA" 2>&1)
		V=$(echo "$OUT" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')
		M=$(echo "$OUT" | sed -n 's/.*cache: [0-9]* hit(s), \([0-9]*\) miss.*/\1/p')
		echo "  arity=$K value=$V miss=$M"
		[ -n "$V" ] || { echo "FAIL: arity=$K で値が出ない"; echo "$OUT"; OK=0; }
		if [ -z "$REF" ]; then REF=$V
		else
			[ "$(near "$V" "$REF" 1e-9)" = 1 ] || { echo "FAIL: arity=$K の値 $V が arity=2 の $REF と違う"; OK=0; }
		fi
		if [ -z "$M" ] || [ "$M" -ge "$PREV" ]; then
			echo "FAIL: arity=$K で節点数が減っていない (miss=$M >= $PREV)"; OK=0
		fi
		PREV=$M
	done
	rm -rf "$D-k2" "$D-k3" "$D-k4" "$D-k8"
	[ "$OK" = 1 ] && echo "CHERCHI-ARITY-OK value=$REF (k に依らず一定)" ;;
contact)
	# ★ 既知の限界: 面でちょうど接する 2 立体の union が誤る (共有壁が両側から残る)。
	#   ⚠ この壊れ方は **境界辺を残さない**ので chMesh.cpp の検査では捕まえられない。
	#     合否は「不透明に死なない」ことだけを見て、誤った値は KNOWN-LIMIT として**見せる**。
	#     ★ 誤った値を「期待値」として固定はしない (バグを仕様に格上げしないため)。直れば行が消える。
	rm -rf "$D-t" "$D-t2"
	OUT=$(SRAVA_CACHE_DIR="$D-t" SRAVA_SOURCE="$CH
	      print(\"T\", volume(box(2,2,2) ||| translate(box(2,2,2),[2,0,0])));" "$SRAVA" 2>&1)
	echo "$OUT" | grep -q 'closed unexpectedly' && { echo "FAIL: 面接触で agent ごと死んだ"; echo "$OUT"; exit 1; }
	T=$(echo "$OUT" | sed -n 's/^T //p')
	[ -n "$T" ] || echo "$OUT" | grep -q 'ERROR' || { echo "FAIL: 面接触がエラーにも値にもならない"; echo "$OUT"; exit 1; }
	# ★ ずらした側 (1e-7 食い込む) は正しく解けること = 限界が「測度 0 の接触」に局在している証拠。
	G=$(SRAVA_CACHE_DIR="$D-t2" SRAVA_SOURCE="$CH
	    print(\"G\", volume(box(2,2,2) ||| translate(box(2,2,2),[1.9999999,0,0])));" "$SRAVA" 2>&1 | sed -n 's/^G //p')
	[ "$(near "$G" 15.9999996 1e-7)" = 1 ] || { echo "FAIL: 1e-7 ずらした側が $G (期待 ~16)"; exit 1; }
	if [ "$(near "$T" 16 1e-9)" = 1 ]; then
		echo "CHERCHI-CONTACT-OK touching=$T (★上流が面接触を解けるようになった — 限界の記述を更新すること)"
	else
		echo "CHERCHI-CONTACT-OK KNOWN-LIMIT touching=${T:-error} (期待 16・共有壁が両側から残る) shifted=$G"
	fi
	rm -rf "$D-t" "$D-t2" ;;
guard)
	# ★ 多重に重なった n 項 = 上流が壊れる配置。以前は **実行のたびに違う誤った値**が返っていた
	#   (8 球 0.7 刻み・arity 8 で 24.43 / 19.23 / 17.78 / 15.36 …)。検査で弾けていること。
	#   ⚠ 固定するのは「値」ではなく **黙って誤らない**こと。上流が解けるようになったら
	#     正しい値 (18.6448…) が返るはずなので、そのときも合格にする。
	SRC=''
	i=0
	while [ $i -le 7 ]; do
		SRC="$SRC v[$i] = translate(sphere(1,32),[$(awk -v i=$i 'BEGIN{printf "%g", i*0.7}'),0,0]);"
		i=$((i+1))
	done
	rm -rf "$D-q"
	OUT=$(SRAVA_CACHE_DIR="$D-q" SRAVA_SOURCE="module(\"cherchi.so\",{priority:99,arity:8});
	      var v=[]; $SRC print(\"V\", volume(union(v)));" "$SRAVA" 2>&1)
	V=$(echo "$OUT" | sed -n 's/^V //p')
	if [ -n "$V" ]; then
		# 値が返ったなら正しい値 (他カーネル 4 つが一致する 18.6448…) でなければならない。
		[ "$(near "$V" 18.644847147089 1e-9)" = 1 ] || { echo "FAIL: 多重重なりが誤った値 $V を返した"; echo "$OUT"; exit 1; }
		echo "CHERCHI-GUARD-OK n-ary=$V (★上流が多重重なりを解けるようになった)"
	else
		echo "$OUT" | grep -q 'produced an open surface' || {
			echo "FAIL: 多重重なりが閉曲面検査で弾かれていない (誤値か別の壊れ方)"; echo "$OUT"; exit 1; }
		echo "CHERCHI-GUARD-OK 多重重なりは検査で弾かれた (以前は実行ごとに違う誤値)"
	fi
	rm -rf "$D-q" ;;
mfcross)
	# mf が作った mesh ("MFM3") を ch が読み、混成ブールが純 mf と同値になること。
	# ★ mf 側のオペランドは **tube** で作る: cherchi は tube を持たないので、cherchi が
	#   priority 99 でも tube の routing は manifold へ行く。
	# ★ 箱は **一般の位置**へずらす: tube の端の蓋 (x=0 の平面) と box の面が同一平面で接すると
	#   上流の退化条件を踏み、混成と純 mf で値が割れる。
	TUBE='tube([[[0,0,0],0.6],[[4,0,0],0.6]], 12)'
	BOX='translate(box(1,1,1),[0.5,0.25,0.25])'
	rm -rf "$D-c" "$D-d"
	MIX=$(SRAVA_CACHE_DIR="$D-c" SRAVA_SOURCE="$MF $CH
	      print(\"V\", volume($TUBE ||| $BOX));" "$SRAVA" 2>&1 | sed -n 's/^V //p')
	REF=$(SRAVA_CACHE_DIR="$D-d" SRAVA_SOURCE="module(\"manifold.so\",{priority:99});
	      print(\"V\", volume($TUBE ||| $BOX));" "$SRAVA" 2>&1 | sed -n 's/^V //p')
	if [ -z "$MIX" ] || [ -z "$REF" ]; then echo "FAIL: 値が出ない mixed=$MIX ref=$REF"; exit 1; fi
	[ "$(near "$MIX" "$REF" 1e-9)" = 1 ] || { echo "FAIL: 混成が純 mf と違う ref=$REF mixed=$MIX"; exit 1; }
	rm -rf "$D-c" "$D-d"
	echo "CHERCHI-MFCROSS-OK $MIX" ;;
cgcross)
	# ★ cg→ch 昇格読み。cgal の厳密メッシュ "MESH" (有理数文字列) を cherchi が double 化して読む。
	#   ★忠実性: 球は共通生成器 (common/geodesic.h) なので cgal も cherchi も同じ double 座標を
	#   持つ。cgal がそれを厳密有理数として書き、cherchi が読み戻す → 往復が無損失なら
	#   体積・頂点数・面数が **純 cherchi と完全一致**する (許容誤差ではなく一致を要求する)。
	#   ⚠ ここはブールを通さない (leaf + cast だけ) ので、上流の退化条件とは無関係。
	CG='module("cgal.so",{priority:99});'
	rm -rf "$D-g" "$D-h"
	VIA=$(SRAVA_CACHE_DIR="$D-g" SRAVA_SOURCE="$CG $CH
	      var g = cast(\"ch-mesh3d\", sphere(1,32));
	      print(\"R\", volume(g), nverts(g), nfaces(g));" "$SRAVA" 2>&1 | sed -n 's/^R //p')
	PURE=$(SRAVA_CACHE_DIR="$D-h" SRAVA_SOURCE="module(\"cherchi.so\",{priority:99});
	      var g = sphere(1,32);
	      print(\"R\", volume(g), nverts(g), nfaces(g));" "$SRAVA" 2>&1 | sed -n 's/^R //p')
	if [ -z "$VIA" ] || [ -z "$PURE" ]; then echo "FAIL: 値が出ない via=$VIA pure=$PURE"; exit 1; fi
	[ "$VIA" = "$PURE" ] || { echo "FAIL: cg 経由 [$VIA] が純 ch [$PURE] と一致しない"; exit 1; }
	rm -rf "$D-g" "$D-h"
	echo "CHERCHI-CGCROSS-OK sphere=[$VIA]" ;;
*)
	echo "unknown mode: $MODE"; exit 1 ;;
esac

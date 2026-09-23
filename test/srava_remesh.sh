#!/bin/sh
# refine / remesh / simplify (#3512) の回帰。**閉形式**で固定し、カーネル合議には頼らない。
# $1 = srava 実行体。$2 = モジュール (.so 名)。$3 = 許容相対誤差。
# $4 = 追加検査 (","区切り): "rs" = remesh/simplify も見る / "angle" = 最小角 / "edge" = 辺長
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# 見ているもの:
#   ⓪ refine は **形を変えない** — 単位箱を len=0.25 で細分しても体積 1 / 面積 6 のまま。
#      ★ cgal 版は相似分割 (EPECK のまま有理数)・manifold 版は RefineToLength で、
#        **作り方は違うが約束は同じ**。面数は一致しないので面数では固定しない。
#   ⓪' refine は面を増やす。
#   ⓪''(edge) ★ **すべての辺が len 以下**になる (OFF を書き出して測る)。これが refine の約束。
#   ① remesh は **形を変えない** — 単位箱を len=0.25 で張り直しても体積 1 / 面積 6 のまま。
#      ★ 「鋭角エッジを既定で保護する」設計の検定。保護しないと 0.999107 / 5.98811 になる。
#   ② remesh は **面を増やす**。
#   ③ (angle) ★★ **針状三角形が消える** — 1x1x0.02 の板は最小角 1.15°。len=0.05 で張り直すと
#      10° を超える。⚠ srava には角度を測る op が無いので **OFF を書き出して測る**。
#   ④ (angle) ★ refine は最小角を **変えない** — cgal は相似分割なので厳密に不変。
#      ここが refine と remesh の違いで、「refine は質に手を触れない」という約束の検定。
#   ⑤ simplify は **形を保つ** — 細かく張り直した箱を 200 面へ落としても体積 1 / 面積 6。
#   ⑥ simplify は **面数を落とす** — 200 を頼んだら 200 以下になる (「以下で止まる」仕様)。
#   ⑦ 目標が現面数以上なら **素通し** — 12 面の箱に 1000 を頼んでも 12 面のまま。
#   ⑧ 引数の検査は **明示エラー** — len=0 と目標面数 3 (四面体未満) は 0 でも無言でもなく落とす。
#   ⑨ (cleanup) simplify_cleanup は **面数を落とし、形のずれは tol の範囲に収まる**。
#      ★ simplify (面数を指定して形を守る) と **約束が逆向き**なので、見るものも逆:
#        面数は成り行きなので「減ったか」だけ・形は「tol から見て妥当な範囲か」を見る。
#      ⚠ tol=0 は「メッシュ自身の許容差」= 厳密に組んだ形では **素通し**。
SRAVA="${1:?srava binary not given}"
SO="${2:?module .so not given}"
TOL="${3:-1e-12}"
EXTRA="${4:-}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
M="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});"
fails=0

# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

near() {   # near <got> <want> <name>
	ok=$(awk -v g="$1" -v w="$2" -v t="$TOL" 'BEGIN{
		d = g - w; if (d < 0) d = -d;
		r = (w < 0 ? -w : w); if (r < 1) r = 1;
		print (d / r <= t) ? 1 : 0 }')
	if [ "$ok" = "1" ]; then echo "ok    $3 = $1"
	else echo "FAIL: $3 = $1 (期待 $2・許容 $TOL)"; fails=$((fails+1)); fi
}

cmp_num() {   # cmp_num <got> <gt|ge|le|lt> <want> <name>
	ok=$(awk -v g="$1" -v w="$3" -v o="$2" 'BEGIN{
		if (o == "gt") print (g >  w) ? 1 : 0;
		else if (o == "ge") print (g >= w) ? 1 : 0;
		else if (o == "le") print (g <= w) ? 1 : 0;
		else print (g <  w) ? 1 : 0 }')
	if [ "$ok" = "1" ]; then echo "ok    $4 = $1"
	else echo "FAIL: $4 = $1 (期待 $2 $3)"; fails=$((fails+1)); fi
}

run() {   # run <suffix> <src>
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="$M $2" "$SRAVA" 2>&1
}

val() { echo "$2" | sed -n "s/^$1 //p"; }

# ---- OFF を読んで測る道具 (③④⓪'' 共用) ----
# ⚠ 行番号では読めない — OFF は数え行の後に **空行**を挟むことがある (CGAL の書き手はそうする)。
#   空行を飛ばして「OFF / 数え行 / 頂点 nv 行 / 面」の順で状態を進める。
offstat() {   # offstat <off ファイル> <minang|maxedge>
	LC_ALL=C awk -v what="$2" '
	     /^[ \t]*$/ { next }
	     st==0 { st=1; next }                      # "OFF"
	     st==1 { nv=$1; st=2; iv=0; next }         # 数え行
	     st==2 { x[iv]=$1; y[iv]=$2; z[iv]=$3; iv++; if (iv>=nv) st=3; next }
	     st==3 && $1==3 {
	       for (k=0;k<3;k++) {
	         a=$(2+k); b=$(2+(k+1)%3); c=$(2+(k+2)%3);
	         ux=x[b]-x[a]; uy=y[b]-y[a]; uz=z[b]-z[a];
	         wx=x[c]-x[a]; wy=y[c]-y[a]; wz=z[c]-z[a];
	         lu=sqrt(ux*ux+uy*uy+uz*uz); lw=sqrt(wx*wx+wy*wy+wz*wz);
	         if (lu<=0 || lw<=0) continue;
	         if (mx=="" || lu>mx) mx=lu;
	         cs=(ux*wx+uy*wy+uz*wz)/(lu*lw);
	         if (cs>1) cs=1; if (cs<-1) cs=-1;
	         ang=atan2(sqrt(1-cs*cs), cs)*45/atan2(1,1);
	         if (mn=="" || ang<mn) mn=ang;
	       } }
	     END { v = (what == "maxedge") ? mx : mn;
	           if (v=="") print "nan"; else printf "%.6f\n", v }' "$1"
}

# ---- ⓪ refine (cgal / manifold 共通) ----
OUT=$(run refine '
  var b = box(1,1,1);
  var r = refine(b, 0.25);
  print("A", volume(r));
  print("B", area(r));
  print("C", nfaces(r));')
A=$(val A "$OUT"); B=$(val B "$OUT"); C=$(val C "$OUT")
if [ -z "$A" ] || [ -z "$B" ] || [ -z "$C" ]; then
	echo "FAIL: refine の値が出ない A=$A B=$B C=$C"; echo "$OUT"; exit 1
fi
near "$A" 1 "⓪ refine は体積を変えない"
near "$B" 6 "⓪ refine は面積を変えない"
cmp_num "$C" gt 12 "⓪' refine は面を増やす"

# ---- ⓪'' refine は全辺を len 以下にする ----
case ",$EXTRA," in *,edge,*)
	rm -f "$D-ref.off"
	OUT=$(run edge "export(\"$D-ref.off\", refine(box(1,1,1), 0.25), \"mm\");")
	if [ ! -s "$D-ref.off" ]; then
		echo "FAIL: ⓪'' OFF が書き出せない"; echo "$OUT"; fails=$((fails+1))
	else
		ME=$(offstat "$D-ref.off" maxedge)
		cmp_num "$ME" le 0.25 "⓪'' refine 後の最長辺 <= len"
	fi ;;
esac

# ---- ①②⑤⑥⑦ remesh / simplify ----
case ",$EXTRA," in *,rs,*)
	OUT=$(run main '
	  var b  = box(1,1,1);
	  var r  = remesh(b, 0.25);
	  var f  = remesh(b, 0.1);
	  var s  = simplify(f, 200);
	  print("A", volume(r));
	  print("B", area(r));
	  print("C", nfaces(r));
	  print("D", volume(s));
	  print("E", area(s));
	  print("F", nfaces(s));
	  print("G", nfaces(f));
	  print("H", nfaces(simplify(b, 1000)));')
	A=$(val A "$OUT"); B=$(val B "$OUT"); C=$(val C "$OUT"); Dv=$(val D "$OUT")
	E=$(val E "$OUT"); F=$(val F "$OUT"); G=$(val G "$OUT"); H=$(val H "$OUT")
	if [ -z "$A" ] || [ -z "$B" ] || [ -z "$C" ] || [ -z "$Dv" ] || [ -z "$E" ] || [ -z "$F" ] || [ -z "$G" ] || [ -z "$H" ]; then
		echo "FAIL: 値が出ない A=$A B=$B C=$C D=$Dv E=$E F=$F G=$G H=$H"; echo "$OUT"; exit 1
	fi
	near "$A" 1 "① remesh は体積を変えない"
	near "$B" 6 "① remesh は面積を変えない"
	cmp_num "$C" gt 100 "② remesh は面を増やす"
	near "$Dv" 1 "⑤ simplify は体積を変えない"
	near "$E" 6 "⑤ simplify は面積を変えない"
	cmp_num "$F" le 200 "⑥ simplify は目標以下まで落とす"
	cmp_num "$F" lt "$G" "⑥ simplify は入力より面が少ない"
	near "$H" 12 "⑦ 目標が現面数以上なら素通し" ;;
esac

# ---- ③④ 最小角 (angle) ----
case ",$EXTRA," in *,angle,*)
	rm -f "$D-in.off" "$D-out.off" "$D-rf.off"
	OUT=$(run angle "
	  var p = box(1,1,0.02);
	  export(\"$D-in.off\",  p, \"mm\");
	  export(\"$D-out.off\", remesh(p, 0.05), \"mm\");
	  export(\"$D-rf.off\",  refine(p, 0.05), \"mm\");")
	if [ ! -s "$D-in.off" ] || [ ! -s "$D-out.off" ] || [ ! -s "$D-rf.off" ]; then
		echo "FAIL: ③ OFF が書き出せない"; echo "$OUT"; fails=$((fails+1))
	else
		MI=$(offstat "$D-in.off" minang); MO=$(offstat "$D-out.off" minang)
		MR=$(offstat "$D-rf.off" minang)
		# ⚠ ${..} で閉じる — $MI° は ° の UTF-8 バイトまで変数名に食われる
		echo "      ③ 最小角: 入力 ${MI}° → remesh 後 ${MO}° / refine 後 ${MR}°"
		cmp_num "$MI" lt 5  "③ 入力は針状 (最小角 < 5°)"
		cmp_num "$MO" gt 10 "③ remesh 後は最小角 > 10°"
		# ⚠ 許容は **1e-3**。厳密には一致する (相似分割) が、測っているのは OFF に
		#   書き出した座標で、OFF ライタは **有効 6 桁**しか出さない (0.000689655 等)。
		#   針状三角形の角度はそこに敏感なので、厳密比較にすると桁落ちで落ちる。
		ok=$(awk -v g="$MR" -v w="$MI" 'BEGIN{ d=g-w; if(d<0)d=-d; print (d/w <= 1e-3)?1:0 }')
		if [ "$ok" = "1" ]; then echo "ok    ④ refine は最小角を変えない = $MR"
		else echo "FAIL: ④ refine が最小角を変えた = $MR (入力 $MI)"; fails=$((fails+1)); fi
	fi ;;
esac

# ---- ⑨ simplify_cleanup (cleanup) ----
case ",$EXTRA," in *,cleanup,*)
	OUT=$(run cleanup '
	  var s = sphere(1, 64);
	  print("A", nfaces(s));
	  print("B", volume(s));
	  print("C", nfaces(simplify_cleanup(s, 0)));
	  print("D", nfaces(simplify_cleanup(s, 0.03)));
	  print("E", volume(simplify_cleanup(s, 0.03)));')
	A=$(val A "$OUT"); B=$(val B "$OUT"); C=$(val C "$OUT"); Dv=$(val D "$OUT"); E=$(val E "$OUT")
	if [ -z "$A" ] || [ -z "$B" ] || [ -z "$C" ] || [ -z "$Dv" ] || [ -z "$E" ]; then
		echo "FAIL: cleanup の値が出ない A=$A B=$B C=$C D=$Dv E=$E"; echo "$OUT"; fails=$((fails+1))
	else
		near    "$C" "$A"  "⑨ tol=0 は素通し (メッシュ自身の許容差)"
		cmp_num "$Dv" lt "$A" "⑨ tol=0.03 で面数が減る"
		cmp_num "$Dv" gt 100  "⑨ 潰れ切ってはいない"
		# 形のずれ: 半径 1 の球で tol=0.03 なら、面は高々 0.03 しか動かない
		# ⇒ 体積は (1-0.03)^3 = 0.913 倍より大きいはず (実測は 0.981 倍)。
		LO=$(awk -v b="$B" 'BEGIN{ printf "%.9f", b*0.913 }')
		cmp_num "$E" gt "$LO" "⑨ 体積のずれが tol から見て妥当"
		cmp_num "$E" lt "$B"  "⑨ 体積は減る側にしか動かない (凸な形なので)"
	fi ;;
esac

# ---- ⑧ 引数の検査は明示エラー ----
BADS="refine(box(1,1,1), 0)"
case ",$EXTRA," in *,rs,*) BADS="$BADS;remesh(box(1,1,1), 0);simplify(box(1,1,1), 3)" ;; esac
case ",$EXTRA," in *,cleanup,*) BADS="$BADS;simplify_cleanup(box(1,1,1), -1)" ;; esac
OLDIFS=$IFS; IFS=';'
for bad in $BADS; do
	IFS=$OLDIFS
	O=$(run bad "print(\"X\", volume($bad));")
	if echo "$O" | grep -qi "error\|must be"; then echo "ok    ⑧ 明示エラー: $bad"
	else echo "FAIL: ⑧ $bad がエラーにならない"; echo "$O"; fails=$((fails+1)); fi
	IFS=';'
done
IFS=$OLDIFS

if [ "$fails" = "0" ]; then echo "REMESH-OK"; else echo "REMESH-FAIL ($fails 件)"; exit 1; fi

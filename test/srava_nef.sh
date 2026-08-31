#!/bin/sh
# $SO (CGAL Nef / SNC・#3433 P1) の回帰テスト。
# $1 = srava 実行体。$2 = モード。$3 = 変種(snc|hybrid)。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# 4 つのモードが見ているもの:
#   agree     … Nef(SNC) と cgal(corefinement) が **同じ厳密解**を出すこと。球生成は共通ヘッダ
#               (common/geodesic.h) なので体積は桁まで一致するはず。
#   cross     … cg の "MESH"(厳密境界) を **nf へ昇格読み**できること (nf_codecs の nf-cg-upgrade)。
#               cache に MESH と NEF3 が両方できることまで見る。
#   unbounded … ★非有界な値が **cache を渡って生き残る**こと (wire 形式が SNC である証明) と、
#               非有界な集合の volume が **明示エラー**になること。境界表現で書いていた頃は
#               「箱の補集合」が「箱」に化けて volume が 8 を返した (実装中に実際に踏んだ)。
#   downgrade … ★nf → cg の**逆方向**。cg は有界な 2-多様体しか表現できないので、
#               ① 普通の立体は変換できる ② 非有界な Nef は **明示エラー** (0 や 8 を黙って返さない)。
#   mfcross   … ★mf ⇄ nf。① mf の "MFM3"(raw double) を nf へ昇格読みできる (無損失)
#               ② mf が作った mesh と nf が作った mesh の**混成ブール**が純 nf と同値
#               ③ nf → mf の**直接**変換が有界立体で通ること (NEF3 のハイブリッド形式のおかげで
#                 普通の立体は厳密境界 = "MESH" と同一フレーミング → manifold が CGAL 無しで読める)
#               ④ ★非有界 (SNC 形式) は manifold では**読めないのが正**。CGAL 非依存 (GPL 非汚染) を
#                 設計として守っているため。もし通るようになったら CGAL が入った合図 = テストが落ちる。
#   minkowski … ★Minkowski 和 A ⊕ B (#3440・offset のプリミティブ)。箱どうしは厳密に箱になる
#               (体積 27) / cg の mesh を渡しても all-foreign sig で nef に routing される /
#               非有界は明示エラー (CGAL は黙って片方を返すので、その前に弾く)。
#   offset    … ★3D offset (#3440 の 2 で cgal.so から移設)。膨張/収縮/中空箱/非有界エラー。
#   cavity    … ★内部空洞を持つ立体が cache を渡っても壊れないこと (hybrid の境界形式のバグ回帰)。
#   convex    … ★凸分解 (#3441)。凸=1 片 / 凹=2 片以上 (nparts) / 各片を part(d,i) で取り出せる
#               (体積の合計が元と一致) / 内壁ができて cg 降格不可 / 範囲外は明示エラー。
#   unify     … ★内壁除去 (#3442・正則化)。単一立体は不変 / 空洞は保つ / 凸分解の内壁を消せる。
#   selfx     … ★自己交差メッシュ (自分を貫く tube) は明示エラー・**agent は落ちない**
#               (以前は CGAL の assertion で agent ごと死んでいた)。
#   solidify  … ★壊れた境界からのソリッド再構成 (#3445)。自己交差 tube の体積が
#               52.02 (重なりの二重計上) → 48.61 (正しい値) になること。健全な立体は不変・
#               空洞は埋まらない・離れた成分は成分数を保つ (深さ合成の回帰)。
#   ggcross   … ★gg ⇄ nf (2026-08-25)。gg-mesh3d の 4CC は "MFM3" (manifold と共有する形式) なので
#               **nf-mf-upgrade codec がそのまま読む** = codec 不要で sig の宣言だけで開通する。
#               ① 昇格読み ② 混成ブール ③ nef 固有 op (minkowski) に gg を直接
#               ④ 直接経路 (gg→nf) と 2 段経路 (gg→cg→nf) がビット一致
#               ⑤ ★負の対照: **solidify には gg を足していない** — geogram も持つ op なので
#                 all-foreign を書くと nef が geogram の自型 solidify まで奪う (priority 5>3)。
#                 nef の priority を上げても solidify(gg) に NEF タグが出ないことを固定する。
#   chain     … ★**Nef 型を維持したまま op 連鎖する**こと (#3433 の中心要件)。
#               union 連鎖の mesh cache が全て NEF3 で、MESH への往復が **1 つも無い**ことを見る。
#               往復が入ると変換税で Nef 本来のコストが測れない (ベンチが無意味になる)。
SRAVA="$1"
MODE="${2:-agree}"
# ★ #3433: nef は 2 変種 (SNC 固定 / ハイブリッド) を別モジュールとしてビルドする。
#   型名・4CC・module 名を分けてあるので同居でき、どちらでも同じ振る舞いテストが通るはず。
VAR="${3:-hybrid}"
case "$VAR" in
snc)    SO="nef_snc.so";    TYPE="nf-mesh3d";  TAG="NEF3"; OTHER_SO="nef_hybrid.so" ;;
hybrid) SO="nef_hybrid.so"; TYPE="nfb-mesh3d"; TAG="NEFB"; OTHER_SO="nef_snc.so" ;;
*) echo "FAIL: unknown variant $VAR"; exit 1 ;;
esac
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
rm -rf "$D" "$D-a" "$D-b" "$D-a2" "$D-b2"

# ★ #3452: 起動時 eager-load 撤去に伴い、この回帰群の一部(agree の cg 比較対照・cast 先の
#   cgal.so 等)が module() 未呼出のまま cgal/geogram 等の他カーネルを暗黙に使う(=旧挙動依存)。
#   $SO は各ケースが個別に module() するのでそのまま。SRAVA_MODULE_ALL=1 で残りを一括解決する
#   (include "module/all.sra"; 相当。$SO の priority 明示はこの後でも上書きとして効く)。
export SRAVA_MODULE_ALL=1
SRAVA_PATH="$(cd "$(dirname "$0")/../lib" && pwd)"
export SRAVA_PATH

# cache dir の mesh 本体から D_META の 4CC を数える (PWIG ヘッダ直後に 4 バイト)。
count_tag() {   # $1=dir $2=tag
	n=0
	for f in "$1"/*.cache ; do
		[ -f "$f" ] || continue
		# ★ LC_ALL=C は必須。バイナリを食わせるので、UTF-8 ロケールの BSD tr (macOS) は
		#   "tr: Illegal byte sequence" で落ちて数が 0 になる (2026-08-20 に mac で踏んだ)。
		if head -c 64 "$f" 2>/dev/null | LC_ALL=C tr -d '\0' | LC_ALL=C grep -q "$2" ; then n=$((n+1)) ; fi
	done
	echo "$n"
}

case "$MODE" in
agree)
	S='var s = sphere(1.5, 40); print("VOL", volume(union(s, translate(s,[0.5,0.5,0.5]))));'
	NF=$(SRAVA_CACHE_DIR="$D-a" \
	     SRAVA_SOURCE="module(\"$SO\",{priority:99}); $S" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	CG=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="$S" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	if [ -z "$NF" ] ; then echo "FAIL: nef produced no volume" ; exit 1 ; fi
	if [ -z "$CG" ] ; then echo "FAIL: cgal produced no volume" ; exit 1 ; fi
	ok=$(awk -v a="$NF" -v b="$CG" 'BEGIN{ d=a-b; if(d<0)d=-d; s=(a<0?-a:a); if(s<1)s=1;
	                                       print (d/s < 1e-12) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: nef=$NF cgal=$CG (厳密カーネル同士が不一致)" ; exit 1 ; fi
	echo "NEF-AGREE-OK nef=$NF cgal=$CG"
	;;

cross)
	# cgal が作った箱 (MESH) を明示 cast で nf へ。昇格読みが効けば体積 8。
	OUT=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="module(\"$SO\"); var b = box(2,2,2);
	      print(\"VOL\", volume(cast(\"$TYPE\", b)));" "$SRAVA" 2>&1)
	V=$(echo "$OUT" | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V" 'BEGIN{ d=a-8; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: cast MESH->nf volume=$V (期待 8)" ; echo "$OUT" ; exit 1 ; fi
	nmesh=$(count_tag "$D-a" MESH)
	nnef=$(count_tag "$D-a" "$TAG")
	if [ "$nmesh" -lt 1 ] || [ "$nnef" -lt 1 ] ; then
		echo "FAIL: cache に MESH($nmesh) と NEF3($nnef) が揃っていない = 昇格読みを通っていない"
		exit 1
	fi
	echo "NEF-CROSS-OK vol=$V mesh=$nmesh nef=$nnef"
	;;

unbounded)
	# ★非有界な値 (箱の補集合) が **cache を渡って生き残る**こと + 体積は明示エラーになること。
	#   cache の wire 形式は SNC なので非有界も厳密に往復する。境界表現で書いていた頃は
	#   「箱の補集合」が「箱」に化けて volume が 8 を返した (実装中に実際に踏んだ回帰)。
	#   ① volume(complement(box)) は **エラー** (非有界な集合に体積は無い)
	#   ② volume(complement(complement(box))) は **8** (非有界を経由して元へ戻る)
	OUT=$(SRAVA_CACHE_DIR="$D-a" \
	      SRAVA_SOURCE="module(\"$SO\",{priority:99}); print(\"VOL\", volume(complement(box(2,2,2))));" \
	      "$SRAVA" 2>&1)
	V=$(echo "$OUT" | sed -n 's/^VOL //p')
	if [ -n "$V" ] ; then
		echo "FAIL: 非有界な集合の volume が値 '$V' を返した (有界化して嘘をついている)"
		echo "$OUT" ; exit 1
	fi
	if ! echo "$OUT" | grep -q "volume" ; then
		echo "FAIL: volume のエラーメッセージが出ていない" ; echo "$OUT" ; exit 1
	fi
	OUT2=$(SRAVA_CACHE_DIR="$D-b" \
	       SRAVA_SOURCE="module(\"$SO\",{priority:99});
	       print(\"VOL\", volume(complement(complement(box(2,2,2)))));" "$SRAVA" 2>&1)
	V2=$(echo "$OUT2" | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V2" 'BEGIN{ d=a-8; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then
		echo "FAIL: 二重補集合が $V2 (期待 8) = 非有界な中間値が cache を渡れていない"
		echo "$OUT2" ; exit 1
	fi
	echo "NEF-UNBOUNDED-OK (volume は明示エラー / 二重補集合は $V2)"
	;;

downgrade)
	# ★ #3440 の 3 以降、**cgal.so は CGAL Nef に依存しない** (SNC をパースできない)。
	#   よって nf → cg の降格が通るのは **payload が厳密境界形式で書かれている値だけ**:
	#     hybrid … 有界・2-多様体・**空洞なし** の立体 (= 境界形式で書かれる) は通る
	#     snc    … 常に SNC なので **どれも通らない** (計画時に承知した代償)
	#   いずれの不可も **明示エラー** (黙って 0 や 8 を返さない)。
	OUT=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="module(\"$SO\",{priority:99}); var b = box(2,2,2);
	      print(\"VOL\", volume(cast(\"cg-mesh3d\", b)));" "$SRAVA" 2>&1)
	V=$(echo "$OUT" | sed -n 's/^VOL //p')
	if [ "$VAR" = hybrid ] ; then
		ok=$(awk -v a="$V" 'BEGIN{ d=a-8; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
		if [ "$ok" != "1" ] ; then echo "FAIL: nf->cg 降格 volume=$V (期待 8)" ; echo "$OUT" ; exit 1 ; fi
	else
		if [ -n "$V" ] ; then
			echo "FAIL: snc の nf->cg 降格が値 '$V' を返した = cgal が SNC を読んでいる (Nef 依存が戻った合図)"
			echo "$OUT" ; exit 1
		fi
		V="明示エラー(SNC)"
	fi
	# ② 非有界な Nef は cg で表現できない → 明示エラー。黙って 0 (空 mesh) や 8 (境界だけ拾う) にしない
	OUT2=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	       var c = complement(box(2,2,2)); print(\"VOL\", volume(cast(\"cg-mesh3d\", c)));" "$SRAVA" 2>&1)
	V2=$(echo "$OUT2" | sed -n 's/^VOL //p')
	if [ -n "$V2" ] ; then
		echo "FAIL: 非有界な Nef の cg 降格が値 '$V2' を返した (エラーになるべき)"
		echo "$OUT2" ; exit 1
	fi
	# ★ 2026-08-28: 不可の**言われ方**が変種で分かれる (どちらも明示エラーであることは同じ)。
	#   hybrid … cgal は NEFB を読めると申告している → routing は通り、実際に読んだ **codec 層**が
	#             落とす。ここは形式 (4CC) を知っている立場なので $TAG が出る。
	#   snc   … cgal は NEF3 を読めると申告していない (CGAL Nef 非依存) → **planner の routing** が
	#             申告の段で落とす。planner は形式に踏み込まない (in-proc の値はまだメモリ上の
	#             body でしかなく 4CC は pigDataCache の都合) ので、出るのは **型名**。
	if [ "$VAR" = hybrid ] ; then
		if ! echo "$OUT2" | grep -q "$TAG" ; then
			echo "FAIL: エラーメッセージが入力形式 ($TAG) を示していない" ; echo "$OUT2" ; exit 1
		fi
	else
		if ! echo "$OUT2" | grep -q "no module declares a conversion" ; then
			echo "FAIL: SNC の不可が routing の申告エラーになっていない" ; echo "$OUT2" ; exit 1
		fi
		if ! echo "$OUT2" | grep -q "$TYPE" ; then
			echo "FAIL: エラーメッセージが入力の型名 ($TYPE) を示していない" ; echo "$OUT2" ; exit 1
		fi
	fi
	# ③ ★空洞つき立体 (中空箱) の降格。cg は中空立体を表現できるので **通るのが正**。
	#    hybrid は空洞つきも境界形式で書く (読み側がシェルを even-odd で復元できるようになったため)。
	#    snc は常に SNC なので通らない。ここが hybrid で落ちたら、書き側が空洞を SNC へ逃がしている
	#    (= cache が太り cg/mf へ渡せなくなる退行) の合図。
	OUT3=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	       var h = difference(box(3,3,3), translate(box(1,1,1),[1,1,1]));
	       print(\"VOL\", volume(cast(\"cg-mesh3d\", h)));" "$SRAVA" 2>&1)
	V3=$(echo "$OUT3" | sed -n 's/^VOL //p')
	if [ "$VAR" = hybrid ] ; then
		ok=$(awk -v a="$V3" 'BEGIN{ d=a-26; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
		if [ "$ok" != "1" ] ; then
			echo "FAIL: 空洞つき立体の cg 降格が $V3 (期待 27-1=26)" ; echo "$OUT3" ; exit 1
		fi
	else
		if [ -n "$V3" ] ; then
			echo "FAIL: snc の空洞つき cg 降格が値 '$V3' を返した = cgal が SNC を読んでいる"
			echo "$OUT3" ; exit 1
		fi
		V3="明示エラー(SNC)"
	fi
	echo "NEF-DOWNGRADE-OK[$VAR] 立体=$V 空洞つき=$V3 / 非有界=明示エラー"
	;;

mfcross)
	# ① mf(MFM3) → nf の昇格読み
	OUT=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="module(\"manifold.so\",{priority:99});
	      var b = box(2,2,2); module(\"$SO\",{priority:120});
	      print(\"VOL\", volume(cast(\"$TYPE\", b)));" "$SRAVA" 2>&1)
	V=$(echo "$OUT" | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V" 'BEGIN{ d=a-8; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: mf->nf 昇格 volume=$V (期待 8)" ; echo "$OUT" ; exit 1 ; fi
	# ② 混成ブール (mf の球 ∪ nf の球を nef が計算) が純 nf と同値であること
	MIX=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="module(\"manifold.so\",{priority:99});
	      var a = sphere(1.5,40); module(\"$SO\",{priority:120});
	      var b = translate(sphere(1.5,40),[0.5,0.5,0.5]);
	      print(\"VOL\", volume(union(a,b)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	PURE=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	       var s = sphere(1.5,40);
	       print(\"VOL\", volume(union(s, translate(s,[0.5,0.5,0.5]))));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	if [ -z "$MIX" ] || [ -z "$PURE" ] ; then echo "FAIL: 混成/純 nf のどちらかが値を返さない ($MIX / $PURE)" ; exit 1 ; fi
	ok=$(awk -v a="$MIX" -v b="$PURE" 'BEGIN{ d=a-b; if(d<0)d=-d; s=(a<0?-a:a); if(s<1)s=1;
	                                          print (d/s < 1e-12) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: 混成=$MIX 純nf=$PURE (一致すべき)" ; exit 1 ; fi
	# ③④ ★nf → mf の**直接**変換は変種で成否が変わる。これは仕様どおりで、両方を固定する:
	#   hybrid … 有界立体は厳密境界形式 (cg の "MESH" と同一フレーミング) で書かれるので
	#            **manifold が CGAL 無しで読める** → 通る。非有界 (SNC 形式) は読めない = 明示エラー。
	#   snc    … 常に SNC なので manifold は読めない (CGAL 非依存 = GPL 非汚染を守る) → 常に明示エラー。
	#            ★#3440 の 3 で cgal も Nef 非依存になったため、**cg を挟む 2 段の逃げ道も無くなった**
	#            (SNC を読めるのは nef 自身だけ)。snc の値を他カーネルへ渡す道は無い = 計画時に
	#            承知した代償。snc のまま export するか、hybrid を使う。
	#   ★ manifold の codec は "NEFB" だけを申告する = **読めないものを申告しない**ので、
	#     planner が routing 段階で正しく判断できる (#3439 の「宣言と実態を一致させる」)。
	V3=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99}); var b = box(2,2,2);
	     module(\"manifold.so\",{priority:50,exec_default:\"thread\"});
	     print(\"VOL\", volume(cast(\"mf-mesh3d\", b)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	if [ "$VAR" = hybrid ] ; then
		ok=$(awk -v a="$V3" 'BEGIN{ d=a-8; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: hybrid の nf->mf 直接 (有界) が volume=$V3 (期待 8)" ; exit 1 ; }
	else
		[ -z "$V3" ] || { echo "FAIL: snc (常に SNC) の nf->mf 直接が通った = manifold が SNC を読んでいる: $V3" ; exit 1 ; }
		# ★cg を挟む 2 段も通らないこと (#3440 の 3 で cgal が Nef 非依存になった)。
		#   ここが通るようになったら cgal に Nef が戻った合図 = テストが落ちる。
		V3=$(SRAVA_CACHE_DIR="$D-b2" SRAVA_SOURCE="module(\"$SO\",{priority:99}); var b = box(2,2,2);
		     module(\"manifold.so\",{priority:50,exec_default:\"thread\"});
		     print(\"VOL\", volume(cast(\"mf-mesh3d\", cast(\"cg-mesh3d\", b))));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
		[ -z "$V3" ] || { echo "FAIL: snc の nf->cg->mf 2 段が通った = cgal が SNC を読んでいる: $V3" ; exit 1 ; }
		V3="明示エラー(SNC は他カーネルへ渡せない)"
	fi
	# 非有界は両変種とも mf へ渡せない (SNC 形式になるので manifold は読めない)
	OUT4=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	       var c = complement(box(2,2,2));
	       module(\"manifold.so\",{priority:50,exec_default:\"thread\"});
	       print(\"VOL\", volume(cast(\"mf-mesh3d\", c)));" "$SRAVA" 2>&1)
	if echo "$OUT4" | grep -q "^VOL " ; then
		echo "FAIL: 非有界 nf の mf 変換が通った = manifold に CGAL が入った可能性 (設計判断が要る)"
		echo "$OUT4" ; exit 1
	fi
	echo "NEF-MFCROSS-OK[$VAR] mf->nf=$V 混成=$MIX 純nf=$PURE nf->mf=$V3 (非有界は明示エラー)"
	;;

ggcross)
	# ⚠ **nef_snc と nef_hybrid を同時にロードしてはいけない** (#3433 の設計)。型名は変種ごとに
	#   違う (nf-mesh3d / nfb-mesh3d) ので型で振れる呼び出しは曖昧にならないが、**all-foreign 行**
	#   (cg/mf/gg を受ける行) は両変種が priority 同値 5 で名乗るので勝者が不定になる。
	#   → 各スクリプトの先頭で必ずもう一方の変種を落としてから測る。
	#   ★ 2026-08-28: module(so,"off") が **実アンロード (dlclose)** になり、未ロードへの off は
	#     明示エラーになったので、module_loaded で守る。#3452 で起動時の一括ロードが廃止された
	#     ので実際には未ロードのことが多いが、守っておけば eager ロードが復活しても効く。
	# ① gg("MFM3") → nf の昇格読み。codec は mf 用のものがそのまま効く。
	OUT=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); } module(\"geogram.so\",{priority:99});
	      var b = box(2,2,2); module(\"$SO\",{priority:120});
	      print(\"VOL\", volume(cast(\"$TYPE\", b)));" "$SRAVA" 2>&1)
	V=$(echo "$OUT" | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V" 'BEGIN{ d=a-8; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: gg->nf 昇格 volume=$V (期待 8)" ; echo "$OUT" ; exit 1 ; fi
	# ② 混成ブール (gg の箱 ∪ nf の箱) が純 nf と同値。ずらした 2 個の 2 立方 = 8+8-1 = 15 (厳密)。
	MIX=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); } module(\"geogram.so\",{priority:99});
	      var a = box(2,2,2); module(\"$SO\",{priority:120});
	      var b = translate(box(2,2,2),[1,1,1]);
	      print(\"VOL\", volume(union(a,b)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	PURE=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); } module(\"$SO\",{priority:99});
	       var a = box(2,2,2);
	       print(\"VOL\", volume(union(a, translate(box(2,2,2),[1,1,1]))));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	if [ -z "$MIX" ] || [ -z "$PURE" ] ; then echo "FAIL: 混成/純 nf のどちらかが値を返さない ($MIX / $PURE)" ; exit 1 ; fi
	if [ "$MIX" != "$PURE" ] ; then echo "FAIL: 混成=$MIX 純nf=$PURE (一致すべき)" ; exit 1 ; fi
	ok=$(awk -v a="$MIX" 'BEGIN{ d=a-15; if(d<0)d=-d; print (d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: 混成ブール volume=$MIX (期待 15)" ; exit 1 ; fi
	# ③ ★nef 固有 op に gg を直接。Minkowski 2³ ⊕ 1³ = 3³ = 27 (厳密)。
	MK=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); } module(\"geogram.so\",{priority:99});
	     var g = box(2,2,2); module(\"$SO\",{priority:120});
	     print(\"VOL\", volume(minkowski(g, box(1,1,1))));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	ok=$(awk -v a="$MK" 'BEGIN{ d=a-27; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: minkowski(gg,nf) volume=$MK (期待 27)" ; exit 1 ; fi
	# ④ ★直接経路 (gg→nf) と 2 段経路 (gg→cg→nf) が **ビット一致** すること。
	#    どちらも計算するのは nef で、違うのは入口の codec だけ (MFM3 読み / MESH 読み)。
	D1=$(SRAVA_CACHE_DIR="$D-b2" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); } module(\"geogram.so\",{priority:99});
	     var g = box(2,2,2); module(\"$SO\",{priority:120});
	     print(\"VOL\", volume(offset(g, 0.5, 1)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	D2=$(SRAVA_CACHE_DIR="$D-c" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); } module(\"geogram.so\",{priority:99});
	     var g = box(2,2,2); module(\"$SO\",{priority:120});
	     print(\"VOL\", volume(offset(cast(\"cg-mesh3d\", g), 0.5, 1)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	if [ -z "$D1" ] || [ "$D1" != "$D2" ] ; then
		echo "FAIL: 直接 gg->nf=$D1 と 2 段 gg->cg->nf=$D2 が一致しない" ; exit 1
	fi
	# ⑤ ★負の対照: solidify は geogram も持つ op なので nef に gg 行を **足していない**。
	#    nef の priority を上げても solidify(gg) は geogram が実行する = NEF タグが出ない。
	rm -rf "$D-d"
	# ⚠ geogram を **cgal (既定 20) より上**にしないと box が cgal で作られ、gg でなく cg の
	#   経路を測ってしまう (最初にこれを踏んだ)。leaf を作らせたいモジュールを必ず最上位にする。
	OUT5=$(SRAVA_CACHE_DIR="$D-d" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); } module(\"geogram.so\",{priority:99});
	       var g = box(2,2,2); module(\"$SO\",{priority:120});
	       print(\"VOL\", volume(solidify(g)));" "$SRAVA" 2>&1)
	NN=$(count_tag "$D-d" "$TAG")
	if [ "$NN" != "0" ] ; then
		echo "FAIL: solidify(gg) を nef が奪った ($TAG が $NN 個) = geogram の自型 op が取られている"
		echo "$OUT5" ; exit 1
	fi
	# ⑥ ★★ solidify の振り分けが **精度クラスを保存する**こと (既定の梯子 geogram 6 > nef 5)。
	#    cg(厳密) 入力 → nef (厳密のまま) / mf(double) 入力 → geogram (double のまま)。
	#    priority を触らずに測る = 既定の挙動そのものを固定する。
	rm -rf "$D-e" "$D-f"
	# ★ #3452: module/all.sra は nef を hybrid だけ束ねる(設計通り)。snc 変種では $SO 自身が
	#   自動ロードされないので、priority は変えずに明示ロードだけ足す(既定の梯子を測る意図は保つ)。
	SRAVA_CACHE_DIR="$D-e" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); } module(\"$SO\",{});
	  print(\"V\", volume(solidify(cast(\"mf-mesh3d\", box(2,2,2)))));" "$SRAVA" >/dev/null 2>&1
	NMF=$(count_tag "$D-e" "$TAG")
	if [ "$NMF" != "0" ] ; then
		echo "FAIL: solidify(mf) が nef へ行った ($TAG が $NMF 個)。double 入力は geogram (double のまま) が正" ; exit 1
	fi
	SRAVA_CACHE_DIR="$D-f" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); } module(\"$SO\",{});
	  print(\"V\", volume(solidify(box(2,2,2))));" "$SRAVA" >/dev/null 2>&1
	NCG=$(count_tag "$D-f" "$TAG")
	if [ "$NCG" = "0" ] ; then
		echo "FAIL: solidify(cg) が nef へ行かなかった ($TAG が 0 個)。厳密入力は厳密のままが正" ; exit 1
	fi
	echo "NEF-GGCROSS-OK[$VAR] gg->nf=$V 混成=$MIX 純nf=$PURE minkowski=$MK offset(直接=$D1 2段=$D2) solidify(mf)=geogram solidify(cg)=nef"
	;;

chain)
	# ★型維持: 全ての mesh cache が NEF3 で、MESH への往復が 1 つも無いこと。
	OUT=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	      var s = box(1,1,1);
	      var u = union(union(s, translate(s,[0.5,0,0])), translate(s,[0,0.5,0]));
	      print(\"VOL\", volume(u));" "$SRAVA" 2>&1)
	V=$(echo "$OUT" | sed -n 's/^VOL //p')
	if [ -z "$V" ] ; then echo "FAIL: chain が値を返さなかった" ; echo "$OUT" ; exit 1 ; fi
	nmesh=$(count_tag "$D-a" MESH)
	nnef=$(count_tag "$D-a" "$TAG")
	if [ "$nmesh" -ne 0 ] ; then
		echo "FAIL: MESH cache が $nmesh 個ある = Nef 型を維持できず往復変換が入っている"
		exit 1
	fi
	if [ "$nnef" -lt 3 ] ; then echo "FAIL: NEF3 cache が $nnef 個しかない" ; exit 1 ; fi
	echo "NEF-CHAIN-OK vol=$V nef=$nnef mesh=$nmesh"
	;;

minkowski)
	# ★Minkowski 和 (#3440)。offset の前段となるプリミティブ。
	#   ① 軸平行な箱どうしは **箱** になる: box(a) ⊕ box(b) = box(a+b) → 体積は厳密に (2+1)^3 = 27
	#   ② cg の mesh を渡しても計算できる (all-foreign sig + 昇格読み)。結果は nf 型
	#   ③ ★異カーネル混成 (mf, cg) の組。sig は入力 3 型の**全 9 組**を書いてある
	#   ④ 非有界 (complement の結果) は **明示エラー**。CGAL は渡すと stderr に文言を出して
	#      片方をそのまま返すので、黙って 8 や 27 が返ってはいけない
	OUT=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	      print(\"VOL\", volume(minkowski(box(2,2,2), box(1,1,1))));" "$SRAVA" 2>&1)
	V=$(echo "$OUT" | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V" 'BEGIN{ d=a-27; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: box(2)⊕box(1) の体積が $V (期待 27)" ; echo "$OUT" ; exit 1 ; fi
	# 型維持: 箱 2 つ + 結果が全て自型で、MESH への往復が無いこと
	nmesh=$(count_tag "$D-a" MESH)
	nnef=$(count_tag "$D-a" "$TAG")
	if [ "$nmesh" -ne 0 ] ; then
		echo "FAIL: MESH cache が $nmesh 個ある = 結果が Nef 型で返っていない" ; exit 1
	fi
	if [ "$nnef" -lt 3 ] ; then echo "FAIL: $TAG cache が $nnef 個しかない (箱 2 + 和 1 を期待)" ; exit 1 ; fi
	# ② cgal が作った箱 (MESH) を minkowski に渡す。cgal.so に minkowski は無いので
	#    nef の all-foreign sig (cg-mesh3d,cg-mesh3d)->nf で routing されるはず。
	#    ★ここは priority を上げられない (上げると box() 自体が nef に行き、cg 入力にならない)。
	#      2 変種は **同 priority(5)** なので、上げずに測ると相手の変種が勝つことがある
	#      (実際に踏んだ) → もう一方を "off" にして、どちらが計算したかを一意にする。
	OUT2=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); } module(\"$SO\");
	       print(\"VOL\", volume(minkowski(box(2,2,2), box(1,1,1))));" "$SRAVA" 2>&1)
	V2=$(echo "$OUT2" | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V2" 'BEGIN{ d=a-27; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then
		echo "FAIL: cg の箱どうしの minkowski が $V2 (期待 27) = all-foreign sig で routing できていない"
		echo "$OUT2" ; exit 1
	fi
	if [ "$(count_tag "$D-b" "$TAG")" -lt 1 ] ; then
		echo "FAIL: cg 入力の minkowski の結果が $TAG で書かれていない" ; exit 1
	fi
	# ③ ★異カーネル混成の組 (mf, cg)。sig は入力 3 型の全 9 組を書いてあるので、
	#    manifold が作った箱 (MFM3) と cgal が作った箱 (MESH) を**そのまま**渡せる。
	#    どちらも昇格読みで nf になり、結果は nf 型。
	OUT4=$(SRAVA_CACHE_DIR="$D-b2" SRAVA_SOURCE="if (module_loaded(\"$OTHER_SO\")) { module(\"$OTHER_SO\",\"off\"); }
	       module(\"manifold.so\",{priority:99}); var a = box(2,2,2);
	       module(\"cgal.so\",{priority:120});   var b = box(1,1,1);
	       module(\"$SO\"); print(\"VOL\", volume(minkowski(a, b)));" "$SRAVA" 2>&1)
	V4=$(echo "$OUT4" | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V4" 'BEGIN{ d=a-27; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then
		echo "FAIL: (mf,cg) 混成の minkowski が $V4 (期待 27) = 混成の組が sig に無いか昇格読みが効いていない"
		echo "$OUT4" ; exit 1
	fi
	nmfm=$(count_tag "$D-b2" MFM3)
	nmesh2=$(count_tag "$D-b2" MESH)
	if [ "$nmfm" -lt 1 ] || [ "$nmesh2" -lt 1 ] ; then
		echo "FAIL: 混成の入力が mf($nmfm) と cg($nmesh2) に分かれていない = 混成を測れていない"
		exit 1
	fi
	if [ "$(count_tag "$D-b2" "$TAG")" -lt 1 ] ; then
		echo "FAIL: 混成 minkowski の結果が $TAG で書かれていない" ; exit 1
	fi
	# ④ 非有界は明示エラー (黙って値を返さない)
	OUT3=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	       print(\"VOL\", volume(minkowski(box(2,2,2), complement(box(1,1,1)))));" "$SRAVA" 2>&1)
	V3=$(echo "$OUT3" | sed -n 's/^VOL //p')
	if [ -n "$V3" ] ; then
		echo "FAIL: 非有界を含む minkowski が値 '$V3' を返した (明示エラーになるべき)"
		echo "$OUT3" ; exit 1
	fi
	# ★エラーの出所まで見る (minkowski が弾いたのか、下流の volume が弾いたのか)。
	#   ここを単に grep "minkowski" にすると **cache dir 名 (nefcache-snc-minkowski) が
	#   出力に出るので常にヒットして**テストが空振りする (実際に踏んだ)。
	if ! echo "$OUT3" | grep -qE "ERROR.*minkowski:" ; then
		echo "FAIL: minkowski 自身の明示エラーが出ていない (下流の volume が弾いただけかもしれない)"
		echo "$OUT3" ; exit 1
	fi
	echo "NEF-MINKOWSKI-OK nf=$V cg入力=$V2 mf×cg混成=$V4 nef=$nnef mesh=$nmesh (非有界は明示エラー)"
	;;

offset)
	# ★3D offset (#3440 の 2: cgal.so から移設)。cgal 時代の 5 ケースを振り替えたもの。
	#   旧テストは export の nv/nf を見ていたが、**planner の nv/nf は nef の cache では当てにならない**
	#   (cg の "MESH" 前提で payload 先頭を読むので、先頭 1 バイトが形式の NEFB ではずれる) ので
	#   体積で見る。体積の方が意味が強い (下の ②③ は厳密値)。
	#   ① 膨張: 8 < V < 27 (箱 8 と、角が四角い場合の 27 の間) かつ subdiv が細かいほど大きい
	#   ② 収縮: box(2,2,2) を 0.3 収縮 = 1.4^3 = 2.744 (角は鋭いまま = 厳密値)
	#   ③ 中空箱: box(3,3,3) --- offset(box(3,3,3),-0.5) = 27 - 8 = 19 (厳密値)
	#   ④ 非有界は明示エラー
	V1=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	     print(\"VOL\", volume(offset(box(2,2,2), 0.5)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	V0=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	     print(\"VOL\", volume(offset(box(2,2,2), 0.5, 0)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V1" -v b="$V0" 'BEGIN{ print (a!="" && b!="" && a>8 && a<27 && b>8 && b<27 && a>b) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then
		echo "FAIL: 膨張 subdiv=1:$V1 subdiv=0:$V0 (8<V<27 かつ subdiv=1 の方が大きいはず)" ; exit 1
	fi
	VIN=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	      print(\"VOL\", volume(offset(box(2,2,2), -0.3)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	ok=$(awk -v a="$VIN" 'BEGIN{ d=a-2.744; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: 収縮 volume=$VIN (期待 1.4^3=2.744)" ; exit 1 ; fi
	VSH=$(SRAVA_CACHE_DIR="$D-b2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	      print(\"VOL\", volume(box(3,3,3) --- offset(box(3,3,3), -0.5)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	ok=$(awk -v a="$VSH" 'BEGIN{ d=a-19; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: 中空箱 volume=$VSH (期待 27-8=19)" ; exit 1 ; fi
	OUT4=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	       print(\"VOL\", volume(offset(complement(box(2,2,2)), 0.5)));" "$SRAVA" 2>&1)
	if echo "$OUT4" | grep -q "^VOL " ; then
		echo "FAIL: 非有界の offset が値を返した (明示エラーになるべき)" ; echo "$OUT4" ; exit 1
	fi
	if ! echo "$OUT4" | grep -qE "ERROR.*offset:" ; then
		echo "FAIL: offset 自身の明示エラーが出ていない" ; echo "$OUT4" ; exit 1
	fi
	echo "NEF-OFFSET-OK 膨張=$V1/$V0 収縮=$VIN 中空=$VSH (非有界は明示エラー)"
	;;

cavity)
	# ★★ 内部空洞を持つ立体が cache を渡っても壊れないこと (#3440 で発覚した #3433 のバグの回帰)。
	#   hybrid は「有界かつ 2-多様体」なら境界形式で書いていたが、**中空立体は読み戻しで空洞が
	#   中実になる** (Nef_polyhedron_3(Mesh) が入れ子シェルを別立体として和で取り込む) ため、
	#   difference(box(3,3,3), 内側の box) の体積が 26 → 27 に化けていた (snc は 26 で正しかった)。
	#   → volume が 2 個 (無限体積 + 中身 1 個) のときだけ境界形式にする、で修正。
	V=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	    print(\"VOL\", volume(difference(box(3,3,3), translate(box(1,1,1),[1,1,1]))));" "$SRAVA" 2>&1 |
	    sed -n 's/^VOL //p')
	ok=$(awk -v a="$V" 'BEGIN{ d=a-26; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then
		echo "FAIL: 空洞つき立体の体積が $V (期待 27-1=26)。境界形式で空洞が失われている" ; exit 1
	fi
	# 空洞を **さらに op へ渡して** も壊れない (cache を 2 回渡る)
	V2=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	     var h = difference(box(3,3,3), translate(box(1,1,1),[1,1,1]));
	     print(\"VOL\", volume(translate(h,[10,0,0])));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V2" 'BEGIN{ d=a-26; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: 空洞つき立体を 2 段渡すと $V2 (期待 26)" ; exit 1 ; fi
	# ★条件を締めすぎていないこと: **離れた複数立体**は入れ子でないので境界形式のままでよい
	#   (面の集まりから組み直しても和として正しく復元する)。ここが SNC に落ちると、部品を
	#   combine/union で並べる普通のモデルで hybrid の利点 (cache が cg 並み) が消える。
	V3=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	     print(\"VOL\", volume(union(box(1,1,1), translate(box(1,1,1),[5,0,0]))));" "$SRAVA" 2>&1 |
	     sed -n 's/^VOL //p')
	ok=$(awk -v a="$V3" 'BEGIN{ d=a-2; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: 離れた 2 立体の体積が $V3 (期待 1+1=2)" ; exit 1 ; fi
	# ★★ 昇格読み (cg/mf → nf) でも空洞が保たれること (#3440 で発覚した読み側のバグの回帰)。
	#   cg も mf も中空立体を正しく表現できるのに、nf へ昇格した瞬間に 26 → 27 になっていた。
	#   原因は Nef_polyhedron_3(Mesh) が入れ子シェルを和で取り込むこと。シェルごとに Nef を作り
	#   対称差 (even-odd) で畳むよう直した。
	V4=$(SRAVA_CACHE_DIR="$D-b2" SRAVA_SOURCE="module(\"$SO\");
	     var h = difference(box(3,3,3), translate(box(1,1,1),[1,1,1]));
	     print(\"VOL\", volume(cast(\"$TYPE\", h)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V4" 'BEGIN{ d=a-26; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: cg→nf 昇格で空洞が失われた: $V4 (期待 26)" ; exit 1 ; fi
	V5=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="module(\"manifold.so\",{priority:99});
	     var h = difference(box(3,3,3), translate(box(1,1,1),[1,1,1]));
	     module(\"$SO\",{priority:120});
	     print(\"VOL\", volume(cast(\"$TYPE\", h)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V5" 'BEGIN{ d=a-26; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: mf→nf 昇格で空洞が失われた: $V5 (期待 26)" ; exit 1 ; fi
	# ★3 段の入れ子 (立体 ⊃ 空洞 ⊃ 立体)。even-odd なので深さに関わらず正しいはず。
	#   216 - 64 + 1 = 153。単純な「外殻 − 空洞」では内側の立体を落とす。
	V6=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\");
	     var shell = difference(box(6,6,6), translate(box(4,4,4),[1,1,1]));
	     var inner = translate(box(1,1,1),[2,2,2]);
	     print(\"VOL\", volume(cast(\"$TYPE\", shell +++ inner)));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V6" 'BEGIN{ d=a-153; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then echo "FAIL: 3 段の入れ子が $V6 (期待 216-64+1=153)" ; exit 1 ; fi
	echo "NEF-CAVITY-OK 空洞=$V 2段=$V2 離れた2立体=$V3 cg昇格=$V4 mf昇格=$V5 3段入れ子=$V6"
	;;

convex)
	# ★凸分解 (#3441)。L 字 (凹形状) を凸片へ割る。
	#   ① 凸形状は 1 片・凹形状は 2 片以上 (nparts で数える)
	#   ② ★分解しても **体積は変わらない**。さらに **各片の体積の合計**も元と同じ
	#      (片は内部で交わらない) = part(d,i) が正しく取り出せている証拠
	#   ③ ★内壁が入るので **cg へ降格できない** (境界が 2-多様体でなくなる) = 分解された証拠。
	#      unify で内壁を消すと降格できるようになる (#3442 と組で意味を持つ)
	#   ④ 範囲外の part は明示エラー
	L='difference(box(3,3,3), translate(box(2,2,4),[1,1,-0.5]))'
	NB=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	     print(\"N\", nparts(convex_decomposition(box(2,2,2))));" "$SRAVA" 2>&1 | sed -n 's/^N //p')
	[ "$NB" = "1" ] || { echo "FAIL: 箱の凸片が $NB (期待 1)" ; exit 1 ; }
	OUT=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	      var d = convex_decomposition($L);
	      print(\"N\", nparts(d));
	      print(\"VD\", volume(d));
	      print(\"P0\", volume(part(d,0)));
	      print(\"P1\", volume(part(d,1)));" "$SRAVA" 2>&1)
	NL=$(echo "$OUT" | sed -n 's/^N //p')
	VD=$(echo "$OUT" | sed -n 's/^VD //p')
	P0=$(echo "$OUT" | sed -n 's/^P0 //p')
	P1=$(echo "$OUT" | sed -n 's/^P1 //p')
	[ "$NL" = "2" ] || { echo "FAIL: L 字の凸片が $NL (期待 2)" ; echo "$OUT" ; exit 1 ; }
	ok=$(awk -v a="$VD" 'BEGIN{ d=a-15; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 分解後の体積が $VD (期待 27-12=15 = 分解前と同じ)" ; exit 1 ; }
	ok=$(awk -v a="$P0" -v b="$P1" 'BEGIN{ s=a+b; d=s-15; if(d<0)d=-d;
	     print (a!="" && b!="" && a>0 && b>0 && d<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || {
		echo "FAIL: 片の体積 $P0 + $P1 が 15 にならない = part(d,i) が塊を取り出せていない" ; exit 1 ; }
	OUT2=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	       print(\"VOL\", volume(cast(\"cg-mesh3d\", convex_decomposition($L))));" "$SRAVA" 2>&1)
	if echo "$OUT2" | grep -q "^VOL " ; then
		echo "FAIL: 分解直後が cg へ降格できた = 内壁が入っていない (分解できていない)" ; exit 1
	fi
	OUT3=$(SRAVA_CACHE_DIR="$D-b2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	       print(\"VOL\", volume(part(convex_decomposition($L), 5)));" "$SRAVA" 2>&1)
	if echo "$OUT3" | grep -q "^VOL " ; then
		echo "FAIL: 範囲外の part が値を返した (明示エラーになるべき)" ; exit 1
	fi
	if ! echo "$OUT3" | grep -qE "ERROR.*part:" ; then
		echo "FAIL: part の範囲外エラーが出ていない" ; echo "$OUT3" ; exit 1
	fi
	echo "NEF-CONVEX-OK 箱=$NB 片 / L字=$NL 片 (体積 $P0 + $P1 = $VD) / 内壁で cg 降格不可 / 範囲外はエラー"
	;;

unify)
	# ★内壁除去 (#3442) = Nef の正則化 closure(interior())。
	#   ① 単一立体は不変 ② ★**空洞は保つ** (空洞の境界は片側が立体でない = 本物の境界)
	#   ③ ★凸分解の内壁を消せる = **cg へ降格できるようになる** (内壁が消えた観測可能な証拠)
	#   ★repair とは別物 (repair は形を変えない)。自動ではやらない。
	L='difference(box(3,3,3), translate(box(2,2,4),[1,1,-0.5]))'
	V1=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	     print(\"VOL\", volume(unify(box(2,2,2))));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V1" 'BEGIN{ d=a-8; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 単一立体の unify が $V1 (期待 8 = 不変)" ; exit 1 ; }
	V2=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	     print(\"VOL\", volume(unify(difference(box(3,3,3), translate(box(1,1,1),[1,1,1])))));" \
	     "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	ok=$(awk -v a="$V2" 'BEGIN{ d=a-26; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 中空箱の unify が $V2 (期待 26 = 空洞を保つ)" ; exit 1 ; }
	# ★内壁が消えたことの**観測**: 内壁があると境界が 2-多様体でないので cg へ降格できない。
	#   unify 後に降格できれば内壁は消えている。ただし cgal は SNC を読めない (#3440 の 3) ので
	#   この観測は **hybrid でしかできない**。snc は体積だけ見る (契約どおりの限界)。
	if [ "$VAR" = hybrid ] ; then
		V3=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
		     print(\"VOL\", volume(cast(\"cg-mesh3d\", unify(convex_decomposition($L)))));" \
		     "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
		ok=$(awk -v a="$V3" 'BEGIN{ d=a-15; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
		[ "$ok" = "1" ] || {
			echo "FAIL: 凸分解を unify した結果の cg 降格が $V3 (期待 15) = 内壁が消えていない" ; exit 1 ; }
	else
		V3=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
		     print(\"VOL\", volume(unify(convex_decomposition($L))));" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
		ok=$(awk -v a="$V3" 'BEGIN{ d=a-15; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: 凸分解を unify した体積が $V3 (期待 15)" ; exit 1 ; }
		V3="$V3 (cg 降格は snc では不可 = 設計どおり)"
	fi
	echo "NEF-UNIFY-OK 単一=$V1 中空=$V2 (空洞維持) 内壁除去後の cg 降格=$V3"
	;;

selfx)
	# ★自己交差したメッシュ (自分を貫く tube) を nef へ渡したときの振る舞い。
	#   Nef は「有効な立体の境界」を前提とするので自己交差は受け取れない。
	#   ★これを入れる前は CGAL の assertion で **agent プロセスごと落ちて**いた
	#     ("agent closed unexpectedly" としか出ず原因が分からなかった) → 明示エラーにした。
	#   ★再構成の道は #3445 の @solidify@ が担う (別モード solidify)。ただし入口が違う:
	#     solidify は **repair を挟まず** cast で nf にしてから呼ぶ (自己交差は Nef 構築を
	#     素通りするので、壊れた形のまま nf に入り面が取り出せる)。ここで見ているのは
	#     「repair (autorefine) の出力は開いていて Nef にできない」= 明示エラーになること。
	#   ★旧コメントの「分割して union すると 50.51 が正しい値」は誤りだったので消した。分割すると
	#     継ぎ目がマイター接合から平らな蓋どうしの重ねに変わって角が太る (自己交差の無い tube で
	#     一本物 52.23 vs 2 分割 union 54.48)。面が囲む本当の体積は solidify の 48.61。
	S='tube([[[0,0,0],0.8],[[10,0,0],0.8],[[10,0,2],0.8],[[0,0,2],0.8],[[0,0,4],0.8],[[5,0,4],0.8],[[5,0,-2],0.8]], 12)'
	V=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="print(\"VALID\", valid($S));" "$SRAVA" 2>&1 | sed -n 's/^VALID //p')
	[ "$V" = "0" ] || { echo "FAIL: テスト用の形状が自己交差していない (valid=$V) = テストの前提が崩れた" ; exit 1 ; }
	# repair (autorefine) してから nf へ: 落ちずに **明示エラー** になること
	OUT=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="module(\"$SO\");
	      print(\"VOL\", volume(cast(\"$TYPE\", repair($S))));" "$SRAVA" 2>&1)
	if echo "$OUT" | grep -q "^VOL " ; then
		echo "FAIL: 自己交差メッシュの nf 変換が値を返した (明示エラーになるべき)" ; echo "$OUT" ; exit 1
	fi
	if echo "$OUT" | grep -q "agent closed unexpectedly" ; then
		echo "FAIL: agent が落ちた = Nef 構築の例外ガードが効いていない" ; echo "$OUT" ; exit 1
	fi
	if ! echo "$OUT" | grep -q "ERROR" ; then
		echo "FAIL: エラーメッセージが出ていない" ; echo "$OUT" ; exit 1
	fi
	echo "NEF-SELFX-OK 自己交差は明示エラー (agent は生存)"
	;;

solidify)
	# ★ #3445: 壊れた境界 (自己交差した閉メッシュ) からソリッドを組み直す。
	#   面ごとの Nef を n 項 union → 有界セルを mark。**重い op** なので既定経路には無く、
	#   利用者が明示的に呼ぶ (sig は (nf)->nf の 1 本・cg から使うときは cast を挟む)。
	SELFX='tube([[[0,0,0],0.8],[[10,0,0],0.8],[[10,0,2],0.8],[[0,0,2],0.8],[[0,0,4],0.8],[[5,0,4],0.8],[[5,0,-2],0.8]], 12)'
	OUT=$(SRAVA_CACHE_DIR="$D-a" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	      var n = cast(\"$TYPE\", $SELFX);
	      print(\"BEFORE\", volume(n));
	      print(\"AFTER\",  volume(solidify(n)));" "$SRAVA" 2>&1)
	B=$(echo "$OUT" | sed -n 's/^BEFORE //p')
	A=$(echo "$OUT" | sed -n 's/^AFTER //p')
	# ① 前提: 自己交差したままだと重なりを二重に数える (52.02)。ここが変わったらテストの前提が崩れた。
	ok=$(awk -v a="$B" 'BEGIN{ d=a-52.016415536710625; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 自己交差のままの体積が $B (期待 52.0164 = 二重計上)"; echo "$OUT"; exit 1; }
	# ② ★本題: solidify すると正しい体積になる。
	ok=$(awk -v a="$A" 'BEGIN{ d=a-48.608763289596638; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: solidify 後の体積が $A (期待 48.6088)"; echo "$OUT"; exit 1; }
	# ③ 健全な立体は不変 (8)・空洞は埋まらない (26)。★Mark_bounded_volumes は有界セルを無差別に
	#    塗るので、連結成分ごとの深さ合成が壊れると中空箱が 27 になる = ここで落ちる。
	OUT2=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	       print(\"BOX\", volume(solidify(box(2,2,2))));
	       var h = difference(box(3,3,3), translate(box(1,1,1),[1,1,1]));
	       print(\"HOLLOW\", volume(solidify(h)));
	       var t = union(box(1,1,1), translate(box(1,1,1),[5,0,0]));
	       print(\"TWO\", volume(solidify(t)));
	       print(\"TWOP\", nparts(solidify(t)));" "$SRAVA" 2>&1)
	for pair in "BOX 8" "HOLLOW 26" "TWO 2" "TWOP 2" ; do
		k=${pair% *}; want=${pair#* }
		got=$(echo "$OUT2" | sed -n "s/^$k //p")
		ok=$(awk -v a="$got" -v w="$want" 'BEGIN{ d=a-w; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: solidify($k) が $got (期待 $want)"; echo "$OUT2"; exit 1; }
	done
	# ④ ★cg / mf 入力も直接受ける (all-foreign 行)。受け取り側は sig の出力型から欲しい型を作るので、
	#    cg("MESH") / mf("MFM3") は codec が nf へ昇格読みして届く。自己交差したままでも変換は通る。
	#    ⚠ **priority:120 が要る** (2026-08-25)。既定の梯子は geogram 6 > nef 5 で、mf(double) 入力は
	#      geogram (double のまま) へ行くのが正しい振り分けだから。ここで見たいのは「nef を選べば
	#      nef が mf を昇格読みして同じ答えを出す」ことなので、明示的に nef を最上位にする。
	#      既定の振り分けそのものは ggcross ⑥ が見る。
	OUT3=$(SRAVA_CACHE_DIR="$D-c" SRAVA_SOURCE="module(\"$SO\",{priority:120});
	       print(\"CG\", volume(solidify($SELFX)));
	       print(\"MF\", volume(solidify(cast(\"mf-mesh3d\", $SELFX))));" "$SRAVA" 2>&1)
	for k in CG MF ; do
		got=$(echo "$OUT3" | sed -n "s/^$k //p")
		ok=$(awk -v a="$got" 'BEGIN{ d=a-48.608763289596638; if(d<0)d=-d; print (a!="" && d<1e-9) ? 1 : 0 }')
		[ "$ok" = "1" ] || { echo "FAIL: $k 入力の solidify が $got (期待 48.6088)"; echo "$OUT3"; exit 1; }
	done
	echo "NEF-SOLIDIFY-OK 自己交差 $B -> $A / 健全は不変 (箱 8・中空 26・2 成分 2) / cg・mf 入力も可"
	;;

subdiv)
	# ★ offset の近似球の細分化 subdiv が **0〜5 で本当に効くこと**と、
	#   **範囲外を黙って丸めないこと** (#3440・2026-08-20)。
	#
	#   旧実装は subdiv>3 を黙って 3 へ丸めていた。利用者は 4 を頼んで 3 の結果を受け取り、
	#   しかもそれと気づけない (「黙ってフォールバックしない」原則違反)。
	#   ★ 値の正しさではなく **「頼んだ通りに効いたか」** を見るテストなので、
	#     真値 (Steiner) に対する誤差が subdiv=3 より 4 の方が小さいことを条件にする。
	#     黙って丸めていれば両者は **完全に同じ値**になるので必ず落ちる。
	#   box(2,2,2) を d=0.1 → 真値 10.592684349420175 (V=8, A=24, M=6pi)。
	rm -rf "$D-sd" "$D-sd2"
	OUT=$(SRAVA_CACHE_DIR="$D-sd" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	  print(\"A\", volume(offset(box(2,2,2), 0.1, 3)));
	  print(\"B\", volume(offset(box(2,2,2), 0.1, 4)));" "$SRAVA" 2>&1)
	A=$(echo "$OUT" | sed -n 's/^A //p'); B=$(echo "$OUT" | sed -n 's/^B //p')
	if [ -z "$A" ] || [ -z "$B" ]; then echo "FAIL: 値が出ない A=$A B=$B"; echo "$OUT"; exit 1; fi
	ok=$(awk -v a="$A" -v b="$B" 'BEGIN{
		t = 10.592684349420175;
		ea = a-t; if(ea<0) ea=-ea;
		eb = b-t; if(eb<0) eb=-eb;
		# (1) 近似球は内接なので **どちらも過小評価** (2) subdiv=4 の方が誤差が小さい
		#     (黙って丸めていれば a==b になり ここで落ちる)
		print (a<t && b<t && eb < ea*0.5) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: subdiv が効いていない subdiv3=$A subdiv4=$B (同値なら黙って丸めている)"; exit 1; }
	# ★ 範囲外は **明示エラー**。しかも**即座に**返ること。
	#   ⚠ ここは必ず **上限 +1 (= 7)** を使う。上限そのもの (6) は正当な値で、
	#     通すと 13 分計算してしまい ctest が TIMEOUT で落ちる (2026-08-20 に実際にやった)。
	OUT2=$(SRAVA_CACHE_DIR="$D-sd2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
	  print(\"C\", volume(offset(box(2,2,2), 0.1, 7)));" "$SRAVA" 2>&1)
	rm -rf "$D-sd2"
	case "$OUT2" in
	*"out of range"*) : ;;   # ★ 文言は英語 (2026-08-26 に日本語から統一)
	*) echo "FAIL: subdiv=7 が明示エラーにならない"; echo "$OUT2"; exit 1 ;;
	esac
	echo "NEF-SUBDIV-OK subdiv3=$A subdiv4=$B (範囲外は明示エラー)" ;;
*)
	echo "FAIL: unknown mode $MODE" ; exit 1 ;;
esac
exit 0

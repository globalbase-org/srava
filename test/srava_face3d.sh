#!/bin/sh
# #3533 — **2D 領域の型が 2 つに割れた**ことの回帰。
#
# $1 = srava 実行体 / $2 = モジュール名 (.so) / $3 = 型の前置 ("cg" / "mf")
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- 固定するもの ------------------------------------------------------------------
#   *-cross2d   z=0 の **簡易表現**    … 生成したまま / cast で降ろしたもの
#   *-face3d    空間に置かれた一般表現 … transform / section を通ったもの
#
# ★★ 規約は 4 つ。この検査はその 4 つを **1 つずつ**見る:
#   ① transform 系は face3d を返す。
#      ★★ #3554 段4 (2026-09-19): **`transform` だけ例外ができた** — 行列が z=0 平面を平面へ
#        写すなら @transform#xy@ の行が選ばれ、**cross2d のまま**返る。
#        規約① の「常に」は *軸は式の中の値なので型では区別できない* ことの代償だったが、
#        AK_MATCH (値の中身を見るマッチ関数) がその前提を外した。
#        ⚠ 救えるのは **cross2d 入力だけ** — face3d は枠が任意なので行列だけでは決まらない。
#        ⚠ translate / rotate / scale / mirror は **まだ規約①のまま** (段5 で同じ形にする)。
#      ⚠ 根拠は「軸は sig に見えないインライン値」— @rotate(r,"z",90)@ と @rotate(r,"x",90)@ を
#        型で区別する手段が無い。⇒ 区別しない、しかない。
#   ② 降格は **cast だけ**。@frame_is_default()@ が偽なら明示エラーで、幾何は動かさない。
#   ③ ブールは「全部 cross2d なら cross2d・face3d が混ざれば face3d」。
#      hull は **face3d が混ざれば立体** (ひさ裁定 2026-09-15)。⇒ 2D の凸包が欲しければ
#      @cast("*-cross2d", …)@ で先に降ろす。⚠ 全部が同一平面なら明示エラー
#        (cgal は 2026-09-15 まで **面 8 枚・閉じている・体積 1e-15 の「立体」を黙って返して**
#         いた — 退化検査が面数と閉じているかだけを見ていたため)。
#   ④ SVG は face3d を断る (平面を表せない)・DXF は OCS で書く。
#   ★ bbox / centroid は cross2d=局所 2 成分・face3d=world 3 成分 (答えの *形* が型で決まる)。
#
# ★★★ この検査の主眼は **③ の bbox が経路に依らないこと**。
#   x 軸まわり 180 度 と 90 度 x 2 は world では同じ図形なので、bbox も同じでなければ
#   ならない。#3533 以前は局所座標で答えていたので **割れていた** (同じ図形に 2 つの答え)。
#   ⇒ ここを壊すと赤くなることを 2026-09-15 に実地で確認した (負の対照)。
SRAVA="$1"
SO="${2:?module .so not given}"
PFX="${3:?type prefix (cg|mf) not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"

# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

fails=0
skips=""
# ★★★ #3544: **occt だけ名乗りを幾何から導く** (ビットでは持たない・ひさ判断 2026-09-17)。
#   理由: occt の値は TopoDS_Face = 空間の面を直に持つので、別のビットを添えると
#   「幾何は z=0 の外なのに cross2d を名乗る」矛盾した値が作れてしまう。cg / mf の「枠」は
#   *値そのものの座標系* なので枠と幾何が離れず、この矛盾は起こらない。
#   ⇒ occt では **面内回転** (rotate(...,"z",90) / rotate(...,"x",180)) の結果が
#     規約①どおり sig では face3d のまま *値に訊くと cross2d* になり、bbox / centroid が
#     **2 成分**で返る。⚠ これは承知の上の 1 ケース (modules/occt/h/oc/c++/ocShape.h)。
#   ★ 規約①そのもの (routing が face3d として扱うこと) は崩れていない — 崩れるとこの検査の
#     ⑤「cross2d + face3d の和」や ③「型が face3d でも降ろせる」が赤くなる。
# ★★ #3554 段5 (2026-09-19): **cgal も面内回転で 2 成分になった**。ただし occt とは *理屈が違う*:
#     occt … 名乗りを **幾何から導く** ⇒ sig は face3d のまま *値に訊くと* cross2d (食い違いを許す)
#     cgal … routing が @rotate#z@ / @translate#xy@ 等の **行**を選ぶ ⇒ *sig も値も* cross2d (一致する)
#   ⇒ 見える答え (bbox の成分数) は同じだが、**sig が何と言っているかが違う**。
#   ⚠ occt 以外で 2D を持つのは cgal / manifold の 2 つだけ (どちらも段5 で揃えた)。
INPLANE_NC=3        # 面内回転 **z 軸** の答えの成分数
INPLANE_X180_NC=3   # 面内回転 **x 軸 180 度** の答えの成分数
INPLANE_WHY=""
X180_WHY=""
if [ "$PFX" = "oc" ]; then
	INPLANE_NC=2; INPLANE_X180_NC=2
	INPLANE_WHY=" (occt は名乗りを幾何から導くので面内回転は cross2d)"
	X180_WHY="$INPLANE_WHY"
elif [ "$PFX" = "cg" ] || [ "$PFX" = "mf" ]; then
	# ★★ #3554 段5: z 軸回転は @rotate#z@ の行が選ばれて **cross2d のまま**。
	# ⚠⚠ **x 軸 180 度は救えない** — 平面を保つかは *軸と角度の両方*で決まるのに、
	#   マッチ関数は @int match(d,e,argNo,arg)@ で **引数を 1 個ずつしか見られない** ので
	#   軸しか判定できない。⇒ 保守的に face3d のまま (3 成分)。
	#   ★ ここを 2 にしたくなったら、*マッチ関数へ引数列を渡す設計* が要る (#3554 で要相談)。
	#   ⚠ 計算本体も **同じ判定 (軸が z か)** を使っている。行列で判定すると
	#     「sig は face3d と言っているのに cross2d が返る」= 型スタンプが嘘になる。
	INPLANE_NC=2
	INPLANE_WHY=" (#3554 段5: rotate#z の行が選ばれ cross2d のまま)"
	X180_WHY=" (#3554 段5: 軸しか見られないので x180 は救えない)"
fi
bbox_nc() {   # bbox ("[[..],[..]]") の成分数
	echo "$1" | sed -n 's/^\[\[\([^]]*\)\].*/\1/p' | awk -F, '{print NF}'
}
arr_nc() {    # 1 次元配列 ("[a,b,c]") の成分数
	echo "$1" | tr -d '[]' | awk -F, '{print NF}'
}
# ★★ #3544: occt を対象に加えたとき、**このカーネルに無い op** で赤が出た (hull / 2D ブール)。
#   ⚠ 黙って飛ばすと「規約を満たした」と読めてしまうので、**何を飛ばしたかを名前で言う**。
#     (今日の家系: 飛ばしたこと自体は正しくても、飛ばしたと書かないと過大評価になる)
has_op() {   # has_op <キャッシュ接尾> <式>  → op が在れば真
	case "$(run "$1" "print(\"V\", $2);")" in
		*"undefined variable"*|*"no module can execute"*) return 1 ;;
		*) return 0 ;;
	esac
}
run() {   # run <cache-suffix> <source>
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
$2" "$SRAVA" 2>&1
}
chks() {  # chks <name> <got> <expected>   完全一致
	if [ "$2" = "$3" ]; then echo "  ok  $1"
	else echo "FAIL: $1 が $2 (期待 $3)"; fails=$((fails+1)); fi
}
chkin() { # chkin <name> <haystack> <needle>   部分一致
	case "$2" in
	*"$3"*) echo "  ok  $1" ;;
	*) echo "FAIL: $1 に \"$3\" が無い: $2"; fails=$((fails+1)) ;;
	esac
}
vals() {  # 出力から "V " 行だけを取り出す
	echo "$1" | sed -n 's/^V //p'
}
chknum() { # chknum <name> <got-bbox> <expected-bbox>   数として絶対 1e-12 で比べる
	#   ⚠ 文字列一致では見られない。回転を **2 回**当てた枠は sin/cos の丸めを 2 度拾うので、
	#     world では同じ図形でも下位桁が 4e-16 ほど違う。⇒ 固定したいのは「3 単位ずれない」
	#     こと (#3533 以前は [[0,-3],[2,0]] 対 [[0,0],[2,3]] だった) であって bit 一致ではない。
	#   ⚠⚠ **libm が違えば下位桁も違う** (2026-09-16・simu01 が MinGW で踏んだ)。
	#     @rotate(U,"z",90)@ の bbox には sin(pi) 相当の *数学的にはゼロ* の量が入る:
	#       Linux/glibc 1.2246467991473532e-16   MinGW 1.2246063538223773e-16   (絶対差 4e-21)
	#     ⇒ **ゼロであるべき量を bit 一致で比べない**。数として比べればどちらも 0 と区別がつかない。
	ok=$(echo "$2|$3" | tr -d '[]' | awk -F'|' '{
		na = split($1, a, ","); nb = split($2, b, ",")
		if ( na != nb || na == 0 ) { print 0; exit }
		for ( i = 1 ; i <= na ; ++i ) { d = a[i] - b[i]; if ( d < 0 ) d = -d; if ( d > 1e-12 ) { print 0; exit } }
		print 1 }')
	if [ "$ok" = "1" ]; then echo "  ok  $1"
	else echo "FAIL: $1 が $2 (期待 $3)"; fails=$((fails+1)); fi
}

# ---- ① 型が 2 つに割れている ---------------------------------------------------------
#   ⚠ 型名そのものは print できないので、**routing のエラー文**に出させる。
#     受けられない op へ渡すと「入力型」が文言に入る。cast の目標型違いが一番素直。
O=$(run t1 'print("V", area(rect(2,3)));
print("V", area(rotate(rect(2,3),"z",90)));')
# ⚠⚠ **値の分割だけ glob を止める** (2026-09-22)。
#   `set -- $(…)` の $( ) は **クォートできない** (単語分割が要る) のでパス名展開に掛かる。
#   比べる値は `[1,1,0]` 形 = glob の **文字クラス**なので、カレント (ビルド dir) に `1` という
#   名前のファイルが 1 つあるだけで `1` に化け、**「値が違う」という、それらしい文言で赤くなる**
#   (2026-09-21 に実際に踏んだ。⚠ PIG_TIMING は *ファイルパス* を取る env なので
#   `PIG_TIMING=1` と打つと `1` というファイルができる)。
#   ★★ **実行と分割を分ける**のが肝。`set -f; set -- $(cmd)` と囲うと -f が $( ) の中まで効き、
#     cmd がシェル関数だと *その中の glob* まで殺す (実測: count_tus → objs_of の
#     `CMakeFiles/*/link.txt` が読めず「全 0 本」になった)。⇒ 先に変数へ取ってから分割する。
_SPLIT_=$(vals "$O")
set -f
set -- $_SPLIT_
set +f
chks "① 面積は型に依らない (cross2d)" "$1" "6"
chks "① 面積は型に依らない (face3d)"  "$2" "6"

# ---- ②★ bbox — cross2d は局所 2 成分 / face3d は world 3 成分 ------------------------
O=$(run t2 'var U = rect(2,3);
print("V", bbox(U));
print("V", bbox(rotate(U,"z",90)));
print("V", bbox(rotate(U,"x",180)));
print("V", bbox(rotate(rotate(U,"x",90),"x",90)));')
_SPLIT_=$(vals "$O")
set -f
set -- $_SPLIT_
set +f
chks "② cross2d の bbox は 2 成分"     "$1" "[[0,0],[2,3]]"
# ★ #3544: 「3 成分」を ",0]]" (= 第 3 成分が 0) で見ていたのは **代理**。数を数える。
#   ⇒ occt は面内回転で 2 成分になる (上の INPLANE_NC の注記)。[[proxy-instead-of-thing]]
chks "② 面内回転の bbox は $INPLANE_NC 成分$INPLANE_WHY" "$(bbox_nc "$2")" "$INPLANE_NC"
# ★★★ ここが本題 — 同じ world 図形に 1 つの答え。
chknum "★ 180 度 と 90 度 x 2 が一致"  "$3" "$4"

# ---- ②b centroid も同じ規約 (bbox と対) -----------------------------------------------
#   ⚠ 2026-09-15 まで **cgal は局所 2 成分・manifold は 2D を受けてすらいなかった**。
#     occt だけが world の 3 成分で、外れ値は cg/mf の方だった。
O=$(run t2b 'var U = rect(2,3);
print("V", centroid(U));
print("V", centroid(rotate(U,"x",180)));
print("V", centroid(rotate(rotate(U,"x",90),"x",90)));')
_SPLIT_=$(vals "$O")
set -f
set -- $_SPLIT_
set +f
chks  "②b cross2d の centroid は 2 成分" "$1" "[1,1.5]"
chknum "★ centroid も 180 度 と 90 度 x 2 が一致" "$2" "$3"
# ★ #3544: 以前は ",0]" を探していたが、それは「3 成分」の **代理**で、しかも第 3 成分が
#   厳密に 0 であることまで要求していた (occt は 1.8e-16 を返して落ちた)。
#   ⇒ ラベルどおり **成分の数**で見る。[[proxy-instead-of-thing]]
chks "②b 面内回転 (x180) の centroid は $INPLANE_X180_NC 成分$X180_WHY" "$(arr_nc "$2")" "$INPLANE_X180_NC"

# ---- ③ cast の降格 (規約②) ----------------------------------------------------------
#   幾何が z=0 に居るなら通る (型が face3d でも)。傾いていれば明示エラー。
O=$(run t3 "var U = rect(2,3);
print(\"V\", bbox(cast(\"$PFX-cross2d\", rotate(U,\"z\",90))));")
#   ⚠ @chks@ (bit 一致) ではなく @chknum@ — 期待値の @1.2246e-16@ は **sin(pi) 相当のゼロ**で、
#     libm が違うと下位桁が変わる (MinGW で実際に赤くなった)。見たいのは「局所 2 成分に戻ること」。
chknum "③ 枠が既定なら降ろせる (局所 2 成分に戻る)" "$(vals "$O")" "[[-3,0],[1.2246467991473532e-16,2]]"
O=$(run t3e "var U = rect(2,3);
print(\"V\", area(cast(\"$PFX-cross2d\", rotate(U,\"x\",90))));")
chkin "③ 傾いていれば明示エラー" "$O" "cast never moves geometry"

# ---- ④ hull (規約③) ------------------------------------------------------------------
if ! has_op c4 "area(hull(rect(2,2), rect(1,4)))"; then
	skips="$skips ④hull"
else
#   ⚠ 2D の長穴は [0,2]x[0,2] と [10,12]x[0,2] の凸包 = [0,12]x[0,2] = 面積 24。
#     translate は規約①で face3d を返すので、**cast で降ろしてから**渡す。
O=$(run t4 "print(\"V\", area(hull(rect(2,2), rect(1,4))));
print(\"V\", area(hull(cast(\"$PFX-cross2d\", translate(rect(2,2),[10,0,0])), rect(2,2))));")
_SPLIT_=$(vals "$O")
set -f
set -- $_SPLIT_
set +f
chks "④ cross2d だけの hull は 2D"        "$1" "7"
chks "④ cast して降ろせば 2D の長穴"      "$2" "24"
O=$(run t4e 'print("V", volume(hull(rotate(rect(2,2),"x",90), rect(1,4))));')
#   ⚠ 20/3 は最下位桁が丸めで決まるので、ここも bit 一致で比べない (③ と同じ理由・予防)。
chknum "④ face3d が混ざれば立体" "$(vals "$O")" "6.6666666666666661"
O=$(run t4c 'var R = rect(2,3);
print("V", area(hull(rotate(R,"x",90), rotate(R,"x",-90))));')
chkin "★ 全部が同一平面なら明示エラー (極薄の立体を黙って返さない)" "$O" "on one plane"
O=$(run t4m 'print("V", volume(hull(box(2,2,2), rect(2,2))));')
chks "④ 3D が混ざれば立体 (#3533 の 1 節)" "$(vals "$O")" "8"

fi
# ---- ⑤ ブール (規約③) ----------------------------------------------------------------
if ! has_op c5 "area(union(rect(2,3), rect(1,1)))"; then
	skips="$skips ⑤2Dブール"
else
O=$(run t5 'var U = rect(2,3);
print("V", area(union(U, translate(U,[1,0,0]))));')
chks "⑤ cross2d + face3d の和は通る" "$(vals "$O")" "9"
O=$(run t5e 'var U = rect(2,3);
print("V", area(union(U, rotate(U,"x",90))));')
chkin "⑤ 別平面どうしの和は明示エラー" "$O" "different planes"

fi
# ---- ⑥★ cache 経路 — 同じ式が cold と warm で同じ型・同じ値 ---------------------------
#   ⚠ 型スタンプはキャッシュのメモリにしか載らないので、**ブロブ自身が face3d を名乗れる**
#     必要がある (枠が既定でも枠の節を書く)。
#   ⚠⚠ **「2 回流して一致」だけでは検定にならない** — 型がブロブから落ちると cold も warm も
#     等しく 2 成分になるので、一致の検査は緑のまま通る (2026-09-15 の負の対照で実測)。
#     ⇒ 「warm でも 3 成分」を **別の検査として**置く。[[mf-cross2d-cache-path-coords]] と同じ型。
rm -rf "$D-t6"
SRC='module("'"$SO"'",{priority:99});module("geomutils.so",{});
print("V", bbox(rotate(rect(2,3),"z",90)));'
C1=$(SRAVA_CACHE_DIR="$D-t6" SRAVA_SOURCE="$SRC" "$SRAVA" 2>&1 | sed -n 's/^V //p')
C2=$(SRAVA_CACHE_DIR="$D-t6" SRAVA_SOURCE="$SRC" "$SRAVA" 2>&1 | sed -n 's/^V //p')
chks "⑥ cold と warm で同じ答え (型がブロブに残る)" "$C2" "$C1"
# ★ #3544: occt は名乗りの根拠が **幾何 = ブロブの中身そのもの**なので、往復しても
#   落ちようがない (「1 バイト書く」案が要らなくなった理由)。⇒ 期待は 2 成分。
chks "⑥ warm でも $INPLANE_NC 成分のまま$INPLANE_WHY" "$(bbox_nc "$C2")" "$INPLANE_NC"

# ---- ⑦★ 規約④ — DXF は置き場所を **書いて、読み戻せる** --------------------------------
#   ⚠ 2026-09-15 に往復で実測して穴が見つかった: 書き手は OCS (210/220/230 + 38) を出して
#     いたのに **読み手が 10/20 しか読んでいなかった** ⇒ 読み戻すと枠が落ちて z=0 に戻る。
#     しかも import_exts は dxf:*-face3d と申告しているので、*型スタンプは face3d なのに
#     値は cross2d* という食い違いになっていた (#3533 で避けたはずの形)。
#   ★ 「書ける」だけでは検定にならない — **読み戻して同じ場所に居る**ことまで見る。
#   ⚠ DXF の書き出しは cgal だけなので mf では飛ばす (op が無いことと拒否を混ぜない)。
if [ "$PFX" = "cg" ]; then
	D7="$D-t7"
	rm -rf "$D7"; mkdir -p "$D7"
	O=$(run t7 "var R = rect(2,3);
export(\"$D7/tilt.dxf\", rotate(R,\"x\",90), \"mm\");
print(\"V\", bbox(rotate(R,\"x\",90)));
print(\"V\", bbox(import(\"$D7/tilt.dxf\")));")
	_SPLIT_=$(vals "$O")
	set -f
	set -- $_SPLIT_
	set +f
	chknum "⑦ DXF を往復しても同じ平面に居る" "$2" "$1"
	chkin  "⑦ 往復後も 3 成分 (face3d)"       "$2" ",3]]"
fi

# ★★ #3544: **飛ばしたものを必ず言う**。0 件の報告に「何を根拠に緑と言っているか」を添える
#   のと同じ理由で、飛ばしたまま緑にすると規約を満たしたと読まれる。
if [ -n "$skips" ]; then echo "  ⚠ このカーネルに無いので飛ばした:$skips"; fi
if [ "$fails" = "0" ]; then echo "FACE3D-OK ($SO)$( [ -n "$skips" ] && echo " ⚠ skipped:$skips" )"; else echo "FACE3D-FAIL ($fails fail)"; fi
exit "$fails"

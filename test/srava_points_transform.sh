#!/bin/sh
# 点群の transform 一族 (translate / rotate / scale / mirror / transform) と union の回帰
# (#3578 ・ 2026-09-22)。
#
# $1 = srava 実行体 / env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- 何を検定しているか ------------------------------------------------------------
# ① 出力型     ★★ 本題。**面外へ出す変換だけが pt-cloud3d を返す**。
#              行は #xy / #z の変種 + 基底の 2 本で、選ぶのは値を見るマッチ関数 (#3554 の仕掛け)。
#              ⚠⚠ rotate だけは **軸が z か**しか見られない (マッチ関数は引数を 1 個ずつしか
#                見られず、角度が使えないため) ⇒ rotate(p,"x",180) は平面を保つのに 3D。
#                **これは仕様**。ここが 2D になったら、routing と計算本体の判定が割れている。
# ② 閾値の両側 keeps_z_plane は厳密な 0 ではなく **相対 1e-12** で見る。
#              ・sin(pi)=1.2e-16 を含む「x 軸 180 度」の行列 → **正当な平面→平面** ⇒ 2D
#              ・意図した面外回転は **1e-9 度でも** m21≈1.7e-11 ⇒ 3D
#              ★ 片側だけだと「全部 2D にする実装」「全部 3D にする実装」を区別できない。
# ③ 値         変換そのものが効いていること (bbox / vert / centroid の閉形式)。
# ④ union      単純に混ぜる ⇒ **nverts の加算が不変条件** / 2D+3D は 3D へ昇格 /
#              空との union が恒等 / ⚠ **可換ではない** (並び = 格納順 = 入力の順・#3527)。
#              ★ n 項 (union(a,b,c) / union([a,b,c])) は sig の fold 形が二項の木へ分解して通る。
#                ⚠ **畳む順で答えが動かない**ことを 3D の位置を変えて押さえる (昇格は max(dim))。
#                ⚠ 分解しても **並びが崩れない**こと (非可換なので左 fold になる)。
#              ⚠⚠ 可換の印の害は **同じ走の中で両方を評価しないと見えない** (鍵の衝突なので)。
# ⑤ 法線       ★★ ここが「静かに嘘をつかない」ことの本体。法線は **余ベクトル**なので点と
#              同じ行列を当てると傾く。⇒ 3D は逆転置・2D は像の平面の中での直交条件。
#              ⚠ 誤った実装 (点と同じ行列 / det で割らない) と **値が明確に違う**組で見る。
#              ★ 点群の法線を覗く op はまだ無いので、.xyz へ書き出して **ファイルで**見る
#                (srava_points.sh ③ と同じ手)。2D の法線も *面外へ出せば 3D になる* ので覗ける。
# ⑥ 明示エラー 特異行列 / 0 倍 / 退化した軸 / 引数の数。
#
# ---- ⚠⚠ ① だけでは足りない (2026-09-22 に較正で分かったこと) ----------------------
# @type_of@ が答えるのは **routing が申告した出力型** であって、本体が実際に作った点群の
# 次元ではない。⇒ 計算本体だけを「常に 3D」に壊しても ① は **全部緑のまま**通り、
# 最初に落ちるのは ③ の値検定 (vert が 2 成分のはずのところで 3 成分を返す) だった。
# ⇒ **型の検定と値の検定を両方置くこと**。型だけ見ていると、routing と本体の判定が
#   割れたときに *型の印が嘘をついている* ことに気づけない。
LC_ALL=C
export LC_ALL
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
W="$D-work"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

rm -rf "$W"
mkdir -p "$W" || exit 1

run() {   # run <cache-suffix> <source>  → 出力全部
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"points.so\",{});
$2" "$SRAVA" 2>&1
}
v() {     # v <cache-suffix> <source>  → "V " 行の中身だけ (空白は落とす)
	# ⚠⚠ エラーは "V " で始まらないので、素の sed だと **空文字列**になり
	#   「値が一致するか」が *空 = 空* で通る = **壊れているのに緑** (#3572 で 2 件踏んだ形)。
	#   ⇒ 印に **どの検定かと出力そのもの**を書いて、末尾で必ず赤にする。
	_out=$(run "$1" "$2")
	case "$_out" in
	*"*** ERROR"*)
		{ echo "PTX_FAIL: v $1 がエラーになった (検定が空振りするので止める):"
		  echo "$_out" ; } >> "$D-verror"
		echo "PTX_FAIL: v $1 がエラーになった (詳細は $D-verror)" >&2 ;;
	esac
	echo "$_out" | sed -n 's/^V //p' | tr -d ' '
}
# ⚠ 前の走の印が残っていると **理由のない赤**になる (D を使い回したとき)。
rm -f "$D-verror"
n=0
chk() {   # chk <name> <got> <expected>
	[ "$2" = "$3" ] || { echo "PTX_FAIL: $1 が [$2] (期待 [$3])"; exit 1; }
	n=$((n+1))
}
chk_ne() {
	[ "$2" != "$3" ] || { echo "PTX_FAIL: $1 が [$2] と一致してしまった (違うはず)"; exit 1; }
	n=$((n+1))
}
chk_err() {   # chk_err <name> <出力> <期待する部分文字列>
	case "$2" in
	*"$3"*) n=$((n+1)) ;;
	*) echo "PTX_FAIL: $1 がエラーにならない/文言が違う: $2" ; exit 1 ;;
	esac
}
# ★ .xyz の 1 行を **数として**比べる (許容 1e-9)。
#   ⚠ 文字列で比べない — mirror は -0 を書く (値は 0 と等しいが文字は違う) し、
#     %.17g の最後の桁は式の書き方で揺れる。⇒ 比べる対象は *値* であって表記ではない。
chk_xyz() {   # chk_xyz <name> <file> <期待する 6 値>
	_got=$(cat "$2" 2>/dev/null)
	_res=$(echo "$_got" | awk -v e="$3" 'BEGIN{ m=split(e,E," "); bad=0; rows=0 }
		{ rows++; if (NF != m) { bad=1; next }
		  for(i=1;i<=m;i++) { d=$i-E[i]; if (d*d > 1e-18) bad=1 } }
		END{ if (rows != 1) print "ROWS=" rows; else if (bad) print "NG"; else print "OK" }')
	[ "$_res" = "OK" ] || {
		echo "PTX_FAIL: $1 — .xyz が期待と違う ($_res)"
		echo "  得た値 : $_got"
		echo "  期待値 : $3"
		exit 1; }
	n=$((n+1))
}

T2='points2d([[0,0],[1,0],[0,1]])'
T3='points3d([[0,0,0],[1,0,0],[0,1,0]])'

# ---- ① 出力型 — 面外へ出す変換だけが 3D --------------------------------------------
chk "translate 面内 [x,y]"      "$(v a01 "print(\"V\", type_of(translate($T2,[1,2])));")"        "pt-cloud2d"
chk "translate 面内 [x,y,0]"    "$(v a02 "print(\"V\", type_of(translate($T2,[1,2,0])));")"      "pt-cloud2d"
chk "translate 面外"            "$(v a03 "print(\"V\", type_of(translate($T2,[0,0,1])));")"      "pt-cloud3d"
# ★ パーサの糖衣 translate(p,x,y,z) も同じ行へ来る (梱包されて 2 引数になる)。
chk "translate 3 スカラ (糖衣)" "$(v a04 "print(\"V\", type_of(translate($T2,1,2,0)));")"        "pt-cloud2d"
chk "translate 3 スカラ 面外"   "$(v a05 "print(\"V\", type_of(translate($T2,0,0,1)));")"        "pt-cloud3d"
chk "translate 3D"              "$(v a06 "print(\"V\", type_of(translate($T3,[1,2,0])));")"      "pt-cloud3d"
chk "rotate 軸 z"               "$(v a07 "print(\"V\", type_of(rotate($T2,\"z\",30)));")"        "pt-cloud2d"
chk "rotate 軸省略 (糖衣)"      "$(v a08 "print(\"V\", type_of(rotate($T2,30)));")"              "pt-cloud2d"
chk "rotate 軸 x"               "$(v a09 "print(\"V\", type_of(rotate($T2,\"x\",90)));")"        "pt-cloud3d"
# ⚠⚠ **仕様**: 平面を保つ回転だが軸が z でないので救えない (保守的に 3D)。
#   ここが pt-cloud2d になったら、routing の述語と計算本体の判定が割れている。
chk "★ rotate x 180 も 3D"      "$(v a10 "print(\"V\", type_of(rotate($T2,\"x\",180)));")"       "pt-cloud3d"
chk "rotate 任意軸"             "$(v a11 "print(\"V\", type_of(rotate($T2,[1,1,1],10)));")"      "pt-cloud3d"
chk "rotate 3D"                 "$(v a12 "print(\"V\", type_of(rotate($T3,\"z\",30)));")"        "pt-cloud3d"
chk "scale 均等"                "$(v a13 "print(\"V\", type_of(scale($T2,2)));")"                "pt-cloud2d"
chk "scale 軸別"                "$(v a14 "print(\"V\", type_of(scale($T2,[2,3,1])));")"          "pt-cloud2d"
# ★ z 倍率は z=0 に掛かるので 2D では効かない ⇒ **エラーではなく恒等**。
chk "scale z 倍率は 2D で無効"  "$(v a15 "print(\"V\", type_of(scale($T2,[1,1,5])));")"          "pt-cloud2d"
chk "scale 3 スカラ (糖衣)"     "$(v a16 "print(\"V\", type_of(scale($T2,2,3,1)));")"            "pt-cloud2d"
chk "scale 3D"                  "$(v a17 "print(\"V\", type_of(scale($T3,2)));")"                "pt-cloud3d"
chk "mirror x"                  "$(v a18 "print(\"V\", type_of(mirror($T2,\"x\")));")"           "pt-cloud2d"
chk "mirror z (2D では恒等)"    "$(v a19 "print(\"V\", type_of(mirror($T2,\"z\")));")"           "pt-cloud2d"
chk "mirror 任意法線"           "$(v a20 "print(\"V\", type_of(mirror($T2,[1,1,1])));")"         "pt-cloud3d"
chk "mirror 3D"                 "$(v a21 "print(\"V\", type_of(mirror($T3,\"x\")));")"           "pt-cloud3d"
chk "transform 面内 (12)"       "$(v a22 "print(\"V\", type_of(transform($T2,[2,0,0,1, 0,3,0,2, 0,0,1,0])));")" "pt-cloud2d"
chk "transform 面内 (16)"       "$(v a23 "print(\"V\", type_of(transform($T2,[2,0,0,1, 0,3,0,2, 0,0,1,0, 0,0,0,1])));")" "pt-cloud2d"
chk "transform 面外"            "$(v a24 "print(\"V\", type_of(transform($T2,[1,0,0,0, 0,1,0,0, 0,0,1,5])));")" "pt-cloud3d"
chk "transform 3D"              "$(v a25 "print(\"V\", type_of(transform($T3,[2,0,0,0, 0,1,0,0, 0,0,1,0])));")" "pt-cloud3d"

# ---- ② 閾値の両側 (transform) ------------------------------------------------------
# ★ x 軸 180 度の行列。m21 = sin(pi) = 1.2246467991473532e-16 は **丸めの残り**なので
#   平面 → 平面 と読む (affine.h の実測コメント)。
RX180='[1,0,0,0, 0,-1,-1.2246467991473532e-16,0, 0,1.2246467991473532e-16,-1,0]'
# ★ x 軸 1e-9 度の行列。m21 = 1.745329252e-11 は **意図した面外回転** ⇒ 相対 1e-12 を越える。
RXTINY='[1,0,0,0, 0,1,-1.745329252e-11,0, 0,1.745329252e-11,1,0]'
chk "閾値の内側 (sin(pi))"      "$(v b01 "print(\"V\", type_of(transform($T2,$RX180)));")"       "pt-cloud2d"
chk "閾値の外側 (1e-9 度)"      "$(v b02 "print(\"V\", type_of(transform($T2,$RXTINY)));")"      "pt-cloud3d"

# ---- ③ 値 -------------------------------------------------------------------------
P2='points2d([[1,0],[0,1]])'
chk "translate の bbox"  "$(v c01 "print(\"V\", bbox(translate($P2,[10,20])));")"        "[[10,20],[11,21]]"
chk "scale の bbox"      "$(v c02 "print(\"V\", bbox(scale($P2,[2,3,1])));")"            "[[0,0],[2,3]]"
chk "mirror の bbox"     "$(v c03 "print(\"V\", bbox(mirror($P2,\"x\")));")"             "[[-1,0],[0,1]]"
chk "面外 translate の点" "$(v c04 "print(\"V\", vert(translate($P2,[0,0,5]),0));")"      "[1,0,5]"
chk "恒等 transform"     "$(v c05 "print(\"V\", vert(transform($P2,[1,0,0,0, 0,1,0,0, 0,0,1,0]),1));")" "[0,1]"
chk "点数は変わらない"   "$(v c06 "print(\"V\", nverts(rotate($P2,\"x\",90)));")"        "2"
# ★ rotate("z",90) は (1,0) を (0,1) へ。⚠ cos/sin は double 近似なので x は 6.1e-17 になる
#   (厳密な 0 ではない) ⇒ **値そのもの**で比べる (表記の一致を期待しない)。
chk "rotate z 90 の x 成分が 0 近傍" \
	"$(v c07 "var q = rotate($P2,\"z\",90); print(\"V\", abs(vert(q,0)[0]) < 1e-12);")" "1"
chk "rotate z 90 の y 成分"  "$(v c08 "var q = rotate($P2,\"z\",90); print(\"V\", vert(q,0)[1]);")" "1"

# ---- ④ union ----------------------------------------------------------------------
A='points2d([[1,0],[0,1]])'
B='points2d([[5,5]])'
chk "点数の加算 (不変条件)" "$(v d01 "print(\"V\", nverts(union($A,$B)));")"                "3"
chk "2D + 2D は 2D"         "$(v d02 "print(\"V\", type_of(union($A,$B)));")"               "pt-cloud2d"
chk "2D + 3D は 3D"         "$(v d03 "print(\"V\", type_of(union($A,points3d([[0,0,9]]))));")" "pt-cloud3d"
chk "3D + 2D も 3D"         "$(v d04 "print(\"V\", type_of(union(points3d([[0,0,9]]),$A)));")" "pt-cloud3d"
chk "3D + 3D は 3D"         "$(v d05 "print(\"V\", type_of(union($T3,$T3)));")"             "pt-cloud3d"
chk "昇格した点は z=0"      "$(v d06 "print(\"V\", vert(union($A,points3d([[0,0,9]])),0));")" "[1,0,0]"
chk "空との union は恒等 (点数)"   "$(v d07 "print(\"V\", nverts(union($A,points2d([]))));")"   "2"
chk "空との union は恒等 (重心)"   "$(v d08 "print(\"V\", centroid(union($A,points2d([]))));")" "[0.5,0.5]"
chk "空との union は恒等 (型)"     "$(v d09 "print(\"V\", type_of(union($A,points2d([]))));")"  "pt-cloud2d"
chk "空 + 空"                      "$(v d10 "print(\"V\", nverts(union(points2d([]),points2d([]))));")" "0"
# ★★ **可換ではない** — 並びは格納順 = 入力の順 (#3527)。可換の印を立てるとキャッシュキーが
#   正規化されて両者が同じ結果を返し、索引の約束が静かに破れる ⇒ ここがその番。
chk "union(a,b) の先頭"  "$(v d11 "print(\"V\", vert(union($A,$B),0));")"  "[1,0]"
chk "union(b,a) の先頭"  "$(v d12 "print(\"V\", vert(union($B,$A),0));")"  "[5,5]"
chk_ne "a,b と b,a は別の点群" \
	"$(v d13 "print(\"V\", vert(union($A,$B),0));")" "$(v d14 "print(\"V\", vert(union($B,$A),0));")"
# ★★★ 可換の印を立てていないことの **本当の**検定。
# ⚠⚠ **同じ走の中で両方を評価する**こと。別々の走 (別キャッシュ dir) だと、キャッシュキーが
#   衝突していても双方が自分で計算してしまい **正しい答えが返る** — つまり上の d11〜d13 は
#   可換の印を立てても緑のままだった (2026-09-22 に較正で実測)。
#   ★ 「同じ cache dir で 2 回流すのは検定にならない」の **逆向きの罠**: *鍵の衝突*を見るには
#     同じ走でなければならない。
# ★ 可換の印を立てると compute_arg_hash が引数を正規化するので、2 つ目が 1 つ目の結果を
#   HIT で受け取り、union(b,a) の先頭が [1,0] になる (= 索引の約束が静かに破れる)。
_SPLIT_=$(v d20 "var p = union($A,$B); var q = union($B,$A);
print(\"V\", vert(p,0));
print(\"V\", vert(q,0));")
set -f
set -- $_SPLIT_
set +f
chk "同じ走: union(a,b) の先頭" "$1" "[1,0]"
chk "同じ走: union(b,a) の先頭" "$2" "[5,5]"
chk "入れ子で 3 つ"      "$(v d15 "print(\"V\", nverts(union(union($A,$B),$A)));")"  "5"
chk "演算子 ||| も同じ"  "$(v d16 "print(\"V\", nverts($A ||| $B));")"              "3"
chk "変換と混ぜる"       "$(v d17 "print(\"V\", type_of(union($A, translate($A,[0,0,1]))));")" "pt-cloud3d"
# ★ `|||` は左結合の二項なので **3 つ以上も鎖で書ける** (fold ではない)。
chk "||| の鎖 (点数)"    "$(v d18 "print(\"V\", nverts($A ||| $B ||| $A));")"       "5"
chk "||| の鎖 (昇格)"    "$(v d19 "print(\"V\", type_of($A ||| $B ||| points3d([[0,0,9]])));")" "pt-cloud3d"

# ---- ④-2 n 項 (fold 形) — union(a,b,c) と union([a,b,c]) -----------------------------
# ★★ sig を fold 形 "[3d,2d](2)->3d ; [2d](2)->2d" にしたので、3 項以上は
#   pigfModuleAgent::try_decompose が **二項の木へ分解**して通る (#3578 追加・ひさ指示)。
#   ⚠ (2) は「一度に 2 項まで」という capability。n 項ノードのままでは *わざと* 成立しない。
C='points2d([[7,7],[8,8]])'
chk "n 項 (並べ書き)"      "$(v e01 "print(\"V\", nverts(union($A,$B,$C)));")"     "5"
chk "n 項 (配列形)"        "$(v e02 "print(\"V\", nverts(union([$A,$B,$C])));")"   "5"
chk "n 項の型 (全部 2D)"   "$(v e03 "print(\"V\", type_of(union([$A,$B,$C])));")"  "pt-cloud2d"
# ★★ **畳む順で答えが動かない**ことの検定。昇格は max(dim) = 結合的かつ単調なので、
#   3D がどの位置に在っても結果は 3D。⇒ #3575 の「混在は主型が動く」は union には当たらない。
# ⚠⚠ **`D` は使わない** — `D` は SRAVA_CACHE_DIR を持つ変数 (冒頭)。上書きすると
#   `run` の作るキャッシュ dir が **カレント (ビルド dir) に散らばる** (2026-09-22 に実際に
#   踏んだ: `points3d([[0,0,9]])-e04/` が 22 個できた)。⚠ しかも `$D-verror` の印も移るので
#   **v() の番が効かなくなる** = 検定が静かに弱くなる。
PD='points3d([[0,0,9]])'
chk "3D が先頭"            "$(v e04 "print(\"V\", type_of(union([$PD,$A,$B])));")"  "pt-cloud3d"
chk "3D が中"              "$(v e05 "print(\"V\", type_of(union([$A,$PD,$B])));")"  "pt-cloud3d"
chk "3D が末尾"            "$(v e06 "print(\"V\", type_of(union([$A,$B,$PD])));")"  "pt-cloud3d"
chk "n 項の点数 (混在)"    "$(v e07 "print(\"V\", nverts(union([$A,$PD,$B,$C])));")" "6"
# ★★ **分解しても並びが崩れない**こと。非可換なので try_decompose は左 fold を組む
#   (可換の印を立てると均衡木 + キャッシュキー正規化になり、ここが壊れる)。
chk "n 項の並び 0"         "$(v e08 "print(\"V\", vert(union([$A,$B,$C]),0));")"   "[1,0]"
chk "n 項の並び 2 (b の頭)" "$(v e09 "print(\"V\", vert(union([$A,$B,$C]),2));")"  "[5,5]"
chk "n 項の並び 3 (c の頭)" "$(v e10 "print(\"V\", vert(union([$A,$B,$C]),3));")"  "[7,7]"
chk "逆順は別の点群"       "$(v e11 "print(\"V\", vert(union([$C,$B,$A]),0));")"   "[7,7]"
# ★ 1 要素は素通り (union(m)=m)。
chk "1 要素の配列"         "$(v e12 "print(\"V\", nverts(union([$A])));")"         "2"
# ⚠ 空配列は **点群にならない** — pigfArrayFold が fold の単位元 {} を返すため。
#   ★ これは **全カーネル共通の既存の振る舞い** (cgal でも nverts(union([])) は同じエラー)。
chk "空配列は {} (点群ではない)" "$(v e13 "print(\"V\", type_of(union([])));")"    "value"

# ---- ⑤ 法線 — 余ベクトルとして運べているか ------------------------------------------
# ★ 1/sqrt(2)。法線 (1,1)/sqrt(2) は **誤った実装と値が明確に違う**ので選んである。
S=0.7071067811865476
# (a) 3D ・ scale([2,1,1])
#     正 (逆転置 M^-T n) … (0.5,1,0) を正規化 = (0.4472135955, 0.894427191, 0)
#     誤 (点と同じ M n)  … (1.414,0.707,0) を正規化 = (0.894427191, 0.4472135955, 0)  ← 入れ替わる
run n01 "export(\"$W/a.xyz\", scale(points3d([[[1,1,0],[$S,$S,0]]]), [2,1,1]));" > /dev/null
chk_xyz "3D の法線は逆転置" "$W/a.xyz" "2 1 0 0.44721359549995793 0.89442719099991586 0"
# (b) 3D ・ mirror("x")
#     正 (det で割る)   … (-1,0,0)   誤 (余因子だけ) … (1,0,0)  ← 向きが裏返る
run n02 "export(\"$W/b.xyz\", mirror(points3d([[[1,0,0],[1,0,0]]]), \"x\"));" > /dev/null
chk_xyz "反射で法線が裏返らない" "$W/b.xyz" "-1 0 0 -1 0 0"
# (c) 2D の面内変換 → そのあと面外へ出して覗く。
#     scale([2,1,1]) の面内法線は 2x2 の逆転置 ⇒ (0.4472,0.8944)。translate([0,0,1]) は
#     線形部が単位なので法線を変えない。★ 2D の法線が **面内の法線として**運ばれている証拠。
run n03 "export(\"$W/c.xyz\", translate(scale(points2d([[[1,1],[$S,$S]]]), [2,1,1]), [0,0,1]));" > /dev/null
chk_xyz "2D 面内の法線も逆転置" "$W/c.xyz" "2 1 1 0.44721359549995793 0.89442719099991586 0"
# (d) 2D → 面外 (x 軸 90 度)。点 (0,1) → (0,0,1) ・ 面内法線 (0,1) → (0,0,1)。
#     ★ 平面ごと空間へ倒れるので、法線も **像の平面の中で**倒れる。
run n04 "export(\"$W/d.xyz\", transform(points2d([[[0,1],[0,1]]]), [1,0,0,0, 0,0,-1,0, 0,1,0,0]));" > /dev/null
chk_xyz "2D の法線が面外でも面内に留まる" "$W/d.xyz" "0 0 1 0 0 1"
# (e) 法線の印は運ばれる (向き付けの印も) — 書き戻したファイルが 6 列であることがその証拠。
chk "変換後も法線つき (6 列)" "$(awk 'NR==1{print NF}' "$W/a.xyz")" "6"
# (f) union の法線: 片方にしか無ければ **落とす** (3 列になる)。⚠ 既定の法線を作らない。
run n05 "export(\"$W/e.xyz\", union(points3d([[[1,1,0],[$S,$S,0]]]), points3d([[9,9,9]])));" > /dev/null
chk "法線が片方だけなら落ちる" "$(awk 'NR==1{print NF}' "$W/e.xyz")" "3"
# (g) 両方にあれば運ぶ。
run n06 "export(\"$W/f.xyz\", union(points3d([[[1,1,0],[$S,$S,0]]]), points3d([[[9,9,9],[0,0,1]]])));" > /dev/null
chk "両方にあれば運ぶ" "$(awk 'NR==1{print NF}' "$W/f.xyz")" "6"
# (h) ⚠ **空の点群は法線の有無を問わない** — でないと union(p, 空) が恒等でなくなる。
run n07 "export(\"$W/g.xyz\", union(points3d([[[1,1,0],[$S,$S,0]]]), points3d([])));" > /dev/null
chk "空との union は法線を落とさない" "$(awk 'NR==1{print NF}' "$W/g.xyz")" "6"
# (i) 2D の法線が union で昇格したら z=0 になる。
run n08 "export(\"$W/h.xyz\", union(points2d([[[1,1],[$S,$S]]]), points3d([[[9,9,9],[0,0,1]]])));" > /dev/null
_got=$(awk 'NR==1{print $6}' "$W/h.xyz")
chk "2D の法線は z=0 で昇格" "$_got" "0"

# ---- ⑥ 明示エラー -----------------------------------------------------------------
chk_err "特異行列" \
	"$(run f01 "print(\"V\", nverts(transform($T2,[1,0,0,0, 2,0,0,0, 0,0,1,0])));")" \
	"singular matrix"
chk_err "0 倍" "$(run f02 "print(\"V\", nverts(scale($T2,0)));")" "degenerate (zero) scale factor"
chk_err "退化した軸" "$(run f03 "print(\"V\", nverts(rotate($T3,[0,0,0],30)));")" "degenerate axis vector"
chk_err "行列の要素数" \
	"$(run f04 "print(\"V\", nverts(transform($T2,[1,0,0])));")" \
	"matrix must have 12 (3x4) or 16 (4x4) elements"
chk_err "translate にベクトルでない値" \
	"$(run f05 "print(\"V\", nverts(translate($T2,3)));")" \
	"needs a vector"

# ★★ v() がエラーを飲み込んだ検定が 1 つでもあれば赤にする (上の v() の番の受け)。
if [ -f "$D-verror" ]; then
	echo "PTX_FAIL: v がエラーを返した検定がある — 中身:"
	cat "$D-verror"
	rm -f "$D-verror"
	exit 1
fi
echo "PTX_OK ($n checks)"

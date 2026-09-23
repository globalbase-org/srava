#!/bin/sh
# #3533 — **平面 → 枠 (O,U,V) の表を 1 箇所に保つ**ための、ソースを数える検査。
#
# $1 = ソースツリーの根 (CMAKE_CURRENT_SOURCE_DIR)
#
# ---- なぜ値の検査ではなくソースの検査なのか ----------------------------------------
# ★★ #3533 で bbox / centroid を world にしたので、**局所座標は言語からまったく観測できない**
#   (面積も周長も不変・bbox も centroid も world・DXF は法線から OCS を作り直す)。
#   ⇒ 表が食い違っても *値の検査では赤くならない*。実測で確かめてある。
#   ⇒ 守りたい不変量「**枠を作ってよいのは 1 箇所。ほかは運ぶだけ**」は、
#     ソースの形でしか言えない。⚠ 申し送り (コメント) は合流を越えないので検査にする。
#
# ---- 何を数えるか --------------------------------------------------------------------
#   ① 表の本体 (plane_frame の定義) は **src/h/common/affine.h に 1 つだけ**
#   ② 2D が **生まれる** 3 経路が全部その表を呼んでいる
#        cgal   の section      modules/cgal/c++/cgMesh3D.cpp
#        mf     の section      modules/manifold/c++/mfaSection.cpp
#        occt_mf の polygonize  modules/occt_mf/c++/ocmPolygonize.cpp
#   ③ @set_frame(@ を呼ぶファイルは、**枠を運んでいる** (@frame_o()@ が在る) か
#      **共有の表を借りている** (@common/affine.h@ を include している) かのどちらか。
#      ⚠ ③ はファイル単位の粗い判定。「新しいファイルで表を手書きし始めた」を捕まえるための柵で、
#        同じファイルの中で運搬と手書きが混ざる場合は捕まえられない。**粗いと承知で置いている**。
#      ⚠⚠ 2026-09-17 まで ③ は **ヘッダを対象外**にしていた ("宣言だけの header は対象外")。
#        ⇒ ヘッダで枠を手書きしても黙って通る盲点があった。実際 #3527 で geomutils を足したとき、
#          .cpp で捕まった実装を **ヘッダへ inline で移すと通ってしまう**ことが分かった
#          (manifold の mfMesh.h がまさにその形で identity をベタ書きしている)。
#        ⇒ 除外を撤去した。撤去しても既存 3 本 (cgMesh.h / mfMesh.h / guGeom.h) は
#          frame_o() を持つので **そのまま通る** = 除外リストは要らなかった。
#        ★ 「既知だから除外」は「いつまでも除外」になる。除外を作らずに済むなら作らない。
#      ⚠⚠ さらに深い盲点が同時に出た (2026-09-17): 借用の判定が **素の grep** だったので、
#        *コメントに "common/affine.h" と書いてあるだけ*で通っていた。⇒ **#include 行**で見る。
#        ★ この 2 つ目は、除外撤去の**陽性対照が発火しなかった**ことで見つかった。
#          「通った」理由を確かめずに緑を受け取っていたら、両方とも残っていた。
#      ★ 「@plane_frame@ を呼ぶこと」ではなく「@affine.h@ を借りること」で見る理由: 平面 → 軸の
#        規約は 1 つではない。DXF の OCS は **規約が外部 (DXF 仕様) で決まっている**ので
#        @plane_frame@ には寄せられず、@dxf_ocs_axes@ という別の関数になる。⇒ 守りたいのは
#        「*その規約が 1 箇所に在って、書き手と読み手が同じものを使う*」で、関数名ではない。
#        ⚠ 2026-09-15 に **この検査が実際に捕まえた** — DXF の読み手に枠を持たせたとき。
#
# ⚠ 較正済み (2026-09-15): ② を壊す (mfaSection の呼び出しを手書きの表に戻す) と赤くなることを
#   実地で確認した。検査自身が赤くなる条件を持たないと、緑は「壊れていない」を意味しない。
# ⚠ 較正済み (2026-09-17): ③ の **ヘッダも見る**ようにした後、guGeom.h の frame_o() を潰すと
#   赤くなることを実地で確認した (撤去が実際に効いていることの陽性対照)。
ROOT="${1:?source root not given}"

# ★★ #3522: ハングの番犬 (共通)。⚠ この検査は srava を **起動しない**ので番犬は実質 no-op だが、
#   srava_hangwatch_coverage は「test/srava_*.sh は全部 source する」を **例外なし**で数える。
#   ⇒ 例外表を作るとその表が新しい歯抜けの置き場所になるので、規則の方に合わせる。
. "$(dirname "$0")/srava_hangwatch.sh"

NG=0
n=0

# ---- ① 表の定義は 1 つだけ ------------------------------------------------------------
DEFS=$(grep -rln "^inline int plane_frame(" "$ROOT/src" "$ROOT/modules" 2>/dev/null | sort)
CNT=$(echo "$DEFS" | grep -c .)
if [ "$CNT" = "1" ] && [ "$DEFS" = "$ROOT/src/h/common/affine.h" ]; then
	echo "  ok  ① 表の定義は src/h/common/affine.h に 1 つだけ"; n=$((n+1))
else
	echo "FRAME_FAIL: ① plane_frame の定義が $CNT 箇所: $DEFS"; NG=1
fi

# ---- ② 2D が生まれる 3 経路が表を呼んでいる --------------------------------------------
for f in modules/cgal/c++/cgMesh3D.cpp modules/manifold/c++/mfaSection.cpp \
         modules/occt_mf/c++/ocmPolygonize.cpp ; do
	if grep -q "plane_frame" "$ROOT/$f" 2>/dev/null; then
		echo "  ok  ② $f は表を呼んでいる"; n=$((n+1))
	else
		echo "FRAME_FAIL: ② $f が平面から枠を作っているのに plane_frame を呼んでいない"
		echo "            (軸の表を手書きしていないか確認する — 表は 1 箇所の約束)"
		NG=1
	fi
done

# ---- ③ set_frame を呼ぶファイルは「運ぶ」か「表を呼ぶ」か ------------------------------
for f in $(grep -rl "set_frame(" "$ROOT/modules" "$ROOT/src" 2>/dev/null | sort); do
	# ⚠ ヘッダも見る (2026-09-17)。除外していた間は「.cpp から .h へ移すと通る」盲点だった。
	# ⚠⚠ 2026-09-17: 借用の判定は **#include 行**で見る。以前は素の grep だったので、
	#   *コメントに "common/affine.h" と書いてあるだけ*のファイルが通っていた
	#   (guGeom.h がまさにそれで、通っていた理由が運搬ではなくコメントだった)。
	if grep -q "frame_o()" "$f" || grep -qE '^[[:space:]]*#include[[:space:]].*common/affine\.h' "$f"; then
		n=$((n+1))
	else
		echo "FRAME_FAIL: ③ $f が枠を **手で組んで** いる (運搬でも共有の表でもない)"
		echo "            ⇒ src/h/common/affine.h の表を借りること (平面 → 軸の規約は 1 箇所)"
		NG=1
	fi
done
[ "$NG" = "0" ] && echo "  ok  ③ set_frame を呼ぶファイルはすべて運搬か表の呼び出し"

if [ "$NG" = "0" ]; then echo "FRAMETABLE-OK ($n checks)"; else echo "FRAMETABLE-FAIL"; fi
exit "$NG"

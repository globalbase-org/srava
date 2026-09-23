/*
 * cgaCast — cast("exact", mesh) の計算本体(ptsCalcBody 派生・#3404)。
 *   カーネル間の **明示的な表現変換**。cg agent(CGAL カーネル)側の cast は、入力メッシュをそのまま
 *   出力する identity。核心は **リーダ側**にある: cg のメッシュリーダ(ptscgWireCacheStreamReaderMesh)は
 *   D_META タグが "MESH"(exact)でも "MFM3"(Manifold)でも読め、MFM3 の場合は double→EPECK 有理数へ
 *   **無損失昇格** して cgMesh3D を作る(cgMesh3D::decode_mfm3)。従って cast は「読んで(=昇格が起きる)
 *   そのまま MESH で書き出す」だけで float→exact 変換が完成する。
 *
 *   引数: args=[type_string(inline・"exact"), mesh(cache)]。type はプランナ(pigfModuleAgent::
 *   decide_out_module)が既にカーネル選択に使っており、ここ(agent)では無視してよい。
 *   既に exact なメッシュを cast("exact", ...) した場合も単なる再エンコード(no-op 相当)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<cstring>
#include	"_ts2/c++/cgaCast_.h"

CLASS_TINYSTATE(cg/c++/cgaCast,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaCast_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class ptsObject;
class pigData;
class stdString;
class cgMesh;
TS_END_INTERFACE

#endif


cgaCast_::cgaCast_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaCast_::compute()
{
	/* args=[type(inline), mesh(cache)]。mesh は args[1](リーダが MESH/MFM3 どちらも decode 済み・
	 * MFM3 なら EPECK へ無損失昇格済み)。cast はそれをそのまま出力する identity。 */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 1 ) ? sPtr<cgMesh>::d_cast((*args)[1]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("cast: needs a mesh (2nd arg)")));
		return;
	}
	mesh = in;   /* identity。保存時の WriterMesh が MESH(exact)で再エンコード = float→exact 変換の実体 */

	/* ★★ #3533 規約②: **降格 (cg-face3d → cg-cross2d) だけは identity ではない**。
	 *   「空間に置かれた 2D」を「z=0 の簡易表現」と名乗り直す操作なので、*本当に z=0 に
	 *   居るとき* しか許さない。⇒ @frame_is_default()@ が偽なら明示エラー。幾何は 1 ミリも
	 *   動かさない (傾いたものを落としたいなら #3534 の project_flatten を使う)。
	 *   ★ 逆に「枠は既定だが型は face3d」= @rotate(rect,"z",90)@ の結果は *通る*。
	 *     型 (規約①) と幾何 (枠) を別のビットで持っているのはこのため。
	 *   ⚠ 目標型は args[0] (インラインの文字列)。プランナは既に routing に使っているが、
	 *     ここは **値を作る側**なので自分で読む。 */
	sPtr<cgMesh2D> c2 = sPtr<cgMesh2D>::d_cast(in);
	if ( c2.is_notNull() && na > 0 ) {
		sPtr<pigData> tv = (*args)[0];
		const char *tname = ( tv.is_notNull() && tv->get_str() != thNULL )
		                  ? tv->get_str()->get_str() : "";
		if ( ::strcmp(tname, "cg-cross2d") == 0 && c2->is_placed() ) {
			if ( ! c2->frame_is_default() ) {
				result = cga_err(thNEW(stdString,(
				    "cast: this 2D region is placed on another plane, so it cannot be named "
				    "\"cg-cross2d\" (the z=0 representation); cast never moves geometry — "
				    "use project_flatten(...) to drop it onto z=0, or transform it back first")));
				mesh = thNULL;
				return;
			}
			/* 幾何はそのまま・**名乗りだけ**下げる (共有されうる値なので複製する)。 */
			sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
			out->copy_contents_from(c2);   /* ★ #3545: 複製は幾何 lib 側 */
			out->set_placed(0);
			mesh = out;
		}
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaCast_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}

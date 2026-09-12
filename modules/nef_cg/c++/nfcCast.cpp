/*
 * nfcCast — cast("cg-mesh3d", <nef の値>) の計算本体 (akira-project #3499)。
 *   ★ **nef_snc ⇄ cgal の境界モジュール** nef_cg.so が持つ唯一の op。
 *
 * ★★ このモジュールの存在理由: nef_snc は **常に SNC (Nef 本来の表現) だけ**を書く。
 *   SNC のパースには CGAL Nef が要り、cgal.so は **CGAL Nef 非依存** (#3440) を設計として
 *   守っているので、cgal.so 側に reader を足すことはできない。
 *   #3478 は逆向きに解いた — nef_snc が SNC の後ろに厳密境界を **付録**として併記し、
 *   cgal / manifold にはそれを読ませた。だが付録のために **encode ごとに to_mesh() が走り**、
 *   実時間の代償が大きかった (#3499)。
 *   ⇒ 変換だけを取り出し、**両側の本物のクラス** (nfMesh / cgMesh3D) を使う別 .so に置く。
 *     to_mesh() を払うのは **cast が呼ばれたときだけ**になる。occt_mf (#3452) と同じ構図。
 *
 * ★ 幾何クラス (nfMesh) は **libsrava_nf_snc.so** に置いてある。nef_snc.so / nef_cg.so が
 *   **同じ実体**を共有するため (各自がコピーを持つと in-proc で d_cast が失敗する)。
 *
 * ★ 変換そのものは **無損失**。nfMesh::Mesh も cgMesh::Mesh も
 *   CGAL::Surface_mesh<EPECK::Point_3> = **同じ型**なので、境界を取り出してそのまま渡せる。
 *   落ちるのは「境界表現を持てない値」(非有界 = complement の結果など) だけで、それは
 *   cg の表現力に無いものなので明示エラーが正しい。
 *
 * ライセンス: どちらも CGAL (GPL)。この橋で新たに混ざるものは無い。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfcCast_.h"
#include	"pig/c++/pigModuleError.h"
/* ★ #3475: このモジュール専用のエラー生成子。文言は "[TAG] nef_cg/op: message" になる。 */
PIG_DEFINE_MODULE_ERR(nfc_err, "nef_cg")


CLASS_TINYSTATE(nfc/c++/nfcCast,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfcCast_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh3D>	out;
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
class cgMesh3D;
TS_END_INTERFACE

#endif


nfcCast_::nfcCast_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
nfcCast_::compute()
{
	/* ★ cast の引数は **cast(型名, 幾何)** の 2 つ (CAST_IN = { AK_INLINE, AK_CACHE })。
	 *   幾何は args[1]。args[0] は目標の型名で、ここに来ている時点で既に解決済み。 */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 1 ) ? sPtr<nfMesh>::d_cast((*args)[1]) : sPtr<nfMesh>();
	if ( ! in.is_notNull() ) {
		result = nfc_err(thNEW(stdString,("cast: needs a Nef (SNC) value")));
		return;
	}
	nfMesh::Mesh bnd;
	if ( ! in->to_mesh(bnd) ) {
		/* ★ 理由を書く (#3479 の作法)。ここに来るのは cg に表現が無い値だけ。 */
		result = nfc_err(thNEW(stdString,
		    /* ★ **入力の形式 (NEF3) を文面に出す** — 利用者が受け取るのは「どの値が
		     *   どの形式で書かれていて、なぜ渡せないのか」で、型名だけでは
		     *   キャッシュを見に行く手がかりにならない (#3479 の作法)。 */
		    ("cast: the Nef value (nf-mesh3d, cache format NEF3) is a bare SNC with no "
		     "boundary representation (it is unbounded, e.g. the result of complement), "
		     "which cg-mesh3d cannot represent")));
		return;
	}
	out = thNEW(cgMesh3D,());
	out->mesh() = bnd;   /* ★ 同じ EPECK Surface_mesh。厳密なまま素通し */
}

sPtr<pigData>
nfcCast_::get_result()
{
	return ( result != thNULL ) ? result : out;
}

/*
 * cgaSphere — 球(測地球)生成の計算本体(ptsCalcBody 派生)。
 * args=[radius, seg](INLINE)。seg=円周分割数(連続)。種=正八面体を seg→n 分割して球面投影。
 * ★manifold の sphere と **同一アルゴリズム**(src/h/common/geodesic.h)で頂点・面が一致し
 *   体積が数値誤差レベルで揃う(2026-08-11 ひさ設計)。細分回数で指定したいときは icosphere(r,subdiv)。
 * seg 省略時は既定 seg=32 相当(n=8・8·64=512 面)。実体は cgMesh3D.cpp の cga_make_geodesic。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/geodesic.h"   /* seg_to_n / SEED_OCTAHEDRON */
#include	"_ts2/c++/cgaSphere_.h"

/* cgMesh3D.cpp 定義の共有ヘルパ(種 seed を n 分割した半径 r の測地球を ball に)。 */
void cga_make_geodesic(cgMesh::Mesh& ball, int seed, int n, double r);

CLASS_TINYSTATE(cg/c++/cgaSphere,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaSphere_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh3D>	mesh;
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


cgaSphere_::cgaSphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaSphere_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double r   = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    seg = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 円周分割数。0=既定(seg=32 相当) */
	int    n   = srava_geo::seg_to_n(seg);

	mesh = thNEW(cgMesh3D,());
	cga_make_geodesic(mesh->mesh(), srava_geo::SEED_OCTAHEDRON, n, r);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaSphere_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}

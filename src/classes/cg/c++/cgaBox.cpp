/*
 * cgaBox — 直方体生成の計算本体(ptsCalcBody 派生)。入力 args=[w,h,d](INLINE スカラ)。
 * CGAL の Surface_mesh(EPECK) で w×h×d の箱(8 頂点 → 三角形化 12 面)を作り、cgMesh(ラッパ)に
 * 保持する。中間 blob は作らない。cgatsAgent は get_writer() で受けた ...WriterMesh に cgMesh を
 * 渡し、writer 側で cgaMeshCodec::encode が D_CHUNK へ直接ストリーム書き込みする(確認①)。
 * 下流 union は ...ReaderMesh で D_CHUNK を読み、cgaMeshCodec::decode で厳密に mesh 復元して corefine。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"   /* 基底 ptsObject の sPtr<ptsApplication> メンバ用 */
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaBox_.h"

#include	<CGAL/boost/graph/generators.h>
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>

CLASS_TINYSTATE(cg/c++/cgaBox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaBox_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
protected:
	virtual void	compute();
	sPtr<cgMesh3D>	mesh;     /* compute() が作った箱(get_writer が writer へ渡す) */
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
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


cgaBox_::cgaBox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* w,h,d の箱(原点隅)を CGAL で cgMesh に作る。シリアライズは writer 側(get_writer)。
 * 重い計算は ACT_START(スレッド)。cache 出力なので result(テキスト)は持たない。 */
void
cgaBox_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double w = 1.0, h = 1.0, d = 1.0;
	/* boxa([w,h,d]): 寸法を 1 つの array(構造 inline 引数)で受け取る形。value-parse 済みの
	 * pigDataArray が来るので要素を get_ix で取り出す。従来の box(w,h,d) は 3 スカラ。 */
	sPtr<pigDataArray> dims = ( na == 1 ) ? sPtr<pigDataArray>::d_cast((*args)[0])
	                                      : sPtr<pigDataArray>();
	if ( dims.is_notNull() ) {
		int nd = dims->length();
		if ( nd > 0 ) w = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		if ( nd > 1 ) h = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( nd > 2 ) d = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt();
	} else {
		if ( na > 0 ) w = (*args)[0]->get_flt();
		if ( na > 1 ) h = (*args)[1]->get_flt();
		if ( na > 2 ) d = (*args)[2]->get_flt();
	}

	mesh = thNEW(cgMesh3D,());
	cgMesh::Mesh& m = mesh->mesh();
	typedef cgMesh::Point_3 cgP;
	CGAL::make_hexahedron(
		cgP(0,0,0), cgP(w,0,0), cgP(w,h,0), cgP(0,h,0),
		cgP(0,0,d), cgP(w,0,d), cgP(w,h,d), cgP(0,h,d), m);
	CGAL::Polygon_mesh_processing::triangulate_faces(m);
}

/* mesh 出力: parent=cgatsAgent で WriterMesh を生成し cgMesh を渡す(writer が encode で D_CHUNK
 * へ直接ストリーム。cgMesh は本オブジェクト所有・writer の寿命中ずっと生存)。 */
sPtr<ptsWireCacheStreamWriter>
cgaBox_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}

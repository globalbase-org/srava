/*
 * ocmTriangulate — triangulate(s, deflection) の計算本体。
 *   ★**表現クラスをまたぐ唯一の出口** (#3437 P5) で、occt_mf.so (境界モジュール) が持つ。
 *
 * ★★ 旧 modules/occt/c++/ocaTriangulate.cpp との違いは **出力の実体** だけ:
 *   旧: occt 内部クラス ocMesh を作り、型名だけ "mf-mesh3d" を名乗っていた
 *       → in-proc で d_cast<mfMesh> が失敗する / codec の読み手が本家と競合する
 *   新: **本物の mfMesh** (manifold::Manifold) を作る。openvdb_mf の isosurface と同じ作法。
 *   B-rep 側の三角形化ロジック (頂点の溶接・向きの補正) はそのまま移設した。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocmTriangulate_.h"
#include	<BRepMesh_IncrementalMesh.hxx>
#include	<BRep_Tool.hxx>
#include	<Poly_Triangulation.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Face.hxx>
#include	<TopLoc_Location.hxx>
#include	<gp_Pnt.hxx>
#include	<vector>
#include	<map>
#include	<tuple>

CLASS_TINYSTATE(ocm/c++/ocmTriangulate,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocmTriangulate_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfMesh>	out;
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
class mfMesh;
TS_END_INTERFACE

#endif


ocmTriangulate_::ocmTriangulate_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocmTriangulate_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() || in->shape().IsNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("triangulate: needs an OCCT shape"))));
		return;
	}
	/* deflection = **曲面と弦の最大距離**。小さいほど三角形が増える。★ここが情報損失を伴う
	 *   パラメータなので、暗黙 cast にせず明示 op で必ず書かせる (voxelize の dx と同じ思想)。 */
	double defl = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	if ( !(defl > 0) ) {
		result = thNEW(pigDataError,(thNEW(stdString,
		    ("triangulate: deflection (max chord distance) must be > 0"))));
		return;
	}

	TopoDS_Shape s = in->shape();
	BRepMesh_IncrementalMesh mesher(s, defl);
	if ( ! mesher.IsDone() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("triangulate: BRepMesh failed"))));
		return;
	}

	std::vector<double>   vv;
	std::vector<uint32_t> tt;
	/* ★★ **頂点の溶接が必須**。OCCT の三角形分割は **Face ごとに独立した頂点配列**を持つので、
	 *   そのまま並べると隣接面が頂点を共有しない「三角形スープ」になり、**閉じていない**
	 *   メッシュとして下流に渡る (Manifold は無効と見なして volume 0 を返した — 実際に踏んだ)。
	 *   共有稜の上の点は隣接 Face が同じ稜ポリゴンから取るので**座標は一致する**。よって
	 *   座標をキーにした重複排除で溶接できる。
	 *   ★ B-rep → mesh の変換で必ず要る処理で、「面が第一級」という表現の裏返し。 */
	std::map<std::tuple<double,double,double>, uint32_t> weld;
	for ( TopExp_Explorer ex(s, TopAbs_FACE) ; ex.More() ; ex.Next() ) {
		TopoDS_Face f = TopoDS::Face(ex.Current());
		TopLoc_Location loc;
		Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(f, loc);
		if ( tri.IsNull() ) continue;
		const gp_Trsf &tr = loc.Transformation();
		std::vector<uint32_t> map1((size_t)tri->NbNodes() + 1, 0);
		for ( Standard_Integer i = 1 ; i <= tri->NbNodes() ; ++i ) {
			gp_Pnt p = tri->Node(i).Transformed(tr);
			std::tuple<double,double,double> key(p.X(), p.Y(), p.Z());
			auto it = weld.find(key);
			if ( it != weld.end() ) {
				map1[(size_t)i] = it->second;
			} else {
				uint32_t idx = (uint32_t)(vv.size() / 3);
				vv.push_back(p.X()); vv.push_back(p.Y()); vv.push_back(p.Z());
				weld.emplace(key, idx);
				map1[(size_t)i] = idx;
			}
		}
		/* ★ 面の向き: OCCT は Face ごとに Orientation を持ち、REVERSED なら三角形の
		 *   巻き順を反転しないと法線が内向きになる (体積が負になる)。 */
		Standard_Boolean rev = ( f.Orientation() == TopAbs_REVERSED );
		for ( Standard_Integer i = 1 ; i <= tri->NbTriangles() ; ++i ) {
			Standard_Integer a, b, c;
			tri->Triangle(i).Get(a, b, c);
			uint32_t ia = map1[(size_t)a], ib = map1[(size_t)b], ic = map1[(size_t)c];
			if ( ia == ib || ib == ic || ia == ic ) continue;   /* 溶接で潰れた三角形は捨てる */
			tt.push_back(ia);
			if ( rev ) { tt.push_back(ic); tt.push_back(ib); }
			else       { tt.push_back(ib); tt.push_back(ic); }
		}
	}
	if ( tt.empty() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("triangulate: produced no triangles"))));
		return;
	}

	/* ★ MeshGL64 → Manifold。**本物の mfMesh を返す** (mfMesh::decode と同じ組み立て方)。
	 *   openvdb_mf の isosurface と同じで、これが境界モジュールの存在理由そのもの。 */
	manifold::MeshGL64 g;
	g.numProp = 3;
	g.vertProperties = vv;
	g.triVerts.reserve(tt.size());
	for ( size_t i = 0 ; i < tt.size() ; ++i ) g.triVerts.push_back((uint64_t)tt[i]);
	out = thNEW(mfMesh,(manifold::Manifold(g)));
}

sPtr<pigData>
ocmTriangulate_::get_result()
{
	return ( result != thNULL ) ? result : out;
}

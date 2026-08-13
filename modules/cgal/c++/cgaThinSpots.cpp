/*
 * cgaThinSpots — thin_spots(mesh, t_min) の計算本体(ptsCalcBody 派生)= 肉厚 SDF 解析で
 *   厚みが t_min 未満の面を拾い、**[[x,y,z,thk], ...] の入れ子配列で返す値返し op**。
 *   3Dプリント時に「薄すぎて割れる」箇所を位置つきで検出する用途。
 *   多態 cgMesh3D::op_thin_spots が (重心x,y,z, 厚み) を 4 個ずつ flat に返す。3D 専用。
 * 配列返し: result=pigDataArray(各危険点が pigDataArray[x,y,z,thk])。cgatsAgent が serialize→
 *   プランナが VALUE パースで入れ子 pigDataArray に復元 → 添字可(closest/bbox と同じ仕掛け)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaThinSpots_.h"
#include	<vector>

CLASS_TINYSTATE(cg/c++/cgaThinSpots,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaThinSpots_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

protected:
	virtual void	compute();
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
TS_END_INTERFACE

#endif


cgaThinSpots_::cgaThinSpots_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaThinSpots_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh3D> in = ( na > 0 ) ? sPtr<cgMesh3D>::d_cast((*args)[0]) : sPtr<cgMesh3D>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("thin_spots: needs a 3D mesh"))));
		return;
	}
	double t_min = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	int    rays  = ( na > 2 ) ? (int)(*args)[2]->get_flt() : 25;   /* 既定 25。速度優先なら小さく */
	double cone  = ( na > 3 ) ? (*args)[3]->get_flt() : 45.0;      /* コーン全角(度)。既定 45(肉厚向き)。広角=120 で SDF 風 */

	std::vector<double> spots;   /* x,y,z,thk を 4 個ずつ flat */
	in->op_thin_spots(t_min, rays, cone, spots);

	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( size_t i = 0 ; i + 3 < spots.size() ; i += 4 ) {
		sPtr<pigDataArray> e = thNEW(pigDataArray,());   /* [x,y,z,thk] */
		for ( int k = 0 ; k < 4 ; ++k )
			e->push(thNEW(pigDataFloat,(spots[i + k])));
		arr->push(e);
	}
	result = arr;
}

/*
 * cgaFaceVerts — face_verts(m, i) — 面 i の **頂点番号** [i0,i1,i2] (#3527)。
 *
 * ⚠ **なぜ後から足したか**: #3510 の表の「頂点を読む」の行で cgal だけ 2/3 と出た (2026-09-18)。
 * ★★ @vert@ (座標) と **わざと分けてある** — 連結関係が要る場面では番号が要り、座標から番号へ
 *   引き戻すのは丸めが絡んで危うい (#3527 本文の⚠)。番号は @vert@ / @verts@ と同じ列を指す。
 * ⚠ 三角形以外の面は 3 頂点で名乗れないので断る。2D は面を持たないので sig に行を置かない。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"

#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaFaceVerts_.h"
#include	<stdio.h>

CLASS_TINYSTATE(cg/c++/cgaFaceVerts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaFaceVerts_(
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
class cgMesh;
TS_END_INTERFACE

#endif

cgaFaceVerts_::cgaFaceVerts_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaFaceVerts_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh3D> in = ( na > 0 ) ? sPtr<cgMesh3D>::d_cast((*args)[0]) : sPtr<cgMesh3D>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("face_verts: needs a 3D mesh (a 2D region has no faces)")));
		return;
	}
	const int idx = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;
	const int n   = in->op_nfaces();
	if ( idx < 0 || idx >= n ) {
		char b[176];
		::snprintf(b, sizeof b, "face_verts: index %d is out of range (the value has %d face(s))", idx, n);
		result = cga_err(thNEW(stdString,(b)));
		return;
	}
	int f[3] = {0,0,0};
	if ( in->op_face_verts(idx, f) <= 0 ) {
		result = cga_err(thNEW(stdString,(
		    "face_verts: this face is not a triangle, so it cannot be named by three vertices; "
		    "triangulate the mesh first")));
		return;
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < 3 ; ++i ) arr->push(thNEW(pigDataInteger,((INTEGER64)f[i])));
	result = arr;
}


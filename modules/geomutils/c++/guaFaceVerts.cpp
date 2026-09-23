/*
 * guaFaceVerts — face_verts(m, i) — 面 i の **頂点番号** [i0,i1,i2] (#3527 段 5)。
 *
 * ★★ @vert@ と **わざと分けてある**。#3527 の本文の⚠がその理由:
 *     「*座標*で返す」のと「*構成要素の番号*で返す」は **別の約束**。
 *   連結関係が欲しい場面 (どの面がどの頂点を共有するか・Delaunay の隣接) では番号が要り、
 *   座標から引き戻すのは double のカーネルでは丸めが入って危うい。
 *   ⇒ 番号で返し、座標が要るなら @vert(m, face_verts(m,i)[k])@ と繋ぐ。番号は同じ列を指す。
 *
 * ⚠ mesh 系の「面」は **三角形 1 枚**。occt の @face@ (トリム面) とは桁が違う値になるが、
 *   *その違いこそ表現の要点* なのであえて同じ語を使う (#3527 の規約⑤ ・ ocaNfaces と同じ判断)。
 *   ★ だから @face(m,i)@ (三角形そのものを mesh で返す) は作らない — 百万個の実装依存の索引を
 *     キャッシュに焼き付けることになり、欲しいのは三角形ではなく **座標か番号**だから。
 *
 * ⚠ 2D は面を持たない ⇒ sig に 2D の行を置かない (ルータが先に弾く)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"

#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaFaceVerts_.h"
#include	<stdio.h>

CLASS_TINYSTATE(gu/c++/guaFaceVerts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaFaceVerts_(
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
class guGeom;
class ptCloud;
TS_END_INTERFACE

#endif

guaFaceVerts_::guaFaceVerts_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaFaceVerts_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("face_verts: needs a mesh")));
		return;
	}
	const int idx = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;
	const int n   = in->op_nfaces();
	if ( idx < 0 || idx >= n ) {
		char b[176];
		::snprintf(b, sizeof b, "face_verts: index %d is out of range (the value has %d face(s))", idx, n);
		result = gua_err(thNEW(stdString,(b)));
		return;
	}
	int f[3] = {0,0,0};
	if ( in->op_face_verts(idx, f) <= 0 ) {
		/* ★ 通常経路ではここへ来ない (sig が 3D しか受けない)。sig を書き換えた人への保険。 */
		result = gua_err(thNEW(stdString,(
		    "face_verts: this value has no faces (a 2D region is bounded by rings, not faces)")));
		return;
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < 3 ; ++i ) arr->push(thNEW(pigDataInteger,((INTEGER64)f[i])));
	result = arr;
}

/*
 * cgaUnion — 2 メッシュの和(corefinement union)の計算本体(ptsCalcBody 派生)。
 * 入力 args=[meshA, meshB] は **cgMesh**(...ReaderMesh が別プロセスのキャッシュ(D_CHUNK)から
 * 厳密 decode した Surface_mesh ラッパ)。corefine_and_compute_union(EPECK)で和を取り、結果を
 * cgMesh に保持して get_writer() で返す(シリアライズは writer 側)。中間 blob は持たない。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaBoolError.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaUnion_.h"

CLASS_TINYSTATE(cg/c++/cgaUnion,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaUnion_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;     /* compute() の union 結果(get_writer が writer へ渡す) */
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


cgaUnion_::cgaUnion_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* args[0], args[1] の cgMesh(reader が decode 済)を corefinement union → 結果を cgMesh に。
 * シリアライズは writer 側。重い計算は ACT_START(スレッド)。引き渡しは get_writer()。 */
void
cgaUnion_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> ma = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	sPtr<cgMesh> mb = ( na > 1 ) ? sPtr<cgMesh>::d_cast((*args)[1]) : sPtr<cgMesh>();
	if ( ! ma.is_notNull() || ! mb.is_notNull() ) {
		result = thNEW(pigDataError,(cga_missing_operand_msg("union",
		             (na>0)?(*args)[0]:sPtr<pigData>(), (na>1)?(*args)[1]:sPtr<pigData>(), na)));
		return;
	}
	mesh = ma->op_union(mb);   /* 多態: 3D=corefinement / 2D=bso_2(将来)。次元を知らない */
	if ( ! mesh.is_notNull() ) {
		const char *m = ( ma->dim() != mb->dim() )
		    ? "union: cannot mix 2D and 3D operands"
		    : "union: boolean failed. Operands must be closed solids that do not touch tangentially or "
		      "share coplanar faces, and must not self-intersect (an earlier boolean may have made "
		      "invalid geometry — check valid()). Overlap operands slightly (e.g. by 0.01) instead of "
		      "making them exactly touch.";
		result = thNEW(pigDataError,(thNEW(stdString,(m))));
	}
}

sPtr<ptsWireCacheStreamWriter>
cgaUnion_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}

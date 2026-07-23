/*
 * cgaRepair — repair(mesh) の計算本体(ptsCalcBody 派生)= 修復した新 mesh を返す(多態 op_repair に委譲)。
 *   3D=autorefine(自己交差を幾何的に解消)、2D=Polygon_repair::repair(even-odd で自己交差/重なり正規化)。
 * mesh 返し op の作法は cgaOffset と同じ: compute() で mesh を立て、get_writer で WriterMesh を返す。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaRepair_.h"

CLASS_TINYSTATE(cg/c++/cgaRepair,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaRepair_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
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
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


cgaRepair_::cgaRepair_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaRepair_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("repair: needs a mesh"))));
		return;
	}
	mesh = in->op_repair();   /* 多態: 3D=autorefine / 2D=even-odd repair */
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("repair: operation failed"))));
}

sPtr<ptsWireCacheStreamWriter>
cgaRepair_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}

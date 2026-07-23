/*
 * cgaSphere — 球(の多面体近似 = icosphere)生成の計算本体(ptsCalcBody 派生)。
 * args=[radius, subdiv](INLINE)。subdiv=0=正二十面体(12 頂点 20 面・粗い)、
 * 1=80 面、2=320 面…(大=滑らか・重い)。実体は cgMesh3D.cpp の cga_make_icosphere を共有
 * (offset 3D Minkowski の球と同じ近似)。subdiv 既定 0(= 従来の sphere(r) と同一形状)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaSphere_.h"

/* cgMesh3D.cpp 定義の共有ヘルパ(半径 r・細分化 subdiv の icosphere を ball に)。 */
void cga_make_icosphere(cgMesh::Mesh& ball, double r, int subdiv);

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

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
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
class ptsWireCacheStreamWriter;
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
	double r      = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    subdiv = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 細分化(精度ピッチ)。既定 0 */
	if ( subdiv < 0 ) subdiv = 0;
	if ( subdiv > 4 ) subdiv = 4;                                  /* 上限(暴走防止) */

	mesh = thNEW(cgMesh3D,());
	cga_make_icosphere(mesh->mesh(), r, subdiv);
}

sPtr<ptsWireCacheStreamWriter>
cgaSphere_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}

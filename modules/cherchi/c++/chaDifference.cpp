/*
 * chaDifference — difference(a,b) の計算本体 (cherchi 版)。mesh arrangement + 厳密述語による二項ブール。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ch/c++/chMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/chaDifference_.h"

CLASS_TINYSTATE(ch/c++/chaDifference,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	chaDifference_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<chMesh>	mesh;
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
class chMesh;
TS_END_INTERFACE

#endif


chaDifference_::chaDifference_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
chaDifference_::compute()
{
	/* ★ #3436 P4: n 項で受ける。3 項以上は arrangement を 1 回だけ走らせる n 項ブールへ
	 *   (中間メッシュを作らない)。2 項は従来の二項 API のまま。 */
	const char *msg = 0;
	/* ★ 理由の受け皿は **この compute のローカル** (モジュール大域の static を置かない・
	 * in-proc では複数 op が同居しうるため。ひさ指示 2026-08-26)。 */
	char why[512];
	why[0] = '\0';
	mesh = chMesh::bool_from_args(args, "difference", &msg, why, (int)sizeof why);
	if ( ! mesh.is_notNull() ) {
		char b[600];   /* IRMB の例外文は長いことがある */
		::snprintf(b, sizeof b, "difference: %s", msg ? msg : "boolean failed");
		result = thNEW(pigDataError,(thNEW(stdString,(b))));
	}
}

sPtr<pigData>
chaDifference_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}

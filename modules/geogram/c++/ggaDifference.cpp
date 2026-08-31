/*
 * ggaDifference — difference(a,b) の計算本体 (geogram 版)。mesh arrangement + 厳密述語による二項ブール。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gg/c++/ggMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/ggaDifference_.h"

CLASS_TINYSTATE(gg/c++/ggaDifference,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggaDifference_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ggMesh>	mesh;
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
class ggMesh;
TS_END_INTERFACE

#endif


ggaDifference_::ggaDifference_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ggaDifference_::compute()
{
	/* ★ #3436 P4: n 項で受ける。3 項以上は arrangement を 1 回だけ走らせる n 項ブールへ
	 *   (中間メッシュを作らない)。2 項は従来の二項 API のまま。 */
	const char *msg = 0;
	/* ★ 理由の受け皿は **この compute のローカル** (モジュール大域の static を置かない・
	 * in-proc では複数 op が同居しうるため。ひさ指示 2026-08-26)。 */
	char why[512];
	why[0] = '\0';
	mesh = ggMesh::bool_from_args(args, "difference", &msg, why, (int)sizeof why);
	if ( ! mesh.is_notNull() ) {
		char b[600];   /* geogram の FATAL 文は長い (file/line 付き) */
		::snprintf(b, sizeof b, "difference: %s", msg ? msg : "boolean failed");
		result = thNEW(pigDataError,(thNEW(stdString,(b))));
	}
}

sPtr<pigData>
ggaDifference_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}

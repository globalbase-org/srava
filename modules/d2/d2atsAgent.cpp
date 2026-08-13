/*
 * d2atsAgent — 第2(2D)モジュール "d2" の実行体 (ptsGenericAgent 派生・rev4 次元分担デモ)。
 *   d3atsAgent の 2D 対。状態機械は共通基底 ptsGenericAgent に集約済みで、この派生は OPS 表と記述子だけ。
 *
 * 位置づけ (§9.4/§9.7 Q-E の実演): d3 (3D 専用) と d2 (2D 専用) が **同じ op 名 `dcount`** を、それぞれ
 *   自分の次元型 (d3-mesh3d / d2-shape2d) で申告する。dcount は 2 owner なので op-owner routing は発火せず、
 *   decide_executor が **入力型 (次元)** で正しいモジュールへ振る = (module×次元) 2 軸問題の型ディスパッチ解決。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"    /* ptsApp 値メンバの完全型 (基底 sRptr のデストラクタ実体化用) */
#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/ptsGenericAgent.h"   /* 共通基底 (状態機械) */
#include	"pig/c++/pigAgentRegistry.h"
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"d2/c++/d2aSquare.h"
#include	"d2/c++/d2aCount.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d2atsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(d2/c++/d2atsAgent,pig/c++/ptsGenericAgent)

template<class T> static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> p, sArray<sPtr<pigData> > *a, sPtr<stdString> t) { return thNEW(T,(p, a, t)); }

static const pigArgKind SQUARE_IN[]  = { AK_INLINE };   /* d2_square(s) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };    /* dcount(shape) */
static const pigOpEntry OPS[] = {
	{ "d2_square", SQUARE_IN,  1, AK_CACHE,  &mkCalcT<d2aSquare>, 0, "->d2-shape2d" },       /* leaf producer */
	/* ★ 共有 op (次元分担デモ): d3 と同じ `dcount` を **2D 型**で申告。decide_executor が入力型で振る。
	 *   2D の dcount は点数を返す (正方形=4)。 */
	{ "dcount",    MEASURE_IN, 1, AK_INLINE, &mkCalcT<d2aCount>,  0, "(d2-shape2d)->value" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d2atsAgent_(
		sPtr<ptsObject> parent);

protected:
	virtual const pigOpEntry*	agent_ops();
	virtual int			agent_n_ops();
	virtual const char*		agent_name();
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"pig/c++/pigOpEntry.h"
class ptsObject;
TS_END_INTERFACE

#endif


d2atsAgent_::d2atsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	d2atsAgent_::agent_ops()   { return OPS; }
int			d2atsAgent_::agent_n_ops() { return N_OPS; }
const char*		d2atsAgent_::agent_name()  { return "d2"; }

static sPtr<ptsAgent>
mk_d2atsAgent(sPtr<ptsObject> med)
{
	return thNEW(d2atsAgent,(med));
}

extern const pigModuleCodec d2_codecs[];   /* d2CacheCodec.cpp */
extern const srava_module_descriptor d2atsAgent_descriptor;
extern const srava_module_descriptor d2atsAgent_descriptor = {
	SRAVA_MODULE_ABI, "d2", 0,
	&mk_d2atsAgent, (unsigned)(EXEC_THREAD | EXEC_PROCESS), EXEC_PROCESS,
	OPS, N_OPS, 0, 0,
	"D2S2",   /* codec_tags */
	d2_codecs,
	"d2-shape2d", "D2S2",   /* types / type_tags (#3427 で manifest.cpp から移動) */
	"\x01" "D2S",   /* hash_salt: キャッシュキー弁別 (#3427 で manifest.cpp から移動) */
};
/* ★ #3427 ③: 旧・静的初期化の register_descriptor は撤去。登録は dlopen 経路
 * (pigModuleRegistry::load_file → register_descriptor) の 1 本 = app 所有レジストリへ。 */

/*
 * d2atsAgent — 第2(2D)モジュール "d2" の実行体 (ptsGenericAgent 派生・rev4 次元分担デモ)。
 *   d3atsAgent の 2D 対。状態機械は共通基底 ptsGenericAgent に集約済みで、この派生は OPS 表と記述子だけ。
 *
 * 位置づけ (§9.4/§9.7 Q-E の実演): d3 (3D 専用) と d2 (2D 専用) が **同じ op 名 `dcount`** を、それぞれ
 *   自分の次元型 (d3-mesh3d / d2-shape2d) で申告する。dcount は同名 op を 2 モジュールが持つので、
 *   decide_executor が **入力型 (次元)** で正しいモジュールへ振る = (module×次元) 2 軸問題の型ディスパッチ解決。
 */
#include	"pig/c++/ptsObject.h"
#include	"d2/c++/d2Shape.h"
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


static const pigArgKind SQUARE_IN[]  = { AK_INLINE };   /* d2_square(s) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };    /* dcount(shape) */
static const pigOpEntry OPS[] = {
	{ "d2_square", SQUARE_IN,  1, AK_CACHE,  OPWIRE(d2aSquare), 0, "->d2-shape2d" },       /* leaf producer */
	/* ★ 共有 op (次元分担デモ): d3 と同じ `dcount` を **2D 型**で申告。decide_executor が入力型で振る。
	 *   2D の dcount は点数を返す (正方形=4)。 */
	{ "dcount",    MEASURE_IN, 1, AK_INLINE, OPWIRE(d2aCount, d2Shape),  0, "(d2-shape2d)->value" },
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

extern const pigModuleType d2_provides[];
extern const srava_module_descriptor d2atsAgent_descriptor;
extern const srava_module_descriptor d2atsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "d2",
	.priority      = -3,   /* テスト専用。既定カーネル候補としては最下位群 (負値)・同点回避 */
	.make_agent    = &mk_d2atsAgent,
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = 0,
	.export_exts   = 0,
	.provides      = d2_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	.hash_salt     = "\x01" "D2S",   /* キャッシュキー弁別 (#3427 で manifest.cpp から移動) */
	/* ★ v7 (#3419): op 内並列の方式と σ (docs/srava_load_control_design.md §5.5/§5.6)。
	 *   テスト専用 */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};
/* ★ #3427 ③: 旧・静的初期化の register_descriptor は撤去。登録は dlopen 経路
 * (pigModuleRegistry::load_file → register_descriptor) の 1 本 = app 所有レジストリへ。 */

/*
 * d3atsAgent — 第3(mesh 出力)モジュール "d3" の実行体 (ptsGenericAgent 派生・rev4 Phase D-3)。
 *   ★ 状態機械は共通基底 ptsGenericAgent に集約済み (WAIT/STARTCALC/CALC/ERROR/FIN)。この派生は
 *   **OPS[] 表と記述子だけ**を持ち、agent_ops()/agent_n_ops()/agent_name() を override して基底に渡す。
 *   CGAL/Manifold/srava 言語を一切参照しない。
 *
 * 位置づけ (rev4 最終形の実証): demo.so (value-only) に続き **mesh (cacheable typed body) を出力する**
 *   第3モジュールを、ホスト無改修で走らせる。d3.so を探索路に置くだけで d3_cube/d3_merge/d3_nfaces/
 *   d3_nverts が使え、mesh の codec/wire-stream/cache 往復が成立する。
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
#include	"d3/c++/d3aCube.h"
#include	"d3/c++/d3aMerge.h"
#include	"d3/c++/d3aNfaces.h"
#include	"d3/c++/d3aNverts.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d3atsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(d3/c++/d3atsAgent,pig/c++/ptsGenericAgent)

/* ---- ディスパッチテーブル (ファイルスコープ・pig 層の共通型) ---- */
template<class T> static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> p, sArray<sPtr<pigData> > *a, sPtr<stdString> t) { return thNEW(T,(p, a, t)); }

static const pigArgKind CUBE_IN[]    = { AK_INLINE };            /* d3_cube(s) */
static const pigArgKind MERGE_IN[]   = { AK_CACHE, AK_CACHE };   /* d3_merge(a,b) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };            /* d3_nfaces/d3_nverts(m) */
static const pigOpEntry OPS[] = {
	{ "d3_cube",   CUBE_IN,    1, AK_CACHE,  &mkCalcT<d3aCube>,   0, "->d3-mesh3d" },                 /* leaf producer */
	{ "d3_merge",  MERGE_IN,   2, AK_CACHE,  &mkCalcT<d3aMerge>,  0, "(d3-mesh3d,d3-mesh3d)->d3-mesh3d" },
	{ "d3_nfaces", MEASURE_IN, 1, AK_INLINE, &mkCalcT<d3aNfaces>, 0, "(d3-mesh3d)->value" },
	{ "d3_nverts", MEASURE_IN, 1, AK_INLINE, &mkCalcT<d3aNverts>, 0, "(d3-mesh3d)->value" },
	/* ★ 共有 op (次元分担デモ・§9.4/§9.7 Q-E): d2 モジュールと **同じ op 名 `dcount`** を、それぞれ自分の
	 *   次元型で申告する。2 owner なので op-owner routing は発火せず、decide_executor が入力型 (d3-mesh3d
	 *   か d2-shape2d か) で正しいモジュールへ振る = (module×次元) 2 軸問題の型ディスパッチによる解決。
	 *   3D の dcount は頂点数を返す (計算本体は d3aNverts を再利用・立方体=8)。 */
	{ "dcount",    MEASURE_IN, 1, AK_INLINE, &mkCalcT<d3aNverts>, 0, "(d3-mesh3d)->value" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d3atsAgent_(
		sPtr<ptsObject> parent);

protected:
	/* 基底 ptsGenericAgent の generic 状態機械へ OPS 表 / 名前を渡すだけ (状態機械は書かない)。 */
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


d3atsAgent_::d3atsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	d3atsAgent_::agent_ops()   { return OPS; }
int			d3atsAgent_::agent_n_ops() { return N_OPS; }
const char*		d3atsAgent_::agent_name()  { return "d3"; }

/* この実行体を "d3" として登録 (srava_agent が dlopen 時に make_agent で起こす)。 */
static sPtr<ptsAgent>
mk_d3atsAgent(sPtr<ptsObject> med)
{
	return thNEW(d3atsAgent,(med));
}

/* 自己申告記述子 (単一ソース)。priority=0 (opt-in)・exec_caps THREAD|PROCESS・**exec_default=PROCESS**。
 * PROCESS 既定にするのは、in-proc(THREAD)だと mesh 本体が planner 内に in-memory 共有され codec を
 * 経由しないため — PROCESS で agent プロセス跨ぎにし、d3 の codec/wire-stream/cache 往復を実走させる
 * (かつ複数 in-proc thread モジュール境界 ⑤ を踏まない)。namespace scope の const は既定で内部
 * リンケージ → manifest.cpp から extern 参照するため extern 明示。 */
extern const pigModuleCodec d3_codecs[];   /* d3CacheCodec.cpp */
extern const srava_module_descriptor d3atsAgent_descriptor;
extern const srava_module_descriptor d3atsAgent_descriptor = {
	SRAVA_MODULE_ABI, "d3", 0,
	&mk_d3atsAgent, (unsigned)(EXEC_THREAD | EXEC_PROCESS), EXEC_PROCESS,
	OPS, N_OPS, 0, 0,
	"D3M3",   /* codec_tags */
	d3_codecs,   /* reader/writer factory (自型 D3M3) */
	"d3-mesh3d", "D3M3",   /* types / type_tags (#3427 で manifest.cpp から移動) */
	"\x01" "D3M",   /* hash_salt: キャッシュキー弁別 (#3427 で manifest.cpp から移動) */
};
/* ★ #3427 ③: 旧・静的初期化の register_descriptor は撤去。登録は dlopen 経路
 * (pigModuleRegistry::load_file → register_descriptor) の 1 本 = app 所有レジストリへ。 */

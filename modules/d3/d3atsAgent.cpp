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
#include	"d3/c++/d3Mesh.h"
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

static const pigArgKind CUBE_IN[]    = { AK_INLINE };            /* d3_cube(s) */
static const pigArgKind MERGE_IN[]   = { AK_CACHE, AK_CACHE };   /* d3_merge(a,b) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };            /* d3_nfaces/d3_nverts(m) */
static const pigOpEntry OPS[] = {
	{ "d3_cube",   CUBE_IN,    1, AK_CACHE,  OPWIRE(d3aCube),   0, "->d3-mesh3d" },                 /* leaf producer */
	{ "d3_merge",  MERGE_IN,   2, AK_CACHE,  OPWIRE(d3aMerge, d3Mesh, d3Mesh),  0, "(d3-mesh3d,d3-mesh3d)->d3-mesh3d" },
	{ "d3_nfaces", MEASURE_IN, 1, AK_INLINE, OPWIRE(d3aNfaces, d3Mesh), 0, "(d3-mesh3d)->value" },
	{ "d3_nverts", MEASURE_IN, 1, AK_INLINE, OPWIRE(d3aNverts, d3Mesh), 0, "(d3-mesh3d)->value" },
	/* ★ 共有 op (次元分担デモ・§9.4/§9.7 Q-E): d2 モジュールと **同じ op 名 `dcount`** を、それぞれ自分の
	 *   次元型で申告する。同名 op を 2 モジュールが持つので、decide_executor が入力型 (d3-mesh3d
	 *   か d2-shape2d か) で正しいモジュールへ振る = (module×次元) 2 軸問題の型ディスパッチによる解決。
	 *   3D の dcount は頂点数を返す (計算本体は d3aNverts を再利用・立方体=8)。 */
	{ "dcount",    MEASURE_IN, 1, AK_INLINE, OPWIRE(d3aNverts, d3Mesh), 0, "(d3-mesh3d)->value" },
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

/* 自己申告記述子 (単一ソース)。priority=-2 (opt-in)・exec_caps THREAD|PROCESS・**exec_default=PROCESS**。
 * PROCESS 既定にするのは、in-proc(THREAD)だと mesh 本体が planner 内に in-memory 共有され codec を
 * 経由しないため — PROCESS で agent プロセス跨ぎにし、d3 の codec/wire-stream/cache 往復を実走させる
 * (かつ複数 in-proc thread モジュール境界 ⑤ を踏まない)。namespace scope の const は既定で内部
 * リンケージ → manifest.cpp から extern 参照するため extern 明示。 */
extern const pigModuleType d3_provides[];
extern const srava_module_descriptor d3atsAgent_descriptor;
extern const srava_module_descriptor d3atsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "d3",
	.priority      = -2,   /* テスト専用。既定カーネル候補としては最下位群 (負値)・同点回避 */
	.make_agent    = &mk_d3atsAgent,
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = 0,
	.export_exts   = 0,
	.provides      = d3_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	.hash_salt     = "\x01" "D3M",   /* キャッシュキー弁別 (#3427 で manifest.cpp から移動) */
	/* ★ v7 (#3419): op 内並列の方式と σ (docs/srava_load_control_design.md §5.5/§5.6)。
	 *   テスト専用 */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};
/* ★ #3427 ③: 旧・静的初期化の register_descriptor は撤去。登録は dlopen 経路
 * (pigModuleRegistry::load_file → register_descriptor) の 1 本 = app 所有レジストリへ。 */

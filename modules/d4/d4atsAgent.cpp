/*
 * d4atsAgent — 第4(in-proc mesh 消費)モジュール "d4" の実行体 (ptsGenericAgent 派生・⑤ P4)。
 *   ★ 状態機械は共通基底 ptsGenericAgent に集約済み (WAIT/STARTCALC/CALC/ERROR/FIN)。この派生は
 *   **OPS[] 表と記述子だけ**を持ち、agent_ops()/agent_n_ops()/agent_name() を override して基底に渡す。
 *   CGAL/Manifold/srava 言語を一切参照しない (d3 のクローン)。
 *
 * 位置づけ (⑤ cross-module 型変換の実証): d3 と違い **exec_default=EXEC_THREAD (in-proc)**。
 *   2 個目の in-proc mesh モジュールとして、別モジュール (manifold=in-proc) が作った mf-mesh3d を
 *   **自型 (d4-mesh3d) として in-proc 消費する** = converted get_body(type) 経路を初 exercise する。
 *   d4_nfaces/d4_nverts は sig に foreign 入力型 (mf-mesh3d) を明示列挙 → decide_executor が d4 へ振り、
 *   d4CacheCodec の d4-mf-upgrade reader が MFM3 file を d4Mesh へ変換読みする (mfMesh の MFM3 framing
 *   は d4Mesh と同一)。d4_cube/d4_merge は自型 leaf/consumer (自型往復・standalone 実証用)。
 */
#include	"pig/c++/ptsObject.h"
#include	"d4/c++/d4Mesh.h"
#include	"pig/c++/ptsApplication.h"    /* ptsApp 値メンバの完全型 (基底 sRptr のデストラクタ実体化用) */
#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/ptsGenericAgent.h"   /* 共通基底 (状態機械) */
#include	"pig/c++/pigAgentRegistry.h"
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"d4/c++/d4aCube.h"
#include	"d4/c++/d4aMerge.h"
#include	"d4/c++/d4aNfaces.h"
#include	"d4/c++/d4aNverts.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d4atsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(d4/c++/d4atsAgent,pig/c++/ptsGenericAgent)

/* ---- ディスパッチテーブル (ファイルスコープ・pig 層の共通型) ---- */

static const pigArgKind CUBE_IN[]    = { AK_INLINE };            /* d4_cube(s) */
static const pigArgKind MERGE_IN[]   = { AK_CACHE, AK_CACHE };   /* d4_merge(a,b) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };            /* d4_nfaces/d4_nverts(m) */
static const pigOpEntry OPS[] = {
	{ "d4_cube",   CUBE_IN,    1, AK_CACHE,  OPWIRE(d4aCube),   0, "->d4-mesh3d" },                 /* leaf producer */
	{ "d4_merge",  MERGE_IN,   2, AK_CACHE,  OPWIRE(d4aMerge, d4Mesh, d4Mesh),  0, "(d4-mesh3d,d4-mesh3d)->d4-mesh3d" },
	/* ★ ⑤ P4: d4_nfaces/d4_nverts は **自型 d4-mesh3d に加え foreign mf-mesh3d も引受ける** (sig に明示列挙)。
	 *   → decide_executor が d4_nfaces(mfMesh) を d4 へ振り (d4 が唯一の owner・mf 入力を sig で受理)、
	 *   d4 agent の get_body(wantTypes=[d4-mesh3d]) が MFM3 file を d4-mf-upgrade reader で変換読みする。
	 *   これが in-proc cross-module 変換 (converted 経路) を発火させる唯一の入口 (rev4 sig 化の disjoint 原則)。 */
	{ "d4_nfaces", MEASURE_IN, 1, AK_INLINE, OPWIRE(d4aNfaces, d4Mesh), 0, "(d4-mesh3d)->value;(mf-mesh3d)->value" },
	{ "d4_nverts", MEASURE_IN, 1, AK_INLINE, OPWIRE(d4aNverts, d4Mesh), 0, "(d4-mesh3d)->value;(mf-mesh3d)->value" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d4atsAgent_(
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


d4atsAgent_::d4atsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	d4atsAgent_::agent_ops()   { return OPS; }
int			d4atsAgent_::agent_n_ops() { return N_OPS; }
const char*		d4atsAgent_::agent_name()  { return "d4"; }

/* この実行体を "d4" として登録 (srava_agent が dlopen 時に make_agent で起こす)。 */
static sPtr<ptsAgent>
mk_d4atsAgent(sPtr<ptsObject> med)
{
	return thNEW(d4atsAgent,(med));
}

/* 自己申告記述子 (単一ソース)。priority=-4 (opt-in)・exec_caps THREAD|PROCESS・**exec_default=EXEC_THREAD**。
 * ★ d3 と異なり THREAD 既定にするのが ⑤ P4 の核心: 別モジュール (manifold) の mf mesh を **in-proc で
 *   消費する** 2 個目の in-proc thread モジュールとして、converted get_body(type) 経路を exercise する。
 *   同型 (d4→d4) の in-proc は in-memory fast path (codec 非経由) だが、foreign (mf→d4) の消費では
 *   get_body(type) が file を d4-mf-upgrade reader で変換読みするので codec/wire-stream/cache が走る。
 *   namespace scope の const は既定で内部リンケージ → manifest.cpp から extern 参照するため extern 明示。 */
extern const pigModuleType d4_provides[];
extern const srava_module_descriptor d4atsAgent_descriptor;
extern const srava_module_descriptor d4atsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "d4",
	.priority      = -4,   /* テスト専用。既定カーネル候補としては最下位群 (負値)・同点回避 */
	.make_agent    = &mk_d4atsAgent,
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_THREAD,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = 0,
	.export_exts   = 0,
	.provides      = d4_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	.hash_salt     = "\x01" "D4M",   /* キャッシュキー弁別 (#3427 で manifest.cpp から移動) */
	/* ★ v7 (#3419): op 内並列の方式と σ (docs/srava_load_control_design.md §5.5/§5.6)。
	 *   テスト専用 */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};
/* ★ #3427 ③: 旧・静的初期化の register_descriptor は撤去。登録は dlopen 経路
 * (pigModuleRegistry::load_file → register_descriptor) の 1 本 = app 所有レジストリへ。 */

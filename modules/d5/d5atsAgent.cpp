/*
 * d5atsAgent — 第5(in-proc mesh 消費)モジュール "d5" の実行体 (ptsGenericAgent 派生・⑤ P4)。
 *   ★ 状態機械は共通基底 ptsGenericAgent に集約済み (WAIT/STARTCALC/CALC/ERROR/FIN)。この派生は
 *   **OPS[] 表と記述子だけ**を持ち、agent_ops()/agent_n_ops()/agent_name() を override して基底に渡す。
 *   CGAL/Manifold/srava 言語を一切参照しない (d3 のクローン)。
 *
 * 位置づけ (⑤ の conv **多型共存**テスト用): d4 と同型の in-proc mesh 消費モジュール (exec_default=THREAD)
 *   だが **別の自型 d5-mesh3d** を持つ。d4 と d5 が **同一の mf mesh を各々の自型 (d4-mesh3d / d5-mesh3d)
 *   として同時 in-proc 消費**することで、pigDataCache の conv body-list に **型別エントリが 2 つ共存**する
 *   ことを実証する (旧・単一 conv スロットだと互いに上書き/競合した所)。STARTCALC の wantTypes は
 *   モジュールの mesh 出力型から作られるので、別自型を持つには別モジュールが要る (d4 内の 2 型では不可)。
 *   d5_nfaces/d5_nverts は sig に foreign 入力型 (mf-mesh3d) を明示列挙 → decide_executor が d5 へ振り、
 *   d5CacheCodec の d5-mf-upgrade reader が MFM3 file を d5Mesh へ変換読みする。d5_cube/d5_merge は自型 leaf。
 */
#include	"pig/c++/ptsObject.h"
#include	"d5/c++/d5Mesh.h"
#include	"pig/c++/ptsApplication.h"    /* ptsApp 値メンバの完全型 (基底 sRptr のデストラクタ実体化用) */
#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/ptsGenericAgent.h"   /* 共通基底 (状態機械) */
#include	"pig/c++/pigAgentRegistry.h"
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"d5/c++/d5aCube.h"
#include	"d5/c++/d5aMerge.h"
#include	"d5/c++/d5aNfaces.h"
#include	"d5/c++/d5aNverts.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d5atsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(d5/c++/d5atsAgent,pig/c++/ptsGenericAgent)

/* ---- ディスパッチテーブル (ファイルスコープ・pig 層の共通型) ---- */

static const pigArgKind CUBE_IN[]    = { AK_INLINE };            /* d5_cube(s) */
static const pigArgKind MERGE_IN[]   = { AK_CACHE, AK_CACHE };   /* d5_merge(a,b) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };            /* d5_nfaces/d5_nverts(m) */
static const pigOpEntry OPS[] = {
	{ "d5_cube",   CUBE_IN,    1, AK_CACHE,  OPWIRE(d5aCube),   0, "->d5-mesh3d" },                 /* leaf producer */
	{ "d5_merge",  MERGE_IN,   2, AK_CACHE,  OPWIRE(d5aMerge, d5Mesh, d5Mesh),  0, "(d5-mesh3d,d5-mesh3d)->d5-mesh3d" },
	/* ★ ⑤ P4: d5_nfaces/d5_nverts は **自型 d5-mesh3d に加え foreign mf-mesh3d も引受ける** (sig に明示列挙)。
	 *   → decide_executor が d5_nfaces(mfMesh) を d5 へ振り (d5 が唯一の owner・mf 入力を sig で受理)、
	 *   d5 agent の get_body(wantTypes=[d5-mesh3d]) が MFM3 file を d5-mf-upgrade reader で変換読みする。
	 *   これが in-proc cross-module 変換 (converted 経路) を発火させる唯一の入口 (rev4 sig 化の disjoint 原則)。 */
	{ "d5_nfaces", MEASURE_IN, 1, AK_INLINE, OPWIRE(d5aNfaces, d5Mesh), 0, "(d5-mesh3d)->value;(mf-mesh3d)->value" },
	{ "d5_nverts", MEASURE_IN, 1, AK_INLINE, OPWIRE(d5aNverts, d5Mesh), 0, "(d5-mesh3d)->value;(mf-mesh3d)->value" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d5atsAgent_(
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


d5atsAgent_::d5atsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	d5atsAgent_::agent_ops()   { return OPS; }
int			d5atsAgent_::agent_n_ops() { return N_OPS; }
const char*		d5atsAgent_::agent_name()  { return "d5"; }

/* この実行体を "d5" として登録 (srava_agent が dlopen 時に make_agent で起こす)。 */
static sPtr<ptsAgent>
mk_d5atsAgent(sPtr<ptsObject> med)
{
	return thNEW(d5atsAgent,(med));
}

/* 自己申告記述子 (単一ソース)。priority=-5 (opt-in)・exec_caps THREAD|PROCESS・**exec_default=EXEC_THREAD**。
 * ★ d3 と異なり THREAD 既定にするのが ⑤ P4 の核心: 別モジュール (manifold) の mf mesh を **in-proc で
 *   消費する** 2 個目の in-proc thread モジュールとして、converted get_body(type) 経路を exercise する。
 *   同型 (d5→d5) の in-proc は in-memory fast path (codec 非経由) だが、foreign (mf→d5) の消費では
 *   get_body(type) が file を d5-mf-upgrade reader で変換読みするので codec/wire-stream/cache が走る。
 *   namespace scope の const は既定で内部リンケージ → manifest.cpp から extern 参照するため extern 明示。 */
extern const pigModuleType d5_provides[];
extern const srava_module_descriptor d5atsAgent_descriptor;
extern const srava_module_descriptor d5atsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "d5",
	.priority      = -5,   /* テスト専用。既定カーネル候補としては最下位群 (負値)・同点回避 */
	.make_agent    = &mk_d5atsAgent,
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_THREAD,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = 0,
	.export_exts   = 0,
	.provides      = d5_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	.hash_salt     = "\x01" "D5M",   /* キャッシュキー弁別 (#3427 で manifest.cpp から移動) */
	/* ★ v7 (#3419): op 内並列の方式と σ (docs/srava_load_control_design.md §5.5/§5.6)。
	 *   テスト専用 */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};
/* ★ #3427 ③: 旧・静的初期化の register_descriptor は撤去。登録は dlopen 経路
 * (pigModuleRegistry::load_file → register_descriptor) の 1 本 = app 所有レジストリへ。 */

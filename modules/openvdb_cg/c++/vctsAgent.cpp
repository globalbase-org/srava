/*
 * vctsAgent — ★ **openvdb ⇄ manifold の境界モジュール** (#3434・2026-08-22)。
 *
 * ★★ このモジュールの存在理由: **cast 系は両側を知っていなければならない**。
 *   旧実装は openvdb.so の中で mf-mesh3d を名乗る新クラス vdMesh を作っていたが、それは
 *   「新しい型を作らない」の違反で、**in-proc で d_cast が失敗する**原因だった。
 *   → 変換だけを取り出し、**両側の本物のクラス** (mfMesh / vdGrid) を使う。
 *
 * ★ 幾何クラス (vdGrid) は **libsrava_vd.so** に置いてある。openvdb.so / openvdb_mf.so /
 *   openvdb_cg.so が **同じ実体**を共有するため (各自がコピーを持つと同じバグが再発する)。
 *
 * ⚠⚠ ライセンス: OpenVDB (Apache-2.0) + **CGAL (GPL)** → **このモジュールは GPL**。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/ptsGenericAgent.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"vd/c++/vdGrid.h"
#include	"cg/c++/cgMesh.h"
#include	"vc/c++/vcaVoxelize.h"
#include	"vc/c++/vcaIsosurface.h"
#include	"_ts2/c++/vctsAgent_.h"

#include	<stdio.h>    /* SRAVA_LOAD_LOG の診断出力 */
#include	<stdlib.h>   /* getenv */

CLASS_TINYSTATE(vc/c++/vctsAgent,pig/c++/ptsGenericAgent)

/* 計算本体生成子 thunk (各モジュール .cpp に据え置く定型)。 */
template <class T>
static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> parent, sArray<sPtr<pigData> > *args, sPtr<stdString> target)
{
	return sPtr<ptsCalcBody>::d_cast(thNEW(T,(parent, args, target)));
}

static const pigArgKind VOXELIZE_IN[] = { AK_CACHE, AK_INLINE };   /* voxelize(mesh, dx) */
static const pigArgKind GRIDISO_IN[]  = { AK_CACHE, AK_INLINE };   /* isosurface(v, iso) */

static const pigOpEntry OPS[] = {
	/* ★ 境界の変換 2 本だけ。格子演算は openvdb.so が持つ (このモジュールは繋ぐだけ)。
	 *   ★ **codec ではなく普通の op** — 両側を知っているので dx / iso を素直に引数で取れる。 */
	{ "voxelize",   VOXELIZE_IN, 2, AK_CACHE, OPWIRE(vcaVoxelize, cgMesh),   0, "(cg-mesh3d)->" VD_TYPE },
	{ "isosurface", GRIDISO_IN,  2, AK_CACHE, OPWIRE(vcaIsosurface, vdGeom), 0, "(" VD_TYPE ")->cg-mesh3d" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vctsAgent_(
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
#include	"ts2/c++/sRptr.h"
class ptsObject;
struct pigOpEntry;
TS_END_INTERFACE

#endif


vctsAgent_::vctsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	vctsAgent_::agent_ops()	  { return OPS; }
int			vctsAgent_::agent_n_ops() { return N_OPS; }
const char*		vctsAgent_::agent_name()  { return "openvdb_cg"; }


static sPtr<ptsAgent>
mk_vctsAgent(sPtr<ptsObject> parent)
{
	return sPtr<ptsAgent>::d_cast(thNEW(vctsAgent,(parent)));
}

/* 自己申告記述子。
 *  - priority=1: **既定カーネルにはならない** (既定は cgal の 20)。そもそも leaf 生成 op を
 *    持たない (ボリュームはメッシュから作るもの) ので、既定になっても意味が無い。
 *    ★ 現在の梯子 (2026-08-25): cgal 20 > manifold 10 > geogram 6 > nef 5 > pipe_proximity 4 >
 *      occt 2 > openvdb 1 > (テスト専用は負値: demo -1 / d3 -2 / d2 -3 / d4 -4 / d5 -5)。
 *      ★同点の勝敗は不定なので、同梱モジュールは互いに重複させない。
 *  - exec: PROCESS のみ。OpenVDB はプロセス全体のグローバル初期化 (openvdb::initialize) と
 *    **TBB** を使うので、in-proc (planner 内スレッド) の安全性は未検証。geogram と同じく
 *    まず process から。★in-proc 化は #3419 (op 内並列と op 間並列の調停) とまとめて扱う —
 *    そこに踏み込むと planner のプロセスに TBB のスレッドプールが入り、srava 自身の
 *    DAG 並列とスレッドを取り合うため。
 */
extern const pigModuleType openvdb_cg_provides[];
extern const srava_module_descriptor vctsAgent_descriptor;
extern const srava_module_descriptor vctsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "openvdb_cg",
		/* ★ 変換専用なので既定カーネルにはならない。梯子 (2026-08-25): cgal 20 > manifold 10 > geogram 6 >
	 *   nef 5 > pipe_proximity 4 > occt 2 > openvdb 1 > openvdb_mf 0 (テスト専用は負値)。 */
	.priority      = 0,
	.make_agent    = &mk_vctsAgent,
	/* ★ 実験用 (2026-08-22): in-proc も **可能**にする。⚠ 既定は PROCESS のまま変えない。
	 *   in-proc を試すには module("openvdb.so",{exec_default:"thread"}) と明示すること。
	 *   狙い: in-proc agent は planner と同一アドレス空間なので **C_MEM に遅延なく含まれる**
	 *   (process agent の pid 登録遅れが原理的に無い)。メモリ制御が効くかを測るため。
	 *   ⚠ 安全性は未検証: openvdb のグローバル初期化と TBB を planner プロセスに持ち込む。 */
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = "",   /* なし */
	.export_exts   = "",   /* なし (等値面を書きたいなら cast でメッシュ系へ) */
	/* ★ **codec を持たない**。読み書きは相手側 (libsrava_mf / libsrava_vd) の codec が行う。
	 *   このモジュールは「両側の本物のクラスを繋ぐ op」だけを提供する。 */
	.provides      = openvdb_cg_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	/* ★ **自分の型を持たない** (新しい型を作らない)。入出力はどちらも相手の型
	 *   (mf-mesh3d = manifold / vd-grid3d = openvdb)。 */
	/* ★ **両側の型を申告する**。新しい型は作っていない — 実体は libsrava_mf / libsrava_vd の
	 *   本物のクラス (mfMesh / vdGrid) なので、in-proc でも d_cast が通る。 */
	.hash_salt     = "\x01" "VDC",   /* キャッシュキー弁別 */
	/* initialize: 無し。openvdb::initialize() は vdGrid::ensure_init() が全 op の入口で
	 * 1 回だけ呼んでいる (このモジュールは EXEC_PROCESS = 1 プロセス 1 モジュール)。
	 * in-proc 化 (#3419) するときに、ここへ移すかを再検討する。 */
	.initialize    = 0,
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};

/*
 * nfctsAgent — ★ **nef_snc ⇄ cgal の境界モジュール** (akira-project #3499)。
 *
 * ★ 経緯: #3478 は nef_snc の cache に厳密境界を **付録**として併記し、cgal / manifold に
 *   それを読ませていた。付録のために **encode ごとに to_mesh() が走る**ので実時間の代償が
 *   大きく (#3499)、nef_snc は「常に SNC だけ」へ戻した。他カーネルへ渡す口はこの橋が持つ。
 *
 * ★ 受けるのは **nf-mesh3d だけ** (nfb-mesh3d = nef_hybrid は受けない)。
 *   理由は 2 つ:
 *     ① hybrid は普通の立体を厳密境界で書くので、cgal.so がそのまま読める = 橋が要らない。
 *     ② 幾何クラスの実体は **変種ごとに別の共有ライブラリ** (libsrava_nf_snc / _hybrid) に
 *        あり、シンボルバージョンで分離してある。この橋は snc 側にリンクしているので、
 *        hybrid が作った nfMesh を in-proc で d_cast しても通らない。
 *
 * ライセンス: CGAL (GPL) のみ。nef も cgal も CGAL なので新たな混入は無い。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/ptsGenericAgent.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"nf/c++/nfMesh.h"
#include	"cg/c++/cgMesh.h"
#include	"nfc/c++/nfcCast.h"
#include	"_ts2/c++/nfctsAgent_.h"

CLASS_TINYSTATE(nfc/c++/nfctsAgent,pig/c++/ptsGenericAgent)

/* 計算本体生成子 thunk (各モジュール .cpp に据え置く定型)。 */
template <class T>
static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> parent, sArray<sPtr<pigData> > *args, sPtr<stdString> target)
{
	return sPtr<ptsCalcBody>::d_cast(thNEW(T,(parent, args, target)));
}

static const pigArgKind CAST_IN[] = { AK_INLINE, AK_CACHE };   /* cast("cg-mesh3d", x) */

static const pigOpEntry OPS[] = {
	/* ★ 変換 1 本だけ。Nef のブールは nef_snc.so が持つ (この橋は繋ぐだけ)。
	 *   ★ **cast** で正しい — 情報の落ちない厳密変換なので粒度パラメータが要らない
	 *     (occt_mf の triangulate が cast でないのは deflection が要るから)。
	 *   ★ 出力型は cg-mesh3d = **本物の cgMesh3D**。名前だけ借りた別クラスではない。 */
	{ "cast", CAST_IN, 2, AK_CACHE, OPWIRE(nfcCast, nfGeom), 0, "(nf-mesh3d)->cg-mesh3d" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfctsAgent_(
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


nfctsAgent_::nfctsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	nfctsAgent_::agent_ops()   { return OPS; }
int			nfctsAgent_::agent_n_ops() { return N_OPS; }
const char*		nfctsAgent_::agent_name()  { return "nef_cg"; }


static sPtr<ptsAgent>
mk_nfctsAgent(sPtr<ptsObject> parent)
{
	return sPtr<ptsAgent>::d_cast(thNEW(nfctsAgent,(parent)));
}

extern const pigModuleType nef_cg_provides[];
extern const srava_module_descriptor nfctsAgent_descriptor;
extern const srava_module_descriptor nfctsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "nef_cg",
	/* ★ 変換専用なので既定カーネルにはならない (leaf 生成 op を持たない)。
	 *   境界モジュールは priority 0 で揃える (occt_mf / openvdb_mf と同じ)。 */
	.priority      = 0,
	.make_agent    = &mk_nfctsAgent,
	/* ★ nef_snc.so と揃えて **PROCESS のみ**。入力は EPECK 上に構築された Nef で、
	 *   その値は in-proc 共有に耐えない (nef / cgal と同じ理由)。 */
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = "",   /* なし (ファイルの入口は nef_snc.so / cgal.so が持つ) */
	.provides      = nef_cg_provides,
	.cache_version = 1,
};

/*
 * nfmtsAgent — ★ **nef_snc ⇄ manifold の境界モジュール** (akira-project #3499)。
 *
 * ★ 経緯: #3478 は nef_snc の cache に厳密境界を **付録**として併記し、cgal / manifold に
 *   それを読ませていた。付録のために **encode ごとに to_mesh() が走る**ので実時間の代償が
 *   大きく (#3499)、nef_snc は「常に SNC だけ」へ戻した。他カーネルへ渡す口はこの橋が持つ。
 *
 * ★ 受けるのは **nf-mesh3d だけ** (nfb-mesh3d = nef_hybrid は受けない)。
 *     ① hybrid は普通の立体を厳密境界で書くので manifold.so がそのまま読める = 橋が要らない。
 *     ② 幾何クラスの実体は **変種ごとに別の共有ライブラリ** (libsrava_nf_snc / _hybrid) に
 *        あり、シンボルバージョンで分離してある。この橋は snc 側にリンクしているので、
 *        hybrid が作った nfMesh を in-proc で d_cast しても通らない。
 *
 * ライセンス: CGAL (GPL) + Manifold (Apache-2.0)。**混ざるのはこの .so だけ**で、
 *   manifold.so は CGAL 非依存のまま (それが #3478 で reader を足せなかった理由でもある)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/ptsGenericAgent.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"nf/c++/nfMesh.h"
#include	"mf/c++/mfMesh.h"
#include	"nfm/c++/nfmCast.h"
#include	"_ts2/c++/nfmtsAgent_.h"

CLASS_TINYSTATE(nfm/c++/nfmtsAgent,pig/c++/ptsGenericAgent)

/* 計算本体生成子 thunk (各モジュール .cpp に据え置く定型)。 */
template <class T>
static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> parent, sArray<sPtr<pigData> > *args, sPtr<stdString> target)
{
	return sPtr<ptsCalcBody>::d_cast(thNEW(T,(parent, args, target)));
}

static const pigArgKind CAST_IN[] = { AK_INLINE, AK_CACHE };   /* cast("mf-mesh3d", x) */

static const pigOpEntry OPS[] = {
	/* ★ 変換 1 本だけ。Nef のブールは nef_snc.so が持つ (この橋は繋ぐだけ)。
	 *   ★ **cast** で正しい — 粒度パラメータが要らない (厳密境界を取ってから double 化する
	 *     だけで、丸め方は cg → mf の既存 cast と同一)。
	 *   ★ 出力型は mf-mesh3d = **本物の mfMesh**。名前だけ借りた別クラスではない。 */
	{ "cast", CAST_IN, 2, AK_CACHE, OPWIRE(nfmCast, nfGeom), 0, "(nf-mesh3d)->mf-mesh3d" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfmtsAgent_(
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


nfmtsAgent_::nfmtsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	nfmtsAgent_::agent_ops()   { return OPS; }
int			nfmtsAgent_::agent_n_ops() { return N_OPS; }
const char*		nfmtsAgent_::agent_name()  { return "nef_mf"; }


static sPtr<ptsAgent>
mk_nfmtsAgent(sPtr<ptsObject> parent)
{
	return sPtr<ptsAgent>::d_cast(thNEW(nfmtsAgent,(parent)));
}

extern const pigModuleType nef_mf_provides[];
extern const srava_module_descriptor nfmtsAgent_descriptor;
extern const srava_module_descriptor nfmtsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "nef_mf",
	/* ★ 変換専用なので既定カーネルにはならない (leaf 生成 op を持たない)。
	 *   境界モジュールは priority 0 で揃える (occt_mf / openvdb_mf と同じ)。 */
	.priority      = 0,
	.make_agent    = &mk_nfmtsAgent,
	/* ★ nef_snc.so と揃えて **PROCESS のみ**。入力は EPECK 上に構築された Nef で、
	 *   その値は in-proc 共有に耐えない (nef / cgal と同じ理由)。 */
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = "",   /* なし (ファイルの入口は nef_snc.so / manifold.so が持つ) */
	.provides      = nef_mf_provides,
	.cache_version = 1,
};

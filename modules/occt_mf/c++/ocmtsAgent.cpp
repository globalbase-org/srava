/*
 * ocmtsAgent — ★ **occt ⇄ manifold の境界モジュール** (akira-project #3452)。
 *
 * ★★ このモジュールの存在理由: **表現クラスをまたぐ op は両側を知っていなければならない**。
 *   旧実装は occt.so の中で mf-mesh3d を名乗る内部クラス ocMesh を作っていたが、それは
 *   「新しい型を作らない」の違反で、
 *     ① in-proc で d_cast<mfMesh> が失敗する
 *     ② codec が (MFM3 → mf-mesh3d) の**読み手**としても名乗るので、#3452 (遅延ロード) で
 *        登録順が module() 順になった結果 **occt を先に読むと本家 manifold より先に選ばれ**、
 *        下流の nfaces が壊れる (module() の呼び出し順で結果が変わる、という症状で発覚)
 *   の原因だった。→ 変換だけを取り出し、**両側の本物のクラス** (ocShape / mfMesh) を使う。
 *   openvdb + openvdb_mf/cg/gg (#3434) とまったく同じ構図。
 *
 * ★ 幾何クラス (ocShape) は **libsrava_oc.so** に置いてある。occt.so / occt_mf.so が
 *   **同じ実体**を共有するため (各自がコピーを持つと in-proc で d_cast が失敗する)。
 *
 * ★ occt.so 自身は **manifold に依存しないまま**である (受け入れ条件)。manifold を要るのは
 *   この境界モジュールだけなので、SRAVA_MODULE_OCCT=ON は manifold を要求しない。
 *
 * ライセンス: OCCT (LGPL-2.1 + 例外) + Manifold (Apache-2.0)。**GPL は混ざらない**。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/ptsGenericAgent.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"oc/c++/ocShape.h"
#include	"mf/c++/mfMesh.h"
#include	"ocm/c++/ocmTriangulate.h"
#include	"_ts2/c++/ocmtsAgent_.h"

CLASS_TINYSTATE(ocm/c++/ocmtsAgent,pig/c++/ptsGenericAgent)

/* 計算本体生成子 thunk (各モジュール .cpp に据え置く定型)。 */
template <class T>
static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> parent, sArray<sPtr<pigData> > *args, sPtr<stdString> target)
{
	return sPtr<ptsCalcBody>::d_cast(thNEW(T,(parent, args, target)));
}

static const pigArgKind TRI_IN[] = { AK_CACHE, AK_INLINE };   /* triangulate(s, defl) */

static const pigOpEntry OPS[] = {
	/* ★ 境界の変換 1 本だけ。B-rep 演算は occt.so が持つ (このモジュールは繋ぐだけ)。
	 *   ★ **codec ではなく普通の op** — 両側を知っているので deflection を素直に引数で取れる
	 *     (openvdb_mf の voxelize/isosurface が dx/iso を取るのと同じ)。
	 *   ★ 出力型は mf-mesh3d = **本物の mfMesh**。名前だけ借りた別クラスではない。 */
	{ "triangulate", TRI_IN, 2, AK_CACHE, OPWIRE(ocmTriangulate, ocGeom), 0, "(" OC_TYPE ")->mf-mesh3d" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocmtsAgent_(
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


ocmtsAgent_::ocmtsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	ocmtsAgent_::agent_ops()   { return OPS; }
int			ocmtsAgent_::agent_n_ops() { return N_OPS; }
const char*		ocmtsAgent_::agent_name()  { return "occt_mf"; }


static sPtr<ptsAgent>
mk_ocmtsAgent(sPtr<ptsObject> parent)
{
	return sPtr<ptsAgent>::d_cast(thNEW(ocmtsAgent,(parent)));
}

extern const pigModuleType occt_mf_provides[];
extern const srava_module_descriptor ocmtsAgent_descriptor;
extern const srava_module_descriptor ocmtsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "occt_mf",
	/* ★ 変換専用なので既定カーネルにはならない (leaf 生成 op を持たない)。
	 *   梯子 (2026-08-25): cgal 20 > manifold 10 > geogram 6 > nef 5 > pipe_proximity 4 >
	 *   occt 2 > openvdb 1 > openvdb_mf 0 (テスト専用は負値)。境界モジュールは 0 で揃える。
	 *   ★同点の勝敗は不定だが、openvdb_mf とは op 名も入力型も重ならないので衝突しない。 */
	.priority      = 0,
	.make_agent    = &mk_ocmtsAgent,
	/* ★ occt.so と揃えて PROCESS のみ。OCCT はプロセス全体の診断出力設定
	 *   (ocShape::ensure_init) を触るので、in-proc の安全性は occt.so 側と同時に検討する。 */
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = "",   /* なし (ファイルの入口は occt.so が持つ) */
	.export_exts   = "",   /* なし (出力はメッシュなので下流のメッシュ系が書く) */
	/* ★ process 実行では agent プロセスに occt_mf.so しか load されないので、
	 *   入力 (BREP) と出力 (MFM3) の **両方の codec を自分で申告する**。
	 *   実体はどちらも相手側の本物のクラス (ocmCacheCodec.cpp 参照)。 */
	.provides      = occt_mf_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	/* ★ **両側の型を申告する**。新しい型は作っていない — 実体は libsrava_oc / libsrava_mf の
	 *   本物のクラス (ocShape / mfMesh) なので、in-proc でも d_cast が通る。 */
	.hash_salt     = "\x01" "OCM",   /* キャッシュキー弁別 */
	.initialize    = 0,   /* 無し (ocShape::ensure_init は op の入口で呼ぶ) */
	.configure     = 0,   /* module() の opts は消費しない */
};

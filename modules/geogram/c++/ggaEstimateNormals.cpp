/*
 * ggaEstimateNormals — estimate_normals(p [, k]) の計算本体 (geogram 版・#3528)。
 *   cgal 版 (cgaEstimateNormals) と **同じ op 名・同じ sig** で、中身が違う。
 *
 * ★★ なぜ 2 つあってよいか (ひさ判断 2026-09-13): estimate_normals は利用者から見れば
 *   **1 つの op** で、実装がモジュールごとにあるのは srava では普通の形 (nverts は 8 モジュール、
 *   import / export / hull / box も複数が名乗っている)。同じ入力型を 2 つが主張する例も既にあり
 *   (minkowski = manifold と nef 系)、両方ロードしていれば priority で決まり、
 *   **`"geogram"::estimate_normals(p)` で名指しできる** (#3467)。
 *   ⇒ cgal (GPL) を入れない構成でも法線推定ができる、という穴が閉じる。
 *   ★ キャッシュキーのソルトに **モジュール名 + cache_version** が入るので、2 つの実装の結果が
 *     同じキーに混ざることはない。
 *
 * 中身: Co3Ne_compute_normals(M, k, reorient=true)。
 *   ・法線は kNN の最小二乗平面から (normalize 済み・co3ne.cpp:2443)
 *   ・reorient=true で kNN グラフ上を BFS して向きを揃える (co3ne.cpp:2094)
 *   ・法線は頂点属性 "normal" (vector attribute dim=3) に入る。これは Co3Ne_reconstruct が
 *     読む名前と同じで、形式 (xyz の 6 列) → ライブラリ → 再構成が一本に繋がっている。
 *
 * ⚠⚠ cgal 版との**約束の差** (利用者に見える差ではないが、印の根拠の強さが違う):
 *   CGAL の mst_orient_normals は **向き付けできなかった点を返す**ので「全点を向き付けられた」
 *   ことを確かめてから印を立てられる。Co3Ne の reorient_normals は **取り消し以外では常に true**
 *   を返し、連結成分ごとに BFS するだけで「成分間の相対符号」は報告しない。
 *   ⇒ ここでは戻り値をそのまま印にしている。⚠ kNN グラフが複数成分に割れる点群 (離れた塊が
 *     複数ある / 点が疎すぎる) では、**成分ごとの符号が揃わないまま印が立ちうる**。
 *     揃っていることを確かめたいなら cgal 版を名指すか、球のような閉形式で検定する。
 *
 * ★ 点群型 (pt-cloud3d) は **中立の libsrava_pt** が持つ。geogram はそれを借りるだけで、
 *   自分のクラスも wire 形式も作らない (occt_mf が mfGeom を借りるのと同じ作法)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gg/c++/ggMesh.h"        /* gga_err / ggMesh::co3ne_normals (#3535①) */
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
/* ★★ #3535①: **このファイルは GEO:: を 1 つも呼ばない**。
 *   ⚠ 「geogram のヘッダを include しない」ではない — ggMesh.h は @c GEO::Mesh を値で持つので
 *     ヘッダは推移的に入る。守っているのは *シンボルを参照しないこと* の方で、参照しなければ
 *     静的アーカイブからその実体が geogram.so へ引き込まれない (アーカイブは参照された
 *     オブジェクトだけを引く)。
 *   ⇒ GEO:: に触るのは libsrava_gg 側 (ggMesh::co3ne_normals) だけ、という規約。破ると
 *     geogram.so 側に GEO:: の 2 つ目のコピーができ、初期化したコピーと使うコピーが食い違って
 *     SIGSEGV する (2026-09-14・Linux 実測)。根拠は ggMesh.h の宣言のところに書いた。
 *   ★ CMake で geogram.so から geogram アーカイブを外してあるので、破ると **リンクで止まる**。 */
#include	"_ts2/c++/ggaEstimateNormals_.h"
#include	<vector>
#include	<exception>
#include	<stdio.h>

CLASS_TINYSTATE(gg/c++/ggaEstimateNormals,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggaEstimateNormals_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ptCloud>	cloud;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class ptsObject;
class pigData;
class stdString;
class ptCloud;
TS_END_INTERFACE

#endif


ggaEstimateNormals_::ggaEstimateNormals_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 既定近傍数。⚠ **cgal 版と同じ 18 に揃えてある** — 同じ op 名なので、名指しを変えただけで
 * 近傍数まで変わるのは分かりにくい (geogram の vorpalite 既定は 30)。 */
#define GGA_EN_DEFAULT_K	18

void
ggaEstimateNormals_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	if ( ! in.is_notNull() ) {
		result = gga_err(thNEW(stdString,("estimate_normals: needs a point cloud")));
		return;
	}
	if ( in->dim() != 3 ) {
		result = gga_err(thNEW(stdString,(
		    "estimate_normals: only 3D point clouds have normals (pt-cloud3d)")));
		return;
	}
	int np = in->np();
	if ( np < 3 ) {
		result = gga_err(thNEW(stdString,(
		    "estimate_normals: needs at least 3 points to fit a tangent plane")));
		return;
	}
	int k = ( na > 1 ) ? (int)(*args)[1]->get_int() : GGA_EN_DEFAULT_K;
	if ( k < 1 )      k = 1;
	if ( k > np - 1 ) k = np - 1;   /* 近傍数は自分を除く点数まで */

	/* ★★ #3535①: geogram に触るのは **この 1 行だけ**。中身 (GEO::initialize / CmdLine の
	 *   arg group / Co3Ne_compute_normals / 点数の照合) は libsrava_gg 側へ移した。
	 *   ⚠ 2026-09-14 まではここで `GEO::initialize()` を**もう一度**呼んでいた。geogram が
	 *     2 つの .so に静的リンクされていて、初期化したコピー (libsrava_gg) と使うコピー
	 *     (geogram.so) が別だったための対症療法 (7a3f853)。
	 *   ⇒ 実体を片方に寄せたので **その 2 度呼びは要らなくなった** (#3535 5 節の ensure_init
	 *     inline 化も不要)。申し送りで守る規約が 1 つ減り、構造が代わりに守る。 */
	char eb[256];
	eb[0] = '\0';
	sPtr<ptCloud> out = thNEW(ptCloud,());
	out->set_dim(3);
	int oriented = ggMesh::co3ne_normals(&in->xyz()[0], np, k, out->xyz(), out->nrm(),
	                                     eb, (int)sizeof eb);
	if ( oriented < 0 ) {
		char b[320];
		::snprintf(b, sizeof b, "estimate_normals: %s", eb);
		result = gga_err(thNEW(stdString,(b)));
		return;
	}
	out->set_oriented(oriented);
	cloud = out;
}

/* この演算の結果。エラー時は result 優先。保存は agent が出力 pigDataCache 経由で行う。 */
sPtr<pigData>
ggaEstimateNormals_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}

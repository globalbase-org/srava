/*
 * nfcBridge — ★★ #3545 段 4: nef ⇄ cgal の変換の **実体**。
 *
 * ⚠⚠ **CGAL を読んでよいのはこの TU だけ** (橋の中で)。宣言は nfc/c++/nfcBridge.h に在り、
 *   そちらは CGAL-free なので、橋の op TU (nfcCast.cpp) は CGAL を 1 枚も引かない。
 * ★★ #3559: この TU は **libsrava_cg** の一部になった (旧 libsrava_nfcg)。置き場所が変わった
 *   理由は nfcBridge.h の「置き場所」を見よ。中身 (素通しの代入) は変わっていない。
 *
 * ★ 変換そのものは **無損失**。nfNefBox::Mesh も cgal 側の Mesh も
 *   @CGAL::Surface_mesh<EPECK::Point_3>@ = **同じ型**なので、境界を取り出してそのまま渡せる。
 *   ⚠ どちらの幾何クラスの実体も **libsrava_cg に 1 つだけ**在る (#3559)。
 *
 * ライセンス: どちらも CGAL (GPL)。この橋で新たに混ざるものは無い (nef_cg.so と同じ立場)。
 */
#include	"nfc/c++/nfcBridge.h"
#include	"nf/c++/nfMeshCgal.h"
#include	"cg/c++/cgMeshCgal.h"

int
nfcg_nef_to_cgmesh(sPtr<nfMeshSnc> in, sPtr<cgMesh3D> *out)
{
	nfNefBox::Mesh bnd;
	if ( ! nf_to_mesh(in, bnd) )
		return 0;                       /* 境界表現が無い (非有界など) */
	sPtr<cgMesh3D> m = thNEW(cgMesh3D,());
	cg_mesh(m) = bnd;                       /* ★ 同じ EPECK Surface_mesh。厳密なまま素通し */
	*out = m;
	return 1;
}

#ifndef ___nfTriSink_H___
#define ___nfTriSink_H___
/*
 * nfTriSink — common/geodesic.h / common/solids.h の **Sink 契約**を nef の境界メッシュへ
 * 流す接続子 (#3474)。nfaSphere が持っていた GeoSink を生成 op 共通に出したもの。
 *
 * ★★ #3545: **素の配列で受けるようにした** (CGAL-free)。
 *   ⚠⚠ 以前は @nfNefMesh::Mesh@ (= CGAL の Surface_mesh) を直に持っていたので、
 *     このヘッダを読む **9 本の op TU** が CGAL を引き込んでいた。
 *     CGAL は **カーネルのヘッダ**を 1 枚 include するだけで可変大域を .o に emit するので
 *     (実測 2026-09-16)、-fvisibility=hidden と相まってモジュールごとの別コピーになる。
 *   ★ 教訓: 「op が CGAL 型を使っているか」を **字面で数えても足りない**。
 *     この 9 本はどれも @nfNefMesh::Mesh@ と書いていないが、**推移的に**引いていた。
 *     ⇒ 数えるなら「その TU の .o に emit されるか」で数える (それが唯一の実体)。
 */
#include	"nf/c++/nfMesh.h"
#include	<vector>

struct nfTriSink {
	std::vector<double> xyz;   /* 頂点 (3 成分ずつ) */
	std::vector<int>    tri;   /* 三角形 (3 索引ずつ) */

	int  add_vertex(double x, double y, double z) {
		xyz.push_back(x); xyz.push_back(y); xyz.push_back(z);
		return (int)(xyz.size() / 3) - 1;
	}
	void add_triangle(int a, int b, int c) {
		tri.push_back(a); tri.push_back(b); tri.push_back(c);
	}
	/* ★★ #3559: **自分の module の変種で作る** (@NF_MESH@ = nef_snc なら nfMeshSnc /
	 *   nef_hybrid なら nfMesh)。⚠ ここに具体クラス名を直書きすると、*もう一方の変種の値*
	 *   を作っても **コンパイルは通る** — 型名が変わるので下流の materialize で初めて落ちる。
	 *   (2026-09-19 に実際にそうなった: nef_snc の prism / tube / icosphere 等 9 本が
	 *    nfb-mesh3d を作り、area がエラー値 'TEXT' を受け取っていた。) */
	sPtr<nfNefMesh> finish() {
		sPtr<nfNefMesh> o = thNEW(NF_MESH,());
		o->build_from_triangles(xyz.empty() ? 0 : &xyz[0], (int)(xyz.size() / 3),
		                        tri.empty() ? 0 : &tri[0], (int)(tri.size() / 3));
		return o;
	}
};

#endif

#ifndef ___cgTriSink_H___
#define ___cgTriSink_H___
/*
 * cgTriSink — common/geodesic.h / common/solids.h の **Sink 契約**を cgMesh3D へ流す接続子 (#3474)。
 * ★ 共通生成器と同じ順で add_vertex が呼ばれるので、頂点 index は生成器側の index と一致する。
 *
 * ★★ #3545 段 2: **素の配列で受けるようにした** (CGAL-free)。
 *   ⚠⚠ 以前は @cgMesh::Mesh@ (= CGAL の Surface_mesh) を直に持っていたので、このヘッダを
 *     読む op TU がすべて CGAL を引き込んでいた。CGAL は **カーネルのヘッダ**を 1 枚 include するだけで
 *     可変大域を .o に emit するので (実測 2026-09-16)、@-fvisibility=hidden@ と相まって
 *     モジュールごとの別コピーになる。
 *   ★ 実体化は @cgMesh3D::build_from_triangles@ (libsrava_cg) に閉じる。
 */
#include	"cg/c++/cgMesh.h"
#include	<vector>

struct cgTriSink {
	std::vector<double> xyz;   /* 頂点 (3 成分ずつ) */
	std::vector<int>    tri;   /* 三角形 (3 索引ずつ) */

	int  add_vertex(double x, double y, double z) {
		xyz.push_back(x); xyz.push_back(y); xyz.push_back(z);
		return (int)(xyz.size() / 3) - 1;
	}
	void add_triangle(int a, int b, int c) {
		tri.push_back(a); tri.push_back(b); tri.push_back(c);
	}
	/* 積んだものを mesh へ流す。⚠ 生成器を回し終えてから 1 度だけ呼ぶこと。 */
	void flush_to(sPtr<cgMesh3D> m) {
		m->build_from_triangles(xyz.empty() ? 0 : &xyz[0], (int)(xyz.size() / 3),
		                        tri.empty() ? 0 : &tri[0], (int)(tri.size() / 3));
	}
};

#endif

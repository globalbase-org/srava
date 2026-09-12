#ifndef ___cgTriSink_H___
#define ___cgTriSink_H___
/*
 * cgTriSink — common/geodesic.h / common/solids.h の **Sink 契約**を CGAL Surface_mesh へ
 * 流す接続子 (#3474)。共通生成器がカーネル非依存なぶん、受け側の 1 枚をここに持つ。
 * ★ 共通生成器と同じ順で add_vertex が呼ばれるので、頂点 index は生成器側の index と一致する。
 */
#include	"cg/c++/cgMesh.h"
#include	<vector>

struct cgTriSink {
	cgMesh::Mesh&                            m;
	std::vector<cgMesh::Mesh::Vertex_index>  vs;
	cgTriSink(cgMesh::Mesh& mm) : m(mm) {}
	int  add_vertex(double x, double y, double z) {
		vs.push_back(m.add_vertex(cgMesh::Point_3(x, y, z)));
		return (int)vs.size() - 1;
	}
	void add_triangle(int a, int b, int c) { m.add_face(vs[a], vs[b], vs[c]); }
};

#endif

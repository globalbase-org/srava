#ifndef ___nfTriSink_H___
#define ___nfTriSink_H___
/*
 * nfTriSink — common/geodesic.h / common/solids.h の **Sink 契約**を nef の境界メッシュへ
 * 流す接続子 (#3474)。nfaSphere が持っていた GeoSink を生成 op 共通に出したもの。
 * ★ finish() で Nef 表現へ載せる (set_from_mesh)。
 */
#include	"nf/c++/nfMesh.h"
#include	<vector>

struct nfTriSink {
	nfMesh::Mesh                             m;
	std::vector<nfMesh::Mesh::Vertex_index>  vs;
	int  add_vertex(double x, double y, double z) {
		vs.push_back(m.add_vertex(nfMesh::Point_3(x, y, z)));
		return (int)vs.size() - 1;
	}
	void add_triangle(int a, int b, int c) { m.add_face(vs[a], vs[b], vs[c]); }
	sPtr<nfMesh> finish() {
		sPtr<nfMesh> o = thNEW(nfMesh,());
		o->set_from_mesh(m);
		return o;
	}
};

#endif

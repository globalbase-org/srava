#ifndef ___mfTriSink_H___
#define ___mfTriSink_H___
/*
 * mfTriSink — common/geodesic.h / common/solids.h の **Sink 契約**を Manifold の MeshGL64 へ
 * 流す接続子 (#3474)。mfMesh.cpp の MfGeoSink (sphere 専用・匿名 namespace) と同じ形を
 * 生成 op から使えるように出したもの。
 */
#include	"mf/c++/mfMesh.h"
#include	<stdint.h>

struct mfTriSink {
	manifold::MeshGL64	m;
	mfTriSink() { m.numProp = 3; }
	int add_vertex(double x, double y, double z) {
		int id = (int)(m.vertProperties.size() / 3);
		m.vertProperties.push_back(x);
		m.vertProperties.push_back(y);
		m.vertProperties.push_back(z);
		return id;
	}
	void add_triangle(int a, int b, int c) {
		m.triVerts.push_back((uint64_t)a);
		m.triVerts.push_back((uint64_t)b);
		m.triVerts.push_back((uint64_t)c);
	}
	sPtr<mfMesh> finish() const { return thNEW(mfMesh,(manifold::Manifold(m))); }
};

#endif

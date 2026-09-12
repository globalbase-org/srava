#ifndef ___ggTriSink_H___
#define ___ggTriSink_H___
/*
 * ggTriSink — common/geodesic.h / common/solids.h の **Sink 契約**を ggMesh へ流す接続子
 * (#3474)。ggMesh が add_vertex / add_triangle をそのまま持っているので薄い。
 */
#include	"gg/c++/ggMesh.h"

struct ggTriSink {
	sPtr<ggMesh>	m;
	ggTriSink() { m = thNEW(ggMesh,()); }
	int  add_vertex(double x, double y, double z) { return m->add_vertex(x, y, z); }
	void add_triangle(int a, int b, int c)        { m->add_triangle(a, b, c); }
	sPtr<ggMesh> finish() const { return m; }
};

#endif

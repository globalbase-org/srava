#ifndef ___chTriSink_H___
#define ___chTriSink_H___
/*
 * chTriSink — common/geodesic.h / common/solids.h の **Sink 契約**を chMesh へ流す接続子
 * (#3474)。chMesh が add_vertex / add_triangle をそのまま持っているので薄い
 * (ggTriSink と同じ形)。
 */
#include	"ch/c++/chMesh.h"

struct chTriSink {
	sPtr<chMesh>	m;
	chTriSink() { m = thNEW(chMesh,()); }
	int  add_vertex(double x, double y, double z) { return m->add_vertex(x, y, z); }
	void add_triangle(int a, int b, int c)        { m->add_triangle(a, b, c); }
	sPtr<chMesh> finish() const { return m; }
};

#endif

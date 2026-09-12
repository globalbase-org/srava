#ifndef ___vdTriSink_H___
#define ___vdTriSink_H___
/*
 * vdTriSink — common/solids.h の **Sink 契約**を OpenVDB の level set へ流す接続子 (#3474)。
 * ★ OpenVDB は角錐・円柱・トーラスの生成器を持たないので、vdGrid::make_box と同じく
 *   三角形を組んで meshToLevelSet へ渡す。**メッシュカーネルには依存しない**
 *   (頂点をこの場で作るだけ)。
 * ⚠ finish() は dx (ボクセルサイズ) を取る。ボクセル表現では精度は分割数ではなく dx で決まる。
 */
#include	"vd/c++/vdGrid.h"
#include	<openvdb/tools/MeshToVolume.h>
#include	<vector>
#include	<stdint.h>

struct vdTriSink {
	std::vector<openvdb::Vec3s>	pts;
	std::vector<openvdb::Vec3I>	tri;
	int  add_vertex(double x, double y, double z) {
		pts.push_back(openvdb::Vec3s((float)x, (float)y, (float)z));
		return (int)pts.size() - 1;
	}
	void add_triangle(int a, int b, int c) {
		tri.push_back(openvdb::Vec3I((uint32_t)a, (uint32_t)b, (uint32_t)c));
	}
	sPtr<vdGrid> finish(double dx) const {
		openvdb::math::Transform::Ptr xform =
		    openvdb::math::Transform::createLinearTransform(dx);
		openvdb::FloatGrid::Ptr g =
		    openvdb::tools::meshToLevelSet<openvdb::FloatGrid>(*xform, pts, tri);
		if ( ! g ) return sPtr<vdGrid>();
		sPtr<vdGrid> o = thNEW(vdGrid,());
		o->set_grid(g);
		return o;
	}
};

#endif

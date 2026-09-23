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
#include	<vector>
#include	<stdint.h>

/* ★★ #3545 段 5 (2026-09-18): **素の配列で受ける**形に作り替えた。
 *   ⚠ 以前は @openvdb::Vec3s@ / @Vec3I@ を直に持ち @meshToLevelSet@ をここで呼んでいたので、
 *     このヘッダを include する **9 本の op TU** が上流を引いていた
 *     (nef が踏んだ「⑥ 推移的な取り込み」と同じ形 — 字面で数えると見落とす)。
 *   ⇒ 実体は @vdGrid::from_triangles@ (幾何 lib 側)。ここは並べるだけ。
 *   ⚠ 精度は変わらない — 以前も内部で float に落としていた (Vec3s)。 */
struct vdTriSink {
	std::vector<double>	pts;   /* 3*nv */
	std::vector<uint32_t>	tri;   /* 3*nt */
	int  add_vertex(double x, double y, double z) {
		pts.push_back(x); pts.push_back(y); pts.push_back(z);
		return (int)(pts.size() / 3) - 1;
	}
	void add_triangle(int a, int b, int c) {
		tri.push_back((uint32_t)a); tri.push_back((uint32_t)b); tri.push_back((uint32_t)c);
	}
	sPtr<vdGrid> finish(double dx) const {
		return vdGrid::from_triangles(pts.empty() ? 0 : &pts[0], (int)(pts.size() / 3),
		                              tri.empty() ? 0 : &tri[0], (int)(tri.size() / 3), dx);
	}
};

#endif

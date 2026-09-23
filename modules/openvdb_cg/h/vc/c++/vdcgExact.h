#ifndef ___vdcgExact_h___
#define ___vdcgExact_h___
/*
 * vdcgExact.h — ★★ #3545 段 4: **export_vox の厳密幾何の宣言** (CGAL-free)。
 *
 * ⚠⚠ ここには CGAL の型が **1 つも出てきません**。実体は libsrava_vdcg (vdcgExact.cpp) に在り、
 *   そこだけが cg/c++/cgMeshCgal.h を読みます。
 *
 * ---- なぜ橋が自分の .so を持つのか ----
 * この厳密計算 (SoS 述語つきの z-パリティ voxelize) は **橋固有のロジック**で、
 * ひさ判断 (2026-09-15) により **cgal の幾何ライブラリには入れない**
 *   「openvdb とも共有しているわけですしね。やめておきましょう」
 * ⇒ libsrava_cg にも libsrava_vd にも置けない ⇒ **橋が自分の共有ライブラリを持つ**。
 *   ★ 置き場所は cgal の lib ではなく *橋自身の lib* なので、上のひさ判断と衝突しない。
 *
 * ★ これで op TU (vcaExportVox.cpp) が CGAL を 1 枚も引かなくなる。段 1/2 と同じ形で、
 *   @u@ (STB_GNU_UNIQUE) にも dllexport にも依存しない。
 */
#include	"pig/c++/pigData.h"
#include	<vector>
#include	<stdint.h>

class cgMesh3D;

/* 領域ごとの厳密三角リスト。**不透明** (中身は EPECK の有理数)。 */
class vdcgTris;

vdcgTris* vdcg_tris_new(int nregion);
void      vdcg_tris_free(vdcgTris *t);

/* cgMesh3D の面を region の三角リストへ積み、world の外接箱 lo/hi を更新する。
 * ⚠ 非三角面は扇分割。座標は **厳密なまま** 持ち、lo/hi だけ double に落とす。 */
void      vdcg_tris_add_mesh(vdcgTris *t, int region, sPtr<cgMesh3D> m,
                             double lo[3], double hi[3]);

/* 三角リストを格子へ厳密 z-パリティでボクセル化 → inside[Nx*Ny*Nz]
 * (C-order: ((ix*Ny)+iy)*Nz+iz)。返り = 奇数パリティになった列数
 * (閉メッシュ入力なら 0 のはず。非閉入力では起こり得る)。 */
long      vdcg_voxelize(const vdcgTris *t, int region, const double org[3], double dx,
                        int Nx, int Ny, int Nz, std::vector<uint8_t> &inside);

/* ★ 早期 return の多い呼び手 (op の compute) のための RAII。⚠ CGAL-free のまま。 */
struct vdcgTrisHandle {
	vdcgTris *p;
	vdcgTrisHandle(int n) : p(vdcg_tris_new(n)) {}
	~vdcgTrisHandle() { vdcg_tris_free(p); }
private:
	vdcgTrisHandle(const vdcgTrisHandle &);
	void operator=(const vdcgTrisHandle &);
};

#endif

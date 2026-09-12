#ifndef VD_MESH_VOXELIZE_H
#define VD_MESH_VOXELIZE_H
/*
 * vdMeshVoxelize.h — 三角形メッシュ → narrow-band level set (#3491)。
 *                    voxelize / renormalize の **共通の入口** (ヘッダオンリー)。
 *
 * ---- なぜ tools::meshToLevelSet を直接呼ばないのか (#3491) --------------------
 *
 * ★★ @c meshToVolume は **内部空洞を埋める**。中空の殻を voxelize すると「詰まった球」になる。
 *
 *   OpenVDB は符号を「グリッド外周から到達できるか」で決める (@c traceExteriorBoundaries)。
 *   これは *意図された設計*で、ヘッダにも "is independent of mesh surface normals" と書いてある
 *   (向きの壊れたメッシュでも動く、という利点と引き換え)。しかし **閉じた空洞は外部から
 *   到達できない**ので、空洞は内側と塗られて材料に化ける。
 *
 *   実測 (中空の殻 sphere(1.5)---sphere(1.0)・真値 V=9.948377):
 *
 *       meshToLevelSet                 dx=0.05 14.127817 / 0.02 14.135968 / 0.01 14.137142
 *       interiorTest を渡すと (本実装)  dx=0.05  9.954777 / 0.02  9.949992 / 0.01  9.949430
 *
 * ---- 直し方 ------------------------------------------------------------------
 *
 * OpenVDB 11 以降の @c meshToVolume は **interiorTest** (`Coord -> bool`) を受け取り、
 * 到達可能性の代わりにこちらの答えで符号を決める。渡すのは **巻き数 (winding number)**:
 *
 *   列 (ix,iy) を通る +z 方向の直線と三角形の交点を全部集め、各交点に
 *   **法線の z 成分の符号** (= xy へ射影した三角形の向き) を持たせる。
 *   点 p の巻き数 = 「p より上の交点の符号和」。0 でなければ内側。
 *
 *   ★ 符号を持たせるのが要点。偶奇 (パリティ) でも空洞は解けるが、**自己交差した閉曲面**で
 *     意味が変わってしまう (パリティは二重に覆われた領域を外側と見なす)。巻き数なら
 *     「2 回巻いていれば内側」= 従来の到達可能性と同じ意味になるので、
 *     空洞だけを直して他の入力の振る舞いを変えない。
 *   ★ 符号は交点判定で既に計算している 2 次元外積の符号そのものなので **追加コストは無い**。
 *
 * ★ 戦略は @c EVAL_EVERY_TILE。零交差から 0.75 ボクセル以内では oracle を訊かず、面付近の符号は
 *   従来どおり @c ComputeIntersectingVoxelSign が幾何から決める。つまり
 *   **oracle は「面から十分離れた所の内外」しか答えなくてよい** — 退化に神経を使わずに済む。
 *
 * ⚠⚠ **列の位置を微小にずらす** (VD_MTV_EX / EY)。格子に揃った形 (box) では稜がちょうど列の
 *   真上に乗り、「交点 0 個 or 2 個」に化ける。上のとおり oracle は面から 0.75 ボクセル以上
 *   離れた点しか訊かれないので、この程度のずらしで答えが変わることはない。
 *
 * ⚠⚠ **閉じていない / 向きが揃っていない入力では従来経路へ退避する**。
 *   閉じた向きの揃った曲面なら、どの直線でも交点の符号和は 0 になる。1 つでも 0 でない列が
 *   あればその前提が崩れているので、oracle を使わず @c meshToLevelSet をそのまま呼ぶ。
 *   ⇒ 汚い入力の振る舞いは **1 ミリも変わらない** (OpenVDB の向き非依存の強みを残す)。
 *
 * ⚠⚠ @c meshToVolume の **interrupter を取らないオーバーロードは interiorTest を黙って捨てる**
 *   (OpenVDB 12.1.1 の tools/MeshToVolume.h・引数名がコメントアウトされていて転送していない)。
 *   必ず @c util::NullInterrupter を渡す方を呼ぶこと。
 * ⚠⚠ @c QuadAndTriangleDataAdapter へ渡す点は **index 空間**。world 空間のまま渡すと
 *   「1 ボクセルを 1 世界単位」と解釈され、桁違いに小さい形になる (実測 14.14 が 0.0023)。
 */

#include <openvdb/openvdb.h>
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/VolumeToMesh.h>
#include <openvdb/util/NullInterrupter.h>
#include "vd/c++/vdBreak.h"   /* #3498: 中断 */

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

/* 列をずらす量 (ボクセル単位)。格子に揃った形との同時発生を避けるための無理数めいた値。 */
#define VD_MTV_EX	0.0013741
#define VD_MTV_EY	0.0021937

/* ---- 巻き数による内外判定 (index 空間) ----------------------------------------
 * ★ 実体は shared_ptr の先に置く。meshToVolume は oracle を **コピーして**スレッドへ配るので、
 *   実体を値で持つと交点表がスレッド数ぶん複製される。共有 + 読み取り専用ならそれも起きない
 *   ("evaluating different copies has to be thread-safe" という OpenVDB の要求も満たす)。 */
class vdWindingInside {
public:
	vdWindingInside(const std::vector<openvdb::Vec3s>& ip,   /* index 空間の点 */
	                const std::vector<openvdb::Vec3I>& tris)
	    : d_(std::make_shared<Data>(ip, tris)) {}

	/* 閉じた向きの揃った曲面か (符号和が 0 でない列が 1 つも無いか)。 */
	bool consistent() const { return d_->bad == 0; }
	long bad_columns() const { return d_->bad; }

	bool operator()(const openvdb::Coord& c) const
	{
		const Data& d = *d_;
		const int ix = c.x() - d.x0, iy = c.y() - d.y0;
		if ( ix < 0 || iy < 0 || ix >= d.nx || iy >= d.ny ) return false;
		const size_t col = (size_t)ix*d.ny + iy;
		const size_t b = d.off[col], e = d.off[col+1];
		int w = 0;
		for ( size_t k = e ; k > b ; --k ) {
			if ( d.z[k-1].first <= (double)c.z() ) break;   /* z は昇順 */
			w += d.z[k-1].second;
		}
		return w != 0;
	}

private:
	struct Data {
		std::vector<std::pair<double,int> > z;   /* 列ごとに z 昇順。second = 法線 z の符号 */
		std::vector<size_t> off;                 /* CSR の開始位置 (列数 + 1) */
		int  x0 = 0, y0 = 0, nx = 0, ny = 0;
		long bad = 0;

		Data(const std::vector<openvdb::Vec3s>& ip, const std::vector<openvdb::Vec3I>& tris)
		{
			if ( ip.empty() || tris.empty() ) return;
			double mn[2] = { ip[0][0], ip[0][1] }, mx[2] = { mn[0], mn[1] };
			for ( size_t i = 1 ; i < ip.size() ; ++i )
				for ( int k = 0 ; k < 2 ; ++k ) {
					const double v = ip[i][k];
					if ( v < mn[k] ) mn[k] = v;   if ( v > mx[k] ) mx[k] = v;
				}
			x0 = (int)std::floor(mn[0]) - 1;
			y0 = (int)std::floor(mn[1]) - 1;
			nx = (int)std::floor(mx[0]) + 2 - x0;
			ny = (int)std::floor(mx[1]) + 2 - y0;
			if ( nx <= 0 || ny <= 0 ) { nx = ny = 0; return; }
			/* 2 パスで CSR を作る (列ごとの vector を持つとメモリが列数に比例して無駄になる)。 */
			off.assign((size_t)nx*ny + 1, 0);
			std::vector<size_t> fill;
			for ( int pass = 0 ; pass < 2 ; ++pass ) {
				if ( pass == 1 ) {
					size_t acc = 0;
					for ( size_t i = 0 ; i < off.size() ; ++i ) { const size_t c = off[i]; off[i] = acc; acc += c; }
					z.resize(acc);
					fill = off;
				}
				for ( size_t t = 0 ; t < tris.size() ; ++t ) {
					const openvdb::Vec3s &a = ip[tris[t][0]], &b = ip[tris[t][1]], &c = ip[tris[t][2]];
					const double lo0 = std::min((double)a[0], std::min((double)b[0], (double)c[0]));
					const double hi0 = std::max((double)a[0], std::max((double)b[0], (double)c[0]));
					const double lo1 = std::min((double)a[1], std::min((double)b[1], (double)c[1]));
					const double hi1 = std::max((double)a[1], std::max((double)b[1], (double)c[1]));
					int i0 = (int)std::ceil(lo0 - VD_MTV_EX), i1 = (int)std::floor(hi0 - VD_MTV_EX);
					int j0 = (int)std::ceil(lo1 - VD_MTV_EY), j1 = (int)std::floor(hi1 - VD_MTV_EY);
					if ( i0 < x0 ) i0 = x0;   if ( i1 > x0+nx-1 ) i1 = x0+nx-1;
					if ( j0 < y0 ) j0 = y0;   if ( j1 > y0+ny-1 ) j1 = y0+ny-1;
					for ( int ix = i0 ; ix <= i1 ; ++ix )
					for ( int iy = j0 ; iy <= j1 ; ++iy ) {
						double zc; int sgn;
						if ( ! hit(a, b, c, ix + VD_MTV_EX, iy + VD_MTV_EY, zc, sgn) ) continue;
						const size_t col = (size_t)(ix - x0)*ny + (iy - y0);
						if ( pass == 0 ) ++off[col];
						else             z[fill[col]++] = std::make_pair(zc, sgn);
					}
				}
			}
			for ( size_t col = 0 ; col + 1 < off.size() ; ++col ) {
				std::sort(z.begin() + off[col], z.begin() + off[col+1]);
				int net = 0;
				for ( size_t k = off[col] ; k < off[col+1] ; ++k ) net += z[k].second;
				if ( net != 0 ) ++bad;   /* 閉じた向きの揃った曲面なら必ず 0 */
			}
		}

		/* (px,py) を通る +z 方向の直線と三角形の交点。内側なら true・z と法線 z の符号を返す。 */
		static bool hit(const openvdb::Vec3s& a, const openvdb::Vec3s& b, const openvdb::Vec3s& c,
		                double px, double py, double& z, int& sgn)
		{
			const double d = ((double)b[0]-a[0])*((double)c[1]-a[1])
			               - ((double)b[1]-a[1])*((double)c[0]-a[0]);
			if ( d == 0.0 ) return false;              /* xy へ潰れた三角形 */
			sgn = ( d > 0.0 ) ? 1 : -1;                /* = 法線の z 成分の符号 */
			const double u = ((px-a[0])*((double)c[1]-a[1]) - (py-a[1])*((double)c[0]-a[0])) / d;
			const double v = (((double)b[0]-a[0])*(py-a[1]) - ((double)b[1]-a[1])*(px-a[0])) / d;
			if ( u < 0.0 || v < 0.0 || u + v > 1.0 ) return false;
			z = (double)a[2] + u*((double)b[2]-a[2]) + v*((double)c[2]-a[2]);
			return true;
		}
	};
	std::shared_ptr<const Data> d_;
};

/* ---- world 空間の三角形メッシュ → level set --------------------------------
 * points は **world 空間**・halfWidth は voxel 単位。失敗は null。
 * fell_back に 0 でない値が入ったら「閉じた向きの揃った曲面ではないので従来経路で作った」。 */
inline openvdb::FloatGrid::Ptr
vd_mesh_to_levelset(const std::vector<openvdb::Vec3s>& points,
                    const std::vector<openvdb::Vec3I>& tris,
                    const openvdb::math::Transform& xform,
                    float halfWidth,
                    long *fell_back = 0,
                    const pigBreak *brk = 0)
{
	if ( fell_back ) *fell_back = 0;
	if ( points.empty() || tris.empty() ) return openvdb::FloatGrid::Ptr();

	/* ⚠ adapter へ渡す点は index 空間 (ヘッダ冒頭の但し書き)。 */
	std::vector<openvdb::Vec3s> ip(points.size());
	for ( size_t i = 0 ; i < points.size() ; ++i ) {
		const openvdb::Vec3d w =
		    xform.worldToIndex(openvdb::Vec3d(points[i][0], points[i][1], points[i][2]));
		ip[i] = openvdb::Vec3s((float)w[0], (float)w[1], (float)w[2]);
	}

	const vdWindingInside oracle(ip, tris);
	if ( ! oracle.consistent() ) {
		/* 閉じていない / 向きが揃っていない ⇒ 巻き数は意味を持たない。従来どおりに作る。 */
		if ( fell_back ) *fell_back = oracle.bad_columns();
		/* ★ #3498: 退避経路も中断できるようにする (器を取る多重定義がある)。 */
		vdBreakScope fbr(brk);
		return openvdb::tools::meshToLevelSet<openvdb::FloatGrid>(
		    fbr.ref(), xform, points, tris, halfWidth);
	}
	openvdb::tools::QuadAndTriangleDataAdapter<openvdb::Vec3s, openvdb::Vec3I>
	    mesh(&ip[0], ip.size(), &tris[0], tris.size());
	/* ⚠ 変数名に nil を使わないこと — macOS のヘッダがマクロで定義している。
	 * ★ #3498: 中断の器。brk==0 なら素の NullInterrupter と同じ挙動 (vdBreak.h)。
	 *   MeshToVolume.h には中断点が 11 箇所ある = **voxelize は実際に止まる**。 */
	vdBreakScope br(brk);
	return openvdb::tools::meshToVolume<openvdb::FloatGrid>(
	    br.ref(), mesh, xform, halfWidth, halfWidth, /*flags=*/0, /*polygonIndexGrid=*/nullptr,
	    oracle, openvdb::tools::EVAL_EVERY_TILE);
}

#endif /* VD_MESH_VOXELIZE_H */

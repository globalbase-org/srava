/*
 * cgaVoxelize — export_vox(path, params, mesh0, mesh1, …) を Cartesian 格子へボクセル化し、中立な
 *   vox.h5(srava と k-Wave の間の中立フォーマット)を書く計算本体(ptsCalcBody 派生)。
 *   args = [ path(INLINE 文字列), params(INLINE ハッシュ), mesh…(CACHE 可変個) ]。
 *   params = { dx, pad, regions:[{name,side},…] }。regions[i] ↔ mesh[i]。
 * 内外判定は **厳密 z-パリティ法**(各(ix,iy)列で +z レイの三角交差 z を集めソート→ペア区間を内部)。
 *   判定・交点は EPECK の厳密有理数で行い、サンプル点が辺・頂点・鉛直面上に載る縮退は
 *   symbolic perturbation(サンプルを (px+ε, py+ε², z+ε³) に置いた極限の辞書式符号)で一般位置に
 *   帰着する。よって結果はメッシュの「幾何」だけの関数(格納順・面の開始 halfedge に非依存)で、
 *   閉メッシュなら交点は必ず偶数。z 充填は half-open [z_lo, z_hi)(下端含む・上端含まず。x/y も
 *   ε の向きにより同じ half-open 意味論)。サンプル点は**セル中心** org+(i+1/2)·dx(丸い設計値と
 *   タイを起こさない+格子面上の軸に対し鏡像対称)。旧実装(to_double+丸め任せ)の奇数パリティ・ゴミ voxel・
 *   実行毎の非決定はこれで根絶(2026-08-15・ひさ承認の B 案)。
 * 出力は export と同様 **D_REF OUTPUT**(path+size+mtime+content_hash・mesh バイナリは書かない)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"pig/c++/pigDataRef.h"   /* 結果 = D_REF の pigData 表現 */
#include	"ts2/c++/stdString.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/cgaVoxelize_.h"

#include	<CGAL/number_utils.h>     /* to_double */
#include	<hdf5.h>
#include	<vector>
#include	<algorithm>
#include	<cmath>
#include	<string>
#include	<string.h>
#include	<stdio.h>
#include	<stdint.h>
#include	<sys/stat.h>

CLASS_TINYSTATE(cg/c++/cgaVoxelize,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaVoxelize_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

protected:
	virtual void	compute();
	sPtr<stdString>	refPath;
	INTEGER64	refSize;
	INTEGER64	refMtime;
	pHashKeyType	refHash;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class stdString;
class cgMesh;
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


cgaVoxelize_::cgaVoxelize_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
	refSize = 0; refMtime = 0; refHash = 0;
}

/*******************************************
	INSTANCE FUNCTIONS
********************************************/

namespace {

typedef cgMesh3D::Mesh::Point EPoint;
typedef CGAL::Exact_predicates_exact_constructions_kernel::FT EFT;

/* 厳密三角形 + 前計算(2 段構えの速度設計):
 *   - 通常列は「誤差限界つき double」で判定・交点計算(静的フィルタ)。
 *   - 限界に触れた列だけ「列ごと厳密パス」で全部やり直す(正しさは厳密側が常に持つ)。
 * 前計算: 辺係数 f_e(x,y)=a*x+b*y+c(固定向き 0→1→2→0)・平面法線 n。ori(=sign(nz))だけは
 * 三角形ごとに厳密に 1 回確定しておく(鉛直面の除外を丸めに任せない)。 */
struct Tri {
	EFT x[3], y[3], z[3];
	EFT ea[3], eb[3], ec[3];     /* 辺 i: (i)→(i+1) の f 係数(厳密) */
	EFT nx, ny, nz;              /* 平面法線(厳密) */
	int ori;                     /* sign(nz) を厳密に前計算(0=鉛直面: 横断交差なし) */
	double dxc[3], dyc[3], dzc[3];               /* 頂点 double 近似 */
	double da[3], db[3], dc_[3], dnx, dny, dnz;  /* 係数 double 近似 */
	double bb[4];   /* minx,maxx,miny,maxy(to_double・丸め方向は使用側で ±1 列補償) */
};

/* 前計算(mesh_to_tris が頂点を詰めた後に呼ぶ)。 */
static void tri_precompute(Tri& T)
{
	for ( int e = 0 ; e < 3 ; ++e ) {
		int p = e, q = (e+1)%3;
		T.ea[e] = T.y[p] - T.y[q];                       /* a = -(Qy-Py) */
		T.eb[e] = T.x[q] - T.x[p];                       /* b =  (Qx-Px) */
		T.ec[e] = T.x[p]*T.y[q] - T.x[q]*T.y[p];         /* c = PxQy - QxPy */
	}
	T.nx = (T.y[1]-T.y[0])*(T.z[2]-T.z[0]) - (T.z[1]-T.z[0])*(T.y[2]-T.y[0]);
	T.ny = (T.z[1]-T.z[0])*(T.x[2]-T.x[0]) - (T.x[1]-T.x[0])*(T.z[2]-T.z[0]);
	T.nz = (T.x[1]-T.x[0])*(T.y[2]-T.y[0]) - (T.y[1]-T.y[0])*(T.x[2]-T.x[0]);
	T.ori = (int)CGAL::sign(T.nz);
	for ( int k = 0 ; k < 3 ; ++k ) {
		T.dxc[k] = CGAL::to_double(T.x[k]);
		T.dyc[k] = CGAL::to_double(T.y[k]);
		T.dzc[k] = CGAL::to_double(T.z[k]);
	}
	for ( int e = 0 ; e < 3 ; ++e ) {
		T.da[e]  = CGAL::to_double(T.ea[e]);
		T.db[e]  = CGAL::to_double(T.eb[e]);
		T.dc_[e] = CGAL::to_double(T.ec[e]);
	}
	T.dnx = CGAL::to_double(T.nx);
	T.dny = CGAL::to_double(T.ny);
	T.dnz = CGAL::to_double(T.nz);
}

/* cgMesh3D の EPECK Surface_mesh → 厳密三角リスト + double bbox 更新。非三角面は扇分割。
 * (どの扇分割でも平面多角形の被覆は同じ+SoS で内部辺は片側 1 回なのでパリティ不変。) */
static void mesh_to_tris(cgMesh3D::Mesh& M, std::vector<Tri>& tris, double lo[3], double hi[3])
{
	typedef cgMesh3D::Mesh Mesh;
	for ( Mesh::Face_index f : M.faces() ) {
		std::vector<Mesh::Vertex_index> vs;
		Mesh::Halfedge_index h0 = M.halfedge(f), h = h0;
		do { vs.push_back(M.target(h)); h = M.next(h); } while ( h != h0 );
		for ( size_t t = 1 ; t + 1 < vs.size() ; ++t ) {
			Mesh::Vertex_index iv[3] = { vs[0], vs[t], vs[t+1] };
			Tri T;
			double dminx = 1e300, dmaxx = -1e300, dminy = 1e300, dmaxy = -1e300;
			for ( int k = 0 ; k < 3 ; ++k ) {
				const Mesh::Point& P = M.point(iv[k]);
				T.x[k] = P.x(); T.y[k] = P.y(); T.z[k] = P.z();
				double dc[3] = { CGAL::to_double(P.x()), CGAL::to_double(P.y()),
				                 CGAL::to_double(P.z()) };
				for ( int a = 0 ; a < 3 ; ++a ) {
					if ( dc[a] < lo[a] ) lo[a] = dc[a];
					if ( dc[a] > hi[a] ) hi[a] = dc[a];
				}
				if ( dc[0] < dminx ) dminx = dc[0];
				if ( dc[0] > dmaxx ) dmaxx = dc[0];
				if ( dc[1] < dminy ) dminy = dc[1];
				if ( dc[1] > dmaxy ) dmaxy = dc[1];
			}
			T.bb[0] = dminx; T.bb[1] = dmaxx; T.bb[2] = dminy; T.bb[3] = dmaxy;
			tri_precompute(T);
			tris.push_back(T);
		}
	}
}

/* 有向辺 (P→Q) に対する点 (px,py) の SoS 符号(+1: 左 / -1: 右。0 は返らない=P==Q の退化のみ)。
 * f(x,y) = (Qx-Px)*(y-Py) - (Qy-Py)*(x-Px) の (px+ε, py+ε²) での辞書式符号:
 *   ① f(px,py) ② ∂f/∂x = -(Qy-Py) ③ ∂f/∂y = (Qx-Px)。全て厳密比較で epsilon 数値なし。 */
static inline int sos_side(const EFT& Px, const EFT& Py, const EFT& Qx, const EFT& Qy,
                           const EFT& px, const EFT& py)
{
	int s = (int)CGAL::sign( (Qx-Px)*(py-Py) - (Qy-Py)*(px-Px) );
	if ( s ) return s;
	s = (int)CGAL::sign( Py - Qy );
	if ( s ) return s;
	return (int)CGAL::sign( Qx - Px );
}

/* 三角リストを格子へ厳密 z-パリティでボクセル化 → inside[Nx*Ny*Nz](C-order: ((ix*Ny)+iy)*Nz+iz)。
 * 返り値 = 奇数パリティになった列数(閉メッシュ入力なら 0 のはず。非閉入力では起こり得る)。 */
static long voxelize_tris(const std::vector<Tri>& tris, const double org[3], double dx,
                          int Nx, int Ny, int Nz, std::vector<uint8_t>& inside)
{
	/* サンプル座標を厳密値で前計算(org+i*dx を FT で。double の org,dx は 2 進有理数なので厳密)。 */
	std::vector<EFT> PX((size_t)Nx), PY((size_t)Ny), PZ((size_t)Nz);
	{
		/* ★セル中心サンプル (org + (i+1/2)·dx)。丸い設計値(格子整数倍の面)とタイを起こさず、
		 * 格子面上に軸を持つ対称形状で鏡像対称が保たれる(2026-08-15 sim 検出の非対称の根治・
		 * ひさ承認)。タイが万一 (k+1/2)·dx で起きても SoS half-open で決定的なのは不変。 */
		EFT o0(org[0]), o1(org[1]), o2(org[2]), fdx(dx);
		EFT half = fdx / EFT(2);
		for ( int i = 0 ; i < Nx ; ++i ) PX[i] = o0 + EFT(i)*fdx + half;
		for ( int i = 0 ; i < Ny ; ++i ) PY[i] = o1 + EFT(i)*fdx + half;
		for ( int i = 0 ; i < Nz ; ++i ) PZ[i] = o2 + EFT(i)*fdx + half;
	}
	/* pass1: 三角形 → 候補列ビニング(double bbox を ±1 列補償)。 */
	std::vector<std::vector<int32_t> > bins((size_t)Nx*Ny);
	for ( size_t ti = 0 ; ti < tris.size() ; ++ti ) {
		const Tri& T = tris[ti];
		int ix0 = (int)std::ceil ((T.bb[0]-org[0])/dx - 0.5) - 1; if ( ix0 < 0 ) ix0 = 0;
		int ix1 = (int)std::floor((T.bb[1]-org[0])/dx - 0.5) + 1; if ( ix1 > Nx-1 ) ix1 = Nx-1;
		int iy0 = (int)std::ceil ((T.bb[2]-org[1])/dx - 0.5) - 1; if ( iy0 < 0 ) iy0 = 0;
		int iy1 = (int)std::floor((T.bb[3]-org[1])/dx - 0.5) + 1; if ( iy1 > Ny-1 ) iy1 = Ny-1;
		for ( int iy = iy0 ; iy <= iy1 ; ++iy )
			for ( int ix = ix0 ; ix <= ix1 ; ++ix )
				bins[(size_t)iy*Nx + ix].push_back((int32_t)ti);
	}
	/* z 一定(格子面に乗りがちなクリップ面等)の三角形は、交点 z = その定数なので
	 * 「最初に z ≥ zc となる平面番号 kGE」を三角形ごとに 1 回だけ厳密に前計算しておく。
	 * これで格子面ちょうどのタイが列ループ内で厳密演算ゼロで解決する(速度の要)。 */
	std::vector<int32_t> triKGE(tris.size(), -1);
	for ( size_t ti = 0 ; ti < tris.size() ; ++ti ) {
		const Tri& T = tris[ti];
		if ( T.ori == 0 ) continue;
		if ( T.z[0] == T.z[1] && T.z[1] == T.z[2] ) {
			int k = (int)std::ceil((T.dzc[0]-org[2])/dx - 0.5);
			if ( k < 0 ) k = 0;
			if ( k > Nz ) k = Nz;
			while ( k > 0 && !(PZ[k-1] < T.z[0]) ) --k;
			while ( k < Nz && PZ[k] < T.z[0] ) ++k;
			triKGE[ti] = k;
		}
	}
	/* pass2: 通常列は誤差限界つき double(静的フィルタ)。曖昧さが出た列だけ厳密で解き直す。
	 * double 係数は「厳密値を最後に 1 回丸めたもの」なので相対誤差 ≤ 1ulp。誤差限界には
	 * 演算誤差(大きさ和 × FILT)に加えサンプル座標 pxd/pyd 自体の丸め(絶対項)も織り込む。 */
	const double FILT = 1e-14;
	inside.assign((size_t)Nx*Ny*Nz, 0);
	long oddCols = 0;
	std::vector<EFT> zs;                       /* 厳密列パス用 */
	struct Cross { double z, err; int32_t kge, tri; };
	std::vector<Cross> zd;                     /* fast パス用 */
	for ( int iy = 0 ; iy < Ny ; ++iy )
	for ( int ix = 0 ; ix < Nx ; ++ix ) {
		std::vector<int32_t>& bin = bins[(size_t)iy*Nx + ix];
		if ( bin.empty() ) continue;
		uint8_t *col = &inside[((size_t)ix*Ny + iy)*Nz];
		const double pxd = org[0] + (ix+0.5)*dx, pyd = org[1] + (iy+0.5)*dx;
		bool exact = false;
		/* ---- fast パス ---- */
		zd.clear();
		for ( size_t bi = 0 ; bi < bin.size() && !exact ; ++bi ) {
			const Tri& T = tris[bin[bi]];
			if ( T.ori == 0 ) continue;        /* 鉛直面(厳密判定済み)=横断交差なし */
			int inside3 = 1;
			for ( int e = 0 ; e < 3 && inside3 ; ++e ) {
				double fd  = T.da[e]*pxd + T.db[e]*pyd + T.dc_[e];
				double mag = std::fabs(T.da[e]*pxd) + std::fabs(T.db[e]*pyd) + std::fabs(T.dc_[e]);
				if ( std::fabs(fd) <= mag*FILT ) { exact = true; break; }   /* タイ疑い → 厳密列 */
				if ( ( fd > 0 ? 1 : -1 ) != T.ori ) inside3 = 0;
			}
			if ( exact || !inside3 ) continue;
			Cross c; c.tri = bin[bi]; c.kge = triKGE[bin[bi]];
			if ( c.kge >= 0 ) {                /* z 一定面: 厳密 z 既知・誤差ゼロ */
				c.z = T.dzc[0]; c.err = 0.0;
			} else {
				double ddx = pxd - T.dxc[0], ddy = pyd - T.dyc[0];
				double num = T.dnx*ddx + T.dny*ddy;
				double az  = std::fabs(T.dnz);
				c.z   = T.dzc[0] - num/T.dnz;
				/* 演算誤差(相対)+ pxd/pyd の丸め(絶対・|ddx| に比例しない)を両方覆う */
				c.err = ( (std::fabs(T.dnx*ddx) + std::fabs(T.dny*ddy)) * FILT
				        + (std::fabs(T.dnx)*(std::fabs(pxd)+std::fabs(T.dxc[0]))
				         + std::fabs(T.dny)*(std::fabs(pyd)+std::fabs(T.dyc[0]))) * 3e-16 ) / az
				      + std::fabs(T.dzc[0]) * 1e-15;
				if ( !std::isfinite(c.z) || !std::isfinite(c.err) ) { exact = true; break; }
			}
			zd.push_back(c);
		}
		if ( ! exact ) {
			std::sort(zd.begin(), zd.end(),
			          [](const Cross& a, const Cross& b){ return a.z < b.z; });
			/* 近接交点: 順序・ペアリングが誤差内で曖昧なら厳密列へ。ただし両方 z 一定面で
			 * kGE が同じなら、充填は kGE しか使わないので順序不問 = 曖昧でない。 */
			for ( size_t k = 0 ; k + 1 < zd.size() ; ++k ) {
				if ( zd[k+1].z - zd[k].z > zd[k].err + zd[k+1].err ) continue;
				if ( zd[k].kge >= 0 && zd[k+1].kge >= 0 && zd[k].kge == zd[k+1].kge
				     && zd[k].z == zd[k+1].z ) continue;
				exact = true; break;
			}
		}
		if ( ! exact ) {
			if ( zd.size() & 1 ) ++oddCols;
			/* 端の平面番号: kGE 前計算があれば即値。無ければ double 見積り+すれすれ時だけ
			 * その交差 1 個を厳密に解いて確定(列全体の厳密落ちはしない)。 */
			auto kge_of = [&](const Cross& c) -> int {
				if ( c.kge >= 0 ) return c.kge;
				double t = (c.z - org[2])/dx - 0.5;
				double g = (c.err + (std::fabs(c.z)+std::fabs(org[2]))*5e-16)/dx + 1e-12;
				int k = (int)std::ceil(t);
				if ( std::fabs(t - std::nearbyint(t)) > g ) {   /* 十分離れている */
					if ( k < 0 ) k = 0;
					if ( k > Nz ) k = Nz;
					return k;
				}
				const Tri& T = tris[c.tri];                     /* すれすれ → この交差だけ厳密 */
				EFT ze = T.z[0] - (T.nx*(PX[ix]-T.x[0]) + T.ny*(PY[iy]-T.y[0])) / T.nz;
				if ( k < 0 ) k = 0;
				if ( k > Nz ) k = Nz;
				while ( k > 0 && !(PZ[k-1] < ze) ) --k;
				while ( k < Nz && PZ[k] < ze ) ++k;
				return k;
			};
			for ( size_t k = 0 ; k + 1 < zd.size() ; k += 2 ) {
				int klo = kge_of(zd[k]), khi = kge_of(zd[k+1]);
				for ( int izv = klo ; izv < khi ; ++izv ) col[izv] = 1;   /* half-open [zlo,zhi) */
			}
			continue;
		}
		/* ---- 厳密列パス(タイ・すれすれ・近接のみここに来る)---- */
		const EFT &px = PX[ix], &py = PY[iy];
		zs.clear();
		for ( size_t bi = 0 ; bi < bin.size() ; ++bi ) {
			const Tri& T = tris[bin[bi]];
			if ( T.ori == 0 ) continue;
			int in3 = 1;
			for ( int e = 0 ; e < 3 && in3 ; ++e ) {
				/* SoS 辞書式: ① f ② ∂f/∂x = ea ③ ∂f/∂y = eb(0 は返らない) */
				int s = (int)CGAL::sign( T.ea[e]*px + T.eb[e]*py + T.ec[e] );
				if ( ! s ) s = (int)CGAL::sign(T.ea[e]);
				if ( ! s ) s = (int)CGAL::sign(T.eb[e]);
				if ( s != T.ori ) in3 = 0;
			}
			if ( ! in3 ) continue;
			zs.push_back(T.z[0] - (T.nx*(px-T.x[0]) + T.ny*(py-T.y[0])) / T.nz);
		}
		if ( zs.size() < 2 ) { if ( zs.size() == 1 ) ++oddCols; continue; }
		std::sort(zs.begin(), zs.end());
		if ( zs.size() & 1 ) ++oddCols;   /* 非閉入力のみ。最後の 1 個は旧実装同様に無視 */
		for ( size_t k = 0 ; k + 1 < zs.size() ; k += 2 ) {
			const EFT &zlo = zs[k], &zhi = zs[k+1];
			/* 最小の iz で PZ[iz] >= zlo(double 見積り→厳密比較で ±補正)。 */
			int iz = (int)std::ceil((CGAL::to_double(zlo)-org[2])/dx - 0.5);
			if ( iz < 0 ) iz = 0;
			if ( iz > Nz ) iz = Nz;
			while ( iz > 0 && !(PZ[iz-1] < zlo) ) --iz;
			while ( iz < Nz && PZ[iz] < zlo ) ++iz;
			for ( ; iz < Nz && PZ[iz] < zhi ; ++iz )   /* half-open: zhi ちょうどは含まない */
				col[iz] = 1;
		}
	}
	return oddCols;
}

/* HDF5 ヘルパ: スカラ / 1D / 3D dataset を書く。 */
static void h5_scalar(hid_t f, const char* name, hid_t type, const void* val)
{
	hid_t sp = H5Screate(H5S_SCALAR);
	hid_t d  = H5Dcreate2(f, name, type, sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	H5Dwrite(d, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, val);
	H5Dclose(d); H5Sclose(sp);
}
static void h5_attr_str(hid_t f, const char* name, const char* val)
{
	hid_t t = H5Tcopy(H5T_C_S1); H5Tset_size(t, strlen(val)+1);
	hid_t sp = H5Screate(H5S_SCALAR);
	hid_t a = H5Acreate2(f, name, t, sp, H5P_DEFAULT, H5P_DEFAULT);
	H5Awrite(a, t, val);
	H5Aclose(a); H5Sclose(sp); H5Tclose(t);
}

} // namespace

static sPtr<pigData> herr(const char* msg) { return thNEW(pigDataError,(thNEW(stdString,(msg)))); }

void
cgaVoxelize_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 3 ) { result = herr("export_vox: needs path, params, and >=1 region mesh"); return; }
	refPath = (*args)[0]->get_str();
	const char* path = refPath->get_str();

	/* params: dx(必須) / pad(既定8) / regions[{name,side}] */
	sPtr<pigData> ph = (*args)[1];
	sPtr<pigData> vdx  = ph->get_ix(thNEW(pigDataString,("dx")));
	if ( vdx == thNULL ) { result = herr("export_vox: params needs dx"); return; }
	double dx = vdx->get_flt();
	if ( !(dx > 0) ) { result = herr("export_vox: dx must be > 0"); return; }
	sPtr<pigData> vpad = ph->get_ix(thNEW(pigDataString,("pad")));
	int pad = ( vpad != thNULL ) ? (int)vpad->get_int() : 8;
	sPtr<pigDataArray> regions = ph->get_ix(thNEW(pigDataString,("regions")))->obt_array();

	int nmesh = na - 2;
	std::vector<std::vector<Tri> > regionTris(nmesh);
	double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
	for ( int i = 0 ; i < nmesh ; ++i ) {
		sPtr<cgMesh3D> m3 = sPtr<cgMesh3D>::d_cast((*args)[2+i]);
		if ( ! m3.is_notNull() ) { result = herr("export_vox: region inputs must be 3D meshes"); return; }
		mesh_to_tris(m3->mesh(), regionTris[i], lo, hi);
	}
	if ( lo[0] > hi[0] ) { result = herr("export_vox: empty geometry"); return; }

	double org[3];
	int N[3];
	for ( int a = 0 ; a < 3 ; ++a ) {
		org[a] = lo[a] - pad*dx;
		N[a]   = (int)std::ceil((hi[a] - lo[a] + 2*pad*dx)/dx) + 1;
		if ( N[a] < 1 ) N[a] = 1;
	}
	int Nx = N[0], Ny = N[1], Nz = N[2];

	/* HDF5 vox.h5 を書く(python mesh2vox.py と同一スキーマ)。 */
	hid_t f = H5Fcreate(path, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if ( f < 0 ) { result = herr("export_vox: cannot create HDF5 file"); return; }
	h5_attr_str(f, "format", "srava-vox");
	h5_attr_str(f, "version", "1");
	int64_t nx = Nx, ny = Ny, nz = Nz;
	h5_scalar(f, "Nx", H5T_NATIVE_INT64, &nx);
	h5_scalar(f, "Ny", H5T_NATIVE_INT64, &ny);
	h5_scalar(f, "Nz", H5T_NATIVE_INT64, &nz);
	h5_scalar(f, "dx", H5T_NATIVE_DOUBLE, &dx);
	h5_scalar(f, "dy", H5T_NATIVE_DOUBLE, &dx);
	h5_scalar(f, "dz", H5T_NATIVE_DOUBLE, &dx);
	{ hsize_t d3 = 3; hid_t sp = H5Screate_simple(1, &d3, NULL);
	  hid_t d = H5Dcreate2(f, "origin", H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	  H5Dwrite(d, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, org); H5Dclose(d); H5Sclose(sp); }

	hid_t g = H5Gcreate2(f, "masks", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	hsize_t dims[3] = { (hsize_t)Nx, (hsize_t)Ny, (hsize_t)Nz };
	std::vector<uint8_t> ins;
	for ( int i = 0 ; i < nmesh ; ++i ) {
		long odd = voxelize_tris(regionTris[i], org, dx, Nx, Ny, Nz, ins);
		if ( odd > 0 )   /* 閉メッシュ入力ならあり得ない(非閉入力の自己診断。stderr は PIG_SEP_LOG で採取可) */
			::fprintf(stderr, "[export_vox] WARN: region %d: %ld column(s) with odd crossing parity (non-closed input mesh?)\n", i, odd);
		/* name / side(inside/outside) を regions[i] から(無ければ既定) */
		std::string name = "region" + std::to_string(i);
		bool outside = false;
		if ( regions.is_notNull() && i < regions->length() ) {
			sPtr<pigData> rh = regions->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
			if ( rh != thNULL ) {
				sPtr<pigData> vn = rh->get_ix(thNEW(pigDataString,("name")));
				if ( vn != thNULL ) name = vn->get_str()->get_str();
				sPtr<pigData> vs = rh->get_ix(thNEW(pigDataString,("side")));
				if ( vs != thNULL && strcmp(vs->get_str()->get_str(), "outside") == 0 ) outside = true;
			}
		}
		if ( outside ) for ( size_t k = 0 ; k < ins.size() ; ++k ) ins[k] = ins[k] ? 0 : 1;
		hid_t sp = H5Screate_simple(3, dims, NULL);
		hid_t d  = H5Dcreate2(g, name.c_str(), H5T_NATIVE_UINT8, sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Dwrite(d, H5T_NATIVE_UINT8, H5S_ALL, H5S_ALL, H5P_DEFAULT, ins.empty()?NULL:&ins[0]);
		H5Dclose(d); H5Sclose(sp);
	}
	H5Gclose(g);
	H5Fclose(f);

	/* D_REF 用の content_hash + size/mtime。 */
	{ uint64_t h = 1469598103934665603ULL; const uint64_t prime = 1099511628211ULL;
	  FILE* fp = ::fopen(path, "rb");
	  if ( fp ) { uint8_t buf[65536]; size_t n;
	    while ( (n = ::fread(buf,1,sizeof buf,fp)) > 0 )
	      for ( size_t i = 0 ; i < n ; ++i ) { h ^= buf[i]; h *= prime; }
	    ::fclose(fp); }
	  refHash = (pHashKeyType)h; }
	struct stat st;
	if ( ::stat(path, &st) == 0 ) { refSize = (INTEGER64)st.st_size; refMtime = (INTEGER64)st.st_mtime; }

	/* 結果 = D_REF OUTPUT の pigData 表現。cgaExport と同じ形(#3406, 2026-07-31 メモ 1./2.)。
	 * 書き込みは agent の set_body → ptsDataCache → codec が選ぶ WriterRef。 */
	result = pig_data_ref_make(PIG_DREF_OUTPUT, refPath, refSize, refMtime, refHash);
}

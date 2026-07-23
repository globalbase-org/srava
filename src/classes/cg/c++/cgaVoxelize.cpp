/*
 * cgaVoxelize — export_vox(path, params, mesh0, mesh1, …) を Cartesian 格子へボクセル化し、中立な
 *   vox.h5(srava と k-Wave の間の中立フォーマット)を書く計算本体(ptsCalcBody 派生)。
 *   args = [ path(INLINE 文字列), params(INLINE ハッシュ), mesh…(CACHE 可変個) ]。
 *   params = { dx, pad, regions:[{name,side},…] }。regions[i] ↔ mesh[i]。
 * 内外判定は **自前 z-パリティ法**(各(ix,iy)列で +z レイの三角交差 z を集めソート→ペア区間を内部)。
 *   三角は EPECK mesh を to_double した double で扱う(厳密 inside 機械は使わない=軽量・依存最小)。
 * 出力は export と同様 **D_REF OUTPUT**(path+size+mtime+content_hash・mesh バイナリは書かない)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"pig/c++/ptsWireCacheStreamWriterRef.h"
#include	"ts2/c++/stdString.h"
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

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
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

struct Tri { double a[3], b[3], c[3]; };

/* cgMesh3D の EPECK Surface_mesh → double 三角リスト + bbox 更新。非三角面は扇分割。 */
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
			double* pp[3] = { T.a, T.b, T.c };
			for ( int k = 0 ; k < 3 ; ++k ) {
				const Mesh::Point& P = M.point(iv[k]);
				pp[k][0] = CGAL::to_double(P.x());
				pp[k][1] = CGAL::to_double(P.y());
				pp[k][2] = CGAL::to_double(P.z());
				for ( int a = 0 ; a < 3 ; ++a ) {
					if ( pp[k][a] < lo[a] ) lo[a] = pp[k][a];
					if ( pp[k][a] > hi[a] ) hi[a] = pp[k][a];
				}
			}
			tris.push_back(T);
		}
	}
}

/* 三角リストを格子へ z-パリティでボクセル化 → inside[Nx*Ny*Nz](C-order: ((ix*Ny)+iy)*Nz+iz)。 */
static void voxelize_tris(const std::vector<Tri>& tris, const double org[3], double dx,
                          int Nx, int Ny, int Nz, std::vector<uint8_t>& inside)
{
	std::vector<std::vector<float> > cross((size_t)Nx*Ny);
	for ( size_t ti = 0 ; ti < tris.size() ; ++ti ) {
		const Tri& T = tris[ti];
		const double *a = T.a, *b = T.b, *c = T.c;
		double minx = std::min(std::min(a[0],b[0]),c[0]), maxx = std::max(std::max(a[0],b[0]),c[0]);
		double miny = std::min(std::min(a[1],b[1]),c[1]), maxy = std::max(std::max(a[1],b[1]),c[1]);
		int ix0 = (int)std::ceil ((minx-org[0])/dx); if ( ix0 < 0 ) ix0 = 0;
		int ix1 = (int)std::floor((maxx-org[0])/dx); if ( ix1 > Nx-1 ) ix1 = Nx-1;
		int iy0 = (int)std::ceil ((miny-org[1])/dx); if ( iy0 < 0 ) iy0 = 0;
		int iy1 = (int)std::floor((maxy-org[1])/dx); if ( iy1 > Ny-1 ) iy1 = Ny-1;
		if ( ix0 > ix1 || iy0 > iy1 ) continue;
		double d = (b[1]-c[1])*(a[0]-c[0]) + (c[0]-b[0])*(a[1]-c[1]);
		if ( std::fabs(d) < 1e-18 ) continue;
		for ( int ix = ix0 ; ix <= ix1 ; ++ix ) {
			double px = org[0] + ix*dx;
			for ( int iy = iy0 ; iy <= iy1 ; ++iy ) {
				double py = org[1] + iy*dx;
				double l1 = ((b[1]-c[1])*(px-c[0]) + (c[0]-b[0])*(py-c[1])) / d;
				double l2 = ((c[1]-a[1])*(px-c[0]) + (a[0]-c[0])*(py-c[1])) / d;
				double l3 = 1.0 - l1 - l2;
				if ( l1 < 0 || l2 < 0 || l3 < 0 ) continue;
				double z = l1*a[2] + l2*b[2] + l3*c[2];
				cross[(size_t)iy*Nx + ix].push_back((float)z);
			}
		}
	}
	inside.assign((size_t)Nx*Ny*Nz, 0);
	for ( int iy = 0 ; iy < Ny ; ++iy ) {
		for ( int ix = 0 ; ix < Nx ; ++ix ) {
			std::vector<float>& zs = cross[(size_t)iy*Nx + ix];
			if ( zs.size() < 2 ) continue;
			std::sort(zs.begin(), zs.end());
			for ( size_t k = 0 ; k + 1 < zs.size() ; k += 2 ) {
				double zlo = zs[k], zhi = zs[k+1];
				int iz0 = (int)std::ceil ((zlo-org[2])/dx); if ( iz0 < 0 ) iz0 = 0;
				int iz1 = (int)std::floor((zhi-org[2])/dx); if ( iz1 > Nz-1 ) iz1 = Nz-1;
				for ( int iz = iz0 ; iz <= iz1 ; ++iz )
					inside[((size_t)ix*Ny + iy)*Nz + iz] = 1;
			}
		}
	}
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
	sPtr<pigDataArray> regions = sPtr<pigDataArray>::d_cast(ph->get_ix(thNEW(pigDataString,("regions"))));

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
		voxelize_tris(regionTris[i], org, dx, Nx, Ny, Nz, ins);
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
}

sPtr<ptsWireCacheStreamWriter>
cgaVoxelize_::get_writer()
{
	return thNEW(ptsWireCacheStreamWriterRef,(parent, target, refPath, refSize, refMtime, refHash));
}

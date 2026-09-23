/*
 * vcaExportVox — export_vox(path, params, mesh0, mesh1, …) を Cartesian 格子へボクセル化し、中立な
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
 *
 * ★★ #3468 (2026-09-01): **cgal.so から openvdb_cg.so へ移設**した (旧 cgaVoxelize)。
 *   理由: この op は「メッシュ全般 → vox.h5」という **cgal の幾何とは無関係な仕事**で、
 *   それを cgal が所有していた (モジュール境界の約束①「他カーネルの機能を借りて自分の顔で
 *   出さない」の裏返し)。そのために cgal.so が libhdf5 を背負っていた。
 *   ⚠ 「cgal.so が hdf5 のぶん太っていた」わけではない — hdf5 は外部の DT_NEEDED で、
 *     cgal.so 内の H5 シンボルは undefined のみ。**利得は責務の置き場所**であってサイズではない。
 *   ★ 受け皿を openvdb_cg にしたのは、**CGAL と OpenVDB を両方リンクしている唯一の既存モジュール**
 *     だから。#3469 (vd-grid も受ける) が要求するのはまさにその 2 つで、追加は hdf5 だけで済む。
 *     新モジュール (voxutils) を立てても #3469 をやる限り openvdb 依存は入るので、
 *     「openvdb に縛られたくない」は新設を選ぶ理由にならない (ひさ判断 2026-09-01)。
 *   ⚠ 代償: export_vox の可用性が SRAVA_MODULE_OPENVDB に従属する。既定 ON なので通常は無害。
 *
 * ★★ #3469 (2026-09-01): **vd-grid3d (OpenVDB のグリッド) も受ける**。1 つの h5 に、別カーネルで
 *   作ったレイヤを混在させられる。sig を広げるだけで routing は自動 (planner は型を見てモジュールを
 *   選ぶ)。★ **1 op で混在レイヤを受けるので追記モードが要らない** — 格子の自動決定・D_REF が
 *   上書きを表現できないこと・並行書き込み、の 3 問題が構造的に起きない。
 *
 *   ★ 値はメッシュ経路と **一致しない**。混同させないこと:
 *       メッシュ入力  厳密 z-パリティ (EPECK + symbolic perturbation)。メッシュの幾何だけの関数
 *       vd-grid 入力  level set の **符号**。grid の dx で既に離散化済み
 *     「同じ答えを速く出す」のではなく **別の答え**。速さの話ではなく、
 *     「openvdb で作った形をそのまま h5 に落とせる」ことが価値。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"vc/c++/vdcgExact.h"   /* ★ #3545 段 4: 厳密幾何は libsrava_vdcg に在る (CGAL-free の宣言) */
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdGridVdb.h"   /* ★ #3545 段 5: 橋は OpenVDB 型を扱うので読んでよい */          /* ★ #3469: vd-grid3d 入力 */
#include	<openvdb/openvdb.h>
#include	<openvdb/tools/Interpolation.h>   /* GridSampler / BoxSampler (world-space サンプル) */
#include	"pig/c++/pigDataRef.h"   /* 結果 = D_REF の pigData 表現 */
#include	"ts2/c++/stdString.h"
#include	"vd/c++/vdArena.h"   /* #3474: 例外境界 vd_arena_guard */
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/vcaExportVox_.h"

#include	<hdf5.h>
#include	<vector>
#include	<algorithm>
#include	<cmath>
#include	<string>
#include	<string.h>
#include	<stdio.h>
#include	<stdint.h>
#include	<sys/stat.h>
#include	"pig/c++/pigModuleError.h"
/* ★ #3475: このモジュール専用のエラー生成子 (共有ヘッダを持たないので
 *   ここで定義する)。文言は "[TAG] openvdb_cg/op: message" になる。 */
PIG_DEFINE_MODULE_ERR(vca_err, "openvdb_cg")


CLASS_TINYSTATE(vc/c++/vcaExportVox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vcaExportVox_(
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


vcaExportVox_::vcaExportVox_(TS_ARGS0)
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

/* ⚠ #3545 段 4: 厳密幾何 (Tri / tri_precompute / mesh_to_tris / sos_side / voxelize_tris) は
 *   **libsrava_vdcg へ移した** (modules/openvdb_cg/c++/vdcgExact.cpp)。アルゴリズムは 1 行も
 *   変えていないので値は動かない。⇒ この TU は CGAL を 1 枚も引かなくなった。
 *   ★ 移した先が cgal の幾何ライブラリ **ではない**のは、この厳密計算が橋固有のロジックで、
 *     libsrava_cg には入れないというひさ判断 (2026-09-15) があるため。置き場所は橋自身の .so。
 */


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

static sPtr<pigData> herr(const char* msg) { return vca_err(thNEW(stdString,(msg))); }

void
vcaExportVox_::compute()
{
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。この op は arena を使わない (TBB 予算の対象外) が、
	 *   openvdb / HDF5 が投げると受け手が無く agent ごと死ぬので境界だけ張る。 */
	if ( ! vd_arena_guard("export_vox", [&]{
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

	/* ★ #3469: 領域は **メッシュ** か **vd-grid3d** のどちらか。型ごとに集め方が違うので分ける。
	 *   ⚠ 「格子が揃っていること」は要求しない。起票時は複数 vd-grid の transform 不一致を
	 *     明示エラーにする予定だったが、**要らなかった** — ここは各入力を出力格子へ
	 *     独立にラスタライズするだけで、#3463 の bool_from_args のようにツリーを直接
	 *     合成しない。dx も原点も違うグリッドを混ぜて問題ない。 */
	int nmesh = na - 2;
	vdcgTrisHandle regionTris(nmesh);   /* ★ #3545 段 4: 不透明 (中身は libsrava_vdcg 側) */
	std::vector<openvdb::FloatGrid::ConstPtr> regionGrid(nmesh);
	double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
	for ( int i = 0 ; i < nmesh ; ++i ) {
		sPtr<cgMesh3D> m3 = sPtr<cgMesh3D>::d_cast((*args)[2+i]);
		if ( m3.is_notNull() ) { vdcg_tris_add_mesh(regionTris.p, i, m3, lo, hi); continue; }
		sPtr<vdGrid> vg = sPtr<vdGrid>::d_cast((*args)[2+i]);
		if ( vg.is_notNull() && vg->box().g ) {
			regionGrid[i] = vg->box().g;
			/* bbox は **active voxel** の世界座標。level set の active は narrow band だが、
			 * 帯は表面を覆うので物体の外接箱としてはこれで足りる (内部は inactive タイル)。 */
			openvdb::CoordBBox ib = vg->box().g->evalActiveVoxelBoundingBox();
			if ( ib.empty() ) continue;
			/* 8 隅を world へ (map が非軸平行でも正しい外接箱になるように全隅を見る)。 */
			for ( int c = 0 ; c < 8 ; ++c ) {
				openvdb::Coord k( (c&1) ? ib.max().x() : ib.min().x(),
				                  (c&2) ? ib.max().y() : ib.min().y(),
				                  (c&4) ? ib.max().z() : ib.min().z() );
				openvdb::Vec3d w = vg->box().g->transform().indexToWorld(k.asVec3d());
				for ( int a = 0 ; a < 3 ; ++a ) {
					if ( w[a] < lo[a] ) lo[a] = w[a];
					if ( w[a] > hi[a] ) hi[a] = w[a];
				}
			}
			continue;
		}
		result = herr("export_vox: region inputs must be 3D meshes or volume grids");
		return;
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
		if ( regionGrid[i] ) {
			/* ★ #3469: level set の **符号**で内外を決め、出力格子のセル中心へ焼く。
			 *   ★ **world 空間でサンプルする** (grid 自身の格子へ resample しない)。出力格子の
			 *     原点・dx は全入力の bbox から決まるので grid の格子とは一般に一致せず、
			 *     resampleToMatch だけでは原点のずれを吸収できないため。
			 *     GridSampler::wsSample が map を通して補間するので dx 不一致も原点ずれも同時に扱える。
			 *   ⚠ narrow band の外は ±background だが、正しい level set なら内部は負・外部は正
			 *     なので符号判定は帯の外でも成立する。
			 *   ⚠ サンプル点は **セル中心** org+(i+1/2)dx — メッシュ経路と同じ規約に揃える。 */
			openvdb::tools::GridSampler<openvdb::FloatGrid, openvdb::tools::BoxSampler>
			    sampler(*regionGrid[i]);
			ins.assign((size_t)Nx*Ny*Nz, 0);
			for ( int ix = 0 ; ix < Nx ; ++ix )
			for ( int iy = 0 ; iy < Ny ; ++iy )
			for ( int iz = 0 ; iz < Nz ; ++iz ) {
				openvdb::Vec3d w( org[0] + (ix + 0.5)*dx,
				                  org[1] + (iy + 0.5)*dx,
				                  org[2] + (iz + 0.5)*dx );
				if ( sampler.wsSample(w) < 0.0f )
					ins[((size_t)ix*Ny + iy)*Nz + iz] = 1;
			}
		} else {
		long odd = vdcg_voxelize(regionTris.p, i, org, dx, Nx, Ny, Nz, ins);
		if ( odd > 0 )   /* 閉メッシュ入力ならあり得ない(非閉入力の自己診断。stderr は PIG_SEP_LOG で採取可) */
			::fprintf(stderr, "[export_vox] WARN: region %d: %ld column(s) with odd crossing parity (non-closed input mesh?)\n", i, odd);
		}
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
	}, vdwhy) )
		result = vca_err(thNEW(stdString,(vdwhy.c_str())));
}

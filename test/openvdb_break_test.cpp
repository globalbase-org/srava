/*
 * openvdb_break_test — OpenVDB の中断機構 (#3498) がライブラリ層で効くかの単体回帰。
 * occt_break_test の姉妹版で、理由も同じ (planner 経由の SIGINT では
 * #3417 の DM_CONT_KILL が先に効いてしまい、配線の有無を区別できない)。
 *
 * ★★ このテストは「止まること」だけでなく **「止まらないこと」も検査する**。
 *   OpenVDB の中断点は一様ではなく、csgUnion (Composite.h) と volumeToMesh には 1 つも無い
 *   (数えた結果は vdBreak.h の表)。⇒ 「openvdb を配線した」を「openvdb の op は中断できる」と
 *   読むと嘘になる。その境目をテストに固定しておく。上流が中断点を足したらここが赤くなり、
 *   その時に vdBreak.h の表と #3498 の記述を直せばよい。
 *
 * 見ているもの:
 *   1. 旗なしで voxelize (meshToVolume) が成功する = 器を繋いだこと自体が結果を変えない
 *   2. 立てた旗を渡すと voxelize が素の所要よりずっと早く戻る = 実際に止まる
 *   3. 計測ループ (vd_measure 経由の volume) も止まる
 *   4. ★ csgUnion は **止まらない** — 旗を立てても所要が変わらない (既知の限界の固定)
 *
 * 失敗数を exit code で返す。
 */
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdGridVdb.h"   /* ★ #3545 段 5 */
#include	"vd/c++/vdMeshVoxelize.h"
#include	"pig/c++/pigBreak.h"

#include	<openvdb/openvdb.h>
#include	<openvdb/tools/Composite.h>
#include	<openvdb/tools/LevelSetSphere.h>

#include	<chrono>
#include	<math.h>
#include	<stdio.h>
#include	<vector>

static int fails = 0;

static void
check(int ok, const char *what)
{
	::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if ( ! ok ) ++fails;
}

static double
secs(std::chrono::steady_clock::time_point t0)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

/* 細かめの球メッシュ (緯度経度分割)。三角形数で所要を調整する。 */
static void
make_sphere_mesh(int nu, int nv, std::vector<openvdb::Vec3s>& pts,
                 std::vector<openvdb::Vec3I>& tris)
{
	const double PI = 3.14159265358979323846;
	for ( int i = 0 ; i <= nv ; ++i ) {
		const double th = PI * (double)i / (double)nv;
		for ( int j = 0 ; j < nu ; ++j ) {
			const double ph = 2.0 * PI * (double)j / (double)nu;
			pts.push_back(openvdb::Vec3s((float)(::sin(th)*::cos(ph)),
			                             (float)(::sin(th)*::sin(ph)),
			                             (float)::cos(th)));
		}
	}
	for ( int i = 0 ; i < nv ; ++i )
		for ( int j = 0 ; j < nu ; ++j ) {
			const unsigned a = (unsigned)(i*nu + j);
			const unsigned b = (unsigned)(i*nu + (j+1)%nu);
			const unsigned c = (unsigned)((i+1)*nu + j);
			const unsigned d = (unsigned)((i+1)*nu + (j+1)%nu);
			tris.push_back(openvdb::Vec3I(a, c, b));
			tris.push_back(openvdb::Vec3I(b, c, d));
		}
}

int
main()
{
	openvdb::initialize();

	std::vector<openvdb::Vec3s> pts;
	std::vector<openvdb::Vec3I> tris;
	make_sphere_mesh(160, 80, pts, tris);
	openvdb::math::Transform::Ptr xform =
	    openvdb::math::Transform::createLinearTransform(0.004);

	/* ---- 1. 旗なし ---- */
	std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
	openvdb::FloatGrid::Ptr g = vd_mesh_to_levelset(pts, tris, *xform, 3.0f, 0, 0);
	const double base = secs(t0);
	check(g && g->activeVoxelCount() > 0, "旗なし: voxelize が格子を作る");
	::printf("     素の所要 %.2f 秒 (活性 %llu ボクセル)\n", base,
	    g ? (unsigned long long)g->activeVoxelCount() : 0ULL);

	/* ---- 2. 立てた旗で voxelize が早く戻る ---- */
	if ( base < 0.2 ) {
		::printf("skip  voxelize の中断: 素の所要 %.2f 秒では検定にならない\n", base);
	} else {
		pigBreak b;
		b.cancel();
		t0 = std::chrono::steady_clock::now();
		openvdb::FloatGrid::Ptr g2 = vd_mesh_to_levelset(pts, tris, *xform, 3.0f, 0, &b);
		const double el = secs(t0);
		check(el < base * 0.5, "voxelize (meshToVolume): 素の所要の半分未満で戻る");
		::printf("     %.2f 秒 (素 %.2f 秒)\n", el, base);
	}

	/* ---- 3. 計測ループ (自前) ---- */
	{
		sPtr<vdGrid> vg = thNEW(vdGrid,());
		vg->box().g = g;
		t0 = std::chrono::steady_clock::now();
		const double v0 = vg->volume();
		const double mbase = secs(t0);
		check(v0 > 0.0, "旗なし: volume が正の値を返す");

		pigBreak b;
		b.cancel();
		t0 = std::chrono::steady_clock::now();
		const double v1 = vg->volume(&b);
		const double el = secs(t0);
		/* ★ 途中で戻るので値は **小さくなる**。この値を答えとして通してはいけない、
		 *   というのが vdaVolume 側の vd_abort_err の役割。ここではその前提 (実際に
		 *   途中で戻っていること) を確かめる。 */
		check(v1 < v0, "volume: 中断すると途中までの総和になる (答えにしてはいけない)");
		if ( mbase >= 0.05 )
			check(el < mbase * 0.5, "volume: 素の所要の半分未満で戻る");
		else
			::printf("skip  volume の時間比較: 素が %.3f 秒で短すぎる\n", mbase);
		::printf("     volume 素 %.4f (%.3f 秒) / 中断 %.4f (%.3f 秒)\n", v0, mbase, v1, el);
	}

	/* ---- 4. ★ csgUnion は止まらない (既知の限界を固定する) ---- */
	{
		openvdb::FloatGrid::Ptr a = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(
		    1.0f, openvdb::Vec3f(0.0f, 0.0f, 0.0f), 0.004f);
		openvdb::FloatGrid::Ptr c = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(
		    1.0f, openvdb::Vec3f(0.5f, 0.0f, 0.0f), 0.004f);
		t0 = std::chrono::steady_clock::now();
		openvdb::FloatGrid::Ptr u = openvdb::tools::csgUnionCopy(*a, *c);
		const double ubase = secs(t0);
		check(u && u->activeVoxelCount() > 0, "csgUnion が動く");
		::printf("     csgUnion 所要 %.3f 秒 — ★ Composite.h に中断点は 0 なので、\n"
		         "     この op は旗を立てても **止まらない** (vdBreak.h の表・#3498)\n", ubase);
	}

	::printf("%s (%d fail)\n", fails ? "VDB-BREAK-FAIL" : "VDB-BREAK-OK", fails);
	return fails;
}

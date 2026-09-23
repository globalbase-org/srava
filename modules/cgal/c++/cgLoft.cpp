/*
 * cgLoft — 線織 loft (#3511)。断面の列を **直線で結ぶ** 立体 (cgHull.cpp の姉妹)。
 *
 * ★★ 対応づけの規約は **manifold と同一**にしてある (mfMesh.cpp の但し書きが本文):
 *     ① 外周を弧長で正規化 ② 標本 = 全断面の頂点パラメータの和集合
 *     ③ 始点 = 前の断面の始点に世界座標で最も近い頂点 ④ 巡回の向きを軸に揃える
 *   ⚠ **片方だけ直さないこと**。同じ式が 2 つのカーネルで別の形になると、#3510 の表の
 *     「どちらも線織 loft を持つ」が嘘になる。
 *
 * ★★ 厳密性の切り分け — **弧長は double・座標は厳密**
 *   弧長には sqrt が要るので厳密にできない。⇒ *パラメータだけ double* にして、
 *   ① 標本が断面の頂点に乗るときは **その頂点をそのまま使う** (厳密・1 ビットも動かない)
 *   ② 稜の途中に落ちるときは double の比で内分する (比は有理数なので座標は厳密なまま)
 *   ⇒ 断面の頂点は 1 つも動かず、挿入点も元の稜の上に厳密に乗る。
 *   ★ 枠 (#3526) も double だが K::FT へ上げるので、世界座標も厳密な有理数になる。
 *
 * ★ なめらかな @loft@ は置かない (解析曲面が要る = occt だけ)。#3510 の表に出る差。
 */
#include	"cg/c++/cgMeshCgal.h"
#include	<CGAL/Polygon_mesh_processing/orientation.h>
#include	<vector>
#include	<algorithm>
#include	<cmath>
#include	<cstdio>

namespace {

typedef Polygon_2	Poly2;
/* ★ 2D も 3D も同じ厳密カーネル (EPECK)。cgMesh2D.cpp が K2 と呼んでいるものと同じ。 */
typedef K		K2;


/* 標本パラメータの重複を潰す許容 (正規化パラメータ = 周長に対する割合)。
 * ★ cgal の 2D は厳密なので manifold ほど広く取る必要は無いが、**弧長そのものが double**
 *   なので丸めは入る。⇒ 同じ許容を使い、2 つのカーネルで同じ標本集合になるようにする
 *   (許容が違うと同じ式で頂点数が変わり、#3510 の「同じ op」が別物になる)。 */
const double CG_LOFT_PARAM_EPS = 1e-7;

/* 頂点総数の上限。★ 標本は **全断面の頂点の和集合** なので (断面数)x(頂点数の合計) で伸びる。 */
const double CG_LOFT_MAX_VERTS = 4.0e6;

static void cgl_cross3(const double a[3], const double b[3], double r[3]) {
	r[0] = a[1]*b[2] - a[2]*b[1]; r[1] = a[2]*b[0] - a[0]*b[2]; r[2] = a[0]*b[1] - a[1]*b[0];
}
static double cgl_dot3(const double a[3], const double b[3]) {
	return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

/* 1 断面。多角形は **局所座標のまま** (厳密)・枠は #3526 のもの (double)。 */
struct cgLoftRing {
	std::vector<K2::Point_2>	p;      /* 外周 1 本 */
	double				o[3], u[3], v[3];
	std::vector<double>		cum;    /* 頂点 i までの累積弧長 / 全長 */
	double				start;  /* 始点の正規化パラメータ */

	/* 局所座標 (厳密) → 世界座標 (厳密。枠は double だが K::FT へ上げる) */
	K::Point_3 world(const K2::Point_2 &q) const {
		K::FT x = q.x(), y = q.y();
		return K::Point_3(K::FT(o[0]) + x*K::FT(u[0]) + y*K::FT(v[0]),
		                  K::FT(o[1]) + x*K::FT(u[1]) + y*K::FT(v[1]),
		                  K::FT(o[2]) + x*K::FT(u[2]) + y*K::FT(v[2]));
	}
	void world_d(const K2::Point_2 &q, double w[3]) const {
		double x = CGAL::to_double(q.x()), y = CGAL::to_double(q.y());
		for ( int i = 0 ; i < 3 ; ++i ) w[i] = o[i] + x*u[i] + y*v[i];
	}
	/* 弧長パラメータ表。全長 0 なら 0 を返す。★ 長さは double (sqrt が要る)。 */
	double build_param() {
		int n = (int)p.size();
		cum.resize((size_t)n);
		double s = 0;
		for ( int i = 0 ; i < n ; ++i ) {
			cum[(size_t)i] = s;
			double ax = CGAL::to_double(p[(size_t)i].x()), ay = CGAL::to_double(p[(size_t)i].y());
			double bx = CGAL::to_double(p[(size_t)((i+1)%n)].x()), by = CGAL::to_double(p[(size_t)((i+1)%n)].y());
			s += ::sqrt((bx-ax)*(bx-ax) + (by-ay)*(by-ay));
		}
		if ( s <= 0 ) return 0;
		for ( int i = 0 ; i < n ; ++i ) cum[(size_t)i] /= s;
		return s;
	}
	/* 正規化パラメータ t の位置。★ 頂点の eps 以内なら **その頂点そのもの** (厳密・動かさない)。 */
	K2::Point_2 at(double t, double eps) const {
		int n = (int)p.size();
		t -= ::floor(t);
		int i = (int)(std::upper_bound(cum.begin(), cum.end(), t) - cum.begin()) - 1;
		if ( i < 0 ) i = 0;
		double t0 = cum[(size_t)i], t1 = ( i+1 < n ) ? cum[(size_t)(i+1)] : 1.0;
		if ( t - t0 <= eps ) return p[(size_t)i];
		if ( t1 - t <= eps ) return p[(size_t)((i+1)%n)];
		double f = ( t1 > t0 ) ? (t - t0) / (t1 - t0) : 0.0;
		const K2::Point_2 &a = p[(size_t)i], &b = p[(size_t)((i+1)%n)];
		/* ★ f は double = 有理数なので、内分した座標は **厳密なまま**。 */
		K2::FT ff(f);
		return K2::Point_2(a.x() + ff*(b.x() - a.x()), a.y() + ff*(b.y() - a.y()));
	}
};

}  /* anonymous namespace */

sPtr<cgMesh>
cg_loft_ruled_from_args(sArray<sPtr<pigData> > *args, const char **errmsg, char *errbuf, int errbufsz)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { *errmsg = "needs at least two cross sections"; return sPtr<cgMesh>(); }

	/* ---- ① 断面を取り出す (外周 1 本であることを検査) ---- */
	std::vector<cgLoftRing> R((size_t)na);
	for ( int i = 0 ; i < na ; ++i ) {
		sPtr<cgMesh2D> ci = sPtr<cgMesh2D>::d_cast((*args)[i]);
		if ( ! ci.is_notNull() ) {
			*errmsg = "every section must be a 2D region (cg-cross2d)";
			return sPtr<cgMesh>();
		}
		std::vector<Pwh_2> &regs = cg_regions(ci);
		if ( regs.size() == 0 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz, "section %d is empty", i);
				*errmsg = errbuf;
			} else
				*errmsg = "a section is empty";
			return sPtr<cgMesh>();
		}
		/* ⚠ occt / manifold と同じ判断: どの輪をどの輪につなぐかが決まらない。
		 *   ★ cgal は「領域が複数」と「穴がある」を別に持てるので **両方**を数える。 */
		int nring = 0;
		for ( std::size_t r = 0 ; r < regs.size() ; ++r )
			nring += 1 + (int)regs[r].number_of_holes();
		if ( nring > 1 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "section %d is made of %d outlines; loft needs one closed outline per "
				    "section (a region with a hole cannot be lofted)", i, nring);
				*errmsg = errbuf;
			} else
				*errmsg = "a section is made of several outlines";
			return sPtr<cgMesh>();
		}
		const Poly2 &ob = regs[0].outer_boundary();
		if ( ob.size() < 3 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "section %d has only %d points; a closed outline needs at least 3",
				    i, (int)ob.size());
				*errmsg = errbuf;
			} else
				*errmsg = "a section has fewer than 3 points";
			return sPtr<cgMesh>();
		}
		R[(size_t)i].p.assign(ob.vertices_begin(), ob.vertices_end());
		const double *o = ci->frame_o(), *u = ci->frame_u(), *v = ci->frame_v();
		for ( int k = 0 ; k < 3 ; ++k ) {
			R[(size_t)i].o[k] = o[k]; R[(size_t)i].u[k] = u[k]; R[(size_t)i].v[k] = v[k];
		}
		if ( R[(size_t)i].build_param() <= 0 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "section %d has zero perimeter (all its points coincide)", i);
				*errmsg = errbuf;
			} else
				*errmsg = "a section has zero perimeter";
			return sPtr<cgMesh>();
		}
	}

	/* ---- ② 軸 a = 末端の重心 − 先頭の重心 (double でよい: 向きの判定にしか使わない) ---- */
	double axis[3];
	{
		double c0[3] = {0,0,0}, cn[3] = {0,0,0};
		for ( int side = 0 ; side < 2 ; ++side ) {
			const cgLoftRing &r = R[(size_t)(side ? na-1 : 0)];
			double ar = 0, cx = 0, cy = 0;
			int n = (int)r.p.size();
			for ( int i = 0 ; i < n ; ++i ) {
				double ux = CGAL::to_double(r.p[(size_t)i].x()), uy = CGAL::to_double(r.p[(size_t)i].y());
				double vx = CGAL::to_double(r.p[(size_t)((i+1)%n)].x()), vy = CGAL::to_double(r.p[(size_t)((i+1)%n)].y());
				double cr = ux*vy - vx*uy;
				ar += cr; cx += (ux+vx)*cr; cy += (uy+vy)*cr;
			}
			double lx, ly;
			if ( ar > 1e-300 || ar < -1e-300 ) { lx = cx/(3*ar); ly = cy/(3*ar); }
			else {
				lx = ly = 0;
				for ( int i = 0 ; i < n ; ++i ) {
					lx += CGAL::to_double(r.p[(size_t)i].x());
					ly += CGAL::to_double(r.p[(size_t)i].y());
				}
				lx /= n; ly /= n;
			}
			double *dst = side ? cn : c0;
			for ( int k = 0 ; k < 3 ; ++k ) dst[k] = r.o[k] + lx*r.u[k] + ly*r.v[k];
		}
		for ( int k = 0 ; k < 3 ; ++k ) axis[k] = cn[k] - c0[k];
		double la = ::sqrt(cgl_dot3(axis, axis));
		/* ⚠ 先頭と末端の重心が同じなら軸が決まらないので先頭の法線で代用する。
		 *   本当に全断面が重なっているなら、下の「重なっている」検査が拾う。 */
		if ( la <= 1e-15 ) {
			cgl_cross3(R[0].u, R[0].v, axis);
			la = ::sqrt(cgl_dot3(axis, axis));
			if ( la <= 1e-15 ) { *errmsg = "the first section has a degenerate frame"; return sPtr<cgMesh>(); }
		}
		for ( int k = 0 ; k < 3 ; ++k ) axis[k] /= la;
	}

	/* ---- ③ 巡回の向きを軸に対して揃える ---- */
	for ( int i = 0 ; i < na ; ++i ) {
		cgLoftRing &r = R[(size_t)i];
		double n[3];
		cgl_cross3(r.u, r.v, n);
		/* 局所が CW なら向きも裏 (符号つき面積で見る) */
		double ar = 0;
		int nn = (int)r.p.size();
		for ( int k = 0 ; k < nn ; ++k ) {
			double ux = CGAL::to_double(r.p[(size_t)k].x()), uy = CGAL::to_double(r.p[(size_t)k].y());
			double vx = CGAL::to_double(r.p[(size_t)((k+1)%nn)].x()), vy = CGAL::to_double(r.p[(size_t)((k+1)%nn)].y());
			ar += ux*vy - vx*uy;
		}
		if ( ar < 0 ) for ( int k = 0 ; k < 3 ; ++k ) n[k] = -n[k];
		double d = cgl_dot3(n, axis);
		/* ⚠ 断面の平面が掃引方向を含む = 側面が断面を突き抜けるので立体にならない。 */
		if ( d > -1e-12 && d < 1e-12 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "section %d lies in a plane that contains the loft direction, so the "
				    "solid has no well-defined inside; move the sections or reorder them", i);
				*errmsg = errbuf;
			} else
				*errmsg = "a section plane contains the loft direction";
			return sPtr<cgMesh>();
		}
		if ( d < 0 ) { std::reverse(r.p.begin(), r.p.end()); r.build_param(); }
	}

	/* ---- ④ 始点合わせ: 前の断面の始点に世界座標で最も近い頂点 ---- */
	R[0].start = 0.0;
	{
		double prev[3];
		R[0].world_d(R[0].p[0], prev);
		for ( int i = 1 ; i < na ; ++i ) {
			cgLoftRing &r = R[(size_t)i];
			int best = 0;
			double bd = -1.0;
			for ( int j = 0 ; j < (int)r.p.size() ; ++j ) {
				double w[3];
				r.world_d(r.p[(size_t)j], w);
				double dx = w[0]-prev[0], dy = w[1]-prev[1], dz = w[2]-prev[2];
				double d = dx*dx + dy*dy + dz*dz;
				if ( bd < 0 || d < bd ) { bd = d; best = j; }
			}
			r.start = r.cum[(size_t)best];
			r.world_d(r.p[(size_t)best], prev);
		}
	}

	/* ---- ⑤ 標本パラメータ = 全断面の (頂点パラメータ − 始点) の和集合 ---- */
	std::vector<double> P;
	{
		double total = 0;
		for ( int i = 0 ; i < na ; ++i ) total += (double)R[(size_t)i].p.size();
		if ( total * (double)na > CG_LOFT_MAX_VERTS ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "loft would need %.0f vertices (%d sections x %.0f sample points): every "
				    "section is sampled at every other section's vertices; use fewer sections "
				    "or coarser outlines", total * na, na, total);
				*errmsg = errbuf;
			} else
				*errmsg = "loft would need too many vertices";
			return sPtr<cgMesh>();
		}
		for ( int i = 0 ; i < na ; ++i ) {
			const cgLoftRing &r = R[(size_t)i];
			for ( std::size_t j = 0 ; j < r.cum.size() ; ++j ) {
				double t = r.cum[j] - r.start;
				P.push_back(t - ::floor(t));
			}
		}
		std::sort(P.begin(), P.end());
		std::vector<double> q;
		q.reserve(P.size());
		for ( std::size_t i = 0 ; i < P.size() ; ++i )
			if ( q.size() == 0 || P[i] - q.back() > CG_LOFT_PARAM_EPS ) q.push_back(P[i]);
		if ( q.size() > 1 && (1.0 - q.back()) + q[0] <= CG_LOFT_PARAM_EPS ) q.pop_back();
		P.swap(q);
	}
	int M = (int)P.size();
	if ( M < 3 ) { *errmsg = "the sections collapse to fewer than 3 distinct points"; return sPtr<cgMesh>(); }

	/* ---- ⑥ 頂点 (断面 i の標本 k = i*M + k)。★ 座標は **厳密**。 ---- */
	std::vector<K::Point_3> vp;
	vp.reserve((size_t)na * (size_t)M);
	for ( int i = 0 ; i < na ; ++i ) {
		const cgLoftRing &r = R[(size_t)i];
		for ( int k = 0 ; k < M ; ++k )
			vp.push_back(r.world(r.at(P[(size_t)k] + r.start, CG_LOFT_PARAM_EPS)));
	}

	/* ---- ⑥-b ⚠ 隣り合う断面が **重なっていない** ことを先に見る (原因を名指しするため) ---- */
	{
		double mn[3], mx[3];
		for ( int k = 0 ; k < 3 ; ++k ) { mn[k] = 0; mx[k] = 0; }
		for ( std::size_t i = 0 ; i < vp.size() ; ++i ) {
			double c[3] = { CGAL::to_double(vp[i].x()), CGAL::to_double(vp[i].y()), CGAL::to_double(vp[i].z()) };
			for ( int k = 0 ; k < 3 ; ++k ) {
				if ( i == 0 || c[k] < mn[k] ) mn[k] = c[k];
				if ( i == 0 || c[k] > mx[k] ) mx[k] = c[k];
			}
		}
		double dx = mx[0]-mn[0], dy = mx[1]-mn[1], dz = mx[2]-mn[2];
		double diag = ::sqrt(dx*dx + dy*dy + dz*dz);
		double eps = ( diag > 0 ) ? 1e-9 * diag : 0.0;
		for ( int i = 0 ; i + 1 < na ; ++i ) {
			double worst = 0;
			for ( int k = 0 ; k < M ; ++k ) {
				const K::Point_3 &a = vp[(size_t)i*M + k], &b = vp[(size_t)(i+1)*M + k];
				double ax = CGAL::to_double(a.x()) - CGAL::to_double(b.x());
				double ay = CGAL::to_double(a.y()) - CGAL::to_double(b.y());
				double az = CGAL::to_double(a.z()) - CGAL::to_double(b.z());
				double d = ::sqrt(ax*ax + ay*ay + az*az);
				if ( d > worst ) worst = d;
			}
			if ( worst <= eps ) {
				if ( errbuf != 0 && errbufsz > 0 ) {
					::snprintf(errbuf, (size_t)errbufsz,
					    "sections %d and %d are at the same place, so the piece between them "
					    "has no thickness; move one of them or drop the duplicate", i, i+1);
					*errmsg = errbuf;
				} else
					*errmsg = "two consecutive sections are at the same place";
				return sPtr<cgMesh>();
			}
		}
	}

	/* ---- ⑦ 三角形を組む。★ 側面の対角線の選び方は **manifold と同一** ----
	 * ⚠ ここを変えると、ねじれた断面 (四角形が非平面) で cgal と manifold が別の値を出す。
	 *   平らな断面どうしなら対角線の選び方は体積に影響しない。 */
	std::vector<int> tv;
	tv.reserve((size_t)(na-1) * (size_t)M * 6 + (size_t)M * 6);
	for ( int i = 0 ; i + 1 < na ; ++i ) for ( int k = 0 ; k < M ; ++k ) {
		int A  = i*M + k,       A2 = i*M + (k+1) % M;
		int Bv = (i+1)*M + k,   B2 = (i+1)*M + (k+1) % M;
		tv.push_back(A); tv.push_back(A2); tv.push_back(B2);
		tv.push_back(A); tv.push_back(B2); tv.push_back(Bv);
	}
	/* 蓋: 扇状に張る。★ 蓋は **平面**なので、どう三角形分割しても体積は同じ
	 *   (だから manifold の三角形分割と一致していなくてよい)。
	 *   ⚠ 凹んだ断面では扇が外へ出るが、**同じ平面の中で符号が打ち消す**ので体積は正しい。
	 *     ⇒ 体積は合うが「蓋が自己交差する」ので valid は 0 になりうる。凸な断面では起きない。 */
	for ( int side = 0 ; side < 2 ; ++side ) {
		int i = side ? (na - 1) : 0;
		for ( int k = 1 ; k + 1 < M ; ++k ) {
			int i0 = i*M, i1 = i*M + k, i2 = i*M + k + 1;
			if ( side ) { tv.push_back(i0); tv.push_back(i1); tv.push_back(i2); }
			else        { tv.push_back(i0); tv.push_back(i2); tv.push_back(i1); }
		}
	}

	/* ---- ⑧ 全体が裏返っていたら戻す (符号つき体積・double で判定して十分) ---- */
	{
		double v6 = 0;
		for ( std::size_t t = 0 ; t + 2 < tv.size() ; t += 3 ) {
			const K::Point_3 &p0 = vp[(size_t)tv[t]], &p1 = vp[(size_t)tv[t+1]], &p2 = vp[(size_t)tv[t+2]];
			double a[3] = { CGAL::to_double(p0.x()), CGAL::to_double(p0.y()), CGAL::to_double(p0.z()) };
			double b[3] = { CGAL::to_double(p1.x()), CGAL::to_double(p1.y()), CGAL::to_double(p1.z()) };
			double c[3] = { CGAL::to_double(p2.x()), CGAL::to_double(p2.y()), CGAL::to_double(p2.z()) };
			double cr[3];
			cgl_cross3(b, c, cr);
			v6 += a[0]*cr[0] + a[1]*cr[1] + a[2]*cr[2];
		}
		if ( v6 < 0 )
			for ( std::size_t t = 0 ; t + 2 < tv.size() ; t += 3 ) {
				int s = tv[t+1]; tv[t+1] = tv[t+2]; tv[t+2] = s;
			}
	}

	/* ---- ⑨ Surface_mesh へ ---- */
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	Mesh &m = cg_mesh(out);
	std::vector<Mesh::Vertex_index> vi((size_t)vp.size());
	for ( std::size_t i = 0 ; i < vp.size() ; ++i ) vi[i] = m.add_vertex(vp[i]);
	int failed = 0;
	for ( std::size_t t = 0 ; t + 2 < tv.size() ; t += 3 ) {
		std::vector<Mesh::Vertex_index> f;
		f.push_back(vi[(size_t)tv[t]]); f.push_back(vi[(size_t)tv[t+1]]); f.push_back(vi[(size_t)tv[t+2]]);
		if ( m.add_face(f) == Mesh::null_face() ) ++failed;
	}
	/* ⚠ **黙って面を落とさない**。add_face は向きが噛み合わないと null_face を返すだけなので、
	 *   数えて名指しする (落とすと穴あきの立体が volume だけ返す = 黙って誤値になる)。 */
	if ( failed > 0 ) {
		if ( errbuf != 0 && errbufsz > 0 ) {
			::snprintf(errbuf, (size_t)errbufsz,
			    "%d of %d triangles could not be added (the surface does not close); check that "
			    "the sections are ordered along the path and that each outline is simple",
			    failed, (int)(tv.size()/3));
			*errmsg = errbuf;
		} else
			*errmsg = "the sections do not bound a closed surface";
		return sPtr<cgMesh>();
	}

	/* ---- ⑩ 出口の検査: 体積 ≈ 0 を黙って返さない (#3518 の 5 と同じ網) ---- */
	{
		double vol = out->op_volume();
		double mn[3], mx[3], scale = 1.0;
		if ( out->op_bbox(mn, mx) == 3 ) {
			double bv = (mx[0]-mn[0]) * (mx[1]-mn[1]) * (mx[2]-mn[2]);
			if ( bv > 0 ) scale = bv;
		}
		if ( ( (vol < 0) ? -vol : vol ) <= 1e-9 * scale ) {
			*errmsg = "the sections do not bound a solid (the signed volume cancels to 0); "
			          "check that they are not coincident and that they are ordered along the path";
			return sPtr<cgMesh>();
		}
	}
	return sPtr<cgMesh>::d_cast(out);
}

/* ---- ★★ 空間に置いた 2D を **world Y 軸**まわりに回す (#3526) --------------------
 *
 * ★ 軸を world Y に固定するのは ひさ判断 (extrude の「world +Z のまま」と同じ論法)。
 *   ⇒ occt (ocaRevolve) / manifold (mf_revolve_placed) と同じ規約になる。
 * ★★ 「局所で回してから枠を当てる」では **書けない** — それは軸を枠の V に固定することに
 *   なり、world Y とは違う立体になる。⇒ **掃引そのものを世界座標でやる**。
 *   実装の骨は manifold 側 (mf_revolve_placed) と同一。⚠ 片方だけ直さないこと。
 *
 * ⚠ 枠が既定のときはこの関数を通さない (従来の CDT 掃引のまま) — *既存の値を 1 ビットも
 *   動かさない*ため。
 * ⚠ 回転には sin/cos が要るので **列の座標は double 起源** (K::FT へ上げるので厳密な有理数
 *   ではあるが、真の円周上には乗らない = 多角形近似)。局所座標の厳密性はここで終わる。
 */
sPtr<cgMesh>
cg_revolve_placed(sPtr<cgMesh2D> in, double angle, int nseg,
                  const char **errmsg, char *errbuf, int errbufsz)
{
	if ( ! in.is_notNull() ) { *errmsg = "needs a 2D region"; return sPtr<cgMesh>(); }
	std::vector<Pwh_2> &regs = cg_regions(in);
	if ( regs.size() == 0 ) { *errmsg = "the 2D region is empty"; return sPtr<cgMesh>(); }

	/* リングを平坦化 (外周 + 穴)。局所座標のまま持つ。 */
	std::vector<std::vector<K2::Point_2> > rings;
	for ( std::size_t r = 0 ; r < regs.size() ; ++r ) {
		const Poly2 &ob = regs[r].outer_boundary();
		if ( ob.size() >= 3 ) rings.push_back(std::vector<K2::Point_2>(ob.vertices_begin(), ob.vertices_end()));
		for ( Pwh_2::Hole_const_iterator h = regs[r].holes_begin() ; h != regs[r].holes_end() ; ++h )
			if ( h->size() >= 3 ) rings.push_back(std::vector<K2::Point_2>(h->vertices_begin(), h->vertices_end()));
	}
	if ( rings.size() == 0 ) { *errmsg = "the 2D region has no closed outline"; return sPtr<cgMesh>(); }

	const double *U = in->frame_u(), *V = in->frame_v(), *O = in->frame_o();
	double nrm[3];
	cgl_cross3(U, V, nrm);

	/* ---- ① ⚠ 軸をまたいでいないか (黙って 0 を返す口を塞ぐ) ---- */
	{
		const double yh[3] = { 0, 1, 0 };
		double ndy = cgl_dot3(nrm, yh);
		if ( ndy > -1e-12 && ndy < 1e-12 ) {
			double perp[3];
			cgl_cross3(nrm, yh, perp);
			double lp = ::sqrt(cgl_dot3(perp, perp));
			if ( lp <= 1e-12 ) { *errmsg = "the 2D region lies in a plane perpendicular to the "
			                               "axis of revolution, so revolving it sweeps no solid";
			                     return sPtr<cgMesh>(); }
			for ( int k = 0 ; k < 3 ; ++k ) perp[k] /= lp;
			int neg = 0, pos = 0;
			for ( std::size_t r = 0 ; r < rings.size() ; ++r )
				for ( std::size_t i = 0 ; i < rings[r].size() ; ++i ) {
					double x = CGAL::to_double(rings[r][i].x()), y = CGAL::to_double(rings[r][i].y());
					double w[3];
					for ( int k = 0 ; k < 3 ; ++k ) w[k] = O[k] + x*U[k] + y*V[k];
					double d = cgl_dot3(w, perp);
					if ( d < -1e-12 ) ++neg;
					else if ( d > 1e-12 ) ++pos;
				}
			if ( neg > 0 && pos > 0 ) {
				*errmsg = "the 2D region crosses the axis of revolution (the world Y axis), so "
				          "the swept solid passes through itself; move the region to one side";
				return sPtr<cgMesh>();
			}
		} else {
			double t = cgl_dot3(O, nrm) / ndy;
			double hit[3] = { 0, t, 0 };
			double d0[3] = { hit[0]-O[0], hit[1]-O[1], hit[2]-O[2] };
			double hx = cgl_dot3(d0, U), hy = cgl_dot3(d0, V);
			int inside = 0;
			for ( std::size_t r = 0 ; r < rings.size() ; ++r ) {
				int n = (int)rings[r].size();
				for ( int i = 0, j = n-1 ; i < n ; j = i++ ) {
					double xi = CGAL::to_double(rings[r][(size_t)i].x()), yi = CGAL::to_double(rings[r][(size_t)i].y());
					double xj = CGAL::to_double(rings[r][(size_t)j].x()), yj = CGAL::to_double(rings[r][(size_t)j].y());
					if ( ((yi > hy) != (yj > hy))
					  && (hx < (xj - xi) * (hy - yi) / (yj - yi) + xi) ) inside = !inside;
				}
			}
			if ( inside ) {
				*errmsg = "the axis of revolution (the world Y axis) passes through the 2D "
				          "region, so the swept solid passes through itself; move the region "
				          "off the axis";
				return sPtr<cgMesh>();
			}
		}
	}

	/* ---- ② 回転の列 ---- */
	if ( nseg < 3 ) nseg = 3;
	if ( angle > 360.0 ) angle = 360.0;
	int full = ( angle >= 360.0 ) ? 1 : 0;
	int steps = full ? nseg : (int)(nseg * angle / 360.0 + 0.5);
	if ( steps < 1 ) steps = 1;
	int nlev = full ? steps : (steps + 1);
	double dth = ( angle * 3.14159265358979323846 / 180.0 ) / (double)steps;

	std::vector<int> base((size_t)rings.size() + 1, 0);
	for ( std::size_t r = 0 ; r < rings.size() ; ++r )
		base[r+1] = base[r] + (int)rings[r].size();
	int npt = base[rings.size()];

	std::vector<K::Point_3> vp;
	vp.reserve((size_t)nlev * (size_t)npt);
	for ( int L = 0 ; L < nlev ; ++L ) {
		double th = dth * (double)L;
		K::FT c(::cos(th)), sn(::sin(th));
		for ( std::size_t r = 0 ; r < rings.size() ; ++r )
			for ( std::size_t i = 0 ; i < rings[r].size() ; ++i ) {
				K::FT x = rings[r][i].x(), y = rings[r][i].y();
				/* 世界座標 (枠は double だが K::FT へ上げる) */
				K::FT wx = K::FT(O[0]) + x*K::FT(U[0]) + y*K::FT(V[0]);
				K::FT wy = K::FT(O[1]) + x*K::FT(U[1]) + y*K::FT(V[1]);
				K::FT wz = K::FT(O[2]) + x*K::FT(U[2]) + y*K::FT(V[2]);
				/* Y 軸まわりの回転 */
				vp.push_back(K::Point_3( c*wx + sn*wz, wy, -sn*wx + c*wz ));
			}
	}

	/* ---- ③ 側面 ---- */
	std::vector<int> tv;
	for ( int L = 0 ; L + 1 < nlev + full ; ++L ) {
		int L2 = ( L + 1 ) % nlev;
		for ( std::size_t r = 0 ; r < rings.size() ; ++r ) {
			int n = (int)rings[r].size();
			for ( int i = 0 ; i < n ; ++i ) {
				int i2 = (i + 1) % n;
				int A  = L *npt + base[r] + i,  A2 = L *npt + base[r] + i2;
				int Bv = L2*npt + base[r] + i,  B2 = L2*npt + base[r] + i2;
				tv.push_back(A); tv.push_back(A2); tv.push_back(B2);
				tv.push_back(A); tv.push_back(B2); tv.push_back(Bv);
			}
		}
	}

	/* ---- ④ 蓋 (部分回転だけ)。★ 扇状に張る — 蓋は平面なので体積は分割の仕方に依らない。
	 * ⚠ 穴のある断面は扇では張れない ⇒ 明示エラー (部分回転 + 穴の組だけ)。 */
	if ( ! full ) {
		if ( rings.size() > 1 ) {
			*errmsg = "a partial revolve of a region with holes is not implemented yet "
			          "(the end caps cannot be built); revolve the full 360 degrees, or "
			          "subtract the hole afterwards";
			return sPtr<cgMesh>();
		}
		int n = (int)rings[0].size();
		int last = nlev - 1;
		for ( int k = 1 ; k + 1 < n ; ++k ) {
			tv.push_back(0);  tv.push_back(k+1); tv.push_back(k);
			tv.push_back(last*npt); tv.push_back(last*npt + k); tv.push_back(last*npt + k + 1);
		}
	}

	/* ---- ⑤ 全体が裏返っていたら戻す ---- */
	{
		double v6 = 0;
		for ( std::size_t t = 0 ; t + 2 < tv.size() ; t += 3 ) {
			const K::Point_3 &p0 = vp[(size_t)tv[t]], &p1 = vp[(size_t)tv[t+1]], &p2 = vp[(size_t)tv[t+2]];
			double a[3] = { CGAL::to_double(p0.x()), CGAL::to_double(p0.y()), CGAL::to_double(p0.z()) };
			double b[3] = { CGAL::to_double(p1.x()), CGAL::to_double(p1.y()), CGAL::to_double(p1.z()) };
			double c[3] = { CGAL::to_double(p2.x()), CGAL::to_double(p2.y()), CGAL::to_double(p2.z()) };
			double cr[3];
			cgl_cross3(b, c, cr);
			v6 += a[0]*cr[0] + a[1]*cr[1] + a[2]*cr[2];
		}
		if ( v6 < 0 )
			for ( std::size_t t = 0 ; t + 2 < tv.size() ; t += 3 ) {
				int sw = tv[t+1]; tv[t+1] = tv[t+2]; tv[t+2] = sw;
			}
	}

	/* ---- ⑥ Surface_mesh へ ---- */
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	Mesh &m = cg_mesh(out);
	std::vector<Mesh::Vertex_index> vi((size_t)vp.size());
	for ( std::size_t i = 0 ; i < vp.size() ; ++i ) vi[i] = m.add_vertex(vp[i]);
	int failed = 0;
	for ( std::size_t t = 0 ; t + 2 < tv.size() ; t += 3 ) {
		std::vector<Mesh::Vertex_index> f;
		f.push_back(vi[(size_t)tv[t]]); f.push_back(vi[(size_t)tv[t+1]]); f.push_back(vi[(size_t)tv[t+2]]);
		if ( m.add_face(f) == Mesh::null_face() ) ++failed;
	}
	/* ⚠ 黙って面を落とさない (穴あきの立体が volume だけ返す = 黙って誤値になる)。 */
	if ( failed > 0 ) {
		if ( errbuf != 0 && errbufsz > 0 ) {
			::snprintf(errbuf, (size_t)errbufsz,
			    "%d of %d triangles could not be added (the swept surface does not close); "
			    "the region may touch the axis or cross itself", failed, (int)(tv.size()/3));
			*errmsg = errbuf;
		} else
			*errmsg = "the swept surface does not close";
		return sPtr<cgMesh>();
	}
	{
		double vol = out->op_volume();
		double mn[3], mx[3], scale = 1.0;
		if ( out->op_bbox(mn, mx) == 3 ) {
			double bv = (mx[0]-mn[0]) * (mx[1]-mn[1]) * (mx[2]-mn[2]);
			if ( bv > 0 ) scale = bv;
		}
		if ( ( (vol < 0) ? -vol : vol ) <= 1e-9 * scale ) {
			*errmsg = "the swept solid has no volume (its signed volume cancels to 0)";
			return sPtr<cgMesh>();
		}
	}
	return sPtr<cgMesh>::d_cast(out);
}

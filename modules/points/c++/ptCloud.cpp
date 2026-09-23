/*
 * ptCloud — 点群本体の実装 (#3528)。外部ライブラリ非依存 (平坦な double 配列のみ)。
 *   設計の根拠と cache 形式は pt/c++/ptCloud.h の冒頭。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ts2/c++/stdString.h"
#include	"pt/c++/ptCloud.h"
#include	"common/affine.h"   /* ★ #3578: xform_point (7 カーネル共通の規約) */

#include	<string.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<math.h>

/* ---- codec の下請け (little-endian・d2Shape / ggMesh と同じ流儀) ---------------- */

/* ★ 大きな配列は **塊で**流す。1 点ずつ virtual を叩くと 10^6 点で 10^6 回になるうえ、
 *   ⚠ 全体を 1 回で渡すと int が 2GiB で溢れる (#3506 で実際に踏んだ形) ので 1MiB ずつ。 */
#define PT_BLK	(1 << 20)

static void put_u32(ptChunkSink &s, uint32_t v) {
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
	s.chunk(b, 4);
}
static uint32_t get_u32(ptChunkSource &s) {
	uint8_t b[4]; s.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}
static void put_f64v(ptChunkSink &s, const std::vector<double> &v) {
	size_t nb  = v.size() * sizeof(double);
	size_t pos = 0;
	const uint8_t *p = ( v.empty() ) ? (const uint8_t*)0 : (const uint8_t*)&v[0];
	while ( pos < nb ) {
		size_t take = ( nb - pos < (size_t)PT_BLK ) ? (nb - pos) : (size_t)PT_BLK;
		s.chunk(p + pos, (int)take);
		pos += take;
	}
}
static void get_f64v(ptChunkSource &s, std::vector<double> &v, size_t n) {
	v.resize(n);
	size_t nb  = n * sizeof(double);
	size_t pos = 0;
	uint8_t *p = ( v.empty() ) ? (uint8_t*)0 : (uint8_t*)&v[0];
	while ( pos < nb ) {
		size_t take = ( nb - pos < (size_t)PT_BLK ) ? (nb - pos) : (size_t)PT_BLK;
		s.pull(p + pos, (int)take);
		pos += take;
	}
}

/* ---- 表示 ---------------------------------------------------------------------- */

sPtr<stdString>
ptCloud::get_str()
{
	char b[96];
	::snprintf(b, sizeof b, "%s(%d%s)", type_name(), np(),
	           has_normals() ? ( oriented_ ? ", oriented normals" : ", normals" ) : "");
	return thNEW(stdString,(b));
}

/* ---- codec --------------------------------------------------------------------- */

void
ptCloud::encode(ptChunkSink &sink)
{
	uint32_t flags = 0;
	if ( has_normals() ) flags |= 1u;
	if ( oriented_ )     flags |= 2u;
	put_u32(sink, (uint32_t)np());
	put_u32(sink, flags);
	put_f64v(sink, xyz_);
	if ( has_normals() )
		put_f64v(sink, nrm_);
}

void
ptCloud::decode(ptChunkSource &src)
{
	uint32_t n     = get_u32(src);
	uint32_t flags = get_u32(src);
	get_f64v(src, xyz_, (size_t)n * (size_t)dim_);
	if ( flags & 1u )
		get_f64v(src, nrm_, (size_t)n * (size_t)dim_);
	else
		nrm_.clear();
	oriented_ = ( flags & 2u ) ? 1 : 0;
}

sPtr<ptCloud>
ptCloud::create_for_meta(const uint8_t *meta, int len)
{
	if ( len != 4 || meta == 0 )
		return sPtr<ptCloud>();
	int d = 0;
	if ( ::memcmp(meta, PT_TAG_3D, 4) == 0 ) d = 3;
	else if ( ::memcmp(meta, PT_TAG_2D, 4) == 0 ) d = 2;
	if ( d == 0 )
		return sPtr<ptCloud>();
	sPtr<ptCloud> p = thNEW(ptCloud,());
	p->set_dim(d);
	return p;
}

/* ---- 計測 ----------------------------------------------------------------------- */

int
ptCloud::op_bbox(double mn[3], double mx[3]) const
{
	int n = np();
	mn[0] = mn[1] = mn[2] = 0;
	mx[0] = mx[1] = mx[2] = 0;
	if ( n <= 0 )
		return 0;   /* 空の点群に AABB は無い (呼び元が明示エラーにする) */
	for ( int k = 0 ; k < dim_ ; ++k ) mn[k] = mx[k] = xyz_[k];
	for ( int i = 1 ; i < n ; ++i )
		for ( int k = 0 ; k < dim_ ; ++k ) {
			double v = xyz_[(size_t)i * dim_ + k];
			if ( v < mn[k] ) mn[k] = v;
			if ( v > mx[k] ) mx[k] = v;
		}
	return dim_;
}

int
ptCloud::op_centroid(double out[3]) const
{
	int n = np();
	out[0] = out[1] = out[2] = 0;
	if ( n <= 0 )
		return 0;   /* 空の点群に重心は無い */
	/* ★ 点群の重心は **点の平均** (メッシュの面積/体積重心とは別物)。 */
	double acc[3] = { 0, 0, 0 };
	for ( int i = 0 ; i < n ; ++i )
		for ( int k = 0 ; k < dim_ ; ++k ) acc[k] += xyz_[(size_t)i * dim_ + k];
	for ( int k = 0 ; k < dim_ ; ++k ) out[k] = acc[k] / (double)n;
	return dim_;
}

/* ---- 値配列から組む (points2d / points3d) --------------------------------------- */

static sPtr<pigData>
pt_at(sPtr<pigDataArray> a, int i)
{
	return a->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
}

sPtr<ptCloud>
pt_cloud_from_value(sPtr<pigData> v, int dim, char *err, int errsz)
{
	const char *opn   = ( dim == 2 ) ? "points2d" : "points3d";
	const char *shape = ( dim == 2 ) ? "[[x,y],...] or [[[x,y],[nx,ny]],...]"
	                                 : "[[x,y,z],...] or [[[x,y,z],[nx,ny,nz]],...]";
	sPtr<pigDataArray> pts = v.is_notNull() ? v->obt_array() : sPtr<pigDataArray>();
	if ( ! pts.is_notNull() ) {
		::snprintf(err, errsz, "%s: needs an array of points %s", opn, shape);
		return sPtr<ptCloud>();
	}
	sPtr<ptCloud> pc = thNEW(ptCloud,());
	pc->set_dim(dim);
	int np = pts->length();
	if ( np <= 0 )
		return pc;   /* 空の点群も値として作れる (valid(p)=0 がそれを言う) */

	/* ★ 形は **先頭の要素**が決める。以後これと違う形が来たら断る。 */
	sPtr<pigDataArray> e0 = pt_at(pts, 0)->obt_array();
	if ( ! e0.is_notNull() ) {
		::snprintf(err, errsz, "%s: each element must be %s", opn, shape);
		return sPtr<ptCloud>();
	}
	int nested = ( e0->length() > 0 && pt_at(e0, 0)->obt_array().is_notNull() ) ? 1 : 0;

	std::vector<double> &X = pc->xyz();
	std::vector<double> &N = pc->nrm();
	X.reserve((size_t)np * dim);
	if ( nested ) N.reserve((size_t)np * dim);

	for ( int i = 0 ; i < np ; ++i ) {
		sPtr<pigDataArray> e = pt_at(pts, i)->obt_array();
		if ( ! e.is_notNull() ) {
			::snprintf(err, errsz, "%s: element %d must be %s", opn, i, shape);
			return sPtr<ptCloud>();
		}
		sPtr<pigDataArray> p = e, n;
		if ( nested ) {
			p = ( e->length() > 0 ) ? pt_at(e, 0)->obt_array() : sPtr<pigDataArray>();
			n = ( e->length() > 1 ) ? pt_at(e, 1)->obt_array() : sPtr<pigDataArray>();
			if ( ! p.is_notNull() ) {
				/* ⚠ 混在: 先頭は [点, 法線] だったのにここは裸の点。 */
				::snprintf(err, errsz,
				    "%s: element %d is a bare point but the first element was [point, normal]"
				    " (mixing the two forms is not allowed)", opn, i);
				return sPtr<ptCloud>();
			}
			if ( ! n.is_notNull() ) {
				::snprintf(err, errsz,
				    "%s: element %d must be [point, normal] (the first element set that form)",
				    opn, i);
				return sPtr<ptCloud>();
			}
		} else if ( e->length() > 0 && pt_at(e, 0)->obt_array().is_notNull() ) {
			/* ⚠ 混在: 先頭は平坦だったのにここは入れ子。黙って片方に寄せない。 */
			::snprintf(err, errsz,
			    "%s: element %d is [point, normal] but the first element was a bare point"
			    " (mixing the two forms is not allowed)", opn, i);
			return sPtr<ptCloud>();
		}
		if ( p->length() < dim ) {
			::snprintf(err, errsz, "%s: point %d needs %d coordinates", opn, i, dim);
			return sPtr<ptCloud>();
		}
		for ( int k = 0 ; k < dim ; ++k )
			X.push_back(pt_at(p, k)->get_flt());
		if ( nested ) {
			if ( n->length() < dim ) {
				::snprintf(err, errsz, "%s: normal %d needs %d components", opn, i, dim);
				return sPtr<ptCloud>();
			}
			double len2 = 0;
			for ( int k = 0 ; k < dim ; ++k ) {
				double c = pt_at(n, k)->get_flt();
				len2 += c * c;
				N.push_back(c);
			}
			/* ⚠⚠ 長さ 0 の法線は **黙って通さない**。Poisson も RANSAC も
			 *   エラーにならずに走り、静かに嘘の形を返すため (既定の法線を作らないのと同じ理由)。 */
			if ( len2 == 0.0 ) {
				::snprintf(err, errsz, "%s: normal %d is the zero vector", opn, i);
				return sPtr<ptCloud>();
			}
		}
	}
	/* ★ 規約: **明示的に与えられた法線は向き付けされているとみなす** (与えた人が向きに
	 *   意味を持たせている)。推定した法線は estimate_normals が実際に向き付けできたときだけ。 */
	pc->set_oriented(nested ? 1 : 0);
	return pc;
}

/* ---- xyz ファイル --------------------------------------------------------------- */

/* 拡張子が xyz か (大文字小文字を問わない)。 */
static int
pt_is_xyz(const char *path)
{
	const char *dot = ::strrchr(path, '.');
	if ( dot == 0 )
		return 0;
	return ( ::strlen(dot) == 4
	         && ::tolower((unsigned char)dot[1]) == 'x'
	         && ::tolower((unsigned char)dot[2]) == 'y'
	         && ::tolower((unsigned char)dot[3]) == 'z' );
}

sPtr<ptCloud>
ptCloud::read_xyz(const char *path, char *err, int errsz)
{
	if ( err && errsz > 0 ) err[0] = '\0';
	if ( ! pt_is_xyz(path) ) {
		if ( err ) ::snprintf(err, errsz, "import: not an xyz file: %s", path);
		return sPtr<ptCloud>();
	}
	FILE *fp = ::fopen(path, "r");
	if ( fp == 0 ) {
		if ( err ) ::snprintf(err, errsz, "import: cannot open %s", path);
		return sPtr<ptCloud>();
	}
	sPtr<ptCloud> pc = thNEW(ptCloud,());
	pc->set_dim(3);
	std::vector<double> &X = pc->xyz();
	std::vector<double> &N = pc->nrm();

	char line[4096];
	int  nf    = -1;       /* 最初のデータ行が決める列数 (2/3/6) */
	long lineno = 0;
	while ( ::fgets(line, sizeof line, fp) != 0 ) {
		++lineno;
		const char *s = line;
		while ( *s == ' ' || *s == '\t' ) ++s;
		if ( *s == '\0' || *s == '\n' || *s == '\r' || *s == '#' )
			continue;   /* 空行とコメント */
		double v[8];
		int    got = 0;
		char  *end;
		while ( got < 8 ) {
			double d = ::strtod(s, &end);
			if ( end == s ) break;
			v[got++] = d;
			s = end;
		}
		if ( nf < 0 ) {
			/* ★ 列数は **最初のデータ行**が決める。geogram の XYZIOHandler と同じ判定
			 *   (2 or 3 = 座標だけ / 6 = 座標 + 法線)。 */
			if ( got != 2 && got != 3 && got != 6 ) {
				if ( err ) ::snprintf(err, errsz,
				    "import: %s:%ld: wrong number of fields (%d; xyz needs 2, 3 or 6)",
				    path, lineno, got);
				::fclose(fp);
				return sPtr<ptCloud>();
			}
			nf = got;
		} else if ( got != nf ) {
			/* ⚠ 混在は明示エラー — 途中から法線が増える/減るファイルを黙って
			 *   詰めると、法線の印が全体について嘘になる。 */
			if ( err ) ::snprintf(err, errsz,
			    "import: %s:%ld: has %d fields but the file started with %d",
			    path, lineno, got, nf);
			::fclose(fp);
			return sPtr<ptCloud>();
		}
		X.push_back(v[0]);
		X.push_back(v[1]);
		X.push_back(( nf >= 3 ) ? v[2] : 0.0);   /* 2 列は z=0 (geogram と同じ) */
		if ( nf == 6 ) {
			N.push_back(v[3]);
			N.push_back(v[4]);
			N.push_back(v[5]);
		}
	}
	::fclose(fp);
	/* ★ 規約: **ファイルが持っていた法線は向き付けされているとみなす** (書いた人が
	 *   向きに意味を持たせている)。推定した法線と違い、こちらは事実の申告。 */
	pc->set_oriented(( nf == 6 ) ? 1 : 0);
	return pc;
}

bool
ptCloud::write_to(const char *path, const char * /*unit*/)
{
	/* 単位概念のない形式なので unit は無視 (OFF/STL と同じ)。 */
	if ( ! pt_is_xyz(path) )
		return false;
	if ( dim_ != 2 && dim_ != 3 )
		return false;
	FILE *fp = ::fopen(path, "w");
	if ( fp == 0 )
		return false;
	int n  = np();
	int wn = has_normals();
	/* ★★ #3582 (ひさ判断 2026-09-22): **2D も書ける**。列数は .xyz の約束どおり 2 / 3 / 6:
	 *
	 *     法線なし 2D → 2 列  "x y"                 法線なし 3D → 3 列  "x y z"
	 *     法線あり 2D → 6 列  "x y 0 nx ny 0"       法線あり 3D → 6 列  "x y z nx ny nz"
	 *
	 *   ⚠ 4 列 (x y nx ny) は **書けない** — 読む側が受けるのは 2 / 3 / 6 だけ
	 *     (geogram の XYZIOHandler / CGAL read_xyz_points と揃えた形)。
	 *   ★ 法線つき 2D を 6 列で書くのは **情報を落とさない**ため (ひさ判断 2026-09-22)。
	 *     z=0 / nz=0 を補うので、読み戻すと 3D + 法線になる — これは法線なしの 2D が
	 *     3D で戻るのと **同じ向き**の変化で、新しい約束は増えない。
	 *   ⚠⚠ **往復で型が変わる** (pt-cloud2d → pt-cloud3d)。.xyz に「2D である」ことを
	 *     書く場所が無いため。⇒ 隠さず文書に書いてある (#3582 の案 (a))。 */
	for ( int i = 0 ; i < n ; ++i ) {
		const double *p = &xyz_[(size_t)i * dim_];
		if ( wn ) {
			const double *q = &nrm_[(size_t)i * dim_];
			/* ★ %.17g = double の往復が bit 一致する最短表記。 */
			::fprintf(fp, "%.17g %.17g %.17g %.17g %.17g %.17g\n",
			          p[0], p[1], ( dim_ == 3 ) ? p[2] : 0.0,
			          q[0], q[1], ( dim_ == 3 ) ? q[2] : 0.0);
		} else if ( dim_ == 3 ) {
			::fprintf(fp, "%.17g %.17g %.17g\n", p[0], p[1], p[2]);
		} else {
			::fprintf(fp, "%.17g %.17g\n", p[0], p[1]);
		}
	}
	int bad = ( ::ferror(fp) != 0 );
	if ( ::fclose(fp) != 0 )
		bad = 1;
	return ! bad;
}

/* ---- アフィン変換 (#3578) --------------------------------------------------------
 * ★★ 法線は **ベクトルではなく余ベクトル** — 点と同じ行列を当てると scale([2,1,1]) で
 *   面に対して傾く。正しくは逆転置 (3D) / 像の平面の中での直交条件 (2D)。⇒ 下の 2 本。
 */

/* 3D の法線: n' = M^-T n = cof(M) n / det(M)。
 * ⚠ **det で割る** (= 符号を掛ける)。割らずに cof だけ使うと反射 (det<0) で法線が裏返り、
 *   mirror した点群の内外が逆になる (球を鏡映すると法線が内向きになる)。 */
static void
pt_normal3(const double e[12], const double n[3], double o[3])
{
	double c00 =  ( e[5]*e[10] - e[6]*e[9]  );
	double c01 = -( e[4]*e[10] - e[6]*e[8]  );
	double c02 =  ( e[4]*e[9]  - e[5]*e[8]  );
	double c10 = -( e[1]*e[10] - e[2]*e[9]  );
	double c11 =  ( e[0]*e[10] - e[2]*e[8]  );
	double c12 = -( e[0]*e[9]  - e[1]*e[8]  );
	double c20 =  ( e[1]*e[6]  - e[2]*e[5]  );
	double c21 = -( e[0]*e[6]  - e[2]*e[4]  );
	double c22 =  ( e[0]*e[5]  - e[1]*e[4]  );
	double det = e[0]*c00 + e[1]*c01 + e[2]*c02;    /* = det3(e) ・ 0 は呼ぶ前に弾いてある */
	double s   = ( det != 0.0 ) ? 1.0 / det : 1.0;
	o[0] = ( c00*n[0] + c01*n[1] + c02*n[2] ) * s;
	o[1] = ( c10*n[0] + c11*n[1] + c12*n[2] ) * s;
	o[2] = ( c20*n[0] + c21*n[1] + c22*n[2] ) * s;
}

/* 2D (面内) の法線: 像の平面の中で、変換後の接線に直交する向き。
 *   B = 線形部の第 1・2 列 (3x2 ・ xy 平面の基底の行き先) ・ G = BᵀB ・ J = 面内 90 度回転
 *   接線 τ = J n なので、求める m は G J n に直交 ⇒ m ∝ J (G J n)。符号は恒等変換が n を
 *   そのまま返すように取る (⇒ マイナス)。出力は n' = B m。
 * ★ 平面を保つ変換では、これは 2x2 の逆転置 A^-T と **厳密に一致する** (2026-09-22 に
 *   乱数行列 2000 本で確認: 平面の中に留まる / 接線に直交する / 外向きが外向きのまま)。 */
static void
pt_normal2(const double e[12], const double n[2], double o[3])
{
	double g00 = e[0]*e[0] + e[4]*e[4] + e[8]*e[8];
	double g01 = e[0]*e[1] + e[4]*e[5] + e[8]*e[9];
	double g11 = e[1]*e[1] + e[5]*e[5] + e[9]*e[9];
	/* v = G J n   (J n = (-n1, n0)) */
	double v0 = -g00*n[1] + g01*n[0];
	double v1 = -g01*n[1] + g11*n[0];
	/* m = -J v = (v1, -v0) */
	double m0 =  v1;
	double m1 = -v0;
	o[0] = e[0]*m0 + e[1]*m1;
	o[1] = e[4]*m0 + e[5]*m1;
	o[2] = e[8]*m0 + e[9]*m1;
}

/* 長さを 1 に直す (d 成分)。長さ 0 なら 0 を返す (呼び手が明示エラーにする)。 */
static int
pt_normalize(double *v, int d)
{
	double s = 0;
	for ( int k = 0 ; k < d ; ++k ) s += v[k] * v[k];
	if ( s <= 0.0 )
		return 0;
	s = 1.0 / ::sqrt(s);
	for ( int k = 0 ; k < d ; ++k ) v[k] *= s;
	return 1;
}

sPtr<ptCloud>
pt_cloud_affine(sPtr<ptCloud> in, const double e[12], int outDim,
                const char *opn, char *err, int errsz)
{
	if ( ! in.is_notNull() ) {
		::snprintf(err, errsz, "%s: needs a point cloud", opn);
		return sPtr<ptCloud>();
	}
	int d = in->dim();
	int n = in->np();
	sPtr<ptCloud> out = thNEW(ptCloud,());
	out->set_dim(outDim);

	const std::vector<double> &X = in->xyz();
	std::vector<double>       &Y = out->xyz();
	Y.reserve((size_t)n * outDim);
	for ( int i = 0 ; i < n ; ++i ) {
		const double *p = &X[(size_t)i * d];
		double o[3];
		/* ★ 2D は z=0 の点として空間で変換する (平面を保つ変換なら z は 0 のまま戻る)。 */
		srava_affine::xform_point(e, p[0], p[1], ( d == 3 ) ? p[2] : 0.0, o);
		for ( int k = 0 ; k < outDim ; ++k ) Y.push_back(o[k]);
	}

	if ( in->has_normals() ) {
		const std::vector<double> &NI = in->nrm();
		std::vector<double>       &NO = out->nrm();
		NO.reserve((size_t)n * outDim);
		for ( int i = 0 ; i < n ; ++i ) {
			const double *q = &NI[(size_t)i * d];
			double o[3];
			if ( d == 3 ) pt_normal3(e, q, o);
			else          pt_normal2(e, q, o);
			if ( ! pt_normalize(o, outDim) ) {
				/* ⚠ 起きないはず (特異行列は common/affine.h が先に弾く) が、黙って
				 *   長さ 0 の法線を通すと下流が静かに嘘の形を返す ⇒ 明示エラー。 */
				::snprintf(err, errsz,
				    "%s: the transform collapses normal %d to the zero vector", opn, i);
				return sPtr<ptCloud>();
			}
			for ( int k = 0 ; k < outDim ; ++k ) NO.push_back(o[k]);
		}
	}
	/* ★ 向き付けの印はそのまま運ぶ — 変換は「向きが意味を持つ」ことを変えない
	 *   (反射で裏返るのは向きそのものであって、向き付けされているという事実ではない)。 */
	out->set_oriented(in->oriented());
	return out;
}

/* ---- union (#3578) ---------------------------------------------------------------- */

sPtr<ptCloud>
pt_cloud_union(sPtr<ptCloud> a, sPtr<ptCloud> b, const char *opn, char *err, int errsz)
{
	if ( ! a.is_notNull() || ! b.is_notNull() ) {
		::snprintf(err, errsz, "%s: needs two point clouds", opn);
		return sPtr<ptCloud>();
	}
	int outDim = ( a->dim() == 3 || b->dim() == 3 ) ? 3 : 2;

	/* ★ 法線は **空でない側がどちらも持っているときだけ**運ぶ。
	 *   ⚠ 空の点群は問わない — でないと union(p, points3d([])) が恒等でなくなる。 */
	sPtr<ptCloud> ab[2] = { a, b };
	int keep = 0, ori = 1, nonEmpty = 0;
	for ( int s = 0 ; s < 2 ; ++s ) {
		if ( ab[s]->np() <= 0 )
			continue;
		++nonEmpty;
		if ( ! ab[s]->has_normals() ) { keep = 0; break; }
		keep = 1;
		if ( ! ab[s]->oriented() ) ori = 0;
	}
	if ( nonEmpty == 0 )
		keep = 0;

	sPtr<ptCloud> out = thNEW(ptCloud,());
	out->set_dim(outDim);
	std::vector<double> &Y = out->xyz();
	std::vector<double> &N = out->nrm();
	Y.reserve(((size_t)a->np() + (size_t)b->np()) * outDim);
	if ( keep ) N.reserve(((size_t)a->np() + (size_t)b->np()) * outDim);

	for ( int s = 0 ; s < 2 ; ++s ) {
		int d = ab[s]->dim();
		int n = ab[s]->np();
		const std::vector<double> &X = ab[s]->xyz();
		const std::vector<double> &M = ab[s]->nrm();
		for ( int i = 0 ; i < n ; ++i ) {
			const double *p = &X[(size_t)i * d];
			for ( int k = 0 ; k < outDim ; ++k )
				Y.push_back(( k < d ) ? p[k] : 0.0);   /* ★ 2D → 3D は z=0 */
			if ( keep ) {
				const double *q = &M[(size_t)i * d];
				for ( int k = 0 ; k < outDim ; ++k )
					N.push_back(( k < d ) ? q[k] : 0.0);
			}
		}
	}
	out->set_oriented(keep ? ori : 0);
	return out;
}

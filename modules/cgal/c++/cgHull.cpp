/*
 * cgHull — hull(a[,b,…]) の計算本体 (#3511)。cgal 版。
 *
 * ★ **ブールではない**ので corefinement を通らない。全オペランドの**頂点を 1 つの点集合に
 *   集めて 1 回 convex_hull_3 / convex_hull_2 を回す**だけ。⇒ 二項に分けて畳む必要が無く、
 *   n 項が素直に書ける (union が二項 `(2)` 止まりなのとはここが違う)。
 * ★ 入力が閉じている必要も無い。点しか見ないので、自己交差でも開いていても凸包は定義できる。
 *
 * ⚠ **退化を黙って通さない** — 点が 1 点 / 1 直線上 / 1 平面上に乗っていると凸包は立体に
 *   ならない。CGAL は面グラフ版でも落ちずに「孤立点だけ」や平たい面グラフを返すので、
 *   ここで閉じているかを見て明示エラーにする (体積 0 の"立体"を返さない)。
 * ⚠ 2D は穴も点として拾ってよい — 穴の頂点は外周の内側にあるので凸包に影響しない。
 *   拾わない実装にすると「外周だけを持つ Pwh」を作る手間が増えるだけで結果は同じ。
 *
 * ★★ #3528: **点群 (pt-cloud2d / pt-cloud3d) も受ける**。
 *   hull はもともと入力から **頂点しか使っていない** ので、点群は拡張ではなく **素の入力**で、
 *   メッシュを渡す方が「頂点以外を捨てる」特殊ケースだった。
 *   ⚠ このため hull は **入力の表現と出力の表現が違う** (点 → メッシュ)。sig の
 *     「fold 形の出力は必ず主型」という規則はこれを表現できず、2026-09-13 に撤去した
 *     (pigModuleRegistry.cpp の該当コメント)。sig は次の 2 行で分解が閉じる:
 *       [cg-mesh3d,…,pt-cloud3d](*)->cg-mesh3d   メッシュが 1 つ以上混ざる呼び出し
 *       [pt-cloud3d](*)->cg-mesh3d               点群だけ (出力 ≠ 主型)
 */
#include	"cg/c++/cgMeshCgal.h"
#include	"pt/c++/ptCloud.h"   /* ★ #3528: 点群も受ける (中立の libsrava_pt) */
#include	"ts2/c++/stdString.h"

#include	<CGAL/convex_hull_3.h>
#include	<CGAL/convex_hull_2.h>
#include	<CGAL/boost/graph/helpers.h>   /* is_closed */
#include	<vector>

/* ---- 3D: 全オペランドの頂点 → convex_hull_3 → Surface_mesh ---- */
static sPtr<cgMesh>
cg_hull_3d(sArray<sPtr<pigData> > *args, int na, const char **errmsg)
{
	/* ★★ #3533: **入力が全部 2D で 1 つの平面に載っているなら、出力側を見るまでもなく立体に
	 *   ならない**。⇒ 点を積む前に構造で断る。
	 *   ⚠⚠ これを *出力の退化検査* に任せると **取り逃がす** (2026-09-15 に実測):
	 *     rotate(R,"x",90) と rotate(R,"x",-90) の凸包は、面 8 枚・閉じている・valid=1 で
	 *     体積だけが 1.1e-15 になる。下の `faces < 4 || ! is_closed` はどちらも真にならない。
	 *     ★ 厳密カーネルでは *本当に* 極薄の立体なので「計算が間違っている」わけではない —
	 *       枠が double なので cos(90°)=6.1e-17 の分だけ 2 枚が厳密には共面でないだけ。
	 *       ⇒ 出力に閾値を持ち込むのではなく、**入力の平面が一致するか**で見る (@same_plane@ は
	 *         #3526 から在る述語で、閾値は既に決まっている)。
	 *   ★ 2D 点群は常に z=0 に居るので「既定の枠」として同じ土俵で見る。
	 *   ⚠ manifold は Manifold::Hull の内部 ε で偶然これを捕まえていた (mfaHull の体積検査)。
	 *     捕まえ方が違うと *片方のカーネルだけ黙って値を返す* ので、あちらにも同じ検査を置く。 */
	{
		int all2d = 1, n2d = 0;
		sPtr<cgMesh2D> ref;
		int coplanar = 1, sawCloud2d = 0;
		for ( int i = 0 ; i < na && all2d ; ++i ) {
			sPtr<cgMesh2D> ci = sPtr<cgMesh2D>::d_cast((*args)[i]);
			if ( ci.is_notNull() ) {
				++n2d;
				if ( ! ref.is_notNull() ) ref = ci;
				else if ( ! ref->same_plane(ci->frame_o(), ci->frame_u(), ci->frame_v()) ) coplanar = 0;
				continue;
			}
			sPtr<ptCloud> pi = sPtr<ptCloud>::d_cast((*args)[i]);
			if ( pi.is_notNull() && pi->dim() == 2 ) { sawCloud2d = 1; continue; }
			all2d = 0;
		}
		if ( all2d && sawCloud2d && ref.is_notNull() && ! ref->frame_is_default() ) coplanar = 0;
		if ( all2d && n2d > 0 && coplanar ) {
			*errmsg = "all the operands are on one plane, so their convex hull is not a solid; "
			          "cast(\"cg-cross2d\", ...) the placed ones first so the whole call is a 2D hull";
			return sPtr<cgMesh>();
		}
	}
	std::vector<Point_3> pts;
	int had2d = 0;   /* ★ #3533: 退化したときの文言を選ぶため (下の is_closed 検査) */
	for ( int i = 0 ; i < na ; ++i ) {
		if ( sPtr<cgMesh2D>::d_cast((*args)[i]).is_notNull() ) had2d = 1;
		sPtr<cgMesh3D> mi = sPtr<cgMesh3D>::d_cast((*args)[i]);
		if ( mi.is_notNull() ) {
			const Mesh &m = cg_mesh(mi);
			pts.reserve(pts.size() + m.number_of_vertices());
			for ( Mesh::Vertex_index v : m.vertices() )
				pts.push_back(m.point(v));
			continue;
		}
		/* ★ #3528: 点群はそのまま点として積む (頂点を舐める段が読み出しに変わるだけ)。 */
		sPtr<ptCloud> pi = sPtr<ptCloud>::d_cast((*args)[i]);
		if ( pi.is_notNull() ) {
			/* ★ #3533: **2D 点群も z=0 の点として積む**。2D 点群は生成された時点から常に
			 *   既定の平面に居る (動かす op が 1 つも無い) ので、world へ起こすのに枠が要らない。
			 *   ⇒ @hull(rotate(rect,"x",90), points2d(…))@ = 「z=0 の点と別平面の図形の凸包」
			 *     が立体として求まる (#3533 の 1 節・ひさ裁定 2026-09-15)。
			 *   ⚠ *点群だけ* の次元混在 (@hull(points3d, points2d)@) は **型の側で断る**
			 *     (この行の集合は主型 cg-face3d を要求するので当たらない)。#3528 の規約どおり。 */
			const std::vector<double> &X = pi->xyz();
			int np = pi->np(), d = pi->dim();
			pts.reserve(pts.size() + (size_t)np);
			for ( int k = 0 ; k < np ; ++k )
				pts.push_back(( d == 2 )
				    ? Point_3(X[(size_t)k*2], X[(size_t)k*2+1], 0.0)
				    : Point_3(X[(size_t)k*3], X[(size_t)k*3+1], X[(size_t)k*3+2]));
			continue;
		}
		/* ★★ #3533: **空間に置かれた 2D 領域 (cg-face3d)** も点として積む。これが型を割った
		 *   一番の収穫で、@hull(rotate(rect,"x",90), points3d(…))@ のような「平面図形と空間の
		 *   点の凸包」が初めて書ける (#3533 の 1 節が「1 通りも通らない」と書いていた形)。
		 *   ⚠ 局所座標ではなく **world** へ起こしてから積むこと。局所のまま積むと別の平面の
		 *     図形が全部 z=0 に重なる = 黙って別の立体になる。
		 *   ★ 座標は枠が double なので double へ落ちる (平面の上は厳密・置き場所は double)。 */
		sPtr<cgMesh2D> ci = sPtr<cgMesh2D>::d_cast((*args)[i]);
		if ( ci.is_notNull() ) {
			for ( const Pwh_2 &r : cg_regions(ci) ) {
				const Polygon_2 &ob = r.outer_boundary();
				for ( Polygon_2::Vertex_const_iterator it = ob.vertices_begin() ;
				      it != ob.vertices_end() ; ++it ) {
					double w[3];
					ci->to_world(CGAL::to_double(it->x()), CGAL::to_double(it->y()), w);
					pts.push_back(Point_3(w[0], w[1], w[2]));
				}
			}
			continue;
		}
		*errmsg = "needs 3D meshes, 3D point clouds, or 2D regions placed in space";
		return sPtr<cgMesh>();
	}
	if ( pts.empty() ) { *errmsg = "the operands have no points"; return sPtr<cgMesh>(); }
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	try {
		CGAL::convex_hull_3(pts.begin(), pts.end(), cg_mesh(out));
	} catch ( const std::exception& ) {
		*errmsg = "CGAL could not build the convex hull";
		return sPtr<cgMesh>();
	}
	/* ★ 退化の検出。孤立点 / 線分 / 平面図形はここで閉じていないか面が無い。 */
	if ( cg_mesh(out).number_of_faces() < 4 || ! CGAL::is_closed(cg_mesh(out)) ) {
		/* ★ #3533: 2D 領域が混ざっていたなら「全部 1 つの平面に載っていた」が一番ありそうな
		 *   原因なので、**2D として求め直す書き方**まで言う。型が face3d でも幾何が z=0 に
		 *   居るなら cast で降ろせる (規約②)。 */
		*errmsg = had2d
		    ? "the points are degenerate (all on one plane), so the convex hull is not a solid; "
		      "if the operands really are on one plane, cast(\"cg-cross2d\", ...) the placed ones "
		      "first so the whole call is a 2D hull"
		    : "the points are degenerate (a single point, all on one line, or all on one plane), "
		      "so the convex hull is not a solid";
		return sPtr<cgMesh>();
	}
	return out;
}

/* ---- 2D: 全オペランドの頂点 → convex_hull_2 → 単一の Pwh (穴なし) ---- */
static sPtr<cgMesh>
cg_hull_2d(sArray<sPtr<pigData> > *args, int na, const char **errmsg)
{
	typedef K::Point_2 Point_2;
	std::vector<Point_2> pts;
	/* ★★ #3526 + #3528: hull は **点しか見ない** op なので、置き場所の扱いを 2 種類の
	 *   オペランド (枠を持つ 2D 領域 / 枠を持たない 2D 点群) で揃えておく必要がある。
	 *     ・2D 領域どうし … ブールと同じ **2 段**。同じ平面で軸の取り方だけ違うなら
	 *       **先頭の 2D 領域の枠**で表し直す / 本当に別の平面なら明示エラー。
	 *       ⇒ world の幾何は 1 ミリも動かさない (局所座標の読み方を揃えるだけ)。
	 *     ・2D 点群が混ざる … 点群は平面を **値として宣言しない** (枠のフィールドが無い) 上に、
	 *       **動かす op が 1 つも無い** (translate/rotate/scale/transform の行が pt-cloud に無い)。
	 *       ⇒ 2D 点群は生成された時点から **常に既定の平面 (z=0) に居る**。
	 *       ⇒ 相手の 2D 領域が別の平面に置かれていたら、それは *別の平面にある点集合の凸包* を
	 *         求めろと言われているのと同じで、答えは 2D 領域にならない。上の「本当に別の平面なら
	 *         明示エラー」と **まったく同じ状況** なので、同じく明示エラーにする。
	 *       ⚠ ここで「点群を先頭の枠の局所座標とみなす」と、z=0 に居た点を黙って別の平面へ
	 *         **移してしまう** (2940662「面外へ出す変換は黙って射影しない」の裏返し)。
	 *         空間での凸包が要るなら points3d(...) で 3D の hull を使うのが正しい書き方。
	 *       ★ 後で点群に枠を持たせる余地を残す形でもある (いま意味を与えると将来それを否定する)。
	 *       (macMINI と合意・2026-09-14) */
	sPtr<cgMesh2D> ref;            /* 枠の基準 = 最初の 2D 領域 (点群しか無ければ null = 既定枠) */
	int used_cloud = 0;
	int anyPlaced = 0;             /* ★ #3533 規約③: どれかが face3d なら結果も face3d */
	for ( int i = 0 ; i < na ; ++i ) {
		sPtr<cgMesh2D> ci = sPtr<cgMesh2D>::d_cast((*args)[i]);
		if ( ci.is_notNull() ) {
			if ( ci->is_placed() ) anyPlaced = 1;   /* ⚠ 表し直す前の値で見る */
			if ( ! ref.is_notNull() )
				ref = ci;
			else if ( ! ref->same_frame(ci->frame_o(), ci->frame_u(), ci->frame_v()) ) {
				ci = ci->reexpress(ref->frame_o(), ref->frame_u(), ref->frame_v());
				if ( ! ci.is_notNull() ) {
					*errmsg = "the 2D regions are on different planes, so their convex hull is not a "
					          "2D region; move them onto one plane first";
					return sPtr<cgMesh>();
				}
			}
			for ( const Pwh_2 &r : cg_regions(ci) ) {
				const Polygon_2 &ob = r.outer_boundary();
				for ( Polygon_2::Vertex_const_iterator it = ob.vertices_begin() ;
				      it != ob.vertices_end() ; ++it )
					pts.push_back(*it);
			}
			continue;
		}
		sPtr<ptCloud> pi = sPtr<ptCloud>::d_cast((*args)[i]);
		if ( pi.is_notNull() && pi->dim() == 2 ) {
			const std::vector<double> &X = pi->xyz();
			int np = pi->np();
			pts.reserve(pts.size() + (size_t)np);
			for ( int k = 0 ; k < np ; ++k )
				pts.push_back(Point_2(X[(size_t)k*2], X[(size_t)k*2+1]));
			used_cloud = 1;
			continue;
		}
		*errmsg = "needs 2D meshes or 2D point clouds";
		return sPtr<cgMesh>();
	}
	/* ★ 順序に依らないよう **ループの後で**見る (点群が先に来ても後に来ても同じ答えになる)。 */
	if ( used_cloud && ref.is_notNull() && ! ref->frame_is_default() ) {
		*errmsg = "a 2D point cloud always lies on the default plane (there is no transform for "
		          "point clouds), so it cannot be combined with 2D regions placed on another "
		          "plane; use points3d(...) for a hull in space, or move the regions back to "
		          "the default plane";
		return sPtr<cgMesh>();
	}
	if ( pts.empty() ) { *errmsg = "the operands have no points"; return sPtr<cgMesh>(); }
	std::vector<Point_2> hull;
	try {
		CGAL::convex_hull_2(pts.begin(), pts.end(), std::back_inserter(hull));
	} catch ( const std::exception& ) {
		*errmsg = "CGAL could not build the convex hull";
		return sPtr<cgMesh>();
	}
	if ( hull.size() < 3 ) {
		*errmsg = "the points are degenerate (a single point or all on one line), "
		          "so the convex hull has no area";
		return sPtr<cgMesh>();
	}
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	/* ★ #3526: 出力は **基準の枠**を引き継ぐ (点群が混ざる場合は上の検査により必ず既定枠)。 */
	if ( ref.is_notNull() ) {
		out->set_frame(ref->frame_o(), ref->frame_u(), ref->frame_v());
		/* ★ #3533: ここへ来るのは **単項の face3d** (平面の中の凸包 = 平面は動かない) か、
		 *   全部が cross2d の n 項だけ (face3d が混ざる n 項は 3D 側へ振られる)。 */
		out->set_placed(anyPlaced);
	}
	Polygon_2 p;
	for ( size_t i = 0 ; i < hull.size() ; ++i ) p.push_back(hull[i]);
	cg_regions(out).push_back(Pwh_2(p));
	return out;
}

sPtr<cgMesh>
cg_hull_from_args(sArray<sPtr<pigData> > *args, const char **errmsg)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 1 ) { *errmsg = "needs at least one mesh or point cloud"; return sPtr<cgMesh>(); }
	/* ★★ #3533: 振り分けは **sig の行と 1 対 1** でなければならない。sig が決めた出力型と
	 *   ここが作る値の型が食い違うと、スタンプは cg-mesh3d なのに中身は PLY2、という
	 *   *黙って通るずれ* になる (下流の routing が別のカーネルへ振る)。sig の 3 行:
	 *
	 *     (cg-mesh3d)->cg-mesh3d / (cg-cross2d)->cg-cross2d / (cg-face3d)->cg-face3d   単項
	 *     [cg-cross2d,mf-cross2d,pt-cloud2d](*!)->cg-cross2d                           全部 z=0
	 *     [… 3D 系 + face3d + cross2d + 点群 …](*!)->cg-mesh3d                          混ざる
	 *
	 *   ⇒ ここでの規則は「**3D が混ざるか、2 つ以上あって face3d が混ざれば 3D**」。
	 *   ★ 単項の face3d が 2D 側なのは、*平面の中の凸包は平面から出ない*から (sig 1 行目)。
	 *   ⚠ 型が face3d なら **幾何が同じ平面に居ても 3D へ行く** (ひさ裁定 2026-09-15)。
	 *     2D の凸包が欲しければ @cast("cg-cross2d", …)@ で先に降ろす。同じ平面なのに立体を
	 *     求めると退化するので、下の @cg_hull_3d@ が明示エラーでその書き方を案内する。 */
	int has3d = 0, nplaced2d = 0;
	for ( int i = 0 ; i < na ; ++i ) {
		if ( sPtr<cgMesh3D>::d_cast((*args)[i]).is_notNull() ) { has3d = 1; continue; }
		sPtr<cgMesh2D> ci = sPtr<cgMesh2D>::d_cast((*args)[i]);
		if ( ci.is_notNull() ) { if ( ci->is_placed() ) ++nplaced2d; continue; }
		sPtr<ptCloud> pi = sPtr<ptCloud>::d_cast((*args)[i]);
		if ( pi.is_notNull() ) { if ( pi->dim() == 3 ) has3d = 1; continue; }
		*errmsg = "needs a mesh or a point cloud";
		return sPtr<cgMesh>();
	}
	if ( has3d || ( na > 1 && nplaced2d > 0 ) ) return cg_hull_3d(args, na, errmsg);
	return cg_hull_2d(args, na, errmsg);
}

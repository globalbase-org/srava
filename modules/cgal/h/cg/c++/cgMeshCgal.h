#ifndef ___cgMeshCgal_h___
#define ___cgMeshCgal_h___
/*
 * cgMeshCgal.h — ★★ #3545 段 2: **CGAL を include する唯一のヘッダ** (cgal 側)。
 *
 * ⚠⚠ これを op の .cpp から include してはいけない。読んでよいのは
 *   ① libsrava_cg を作る .cpp (cgMesh3D.cpp / cgMesh2D.cpp / cgHull.cpp / …)
 *   ② **橋** モジュール (nef_cg / openvdb_cg) — CGAL 型を運ぶのが本分なので例外
 *   の 2 つだけ。⇒ 検査 = test/srava_op_cgal_free.sh
 *
 * ---- なぜ分けるか (実測・2026-09-16) ----
 * CGAL は header-only (Debian にコンパイル済み libCGAL は無い) なので、
 * **カーネルのヘッダを 1 枚 include しただけ**で @CGAL::get_static_error_handler()::_error_handler@
 * 等の関数内 static が その TU の .o に emit される (2026-09-16 実測・実ビルド行で):
 *
 *     CGAL/Exact_predicates_exact_constructions_kernel.h    可変大域 2 ・ CGAL:: 42
 *     CGAL/Exact_predicates_inexact_constructions_kernel.h  可変大域 2 ・ CGAL:: 42
 *     CGAL/Nef_polyhedron_3.h  (カーネルを引く)             可変大域 2 ・ CGAL:: 52
 *     ---- ここから下は **0 本** ----
 *     CGAL/Surface_mesh.h / Polygon_2.h / Polygon_with_holes_2.h
 *     CGAL/IO/Color.h / assertions.h / number_utils.h        いずれも 0
 *
 * ⚠⚠ **引き金はカーネルのヘッダ**であって「CGAL のヘッダ全般」ではない。
 *   「*使うと* 実体化する」(#3535② の旧説) も「*include すると* 実体化する」も一段荒い。
 *   ⇒ ただし結論は変わらない: @c cgMesh.h は EPECK を引くので、**公開ヘッダから落とす**しかない。
 *
 * 通常はリンカが @u@ (STB_GNU_UNIQUE) として 1 個に畳むので害が無いが、
 * モジュールは @-fvisibility=hidden@ (#3499 の 2 変種取り違え対策) で建てるため
 * **@d@ (local) に落ちる** = モジュールごとに別コピーになる。
 * ⇒ libsrava_cg 側と状態が食い違う。とくに @IO::Static::get_mode@ は ASCII/binary なので
 *   **書き出しの形式が黙って変わる** (落ちないので気づけない)。
 *
 * ★ 「呼ばなければ出ない」ではないので、非 inline の wrapper を足すだけでは止まらない。
 *   **公開ヘッダ (cgMesh.h) から CGAL を落とす**のが唯一の道。nef が段 1 で通った道と同じ
 *   (modules/nef/h/nf/c++/nfMeshCgal.h)。
 */
#include	<CGAL/Exact_predicates_exact_constructions_kernel.h>
#include	<CGAL/Surface_mesh.h>
#include	<CGAL/IO/Color.h>   /* 面の色(per-face property map "f:color")。color() op / 色つき export 用 */
#include	<CGAL/Polygon_2.h>
#include	<CGAL/Polygon_with_holes_2.h>

#include	"cg/c++/cgMesh.h"
#include	<vector>

/* ★ 旧 @cgMesh::K@ / @cgMesh::Mesh@ / @cgMesh2D::Pwh_2@ … を **ファイル有効域**へ移したもの。
 *   ⚠ 短い名前を大域に置いているが、このヘッダを読むのは上記①②の十数本だけ。
 *     クラスの入れ子 typedef に戻すと、その時点で公開ヘッダが CGAL を要求してしまう。 */
typedef CGAL::Exact_predicates_exact_constructions_kernel	K;
typedef K::Point_3						Point_3;
typedef CGAL::Surface_mesh<Point_3>				Mesh;
typedef CGAL::Polygon_2<K>					Polygon_2;
typedef CGAL::Polygon_with_holes_2<K>				Pwh_2;
typedef std::vector<K::Point_2>					Guide;   /* 開ポリライン(寸法線/ガイド)。閉じない */

/* ★ cgMesh3D / cgMesh2D が値で抱える CGAL の実体。公開ヘッダ側では **不透明**。 */
class cgMesh3DBox {
public:
	Mesh	m;
};

class cgMesh2DBox {
public:
	std::vector<Pwh_2>	regions;   /* 穴あき多角形の集合(空 = 空領域) */
	std::vector<Guide>	guides;    /* ガイド層(開ポリライン群)。ブールは触れず SVG/DXF で線として描く */
};

/* ---- CGAL 型を取る API。公開ヘッダに置けないのでここに自由関数として置く ----
 * ⚠ 精密な friend 宣言は **書けない** (引数型を公開ヘッダで名指しできない)
 *   ⇒ box() を公開アクセサにし、触れるのはこのヘッダを読める TU だけ、という規約で守る。 */
inline Mesh&       cg_mesh(cgMesh3D &o)       { return o.box().m; }
inline const Mesh& cg_mesh(const cgMesh3D &o) { return o.box().m; }
/* ★ sPtr を直に渡せる版 (sPtr は operator* を持たないので overload で受ける)。 */
inline Mesh&       cg_mesh(sPtr<cgMesh3D> o)  { return o->box().m; }

inline std::vector<Pwh_2>&       cg_regions(cgMesh2D &o)       { return o.box().regions; }
inline const std::vector<Pwh_2>& cg_regions(const cgMesh2D &o) { return o.box().regions; }
inline std::vector<Pwh_2>&       cg_regions(sPtr<cgMesh2D> o)  { return o->box().regions; }

inline std::vector<Guide>&       cg_guides(cgMesh2D &o)        { return o.box().guides; }
inline const std::vector<Guide>& cg_guides(const cgMesh2D &o)  { return o.box().guides; }
inline std::vector<Guide>&       cg_guides(sPtr<cgMesh2D> o)   { return o->box().guides; }

/* ★ 測地球 (sphere / icosphere の実体)。定義は cgMesh3D.cpp。
 *   ⚠ op からは cgMesh3D::build_geodesic() を呼ぶこと (素の型で受ける入口)。 */
void cga_make_geodesic(Mesh& ball, int seed, int n, double r);

#endif

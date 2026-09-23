/*
 * nfMeshCgal.h — ★★ #3545: **CGAL を include する唯一のヘッダ**。
 *
 * ⚠⚠ これを op の .cpp から include してはいけない。読んでよいのは **幾何ライブラリ
 *   (libsrava_cg) を作る .cpp** (nfMesh.cpp / nfWire.cpp / nfcBridge.cpp) だけ。
 *   ★ #3559 で置き場所が libsrava_nf_snc / libsrava_nf_hybrid から libsrava_cg へ移った
 *     (上流の実体を 1 イメージに集めるため)。読んでよい範囲の規則そのものは変わらない。
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
 * ⚠⚠ **引き金はカーネルのヘッダ**であって「CGAL のヘッダ全般」ではない (2026-09-16 訂正)。
 *   ここには当初「@Surface_mesh.h@ 単独で 2 本」と書いていたが、**単独では 0 本**だった
 *   — 他の CGAL ヘッダと一緒に読んだときの値を単独の値と取り違えていた。
 *   ★ 「*使うと* 実体化する」(#3535② の旧説) も「*include すると* 実体化する」も
 *     **どちらも一段荒い**。正しくは **どのヘッダを読んだか**で決まる。
 *   ⇒ ただし実務上の結論は変わらない: 幾何クラスの公開ヘッダはカーネルを引くので、
 *     **公開ヘッダから CGAL を落とす**以外に道は無い。
 *     ⚠ nfMesh.h を include しただけ・何も触らない TU にも 2 本 出ていた (これは実測どおり)。
 *
 * 通常はリンカが @u@ (STB_GNU_UNIQUE) として 1 個に畳むので害が無いが、モジュールは
 * @-fvisibility=hidden@ (#3499 の 2 変種取り違え対策) で建てるため **@d@ (local) に落ちる**
 * = モジュールごとに別コピーになる。⇒ cgal.so 側 (@u@ = 1 個) と食い違いうる。
 *
 * ★ 「呼ばなければ出ない」ではないので、非 inline の wrapper を足すだけでは止まらない。
 *   **ヘッダから CGAL を落とす**のが唯一の道 (ひさ提案の glue 方式の成立条件そのもの)。
 */
#ifndef ___nfMeshCgal_h___
#define ___nfMeshCgal_h___

#include	<CGAL/Exact_predicates_exact_constructions_kernel.h>
#include	<CGAL/Surface_mesh.h>
#include	<CGAL/Nef_polyhedron_3.h>

#include	"nf/c++/nfMesh.h"

/* ★ nfNefMesh が値で抱える CGAL の実体。公開ヘッダ側では **不透明**。 */
class nfNefBox {
public:
	typedef CGAL::Exact_predicates_exact_constructions_kernel	K;
	typedef K::Point_3						Point_3;
	typedef CGAL::Surface_mesh<Point_3>				Mesh;
	typedef CGAL::Nef_polyhedron_3<K>				Nef;

	nfNefBox() {}
	nfNefBox(const Nef &n) : n(n) {}

	Nef	n;
};

/* ---- CGAL 型を取る API。公開ヘッダに置けないのでここに自由関数として置く ---- */
inline nfNefBox::Nef&       nf_nef(nfNefMesh &m)       { return m.box().n; }
inline const nfNefBox::Nef& nf_nef(const nfNefMesh &m) { return m.box().n; }

/* 境界表現への変換 / 境界からの構築。⚠ 呼べるのは幾何 lib 側だけ。
 * ★ #3564 (2026-09-20): **sPtr で受ける**。以前は参照版が本体で、sPtr 版がそこへ
 *   @*p.__get()@ で転送していたが、@__get()@ は利用禁止 (ひさ) なので本体を sPtr 側へ寄せた。
 *   ⚠ メンバから呼ぶときは @nf_to_mesh(this, …)@ と書く (sPtr の変換 ctor が拾う)。 */
bool nf_to_mesh(sPtr<nfNefMesh> m, nfNefBox::Mesh &out);
void nf_set_from_mesh(sPtr<nfNefMesh> m, nfNefBox::Mesh &in);

/* ★★ #3559: **新しい値は「誰と同じ変種か」を必ず連れて作る** (@proto@)。
 *   nef_snc の op が作った値が nfb-mesh3d になったら型が黙って変わる ⇒ 引数で縛る。
 *   ⚠ 以前は変種ごとに別ライブラリだったので @thNEW(nfMesh,())@ で済んでいた。
 *     いまは 1 つのライブラリに 2 型が居るので、*どちらか* を言わないと決まらない。 */
sPtr<nfNefMesh> nf_build_from_facets(sPtr<nfNefMesh> proto, nfNefBox::Mesh &m);

/* ★ Nef から nfNefMesh を作る (公開ヘッダに CGAL 型の ctor は置けない)。 */
sPtr<nfNefMesh> nf_make_nef(const nfNefMesh &proto, const nfNefBox::Nef &n,
                            sPtr<pigInfo> i = thNULL);
/* ★ sPtr を直に渡せる版 (⚠ @__get()@ は利用禁止 — ひさ 2026-09-20)。 */
inline sPtr<nfNefMesh> nf_make_nef(sPtr<nfNefMesh> proto, const nfNefBox::Nef &n,
                                   sPtr<pigInfo> i = thNULL)
{ sPtr<nfNefMesh> m = proto->make_empty(i); m->box().n = n; return m; }

inline nfNefBox::Nef& nf_nef(sPtr<nfNefMesh> m)                     { return m->box().n; }

#endif

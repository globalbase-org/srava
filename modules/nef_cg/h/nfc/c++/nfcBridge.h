#ifndef ___nfcBridge_h___
#define ___nfcBridge_h___
/*
 * nfcBridge.h — ★★ #3545 段 4: **nef ⇄ cgal の変換の宣言** (CGAL-free)。
 *
 * ⚠⚠ ここには CGAL の型が **1 つも出てきません**。実体は libsrava_cg (nfcBridge.cpp) に在り、
 *   そこだけが nf/c++/nfMeshCgal.h と cg/c++/cgMeshCgal.h の両方を読みます。
 *
 * ---- 置き場所 (★★ #3559 で変わった) ----
 * 変換は **両側の本物のクラス** (nfMeshSnc / cgMesh3D) を同時に要る。#3545 段 4 の時点では
 *   ・libsrava_nf_* に置くと nef が cgal に依存する
 *   ・libsrava_cg に置くと cgal が **CGAL Nef に依存**する (#3440 が明示的に禁じていた)
 * ⇒ どちらにも置けない ⇒ 橋が自分の共有ライブラリ (libsrava_nfcg) を持つ、という形だった。
 * ★★ #3559 で **#3440 の線を畳んだ** (ひさ裁定)。上流が corefinement と Nef をはっきり
 *   分けられない以上、cgal.so 側が Nef 非依存であり続ける代償の方が大きい — 幾何ライブラリが
 *   3 本あるぶん **CGAL の可変大域が 3 個**になり、PE にはそれを畳む機構が無い。
 *   ⇒ 2 つの幾何クラスが同じ libsrava_cg に居るので、この変換もそこへ同居させる。
 *     libsrava_nfcg は畳んだ。
 *
 * ★ 橋の op TU (nfcCast.cpp) が CGAL を 1 枚も引かない形は **そのまま**。段 1/2 と同じで、
 *   @u@ (STB_GNU_UNIQUE) にも dllexport にも依存しない ⇒ **バイナリ形式の差が土俵から消える**。
 *   ⚠ 段 4 の前は「橋は HIDDEN で建てていないので Linux では @u@ のまま」という *柵* に
 *     頼っていた。柵は OS ごとに確かめ直しが要る (mac / Windows に @u@ は無い)。
 */
#include	"pig/c++/pigData.h"

class nfMeshSnc;
class cgMesh3D;

/* Nef (SNC) の厳密境界を cgMesh3D へ **無損失で**移す。
 *   返り 1 = 移せた (*out に新しい cgMesh3D) / 0 = 境界表現が無い (非有界 = complement の結果など)。
 * ★ 中では同じ @CGAL::Surface_mesh<EPECK::Point_3>@ を代入するだけ = 素通し。
 *   ⚠ バイト列を経由しない (経由すると厳密有理数の文字列化で高くつく)。 */
int nfcg_nef_to_cgmesh(sPtr<nfMeshSnc> in, sPtr<cgMesh3D> *out);

#endif

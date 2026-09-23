/*
 * mfatsAgent — Manifold モジュールの実行体 (ptsGenericAgent 派生)。
 *   ★ 状態機械 (WAIT/STARTCALC/CALC/ERROR/FIN) は共通基底 ptsGenericAgent に集約済み。この派生は
 *   **OPS[] 表と記述子だけ**を持ち、agent_ops()/agent_n_ops()/agent_name() を override して基底に渡す。
 *
 * 配線: 通信は自分では持たず親 (ptsObject) に委ねる。agent process では parent=ptsAgentApplication、
 *   planner 内 thread では parent=ptsMediatorInternal。結果もエラーも pigData のまま set_result() して
 *   FIN へ抜けるだけ (ワイヤ符号化は親の役割)。cgatsAgent 版と同一構造 (ともに ptsGenericAgent 派生)。
 */
#include	"pig/c++/ptsObject.h"
#include	"mf/c++/mfMesh.h"
#include	"pig/c++/ptsApplication.h"    /* ptsApp 値メンバの完全型 */
#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/ptsGenericAgent.h"   /* 共通基底 (状態機械) */
#include	"pig/c++/pigAgentRegistry.h"
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/pigOpMatch.h"   /* ★ #3554 段5: 共通のマッチ述語 */
#include	"pig/c++/ptsCalcBody.h"
#include	"mf/c++/mfaBox.h"
#include	"mf/c++/mfaSphere.h"
#include	"mf/c++/mfaIcosphere.h"
#include	"mf/c++/mfaUnion.h"
#include	"mf/c++/mfaHull.h"
#include	"mf/c++/mfaRefine.h"   /* ★ #3512: refine(m,len) */
#include	"mf/c++/mfaSimplifyCleanup.h"   /* ★ #3512 続き: simplify_cleanup(m,tol) */
#include	"mf/c++/mfaMinkowski.h"
#include	"mf/c++/mfaIntersection.h"
#include	"mf/c++/mfaDifference.h"
#include	"mf/c++/mfaExport.h"
#include	"mf/c++/mfaCast.h"
#include	"mf/c++/mfaPolygon.h"
#include	"mf/c++/mfaPrism.h"
/* ★ #3474: 基本立体をカーネル間で統一 */
#include	"mf/c++/mfaPyramid.h"
#include	"mf/c++/mfaCylinder.h"
#include	"mf/c++/mfaCone.h"
#include	"mf/c++/mfaTorus.h"
#include	"mf/c++/mfaTetrahedron.h"
#include	"mf/c++/mfaRevolve.h"
#include	"mf/c++/mfaVolume.h"
#include	"mf/c++/mfaBbox.h"
#include	"mf/c++/mfaTranslate.h"
#include	"mf/c++/mfaRotate.h"
#include	"mf/c++/mfaScale.h"
#include	"mf/c++/mfaMirror.h"
#include	"mf/c++/mfaTransform.h"
#include	"mf/c++/mfaArea.h"
#include	"mf/c++/mfaNverts.h"
#include	"mf/c++/mfaNfaces.h"
#include	"mf/c++/mfaCentroid.h"
#include	"mf/c++/mfaImport.h"
#include	"mf/c++/mfaRect.h"
#include	"mf/c++/mfaCircle.h"
#include	"mf/c++/mfaNgon.h"
#include	"mf/c++/mfaExtrude.h"
#include	"mf/c++/mfaCombine.h"
#include	"mf/c++/mfaSection.h"
#include	"mf/c++/mfaEmpty2D.h"
#include	"mf/c++/mfaEmpty3D.h"
#include	"mf/c++/mfaOffset.h"
#include	"mf/c++/mfaProjectFlatten.h"   /* ★ #3534: z=0 への直投影 */
#include	"mf/c++/mfaLoftRuled.h"
#include	"mf/c++/mfaTube.h"
#include	"mf/c++/mfaColor.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfatsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(mf/c++/mfatsAgent,pig/c++/ptsGenericAgent)

/* ---- ディスパッチテーブル (ファイルスコープ・pig 層の共通型) ---- */

static const pigArgKind SHAPE3_IN[] = { AK_INLINE, AK_INLINE, AK_INLINE };  /* box/prism/pyramid */
static const pigArgKind SHAPE2_IN[] = { AK_INLINE, AK_INLINE };             /* rect(w,h) 2D */
static const pigArgKind SHAPE1_IN[] = { AK_INLINE };                        /* sphere(r) / boxa([..]) */
static const pigArgKind EXPORT_IN[] = { AK_INLINE, AK_CACHE, AK_INLINE };  /* export(path, mesh, unit) */
static const pigArgKind BINMESH_IN[] = { AK_CACHE, AK_CACHE };  /* 2 mesh 入力(cache ハンドル→reader 読み) */
static const pigArgKind CAST_IN[] = { AK_INLINE, AK_CACHE };  /* cast(type_string, mesh): type=inline, mesh=cache(reader) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };  /* 計測(値返し): mesh 1 個入力 */
static const pigArgKind REVOLVE_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE };  /* revolve(cross, angle, segs) / rotate(m,axis,deg) */
static const pigArgKind SECTION_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };  /* section(m,P,N,mode) */
static const pigArgKind MESH1ARG_IN[] = { AK_CACHE, AK_INLINE };  /* translate/scale/mirror/transform(m, param) */

/* ★★ #3554 段5 (2026-09-19): **xy 平面に帰着する変換は 2D のまま返す** (cgal と同型)。
 *   判定本体は共通述語 (pig/c++/pigOpMatch.h)。モジュール側は「何番を見るか」だけを書く。
 *   ⚠ rotate は **軸が z のときだけ** — マッチ関数は引数を 1 個ずつしか見られないので、
 *     *軸と角度の両方*で決まる「平面を保つか」は判定できない (保守的に face3d のまま)。
 *     ★ 計算本体も同じ判定を使うこと (行列で見ると sig と実型が食い違う)。 */
static int
mf_match_xform_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_keeps_xy(arg); }
static int
mf_match_trans_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_translate_keeps_xy(arg); }
static int
mf_match_scale_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_scale_keeps_xy(arg); }
static int
mf_match_mirror_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_mirror_keeps_xy(arg); }
static int
mf_match_rot_z(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_axis_is_z(arg); }
/* transform 系: 入力 mesh(cache)1 個 + スカラ/構造(inline)。mesh は reader、残りは value-parse。 */
static const pigOpEntry OPS[] = {
	{ "box",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(mfaBox),          0, "->mf-mesh3d" },  /* leaf 3D */
	{ "boxa",         SHAPE1_IN, 1, AK_CACHE, OPWIRE(mfaBox), 0, "->mf-mesh3d" },  /* 寸法を array(構造 inline)で */
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaSphere), 0, "->mf-mesh3d", 0, 0, 1 },  /* sphere(r, seg): seg=円周分割数(既定 32 相当) */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	{ "icosphere",    SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaIcosphere), 0, "->mf-mesh3d", 0, 0, 1 },  /* icosphere(r, subdiv): subdiv=細分回数(既定0=20面) */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	{ "union",        BINMESH_IN,2, AK_CACHE, OPWIRE(mfaUnion, mfGeom, mfGeom),        1, "[mf-mesh3d,gg-mesh3d,ch-mesh3d](*)->mf-mesh3d;[mf-cross2d](*)->mf-cross2d;[mf-face3d,mf-cross2d](*)->mf-face3d", 1 /* ★可換 */ },
	{ "intersection", BINMESH_IN,2, AK_CACHE, OPWIRE(mfaIntersection, mfGeom, mfGeom), 1, "[mf-mesh3d,gg-mesh3d,ch-mesh3d](*)->mf-mesh3d;[mf-cross2d](*)->mf-cross2d;[mf-face3d,mf-cross2d](*)->mf-face3d", 1 /* ★可換 */ },
	{ "difference",   BINMESH_IN,2, AK_CACHE, OPWIRE(mfaDifference, mfGeom, mfGeom), 1, "[mf-mesh3d,gg-mesh3d,ch-mesh3d](*)->mf-mesh3d;[mf-cross2d](*)->mf-cross2d;[mf-face3d,mf-cross2d](*)->mf-face3d" },
	/* ★ #3511: 凸包。**1 個でも受ける**ので nin=1 + variadic=1 (union 系は nin=2)。
	 *   sig は 1 項 = 固定形・2 項以上 = fold 形の 2 本立て (fold 形の下限は 2 なので
	 *   1 項を fold 形では書けない)。3D/2D とも Manifold が Hull を持っている。
	 *   ★ 可換 = 1: 点集合の和集合を取るだけなので順序に依らない。 */
	/* ★★ #3528: **"(*!)" = 分解禁止**。hull は入力から **頂点しか使わない**ので、木に分解すると
	 *   「点 → メッシュを作って cache へ書き、読み戻して面を捨てて頂点に戻す」を段ごとに繰り返す
	 *   = 作ったものを次の段で捨てる。⚠⚠ それ以前に **落ちうる** — 退化検査は部分集合について
	 *   閉じていないので、全体が立体でも群が同一平面になると「立体にならない」で明示エラーになる。
	 *   ⇒ 主型による振り分け (fold 形) は保ったまま、分解だけを止める。 */
	{ "hull",         MEASURE_IN,1, AK_CACHE, OPWIRE(mfaHull, mfGeom),        1, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-cross2d;(mf-face3d)->mf-face3d"
	                                                 /* ★★ #3533: **全部が z=0 なら 2D・1 つでも空間に出れば 3D 立体**。
	                                                  *   ⚠ 行の順序が意味を持つ (先に一致した行が採られる)。 */
	                                                 ";[mf-mesh3d,gg-mesh3d,ch-mesh3d,mf-face3d,mf-cross2d](*!)->mf-mesh3d"
	                                                 ";[mf-face3d,mf-cross2d](*!)->mf-mesh3d"
	                                                 ";[mf-cross2d](*!)->mf-cross2d", 1 /* ★可換 */ },
	/* ★★ #3511: Minkowski 和。**manifold は凸分解を使わない** (三角形ごとの凸包を
	 *   BatchBoolean で畳む) ので、nef の CGAL::minkowski_sum_3 とは別物の速さになる。
	 *   ⚠ sig は **自型 (mf,mf) だけ**。foreign を書かない理由:
	 *     nef の minkowski が cg/mf/gg の全 16 組を all-foreign で名乗っており、そこへ
	 *     manifold が同じ組を書くと **同じ入力型の組を 2 モジュールが名乗る**。priority で
	 *     決まってしまい「どちらが計算したか」が module() の書き順に依存する。
	 *     ⇒ mf 自型だけを名乗り、混成は従来どおり nef が受ける。
	 *   ⚠ それでも **(mf,mf) の既定の行き先は nef から manifold へ変わる** (priority 10 > 5)。
	 *     nef に行かせたいときは "nef_snc"::minkowski(...) と名指しする。
	 *   ⚠ 2D は無い (CrossSection に Minkowski が無いので mf-cross2d は書かない)。 */
	{ "minkowski",    BINMESH_IN,2, AK_CACHE, OPWIRE(mfaMinkowski, mfGeom, mfGeom), 0, "(mf-mesh3d,mf-mesh3d)->mf-mesh3d" },
	/* ★★ #3554 最後の段 4/5 (2026-09-19): export の行は共通述語 @pig_match_export_ext@ が選ぶ
	 *   (= 第 1 引数の拡張子を **d->export_exts** が書けるか)。出力は常に @ref@ なので
	 *   **行を分ける必要は無い** (cast / import と違うのはここ)。
	 *   ⚠⚠ 同時に **規約① (自型優先) を撤去**した — 「入力型の home カーネルが書けるならそこ」
	 *     という routing の特例で、sig でも記述子でもない *3 つめの規則* だった。
	 *     ⇒ いまは priority × sig × 拡張子 の普通の決着。 */
	{ "export",       EXPORT_IN, 3, AK_CACHE, OPWIRE(mfaExport, mfGeom), 0, "(mf-mesh3d)->ref;(mf-cross2d)->ref;(mf-face3d)->ref", 0, 0, 0, &pig_match_export_ext },  /* 出力=D_REF キャッシュ */
	/* ★★ #3554 最後の段 2/5 (2026-09-19): cast は **目標型ごとに 1 行** (cgal と同じ形)。
	 *   行を選ぶのは共通述語 @pig_match_cast_target@。⚠ 1 行に出力型を 2 つ書くと、行の可否
	 *   (どれかの sigline が目標型を産むか) と実際に名乗る型 (入力型で先に当たった sigline) が
	 *   食い違う ⇒ ロード時に pig_descriptor_violation が弾く。
	 *   ★ 計算本体は identity で 3 行とも同じ。cg→mf downgrade は mf_codecs の mf-cg-downgrade
	 *     codec (MESH→mf-mesh3d / PLY2→mf-cross2d) が担う。 */
	{ "cast#mf-mesh3d", CAST_IN, 2, AK_CACHE, OPWIRE(mfaCast, mfGeom),         0,
	                                                  "(mf-mesh3d)->mf-mesh3d;(cg-mesh3d)->mf-mesh3d"
	                                                  ";(gg-mesh3d)->mf-mesh3d;(ch-mesh3d)->mf-mesh3d"     /* geogram/cherchi は MFM3 を名乗る */
	                                                  ";(nfb-mesh3d)->mf-mesh3d"     /* mf-nf-downgrade: NEFB。★ #3499: nf-mesh3d (nef_snc) は橋 nef_mf.so が受ける */
	                                                  /* ★★ #3527: geomutils の型。gu-mesh3d=MFM3 / gu-cross2d・gu-face3d=MFC2 を書くので
	                                                   *   mfGeom::create_for_meta が **native で読める** ⇒ 足すのは sig の行だけ。 */
	                                                  ";(gu-mesh3d)->mf-mesh3d", 0, 0, 0, &pig_match_cast_target },
	{ "cast#mf-cross2d", CAST_IN, 2, AK_CACHE, OPWIRE(mfaCast, mfGeom),        0,
	                                                  "(mf-cross2d)->mf-cross2d;(cg-cross2d)->mf-cross2d;(gu-cross2d)->mf-cross2d"
	                                                  /* ★★ #3533 規約②: **降格は cast だけ**。@frame_is_default()@ が偽なら明示エラー。 */
	                                                  ";(mf-face3d)->mf-cross2d;(cg-face3d)->mf-cross2d;(gu-face3d)->mf-cross2d",
	                                                  0, 0, 0, &pig_match_cast_target },
	{ "cast#mf-face3d", CAST_IN, 2, AK_CACHE, OPWIRE(mfaCast, mfGeom),         0,
	                                                  "(mf-face3d)->mf-face3d;(cg-face3d)->mf-face3d;(gu-face3d)->mf-face3d",   /* ★ #3533: face3d も identity / 橋渡し */
	                                                  0, 0, 0, &pig_match_cast_target },
	{ "polygon",      SHAPE1_IN, 1, AK_CACHE, OPWIRE(mfaPolygon), 0, "->mf-cross2d" },  /* polygon([[x,y]...]): 2D 断面 */
	{ "prism",        SHAPE3_IN, 3, AK_CACHE, OPWIRE(mfaPrism), 0, "->mf-mesh3d" },  /* prism(n,h,r) */
	/* ★ #3474: 基本立体はカーネル差が出ないので全カーネルに置く (common/solids.h)。 */
	{ "pyramid",       SHAPE3_IN, 3, AK_CACHE, OPWIRE(mfaPyramid), 0, "->mf-mesh3d" },  /* pyramid(n,h,r) */
	{ "cylinder",      SHAPE3_IN, 3, AK_CACHE, OPWIRE(mfaCylinder), 0, "->mf-mesh3d", 0, 0, 2 },  /* cylinder(r,h,seg) */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "cone",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(mfaCone), 0, "->mf-mesh3d", 0, 0, 2 },  /* cone(r,h,seg) */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "torus",         SHAPE3_IN, 3, AK_CACHE, OPWIRE(mfaTorus), 0, "->mf-mesh3d", 0, 0, 2 },  /* torus(R,r,seg) */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "tetrahedron",   SHAPE1_IN, 1, AK_CACHE, OPWIRE(mfaTetrahedron), 0, "->mf-mesh3d" },  /* tetrahedron(r) */
	{ "revolve",      REVOLVE_IN,3, AK_CACHE, OPWIRE(mfaRevolve, mfGeom), 0, "(mf-cross2d)->mf-mesh3d;(mf-face3d)->mf-mesh3d", 0, 0, 1 },  /* revolve(cross,angle,segs): 2D→3D */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	{ "volume",       MEASURE_IN,1, AK_INLINE,OPWIRE(mfaVolume, mfGeom),       0, "(mf-mesh3d)->value" },
	/* ★★ #3527 段 3: **3D (mf-mesh3d) の行は geomutils.so (gu) へ寄せた**。中身は元から
	 *   src/h/common/meshprops.h (定義ごと 1 本) を通っていたので答えは変わらない
	 *   — 寄せる前/後の bit 一致を実測で確認した (2026-09-17)。
	 *   ⚠ **2D はここに残す** (ひさ判断): mf が既に持つ 5 本と、mf に無い 6 本は互いに素
	 *     なので、gu は *mf が持たない分だけ* 名乗る ⇒ 重複ゼロで両立する。
	 *   ⚠ 3D の行を書き戻さないこと。2 モジュールが同じ (op, 型) を名乗ると priority で
	 *     答えが決まる = **構成で答えが変わる**。 */
	{ "bbox",         MEASURE_IN,1, AK_INLINE,OPWIRE(mfaBbox, mfGeom), 0, "(mf-cross2d)->value;(mf-face3d)->value" },  /* bbox(mesh): 値返し */
	/* ★ #3512: refine(m,len) — 形を厳密に変えずに面密度だけ上げる (RefineToLength)。
	 *   ⚠ **remesh ではない** — 頂点を動かさないので針状三角形は割られても針状のまま。
	 *   cgal 版と同じ約束 (形不変・全辺 len 以下) だが、作り方が違うので面数と三角形の形は
	 *   一致しない (docs の各節に実測値)。3D 専用 = CrossSection に対応物が無い。 */
	{ "refine",       MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaRefine, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d" },
	/* ★ #3512 続き: simplify_cleanup(m,tol) — **面が tol 未満しか動かない**範囲で潰せるだけ潰す。
	 *   ⚠ cgal の simplify(m,n) (面数を指定・形は保つ) とは約束が逆向き (こちらは形のずれの
	 *     上限を指定して面数は成り行き) なので **名前を分けてある** = 「元の op 名 + _修飾」。 */
	{ "simplify_cleanup", MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaSimplifyCleanup, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d" },
	{ "translate#xy", MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaTranslate, mfGeom), 0, "(mf-cross2d)->mf-cross2d", 0, 0, 0, &mf_match_trans_xy },
	{ "translate",    MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaTranslate, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-face3d;(mf-face3d)->mf-face3d" },  /* translate(m, [x,y,z]) */
	{ "rotate#z", REVOLVE_IN, 3, AK_CACHE,OPWIRE(mfaRotate, mfGeom), 0, "(mf-cross2d)->mf-cross2d", 0, 0, 0, &mf_match_rot_z },
	{ "rotate",       REVOLVE_IN, 3, AK_CACHE,OPWIRE(mfaRotate, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-face3d;(mf-face3d)->mf-face3d" },  /* rotate(m, axis, deg) */
	{ "scale#xy", MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaScale, mfGeom), 0, "(mf-cross2d)->mf-cross2d", 0, 0, 0, &mf_match_scale_xy },
	{ "scale",        MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaScale, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-face3d;(mf-face3d)->mf-face3d" },  /* scale(m, s | [sx,sy,sz]) */
	{ "mirror#xy", MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaMirror, mfGeom), 0, "(mf-cross2d)->mf-cross2d", 0, 0, 0, &mf_match_mirror_xy },
	{ "mirror",       MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaMirror, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-face3d;(mf-face3d)->mf-face3d" },  /* mirror(m, axis) */
	{ "transform#xy", MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaTransform, mfGeom), 0, "(mf-cross2d)->mf-cross2d", 0, 0, 0, &mf_match_xform_xy },
	{ "transform",    MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaTransform, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-face3d;(mf-face3d)->mf-face3d" },  /* transform(m, matrix12/16) */
	/* ★ #3443: 頂点数 / 面数 (planner の表示を op へ移した)。 */
	{ "nverts",       MEASURE_IN,1, AK_INLINE,OPWIRE(mfaNverts, mfGeom), 0, "(mf-cross2d)->value;(mf-face3d)->value" },
	{ "nfaces",       MEASURE_IN,1, AK_INLINE,OPWIRE(mfaNfaces, mfGeom), 0, "(mf-cross2d)->value;(mf-face3d)->value" },
	/* ★ 2026-08-19: (mf-cross2d) を申告に追加。実装は元から 2D 断面の面積を返せていたが sig に
	 *   書かれておらず、routing の fallback (入力型の home module へ配送) に助けられて動いていた。
	 *   fallback を撤去したら「sig に無い = 実行できない」で落ちた = **宣言漏れ**が露出した。 */
	{ "area",         MEASURE_IN,1, AK_INLINE,OPWIRE(mfaArea, mfGeom), 0, "(mf-cross2d)->value;(mf-face3d)->value" },  /* area(mesh|cross): 値返し */
	{ "centroid",     MEASURE_IN,1, AK_INLINE,OPWIRE(mfaCentroid, mfGeom), 0, "(mf-cross2d)->value;(mf-face3d)->value" },  /* centroid(mesh): 配列返し */
	/* ★ #3514: **位相を直接数える** 3 本。これまで位相の健全性は「シェルを 1 枚失えば体積が
	 *   100 倍ずれる」という *体積を代理指標に使う* 判定しかできていなかった
	 *   (wiki Srava_kernel_sweep_20260911-2 §5.4)。代理をやめる。
	 *     nshells(m)  境界シェル = 面の連結成分の枚数   球 1 ・中空の箱 2 ・xor の N 球 N
	 *     nparts(m)   塊 = 立体の連結成分の数           球 1 ・中空の箱 1 ・xor の N 球 N/2
	 *     genus(m)    種数 = 取っ手の総数               球 0 ・トーラス 1 ・中空の箱 0
	 *   ★ nparts は **nef の nparts と同じ約束** (SNC の marked volume = 塊)。ライブラリが直接
	 *     くれるのはシェルの方なので、符号つき体積が正のシェルを数えて塊に直している
	 *     (定義と根拠は src/h/common/meshprops.h)。
	 *   ⚠ nef 版と違って **変換を挟まない** — これが本題。掃引規模のメッシュを Nef へ通すと
	 *     100GB 級になる (#3510 の掃引) ので、そこでは nef の nparts は事実上使えなかった。 */
	/* ★ #3554 最後の段 3/5 (2026-09-19): import の行は共通述語 @pig_match_import_ext@ が選ぶ
	 *   (拡張子が産む型 = @d->import_exts@ の型付き CSV が、**この行の sig の出力型**か)。
	 *   ⚠ 出力型が拡張子で決まるので、*sig だけでは行が決まらない* のが import の特徴。
	 *   ★ このカーネルは import の出力型が 1 つなので **行を分ける必要は無い**。 */
	{ "import",       SHAPE1_IN, 1, AK_CACHE, OPWIRE(mfaImport), 0, "->mf-mesh3d", 0, 0, 0, &pig_match_import_ext },  /* import(path): STL/OFF */
	{ "rect",         SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaRect),         0, "->mf-cross2d" },  /* leaf 2D */
	{ "circle",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaCircle), 0, "->mf-cross2d", 0, 0, 1 },  /* circle(r,segs): 2D */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	{ "ngon",         SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaNgon), 0, "->mf-cross2d" },  /* ngon(n,r): 2D */
	{ "extrude",      MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaExtrude, mfGeom),      0, "(mf-cross2d)->mf-mesh3d;(mf-face3d)->mf-mesh3d" },  /* 2D→3D。★ #3533: 置かれた断面も押し出せる */
	/* ★★ #3511: **loft_ruled** — 断面の列を直線で結ぶ立体。@extrude@ / @revolve@ / @tube@ が
	 *   断面 1 枚なのに対し、*断面が複数枚で形が変わってよい* のがこれ。
	 *   ★ 断面の置き場所は op が決めない (利用者が transform で空間に置く・#3526 の枠)。
	 *   ★ sig は **繰り返し形 + "[]"** (#3511): 2 枚以上を並べても配列 1 個でも渡せる。
	 *     ⚠ **fold 形にはできない** — loft_ruled(loft_ruled(a,b),c) は loft_ruled(a,b,c) と
	 *       別物 (断面の列は分解できない) なので、木に分解してはいけない。
	 *   ★ なめらかな @loft@ は置かない (解析曲面が要る = occt だけ)。 */
	{ "loft_ruled",   0,          0, AK_CACHE,OPWIRE(mfaLoftRuled, mfGeom), 1, "{mf-cross2d,mf-face3d}...[]->mf-mesh3d" }   /* ★ #3533: 断面は 2 型が混ざるので 1 つの集合で受ける */,
	{ "combine",      BINMESH_IN,2, AK_CACHE, OPWIRE(mfaCombine, mfGeom, mfGeom), 0, "[mf-mesh3d,gg-mesh3d,ch-mesh3d](2)->mf-mesh3d;[mf-cross2d](2)->mf-cross2d;[mf-face3d,mf-cross2d](2)->mf-face3d", 1 /* ★可換 */ },  /* combine(a,b) */
	{ "section",      SECTION_IN,4, AK_CACHE, OPWIRE(mfaSection, mfGeom), 0, "(mf-mesh3d)->mf-face3d" },  /* section(mesh,P,N,mode): 3D→2D(Z)。★ #3533: 断面は常に face3d */
	{ "empty2d",      0,         0, AK_CACHE, OPWIRE(mfaEmpty2D), 0, "->mf-cross2d" },  /* 空集合(2D)。{} は中立元なので別物 */
	{ "empty3d",      0,         0, AK_CACHE, OPWIRE(mfaEmpty3D), 0, "->mf-mesh3d" },   /* 空集合(3D) */
	{ "offset",       REVOLVE_IN,3, AK_CACHE, OPWIRE(mfaOffset, mfGeom),       0, "(mf-cross2d)->mf-cross2d;(mf-face3d)->mf-face3d", 0, 0, 2 },  /* ★2D 専用 */  /* ★ nreq=2: 以降は省略可 (既定は op が入れる) */
	/* ★★ #3534: **project_flatten** — world の (x,y) を取り z を捨てる = z=0 への直投影。
	 *   説明は cgatsAgent.cpp の同じ行を参照 (同じ規約の 2 実装・⚠ 片方だけ直さないこと)。 */
	{ "project_flatten", MEASURE_IN,1, AK_CACHE, OPWIRE(mfaProjectFlatten, mfGeom), 0, "(mf-face3d)->mf-cross2d;(mf-cross2d)->mf-cross2d" },
	{ "tube_ruled",         SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaTube),        0, "->mf-mesh3d;->mf-cross2d", 0, 0, 1 },  /* tube(path, segs): 折れ線まわりの掃引管。次元は path 頂点の長さで決まる (#3415・掃引は cgal と共通の common/tube.h) */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	{ "color",        MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaColor, mfGeom),       0, "(mf-mesh3d)->mf-mesh3d" },  /* color(m, c): 頂点プロパティ ch3..5 に RGB。3D 専用 (2D は cgal 同様エラー)。色つき export は 3MF/AMF */
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfatsAgent_(
		sPtr<ptsObject> parent);

protected:
	/* 基底 ptsGenericAgent の generic 状態機械へ OPS 表 / 名前を渡すだけ (状態機械は書かない)。 */
	virtual const pigOpEntry*	agent_ops();
	virtual int			agent_n_ops();
	virtual const char*		agent_name();
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"pig/c++/pigOpEntry.h"
class ptsObject;
TS_END_INTERFACE

#endif


mfatsAgent_::mfatsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	mfatsAgent_::agent_ops()   { return OPS; }
int			mfatsAgent_::agent_n_ops() { return N_OPS; }
const char*		mfatsAgent_::agent_name()  { return "manifold"; }

/* この実行体を "manifold" として登録する。root は具体クラスを知らず enable()/make_agent で起こす。 */
static sPtr<ptsAgent>
mk_mfatsAgent(sPtr<ptsObject> med)
{
	return thNEW(mfatsAgent,(med));
}

/* 自己申告記述子。ops は上の実 OPS[] を再エクスポート (単一ソース)。
 * ★ priority=10。**既定カーネルは cgal (20)** であって manifold ではない
 *   (2026-08-06 に一時 manifold を既定にしたが、その後 cgal 20 > manifold 10 に戻した。
 *    manifold を使うなら cast("manifold",..) / module("manifold.so",{priority}) で明示選択)。
 * namespace scope の const は既定で内部リンケージなので manifest.cpp から extern 参照するため extern 明示。 */
extern const pigModuleType manifold_provides[];
extern const srava_module_descriptor mfatsAgent_descriptor;
extern const srava_module_descriptor mfatsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "manifold",
	.priority      = 10,
	.make_agent    = &mk_mfatsAgent,
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_THREAD,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = "stl:mf-mesh3d,off:mf-mesh3d",   /* rev4 Phase C: 型付き (3D) */
	.export_exts   = "stl,off,3mf,amf",   /* 色つき 3MF/AMF も (共通ライタ common/mesh3mf.h) */
	.provides      = manifold_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	/* ★ v18 (#3466): このモジュールが出す結果の版。**計算を変えたら手で上げる**。 */
	.cache_version = 7,   /* ★ #3527 段 6: v7 = **cg-face3d を読むときに枠を拾う**ようになった。
	                       *   decode_cross_exact が regions で止まっており、
	                       *   cast("mf-face3d", <cg-face3d>) が **枠を黙って捨てて z=0 へ戻して**
	                       *   いた (#3526 で mf→cg は直したが cg→mf が残っていた)。
	                       *   ⇒ 同じ式の結果が変わるので版を上げる。
	                       * ★ #3533: v6 = **2D の型が 2 つ (mf-cross2d / mf-face3d)**。face3d の
	                       *   ブロブは枠が既定でも枠の節を書く (型を自分で名乗るため) ので、
	                       *   同じ式でも v5 とバイト列が変わる。⚠ v5 を引くと transform を通った
	                       *   2D が cross2d に戻り hull の振り分けが変わる ⇒ 引かせてはいけない。
	                       * v5 (#3529) = **2D の真実が輪郭列**になった。decode が Clipper2 の
	                       *   Union を通らなくなり、*同じ式が cache hit と miss で同じ値*を返す。
	                       *   ⚠ blob の形式は変わらないが **同じ blob から違う答え**が出るように
	                       *     なったので版を分ける (古い下流キャッシュ = 量子化済みの面積・
	                       *     体積・頂点数と混ざらないようにするため)。
	                       * v4 (#3526) = **section が切った場所に断面を返す** (枠つきで返る)。
                       *   ⚠ v3 のキャッシュには z=0 に落ちた断面が入っているので引けない。
                       * v3 = 2D が枠 (平面) を持つ。空間に置いた 2D は旧バイナリだと
	                       *   黙って z=0 に戻るので版を上げる。⚠ 平面の 2D の blob は従来と同じ
	                       *   バイト列 (枠が既定なら節を書かない) だが、版は分ける。
	                       * v2 (#3487): valid が自己交差も見る (共通定義 ③) */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};
/* ★ #3427 ③: 旧・静的初期化の register_descriptor は撤去。登録は dlopen 経路
 * (pigModuleRegistry::load_file → register_descriptor) の 1 本 = app 所有レジストリへ。 */

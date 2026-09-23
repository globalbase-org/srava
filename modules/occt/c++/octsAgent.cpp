/*
 * octsAgent — OCCT (B-rep) モジュールの実行体 (#3437 P5)。
 * 状態機械は共通基底 ptsGenericAgent が持つので、ここは OPS 表と記述子だけ。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/ptsGenericAgent.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/pigOpMatch.h"   /* ★ #3554 段5c: 共通のマッチ述語 */
#include	"pig/c++/pigModule.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"oc/c++/ocShape.h"
#include	"oc/c++/ocaBox.h"
#include	"oc/c++/ocaSphere.h"
#include	"oc/c++/ocaUnion.h"
#include	"oc/c++/ocaIntersection.h"
#include	"oc/c++/ocaDifference.h"
#include	"oc/c++/ocaPtIntersection.h"   /* ★ #3581 */
#include	"oc/c++/ocaPtDifference.h"     /* ★ #3581 */
#include	"pt/c++/ptCloud.h"
#include	"oc/c++/ocaOffset.h"
#include	"oc/c++/ocaOffsetThicken.h"   /* ★ #3547 ② */
#include	"oc/c++/ocaVert.h"            /* ★ #3547 ④ */
#include	"oc/c++/ocaVerts.h"           /* ★ #3547 ④ */
#include	"oc/c++/ocaFaceVerts.h"       /* ★ #3547 ④ */
#include	"oc/c++/ocaVolume.h"
#include	"oc/c++/ocaBbox.h"
#include	"oc/c++/ocaCentroid.h"
#include	"oc/c++/ocaValid.h"
#include	"oc/c++/ocaCylinder.h"
#include	"oc/c++/ocaTorus.h"
#include	"oc/c++/ocaTube.h"
#ifdef SRAVA_OCCT_TEXT
#include	"oc/c++/ocaText.h"
#endif
#include	"oc/c++/ocaExtrude.h"
#include	"oc/c++/ocaRevolve.h"
#include	"oc/c++/ocaPrism.h"
/* ★ #3474: 基本立体をカーネル間で統一 */
#include	"oc/c++/ocaPyramid.h"
#include	"oc/c++/ocaCone.h"
#include	"oc/c++/ocaTetrahedron.h"
#include	"oc/c++/ocaIcosphere.h"
/* ★ #3474 続き: 2D プリミティブ (occt は 2D 型を持ちながら text でしか作れなかった) */
#include	"oc/c++/ocaRect.h"
#include	"oc/c++/ocaNgon.h"
#include	"oc/c++/ocaPolygon.h"
#include	"oc/c++/ocaSurfaceControl.h"   /* ★ #3532: 格子を制御点として自由曲面 */
#include	"oc/c++/ocaSurfaceThrough.h"   /* ★ #3532: 格子を通過点として自由曲面 */
#include	"oc/c++/ocaPoles.h"            /* ★ #3532: 制御網の取り出し */
#include	"oc/c++/ocaSetPoles.h"         /* ★ #3532: 制御網の差し替え */
#include	"oc/c++/ocaCast.h"            /* ★ #3544: 2D の名乗りを下げる (規約②) */
#include	"oc/c++/ocaHlr.h"             /* ★ #3544 段 2: 陰線処理 */
#include	"oc/c++/ocaNedges.h"          /* ★ #3544 段 2: 稜の本数 */
#include	"oc/c++/ocaCircle.h"
#include	"oc/c++/ocaEmpty2D.h"
#include	"oc/c++/ocaEmpty3D.h"
#include	"oc/c++/ocaArea.h"
#include	"oc/c++/ocaFillet.h"
#include	"oc/c++/ocaChamfer.h"
#include	"oc/c++/ocaExport.h"
#include	"oc/c++/ocaImport.h"
#include	"oc/c++/ocaNfaces.h"
#include	"oc/c++/ocaFace.h"
#include	"oc/c++/ocaFaceAt.h"
#include	"oc/c++/ocaSurfaceType.h"
#include	"oc/c++/ocaProject.h"
#include	"oc/c++/ocaProjectFlatten.h"   /* ★ #3534: z=0 への直投影 */
#include	"oc/c++/ocaUnifyFaces.h"
#include	"oc/c++/ocaLoft.h"
#include	"oc/c++/ocaLoftRuled.h"
#include	"oc/c++/ocaNverts.h"
#include	"oc/c++/ocaTranslate.h"
#include	"oc/c++/ocaDistanceAt.h"   /* ★ #3514: 点との距離 */
#include	"oc/c++/ocaSection.h"      /* ★ #3514: 断面 */
#include	"oc/c++/ocaRotate.h"
#include	"oc/c++/ocaScale.h"
#include	"oc/c++/ocaMirror.h"
#include	"oc/c++/ocaTransform.h"
#include	"_ts2/c++/octsAgent_.h"

CLASS_TINYSTATE(oc/c++/octsAgent,pig/c++/ptsGenericAgent)

template <class T>
static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> parent, sArray<sPtr<pigData> > *args, sPtr<stdString> target)
{
	return sPtr<ptsCalcBody>::d_cast(thNEW(T,(parent, args, target)));
}

static const pigArgKind SHAPE3_IN[]  = { AK_INLINE, AK_INLINE, AK_INLINE };  /* box(w,h,d) */
/* ★★ #3570 段3 (2026-09-21): 以前は @SHAPE2_IN@ 1 本を sphere/tube/rect/ngon/
 *   surface_through/circle/icosphere で共用し、コメントは @sphere(r,seg)@ のままだった。
 *   ⇒ **行ごとに名前を分ける** (共用すると「この行が何を取るのか」が読めない)。 */
static const pigArgKind SHAPE1V_IN[] = { AK_INLINE };                        /* sphere(r) / circle(r) */
static const pigArgKind SHAPE2V_IN[] = { AK_INLINE, AK_INLINE };             /* cylinder(r,h) / cone(r,h) / torus(R,r) */
static const pigArgKind RECT_IN[]    = { AK_INLINE, AK_INLINE };             /* rect(w,h) */
static const pigArgKind NGON_IN[]    = { AK_INLINE, AK_INLINE };             /* ngon(n,r) */
static const pigArgKind SURFTH_IN[]  = { AK_INLINE, AK_INLINE };             /* surface_through(pts[,opts]) */
static const pigArgKind ICOSPH_IN[]  = { AK_INLINE, AK_INLINE };             /* icosphere(r[,subdiv]) */
static const pigArgKind TUBE_IN[]    = { AK_INLINE, AK_INLINE };             /* tube(path[,{closed:1}]) */
static const pigArgKind REVOLVE_IN[] = { AK_CACHE,  AK_INLINE };             /* revolve(cross2d[,deg]) */
#ifdef SRAVA_OCCT_TEXT
/* ★ #3471: text(fontPath, str, size) — 3 つとも値引数 (幾何 cache 引数は無い = leaf)。 */
static const pigArgKind TEXT_IN[]    = { AK_INLINE, AK_INLINE, AK_INLINE };
#endif
static const pigArgKind BINSHAPE_IN[]= { AK_CACHE, AK_CACHE };               /* 2 shape 入力 */
/* ★ #3581: 点群 x 形。**幾何が 2 つ** = sig に 2 つ現れる (値引数 mode は sig に出ない)。 */
static const pigArgKind PTSPLIT3_IN[] = { AK_CACHE, AK_CACHE, AK_INLINE };  /* intersection(A,M,mode) */
static const pigArgKind PTSPLIT2_IN[] = { AK_CACHE, AK_CACHE };             /* difference(A,M) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };                         /* shape 1 個 */
static const pigArgKind OFFSET_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };   /* offset(s,d,unused) */
static const pigArgKind SHAPE1_IN[]  = { AK_INLINE };                        /* import(path) */
/* ★ #3544: cast(型名, 2D)。型名は inline・値は cache (他カーネルの cast と同じ並び)。 */
static const pigArgKind CAST_IN[]    = { AK_INLINE, AK_CACHE };
/* ★ #3544 段 2: hlr(solid, dir[, up][, mode])。省略できる 2 つは **literal の種類**で分ける
 *   (3 要素配列 = up ・ 文字列 = mode) ⇒ ocaHlr.cpp の注記。nreq=2。 */
static const pigArgKind HLR_IN[]     = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };
static const pigArgKind EDIT_IN[]    = { AK_CACHE, AK_INLINE };              /* fillet(s,r) / chamfer(s,d) */
static const pigArgKind EXPORT_IN[]  = { AK_INLINE, AK_CACHE, AK_INLINE };   /* export(path, s, unit) */
/* ★ #3461: 変換 op。第 1 引数が shape・第 2 (と第 3) がパラメータ。manifold と同じ形。 */
static const pigArgKind XFORM1_IN[]  = { AK_CACHE, AK_INLINE };              /* translate/scale/mirror/transform(s, p) */
static const pigArgKind XFORM2_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };   /* rotate(s, axis, deg) */

/* ★★ #3554 段5c (2026-09-19): **xy 平面に帰着する変換は 2D のまま返す** (cgal / manifold と同型)。
 *
 *   ---- ⚠ occt だけ事情が違う ----
 *   occt は **名乗りを幾何から導く** (@ocShape.h@ の @type_name()@ = @on_z0_plane()@)。
 *   ⇒ *値* は元から「z=0 に居れば cross2d」と答えていた。足りなかったのは **sig のほう**で、
 *     規約① のまま @(oc-cross2d)->oc-face3d@ と宣言していたため
 *     **sig は face3d・値は cross2d** という食い違いが残っていた
 *     (@ocShape.h@ が「代償は 1 か所だけ」と書いていたのがこれ)。
 *   ⇒ 変種行を足すと *sig が値に追いつく*。★ **計算本体は 1 行も触らない**。
 *
 *   ⚠⚠ @rotate(r,"x",180)@ の食い違いは **残る**: routing は軸しか見られないので基底行
 *     (face3d) を選ぶが、幾何は z=0 に留まるので値は cross2d を名乗る。cg / mf では計算本体を
 *     軸判定に揃えて消せたが、occt は *幾何から導く* ので揃えられない (幾何は本当に z=0 に居る)。
 *     ⇒ **承知の上の 1 か所**として残す。 */
static int
oc_match_xform_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_keeps_xy(arg); }
static int
oc_match_trans_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_translate_keeps_xy(arg); }
static int
oc_match_scale_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_scale_keeps_xy(arg); }
/* ★★ #3570 段3: @tube@ の第 2 引数は **ハッシュ** (@{closed:1}@) のときだけ occt が受ける。
 *   #3555 段4a で @tube@ は *occt 単独 op* になり (他カーネルは @tube_ruled@)、
 *   #3530 の「同じ式が cg/mf で落ちて occt で通るのを防ぐ」理由が消えた。
 *   ⇒ @tube(path, 24)@ (segs のつもり) は occt の行が成立せず、候補から外れる。
 *   ⚠ 第 2 引数が **無い** 呼び (@tube(path)@) ではマッチ関数は呼ばれない = 成立する。 */
static int
oc_match_tube_opts(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_is_hash(arg); }
static int
oc_match_mirror_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_mirror_keeps_xy(arg); }
static int
oc_match_rot_z(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{ (void)d; (void)e; return ( argNo != 1 ) ? 1 : pig_val_axis_is_z(arg); }
static const pigArgKind SECTION_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };  /* ★ #3514: section(s,P,N,mode) */
static const pigArgKind BOXA_IN[]    = { AK_INLINE };                        /* boxa([w,h,d]) */
/* ★ #3518 の 3: project(drawing, target, [dx,dy,dz]) — 幾何 2 つ + 方向。 */
static const pigArgKind PROJECT_IN[] = { AK_CACHE, AK_CACHE, AK_INLINE };

/* ★★ #3544: 2D は **2 型**になった (oc-cross2d = z=0 の簡易表現 / oc-face3d = 空間に置かれた
 *   一般表現)。規約は cg / mf と同じ 4 つ — docs/srava_language_reference.md#two-2d-types。
 *   ⇒ *受けるだけ* の op は両方受ける。出力は op ごとに決める (下の各行の注記)。 */
#define OC2IN(out)   "(" OC2_TYPE ")->" out ";(" OC2C_TYPE ")->" out
/* ★ 2D を **2 つ**取る op (project) 用。位置ごとに独立なので 4 通りを並べる。
 *   ⚠ @[a,b](2)@ (fold 形) では書けない — あれは *木に分解してよい / 主型を持つ* 可換な
 *     二項演算のための記法で、project は引数の役割が違う (下書きと投影先) ので当たらない。
 *   ⚠⚠ ここを片受けのままにすると、@rect@ が cross2d を名乗るようになった時点で
 *     project が **routing で落ちる**。⇒ op 自身の明示エラー (方向が零ベクトル等) まで
 *     到達しなくなり、検定は「別の文言で赤くなる」形で現れる (2026-09-17 に実際に踏んだ)。 */
#define OC2IN2(out)  "(" OC2_TYPE  "," OC2_TYPE  ")->" out ";(" OC2_TYPE  "," OC2C_TYPE ")->" out \
                 ";(" OC2C_TYPE "," OC2_TYPE  ")->" out ";(" OC2C_TYPE "," OC2C_TYPE ")->" out

static const pigOpEntry OPS[] = {
	/* 生成: ★どれも**解析曲面**で作る。box は 6 枚の平面 Face、sphere は**厳密な球面 1 枚**。 */
	{ "box",          SHAPE3_IN, 3, AK_CACHE,  OPWIRE(ocaBox),          0, "->" OC_TYPE },
	{ "boxa",         BOXA_IN,   1, AK_CACHE,  OPWIRE(ocaBox),          0, "->" OC_TYPE },  /* 寸法を array で */
	/* ★ sphere の第 2 引数 (分割数) は無視する — 近似しないので意味を持たない。
	 *   このため volume が 4/3·π·r³ ちょうどになり、内接多面体を作る他カーネルとは
	 *   **一致しない**。kernel_agree に素で入れてはいけない (それ自体が結果)。 */
	{ "sphere",       SHAPE1V_IN, 1, AK_CACHE, OPWIRE(ocaSphere),       0, "->" OC_TYPE },  /* ★ #3570 段3: segs を撤去 (厳密な球面 1 枚なので分割数が無い) */
	/* ★ cylinder / torus も**厳密**。cylinder は側面が円筒面 1 枚 (Face 3 枚)、torus は
	 *   トーラス面 1 枚 (Face 1 枚) でできる。どちらも分割数という概念を持たず、
	 *   volume は π r² h / 2π²R r² とちょうど一致する。
	 *   ★ torus は「メッシュ系では必ず近似になるが B-rep では厳密に持てる」形の代表で、
	 *     しかも fillet が稜に作る曲面そのものでもある。 */
	/* ★ #3474: 分割数 seg を **第 3 引数に受ける** (occt は近似しないので無視する)。
	 *   メッシュ系カーネルと arity を揃えないと、同じ式が実行カーネル次第で
	 *   引数個数エラーになってしまう (sphere が seg を取って無視するのと同じ扱い)。 */
	{ "cylinder",     SHAPE2V_IN, 2, AK_CACHE, OPWIRE(ocaCylinder),     0, "->" OC_TYPE },  /* ★ #3570 段3: segs を撤去 */
	/* ★ #3474: 分割数 seg を **第 3 引数に受ける** (occt は近似しないので無視する)。
	 *   メッシュ系カーネルと arity を揃えないと、同じ式が実行カーネル次第で
	 *   引数個数エラーになってしまう (sphere が seg を取って無視するのと同じ扱い)。 */
	{ "torus",        SHAPE2V_IN, 2, AK_CACHE, OPWIRE(ocaTorus),        0, "->" OC_TYPE },  /* ★ #3570 段3: segs を撤去 */
	/* ★ #3470: tube(path[, opts])。★★ **他カーネルの tube とは形が違う** —
	 *   cgal/manifold は折れ線の背骨 + segs 角形断面、occt は点を通る C2 B-spline の背骨 +
	 *   厳密な円の断面。**厳密に一致させることはできない** (カーネル一致の表には入れない)。
	 *   折れ線の管が欲しければ "cgal"::tube(…) と指名する (#3467)。 */
	{ "tube",         TUBE_IN,   2, AK_CACHE,  OPWIRE(ocaTube),         0, "->" OC_TYPE, 0, 0, 1, &oc_match_tube_opts },  /* ★ #3570 段3: 第 2 引数は **ハッシュのときだけ** (nreq=1 で省略可) */
	/* ★ #3471: TrueType の字形を **2D の曲線のまま** 取り込む。text(fontPath, str[, size])。
	 *   fontPath は pigDataFileRef で包まれ、キャッシュキーに **内容ハッシュ**が入る (import と同じ)。 */
#ifdef SRAVA_OCCT_TEXT
	{ "text",         TEXT_IN,   3, AK_CACHE,  OPWIRE(ocaText),         0, "->" OC2C_TYPE },
#endif
	/* ★ #3474 続き (2026-09-05): 2D プリミティブ。occt は oc-face3d 型を持ちながら
	 *   **text でしか 2D を作れず**、単独では「2D を作って押し出す」が書けなかった。
	 *   ★ rect / ngon / polygon は平面の折れ線なので **メッシュ系と厳密に一致する**。
	 *   ⚠ circle だけは **厳密な円** (Geom_Circle) なので内接多角形とは面積が構造的に違う
	 *     (sphere / cylinder / torus と同じ理由で一致検査の表には入れない)。segs は無視する。
	 *   ⚠ line (開ポリライン) は **入れない** — cgal ではブール演算に参加しない
	 *     「SVG/DXF のストローク層」であり、occt の出力形式 (STEP/BREP) に対応物が無い。
	 *     入れると line() が occt へ振られて SVG 出力が落ちる = 改善ではなく退行になる。 */
	{ "rect",         RECT_IN,   2, AK_CACHE,  OPWIRE(ocaRect),         0, "->" OC2C_TYPE },
	{ "ngon",         NGON_IN,   2, AK_CACHE,  OPWIRE(ocaNgon),         0, "->" OC2C_TYPE },
	{ "polygon",      SHAPE1_IN, 1, AK_CACHE,  OPWIRE(ocaPolygon),      0, "->" OC2C_TYPE },
	/* ★★ #3532: 格子から自由曲面。**点の意味が違う 2 つ**を別 op にした (ひさ承認 2026-09-17)。
	 *   surface_control … 格子は **制御点**。面は点を通らない (四隅だけ)。手で形を引っぱる道具
	 *   surface_through … 格子は **通過点**。面が点を通る。#3515 の逆工学が要るのはこちら
	 *   ⚠ 同じ綴り (三重アレイ) を渡すので、**op 名だけが意味を決める**。実測の差は 5e-14 対 5.4e-1。 */
	{ "surface_control", SHAPE1_IN, 1, AK_CACHE,  OPWIRE(ocaSurfaceControl), 0, "->" OC2_TYPE },
	/* ⚠ nreq=1: mode は省略可。既定 "fit" (近似)・"interp" で厳密に通す。
	 *   ★ 補間のほうが **点と点の間で暴れる** (実測 1.3e-3 対 5.3e-4) ので既定は近似。 */
	{ "surface_through", SURFTH_IN, 2, AK_CACHE,  OPWIRE(ocaSurfaceThrough), 0, "->" OC2_TYPE, 0, 0, 1 },
	/* ★ 混在 (通過点と制御点を 1 op で) は **持たない**。この 2 本の合成で書く:
	 *     T = set_poles(surface_through(格子), 直した poles(...))
	 *   ⇒ 制約付き最小二乗は OCCT に口が無く、op というより研究項目の規模になる。 */
	{ "poles",        MEASURE_IN,1, AK_INLINE, OPWIRE(ocaPoles, ocGeom),        0, OC2IN("value") },
	{ "set_poles",    EDIT_IN,   2, AK_CACHE,  OPWIRE(ocaSetPoles, ocGeom),     0, OC2IN(OC2_TYPE) },
	{ "circle",       SHAPE1V_IN, 1, AK_CACHE, OPWIRE(ocaCircle),       0, "->" OC2C_TYPE },  /* ★ #3570 段3: segs を撤去 (厳密な円) */
	{ "empty2d",      0,         0, AK_CACHE,  OPWIRE(ocaEmpty2D),      0, "->" OC2C_TYPE },
	{ "empty3d",      0,         0, AK_CACHE, OPWIRE(ocaEmpty3D), 0, "->" OC_TYPE },  /* 空集合(3D)。{} は中立元なので別物 */
	/* ★ #3471: 2D→3D。同名 op が cgal / manifold にもあるが **入力型が違う**ので sig で分かれる。 */
	{ "extrude",      XFORM1_IN, 2, AK_CACHE,  OPWIRE(ocaExtrude, ocGeom), 0, OC2IN(OC_TYPE) },
	{ "revolve",      REVOLVE_IN, 2, AK_CACHE, OPWIRE(ocaRevolve, ocGeom), 0, OC2IN(OC_TYPE), 0, 0, 1 },  /* ★ #3570 段3: segs を撤去 (角度は残る・nreq=1) */
	/* ★ #3471: prism(n,h,r) は **プリミティブ (leaf)**。平面 n+2 枚なのでメッシュ系と厳密に一致する。 */
	{ "prism",        SHAPE3_IN, 3, AK_CACHE,  OPWIRE(ocaPrism),        0, "->" OC_TYPE },
	/* ★ #3474: 基本立体はカーネル差が出ないので全カーネルに置く (common/solids.h)。 */
	{ "pyramid",       SHAPE3_IN, 3, AK_CACHE, OPWIRE(ocaPyramid), 0, "->" OC_TYPE },  /* pyramid(n,h,r): 平面多面体なのでメッシュ系と厳密に一致 */
	{ "cone",          SHAPE2V_IN, 2, AK_CACHE, OPWIRE(ocaCone), 0, "->" OC_TYPE },  /* ★ #3570 段3: cone(r,h)。segs を撤去 (解析曲面) */
	{ "tetrahedron",   SHAPE1_IN, 1, AK_CACHE, OPWIRE(ocaTetrahedron), 0, "->" OC_TYPE },  /* tetrahedron(r): 平面 4 枚。メッシュ系と厳密に一致 */
	/* ★ #3474 続き (2026-09-05): prism / icosphere / import の歯抜けも埋める。 */
	{ "icosphere",     ICOSPH_IN, 2, AK_CACHE, OPWIRE(ocaIcosphere), 0, "->" OC_TYPE, 0, 0, 1 },  /* icosphere(r,subdiv): 測地多面体なので厳密 */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	/* ★ #3471: 2D 領域の面積。輪郭が曲線のままなので **多角形近似を経ずに厳密**に出る。 */
	{ "area",         MEASURE_IN,1, AK_INLINE, OPWIRE(ocaArea, ocGeom), 0, OC2IN("value") ";(" OC_TYPE ")->value" },   /* ★ #3487: 3D の表面積も (2D 専用だと式の途中でカーネルが裏返る) */
	/* ★ 入口: STEP / .brep を読む。**mesh → B-rep ではない** (どちらも解析曲面をそのまま
	 *   持つ形式なので、読むだけで B-rep が手に入る)。三角形から解析曲面を復元する
	 *   reverse engineering の入口は依然として作らない。 */
	/* ★ #3544 段 3: **dxf を 2D 図面として読む**。⚠ 置かれた .dxf (OCS つき) は断る
	 *   (置き場所を黙って落とさない) — そちらは cgal の import が cg-face3d で読む。 */
	/* ★★ #3554 最後の段 3/5 (2026-09-19): import は **出力型ごとに 1 行**。行を選ぶのは
	 *   共通述語 @pig_match_import_ext@ (拡張子が産む型 = import_exts の型付き CSV が
	 *   この行の sig の出力型か)。⚠ 1 行に 2 つ書いていた旧形では、入力型 0 個の sigline が
	 *   先頭から当たり、.dxf を読んでも oc-brep3d を名乗っていた。 */
	{ "import#" OC_TYPE,   SHAPE1_IN, 1, AK_CACHE,  OPWIRE(ocaImport),       0, "->" OC_TYPE,   0, 0, 0, &pig_match_import_ext },   /* step / iges / brep */
	{ "import#" OC2C_TYPE, SHAPE1_IN, 1, AK_CACHE,  OPWIRE(ocaImport),       0, "->" OC2C_TYPE, 0, 0, 0, &pig_match_import_ext },   /* dxf (2D 図面) */
	/* ブール: 自型どうしだけ。★OCCT は失敗しうるので、失敗は明示エラーにする。 */
	{ "union",        BINSHAPE_IN,2,AK_CACHE,  OPWIRE(ocaUnion, ocGeom, ocGeom),        1, "[" OC_TYPE "](*)->" OC_TYPE, 1 /* ★可換 */ },
	/* ★★ #3518 の 4: **2D x 3D** — 面を立体で切り取る (返りは 2D)。これで
	 *   「面取り出し (face/face_at) + ブール」だけで *曲面上に切り取られた 2D* が作れる。
	 *   ★ 曲面種は保たれ、面積は加法的 (側面 18.8496 = ∩箱 3.1416 + −箱 15.7080・実測)。
	 *   ★ intersection は **可換**なので両向きを載せる。difference は順序に意味があるので
	 *     (面, 立体) だけ。⚠ 載せていない組み合わせは planner が断る。意図的なのは:
	 *     difference の (oc-brep3d, oc-face3d)  体積 0 の面で立体を切っても変わらない = no-op
	 *     union の 2D x 3D                       次元の違う和を表現できる型が無い
	 *   ⇒ 断る場所を op の中でなく **sig** に置くと、「どう書けるか」が記述子に集まる。 */
	/* ================= #3581: **点群と B-rep の積と差** ==============================
	 * ★★★ 境界ちょうどの点は **第 3 の集合**。mode 0/-1/+1 (#3575・section と同じ約束)。
	 * ★ 判定は BRepClass3d_SolidClassifier (3D) / 面までの距離 (2D)。**解析曲面のまま**
	 *   解くので球や円柱でメッシュ近似の誤差が無い = 3 モジュールで一番正確。
	 * ⚠⚠ **変種は無条件の行より前に置く** — 下の intersection/difference は
	 *   [oc-brep3d](*) で *どんな並びでも受ける* ので、後ろに置くと一度も選ばれない
	 *   (ロード時の検査が名指しする)。2026-09-22 に openvdb で踏んだ。
	 * ⚠⚠ **可換にしない ・ fold 形にしない** (型が非対称なので分解が引数を組み替えて壊す)。
	 * ★ 行は 4 つ。⚠ pt-cloud3d x 2D (X > Y) は **書かない** ⇒ routing が断る。 */
#define OC_PTSPLIT	"(" PT_TYPE_3D "," OC_TYPE ")->" PT_TYPE_3D \
			";(" PT_TYPE_2D "," OC_TYPE ")->" PT_TYPE_2D \
			";(" PT_TYPE_2D "," OC2_TYPE ")->" PT_TYPE_2D \
			";(" PT_TYPE_2D "," OC2C_TYPE ")->" PT_TYPE_2D
	{ "intersection#pt", PTSPLIT3_IN, 3, AK_CACHE, OPWIRE(ocaPtIntersection, ptCloud, ocGeom), 0,
	  OC_PTSPLIT },
	{ "difference#pt",   PTSPLIT2_IN, 2, AK_CACHE, OPWIRE(ocaPtDifference,   ptCloud, ocGeom), 0,
	  OC_PTSPLIT },
	{ "intersection", BINSHAPE_IN,2,AK_CACHE,  OPWIRE(ocaIntersection, ocGeom, ocGeom), 1, "[" OC_TYPE "](*)->" OC_TYPE ";(" OC2_TYPE "," OC_TYPE ")->" OC2_TYPE ";(" OC_TYPE "," OC2_TYPE ")->" OC2_TYPE, 1 /* ★可換 */ },
	{ "difference",   BINSHAPE_IN,2,AK_CACHE,  OPWIRE(ocaDifference, ocGeom, ocGeom),   1, "[" OC_TYPE "](*)->" OC_TYPE ";(" OC2_TYPE "," OC_TYPE ")->" OC2_TYPE },
	/* ★ **第 3 の offset 原理**: 解析曲面を直接オフセットし、稜に円筒パッチ・頂点に球パッチを
	 *   生成する (= Steiner の公式を構成的に実行)。nef の近似球 Minkowski 和とも
	 *   openvdb の格子等値面移動とも違う。入力型が disjoint なので他モジュールと衝突しない。 */
	/* ★★ #3547 ② (2026-09-18): 2D は **面の中で** 動かす (cg / mf と同じ約束・返りは同じ型)。
	 *   ⚠ 曲面上の面は明示エラーで offset_thicken を案内する。 */
	{ "offset",       OFFSET_IN, 3, AK_CACHE,  OPWIRE(ocaOffset, ocGeom),       0, "(" OC_TYPE ")->" OC_TYPE ";(" OC2_TYPE ")->" OC2_TYPE ";(" OC2C_TYPE ")->" OC2C_TYPE, 0, 0, 2 },  /* ★ nreq=2: 以降は省略可 (既定は op が入れる) */
	/* ★★ #3547 ②: **面に厚みを付けて立体にする** (2D → 3D)。offset とは約束が違うので
	 *   別の名前 (「元の op 名 + _修飾」・#3519 の規約)。⚠ 片側だけ ・ 凹側の曲率半径を検査する。 */
	{ "offset_thicken", EDIT_IN, 2, AK_CACHE, OPWIRE(ocaOffsetThicken, ocGeom), 0, "(" OC2_TYPE ")->" OC_TYPE ";(" OC2C_TYPE ")->" OC_TYPE },
	/* ★ triangulate (表現クラスをまたぐ唯一の出口) は **occt_mf.so へ移した**
	 *   (akira-project #3452)。ここに置くと occt.so が mf-mesh3d を名乗ることになり、
	 *   その実体は mfMesh ではない別クラス (旧 ocMesh) にならざるを得なかった。
	 *   境界 op は両側の本物のクラスを知っている境界モジュールが持つ (openvdb_mf と同じ)。
	 *   ★ 入口 (mesh → B-rep) は引き続き作らない。 */
	/* ★★ **B-rep でしか厳密に書けない加工**。転がり球の接触軌跡は解析曲面 (円筒・球・
	 *   トーラス) であって、三角形分割の上では**定義そのものが近似になる**。
	 *   メッシュ系にこの op が無いのは偶然ではない。★ 全ての稜に一律で適用する
	 *   (「この稜だけ」を指す語彙が srava に無いため。部分適用は将来)。 */
	{ "fillet",       EDIT_IN,   2, AK_CACHE,  OPWIRE(ocaFillet, ocGeom),       0, "(" OC_TYPE ")->" OC_TYPE },
	{ "chamfer",      EDIT_IN,   2, AK_CACHE,  OPWIRE(ocaChamfer, ocGeom),      0, "(" OC_TYPE ")->" OC_TYPE },
	/* ★ 出口 (B-rep のまま): STEP / .brep。三角形へ落とさずに外の CAD へ渡せる。
	 *   ⚠ 従来は export_exts に "brep" と申告しながら **export op を持っていなかった**
	 *     (= 記述子が嘘をついていた・#3437 で是正)。 */
	/* ★ #3544 段 3: **2D も書ける**ようになった (dxf / svg / brep / step)。
	 *   ⚠ sig に足すだけでは通らない — export は d_cast で受ける型を決めている
	 *     (ocaExport.cpp の注記)。両方を直すこと。 */
	/* ★★ #3554 最後の段 4/5 (2026-09-19): export の行は共通述語 @pig_match_export_ext@ が選ぶ
	 *   (= 第 1 引数の拡張子を **d->export_exts** が書けるか)。出力は常に @ref@ なので
	 *   **行を分ける必要は無い** (cast / import と違うのはここ)。
	 *   ⚠⚠ 同時に **規約① (自型優先) を撤去**した — 「入力型の home カーネルが書けるならそこ」
	 *     という routing の特例で、sig でも記述子でもない *3 つめの規則* だった。
	 *     ⇒ いまは priority × sig × 拡張子 の普通の決着。 */
	{ "export",       EXPORT_IN, 3, AK_CACHE,  OPWIRE(ocaExport, ocGeom),       0, "(" OC_TYPE ")->ref;" OC2IN("ref"), 0, 0, 0, &pig_match_export_ext },
	{ "volume",       MEASURE_IN,1, AK_INLINE, OPWIRE(ocaVolume, ocGeom),       0, "(" OC_TYPE ")->value" },
	/* ★ #3487: 値の素性を訊く op。B-rep のまま測るので解析曲面では mesh 系と構造的に
	 * 違う値が出る (体積と同じ事情) — 検証は kernel_agree ではなく **閉形式**で行う。
	 * valid は BRepAlgoAPI_Check (妥当性 + 自己交差) = 共通定義の ②③。 */
	{ "bbox",         MEASURE_IN,1, AK_INLINE, OPWIRE(ocaBbox, ocGeom),         0, "(" OC_TYPE ")->value;" OC2IN("value") },
	{ "centroid",     MEASURE_IN,1, AK_INLINE, OPWIRE(ocaCentroid, ocGeom),     0, "(" OC_TYPE ")->value;" OC2IN("value") },
	{ "valid",        MEASURE_IN,1, AK_INLINE, OPWIRE(ocaValid, ocGeom),        0, "(" OC_TYPE ")->value;" OC2IN("value") },   /* ★ #3547: 2D も測れる */
	/* ★ **三角形数ではなく Face 数**。円筒の側面は 1 面・トーラスは全体で 1 面なので
	 *   mesh 系の nfaces とは桁が違う値になる。その違いがこの表現の要点。 */
	/* ★ #3518 の 6: **2D も受ける** — ブールで切ると継ぎ目で割れて複数面になるので
	 *   (④ で実測)、「この 2D はいくつの面でできているか」を訊けないと困る。 */
	{ "nfaces",       MEASURE_IN,1, AK_INLINE, OPWIRE(ocaNfaces, ocGeom),       0, "(" OC_TYPE ")->value;" OC2IN("value") },
	{ "nverts",       MEASURE_IN,1, AK_INLINE, OPWIRE(ocaNverts, ocGeom),       0, "(" OC_TYPE ")->value;" OC2IN("value") },   /* ★ #3547: 2D も測れる */
	/* ★★ #3544 段 2: **稜の本数**。共有は 1 本と数える (立方体 = 12)。
	 *   ⚠ @hlr@ の出力は *面 0 枚・稜 N 本* の値なので、nfaces では何も分からない。
	 *     受け入れ条件 (可視 9 / 不可視 3 / 輪郭 1) を srava の言葉で書くのに要る。 */
	{ "nedges",       MEASURE_IN,1, AK_INLINE, OPWIRE(ocaNedges, ocGeom),       0, "(" OC_TYPE ")->value;" OC2IN("value") },
	/* ★★ #3547 ④ (#3527 の規約③): **頂点を読む 3 つ組**。それまで occt は nverts (数える) は
	 *   在るのに *読む* 手段が 0 本だった (#3510 の表で ★頂点を読む / occt = lib)。
	 *   ⚠ occt の頂点は **稜の端点** — 立方体 8 ・ 球 2 ・ 円 1。mesh 系とは数え方が違う。 */
	{ "vert",         EDIT_IN,   2, AK_INLINE, OPWIRE(ocaVert, ocGeom),         0, "(" OC_TYPE ")->value;" OC2IN("value") },
	/* ★ まとめて返す側は **点群型** (#3528)。⇒ そのまま hull / distance / delaunay が食える。
	 *   借りているのは幾何の機能ではなく **値の器**だけなので約束①に触れない (cgal / geomutils と同じ)。 */
	{ "verts",        MEASURE_IN,1, AK_CACHE,  OPWIRE(ocaVerts, ocGeom),        0, "(" OC_TYPE ")->pt-cloud3d;(" OC2_TYPE ")->pt-cloud3d;(" OC2C_TYPE ")->pt-cloud2d" },
	/* ★ 番号で返す側。⚠ B-rep の面は n 頂点 (箱の面 4 ・ 円筒の側面 2) なので **長さは面ごとに違う**
	 *   (三角形前提の cgal は常に 3)。2D は面 1 枚が値そのものなので置かない。 */
	{ "face_verts",   EDIT_IN,   2, AK_INLINE, OPWIRE(ocaFaceVerts, ocGeom),    0, "(" OC_TYPE ")->value" },

	/* ★★ #3518 の 2: **面を取り出す** — 立体 → その面 (oc-face3d)。これで「面取り出し +
	 *   ブール」だけで *曲面上の 2D* が手に入る (投影が無くても始められる)。
	 *   ★ 取り出した面は *平面とは限らない* (円柱の側面はそのまま円筒面のまま出る)。
	 *     これが oc-face3d が TopoDS_Face = 任意曲面上のトリム面である意味そのもの。
	 *   ⚠ メッシュ系に同名 op は置けない (あちらの「面」は三角形で、しかも 2D 型は平面に
	 *     縛られている)。fillet / chamfer と同じ **occt 固有**の行。
	 *
	 * ★★ **2 通りに分けてある** (ひさ指示・2026-09-12)。指し方が違う:
	 *   face(solid, i)         索引で取る。0..nfaces-1。巡回順は TopExp_Explorer の順で、
	 *                          **同じ面集合なら決定的** (同一入力 / ブール / BinTools 往復 /
	 *                          同じ面集合を作る別の式、の 4 条件で測って一致した)。
	 *   face_at(solid, [x,y,z]) 位置で指す。その点にいちばん近い面。
	 *                          ⚠ 立体の **作り方**を変えると面集合そのものが変わる
	 *                          (box を 2 つ積むと継ぎ目が残って 6 面 → 11 面)。そのとき
	 *                          索引は別の面を指す = 順序規約をどう決めても直らない。
	 *                          モデルを書き換えても同じ面を指し続けたいならこちら。 */
	/* ★★ #3518 の 6: **面の素性を訊く**。face / face_at で平面でない 2D が普通に入るように
	 *   なったので、「cast できるのか / polygonize できるのか / extrude して意味があるのか」を
	 *   *踏む前に* 判断する手段が要る。返りは曲面種の名前 ("plane" / "cylinder" / …)。
	 *   ⚠ 複数面で種類が揃わなければ "mixed"・面が無ければ "empty" (0 面はエラーではない)。 */
	{ "surface_type", MEASURE_IN,1, AK_INLINE, OPWIRE(ocaSurfaceType, ocGeom),  0, OC2IN("value") },
	/* ★★ #3518 の 3: **平面図形を曲面へ投影して切る**。project(drawing, target, [dx,dy,dz])。
	 *   ⚠ 直線投影は **閉曲面を 2 回当たる** (手前と奥)。⇒ 外向き法線が投影方向と逆を向く
	 *     面 = *投影元から見える側* だけを残す (実測で閉形式と一致・ocaProject.cpp 冒頭)。
	 *   ★ ④ (面 ∩ 立体) で代用が効くので最後に回した op。実装も ④ の再利用でできている。 */
	/*   ★ #3544: 下書き・投影先とも **両方の 2D を受ける** (rect() は cross2d を名乗る)。
	 *     出力は投影先の曲面の上なので一般に z=0 ではない ⇒ face3d。 */
	{ "project",      PROJECT_IN,3, AK_CACHE,  OPWIRE(ocaProject, ocGeom, ocGeom), 0, OC2IN2(OC2_TYPE) },
	/* ★★ #3534: **project_flatten** — world の (x,y) を取り z を捨てる = z=0 への直投影。
	 *   ⚠ occt だけ **型が変わらない** ((oc-face3d)->oc-face3d)。#3533 で「occt には z=0 の
	 *     簡易表現が存在しない」と決めたため。行き止まりではない (polygonize で mf へ渡せる)
	 *     が、「降格を型で表す」という #3533 の整理からは外れるので明示しておく。
	 *   ⚠ 上の @project@ (曲面へ投影して切る) とは別の op。紛れないよう複合語にしてある。 */
	{ "project_flatten", MEASURE_IN,1, AK_CACHE, OPWIRE(ocaProjectFlatten, ocGeom), 0, OC2IN(OC2C_TYPE)   /* ★ #3544: z=0 へ落とすので **cross2d** */ },
	/* ★★ #3544: 規約② — **降格は cast だけ**。幾何は動かさず、本当に z=0 に居るときだけ通る。
	 *   ⚠ 昇格 (cross2d → face3d) も同じ op で受ける (一般表現のほうが広いので常に通る)。 */
	/* ★★ #3554 最後の段 2/5 (2026-09-19): cast は **目標型ごとに 1 行**。行を選ぶのは共通述語
	 *   @pig_match_cast_target@ (第 1 引数の型名が この行の sig の出力型か)。
	 *   ⚠ 1 行に出力型を 2 つ書くと、行の可否 (どれかの sigline が目標型を産むか) と
	 *     実際に名乗る型 (入力型で先に当たった sigline) が食い違う
	 *     ⇒ ロード時に pig_descriptor_violation が弾く。 */
	{ "cast#" OC2C_TYPE, CAST_IN, 2, AK_CACHE,  OPWIRE(ocaCast, ocGeom),         0, OC2IN(OC2C_TYPE), 0, 0, 0, &pig_match_cast_target },
	{ "cast#" OC2_TYPE,  CAST_IN, 2, AK_CACHE,  OPWIRE(ocaCast, ocGeom),         0, OC2IN(OC2_TYPE),  0, 0, 0, &pig_match_cast_target },
	/* ★★★ #3544 段 2: **陰線処理 (HLR)** — 立体から遮蔽を解いた 2D 図面を起こす。
	 *   @project@ (曲面へ投影して切る) / @project_flatten@ (2D を z=0 へ寝かせる) とは
	 *   **3 つとも別物** ⇒ ocaHlr.cpp 冒頭の表。
	 *   ★ 出力は投影面 (z=0) の上に乗るので **oc-cross2d** (段 0 で実測)。 */
	{ "hlr",          HLR_IN,    4, AK_CACHE,  OPWIRE(ocaHlr, ocShape),         0, "(" OC_TYPE ")->" OC2C_TYPE, 0, 0, 2 },
	/* ★★ #3518: **同じ曲面に載る隣り合う面を 1 枚に畳む** (形は変えない)。
	 *   B-rep の面は「1 つの曲面 + (u,v) を切り取るワイヤ」なので、周期曲面では
	 *   **継ぎ目をまたぐ領域が 2 枚に割れる** ⇒ nfaces が「本物の 2 か所」か
	 *   「継ぎ目で割れただけ」かを区別できない。この op を通すと後者だけが畳まれる。
	 *   ⚠ srava の unify (nef の内壁除去) とは **別物** — あちらは体積が変わる。
	 *     #3519 の作法に倣って名前を分けた。★ 黙ってはやらない (明示 op)。 */
	{ "unify_faces",  MEASURE_IN,1, AK_CACHE,  OPWIRE(ocaUnifyFaces, ocGeom),    0, "(" OC_TYPE ")->" OC_TYPE ";" OC2IN(OC2_TYPE) },
	/* ★★ #3511: **loft** — 断面の列を通る立体。@extrude@ / @revolve@ / @tube@ が断面 1 枚なのに
	 *   対し、*断面が複数枚で形が変わってよい* のが loft (船体・翼・ダクトの遷移部)。
	 *   ★★ 断面の **置き場所は op が決めない** — 利用者が transform で空間に置いたものを
	 *     そのまま使う。#3511 の起票時は「(A) 高さの列を渡す / (B) 断面ごとに変換行列 /
	 *     (C) 3D 閉曲線の新しい型」で迷っていたが、#3518 の 1 で oc-face3d が平面の外へ
	 *     出られるようになったので **(B) が型を増やさずに書けるようになった** (ひさ判断)。
	 *   ★ loft / loft_ruled を **別 op** にする: 線織面は三角形で厳密に表せるのでメッシュ系でも
	 *     実装できるが、なめらかな方は解析曲面が要る。⇒ #3510 の表に差が出せる。
	 *   ⚠ 繰り返し形の sig (@{T}...@) — **fold 形ではない**。断面は 1 回の ThruSections へ
	 *     まとめて渡す必要があり、loft(loft(a,b),c) には分解できない。 */
	{ "loft",         0,         0, AK_CACHE,  OPWIRE(ocaLoft, ocGeom),         1, "{" OC2_TYPE "," OC2C_TYPE "}...[]->" OC_TYPE },
	{ "loft_ruled",   0,         0, AK_CACHE,  OPWIRE(ocaLoftRuled, ocGeom),    1, "{" OC2_TYPE "," OC2C_TYPE "}...[]->" OC_TYPE },
	{ "face",         EDIT_IN,   2, AK_CACHE,  OPWIRE(ocaFace, ocGeom),         0, "(" OC_TYPE ")->" OC2_TYPE ";" OC2IN(OC2_TYPE) },
	{ "face_at",      EDIT_IN,   2, AK_CACHE,  OPWIRE(ocaFaceAt, ocGeom),       0, "(" OC_TYPE ")->" OC2_TYPE ";" OC2IN(OC2_TYPE) },

	/* ★ #3461 変換: 全て ocGeom::op_affine に集約。線形部が直交×一様スケールなら
	 *   gp_Trsf で**解析曲面のまま**、そうでなければ gp_GTrsf (曲面が BSpline になる)。
	 * ★★ #3518: **2D (oc-face3d) も受ける**。cgal / manifold は元から 2D を受けていたので、
	 *   ここは歯抜けだった (#3516 と同じ形の穴 — 表にして数えるまで見えない)。
	 * ⚠ 2D の意味が **カーネル間で違う**: cgal / manifold は XY の 2x2 + (tx,ty) しか使わず
	 *   面外成分を黙って捨てる (rotate("x",45) で面積が cos45 倍)。occt は TopoDS_Face を
	 *   3D の変換で動かすので **面が z=0 平面の外へ出る** (断面を空間に置ける = loft の前提)。
	 *   平面内に留まる変換 (z 軸回転・XY 平行移動・XY スケール) では 3 カーネルが一致する。 */
	{ "translate#xy", XFORM1_IN, 2, AK_CACHE,  OPWIRE(ocaTranslate, ocGeom), 0, "(" OC2C_TYPE ")->" OC2C_TYPE, 0, 0, 0, &oc_match_trans_xy },
	{ "translate",    XFORM1_IN, 2, AK_CACHE,  OPWIRE(ocaTranslate, ocGeom),    0, "(" OC_TYPE ")->" OC_TYPE ";" OC2IN(OC2_TYPE) },
	{ "rotate#z", XFORM2_IN, 3, AK_CACHE,  OPWIRE(ocaRotate, ocGeom), 0, "(" OC2C_TYPE ")->" OC2C_TYPE, 0, 0, 0, &oc_match_rot_z },
	{ "rotate",       XFORM2_IN, 3, AK_CACHE,  OPWIRE(ocaRotate, ocGeom),       0, "(" OC_TYPE ")->" OC_TYPE ";" OC2IN(OC2_TYPE) },
	{ "scale#xy", XFORM1_IN, 2, AK_CACHE,  OPWIRE(ocaScale, ocGeom), 0, "(" OC2C_TYPE ")->" OC2C_TYPE, 0, 0, 0, &oc_match_scale_xy },
	{ "scale",        XFORM1_IN, 2, AK_CACHE,  OPWIRE(ocaScale, ocGeom),        0, "(" OC_TYPE ")->" OC_TYPE ";" OC2IN(OC2_TYPE) },
	{ "mirror#xy", XFORM1_IN, 2, AK_CACHE,  OPWIRE(ocaMirror, ocGeom), 0, "(" OC2C_TYPE ")->" OC2C_TYPE, 0, 0, 0, &oc_match_mirror_xy },
	{ "mirror",       XFORM1_IN, 2, AK_CACHE,  OPWIRE(ocaMirror, ocGeom),       0, "(" OC_TYPE ")->" OC_TYPE ";" OC2IN(OC2_TYPE) },
	{ "transform#xy", XFORM1_IN, 2, AK_CACHE,  OPWIRE(ocaTransform, ocGeom), 0, "(" OC2C_TYPE ")->" OC2C_TYPE, 0, 0, 0, &oc_match_xform_xy },
	{ "transform",    XFORM1_IN, 2, AK_CACHE,  OPWIRE(ocaTransform, ocGeom),    0, "(" OC_TYPE ")->" OC_TYPE ";" OC2IN(OC2_TYPE) },
	/* ★ #3514: **点との距離** — p から境界までの最短距離 (符号なし)。値返し。
	 *   ⚠ 既存の distance(a,b) は **2 つの立体**の間の距離。問うているものが違うので別名にした
	 *     (位置で指す _at は face_at と同じ流儀)。閉形式: 球 (半径 r) の中心から距離 d の点 → |d - r|。
	 */
		/* ★★ #3553: 2D も受ける (定義は 3D と同じ・3D に埋め込まれた 2 次元だから)。 */
{ "distance_at",  XFORM1_IN, 2, AK_INLINE, OPWIRE(ocaDistanceAt, ocGeom),   0, "(" OC_TYPE ")->value;" OC2IN("value") },
	/* ★ #3514: 断面。**解析曲面のまま切る**ので球の断面は真円 (Geom_Circle) で出る。
	 *   3 要素配列の規約 (共面のとき mode 0 は空・±1 が極限) は cgal / manifold と同じ。 */
	{ "section",      SECTION_IN,4, AK_CACHE,  OPWIRE(ocaSection, ocGeom),      0, "(" OC_TYPE ")->" OC2_TYPE },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	octsAgent_(
		sPtr<ptsObject> parent);

protected:
	virtual const pigOpEntry*	agent_ops();
	virtual int			agent_n_ops();
	virtual const char*		agent_name();
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
struct pigOpEntry;
TS_END_INTERFACE

#endif


octsAgent_::octsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	octsAgent_::agent_ops()	  { return OPS; }
int			octsAgent_::agent_n_ops() { return N_OPS; }
const char*		octsAgent_::agent_name()  { return OC_MODULE_NAME; }

static sPtr<ptsAgent>
mk_octsAgent(sPtr<ptsObject> parent)
{
	return sPtr<ptsAgent>::d_cast(thNEW(octsAgent,(parent)));
}

/* 自己申告記述子。
 *  - priority=2: 低いので**既定カーネルにはならない** (既定は cgal の 20)。ベンチで使うときは
 *    module("occt.so",{priority:99}) で明示的に上げる。
 *    ★ 現在の梯子 (2026-08-25): cgal 20 > manifold 10 > geogram 6 > nef 5 > pipe_proximity 4 >
 *      occt 2 > openvdb 1 > (テスト専用は負値: demo -1 / d3 -2 / d2 -3 / d4 -4 / d5 -5)。
 *      ★同点の勝敗は不定なので、同梱モジュールは互いに重複させない。
 *  - exec: PROCESS のみ。OCCT はプロセス全体のグローバル状態を持ち、in-proc の安全性は未検証。
 *    geogram / openvdb と同じ慎重な既定。in-proc 化は #3419 とまとめて扱う。
 */
extern const pigModuleType occt_provides[];
extern const srava_module_descriptor octsAgent_descriptor;
extern const srava_module_descriptor octsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = OC_MODULE_NAME,
	.priority      = 2,
	.make_agent    = &mk_octsAgent,
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	/* ★ import_exts は **型つき** CSV。STEP/BREP は解析曲面を持つ形式なので B-rep が直接出る。
	 *   ⚠ ここに mesh 系の拡張子 (stl/off) を足してはいけない — それは「入口を作らない」と
	 *     決めた reverse engineering そのものになる。 */
/* ★ #3513: STEP と IGES は **別の toolkit** (TKDESTEP / TKDEIGES) なのでフラグも別。
 *   ⇒ 4 通りを明示的に書く。**実体の無い形式を申告しない** — 申告だけ残すと planner が
 *   occt へ振ってから書き込みで落ちる (2026-09-07 に STEP で踏んだ形)。 */
#if defined(SRAVA_OCCT_STEP) && defined(SRAVA_OCCT_IGES)
	.import_exts   = "step:" OC_TYPE ",stp:" OC_TYPE ",iges:" OC_TYPE ",igs:" OC_TYPE ",brep:" OC_TYPE ",dxf:" OC2C_TYPE,
	.export_exts   = "step,stp,iges,igs,brep,dxf,svg",   /* (STEPControl_Writer / IGESControl_Writer / BRepTools::Write / ★ #3544 段 3: 2D 図面) */
#elif defined(SRAVA_OCCT_STEP)
	.import_exts   = "step:" OC_TYPE ",stp:" OC_TYPE ",brep:" OC_TYPE ",dxf:" OC2C_TYPE,   /* */
	.export_exts   = "step,stp,brep,dxf,svg",   /* (STEPControl_Writer / BRepTools::Write / ★ #3544 段 3) */
#elif defined(SRAVA_OCCT_IGES)
	.import_exts   = "iges:" OC_TYPE ",igs:" OC_TYPE ",brep:" OC_TYPE ",dxf:" OC2C_TYPE,
	.export_exts   = "iges,igs,brep,dxf,svg",   /* (IGESControl_Writer / BRepTools::Write / ★ #3544 段 3) */
#else
	/* ★ 2026-09-07: DataExchange 無しの OCCT と組んだビルド。 */
	.import_exts   = "brep:" OC_TYPE ",dxf:" OC2C_TYPE,
	.export_exts   = "brep,dxf,svg",   /* (BRepTools::Write / ★ #3544 段 3) */
#endif
	.provides      = occt_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	/* ★ v18 (#3466): このモジュールが出す結果の版。**計算を変えたら手で上げる**。
	 * ★ v2 (#3501・2026-09-07): union の材料喪失検査を入れた。
	 *   ⚠ *正しい結果は 1 bit も変わっていない* が、**この修正より前に書かれたキャッシュには
	 *   「黙って誤った union」が入っている可能性がある**。ソルトは .so 指紋ではなく
	 *   この番号なので (pigModuleRegistry.cpp)、上げないと旧キャッシュがその誤値を
	 *   *正しい答えとして返し続ける* — #3489 で踏んだのと同じ形。
	 *   ⚠ どのエントリが汚染されているかを見分ける手段は無いので、occt のキャッシュを
	 *   丸ごと捨てる形になる。破綻の頻度に対して重い手だが、細かい道具が無い。
	 * ★★ v3 (2026-09-12・統合): **2 を飛ばして 3 にする**。2 つの開発系統が
	 *   *別の理由で*それぞれ 2 を名乗っていたため — 片方の v2 = #3507 で BREP の framing を
	 *   ブロック分割へ変更 (旧 framing のキャッシュは読めない)、もう片方の v2 = 上記 #3501。
	 *   2 のままだと **どちらの v2 キャッシュも有効扱いになり**、旧 framing と誤 union を
	 *   同時に呼び込む。版番号は「どう計算したか」の名前なので、意味が 2 つあるなら別番号。 */
	/* ★★ v4 (#3536・2026-09-14): **section が断面を運ばなくなった** — 切った場所にそのまま
	 *   返す (それまでは XY 平面へ運んでいた)。⇒ 同じ式が **違う置き場所の 2D** を返すので、
	 *   旧 blob を読ませてはいけない。面積は変わらないが bbox / extrude / export が変わる。 */
	/* ★★ v5 (#3543・2026-09-15): **valid が非多様体を 0 と答えるようになった** —
	 *   BRepAlgoAPI_Check は立体を 1 つずつ見るので「稜だけで接する 2 立体」(2 球の xor) を
	 *   妥当と答えていた。共通定義 (#3487) の ② に合わせて稜の使われ回数を数える。
	 *   ⇒ 同じ式が **違う valid** を返すので旧 blob を読ませてはいけない。 */
	/* ★★ v6 (#3546・2026-09-17): **bbox の値が変わる** — (a) Bezier / B-spline 面では
	 *   poles の箱ではなく AddOptimal の真の箱を返す (実測 4 倍の過大評価が直る)。
	 *   (b) 2D の bbox が 3D と同じ経路になった (useTriangulation=false ・ SetGap(0.0))。
	 *   ⇒ 同じ式が **違う bbox** を返すので旧 blob を読ませてはいけない。 */
	.cache_version = 6,   /* ★ #3544 は blob を変えないので据え置き (名乗りはスタンプ経由) */
	/* ★ #3452: **oc-brep3d だけ**を名乗る。mf-mesh3d は occt_mf.so が (本物の mfMesh で) 扱う。 */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};

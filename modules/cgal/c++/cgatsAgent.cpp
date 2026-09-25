/*
 * cgatsAgent — CGAL カーネルの実行体(ptsAgent 派生 = 演算を実際に実行するクラス)。
 *   ptsAgentStub(echo)を拡張し、ディスパッチ
 * テーブルで演算子ごとの計算本体(ptsCalcBody 派生)を起動する。
 *
 * 配線(#3406 段階 4.2 / 2026-08-02 メモ §1): 通信は自分では持たず、親 (ptsObject) に委ねる。
 *   - agent process では parent = ptsApplication(自 stdin/stdout の ptsWirePipe を内包)
 *   - planner 内 thread では parent = ptsMediatorInternal(pigData 直渡し・4.3)
 *   結果もエラーも **pigData のまま** set_result() して FIN へ抜けるだけ (§5/§6)。ワイヤ形への
 *   符号化も A_SAVE_BEGIN/DONE の組み立ても親の判断・役割。着信は parent からの TSE_PACKET。
 *   (旧構成では自分が実態元祖で s2IOstd + ptsWirePipe を直に抱えていた。)
 *
 * 流れ:
 *   INI      : ディスパッチ状態の初期化(通信は parent が確立済み)
 *   WAIT     : C_OP で OPS 検索(無→A_ERROR)。C_ARG_* を型リストと照合して収集(狂い→A_ERROR)。
 *              pigDataCache 入力は reader を開始(Stage2; box は無し)。C_ARG_END で計算本体起動。
 *   CALC     : 計算本体の TSE_RETURN を待ち、結果を引く。Writer を起こす
 *   WRITING〜: cache(mesh)出力は保存 helper の TSE_ASSERT(header+meta 書込済)で **A_SAVE_BEGIN を先に**
 *              送り(下流が書込中 attach 可=同時ストリーミング)、保存完了(TSE_DESTROY)を待って
 *              A_SAVE_DONE。値(インライン)出力は全書込完了後に本文相乗りの A_SAVE_BEGIN。/BYE/wend
 *              (ev 非依存・1 状態 1 write)
 *   ERROR    : A_ERROR + wend
 *
 * 出力シリアライズ(確認①): 値(インライン)・mesh(cache)いずれも calc の get_result()(#3406
 *   2026-07-30: get_body 統合)で本文を受け取り、出力 pigDataCache へ set_body する。保存 helper
 *   (ptsDataCache)が本文の型で codec を選び、mesh は D_META "MESH" + D_CHUNK でストリーム書き込み。
 *
 * ディスパッチ: 演算子名→{入力型リスト, 出力型, 入力 reader 生成子, 計算本体生成子}。生成子は
 *   テンプレート thunk(各クラスに static New 不要)。型は当面 {INLINE, CACHE} の 2 値。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/ptsAgent.h"         /* 基底(演算実行体) */
#include	"pig/c++/ptsGenericAgent.h"  /* 共通基底 (状態機械を集約) */
#include	"pig/c++/pigAgentRegistry.h" /* 自分を「この実行ファイルの実行体」として登録 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"     /* 共通 op エントリ型 (Phase1-4) */
#include	"pig/c++/pigOpMatch.h"   /* ★ #3554 段4: 共通のマッチ述語 */
#include	"pig/c++/pigModule.h"      /* srava_module_descriptor (cgal.so 記述子・Phase3b) */
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsMediatorPacket.h"     /* Internal 経路の pigData 直渡しパケット (#3406 4.3) */
#include	"pig/c++/ptsDataCache.h"          /* 保存/読出 helper の source 同定 (d_cast・2026-07-29) */
#include	"pig/c++/ptsWireCacheStreamWriterText.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"       /* 出力保存 helper(ptscgWireCacheStreamWriterMesh)用 */   /* 値(インライン)出力の保存 */
#include	"cg/c++/cgMesh.h"           /* set_body へ渡す本文の d_cast(完全型) */
#include	"pig/c++/ptsCalcBody.h"
#include	"cg/c++/cgaBox.h"
#include	"cg/c++/cgaPrism.h"
#include	"cg/c++/cgaPyramid.h"
/* ★ #3474: 基本立体をカーネル間で統一 */
#include	"cg/c++/cgaCylinder.h"
#include	"cg/c++/cgaCone.h"
#include	"cg/c++/cgaTorus.h"
#include	"cg/c++/cgaTetrahedron.h"
#include	"cg/c++/cgaSphere.h"
#include	"cg/c++/cgaIcosphere.h"
#include	"cg/c++/cgaUnion.h"
#include	"cg/c++/cgaHull.h"
#include	"cg/c++/cgaLoftRuled.h"
#include	"cg/c++/cgaCombine.h"
#include	"cg/c++/cgaIntersection.h"
#include	"cg/c++/cgaDifference.h"
#include	"cg/c++/cgaExport.h"
#include	"cg/c++/cgaImport.h"
#include	"cg/c++/cgaTranslate.h"   /* transform 系: 1 mesh + スカラ */
#include	"cg/c++/cgaRotate.h"
#include	"cg/c++/cgaMirror.h"
#include	"cg/c++/cgaScale.h"
#include	"cg/c++/cgaTransform.h"
#include	"cg/c++/cgaColor.h"       /* color(mesh, c): 面色 f:color */
#include	"cg/c++/cgaRect.h"        /* 2D プリミティブ */
#include	"cg/c++/cgaNgon.h"
#include	"cg/c++/cgaCircle.h"
#include	"cg/c++/cgaPolygon.h"
#include	"cg/c++/cgaLine.h"
#include	"cg/c++/cgaSection.h"
#include	"cg/c++/cgaEmpty2D.h"
#include	"cg/c++/cgaEmpty3D.h"
#include	"cg/c++/cgaExtrude.h"     /* 2D→3D */
#include	"cg/c++/cgaTube.h"        /* 3D 折れ線まわりの掃引管 */
#include	"cg/c++/cgaRevolve.h"
#include	"cg/c++/cgaOffset.h"
#include	"cg/c++/cgaArea.h"
#include	"cg/c++/cgaNverts.h"
#include	"cg/c++/cgaNfaces.h"        /* 計測(値返し op): area(m) */
#include	"cg/c++/cgaNshells.h"       /* ★ #3514: 位相を直接数える 3 本 */
#include	"cg/c++/cgaNparts.h"
#include	"cg/c++/cgaGenus.h"
#include	"cg/c++/cgaDistanceAt.h"    /* ★ #3514: 点との距離 */
#include	"cg/c++/cgaEstimateNormals.h"  /* ★ #3528: 点群の法線推定 (pt-cloud3d を借りる) */
#include	"pt/c++/ptCloud.h"          /* ★ #3528: 点群の本体クラス (中立の libsrava_pt) */
#include	"cg/c++/cgaValid.h"       /* 検査(値返し op): valid(m) */
#include	"cg/c++/cgaRepair.h"      /* 修復(mesh 返し op): repair(m) */
#include	"cg/c++/cgaProjectFlatten.h"   /* ★ #3534: z=0 への直投影 */
#include	"cg/c++/cgaRefine.h"      /* ★ #3512: 細分(mesh 返し op): refine(m,len) */
#include	"cg/c++/cgaRemesh.h"      /* ★ #3512: 張り直し(mesh 返し op): remesh(m,len[,iter[,sharp]]) */
#include	"cg/c++/cgaSimplify.h"    /* ★ #3512: 簡約(mesh 返し op): simplify(m,n) */
#include	"cg/c++/cgaVolume.h"      /* 計測(値返し op): volume(m) */
#include	"cg/c++/cgaPerimeter.h"   /* 計測(値返し op): perimeter(m) */
#include	"cg/c++/cgaCentroid.h"    /* 計測(配列返し op): centroid(m) */
#include	"cg/c++/cgaBbox.h"        /* 計測(入れ子配列返し op): bbox(m) */
#include	"cg/c++/cgaDistance.h"    /* 近接(値返し op): distance(a,b) */
#include	"cg/c++/cgaClosest.h"     /* 近接(配列返し op): closest(a,b) */
#include	"cg/c++/cgaFarthest.h"    /* 近接(配列返し op): farthest(a,b) */
#include	"cg/c++/cgaThinSpots.h"   /* 肉厚 SDF(入れ子配列返し op): thin_spots(m,t) */
#include	"cg/c++/cgaVoronoi.h"  /* ★ #3525: voronoi(p, box) — 2D 点群 → セル分割 */
#include	"cg/c++/cgaDelaunay.h" /* ★ #3525: delaunay(p) — 2D 点群 → 三角形分割 */
#include	"cg/c++/cgaPart.h"     /* ★ #3525: part(v, i) — 2D の片 (nef の 3D 版と同名・別型) */
#include	"cg/c++/cgaVert.h"     /* ★ #3527: vert(m, i) — i 番目の頂点の座標 (3 つ組の「取り出す」) */
#include	"cg/c++/cgaVerts.h"    /* ★ #3527: verts(m) — 全頂点を点群で (3 つ組の「まとめて」) */
#include	"cg/c++/cgaShell.h"    /* ★ #3527: shell(m, i) — i 番目の殻 (面の連結成分) */
#include	"cg/c++/cgaShellAt.h"  /* ★ #3527: shell_at(m, p) — 位置で指す側 */
#include	"cg/c++/cgaPartAt.h"   /* ★ #3527: part_at(v, p) — 点を **含む** 片 (3 つ組を揃える) */
#include	"cg/c++/cgaFaceVerts.h"/* ★ #3527: face_verts(m, i) — 面の頂点 **番号** */
#include	"cg/c++/cgaCast.h"        /* cast("exact", mesh): カーネル明示変換(MFM3→EPECK 無損失昇格) */
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/cgatsAgent_.h"

#include	<string.h>
#include	<stdlib.h>   /* getenv(テスト用フォールトインジェクション) */
#include	<unistd.h>   /* usleep(テスト用の計算遅延) */
#include	<stdio.h>
#include	<sys/time.h>

CLASS_TINYSTATE(cg/c++/cgatsAgent,pig/c++/ptsGenericAgent)

/* ---- ディスパッチテーブル(ファイルスコープ) ---- */
/* 型は pig 層の共通型 (pigOpEntry.h・.so 化 Phase1-4)。旧名はエイリアスで温存し OPS 本体は無改修。 */
typedef pigArgKind ArgKind;       /* AK_INLINE / AK_CACHE は pigOpEntry.h 由来 */
typedef pigOpEntry cgaOpEntry;

/* テンプレート thunk: 各クラスに static New を書かずコンストラクタ呼びを生成。
 * 入力は **ポインタ** で渡す(親 cgatsAgent が所有・寿命中生存。sArray の値渡し/コピーは避ける)。
 * 戻り型は pigCalcFactory と一致 (OPS の mkCalc へそのまま入る)。 */

static const ArgKind SHAPE3_IN[] = { AK_INLINE, AK_INLINE, AK_INLINE };  /* box/prism/pyramid */
static const ArgKind SHAPE2_IN[] = { AK_INLINE, AK_INLINE };             /* rect(w,h) 2D */
static const ArgKind SHAPE1_IN[] = { AK_INLINE };                        /* sphere(r) / boxa([..]) */
static const ArgKind EXPORT_IN[] = { AK_INLINE, AK_CACHE, AK_INLINE };  /* export(path, mesh, unit) */
static const ArgKind BINMESH_IN[] = { AK_CACHE, AK_CACHE };  /* 2 mesh 入力(cache ハンドル→reader 読み) */
/* transform 系: 入力 mesh(cache)1 個 + スカラ/構造(inline)。mesh は reader、残りは value-parse。 */
static const ArgKind ROTATE_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };             /* rotate(m,axis,deg) */
static const ArgKind SECTION_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };  /* section(m,P,N,mode) */
static const ArgKind MESH1ARG_IN[] = { AK_CACHE, AK_INLINE };  /* translate(m,vec) / mirror(m,axis) / transform(m,matrix) */

/* ★★ #3554 段4 (2026-09-19): @transform#xy@ の行が成立する条件 —
 *   **第 2 引数 (行列) が z=0 平面を平面へ写すか**。判定本体は共通述語 @pig_val_keeps_xy@
 *   (= @srava_affine::keeps_z_plane@) で、*モジュール側が書くのは「何番を見るか」だけ*。
 *   ⚠ 行列しか見ないので、救えるのは枠が既定と決まっている @cross2d@ だけ
 *     (sig を @(cg-cross2d)->cg-cross2d@ に限ってあるのはそのため)。 */
static int
cg_match_xform_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	if ( argNo != 1 ) return 1;         /* 行列は第 2 引数 */
	return pig_val_keeps_xy(arg);
}

/* ★ #3554 段5: @mirror(m, axis)@ の軸が平面を保つか (x/y/z 軸なら保つ・任意軸は面外)。 */
static int
cg_match_mirror_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	if ( argNo != 1 ) return 1;
	return pig_val_mirror_keeps_xy(arg);
}

/* ★ #3554 段5: @scale(m, vec)@ — matrix_scale は対角しか作らないので構造上いつも保つ。 */
static int
cg_match_scale_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	if ( argNo != 1 ) return 1;
	return pig_val_scale_keeps_xy(arg);
}

/* ★ #3554 段5: @rotate(m, axis, deg)@ — ⚠ **軸 z のときだけ**。角度は見られない (上記参照)。 */
static int
cg_match_rot_z(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	if ( argNo != 1 ) return 1;         /* 軸は第 2 引数 (角度は第 3 で、ここからは見えない) */
	return pig_val_axis_is_z(arg);
}

/* ★ #3554 段5: @translate(m, vec)@ の vec が **xy 内の移動か** (z 成分が 0)。 */
static int
cg_match_trans_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	if ( argNo != 1 ) return 1;         /* ベクトルは第 2 引数 */
	return pig_val_translate_keeps_xy(arg);
}
static const ArgKind MEASURE_IN[] = { AK_CACHE };  /* 計測(値返し): mesh 1 個入力 → 値(AK_INLINE)出力 */
static const ArgKind THIN_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };  /* thin_spots(m, t, rays, cone) */
static const ArgKind REMESH_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };  /* remesh(m, len, iter, sharp) */
static const ArgKind CAST_IN[] = { AK_INLINE, AK_CACHE };  /* cast(type_string, mesh): type=inline, mesh=cache(reader) */
static const cgaOpEntry OPS[] = {
	/* ★ rev4 Phase B spike: 代表 op に型シグネチャ (実装型・cg-mesh3d/cg-cross2d) を付与。
	 *   残りの op と decide_executor 消費は Q-A 書き味確認後 (B-1 全注釈 + B-2)。 */
	{ "box",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(cgaBox),          0, "->cg-mesh3d" },  /* leaf 3D 生成 (mesh 入力なし) */
	{ "boxa",         SHAPE1_IN, 1, AK_CACHE, OPWIRE(cgaBox), 0, "->cg-mesh3d" },  /* 寸法を array(構造 inline)で */
	/* ★★ #3554 最後の段 3/5 (2026-09-19): import は **出力型ごとに 1 行**。
	 *   行を選ぶのは共通述語 @pig_match_import_ext@ = 「拡張子が産む型 (import_exts の
	 *   型付き CSV) が **この行の sig の出力型**か」。
	 *   ⚠⚠ import は *出力型が拡張子で決まる* ので **sig だけでは行が決まらない** (cast は
	 *     引数が型名だったが、こちらは拡張子 → 型の対応表が別に要る)。だから 1 行に
	 *     出力型を 3 つ書いていた旧形では、入力型 0 個の sigline が **先頭から当たり**、
	 *     .svg を読んでも cg-mesh3d を名乗っていた。
	 *   ★ 根拠は申告 (import_exts の型 ＋ sig の出力型) の **一致**。行名は読んでいない。
	 *     ⇒ 「CSV は読めると言うのに sig がその型を産まない」= 記述子の嘘は、
	 *       ロード時検査 (pig_descriptor_violation) が弾く。
	 *   ★ #3533: dxf は OCS を持てるので face3d。svg は z=0 に限るので cross2d。 */
	{ "import#cg-mesh3d",  SHAPE1_IN, 1, AK_CACHE, OPWIRE(cgaImport), 0, "->cg-mesh3d",  0, 0, 0, &pig_match_import_ext },   /* off / stl / obj / ply */
	{ "import#cg-cross2d", SHAPE1_IN, 1, AK_CACHE, OPWIRE(cgaImport), 0, "->cg-cross2d", 0, 0, 0, &pig_match_import_ext },   /* svg */
	{ "import#cg-face3d",  SHAPE1_IN, 1, AK_CACHE, OPWIRE(cgaImport), 0, "->cg-face3d",  0, 0, 0, &pig_match_import_ext },   /* dxf (OCS つき) */
	{ "prism",        SHAPE3_IN, 3, AK_CACHE, OPWIRE(cgaPrism), 0, "->cg-mesh3d" },
	{ "pyramid",      SHAPE3_IN, 3, AK_CACHE, OPWIRE(cgaPyramid), 0, "->cg-mesh3d" },
	/* ★ #3474: 基本立体はカーネル差が出ないので全カーネルに置く (common/solids.h)。 */
	{ "cylinder",      SHAPE3_IN, 3, AK_CACHE, OPWIRE(cgaCylinder), 0, "->cg-mesh3d", 0, 0, 2 },  /* cylinder(r,h,seg): 原点中心・軸 +Z */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "cone",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(cgaCone), 0, "->cg-mesh3d", 0, 0, 2 },  /* cone(r,h,seg): 原点中心・軸 +Z */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "torus",         SHAPE3_IN, 3, AK_CACHE, OPWIRE(cgaTorus), 0, "->cg-mesh3d", 0, 0, 2 },  /* torus(R,r,seg): 原点中心・軸 +Z */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "tetrahedron",   SHAPE1_IN, 1, AK_CACHE, OPWIRE(cgaTetrahedron), 0, "->cg-mesh3d" },  /* tetrahedron(r): 外接球半径 r */
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(cgaSphere), 0, "->cg-mesh3d", 0, 0, 1 },  /* sphere(r, seg): seg=円周分割数(既定 32 相当) */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	{ "icosphere",    SHAPE2_IN, 2, AK_CACHE, OPWIRE(cgaIcosphere), 0, "->cg-mesh3d", 0, 0, 1 },  /* icosphere(r, subdiv): subdiv=細分回数(既定0=20面) */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	{ "union",        BINMESH_IN,2, AK_CACHE, OPWIRE(cgaUnion, cgMesh, cgMesh),        0, "[cg-mesh3d,mf-mesh3d,gg-mesh3d,ch-mesh3d,nfb-mesh3d](2)->cg-mesh3d;[cg-cross2d,mf-cross2d](2)->cg-cross2d;[cg-face3d,mf-face3d,cg-cross2d,mf-cross2d](2)->cg-face3d", 1 /* ★可換 */ },  /* 二項 3D */
	{ "combine",      BINMESH_IN,2, AK_CACHE, OPWIRE(cgaCombine, cgMesh, cgMesh), 0, "[cg-mesh3d,mf-mesh3d,gg-mesh3d,ch-mesh3d,nfb-mesh3d](2)->cg-mesh3d;[cg-cross2d,mf-cross2d](2)->cg-cross2d;[cg-face3d,mf-face3d,cg-cross2d,mf-cross2d](2)->cg-face3d", 1 /* ★可換 */ },  /* +++ 交差許容の単純合体(viewer 用) */
	{ "intersection", BINMESH_IN,2, AK_CACHE, OPWIRE(cgaIntersection, cgMesh, cgMesh), 0, "[cg-mesh3d,mf-mesh3d,gg-mesh3d,ch-mesh3d,nfb-mesh3d](2)->cg-mesh3d;[cg-cross2d,mf-cross2d](2)->cg-cross2d;[cg-face3d,mf-face3d,cg-cross2d,mf-cross2d](2)->cg-face3d", 1 /* ★可換 */ },
	{ "difference",   BINMESH_IN,2, AK_CACHE, OPWIRE(cgaDifference, cgMesh, cgMesh), 0, "[cg-mesh3d,mf-mesh3d,gg-mesh3d,ch-mesh3d,nfb-mesh3d](2)->cg-mesh3d;[cg-cross2d,mf-cross2d](2)->cg-cross2d;[cg-face3d,mf-face3d,cg-cross2d,mf-cross2d](2)->cg-face3d" },
	/* ★ #3511: 凸包。**1 個でも受ける**ので nin=1 + variadic=1。ブールと違い corefinement を
	 *   通らず点しか見ないので、**n 項がそのまま 1 回の convex_hull で済む** = `(*)` と書ける
	 *   (union が `(2)` 止まりなのは二項 API しか無いから)。可換 = 1。 */
	/* ★★ #3528: **点群も受ける**。hull はもともと入力から頂点しか使っていないので、点群は
	 *   拡張ではなく **素の入力**で、メッシュを渡す方が「頂点以外を捨てる」特殊ケースだった。
	 *   ★ "(*!)" = **分解禁止**。hull は分解すると「点 → メッシュ → また頂点にばらす」を繰り返す
	 *     だけで得が無く、⚠⚠ それ以前に退化した群で落ちうる (退化検査が部分集合について
	 *     閉じていないため)。主型による振り分けは保ったまま分解だけ止める。
	 *   ⚠ 点群だけの呼び出しは入力にメッシュ型が 1 つも無いので、型による振り分けが効かない
	 *     = **既定カーネル (priority) が決める** (box() と同じ leaf 的な振る舞い)。 */
	{ "hull",         MEASURE_IN,1, AK_CACHE, OPWIRE(cgaHull, PIGWIRE_ANY(cgMesh, ptCloud)), 1,
	                                                 /* ★ #3533: 単項は **平面から出ない** ので face3d は face3d のまま。 */
	                                                 "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d;(cg-face3d)->cg-face3d"
	                                                 ";(pt-cloud3d)->cg-mesh3d;(pt-cloud2d)->cg-cross2d"
	                                                 /* ★★ #3533 規約③ (ひさ裁定 2026-09-15): n 項の答えは
	                                                  *   **「全部が z=0 の簡易表現なら 2D・1 つでも空間に出れば立体」**。
	                                                  *   ⇒ #3533 の 1 節の 3 例が *sig だけ* で決まる。
	                                                  *   ⚠ @translate@ は規約①で face3d を返すので、2D の凸包が欲しければ
	                                                  *     @hull(cast("cg-cross2d", translate(circle(1),[10,0,0])), circle(1))@ と
	                                                  *     **明示的に降ろしてから**書く。面倒だが、*型で答えの次元が決まる* 方を採る。
	                                                  *   ★★ 2 行目が書けるのは "(N!)" が **分解禁止** だから (#3528)。fold 形の
	                                                  *     「出力は主型」は *分解が 1 行で閉じる* ことの帰結なので、畳まない行には
	                                                  *     当たらない ⇒ 主型 cg-face3d ・出力 cg-mesh3d が成立する
	                                                  *     (2026-09-15 にロード時の検査をここへ合わせた)。
	                                                  *   ⚠ 行の順序が意味を持つ (同じモジュール内では先に一致した行を採る)。 */
	                                                 ";[cg-mesh3d,mf-mesh3d,gg-mesh3d,ch-mesh3d,nfb-mesh3d,pt-cloud3d,cg-face3d,mf-face3d,cg-cross2d,mf-cross2d,pt-cloud2d](*!)->cg-mesh3d"
	                                                 ";[cg-face3d,mf-face3d,cg-cross2d,mf-cross2d,pt-cloud2d,pt-cloud3d](*!)->cg-mesh3d"
	                                                 ";[cg-cross2d,mf-cross2d,pt-cloud2d](*!)->cg-cross2d"
	                                                 /* ★ 点群だけの n 項は主型 (cg-mesh3d) が 1 個も無いので上の行にマッチしない。
	                                                  *   繰り返し形で受ける (主型を持たず、出力が集合の外でよい形)。 */
	                                                 ";({pt-cloud3d}...)->cg-mesh3d"
	                                                 ";({pt-cloud2d}...)->cg-cross2d"
	                                                 , 1 /* ★可換 */ },
	/* ★★ #3554 最後の段 4/5 (2026-09-19): export の行は共通述語 @pig_match_export_ext@ が選ぶ
	 *   (= 第 1 引数の拡張子を **d->export_exts** が書けるか)。出力は常に @ref@ なので
	 *   **行を分ける必要は無い** (cast / import と違うのはここ)。
	 *   ⚠⚠ 同時に **規約① (自型優先) を撤去**した — 「入力型の home カーネルが書けるならそこ」
	 *     という routing の特例で、sig でも記述子でもない *3 つめの規則* だった。
	 *     ⇒ いまは priority × sig × 拡張子 の普通の決着。 */
	{ "export",       EXPORT_IN, 3, AK_CACHE, OPWIRE(cgaExport, cgMesh), 0, "(cg-mesh3d)->ref;(cg-cross2d)->ref;(cg-face3d)->ref;(mf-mesh3d)->ref;(mf-cross2d)->ref;(mf-face3d)->ref;(gg-mesh3d)->ref", 0, 0, 0, &pig_match_export_ext },  /* 出力=D_REF。mf 入力も引受 (Stage2: export の読解 capability を sig 化・cgal は universal reader) */
	/* ★★ #3554 段5: **xy 内の平行移動は 2D のまま返す** (規約① の例外・段4 の transform と同じ形)。 */
	{ "translate#xy", MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaTranslate, cgMesh), 0, "(cg-cross2d)->cg-cross2d", 0, 0, 0, &cg_match_trans_xy },
	{ "translate",    MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaTranslate, cgMesh), 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-face3d;(cg-face3d)->cg-face3d" },
	/* ⚠ **軸 z のときだけ** — マッチ関数は引数を 1 個ずつしか見られないので、角度が要る
	 *   「x 軸 180 度」のような平面を保つ回転は救えない (保守的に face3d のまま)。 */
	{ "rotate#z",     ROTATE_IN, 3, AK_CACHE, OPWIRE(cgaRotate, cgMesh), 0, "(cg-cross2d)->cg-cross2d", 0, 0, 0, &cg_match_rot_z },
	{ "rotate",       ROTATE_IN, 3, AK_CACHE, OPWIRE(cgaRotate, cgMesh), 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-face3d;(cg-face3d)->cg-face3d" },
	{ "mirror#xy", MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaMirror, cgMesh), 0, "(cg-cross2d)->cg-cross2d", 0, 0, 0, &cg_match_mirror_xy },
	{ "mirror",       MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaMirror, cgMesh), 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-face3d;(cg-face3d)->cg-face3d" },
	{ "scale#xy",     MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaScale, cgMesh), 0, "(cg-cross2d)->cg-cross2d", 0, 0, 0, &cg_match_scale_xy },
	{ "scale",        MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaScale, cgMesh), 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-face3d;(cg-face3d)->cg-face3d" },
	/* ★★ #3554 段4: **xy 平面に帰着する変換は 2D のまま返す**。規約① (transform 系は常に
	 *   face3d) は「軸は式の中の値なので型では区別できない」ことの代償だったが、AK_MATCH が
	 *   その前提を外した。⇒ *行列が平面を保つ*なら cross2d を返す。
	 *   ⚠ **変種を先に置く** (無条件の行が前に在ると変種が永久に選ばれない = ロード時に弾かれる)。
	 *   ⚠ 救えるのは cross2d 入力だけ — face3d は枠が任意なので行列だけでは決まらない。
	 *     face3d → cross2d の降格は 規約② (cast が幾何を見る) のまま。 */
	{ "transform#xy", MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaTransform, cgMesh), 0, "(cg-cross2d)->cg-cross2d", 0, 0, 0, &cg_match_xform_xy },
	{ "transform",    MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaTransform, cgMesh), 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-face3d;(cg-face3d)->cg-face3d" },
	{ "color",        MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaColor, cgMesh), 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d;(cg-face3d)->cg-face3d;(mf-mesh3d)->cg-mesh3d;(mf-cross2d)->cg-cross2d;(mf-face3d)->cg-face3d;(gg-mesh3d)->cg-mesh3d" },  /* 面色 f:color (mf 入力も引受=cgal 専用) */
	{ "rect",         SHAPE2_IN, 2, AK_CACHE, OPWIRE(cgaRect),        0, "->cg-cross2d" },  /* leaf 2D 生成 */
	{ "ngon",         SHAPE2_IN, 2, AK_CACHE, OPWIRE(cgaNgon), 0, "->cg-cross2d" },  /* 2D 正 n 角形 */
	{ "circle",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(cgaCircle), 0, "->cg-cross2d", 0, 0, 1 },  /* circle(r, segs): segs=多角形辺数(既定32) */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	{ "polygon",      SHAPE1_IN, 1, AK_CACHE, OPWIRE(cgaPolygon), 0, "->cg-cross2d" },  /* 2D 明示点列 */
	{ "line",         SHAPE1_IN, 1, AK_CACHE, OPWIRE(cgaLine), 0, "->cg-cross2d" },  /* 2D ガイド(寸法線・開ポリライン) */
	{ "extrude",      MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaExtrude, cgMesh),     0, "(cg-cross2d)->cg-mesh3d;(cg-face3d)->cg-mesh3d" },  /* 2D→3D 角柱 (次元変化・Q-C)。★ #3533: 置かれた断面も押し出せる (平面が world +Z を含むかは **値**の検査・型では書けない) */
	/* ★★ #3511: **loft_ruled** — 断面の列を直線で結ぶ立体。@extrude@ / @revolve@ / @tube@ が
	 *   断面 1 枚なのに対し、*断面が複数枚で形が変わってよい* のがこれ。
	 *   ★ 断面の置き場所は op が決めない (利用者が transform で空間に置く・#3526 の枠)。
	 *   ★ sig は **繰り返し形 + "[]"**: 2 枚以上を並べても配列 1 個でも渡せる。
	 *     ⚠ **fold 形にはできない** — loft_ruled(loft_ruled(a,b),c) は loft_ruled(a,b,c) と
	 *       別物 (断面の列は分解できない) なので、木に分解してはいけない。
	 *   ★ なめらかな @loft@ は置かない (解析曲面が要る = occt だけ)。 */
	{ "loft_ruled",   0,          0, AK_CACHE, OPWIRE(cgaLoftRuled, cgMesh), 1, "{cg-cross2d,cg-face3d}...[]->cg-mesh3d" }   /* ★ #3533: 断面は 2 型が混ざる (1 枚目だけ transform 前、等) ので **1 つの集合**で受ける */,
	/* ★★★ #3588 (2026-09-23): **出力型ごとに行を分ける**。
	 *   旧: 1 行に "->cg-mesh3d;->cg-cross2d" と並べていた (「import と同じ多出力注釈」のつもり)。
	 *   ⚠ import と違うのは **行を選ぶマッチ関数が無かった**こと。この op は幾何入力を持たない
	 *     (path も segs も値) ので、@sig_dispatch@ が照合できる入力型が 1 つも無く
	 *     **必ず先頭の sigline が勝つ** ⇒ 2D の帯まで cg-mesh3d を名乗り、@extrude@ と
	 *     2D ブールに拒まれていた (キャッシュの D_META は 'PLY2' = **op は正しく 2D を作っていた**)。
	 *   ⇒ import と同じ形 (1 行 1 出力型 + マッチ関数) にする。振り分けは **path の次元**。
	 *   ⚠⚠ **2D を先に置く** — 3D 側は「2D でないもの」を受ける catch-all (壊れた path を
	 *     op の具体的な診断へ届けるため) なので、逆順だと 2D の行が永久に選ばれない。
	 *   ★ 計算本体は 2 行とも同じ @cgaTube@。分けているのは *申告* だけ (cast と同じ立て付け)。 */
	{ "tube_ruled#cg-cross2d", SHAPE2_IN, 2, AK_CACHE, OPWIRE(cgaTube), 0, "->cg-cross2d", 0, 0, 1, &pig_match_path_is_2d },  /* tube_ruled(path, segs): 2D 折れ線を半幅 r で太らせた帯 */  /* ★ nreq=1: 以降は省略可 */
	{ "tube_ruled#cg-mesh3d",  SHAPE2_IN, 2, AK_CACHE, OPWIRE(cgaTube), 0, "->cg-mesh3d",  0, 0, 1, &pig_match_path_is_3d },  /* tube_ruled(path, segs): 3D 折れ線まわりの掃引立体 */  /* ★ nreq=1: 以降は省略可 */
	{ "revolve",      ROTATE_IN, 3, AK_CACHE, OPWIRE(cgaRevolve, cgMesh), 0, "(cg-cross2d)->cg-mesh3d;(cg-face3d)->cg-mesh3d", 0, 0, 1 },  /* revolve(m,angle,segs): 2D→3D 回転体 */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	/* ★2D のみ (#3440 の 2): **3D offset は nef へ移設**した。3D の中身は Minkowski 和
	 * (Nef + 凸分解) で、他の幾何カーネルの機能を借りて cgal の顔で出していた = モジュール境界の
	 * 約束①違反だった (docs/srava_module_reference.md「モジュールの境界」章)。
	 * 2D は straight skeleton (面取り) で Nef と無関係なので、ここに残る。 */
	{ "offset",       ROTATE_IN, 3, AK_CACHE, OPWIRE(cgaOffset, cgMesh),      0, "(cg-cross2d)->cg-cross2d;(cg-face3d)->cg-face3d", 0, 0, 2 },  /* ★ nreq=2: 以降は省略可 (既定は op が入れる) */
	/* ★ #3443: 頂点数 / 面数。planner が cache のバイト列を直接読んで表示していたのを op へ移した。 */
	{ "nverts",       MEASURE_IN,1, AK_INLINE,OPWIRE(cgaNverts, cgMesh), 0, "(cg-mesh3d)->value;(cg-cross2d)->value;(cg-face3d)->value" },
	/* ★★ #3527: **nverts の相方**。7 モジュールが頂点を数えられるのに、座標を読む op は
	 *   1 本も無かった (2026-09-16 に数えた)。⇒ 3 つ組の「取り出す」側をここから埋める。
	 *   ⚠ 索引は **実装依存**。⇒ 同じ式が同じ i で同じ頂点を返すことを test/srava_vert.sh が見る。 */
	{ "vert",         MESH1ARG_IN,2, AK_INLINE,OPWIRE(cgaVert, cgMesh), 0, "(cg-mesh3d)->value;(cg-cross2d)->value;(cg-face3d)->value" },
	/* ★ 3 つ組の「まとめて」側。⚠ 受け皿の型が在るのは **点群だけ**なので、
	 *   faces(B) / parts(v) のような op は作らない (#3527 の規約 ④)。
	 * ★ 借りているのは *幾何の機能* ではなく **値の器** — 前例は estimate_normals。 */
	/* ★★ #3527 段 5: **face3d は pt-cloud3d** (world)。cross2d は従来どおり pt-cloud2d (局所)。
	 *   op_bbox / op_centroid が #3533 で決めた「face3d は world」をここへ揃えたもの。
	 *   ⚠ 揃える前は 置き場所が黙って落ちて hull(verts(sec)) が常に z=0 に出ていた。 */
	{ "verts",        MEASURE_IN, 1, AK_CACHE, OPWIRE(cgaVerts, cgMesh), 0, "(cg-mesh3d)->pt-cloud3d;(cg-cross2d)->pt-cloud2d;(cg-face3d)->pt-cloud3d" },
	{ "nfaces",       MEASURE_IN,1, AK_INLINE,OPWIRE(cgaNfaces, cgMesh), 0, "(cg-mesh3d)->value;(cg-cross2d)->value;(cg-face3d)->value" },
	{ "area",         MEASURE_IN,1, AK_INLINE,OPWIRE(cgaArea, cgMesh), 0, "(cg-mesh3d)->value;(cg-cross2d)->value;(cg-face3d)->value" },  /* area(m): 値返し(2D 面積 / 3D 表面積) */
	{ "valid",        MEASURE_IN,1, AK_INLINE,OPWIRE(cgaValid, cgMesh), 0, "(cg-mesh3d)->value;(cg-cross2d)->value;(cg-face3d)->value" },  /* valid(m): 値返し(1=正常/0=問題) */
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
	{ "nshells",      MEASURE_IN,1, AK_INLINE,OPWIRE(cgaNshells, cgMesh), 0, "(cg-mesh3d)->value" },
	/* ★★ #3525: **2D にも広げた**。約束は「その値が構造として持っている片の数」で、3D で
	 *   既にそうだった (nef = marked volume の数 / cgal = 符号つき体積が正のシェルの数) ものを
	 *   2D に揃えただけ = regions() の数。⇒ voronoi のようにセルが互いに接する分割もそのまま乗る。
	 *   ⚠ nshells / genus は 2D に無いので **広げない** (曲面の量なので 2D では定義できない)。 */
	{ "nparts",       MEASURE_IN,1, AK_INLINE,OPWIRE(cgaNparts, cgMesh), 0, "(cg-mesh3d)->value;(cg-cross2d)->value;(cg-face3d)->value" },
	{ "genus",        MEASURE_IN,1, AK_INLINE,OPWIRE(cgaGenus, cgMesh), 0, "(cg-mesh3d)->value" },
	/* ★ #3514: **点との距離** — p から境界までの最短距離 (符号なし)。値返し。
	 *   ⚠ 既存の distance(a,b) は **2 つの立体**の間の距離。問うているものが違うので別名にした
	 *     (位置で指す _at は face_at と同じ流儀)。閉形式: 球 (半径 r) の中心から距離 d の点 → |d - r|。
	 */
	{ "distance_at",  MESH1ARG_IN,2, AK_INLINE,OPWIRE(cgaDistanceAt, cgMesh), 0, "(cg-mesh3d)->value" },
	/* ★ #3528: **点群の法線を推定する** — estimate_normals(p[,k])。点群 → 法線つきの点群。
	 *   ⚠⚠ これが独立した op である理由は「**既定の法線を作らない**」ため。(0,0,1) 等で埋めると
	 *     Poisson も RANSAC もエラーにならずに走り、静かに嘘の形を返す。⇒ 推定は明示的に呼ぶ値にし、
	 *     法線を要求する op は無ければ明示エラーにする (黙って推定しない)。
	 *   ★ 型 pt-cloud3d は cgal のものではない — **中立の libsrava_pt** が持つ (点群はどの
	 *     カーネルでも「平坦な double 配列」で、保存すべきカーネル固有表現が無いため)。
	 *     ここは借りているだけで、cgCacheCodec.cpp が &ptCloud::WIRE を provides に並べている。
	 *   ★ cgal が持つ理由: 法線を要求する当の相手 (Poisson / RANSAC・#3515) が CGAL にあり、
	 *     **向きなし推定 (pca) と向き付け (mst) が両方そろっている** = 型の 2 つの印を正しく立てられる。
	 *   ⚠ k (近傍数) は省略可 (既定 18)。 */
	{ "estimate_normals", MESH1ARG_IN,2, AK_CACHE, OPWIRE(cgaEstimateNormals, ptCloud), 0, "(pt-cloud3d)->pt-cloud3d", 0, 0, 1 },
	/* ★★ #3525: **Voronoi 図** — voronoi(p, box)。2D 点群 → セルに分けられた 2D 領域。
	 *   ★ Delaunay を経由せず、セルの定義どおりに箱を半平面で削って作る (cgVoronoi.cpp)。
	 *   ★ 出力は 1 つの値の中に **セルを片として並べて**持つ ⇒ nparts / part がそのまま索引。
	 *     @part(voronoi(p,box), i)@ = p の i 番目のサイトのセル (並べ替えが 1 回も無いので構造的)。
	 *   ⚠ 入力は **点群型だけ** (値の配列は受けない — 位置ごとに AK_ は 1 つしか宣言できないため。
	 *     distance / distance_at を分けたのと同じ理由)。⇒ 値から来るなら points(...) を通す。
	 *   ⚠ 3D は入れない (#3525 の判断: セル数が最悪 O(N^2)・消費者がいない・別の表現クラスが要る)。 */
	{ "voronoi",      MESH1ARG_IN,2, AK_CACHE, OPWIRE(cgaVoronoi, ptCloud), 0, "(pt-cloud2d)->cg-cross2d;(pt-cloud3d)->cg-mesh3d" },
	/* ★ #3525: **Delaunay 三角形分割** — delaunay(p)。voronoi の前段ではなく独立した op。
	 *   ⚠⚠ 三角形の番号は **実装依存** (voronoi のセル番号は定義で決まるのと対照的・#3527)。
	 *     しかもキャッシュに焼き付くので、上流の版が変われば同じ式が別の片を返しても
	 *     値としては正常に見える ⇒ 数える / 全部回す のは安全、i 番目を名指すのは危うい。 */
	{ "delaunay",     MEASURE_IN, 1, AK_CACHE, OPWIRE(cgaDelaunay, ptCloud), 0, "(pt-cloud2d)->cg-cross2d;(pt-cloud3d)->cg-mesh3d" },
	/* ★ #3525: **片の取り出し** — part(v, i)。2D 版 (3D の part は nef が持つ・同名別型)。 */
	/* ⚠⚠ @(cg-mesh3d)@ の行は **nef が既に持っていた** ("(cg-mesh3d)->nfb")。priority が
	 *   cgal 20 > nef 5 なので、この行を足すと *routing がこちらへ移る*。
	 *   ⇒ 片リストを持たない値では **nef と同じ答え** (面の連結成分) になるようにし、
	 *     nef にしか出せない形 (空洞 = シェルの入れ子が要る) は **明示エラーで nef を名指す**。
	 *     黙って違う答えを返さないための線引き。わけは cgMesh3D::op_part。 */
	{ "part",         MESH1ARG_IN,2, AK_CACHE, OPWIRE(cgaPart, cgMesh), 0, "(cg-cross2d)->cg-cross2d;(cg-face3d)->cg-face3d;(cg-mesh3d)->cg-mesh3d" },
	/* ★ #3527: nshells は 3 モジュールが数えられるのに **取り出せなかった**。
	 * ★ part との違いは「空洞を断らない」1 点。向きは **そのまま** (案 A) なので
	 *   空洞の殻は volume が負で返り、**符号が判別子**になる。 */
	{ "shell",        MESH1ARG_IN,2, AK_CACHE, OPWIRE(cgaShell, cgMesh), 0, "(cg-mesh3d)->cg-mesh3d" },
	/* ★ 位置で指す側。**番号には指す先が無い**ので、書き換えに強いのはこちら (#3518 と同じ理由)。 */
	{ "shell_at",     MESH1ARG_IN,2, AK_CACHE, OPWIRE(cgaShellAt, cgMesh), 0, "(cg-mesh3d)->cg-mesh3d" },
	/* ★★ #3527 の規約③「片の名前を付けたら **3 つ組で名乗る**」を cgal 自身が満たしていなかった。
	 *   #3510 の表に「片を取り出す」の行を立てたら cgal だけ 3/4 と出て判明 (2026-09-18)。
	 *   ⇒ **表を作ったことが歯抜けを見つけた**。同じ理由で face_verts も足す (「頂点を読む」が 2/3)。
	 * ⚠ part_at は @shell_at@ と意味が **わざと違う** — こちらは「**含む**」(立体は内側を持つ)。 */
	{ "part_at",      MESH1ARG_IN,2, AK_CACHE, OPWIRE(cgaPartAt, cgMesh), 0,
	  "(cg-cross2d)->cg-cross2d;(cg-face3d)->cg-face3d;(cg-mesh3d)->cg-mesh3d" },
	/* ⚠ 2D は面を持たないので 2D の行は置かない (ルータが先に弾く)。 */
	{ "face_verts",   MESH1ARG_IN,2, AK_INLINE, OPWIRE(cgaFaceVerts, cgMesh), 0, "(cg-mesh3d)->value" },
	{ "repair",       MEASURE_IN,1, AK_CACHE, OPWIRE(cgaRepair, cgMesh), 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d;(cg-face3d)->cg-face3d;(mf-mesh3d)->cg-mesh3d;(mf-cross2d)->cg-cross2d;(mf-face3d)->cg-face3d;(gg-mesh3d)->cg-mesh3d" },  /* repair(m): mesh 返し(3D autorefine / 2D even-odd)・mf 入力も引受 */
	/* ★★ #3534: **project_flatten** — world の (x,y) を取り z を捨てる = z=0 への直投影。
	 *   ⚠ @project@ (#3518・平面図形を曲面へ投影して切る・occt) とは別の op。紛れないよう複合語。
	 *   ★ 出力は必ず **cross2d** (置き場所を失うので face3d ではない) ⇒ SVG へ書けるようになる。
	 *     これが #3533 の規約④「SVG は face3d を断る」に対する唯一の出口。
	 *   ★ 既に z=0 に居るもの (cross2d) も受ける — **冪等**なので恒等。「もう平らか」を
	 *     利用者に判定させない (型で分けると `cast` と `project_flatten` を使い分ける羽目になる)。 */
	{ "project_flatten", MEASURE_IN,1, AK_CACHE, OPWIRE(cgaProjectFlatten, cgMesh), 0, "(cg-face3d)->cg-cross2d;(cg-cross2d)->cg-cross2d;(mf-face3d)->cg-cross2d;(mf-cross2d)->cg-cross2d" },
	/* ★ #3512: 三角形の張り方を作り直す 2 本。**形は保ち、面数と三角形の質だけ動かす**。
	 *   狙いが逆向きなので対で入れてある (remesh は質を上げて面数が増え、simplify は
	 *   面数を落として質が下がる)。どちらも 3D 専用 (2D は面を持たない / 等方リメッシュの
	 *   対応物が無い)。⚠ 中身は EPICK コピー上で解く — 理由は cgRemesh.cpp の冒頭。
	 *   ⚠ 他カーネルの mesh は **受けない**。remesh/simplify は geogram / manifold も
	 *     ライブラリを持つので (#3510 の表)、ここで (gg,mf) まで名乗ると後から
	 *     そちらを配線したときに同じ入力対を 2 モジュールが主張する (disjoint 原則)。 */
	{ "refine",       MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaRefine, cgMesh),   0, "(cg-mesh3d)->cg-mesh3d" },  /* refine(m,len): 形は厳密に不変・面密度だけ上げる */
	{ "remesh",       REMESH_IN, 4, AK_CACHE, OPWIRE(cgaRemesh, cgMesh),   0, "(cg-mesh3d)->cg-mesh3d", 0, 0, 2 },  /* ★ nreq=2: iter/sharp は省略可 (既定 3 / 60° は op が入れる) */
	{ "simplify",     MESH1ARG_IN,2,AK_CACHE, OPWIRE(cgaSimplify, cgMesh), 0, "(cg-mesh3d)->cg-mesh3d" },  /* simplify(m,n): 目標面数 **以下**で止まる */
	{ "section",      SECTION_IN,4, AK_CACHE, OPWIRE(cgaSection, cgMesh), 0, "(cg-mesh3d)->cg-face3d" },  /* section(m,P,N,mode): mode 0=平面ちょうど/-1=直下/+1=直上 */
	{ "empty2d",      0,         0, AK_CACHE, OPWIRE(cgaEmpty2D), 0, "->cg-cross2d" },  /* 空集合(2D)。{} は中立元なので別物 */
	{ "empty3d",      0,         0, AK_CACHE, OPWIRE(cgaEmpty3D), 0, "->cg-mesh3d" },   /* 空集合(3D) */
	{ "volume",       MEASURE_IN,1, AK_INLINE,OPWIRE(cgaVolume, cgMesh),      0, "(cg-mesh3d)->value" },  /* 値出力 (out=value) */
	{ "perimeter",    MEASURE_IN,1, AK_INLINE,OPWIRE(cgaPerimeter, cgMesh), 0, "(cg-cross2d)->value;(cg-face3d)->value;(mf-cross2d)->value;(mf-face3d)->value" },  /* perimeter(m): 値返し(2D 境界長・3D エラー)・mf 入力も引受 */
	{ "centroid",     MEASURE_IN,1, AK_INLINE,OPWIRE(cgaCentroid, cgMesh), 0, "(cg-mesh3d)->value;(cg-cross2d)->value;(cg-face3d)->value" },  /* centroid(m): 配列返し([x,y]/[x,y,z]) */
	{ "bbox",         MEASURE_IN,1, AK_INLINE,OPWIRE(cgaBbox, cgMesh), 0, "(cg-mesh3d)->value;(cg-cross2d)->value;(cg-face3d)->value" },  /* bbox(m): 入れ子配列返し([min隅,max隅]) */
	{ "distance",     BINMESH_IN,2, AK_INLINE,OPWIRE(cgaDistance, cgMesh, cgMesh), 0, "(cg-mesh3d,cg-mesh3d)->value;(cg-mesh3d,mf-mesh3d)->value;(mf-mesh3d,cg-mesh3d)->value;(mf-mesh3d,mf-mesh3d)->value;(cg-mesh3d,gg-mesh3d)->value;(gg-mesh3d,cg-mesh3d)->value;(gg-mesh3d,gg-mesh3d)->value" },  /* distance(a,b): 値返し(3D 最近接距離・近似) */
	{ "closest",      BINMESH_IN,2, AK_INLINE,OPWIRE(cgaClosest, cgMesh, cgMesh), 0, "(cg-mesh3d,cg-mesh3d)->value;(cg-mesh3d,mf-mesh3d)->value;(mf-mesh3d,cg-mesh3d)->value;(mf-mesh3d,mf-mesh3d)->value;(cg-mesh3d,gg-mesh3d)->value;(gg-mesh3d,cg-mesh3d)->value;(gg-mesh3d,gg-mesh3d)->value" },  /* closest(a,b): 配列返し([d,[pa],[pb]]) */
	{ "farthest",     BINMESH_IN,2, AK_INLINE,OPWIRE(cgaFarthest, cgMesh, cgMesh), 0, "(cg-mesh3d,cg-mesh3d)->value;(cg-mesh3d,mf-mesh3d)->value;(mf-mesh3d,cg-mesh3d)->value;(mf-mesh3d,mf-mesh3d)->value;(cg-mesh3d,gg-mesh3d)->value;(gg-mesh3d,cg-mesh3d)->value;(gg-mesh3d,gg-mesh3d)->value" },  /* farthest(a,b): 配列返し(頂点総当り・厳密) */
	{ "thin_spots",   THIN_IN,   4, AK_INLINE,OPWIRE(cgaThinSpots, cgMesh), 0, "(cg-mesh3d)->value;(mf-mesh3d)->value;(gg-mesh3d)->value", 0, 0, 2 },  /* thin_spots(m,t,rays,cone): 肉厚<t の面の[[x,y,z,thk],..](SDF・cone=コーン全角°)・mf 入力も引受 */  /* ★ nreq=2: 以降は省略可 (既定は op が入れる) */
	/* ★★ #3554 最後の段 2/5 (2026-09-19): cast は **目標型ごとに 1 行**。
	 *   行を選ぶのは共通述語 @pig_match_cast_target@ (第 1 引数の型名が この行の sig の出力型か) で、
	 *   routing の cast 専用ブロックはこれに置き換わって撤去された。
	 *   ⚠⚠ **1 行に出力型を 2 つ書いてはいけない** — 行の可否は「どれかの sigline が目標型を産むか」
	 *     で決まるのに、実際に名乗る型は *入力型で先に当たった sigline* から採るので、
	 *     @cast("cg-cross2d", <cg-mesh3d>)@ が cg-mesh3d を名乗る、という食い違いになる。
	 *     ⇒ ロード時に pig_descriptor_violation が弾く。
	 *   ★ 計算本体は 3 行とも同じ (identity)。**変換は行き先カーネルの reader が担う**ので、
	 *     行を分けても払う仕事は増えない。分けているのは *申告* だけ。 */
	{ "cast#cg-mesh3d", CAST_IN, 2, AK_CACHE, OPWIRE(cgaCast, cgMesh),        0,
	                                                 "(cg-mesh3d)->cg-mesh3d;(mf-mesh3d)->cg-mesh3d;(gg-mesh3d)->cg-mesh3d"
	                                                 ";(ch-mesh3d)->cg-mesh3d"     /* cherchi も MFM3 を名乗る = cg-mf-upgrade が読む */
	                                                 ";(nfb-mesh3d)->cg-mesh3d"    /* cg-nf-downgrade: NEFB。★ #3499: nf-mesh3d (nef_snc) は橋 nef_cg.so が受ける */
	                                                 /* ★★ #3527: geomutils の型。gu-mesh3d は **MFM3**・gu-cross2d / gu-face3d は
	                                                  *   **MFC2** を書くので、cgMesh::create_for_meta が **無改造で読める**
	                                                  *   (set_mfm3_input / set_mfc2_input の枝がそのまま当たる) ⇒ 足すのは sig の行だけ。
	                                                  *   part(mf-mesh3d,i) 等が gu-* を返すので、そこから厳密側へ戻る口になる。 */
	                                                 ";(gu-mesh3d)->cg-mesh3d", 0, 0, 0, &pig_match_cast_target },
	{ "cast#cg-cross2d", CAST_IN, 2, AK_CACHE, OPWIRE(cgaCast, cgMesh),       0,
	                                                 "(cg-cross2d)->cg-cross2d;(mf-cross2d)->cg-cross2d;(gu-cross2d)->cg-cross2d"
	                                                 /* ★★ #3533 規約②: **降格は cast だけ**。@frame_is_default()@ が偽なら
	                                                  *   明示エラーで、幾何は 1 ミリも動かさない。「空間にあるものを z=0 へ
	                                                  *   落とす」のは別 op (#3534 の project_flatten) の仕事。 */
	                                                 ";(cg-face3d)->cg-cross2d;(mf-face3d)->cg-cross2d;(gu-face3d)->cg-cross2d",
	                                                 0, 0, 0, &pig_match_cast_target },
	{ "cast#cg-face3d", CAST_IN, 2, AK_CACHE, OPWIRE(cgaCast, cgMesh),        0,
	                                                 "(cg-face3d)->cg-face3d;(mf-face3d)->cg-face3d;(gu-face3d)->cg-face3d",
	                                                 0, 0, 0, &pig_match_cast_target }
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgatsAgent_(
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


cgatsAgent_::cgatsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	cgatsAgent_::agent_ops()   { return OPS; }
int			cgatsAgent_::agent_n_ops() { return N_OPS; }
const char*		cgatsAgent_::agent_name()  { return "cgal"; }

/* この実行ファイル(srava_agent)の実行体として自分を登録する(#3406 4.2)。
 * root(ptsApplication)は具体クラスを知らず、enable() でこの生成子を引いて起こす。
 * §5 の .so 化では、この登録が dlopen 時の登録に置き換わる。 */
static sPtr<ptsAgent>
mk_cgatsAgent(sPtr<ptsObject> med)
{
	return thNEW(cgatsAgent,(med));
}

/* ★ .so 化 Phase 3b: cgal カーネルの **フル記述子** (make_agent + OPS 付き)。cgal.so の
 * manifest.cpp がこれを extern 参照して srava_module() で公開する。planner 側の meta-only 記述子
 * (pigfModuleAgent.cpp の cgal_module_descriptor) とは別物で、こちらは実行体 (agent) を含む。
 * OPS/mk_cgatsAgent は同一 TU の static なのでここで組む。exec_caps=PROCESS のみ (CGAL は
 * thread 不可)・**priority=20 (> manifold 10) = cgal が既定カーネル** (下の記述子を参照)・
 * 拡張子は多形式。静的自己登録はしない (dlopen 時に
 * ローダが register する / 静的 agent は従来どおり lookup(0) で引く)。 */
extern const pigModuleType cgal_provides[];
extern const srava_module_descriptor cgatsAgent_descriptor;
const srava_module_descriptor cgatsAgent_descriptor = {
	/* ★ .so 化 Phase 4c: priority 20 (> manifold 10) = **cgal を既定カーネルに** (ひさ判断
	 * 2026-08-08: manifold は watertight 前提でサイレント破綻し得るため opt-in)。manifold は
	 * cast("manifold",..) / module("manifold.so",{priority}) で明示選択する。 */
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "cgal",
	.priority      = 20,
	.make_agent    = &mk_cgatsAgent,
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	/* ★ rev4 Phase C: import_exts を **型付き** ("ext:出力型") に。形式が出力型を決める (svg/dxf=2D・
	 *   mesh 系=3D)。routing が import の出力型をこれで確定しスタンプする (polymorphic import 解消)。 */
	/* ★ #3533: **dxf は cg-face3d**。DXF は OCS (210/220/230) で置き場所を表せるので、
	 *   同じ .dxf が平面にあるか空間にあるかで型を変えられない (import_exts は拡張子 →
	 *   型の *静的な表*) ⇒ 一般表現に固定する (ひさ判断)。svg は平面しか表せないので
	 *   cg-cross2d のまま。 */
	.import_exts   = "off:cg-mesh3d,stl:cg-mesh3d,obj:cg-mesh3d,ply:cg-mesh3d,svg:cg-cross2d,dxf:cg-face3d",
	.export_exts   = "off,stl,obj,ply,3mf,amf,svg,dxf",   /* P2d: amf 追加 (型軸 export routing ② で解決させ arg_module fallback 依存を解消) */
	.provides      = cgal_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	/* ★ v18 (#3466): このモジュールが出す結果の版。**計算を変えたら手で上げる**。 */
	.cache_version = 10,  /* ⚠⚠ **v10 = 同じ日に別々の機体で 2 つの変更が入った**。片方の
	                       *   番号に寄せると **もう片方のキャッシュが古い誤値を返す** ので両方残す
	                       *   (bench の段 5 と mac の #3551 がどちらも v9 を名乗って衝突した)。
	                       *
	                       * ★ #3527 段 5 (bench): **face3d の vert / verts が world の 3 成分**に
	                       *   なった (従来は枠の中の 2 成分)。#3533 で bbox / centroid が先に
	                       *   world へ揃っており、vert だけ取り残されていた ⇒ 置き場所が黙って
	                       *   落ち、hull(verts(sec)) が常に z=0 に出ていた (2026-09-17 実測)。
	                       *   ⚠ verts の返り型も pt-cloud2d → **pt-cloud3d** に変わる。
	                       *
	                       * ★★ #3551: v9 = **DXF / SVG の読み手が開いた線をガイド層へ入れる**。
	                       *   ⚠ 従来は 70 (閉じ旗) を見ずに全部リングにしていたので、こちらが
	                       *     書いたガイド (line() の寸法線・70=0) を読み戻すと **閉じた領域に
	                       *     化けて面積が増えて**いた: combine(rect(4,4), line(...)) が
	                       *     16 → **20.5**。SVG では逆にガイドが消えていた (nverts 7 → 4)。
	                       *   ⇒ **同じ式の答えが変わる** ⇒ 古いキャッシュを引かせない。
	                       *   ★ 併せて、読めない幾何実体 (LINE / CIRCLE / ARC / ELLIPSE /
	                       *     SPLINE / bulge ・ path の A C Q…) は **明示エラー**にした
	                       *     (黙って落とさない・ひさ判断の案 z)。そちらは値を変えない。
	                       * ★ #3525: v8 = **落とすところで CGAL::exact() を通す**。EPECK の FT は
	                       *   Lazy_exact_nt で、@to_double@ は *区間近似* を返し **正しく丸められない**
	                       *   ⇒ 厳密なまま積んでも *積む式の形*で 1〜2 ulp 動いていた
	                       *   (同じ四辺形が 1 枚なら 8.5450000000000017 ・ 三角形 2 枚なら 8.545)。
	                       *   ⚠ 「厳密カーネルだから厳密」は代理。*どこで落としているか*を見る。
	                       * ★ #3525: v7 = **2D の面積も厳密なまま積む**。片ごとに double へ
	                       *   落としてから足していたため、*片に分けると和が全体と一致しなかった*
	                       *   (area(delaunay(p)) と area(hull(p)) が 1e-15 ずれる)。
	                       *   「片の測度の和 = 全体の測度」は #3527 が守る性質なので直した。
	                       * ★ #3525: v6 = **2D の重心を厳密なまま積むようにした**。以前は各項を
	                       *   double にして /6 してから足していたため、*同じ図形でも頂点の書き出し順で
	                       *   答えが変わって*いた ([1.5,1.5] と [1.4999999999999998,…])。⇒ 値が 1 ulp 級で
	                       *   動く ⇒ 古いキャッシュを引かせない。わけは cgMesh2D.cpp の ring_moment。
	                       * ★ #3533: v5 = **枠の軸表を 1 箇所に畳み、n∥y の行を右手系に揃えた**。
	                       *   y 平面で切った断面の局所座標の符号が変わる (x/z 平面と一般法線は
	                       *   1 ビットも動かない — 実測で確認)。
	                       * v4 (#3533) = **2D の型が 2 つ (cg-cross2d / cg-face3d)**。face3d の
	                       *   ブロブは枠が既定でも枠の節を書く (型を自分で名乗るため) ので、
	                       *   同じ式でも v3 とバイト列が変わる。⚠ v3 のブロブを引くと
	                       *   transform を通った 2D が **cross2d に戻って** hull の振り分けが
	                       *   変わる ⇒ 引かせてはいけない。
	                       * v3 (#3526) = **2D が枠 (平面) を持ち、section が切った場所に断面を返す**。
	                       *   ⚠ v2 のキャッシュには z=0 に落ちた断面が入っているので引けない。
	                       *   ★ 平面の 2D (枠が既定) の blob は v2 とバイト単位で同じなので、
	                       *     版を上げても「同じ形なら同じバイト列」という性質は保たれる。
	                       * v2 = valid が空メッシュに 0 を返す (共通定義 ① 空でない) */
	/* ★ v7 (#3419): op 内並列の方式と σ (docs/srava_load_control_design.md §5.5/§5.6)。
	 *   CGAL のブールは逐次。T1-d 実測でも TBB を引き込まない */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};

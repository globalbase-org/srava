/*
 * cgCacheCodec — CGAL カーネルのキャッシュコーデック定義 TU (#3406 / .so 化 Phase 4③')。
 *
 * ★ Phase 4③': 従来の「静的初期化で register_codec を自己登録」を廃し、**descriptor.codecs 配列**
 *   (cgal_codecs) を extern 公開する。cgatsAgent.cpp の記述子がこれを指し、ローダ (register_descriptor)
 *   が pigCacheCodec へ owner=cgal id 付きで登録する。これで reader が descriptor に接続され、
 *   記述子走査による reader 選択 (reader_for = 4CC × 要求型) が効く。
 *   cgal は 2 種の codec を持つ:
 *     - "MESH"/"PLY2" = cgal ネイティブ (writer あり)
 *     - "MFM3"/"MFC2" = Manifold キャッシュの **EPECK 昇格読み** (旧 cgCacheCodecUpgrade を統合)。
 *       reader は同じ ptscgWireCacheStreamReaderMesh (create_for_meta がタグから型を選び昇格) で、
 *       書きは cgal ネイティブが担うので match/writer は never/0。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsObject 派生 TU の作法 (ptsApp 完全型) */
#include	"cg/c++/cgMeshCgal.h"
#include	"cg/c++/ptscgWireCacheStreamReaderMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"pt/c++/ptCloud.h"   /* ★ #3528: 点群は中立の libsrava_pt が持つ (借りる) */

static sPtr<tinyState>
cg_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	/* create_for_meta がタグ (MESH/PLY2/MFM3/MFC2) から具体型を選ぶ。MFM3/MFC2 は EPECK へ昇格。 */
	return sPtr<tinyState>::d_cast(thNEW(ptscgWireCacheStreamReaderMesh,(parent, path)));
}

static sPtr<tinyState>
cg_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptscgWireCacheStreamWriterMesh,(parent, path, sPtr<cgMesh>::d_cast(body))));
}

static int
cg_match(sPtr<pigData> body)
{
	return sPtr<cgMesh>::d_cast(body).is_notNull();
}

static int
cg_match_never(sPtr<pigData>)
{
	return 0;   /* 昇格読み専用 (書きは cgal ネイティブ MESH/PLY2 が担う) */
}

/* ★ descriptor.codecs が指す配列 (name==0 番兵終端)。cgatsAgent.cpp が extern 参照。 */
/* ★ 2026-08-28 (ABI v12): この階層への配線先。reader は下の codec 行が使うものと同一 —
 *   どの行 (自型読み / foreign 昇格読み) でも reader は 1 本で、階層に帰属するため。 */
PIG_WIRE_DEF(cgMesh, cg_mk_reader, cg_mk_writer);

/* ★ 2026-08-28 (ひさ設計・ABI v16): このモジュールが提供するもの。
 *   1 行 = (本体クラス階層, その階層について名乗る型名, 扱う 4CC)。
 *   ⚠ **types と tags は位置対応しない** (独立した 2 本・個数も一致しない)。どのタグがどの型に
 *     なるかは申告せず、wire->create に通して訊く (pigModule.h の pigModuleType 参照)。
 *   ⚠ tags は **診断専用** — 読めるかを答えるのは wire->create 一本で、この欄は
 *     `srava --module-info` が列挙するための候補にすぎない (実行時の判断に使わない)。 */
extern const pigModuleType cgal_provides[];
const pigModuleType cgal_provides[] = {
	/* ★ #3533: 2D は **2 型が 1 つの 4CC (PLY2) を共有する**。逆引き (4CC → 型) は
	 *   2026-08-19 に撤去済みなので曖昧さは出ない — 正引き (型名 → 4CC) は一意のまま。 */
	{ &cgMesh::WIRE, "cg-mesh3d,cg-cross2d,cg-face3d",
	  "MESH,PLY2,MFM3,MFC2,NEFB" },   /* ★ #3499: NEF3 (nef_snc = 常に SNC) は読まない → 橋 nef_cg.so */
	/* ★ #3528: **自前のクラスを作らず libsrava_pt のクラスをそのまま並べる** (occt_mf が mfGeom を
	 *   借りるのと同じ作法)。estimate_normals が pt-cloud3d を読み書きするため、agent プロセスに
	 *   cgal.so しか load されない process 実行でも codec がここから届く必要がある。
	 *   ★ #3528 続き: hull が **2D の点群も受ける**ようになったので pt-cloud2d も名乗る
	 *     (estimate_normals だけの頃は 3D しか要らなかった)。 */
	{ &ptCloud::WIRE, PT_TYPE_2D "," PT_TYPE_3D, PT_TAG_2D "," PT_TAG_3D },
	{ 0, 0, 0 },
};


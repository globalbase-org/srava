/*
 * ggCacheCodec — geogram カーネルのキャッシュコーデック定義 TU (#3435 P3)。
 * descriptor.codecs (gg_codecs) を extern 公開し、ローダが owner=geogram id で登録する。
 * mfCacheCodec.cpp / nfCacheCodec.cpp のミラー。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"gg/c++/ggMesh.h"
#include	"gg/c++/ptsggWireCacheStreamReaderMesh.h"
#include	"gg/c++/ptsggWireCacheStreamWriterMesh.h"
#include	"pt/c++/ptCloud.h"   /* ★ #3528: 点群は中立の libsrava_pt が持つ (借りる) */

static sPtr<tinyState>
gg_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsggWireCacheStreamReaderMesh,(parent, path)));
}

static sPtr<tinyState>
gg_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsggWireCacheStreamWriterMesh,(parent, path, sPtr<ggGeom>::d_cast(body))));
}

static int
gg_match(sPtr<pigData> body)
{
	return sPtr<ggGeom>::d_cast(body).is_notNull();
}

/* 読取専用 codec 用の match: 書きは相手モジュールに任せ、この codec は writer を出さない。 */
static int
gg_match_never(sPtr<pigData>)
{
	return 0;
}

/* ★ 2026-08-28 (ABI v12): この階層への配線先。reader は下の codec 行が使うものと同一 —
 *   どの行 (自型読み / foreign 昇格読み) でも reader は 1 本で、階層に帰属するため。 */
PIG_WIRE_DEF(ggGeom, gg_mk_reader, gg_mk_writer);

/* ★ 2026-08-28 (ひさ設計・ABI v16): このモジュールが提供するもの。
 *   1 行 = (本体クラス階層, その階層について名乗る型名, 扱う 4CC)。
 *   ⚠ **types と tags は位置対応しない** (独立した 2 本・個数も一致しない)。どのタグがどの型に
 *     なるかは申告せず、wire->create に通して訊く (pigModule.h の pigModuleType 参照)。
 *   ⚠ tags は **診断専用** — 読めるかを答えるのは wire->create 一本で、この欄は
 *     `srava --module-info` が列挙するための候補にすぎない (実行時の判断に使わない)。 */
extern const pigModuleType geogram_provides[];
const pigModuleType geogram_provides[] = {
	{ &ggGeom::WIRE, GG_TYPE,
	  GG_TAG ",MESH" },
	/* ★ #3528: **自前のクラスを作らず libsrava_pt のクラスをそのまま並べる** (occt_mf が mfGeom を
	 *   借りるのと同じ作法)。estimate_normals が pt-cloud3d を読み書きするため、agent プロセスに
	 *   geogram.so しか load されない process 実行でも codec がここから届く必要がある。
	 *   ⚠ 2D は名乗らない — 法線推定は 3D だけ (points.so が pt-cloud2d を持つ)。 */
	{ &ptCloud::WIRE, PT_TYPE_3D, PT_TAG_3D },
	{ 0, 0, 0 },
};


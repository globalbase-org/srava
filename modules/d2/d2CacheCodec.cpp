/*
 * d2CacheCodec — 第2(2D)カーネル "d2" のキャッシュコーデック定義 TU (rev4 次元分担デモ)。
 * d3CacheCodec.cpp のミラー。descriptor.codecs 配列 (d2_codecs) を extern 公開。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"d2/c++/d2Shape.h"
#include	"d2/c++/ptsd2WireCacheStreamReaderShape.h"
#include	"d2/c++/ptsd2WireCacheStreamWriterShape.h"

static sPtr<tinyState>
d2_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsd2WireCacheStreamReaderShape,(parent, path)));
}

static sPtr<tinyState>
d2_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsd2WireCacheStreamWriterShape,(parent, path, sPtr<d2Shape>::d_cast(body))));
}

static int
d2_match(sPtr<pigData> body)
{
	return sPtr<d2Shape>::d_cast(body).is_notNull();
}

extern const pigModuleCodec d2_codecs[];
const pigModuleCodec d2_codecs[] = {
	{ "d2-shape", "D2S2", "d2-shape2d", &d2_match, &d2_mk_reader, &d2_mk_writer },
	{ 0, 0, 0, 0, 0, 0 },
};

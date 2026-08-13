/*
 * d3CacheCodec — 第3(mesh)カーネル "d3" のキャッシュコーデック定義 TU (rev4 Phase D-3)。
 * descriptor.codecs 配列 (d3_codecs) を extern 公開し、ローダが owner=d3 id で登録する。
 * mfCacheCodec.cpp のミラー。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"d3/c++/d3Mesh.h"
#include	"d3/c++/ptsd3WireCacheStreamReaderMesh.h"
#include	"d3/c++/ptsd3WireCacheStreamWriterMesh.h"

static sPtr<tinyState>
d3_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsd3WireCacheStreamReaderMesh,(parent, path)));
}

static sPtr<tinyState>
d3_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsd3WireCacheStreamWriterMesh,(parent, path, sPtr<d3Mesh>::d_cast(body))));
}

static int
d3_match(sPtr<pigData> body)
{
	return sPtr<d3Mesh>::d_cast(body).is_notNull();
}

/* ★ descriptor.codecs が指す配列 (name==0 番兵終端)。d3atsAgent.cpp が extern 参照。 */
extern const pigModuleCodec d3_codecs[];
const pigModuleCodec d3_codecs[] = {
	{ "d3-mesh", "D3M3", "d3-mesh3d", &d3_match, &d3_mk_reader, &d3_mk_writer },
	{ 0, 0, 0, 0, 0, 0 },
};

/*
 * pigRefCacheCodec — 参照レコード(D_REF)のキャッシュコーデック登録 TU
 *                    (#3406, 2026-07-31 メモ 2.)。
 * pigCacheCodec テーブルへ 4CC "REF " の reader と D_REF 本文(pigDataRef.h の pigDataPair)の
 * writer を静的登録する。cg/mf の mesh コーデック(cgCacheCodec.cpp / mfCacheCodec.cpp)と同じ流儀。
 * ただし D_REF はカーネル非依存なので登録 TU は **pig 層**に置く。
 *
 * 実行ファイルに直接コンパイルされる必要がある(静的ライブラリに畳むと未参照 TU として落ちる —
 * pigAgentRegistry / cgCacheCodec と同じ注意)。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigDataRef.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsObject 派生 TU の作法 (ptsApp 完全型) */
#include	"pig/c++/ptsWireCacheStreamReaderRef.h"
#include	"pig/c++/ptsWireCacheStreamWriterRef.h"

static sPtr<tinyState>
ref_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsWireCacheStreamReaderRef,(parent, path)));
}

static sPtr<tinyState>
ref_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsWireCacheStreamWriterRef,(parent, path, body)));
}

/* set_body された本文が D_REF 表現 (pigDataPair("D_REF", {...})) かどうか。 */
static int
ref_match(sPtr<pigData> body)
{
	return pig_data_ref_is(body);
}

/* D_REF は in-proc 参照でカーネル非依存 = 無型 (out_types "")。reader_for_tag は "REF " の自型
 * (type_of_tag=0) が無いのでフォールバック経路 (タグを読める任意 codec) でこの reader を選ぶ。
 * ★ #3427 ③: 旧「静的初期化でグローバル表へ登録」を廃止。pigModuleRegistry (ハブ) の ctor が
 *   この関数で **自分の codec 表へ** 組込登録する (per-registry = per-app・リエントラント)。 */
void
pigRefCacheCodec_register(pigCacheCodec &codecs)
{
	codecs.register_codec("pig-ref", "REF ", "", &ref_match, &ref_mk_reader, &ref_mk_writer);
}

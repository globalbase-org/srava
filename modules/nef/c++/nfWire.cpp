/*
 * nfWire — ★★ #3559: nef の **配線先 (WIRE) の定義 TU**。幾何ライブラリ (libsrava_cg) 側。
 *
 * ★ ここに在るのは「型 1 つぶんの WIRE」×2 と、reader/writer の生成子だけ。
 *   `descriptor.codecs` が指す表 (nef_provides) は **モジュール側** (nfCacheCodec.cpp) に在る —
 *   あちらは変種ごとに違う (型名 1 本・自分の 4CC) ので、モジュールと一緒にビルドされる。
 *
 * ---- なぜ WIRE が 2 本あるか (#3559) ----
 * #3559 より前は @nfGeom::WIRE@ の 1 本だった。1 本で足りていたのは *ライブラリが変種ごとに
 * 別だった* からで (libsrava_nf_snc / libsrava_nf_hybrid に 1 本ずつ = プロセス内では 2 本)、
 * 幾何ライブラリを 1 本に畳んだ今は **型ごとに 1 本**要る:
 *   ・@create@  … どちらの型として実体化するかが変種そのもの
 *   ・@match@   … writer を選ぶときに nfMesh と nfMeshSnc を 1:1 で割る必要がある
 *   ・@mkReader@… 読めない 4CC を断るとき、どのモジュール名で断るかが変種ごとに違う
 * ⚠ 2 変種は lib/module/all.sra + 明示 module() で **日常的に同時ロードされる**ので、
 *   ここが割れていないと「先に読まれた方」へ寄る (#3499 で実際に値が壊れた)。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"nf/c++/nfMesh.h"
#include	"nf/c++/ptsnfWireCacheStreamReaderMesh.h"
#include	"nf/c++/ptsnfWireCacheStreamWriterMesh.h"

/* ★ 変種の顔 (reader の ctor へ渡す)。詳細は nfMesh.h の nfWireVariant。 */
const nfWireVariant NF_VARIANT_SNC    = { "nef_snc",    &nfMeshSnc::create_for_meta };
const nfWireVariant NF_VARIANT_HYBRID = { "nef_hybrid", &nfMesh::create_for_meta    };

static sPtr<tinyState>
nf_mk_reader_snc(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsnfWireCacheStreamReaderMesh,(parent, path, &NF_VARIANT_SNC)));
}

static sPtr<tinyState>
nf_mk_reader_hybrid(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsnfWireCacheStreamReaderMesh,(parent, path, &NF_VARIANT_HYBRID)));
}

/* ★ writer は **変種を知らなくてよい** — 書く 4CC は本体に訊く (@meta_tag()@) ので、
 *   1 本で両方を書ける。⇒ ここだけは #3559 の前から変わっていない。 */
static sPtr<tinyState>
nf_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsnfWireCacheStreamWriterMesh,(parent, path, sPtr<nfGeom>::d_cast(body))));
}

PIG_WIRE_DEF(nfMesh,    nf_mk_reader_hybrid, nf_mk_writer);
PIG_WIRE_DEF(nfMeshSnc, nf_mk_reader_snc,    nf_mk_writer);

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
#include	"pig/c++/pigModule.h"   /* 組込記述子 (#3439 ③) */
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

/* D_REF は in-proc 参照でカーネル非依存 = 無型 (types "")。
 *
 * ★ #3439 ③: 派生テーブルへの登録をやめ、**組込モジュールの記述子**として持つ。
 *   検索は記述子走査 + is_enabled になったので、組込も同じ経路に乗せる必要がある。
 *   この記述子は名前 "pig"・priority 0・ops 無し・**常に有効** (off にできない = モジュール由来でない
 *   ことが構造で表れる)。値キャッシュ ("TEXT") はここにも居ない — あれは ptsDataCache の既定分岐
 *   (codec が無ければ WriterText / wire_tag_is_text なら ReaderText) で、モジュール由来ではない。 */

/* ★ 2026-08-28 (ABI v13/v16): 組込の wire クラス。D_REF は **無型** (pigDataWireTyped の派生ではない)
 *   ので **create を持たない** = op の引数として配線される対象にならないし、4CC から実体化もされない。
 *   writer / match は生きている — export が書く REF キャッシュの writer 選択がこれを引く。
 *   ★ create=0 にしたのは v16 の検証機構の指摘による。以前は「常に null を返す create」を置いていたが、
 *     tags に "REF " を申告していたため「申告したのに受理しない」= ずれとして検出された。
 *     受理しないのは実装の性質なので、**持たない**ことを構造で表すのが正しい。 */
static const pigWireClass pig_ref_wire = { "pigDataRef", 0 /*create*/, &ref_mk_reader, &ref_mk_writer, &ref_match };
/* ★ ABI v16: 階層 × 型名 × 4CC。D_REF は無型なので types は空。 */
static const pigModuleType pig_builtin_provides[] = {
	{ &pig_ref_wire, "", 0 /* tags: create を持たないので列挙しない */ },
	{ 0, 0, 0 },
};

static const srava_module_descriptor pig_builtin_descriptor = {
	SRAVA_MODULE_ABI, "pig", 0,
	0 /*make_agent*/, 0u /*exec_caps*/, 0 /*exec_default*/,
	0 /*ops*/, 0 /*n_ops*/,
	0 /*import_exts*/, 0 /*export_exts*/,
	pig_builtin_provides,  /* provides: 階層 × 型名 × 4CC (ABI v16) */
	/* ★ 2026-08-28 (ABI v11): 旧 types/type_tags ("value,ref" / "TEXT,REF ") は撤去。
	 *   非幾何型は libpig の pig_nongeometric_types が 1 本で持つ (全モジュール共通のため)。 */
	0 /*hash_salt*/,
	0,    /* initialize */
};

const srava_module_descriptor *
pig_builtin_module_descriptor(void)
{
	return &pig_builtin_descriptor;
}

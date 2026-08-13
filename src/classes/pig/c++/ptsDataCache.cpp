/*
 * ptsDataCache — pigDataCache 専用の保存/読み出し helper (#3406, 2026-07-29 メモ 3.)。
 *
 * pigDataCache の set_body / get_body から起動され、ディスク側の処理を状態遷移として内包する。
 * 完了で FIN→ZOM し、pigDataCache 上の bodyHelper を thNULL に戻す。待ち手 (get_body で listen
 * した caller / set_body を呼んだ starter) への合図は **TSE_DESTROY 統一** (ZOM 時に tinyState が
 * listener へ自動配布) + SAVE のメタ書込済のみ TSE_ASSERT 転送 (ひさ受諾 2026-07-29):
 *
 *   MODE_SAVE (set_body 起動): 本文から writer を選ぶ (pigCacheCodec::writer_for_body →
 *     無ければ既定 WriterText(serialize) = 値キャッシュ)。writer の TSE_ASSERT (streamhdr+
 *     D_META 書込済 = 下流 attach 可) で cache を CV_VALID にし、starter へ TSE_ASSERT を転送
 *     (agent はこれで A_SAVE_BEGIN を先行送信 = read-while-write 維持)。TSE_RETURN で FIN。
 *   MODE_LOAD (get_body 起動): ::access で存在検査 → 無ければ CV_INVALID で FIN (get_body は
 *     再評価で thNULL = invalid を返す)。有ればヘッダの **先頭 D_META 4CC** で reader を選ぶ
 *     (pigCacheCodec::reader_for_tag。未登録タグ = コーデック外の既定 = "TEXT" → ReaderText)。
 *     reader の TSE_RETURN で本文を cache へ。decode 失敗 (thNULL) は CV_INVALID。
 *     **D_TEXT の中身は素のテキストではなく pigData のコード** (= serialize 出力) なので、
 *     ReaderText の後に値パーサを通して pigData に戻す (LOADTEXT → LOADPARSE。2026-07-31 メモ 3.2)。
 *
 * starter は ctor の parent。LOAD の追加の待ち手は pigDataCache::get_body が listen で積む。
 * 本クラスは pigDataCache の friend (body/validState/bodyHelper を直接操作する唯一の場所)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigwire.h"          /* WIRE_*_SIZE / D_META / D_TEXT (ヘッダ検査) */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModuleRegistry.h"   /* ★ #3427 ③: app 所有レジストリ (codecs/vparser) */
#include	"pig/c++/pigValueParser.h"                 /* D_TEXT 本文 → pigData (2026-07-31 メモ 3.2) */
#include	"pig/c++/ptsWireCacheStreamWriterText.h"   /* 既定 writer (値キャッシュ) */
#include	"pig/c++/ptsWireCacheStreamReaderText.h"   /* 既定 reader (値キャッシュ) */
#include	"ts2/c++/stdEvent.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptsDataCache_.h"

#include	<stdio.h>    /* fopen/fread (ヘッダ 4CC 読み・get_module_tag と同じ作法) */
#include	<unistd.h>   /* access */

CLASS_TINYSTATE(pig/c++/ptsDataCache,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	/* parent = starter (set_body/get_body を呼んだ TS_STATE の tinyState)。
	 * cache = 対象 pigDataCache。mode = PDC_MODE_SAVE / PDC_MODE_LOAD / PDC_MODE_LOAD_CONV。
	 * target_type = LOAD_CONV のとき「どの型へ変換読みするか」(この helper が不変に保持・共有 convType 廃止)。
	 *   SAVE/LOAD では thNULL。 */
	ptsDataCache_(
		sPtr<tinyState> parent,
		sPtr<pigDataCache> cache,
		int mode,
		sPtr<stdString> target_type);

	sRptr<tinyState,tinyState>		parent;
protected:
	sPtr<tinyState>		worker;   /* reader / writer / 値パーサ (生成子は tinyState で返す) */
private:
	void	set_parsed_body(sPtr<pigData> v);   /* パース結果を cache へ反映 (失敗=CV_INVALID) */
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"pig/c++/pigData.h"   /* sPtr<pigDataCache> cache 値メンバの完全型 */
class tinyState;
class pigDataCache;
TS_END_INTERFACE

#endif


ptsDataCache_::ptsDataCache_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	if ( cache == thNULL )
		return rDO|FIN_START;

	/* ★ starter (set_body/get_body を呼んだ caller = ctor の parent) を**暗黙 listener** に登録する
	 * (ひさ受諾 2026-07-29)。TSE_DESTROY は ZOM 時に **listener にだけ**自動配布される (parent への
	 * 自動通知ではない) ので、これが無いと set_body した agent が保存完了を知れず永久待ちになる。
	 * SAVE の TSE_ASSERT 転送は parent 宛の明示送信 (ACT_SAVE)、完了は listener 宛の TSE_DESTROY。 */
	listen(sPtr<tinyState>(parent), TSE_DESTROY);
	/* ★ TSE_ASSERT も listener 配信にする (2026-08-05)。starter 以外 (pigDataCache::is_valid で
	 * 購読した第三者) にも「メタ書込済」を届けるため。starter はここで両方に登録しておく。 */
	listen(sPtr<tinyState>(parent), TSE_ASSERT);

	if ( mode == PDC_MODE_SAVE ) {
		/* ★ 2026-08-12 再設計: writer entry (converted[conv_index(target_type)]) の body を書く。
		 * mesh 系はコーデック writer、無ければ既定 WriterText(serialize) = 値キャッシュ。 */
		sPtr<pigData> b;
		int wi = ( target_type != thNULL ) ? cache->conv_index(target_type->get_str()) : -1;
		if ( wi >= 0 ) b = cache->converted[wi].body;
		/* ★ #3427 ③: codec 表は app 所有レジストリから (pts 系なので ptsApp 経由)。 */
		pigCacheWriterFn w = ( ptsApp != thNULL && ptsApp->module_registry != thNULL )
		    ? ptsApp->module_registry->codecs.writer_for_body(b) : 0;
		if ( w != 0 )
			worker = w(ifThis, cache->path, b);
		else
			worker = sPtr<tinyState>::d_cast(thNEW(ptsWireCacheStreamWriterText,
			    (ifThis, cache->path,
			     ( b != thNULL ) ? b->serialize()
			                     : sPtr<stdString>(thNEW(stdString,(""))))));
		return ACT_ptsDataCache_SAVE;   /* writer の TSE_ASSERT / TSE_RETURN 待ち → rDO なし */
	}

	/* ★ MODE_LOAD (canonical/conv の区別なし): 存在検査 → file 4CC で **target_type の reader** を選ぶ。
	 * ファイル不在 = キャッシュ無効 (CV_INVALID)。get_body 側が is_valid で先に弾くので通常来ない。 */
	if ( ::access(cache->path->get_str(), F_OK) != 0 ) {
		cache->validState = pigDataCache::CV_INVALID;
		return rDO|FIN_START;                     /* entry は body 無しのまま FIN → done */
	}
	cache->validState = pigDataCache::CV_VALID;   /* 存在 = メタ済 (decode 失敗は entry 側で表現) */
	{
		/* 先頭レコードの種別と 4CC を読む (同期 read・ヘッダ数十 byte)。 */
		unsigned char h[WIRE_STREAMHDR_SIZE + WIRE_RECHDR_SIZE + 4];
		size_t n = 0;
		FILE *fp = ::fopen(cache->path->get_str(), "rb");
		if ( fp != 0 ) { n = ::fread(h, 1, sizeof h, fp); ::fclose(fp); }
		uint16_t rtype = ( n >= WIRE_STREAMHDR_SIZE + WIRE_RECHDR_SIZE )
		    ? (uint16_t)(h[WIRE_STREAMHDR_SIZE + 4] | (h[WIRE_STREAMHDR_SIZE + 5] << 8)) : 0;
		const char *tt = ( target_type != thNULL ) ? target_type->get_str() : "value";
		if ( rtype == D_META && n >= sizeof h ) {
			const unsigned char *tag = h + WIRE_STREAMHDR_SIZE + WIRE_RECHDR_SIZE;
			/* ★ 値キャッシュも D_META で始まる (4CC "TEXT") — 値か型付きかは wire_tag_is_text で
			 * 判別 (型レジストリは per-binary に偏るので判別に使わない)。"TEXT" は下の value 経路へ
			 * (「D_META = mesh」の誤分類が 2026-08-12 の値 warm 読み全滅の原因)。
			 * 型付き: file 4CC + 要求型で reader_for を引く (native も conversion もこれで一意)。 */
			if ( ! wire_tag_is_text(tag) ) {
				pigCacheReaderFn r = ( ptsApp != thNULL && ptsApp->module_registry != thNULL )
				    ? ptsApp->module_registry->codecs.reader_for(tag, tt) : 0;
				if ( r != 0 ) {
					worker = r(ifThis, cache->path);
					return ACT_ptsDataCache_LOAD;   /* reader の TSE_RETURN 待ち → rDO なし */
				}
				return rDO|FIN_START;   /* その形式→その型の reader 無し = 変換不可 (entry body 無しで閉じる) */
			}
		}
		/* 値キャッシュ (D_META "TEXT" + D_TEXT 本文): 要求型が "value" のときだけ読む。 */
		if ( ::strcmp(tt, "value") != 0 )
			return rDO|FIN_START;
		/* D_TEXT の本文は pigData のコード (serialize 出力) なので LOADTEXT でパーサに通す。 */
		worker = sPtr<tinyState>::d_cast(thNEW(ptsWireCacheStreamReaderText,(ifThis, cache->path)));
		return ACT_ptsDataCache_LOADTEXT;
	}
}

TS_STATE(ACT_ptsDataCache_SAVE)
{
	if ( ev->source == worker ) {
		if ( ev->type == TSE_ASSERT ) {
			/* メタ書込済 = valid 成立 (2026-07-29 メモ is_valid)。starter へ転送 —
			 * agent はこれで A_SAVE_BEGIN を先行送信できる (read-while-write 維持)。 */
			cache->validState = pigDataCache::CV_VALID;
			/* ★ starter だけでなく is_valid で購読した待ち手へも配る (2026-08-05)。 */
			invoke_listen(thNEW(stdEvent,(TSE_ASSERT, ifThis, (INTEGER64)0)));
			return 0;
		}
		if ( ev->type == TSE_RETURN )
			return rDO|FIN_START;   /* 全書込完了 → ZOM の TSE_DESTROY が「保存完了」の合図 */
	}
	return 0;
}

TS_STATE(ACT_ptsDataCache_LOAD)   /* mesh 系: reader の TSE_RETURN (msg_obj=本文 pigData) */
{
	if ( ev->type == TSE_RETURN && ev->source == worker ) {
		sPtr<pigData> b = sPtr<pigData>::d_cast(ev->msg_obj);
		/* ★ 全 LOAD: 読んだ body を **自 target_type のエントリ**へ。b==thNULL = decode 失敗 →
		 * entry は body 無しのまま (FIN で done)。file は valid のまま (メタは書けている)。 */
		if ( b != thNULL && target_type != thNULL )
			cache->conv_set_body(target_type->get_str(), b);
		return rDO|FIN_START;
	}
	return 0;
}

/* 本文テキスト → pigData。パース結果を **自 target_type ("value") のエントリ**へ反映。
 * 空/エラーは entry body 無しのまま (get_body は thNULL を返し、上流がエラーにする)。 */
void
ptsDataCache_::set_parsed_body(sPtr<pigData> v)
{
	if ( v != thNULL && ! v->is_error() && target_type != thNULL )
		cache->conv_set_body(target_type->get_str(), v);
}

TS_STATE(ACT_ptsDataCache_LOADTEXT)   /* 値: ReaderText の TSE_RETURN (msg_obj=stdString) */
{
	if ( ev->type == TSE_RETURN && ev->source == worker ) {
		sPtr<stdString> t = sPtr<stdString>::d_cast(ev->msg_obj);
		worker = thNULL;
		if ( t == thNULL ) {
			cache->validState = pigDataCache::CV_INVALID;
			return rDO|FIN_START;
		}
		/* ★ D_TEXT の中身は素のテキストではなく pigData のコード (= body->serialize() の出力)。
		 * pigDataString で包んで返すと下流が「文字列」として扱ってしまう (2026-07-31 メモ 3.2)。
		 * 言語パーサが登録されていれば (planner 側) それを起動、無ければ (agent プロセス側)
		 * pig 層の同期パーサ。詳細は pigValueParser.h。 */
		pigValueParserFn mk = ( ptsApp != thNULL && ptsApp->module_registry != thNULL )
		    ? ptsApp->module_registry->vparser.get() : 0;
		if ( mk != 0 ) {
			worker = mk(ifThis, t);
			if ( worker != thNULL )
				return ACT_ptsDataCache_LOADPARSE;   /* 子パーサの TSE_RETURN 待ち → rDO なし */
		}
		set_parsed_body(pigValueParser::parse_sync(t));
		return rDO|FIN_START;
	}
	return 0;
}

TS_STATE(ACT_ptsDataCache_LOADPARSE)   /* 値: 言語パーサの TSE_RETURN (msg_obj=pigData) */
{
	if ( ev->type == TSE_RETURN && ev->source == worker ) {
		set_parsed_body(sPtr<pigData>::d_cast(ev->msg_obj));
		worker = thNULL;
		return rDO|FIN_START;
	}
	return 0;
}

TS_STATE(FIN_START)
{
	/* helper はお役御免 (2026-07-29 メモ: 終了で bodyHelper=thNULL)。走行 helper は同時に 1 本
	 * (set_body/get_body が bodyHelper 掲示中は新規起動しない) なので無条件クリアでよい。
	 * この後の ZOM 遷移で listener (get_body の待ち手 + starter=parent) へ TSE_DESTROY が
	 * 自動配布され、全員が再評価する。
	 * ★ bodyDone は **bodyHelper をクリアする前** に立てる (ひさ指示 2026-08-05)。set_body/
	 * get_body は「helper を掲示 → bodyDone を見る」の順なので、この順序なら
	 *   (a) 先にここが走った → 掲示側が bodyDone を見て掲示を取り消す
	 *   (b) 先に掲示が済んだ → ここが bodyHelper を thNULL にする
	 * のどちらかになり、ZOM 済み helper が掲示されたまま残ることがない。 */
	/* ★ 2026-08-12 再設計: 全モード共通で **自 type のエントリ**に done を立てて helper を外す
	 * (SAVE=writer type / LOAD=読んだ type)。ZOM の TSE_DESTROY で待ち手が再評価する。 */
	if ( cache != thNULL && target_type != thNULL )
		cache->conv_finish(target_type->get_str());
	worker = thNULL;
	cache = thNULL;
	return rDO|FIN_ptsObject_START;
}

/* ---- pigDataCache へのフック (#3406, pigData.cpp は pig 静的層側なので codegen クラスを
 * フック経由で受け取る)。★ #3427 ③: 旧「静的初期化でグローバルフックへ登録」を廃止し、
 * ptsApplication の INI が ptsDataCache_helper() で取得して **app 所有レジストリ**
 * (module_registry->set_pdc_helper) に登録する。pigDataCache 側は pig_current_registry()
 * (TLS) 経由で引く = プロセス可変 static ゼロ。 ---- */
static sPtr<tinyState>
mk_ptsDataCache(sPtr<tinyState> starter, sPtr<pigDataCache> c, int mode, sPtr<stdString> target_type)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsDataCache,(starter, c, mode, target_type)));
}
pigDataCacheHelperFn
ptsDataCache_helper()
{
	return &mk_ptsDataCache;
}

/*
 * ptsWireCacheStreamWriterRef — 参照レコード(D_REF)キャッシュ用の writer 派生(export 等)。
 * mesh バイナリは持たず、INIT gate で D_META タグ "REF " + D_REF レコード(kind + path + size +
 * mtime + content_hash)を書くだけ。基底が streamhdr / TSE_ASSERT / W_END 番兵 / TSE_RETURN を担う。
 *
 * 本文は他の writer と同じく **pigData** で受け取る (#3406, 2026-07-31 メモ 2.):
 *   pigDataPair("D_REF", pigDataHash{ ref_kind, path, size, mtime, content_hash })  ← pigDataRef.h
 * これにより calc は「結果 pigData を返す」だけ、agent は set_body するだけになり、writer 起動は
 * ptsDataCache + pigCacheCodec の一本道に載る(旧: calc が自前で WriterRef を起こす escape hatch)。
 *
 * D_META を書くのは **全キャッシュファイル共通の不変条件**(catalog §1)。これが無いと
 * 読み側の基底 ptsWireCacheStreamReader が先頭レコードで弾き、reader 選択もできない。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigDataRef.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptsWireCacheStreamWriterRef_.h"

CLASS_TINYSTATE(pig/c++/ptsWireCacheStreamWriterRef,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsWireCacheStreamWriterRef_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<pigData> _body);           /* pigDataRef.h の D_REF 表現 */

	sRptr<ptsObject,tinyState>		parent;
protected:
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"pig/c++/pigData.h"   /* sPtr<pigData> _body 値メンバの完全型 */
class ptsObject;
class stdString;
class pigData;
TS_END_INTERFACE

#endif


ptsWireCacheStreamWriterRef_::ptsWireCacheStreamWriterRef_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META"REF " + D_REF を 1 レコード書く(本体なし) */
{
	static const uint8_t TAG[4] = { 'R','E','F',' ' };
	write_d_meta(TAG, 4);

	int kind = PIG_DREF_OUTPUT;
	sPtr<stdString> path;
	INTEGER64 size = 0, mtime = 0;
	pHashKeyType chash = (pHashKeyType)0;
	if ( pig_data_ref_get(_body, &kind, &path, &size, &mtime, &chash) ) {
		if ( kind == PIG_DREF_INPUT )
			d_ref_input(path, size, mtime, chash);
		else
			d_ref_output(path, size, mtime, chash);
	}
	/* 壊れた本文(D_REF 表現でない)は D_META だけ書いて番兵で閉じる。読み側は D_REF が
	 * 無いことを検知して CV_INVALID にする(黙って空レコードを書くより素直)。 */
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}

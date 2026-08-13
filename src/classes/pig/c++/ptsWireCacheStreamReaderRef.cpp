/*
 * ptsWireCacheStreamReaderRef — 参照レコード(D_REF)キャッシュ入力用の reader 派生。
 *   - METADATA gate で先頭 D_META の形式タグが "REF " かを検証。違えば errCode。
 *   - ACT_START(TS_THREAD)で next_record() を回し、最初の D_REF を pigData 表現へ復元。
 * 基底が open/streamhdr 検証/番兵検出/ポーリング/TSE_RETURN を担う。
 *
 * 復元形は pigDataRef.h の pigDataPair("D_REF", pigDataHash{...})。WriterRef の逆変換であり、
 * これで D_REF も「pigDataCache::body に載る本文」として他形式と対称になる(2026-07-31 メモ 2.)。
 *
 * NB: 現状この reader の消費者はゼロ。HIT 時 planner はキャッシュハンドルを返すだけで中身を
 *     読まないため(pigfAgent の out_cache=1 枝)。content_hash による「出力ファイルが手で
 *     改変された」検知を入れる時にここが効く(catalog §7.2 の穴)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigwire.h"
#include	"pig/c++/pigDataRef.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptsWireCacheStreamReaderRef_.h"

#include	<string.h>

CLASS_TINYSTATE(pig/c++/ptsWireCacheStreamReaderRef,pig/c++/ptsWireCacheStreamReader)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsWireCacheStreamReaderRef_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName);

	sRptr<ptsObject,tinyState>		parent;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class stdString;
TS_END_INTERFACE

#endif


ptsWireCacheStreamReaderRef_::ptsWireCacheStreamReaderRef_(TS_ARGS0)
        : ptsWireCacheStreamReader_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/* payload から LE の INTEGER64 を 1 個読む(WriterRef の d_ref_output と対)。 */
static INTEGER64 get_i64le(const sArray<uint8_t>& p, int off)
{
	INTEGER64 v = 0;
	for ( int s = 0 ; s < 8 ; ++s )
		v |= ((INTEGER64)(uint8_t)p[off + s]) << (s * 8);
	return v;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* 形式タグ "REF " を検証 */
{
	if ( meta.length() != 4
	  || meta[0] != 'R' || meta[1] != 'E' || meta[2] != 'F' || meta[3] != ' ' )
		errCode = -2;      /* 自分の対応形式ではない */
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* 最初の D_REF を pigData へ復元 */
{
	for ( ; ; ) {
		int r = next_record();
		if ( r < 0 ) return rDO|FIN_START;        /* エラー(errCode セット済) */
		if ( r == 0 ) break;                      /* W_END 番兵 → D_REF 無し(下で errCode) */
		if ( rec_type != D_REF ) continue;        /* 参照レコード以外は読み飛ばす */

		/* payload: kind(u8) + path_len(u16 LE) + path + size(i64) + mtime(i64) + chash(i64) */
		int n = rec_payload.length();
		if ( n < 3 ) { errCode = -3; return rDO|FIN_START; }
		int      kind = (int)rec_payload[0];
		uint32_t pl   = (uint32_t)rec_payload[1] | ((uint32_t)rec_payload[2] << 8);
		if ( (INTEGER64)3 + (INTEGER64)pl + 24 > (INTEGER64)n ) {
			errCode = -3; return rDO|FIN_START;   /* 途中で切れている = 壊れたレコード */
		}
		sArray<uint8_t> tmp; tmp.length((int)pl + 1);
		for ( uint32_t i = 0 ; i < pl ; ++i ) tmp[(int)i] = rec_payload[3 + (int)i];
		tmp[(int)pl] = 0;                         /* null 終端して stdString へ */
		sPtr<stdString> path = thNEW(stdString,((const char *)&tmp[0]));

		int off = 3 + (int)pl;
		INTEGER64 size  = get_i64le(rec_payload, off);
		INTEGER64 mtime = get_i64le(rec_payload, off + 8);
		pHashKeyType ch = (pHashKeyType)get_i64le(rec_payload, off + 16);

		result = sPtr<stdObject>::d_cast(pig_data_ref_make(kind, path, size, mtime, ch));
		return rDO|FIN_START;                     /* 1 レコードで確定 */
	}
	errCode = -3;                                     /* D_META"REF " なのに D_REF が無い */
	return rDO|FIN_START;
}

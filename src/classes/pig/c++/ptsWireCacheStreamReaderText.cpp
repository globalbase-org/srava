/*
 * ptsWireCacheStreamReaderText — データキャッシュ(srava 文法テキスト)入力用の reader 派生。
 * 上位フレームワーク依存部の最小実装(往復テスト用):
 *   - METADATA gate で先頭 D_META の形式タグが "TEXT" かを検証。違えば errCode。
 *   - ACT_START(TS_THREAD)で next_record() を回し、D_TEXT を連結 → result(stdString)。
 * 基底が open/streamhdr 検証/番兵検出/ポーリング/TSE_RETURN を担う。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/pigwire.h"
#include	"_ts2/c++/ptsWireCacheStreamReaderText_.h"

#include	<string.h>

CLASS_TINYSTATE(pig/c++/ptsWireCacheStreamReaderText,pig/c++/ptsWireCacheStreamReader)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsWireCacheStreamReaderText_(
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


ptsWireCacheStreamReaderText_::ptsWireCacheStreamReaderText_(TS_ARGS0)
        : ptsWireCacheStreamReader_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* 形式タグ "TEXT" を検証 */
{
	if ( meta.length() != 4
	  || meta[0] != 'T' || meta[1] != 'E' || meta[2] != 'X' || meta[3] != 'T' )
		errCode = -2;      /* 自分の対応形式ではない */
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* D_TEXT を連結して result へ */
{
	sPtr<stdString> acc = thNEW(stdString,(""));
	for ( ; ; ) {
		int r = next_record();
		if ( r < 0 ) return rDO|FIN_START;        /* エラー(errCode セット済) */
		if ( r == 0 ) break;                      /* W_END 番兵 → 正常終了 */
		if ( rec_type != D_TEXT ) continue;       /* テキスト以外は読み飛ばす */
		int n = rec_payload.length();
		sArray<uint8_t> tmp; tmp.length(n + 1);
		for ( int i = 0; i < n; ++i ) tmp[i] = rec_payload[i];
		tmp[n] = 0;                               /* null 終端して stdString へ */
		acc = acc->add((const char *)&tmp[0]);
	}
	result = sPtr<stdObject>::d_cast(acc);
	return rDO|FIN_START;
}

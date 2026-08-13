/*
 * ptsWireCacheStreamWriterText — データキャッシュ(srava 文法テキスト)出力用の writer 派生。
 * 上位フレームワーク依存部の最小実装(往復テスト用):
 *   - INIT gate で D_META に形式タグ "TEXT" を書く。
 *   - ACT_START(TS_THREAD)で payload(stdString)を D_TEXT レコードとして書き出す。
 * 基底が streamhdr / TSE_ASSERT / W_END 番兵 / TSE_RETURN を担う。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"_ts2/c++/ptsWireCacheStreamWriterText_.h"

CLASS_TINYSTATE(pig/c++/ptsWireCacheStreamWriterText,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsWireCacheStreamWriterText_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<stdString> _payload);

	sRptr<ptsObject,tinyState>		parent;
protected:
	sPtr<stdString>	payload;
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


ptsWireCacheStreamWriterText_::ptsWireCacheStreamWriterText_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    payload = _payload;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグを書く */
{
	static const uint8_t TAG[4] = { 'T','E','X','T' };
	write_d_meta(TAG, 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* 本体書き込み(blocking, 専用スレッド) */
{
	write_d_text(payload);
	return rDO|FIN_START;
}

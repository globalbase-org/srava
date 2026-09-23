/*
 * ptsptWireCacheStreamWriterCloud — 点群キャッシュ出力用 writer 派生 (#3528)。
 *   ptsd2WireCacheStreamWriterShape のミラー。INIT で D_META タグ ("PTC2"/"PTC3") を書き、
 *   ACT_START で ptCloud::encode が chunk()=d_chunk を直接呼ぶ。
 *
 * ★ この writer も **モジュール共通** (libsrava_pt)。点群を産むモジュールは自前で書かない。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pt/c++/ptCloud.h"
#include	"_ts2/c++/ptsptWireCacheStreamWriterCloud_.h"

CLASS_TINYSTATE(pt/c++/ptsptWireCacheStreamWriterCloud,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsptWireCacheStreamWriterCloud_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<ptCloud> _cloud);

	sRptr<ptsObject,tinyState>		parent;

	void	chunk(const uint8_t *data, int n);
protected:
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	<stdint.h>
class ptsObject;
class stdString;
class ptCloud;
TS_END_INTERFACE

#endif


ptsptWireCacheStreamWriterCloud_::ptsptWireCacheStreamWriterCloud_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptsptWireCacheStreamWriterCloud_::chunk(const uint8_t *data, int n)
{
	d_chunk(data, n);
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグ (次元) を書く */
{
	if ( _cloud.is_notNull() )
		write_d_meta((const uint8_t*)_cloud->meta_tag(), 4);
	else
		write_d_meta((const uint8_t*)PT_TAG_3D, 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* 点群を D_CHUNK へストリーム書き込み */
{
	if ( _cloud.is_notNull() ) {
		struct Sink : ptChunkSink {
			ptsptWireCacheStreamWriterCloud_ *w;
			void chunk(const uint8_t *data, int n) { w->chunk(data, n); }
		} sink;
		sink.w = this;
		_cloud->encode(sink);
	}
	return rDO|FIN_START;
}

/* ★ §9 (2026-09-04): 書き終えたら本体を手放す。ZOM に入ってもこの状態機械は tsThread の
 *   ワーカーに握られたまま残ることがあるので、ここで落とさないと本体が常駐する。 */
TS_STATE(FIN_START)
{
	_cloud = thNULL;
	return rDO|FIN_ptsWireCacheStreamWriter_START;
}

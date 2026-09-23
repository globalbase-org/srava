/*
 * ptsguWireCacheStreamWriterGeom — 中立幾何キャッシュ出力用 writer 派生 (#3527)。
 *   ptsptWireCacheStreamWriterCloud のミラー。INIT で D_META タグ ("MFM3"/"MFC2") を書き、
 *   ACT_START で guGeom::encode が chunk()=d_chunk を直接呼ぶ。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"gu/c++/guGeom.h"
#include	"_ts2/c++/ptsguWireCacheStreamWriterGeom_.h"

CLASS_TINYSTATE(gu/c++/ptsguWireCacheStreamWriterGeom,pig/c++/ptsWireCacheStreamWriter)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsguWireCacheStreamWriterGeom_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<guGeom> _geom);

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
class guGeom;
TS_END_INTERFACE

#endif

ptsguWireCacheStreamWriterGeom_::ptsguWireCacheStreamWriterGeom_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptsguWireCacheStreamWriterGeom_::chunk(const uint8_t *data, int n)
{
	d_chunk(data, n);
}

/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグを書く */
{
	if ( _geom.is_notNull() )
		write_d_meta((const uint8_t*)_geom->meta_tag(), 4);
	else
		write_d_meta((const uint8_t*)GU_TAG_3D, 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* D_CHUNK へストリーム書き込み */
{
	if ( _geom.is_notNull() ) {
		struct Sink : guChunkSink {
			ptsguWireCacheStreamWriterGeom_ *w;
			void chunk(const uint8_t *data, int n) { w->chunk(data, n); }
		} sink;
		sink.w = this;
		_geom->encode(sink);
	}
	return rDO|FIN_START;
}

/* ★ §9: 書き終えたら本体を手放す (ZOM 後もワーカーが握って常駐するのを防ぐ)。 */
TS_STATE(FIN_START)
{
	_geom = thNULL;
	return rDO|FIN_ptsWireCacheStreamWriter_START;
}

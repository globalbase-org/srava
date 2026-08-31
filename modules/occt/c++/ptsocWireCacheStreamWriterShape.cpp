/*
 * ptsocWireCacheStreamWriterShape — oc (OCCT) キャッシュ出力用 writer 派生 (#3437 P5)。
 * vd 版のミラー。INIT で D_META に形式タグを書き (本文が名乗る meta_tag = "VDB " か "MFM3")、
 * ACT_START で ocGeom::encode が chunk()=d_chunk を直接呼ぶ。
 * 基底 (ptsWireCacheStreamWriter) が streamhdr/TSE_ASSERT/W_END/TSE_RETURN を担う。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"oc/c++/ocShape.h"
#include	"_ts2/c++/ptsocWireCacheStreamWriterShape_.h"

CLASS_TINYSTATE(oc/c++/ptsocWireCacheStreamWriterShape,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsocWireCacheStreamWriterShape_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<ocGeom> _geom);

	sRptr<ptsObject,tinyState>		parent;

	/* ocGeom(3D mesh / 2D cross)の Sink 窓口: 基底 protected の d_chunk を公開して直接ストリームさせる。 */
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
class ocGeom;
TS_END_INTERFACE

#endif


ptsocWireCacheStreamWriterShape_::ptsocWireCacheStreamWriterShape_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptsocWireCacheStreamWriterShape_::chunk(const uint8_t *data, int n)
{
	d_chunk(data, n);
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグを書く (本文が名乗る meta_tag) */
{
	if ( _geom.is_notNull() )
		write_d_meta((const uint8_t*)_geom->meta_tag(), 4);
	else
		write_d_meta((const uint8_t*)OC_TAG, 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* 本文を D_CHUNK へストリーム書き込み */
{
	if ( _geom.is_notNull() ) {
		struct Sink : ocChunkSink {
			ptsocWireCacheStreamWriterShape_ *w;
			void chunk(const uint8_t *data, int n) { w->chunk(data, n); }
		} sink;
		sink.w = this;
		_geom->encode(sink);
	}
	return rDO|FIN_START;
}

/*
 * ptsd2WireCacheStreamWriterShape — d2 (2D) キャッシュ出力用 writer 派生 (rev4 次元分担デモ)。
 * ptsd3WireCacheStreamWriterMesh のミラー。INIT で D_META タグ "D2S2" を書き、ACT_START で
 * d2Shape::encode が chunk()=d_chunk を直接呼ぶ。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"d2/c++/d2Shape.h"
#include	"_ts2/c++/ptsd2WireCacheStreamWriterShape_.h"

CLASS_TINYSTATE(d2/c++/ptsd2WireCacheStreamWriterShape,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsd2WireCacheStreamWriterShape_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<d2Shape> _shape);

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
class d2Shape;
TS_END_INTERFACE

#endif


ptsd2WireCacheStreamWriterShape_::ptsd2WireCacheStreamWriterShape_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptsd2WireCacheStreamWriterShape_::chunk(const uint8_t *data, int n)
{
	d_chunk(data, n);
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグ "D2S2" を書く */
{
	if ( _shape.is_notNull() )
		write_d_meta((const uint8_t*)_shape->meta_tag(), 4);
	else
		write_d_meta((const uint8_t*)"D2S2", 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* shape を D_CHUNK へストリーム書き込み */
{
	if ( _shape.is_notNull() ) {
		struct Sink : d2ChunkSink {
			ptsd2WireCacheStreamWriterShape_ *w;
			void chunk(const uint8_t *data, int n) { w->chunk(data, n); }
		} sink;
		sink.w = this;
		_shape->encode(sink);
	}
	return rDO|FIN_START;
}

/*
 * ptsnfWireCacheStreamWriterMesh — nf(Nef)mesh キャッシュ出力用 writer 派生 (#3433 P1)。
 * cg/mf 版のミラー。INIT で D_META にタグ "NEF3" を書き、ACT_START で nfMesh::encode が
 * chunk()=d_chunk を直接呼ぶ (境界表現を厳密有理数で書く。nfMesh.h の冒頭コメント参照)。
 * 基底 (ptsWireCacheStreamWriter) が streamhdr/TSE_ASSERT/W_END/TSE_RETURN を担う。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"nf/c++/nfMesh.h"
#include	"_ts2/c++/ptsnfWireCacheStreamWriterMesh_.h"

CLASS_TINYSTATE(nf/c++/ptsnfWireCacheStreamWriterMesh,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsnfWireCacheStreamWriterMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<nfGeom> _mesh);

	sRptr<ptsObject,tinyState>		parent;

	/* nfGeom の Sink 窓口: 基底 protected の d_chunk を公開して直接ストリームさせる。 */
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
class nfGeom;
TS_END_INTERFACE

#endif


ptsnfWireCacheStreamWriterMesh_::ptsnfWireCacheStreamWriterMesh_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptsnfWireCacheStreamWriterMesh_::chunk(const uint8_t *data, int n)
{
	d_chunk(data, n);
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグ "NEF3" を書く */
{
	if ( _mesh.is_notNull() )
		write_d_meta((const uint8_t*)_mesh->meta_tag(), 4);
	else
		write_d_meta((const uint8_t*)"NEF3", 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* mesh を D_CHUNK へストリーム書き込み */
{
	if ( _mesh.is_notNull() ) {
		struct Sink : nfChunkSink {
			ptsnfWireCacheStreamWriterMesh_ *w;
			void chunk(const uint8_t *data, int n) { w->chunk(data, n); }
		} sink;
		sink.w = this;
		_mesh->encode(sink);
	}
	return rDO|FIN_START;
}

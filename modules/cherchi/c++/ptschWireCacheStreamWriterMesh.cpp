/*
 * ptschWireCacheStreamWriterMesh — cherchi mesh キャッシュ出力用 writer 派生 (#3438 P6)。
 * mf 版 (ptsmfWireCacheStreamWriterMesh) のミラー。INIT で D_META にタグ (CH_TAG="MFM3") を書き、
 * ACT_START で chMesh::encode が chunk()=d_chunk を直接呼ぶ。
 * 基底 (ptsWireCacheStreamWriter) が streamhdr/TSE_ASSERT/W_END/TSE_RETURN を担う。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"ch/c++/chMesh.h"
#include	"_ts2/c++/ptschWireCacheStreamWriterMesh_.h"

CLASS_TINYSTATE(ch/c++/ptschWireCacheStreamWriterMesh,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptschWireCacheStreamWriterMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<chGeom> _mesh);

	sRptr<ptsObject,tinyState>		parent;

	/* chGeom(3D mesh / 2D cross)の Sink 窓口: 基底 protected の d_chunk を公開して直接ストリームさせる。 */
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
class chGeom;
TS_END_INTERFACE

#endif


ptschWireCacheStreamWriterMesh_::ptschWireCacheStreamWriterMesh_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptschWireCacheStreamWriterMesh_::chunk(const uint8_t *data, int n)
{
	d_chunk(data, n);
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグ (CH_TAG) を書く */
{
	if ( _mesh.is_notNull() )
		write_d_meta((const uint8_t*)_mesh->meta_tag(), 4);
	else
		write_d_meta((const uint8_t*)CH_TAG, 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* mesh を D_CHUNK へストリーム書き込み */
{
	if ( _mesh.is_notNull() ) {
		struct Sink : chChunkSink {
			ptschWireCacheStreamWriterMesh_ *w;
			void chunk(const uint8_t *data, int n) { w->chunk(data, n); }
		} sink;
		sink.w = this;
		_mesh->encode(sink);
	}
	return rDO|FIN_START;
}

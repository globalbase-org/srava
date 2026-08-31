/*
 * ptsggWireCacheStreamWriterMesh — geogram mesh キャッシュ出力用 writer 派生 (#3435 P3)。
 * mf 版 (ptsmfWireCacheStreamWriterMesh) のミラー。INIT で D_META にタグ (GG_TAG="MFM3") を書き、
 * ACT_START で ggMesh::encode が chunk()=d_chunk を直接呼ぶ。
 * 基底 (ptsWireCacheStreamWriter) が streamhdr/TSE_ASSERT/W_END/TSE_RETURN を担う。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"gg/c++/ggMesh.h"
#include	"_ts2/c++/ptsggWireCacheStreamWriterMesh_.h"

CLASS_TINYSTATE(gg/c++/ptsggWireCacheStreamWriterMesh,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsggWireCacheStreamWriterMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<ggGeom> _mesh);

	sRptr<ptsObject,tinyState>		parent;

	/* ggGeom(3D mesh / 2D cross)の Sink 窓口: 基底 protected の d_chunk を公開して直接ストリームさせる。 */
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
class ggGeom;
TS_END_INTERFACE

#endif


ptsggWireCacheStreamWriterMesh_::ptsggWireCacheStreamWriterMesh_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptsggWireCacheStreamWriterMesh_::chunk(const uint8_t *data, int n)
{
	d_chunk(data, n);
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグ (GG_TAG) を書く */
{
	if ( _mesh.is_notNull() )
		write_d_meta((const uint8_t*)_mesh->meta_tag(), 4);
	else
		write_d_meta((const uint8_t*)GG_TAG, 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* mesh を D_CHUNK へストリーム書き込み */
{
	if ( _mesh.is_notNull() ) {
		struct Sink : ggChunkSink {
			ptsggWireCacheStreamWriterMesh_ *w;
			void chunk(const uint8_t *data, int n) { w->chunk(data, n); }
		} sink;
		sink.w = this;
		_mesh->encode(sink);
	}
	return rDO|FIN_START;
}

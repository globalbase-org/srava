/*
 * ptsd4WireCacheStreamWriterMesh — d4 mesh キャッシュ出力用 writer 派生 (rev4 Phase D-3)。
 * ptsmfWireCacheStreamWriterMesh のミラー。INIT で D_META タグ "D4M3" を書き、ACT_START で
 * d4Mesh::encode が chunk()=d_chunk を直接呼ぶ。基底が streamhdr/TSE_ASSERT/W_END/TSE_RETURN を担う。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"d4/c++/d4Mesh.h"
#include	"_ts2/c++/ptsd4WireCacheStreamWriterMesh_.h"

CLASS_TINYSTATE(d4/c++/ptsd4WireCacheStreamWriterMesh,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsd4WireCacheStreamWriterMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<d4Mesh> _mesh);

	sRptr<ptsObject,tinyState>		parent;

	/* d4Mesh の Sink 窓口: 基底 protected の d_chunk を公開して直接ストリームさせる。 */
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
class d4Mesh;
TS_END_INTERFACE

#endif


ptsd4WireCacheStreamWriterMesh_::ptsd4WireCacheStreamWriterMesh_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptsd4WireCacheStreamWriterMesh_::chunk(const uint8_t *data, int n)
{
	d_chunk(data, n);
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグ "D4M3" を書く */
{
	if ( _mesh.is_notNull() )
		write_d_meta((const uint8_t*)_mesh->meta_tag(), 4);
	else
		write_d_meta((const uint8_t*)"D4M3", 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* mesh を D_CHUNK へストリーム書き込み */
{
	if ( _mesh.is_notNull() ) {
		struct Sink : d4ChunkSink {
			ptsd4WireCacheStreamWriterMesh_ *w;
			void chunk(const uint8_t *data, int n) { w->chunk(data, n); }
		} sink;
		sink.w = this;
		_mesh->encode(sink);
	}
	return rDO|FIN_START;
}

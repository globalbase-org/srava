/*
 * ptsd5WireCacheStreamWriterMesh — d5 mesh キャッシュ出力用 writer 派生 (rev4 Phase D-3)。
 * ptsmfWireCacheStreamWriterMesh のミラー。INIT で D_META タグ "D5M3" を書き、ACT_START で
 * d5Mesh::encode が chunk()=d_chunk を直接呼ぶ。基底が streamhdr/TSE_ASSERT/W_END/TSE_RETURN を担う。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"d5/c++/d5Mesh.h"
#include	"_ts2/c++/ptsd5WireCacheStreamWriterMesh_.h"

CLASS_TINYSTATE(d5/c++/ptsd5WireCacheStreamWriterMesh,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsd5WireCacheStreamWriterMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<d5Mesh> _mesh);

	sRptr<ptsObject,tinyState>		parent;

	/* d5Mesh の Sink 窓口: 基底 protected の d_chunk を公開して直接ストリームさせる。 */
	void	chunk(const uint8_t *data, int n);
protected:
	sPtr<d5Mesh>	meshObj;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	<stdint.h>
class ptsObject;
class stdString;
class d5Mesh;
TS_END_INTERFACE

#endif


ptsd5WireCacheStreamWriterMesh_::ptsd5WireCacheStreamWriterMesh_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    meshObj = _mesh;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptsd5WireCacheStreamWriterMesh_::chunk(const uint8_t *data, int n)
{
	d_chunk(data, n);
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグ "D5M3" を書く */
{
	if ( meshObj.is_notNull() )
		write_d_meta((const uint8_t*)meshObj->meta_tag(), 4);
	else
		write_d_meta((const uint8_t*)"D5M3", 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* mesh を D_CHUNK へストリーム書き込み */
{
	if ( meshObj.is_notNull() ) {
		struct Sink : d5ChunkSink {
			ptsd5WireCacheStreamWriterMesh_ *w;
			void chunk(const uint8_t *data, int n) { w->chunk(data, n); }
		} sink;
		sink.w = this;
		meshObj->encode(sink);
	}
	return rDO|FIN_START;
}

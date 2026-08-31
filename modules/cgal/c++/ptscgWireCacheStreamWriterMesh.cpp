/*
 * ptscgWireCacheStreamWriterMesh — mesh(バイナリ)キャッシュ出力用の writer 派生。
 *   - コンストラクタで cgMesh(Surface_mesh ラッパ)を受け取る(blob は持たない)。
 *   - INIT gate で D_META に形式タグ "MESH" を書く。
 *   - ACT_START(TS_THREAD)で cgaMeshCodec::encode が **chunk()=d_chunk を直接呼び**、mesh を
 *     D_CHUNK にストリーム書き込みする(メモリは mesh 本体 + 高々 1 チャンク 8KB のみ)。
 * 基底が streamhdr / TSE_ASSERT / W_END 番兵 / TSE_RETURN を担う。
 *
 * chunk() は cgaMeshCodec(Sink)から呼ばれる窓口で、基底 protected の d_chunk を公開する。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaMeshCodec.h"
#include	"_ts2/c++/ptscgWireCacheStreamWriterMesh_.h"

CLASS_TINYSTATE(cg/c++/ptscgWireCacheStreamWriterMesh,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptscgWireCacheStreamWriterMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<cgMesh> _mesh);

	sRptr<ptsObject,tinyState>		parent;

	/* cgaMeshCodec の Sink 窓口: 基底 protected の d_chunk を公開して直接ストリームさせる。 */
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
class cgMesh;
TS_END_INTERFACE

#endif


ptscgWireCacheStreamWriterMesh_::ptscgWireCacheStreamWriterMesh_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptscgWireCacheStreamWriterMesh_::chunk(const uint8_t *data, int n)
{
	d_chunk(data, n);   /* 基底のチャンクバッファ(8KB)へ。満杯で D_CHUNK レコードを flush */
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_META に形式タグ(mesh が供給。3D="MESH")を書く */
{
	if ( _mesh.is_notNull() )
		write_d_meta((const uint8_t*)_mesh->meta_tag(), 4);
	else
		write_d_meta((const uint8_t*)"MESH", 4);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_THREAD(ACT_START)                          /* mesh を D_CHUNK へストリーム書き込み(多態 encode) */
{
	if ( _mesh.is_notNull() ) {
		/* chunk() を cgChunkSink にアダプトして mesh->encode() に渡す(次元非依存)。 */
		struct Sink : cgChunkSink {
			ptscgWireCacheStreamWriterMesh_ *w;
			void chunk(const uint8_t *data, int n) { w->chunk(data, n); }
		} sink;
		sink.w = this;
		_mesh->encode(sink);
	}
	return rDO|FIN_START;
}

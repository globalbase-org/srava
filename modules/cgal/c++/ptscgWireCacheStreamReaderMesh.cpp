/*
 * ptscgWireCacheStreamReaderMesh — mesh(バイナリ)キャッシュ入力用の reader 派生。
 *   - METADATA gate で先頭 D_META の形式タグが "MESH" かを検証。違えば errCode。
 *   - ACT_START(TS_THREAD)で cgMesh を生成し、cgaMeshCodec::decode が pull() でチャンク境界を
 *     またいでバイトを取り、点群/面を **mesh へ直接登録**する。中間 blob は持たない
 *     (メモリは mesh 本体 + 高々 1 チャンク + 座標 1 個分の文字列のみ)。
 *   - 結果は cgMesh(pigData 派生 = stdObject)を result に渡す(d_cast 不要)。
 * 基底が open/streamhdr 検証/番兵検出/ポーリング(書込中 attach 可)/TSE_RETURN を担う。
 *
 * pull() は cgaMeshCodec(Source)の窓口。現チャンク(rec_payload)を chunkPos で消費し、尽きたら
 * next_record() で次の D_CHUNK を取る(writer が書込中なら基底がポーリングで待つ = read-while-write)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigwire.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaMeshCodec.h"
#include	"_ts2/c++/ptscgWireCacheStreamReaderMesh_.h"

CLASS_TINYSTATE(cg/c++/ptscgWireCacheStreamReaderMesh,pig/c++/ptsWireCacheStreamReader)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptscgWireCacheStreamReaderMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName);

	sRptr<ptsObject,tinyState>		parent;

	/* cgaMeshCodec の Source 窓口: D_CHUNK ストリームから n バイトをチャンク境界をまたいで取る。 */
	void	pull(uint8_t *dst, int n);
	/* まだ D_CHUNK データが残っているか(W_END なら 0)。pull と同じ前進ロジックだが消費しない
	 * (次の有効チャンクを load して chunkPos=先頭に置くだけ)。後方互換の任意セクション読み判定用。 */
	int	more();
protected:
	int	chunkPos;   /* 現 rec_payload(D_CHUNK)内の論理カーソル */
	int	pullErr;    /* 想定外の早期終端/エラー */
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	<stdint.h>
class ptsObject;
class stdString;
TS_END_INTERFACE

#endif


ptscgWireCacheStreamReaderMesh_::ptscgWireCacheStreamReaderMesh_(TS_ARGS0)
        : ptsWireCacheStreamReader_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    chunkPos = 0;
    pullErr  = 0;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptscgWireCacheStreamReaderMesh_::pull(uint8_t *dst, int n)
{
	int got = 0;
	while ( got < n ) {
		while ( chunkPos >= rec_payload.length() ) {   /* 現チャンク尽き → 次の D_CHUNK */
			int r = next_record();
			if ( r <= 0 ) {                            /* W_END / エラー = 想定外の早期終端 */
				pullErr = 1;
				while ( got < n ) dst[got++] = 0;
				return;
			}
			chunkPos = 0;
			if ( rec_type != D_CHUNK )                 /* D_CHUNK 以外は読み飛ばす */
				chunkPos = rec_payload.length();
		}
		int avail = rec_payload.length() - chunkPos;
		int take  = ( n - got < avail ) ? (n - got) : avail;
		for ( int k = 0 ; k < take ; ++k ) dst[got + k] = rec_payload[chunkPos + k];
		got      += take;
		chunkPos += take;
	}
}

int
ptscgWireCacheStreamReaderMesh_::more()
{
	/* pull の冒頭ループと同じ: 現チャンクが尽きていたら次の D_CHUNK へ前進。W_END/エラーなら
	 * これ以上データなし(0)。データが残っていれば 1(chunkPos は先頭のまま=後続 pull が読む)。 */
	while ( chunkPos >= rec_payload.length() ) {
		int r = next_record();
		if ( r <= 0 )
			return 0;   /* W_END = 末尾(旧 blob にはガイド節が無い) */
		chunkPos = 0;
		if ( rec_type != D_CHUNK )
			chunkPos = rec_payload.length();
	}
	return 1;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* D_META タグから具体型を作れるか検証 */
{
	const uint8_t *m = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	if ( cgMesh::create_for_meta(m, meta.length()) == thNULL )
		errCode = -2;      /* 自分の対応形式ではない(未知タグ) */
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* D_CHUNK ストリームを mesh へ多態 decode */
{
	chunkPos = rec_payload.length();   /* INI で読んだ D_META を消費済みにし、最初の pull で D_CHUNK へ */
	pullErr  = 0;
	const uint8_t *mp = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	sPtr<cgMesh> mesh = cgMesh::create_for_meta(mp, meta.length());   /* タグで具体型を生成 */
	if ( mesh == thNULL ) { errCode = -2; return rDO|FIN_START; }
	/* pull() を cgChunkSource にアダプトして mesh->decode() に渡す(次元非依存)。 */
	struct Src : cgChunkSource {
		ptscgWireCacheStreamReaderMesh_ *r;
		void pull(uint8_t *dst, int n) { r->pull(dst, n); }
		int  more() { return r->more(); }
	} src;
	src.r = this;
	mesh->decode(src);
	if ( pullErr ) { errCode = -1; return rDO|FIN_START; }
	result = mesh;                     /* cgMesh は pigData=stdObject → そのまま渡す(d_cast 不要) */
	return rDO|FIN_START;
}

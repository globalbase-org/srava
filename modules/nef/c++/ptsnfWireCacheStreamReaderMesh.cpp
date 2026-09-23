/*
 * ptsnfWireCacheStreamReaderMesh — nf(Nef)mesh キャッシュ入力用 reader 派生 (#3433 P1)。
 * cg/mf 版のミラー。META gate で D_META タグを検証し (自型 "NEF3" と cg の "MESH" を受理 =
 * ★MESH→nf の昇格読みはフレーミングが同一なので同じ decode 経路)、ACT_START で nfNefMesh を
 * 生成して decode が pull() でチャンク境界をまたいでバイトを取り Nef を再構成する。
 *
 * ★★ #3559: **どちらの変種として読むか**は ctor で渡される (@_variant@)。この TU 自身は
 *   変種を焼き込んでいない = 幾何ライブラリ (libsrava_cg) に 1 本だけ在ればよい。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigwire.h"
#include	"nf/c++/nfMesh.h"
#include	"_ts2/c++/ptsnfWireCacheStreamReaderMesh_.h"

#include	<stdio.h>   /* #3479: snprintf (理由文の組み立て) */
#include	<string.h>   /* memcmp */

CLASS_TINYSTATE(nf/c++/ptsnfWireCacheStreamReaderMesh,pig/c++/ptsWireCacheStreamReader)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsnfWireCacheStreamReaderMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		/* ★★ #3559: **どの変種として読むか** (nfMesh.h の nfWireVariant)。
		 *   受ける 4CC の判定と、読めなかったときに名乗るモジュール名が入っている。
		 *   ⚠ 以前は @nfGeom::create_for_meta@ と @NF_MODULE_NAME@ を直に参照していた。
		 *     それだと **この TU が変種ごとに別物**になり、幾何ライブラリを 1 本にできない。 */
		const nfWireVariant *_variant);

	sRptr<ptsObject,tinyState>		parent;

	/* nfMesh の Source 窓口: D_CHUNK ストリームから n バイトを境界跨ぎで取る。 */
	void	pull(uint8_t *dst, int n);
	int	more();
protected:
	int	chunkPos;
	int	pullErr;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	<stdint.h>
class ptsObject;
class stdString;
struct nfWireVariant;
TS_END_INTERFACE

#endif


ptsnfWireCacheStreamReaderMesh_::ptsnfWireCacheStreamReaderMesh_(TS_ARGS0)
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
ptsnfWireCacheStreamReaderMesh_::pull(uint8_t *dst, int n)
{
	/* ★ #3506: 負の n は呼び手の誤り。黙って 0 バイト返すと空の値として通ってしまう。 */
	if ( n < 0 ) { pullErr = 1; return; }
	int got = 0;
	while ( got < n ) {
		while ( chunkPos >= rec_payload.length() ) {
			int r = next_record();
			if ( r <= 0 ) {
				pullErr = 1;
				while ( got < n ) dst[got++] = 0;
				return;
			}
			chunkPos = 0;
			if ( rec_type != D_CHUNK )
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
ptsnfWireCacheStreamReaderMesh_::more()
{
	while ( chunkPos >= rec_payload.length() ) {
		int r = next_record();
		if ( r <= 0 )
			return 0;
		chunkPos = 0;
		if ( rec_type != D_CHUNK )
			chunkPos = rec_payload.length();
	}
	return 1;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* D_META タグが nf の受理形式か検証 */
{
	const uint8_t *m = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	if ( _variant->create(m, meta.length()) == thNULL ) {
		/* ★ #3479: どの形式を誰が読めなかったのかを言う。従来は errCode だけで、
		 *   利用者には「materialize できない」としか届かなかった。 */
		char b[128];
		::snprintf(b, sizeof b, "module '%s' has no reader for format '%.4s'",
		           _variant->module, ( meta.length() >= 4 ) ? (const char*)m : "????");
		set_err(-2, b);
	}      /* nf の対応形式ではない(未知タグ) */
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* D_CHUNK ストリームを nfGeom へ decode */
{
	chunkPos = rec_payload.length();   /* INI の D_META を消費済みにし、最初の pull で D_CHUNK へ */
	pullErr  = 0;
	const uint8_t *mp = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	sPtr<nfGeom> geom = _variant->create(mp, meta.length());
	if ( geom == thNULL ) {                     /* META gate と同じ理由 (再掲) */
		char b[128];
		::snprintf(b, sizeof b, "module '%s' has no reader for format '%.4s'",
		           _variant->module, ( meta.length() >= 4 ) ? (const char*)mp : "????");
		set_err(-2, b);
		return rDO|FIN_START;
	}
	struct Src : nfChunkSource {
		ptsnfWireCacheStreamReaderMesh_ *r;
		void pull(uint8_t *dst, int n) { r->pull(dst, n); }
		int  more() { return r->more(); }
	} src;
	src.r = this;
	geom->decode(src);
	if ( pullErr ) { set_err(-1, "the cache stream ended or could not be read while decoding"); return rDO|FIN_START; }
	/* ★ 境界メッシュから Nef を作れなかった (自己交差など Nef の前提を満たさない入力)。
	 *   黙って空集合を返すと volume が 0 になるので、ここでエラーにする。
	 *   ★これを入れる前は CGAL の assertion で **agent プロセスごと落ちて**いた
	 *   ("agent closed unexpectedly" としか出ず原因が分からなかった)。 */
	{
		sPtr<nfNefMesh> m3 = sPtr<nfNefMesh>::d_cast(geom);
		if ( m3.is_notNull() && m3->build_failed() ) {
			/* ★ #3504: 理由が付いていればそれを出す (4 GiB 超の SNC など)。 */
			const char *why = m3->last_error();
			set_err(-2, why ? why
			                : "the stored boundary mesh could not be built into a Nef polyhedron "
			                  "(self-intersecting or otherwise not a valid solid boundary)");
			return rDO|FIN_START;
		}
	}
	result = geom;
	return rDO|FIN_START;
}

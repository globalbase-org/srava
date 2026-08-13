/*
 * pigDataRef — D_REF の pigData 表現の構築/判定/取り出し (#3406, 2026-07-31 メモ 2.)。
 * 素の C++ (pigData の上の薄いヘルパ)。設計はヘッダ参照。
 */
#include "pig/c++/pigDataRef.h"

#include <string.h>

/* hash のキー名。ReaderRef/WriterRef と本ファイルだけが知っていればよい。 */
static const char *K_KIND  = "ref_kind";
static const char *K_PATH  = "path";
static const char *K_SIZE  = "size";
static const char *K_MTIME = "mtime";
static const char *K_CHASH = "content_hash";

static sPtr<pigData> key_of(const char *k) { return thNEW(pigDataString,(k)); }

sPtr<pigData>
pig_data_ref_make(int kind, sPtr<stdString> path,
                  INTEGER64 size, INTEGER64 mtime, pHashKeyType chash)
{
	sPtr<pigDataHash> h = thNEW(pigDataHash,());
	h->set_ix(key_of(K_KIND),  thNEW(pigDataInteger,((INTEGER64)kind)));
	h->set_ix(key_of(K_PATH),  thNEW(pigDataString,(( path != thNULL ) ? path
	                                                : sPtr<stdString>(thNEW(stdString,(""))))));
	h->set_ix(key_of(K_SIZE),  thNEW(pigDataInteger,(size)));
	h->set_ix(key_of(K_MTIME), thNEW(pigDataInteger,(mtime)));
	h->set_ix(key_of(K_CHASH), thNEW(pigDataInteger,((INTEGER64)chash)));
	return thNEW(pigDataPair,(thNEW(pigDataString,(PIG_DREF_TAG)), sPtr<pigData>::d_cast(h)));
}

int
pig_data_ref_is(sPtr<pigData> body)
{
	sPtr<pigDataPair> p = sPtr<pigDataPair>::d_cast(body);
	if ( p == thNULL )
		return 0;
	sPtr<pigData> c = p->car();
	if ( c == thNULL || c->get_str() == thNULL )
		return 0;
	return ( ::strcmp(c->get_str()->get_str(), PIG_DREF_TAG) == 0 ) ? 1 : 0;
}

int
pig_data_ref_get(sPtr<pigData> body, int *kind, sPtr<stdString> *path,
                 INTEGER64 *size, INTEGER64 *mtime, pHashKeyType *chash)
{
	if ( ! pig_data_ref_is(body) )
		return 0;
	sPtr<pigDataHash> h = sPtr<pigDataPair>::d_cast(body)->cdr()->obt_hash();
	if ( h == thNULL )
		return 0;
	/* get_ix はキー欠落で pigDataError を返す (thNULL ではない)。壊れた表現は 0 で弾く。 */
	sPtr<pigData> vk = h->get_ix(key_of(K_KIND));
	sPtr<pigData> vp = h->get_ix(key_of(K_PATH));
	sPtr<pigData> vs = h->get_ix(key_of(K_SIZE));
	sPtr<pigData> vm = h->get_ix(key_of(K_MTIME));
	sPtr<pigData> vc = h->get_ix(key_of(K_CHASH));
	if ( vk == thNULL || vp == thNULL || vs == thNULL || vm == thNULL || vc == thNULL )
		return 0;
	if ( vk->is_error() || vp->is_error() || vs->is_error() || vm->is_error() || vc->is_error() )
		return 0;
	if ( kind  ) *kind  = (int)vk->get_int();
	if ( path  ) *path  = vp->get_str();
	if ( size  ) *size  = vs->get_int();
	if ( mtime ) *mtime = vm->get_int();
	if ( chash ) *chash = (pHashKeyType)vc->get_int();
	return 1;
}

/*
 * nfmCast — cast("mf-mesh3d", <nef の値>) の計算本体 (akira-project #3499)。
 *   ★ **nef_snc ⇄ manifold の境界モジュール** nef_mf.so が持つ唯一の op。
 *
 * ★★ 存在理由は nfcCast.cpp と同じ。manifold.so は **CGAL 非依存 (GPL 非汚染)** を設計として
 *   守っているので SNC を読めず、行き先に reader を足せない。#3478 は nef_snc 側に厳密境界の
 *   付録を書かせて解いたが、encode ごとの to_mesh() が高くついた (#3499)。
 *   ⇒ 変換をこの .so に切り出し、**cast が呼ばれたときだけ** to_mesh() を払う。
 *
 * ★ 変換は 2 段: Nef → 厳密境界 (EPECK) → double。
 *   ★★ 後段は **cgaMeshCodec の framing をそのまま通す** (メモリ上で encode → decode)。
 *     mfMesh には「cg の "MESH" と同一フレーミングを読む」経路 (mesh_exact) が既にあり、
 *     これは #3478 以前から cast("mf-mesh3d", <cg の値>) が通っている実績のある経路。
 *     ここで有理数→double の丸めを**書き直すと、同じ入力に対して cg 経由と別の答えが
 *     出うる** ので、既存の 1 本を通す。
 *   ⚠ したがってこの橋は CGAL (GPL) と Manifold (Apache-2.0) の両方をリンクする。
 *     混ざるのは **この .so だけ**で、manifold.so は CGAL 非依存のまま。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"cg/c++/cgaMeshCodec.h"
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfmCast_.h"
#include	<vector>
#include	<string.h>
#include	"pig/c++/pigModuleError.h"
/* ★ #3475: このモジュール専用のエラー生成子。文言は "[TAG] nef_mf/op: message" になる。 */
PIG_DEFINE_MODULE_ERR(nfm_err, "nef_mf")


CLASS_TINYSTATE(nfm/c++/nfmCast,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfmCast_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfMesh>	out;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class ptsObject;
class pigData;
class stdString;
class mfMesh;
TS_END_INTERFACE

#endif


nfmCast_::nfmCast_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 厳密境界 (cg の "MESH" framing) をメモリに溜める / 読み返すための最小の Sink/Source。
 * ★ ファイルにもキャッシュにも触らない — 1 回の cast の中で閉じている。 */
namespace {

struct MemSink {
	std::vector<uint8_t> buf;
	void chunk(const uint8_t *d, int n) { buf.insert(buf.end(), d, d + n); }
};

struct MemSource : public mfChunkSource {
	const std::vector<uint8_t> *b;
	size_t pos;
	MemSource(const std::vector<uint8_t> &v) : b(&v), pos(0) {}
	virtual void pull(uint8_t *dst, int n)
	{
		int k = n;
		if ( pos + (size_t)k > b->size() ) k = (int)(b->size() - pos);
		if ( k > 0 ) ::memcpy(dst, &(*b)[pos], (size_t)k);
		if ( k < n ) ::memset(dst + k, 0, (size_t)(n - k));   /* 尽きたら 0 埋め */
		pos += (size_t)( k > 0 ? k : 0 );
	}
	virtual int more() { return ( pos < b->size() ) ? 1 : 0; }
};

}   /* anonymous namespace */

void
nfmCast_::compute()
{
	/* ★ cast の引数は **cast(型名, 幾何)** の 2 つ (CAST_IN = { AK_INLINE, AK_CACHE })。
	 *   幾何は args[1]。args[0] は目標の型名で、ここに来ている時点で既に解決済み。 */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 1 ) ? sPtr<nfMesh>::d_cast((*args)[1]) : sPtr<nfMesh>();
	if ( ! in.is_notNull() ) {
		result = nfm_err(thNEW(stdString,("cast: needs a Nef (SNC) value")));
		return;
	}
	nfMesh::Mesh bnd;
	if ( ! in->to_mesh(bnd) ) {
		result = nfm_err(thNEW(stdString,
		    /* ★ **入力の形式 (NEF3) を文面に出す** — 利用者が受け取るのは「どの値が
		     *   どの形式で書かれていて、なぜ渡せないのか」で、型名だけでは
		     *   キャッシュを見に行く手がかりにならない (#3479 の作法)。 */
		    ("cast: the Nef value (nf-mesh3d, cache format NEF3) is a bare SNC with no "
		     "boundary representation (it is unbounded, e.g. the result of complement), "
		     "which mf-mesh3d cannot represent")));
		return;
	}
	MemSink sink;
	cgaMeshCodec::encode(bnd, sink);

	/* ★ mfMesh は既定構築を持たない (常に Manifold を伴う)。空の Manifold から起こして
	 *   decode で中身を入れる — mfGeom::create_for_meta と同じ組み立て方。 */
	sPtr<mfMesh> m = thNEW(mfMesh,(manifold::Manifold()));
	m->set_mesh_exact_input();   /* decode() は cg の "MESH" framing を読む */
	MemSource src(sink.buf);
	m->decode(src);
	if ( m->decode_failed() ) {
		const char *why = m->decode_why();
		result = nfm_err(thNEW(stdString,
		    (( why != 0 ) ? why : "cast: could not read the exact boundary")));
		return;
	}
	out = m;
}

sPtr<pigData>
nfmCast_::get_result()
{
	return ( result != thNULL ) ? result : out;
}

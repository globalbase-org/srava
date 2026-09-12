#ifndef COMMON_BLOCKFRAME_H
#define COMMON_BLOCKFRAME_H
/*
 * blockframe — cache payload の **ブロック分割フレーミング** (#3507)。
 *
 *   いま   [長さ][payload 全部]
 *                ↑ 書き始めに長さを知らない = **全文を一度作るしかない**
 *   これ   [u32 blocklen][block] [u32 blocklen][block] … [u32 0]
 *                ↑ 各ブロックの長さは「そのブロックを埋めた時点で」分かる
 *                  終端は長さ 0 のブロック
 *
 * 全長を前置する形式は、シリアライザが書き終えるまで長さを教えてくれない以上、
 * 一時領域に全文を作ることを **強制する**。しかも std::ostringstream + str() は
 * それを 2 部持つ (内部バッファとコピー)。SNC が 2.80 GiB になる模型では
 * この 1 op だけで一時領域が約 5.6 GiB になっていた。
 *
 * ブロック化で得られるもの:
 *   - 一時領域が固定バッファ 1 個 (既定 1 MiB) になる
 *   - ブロック長は常に小さいので @int@ の 2 GiB 問題が**構造的に起きない** (#3504/#3506)
 *   - 長さ欄 u32 による **4 GiB の上限が消える** (総量に上限が無くなる)
 *
 * ★ Sink / Source はモジュールごとに別の型 (nfChunkSink / ocChunkSink / vdChunkSink …) だが
 *   シグネチャは同一なので、ここはテンプレートで受ける (共通基底を導入すると
 *   モジュール境界を跨ぐ型になってしまう)。
 *   Sink   … void chunk(const uint8_t *data, int n)
 *   Source … void pull (uint8_t *dst, int n)
 */
#include	<streambuf>
#include	<string>
#include	<vector>
#include	<stdint.h>
#include	<string.h>

namespace blockframe {

/* 既定のブロック長。int にも u32 にも余裕で収まり、1 op あたりの一時領域を決める。 */
enum { BLOCK_BYTES = 1u << 20 };            /* 1 MiB */
/* 読み側の番人: これを越えるブロック長を名乗る payload は壊れているとみなす。
 * (書き手は BLOCK_BYTES しか書かない。将来ブロック長を上げても余裕があるように広めに取る) */
enum { MAX_BLOCK_BYTES = 256u << 20 };      /* 256 MiB */

inline void put_u32(uint8_t *b, uint32_t n)
{
	b[0] = (uint8_t)(n & 0xff);
	b[1] = (uint8_t)((n >>  8) & 0xff);
	b[2] = (uint8_t)((n >> 16) & 0xff);
	b[3] = (uint8_t)((n >> 24) & 0xff);
}

inline uint32_t get_u32(const uint8_t *b)
{
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
	     | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/* ---- 書き: ostream の下に敷いて、埋まったそばからブロックとして sink へ流す ----
 * ★ 書き終えたら **必ず finish() を呼ぶ**。残りを出して終端 [u32 0] を書く。
 *   デストラクタでは呼ばない — 例外を投げられない場所で sink へ書きたくないため。 */
template<class Sink>
class obuf : public std::streambuf {
public:
	explicit obuf(Sink &s, size_t blk = (size_t)BLOCK_BYTES)
	    : sink_(s), buf_(blk ? blk : (size_t)BLOCK_BYTES), done_(0)
	{
		setp(&buf_[0], &buf_[0] + buf_.size());
	}
	void finish()
	{
		if ( done_ ) return;
		flush_block();
		uint8_t b[4];
		put_u32(b, 0);                  /* 終端 */
		sink_.chunk(b, 4);
		done_ = 1;
	}
protected:
	virtual int overflow(int c)
	{
		flush_block();
		if ( c != traits_type::eof() ) {
			*pptr() = traits_type::to_char_type(c);
			pbump(1);
		}
		return c;
	}
	/* ★★ sync() では **ブロックを切らない**。シリアライザは行末ごとに @std::endl@ で
	 *   flush することがあり (CGAL の SNC 出力が実際にそう)、素直に従うと
	 *   **1 行 = 1 ブロック**になってヘッダ 4 バイトと sink 呼び出しが行数ぶん増える
	 *   (実測: 最初のブロックが 22 バイト = "Selective Nef Complex" の 1 行)。
	 *   ブロックの切れ目は「バッファが埋まったとき」と finish() だけでよい —
	 *   sink は装置ではないので、途中で押し出す意味が無い。 */
	virtual int sync() { return 0; }
private:
	/* ★ 空ブロックは **書いてはいけない** (長さ 0 は終端の意味)。sync() は
	 *   ostream::flush や tie で何度でも呼ばれるので、ここで必ず弾く。 */
	void flush_block()
	{
		size_t n = (size_t)(pptr() - pbase());
		if ( n == 0 ) return;
		uint8_t b[4];
		put_u32(b, (uint32_t)n);
		sink_.chunk(b, 4);
		sink_.chunk((const uint8_t*)pbase(), (int)n);
		setp(&buf_[0], &buf_[0] + buf_.size());
	}
	Sink            &sink_;
	std::vector<char> buf_;
	int               done_;
};

/* ---- 読み: istream の下に敷いて、ブロックを 1 個ずつ引く ----
 * ★ seek しない (逐次読みのシリアライザ専用)。位置を戻す相手 (occt の BinTools_IStream)
 *   には read_all + membuf を使う。
 * ★ 途中で読み終えた場合は **drain() を呼んでから** 後続を読むこと。
 *   ストリームは先読みでブロックを丸ごと抱えているので、呼ばないと Source の位置が
 *   ずれる (hybrid の [SNC][境界] のように後ろに別の値が続く形式で効く)。 */
template<class Source>
class ibuf : public std::streambuf {
public:
	explicit ibuf(Source &s) : src_(s), eof_(0), bad_(0) { setg(0, 0, 0); }
	/* 残りのブロックを終端まで読み捨てる。戻り値 0 = 壊れていた。 */
	int drain()
	{
		setg(0, 0, 0);
		while ( ! eof_ && ! bad_ ) {
			uint32_t n = next_len();
			if ( n == 0 ) break;
			buf_.resize(n);
			src_.pull((uint8_t*)&buf_[0], (int)n);
		}
		return bad_ ? 0 : 1;
	}
	int bad() const { return bad_; }
protected:
	virtual int underflow()
	{
		if ( gptr() < egptr() )
			return traits_type::to_int_type(*gptr());
		if ( eof_ || bad_ )
			return traits_type::eof();
		uint32_t n = next_len();
		if ( n == 0 )
			return traits_type::eof();
		buf_.resize(n);
		src_.pull((uint8_t*)&buf_[0], (int)n);
		setg(&buf_[0], &buf_[0], &buf_[0] + n);
		return traits_type::to_int_type(*gptr());
	}
private:
	uint32_t next_len()
	{
		uint8_t b[4];
		src_.pull(b, 4);
		uint32_t n = get_u32(b);
		if ( n == 0 ) { eof_ = 1; return 0; }
		if ( n > (uint32_t)MAX_BLOCK_BYTES ) { bad_ = 1; eof_ = 1; return 0; }
		return n;
	}
	Source           &src_;
	std::vector<char> buf_;
	int               eof_;
	int               bad_;
};

/* ---- ブロック列を 1 本の連続バッファへ集める ----
 * 位置を戻すシリアライザ (occt の BinTools_IStream::GoTo) 用。全文バッファは
 * *1 本が下限* で、これがその 1 本。cap を越えたら 0 を返す (壊れた cache の番人)。 */
template<class Source>
inline int read_all(Source &src, std::string &out, uint64_t cap)
{
	out.clear();
	for ( ; ; ) {
		uint8_t b[4];
		src.pull(b, 4);
		uint32_t n = get_u32(b);
		if ( n == 0 ) return 1;
		if ( n > (uint32_t)MAX_BLOCK_BYTES ) return 0;
		if ( (uint64_t)out.size() + n > cap ) return 0;
		size_t at = out.size();
		out.resize(at + n);
		src.pull((uint8_t*)&out[0] + at, (int)n);
	}
}

/* ---- 既にある連続バッファの上に敷く streambuf (コピーしない・seek できる) ----
 * ★ istringstream(const string&) は中身を **複製する**。占有を 2 本から 1 本へ落とすために
 *   受信バッファを直接 get 領域にする。 */
class membuf : public std::streambuf {
public:
	membuf(const char *p, size_t n)
	{
		char *b = const_cast<char*>(p);
		setg(b, b, b + n);
	}
protected:
	virtual std::streampos seekoff(std::streamoff off, std::ios_base::seekdir way,
	                               std::ios_base::openmode which)
	{
		if ( ! (which & std::ios_base::in) ) return std::streampos(-1);
		std::streamoff base = 0;
		if      ( way == std::ios_base::beg ) base = 0;
		else if ( way == std::ios_base::cur ) base = gptr() - eback();
		else if ( way == std::ios_base::end ) base = egptr() - eback();
		else                                  return std::streampos(-1);
		std::streamoff pos = base + off;
		if ( pos < 0 || pos > (egptr() - eback()) ) return std::streampos(-1);
		setg(eback(), eback() + pos, egptr());
		return std::streampos(pos);
	}
	virtual std::streampos seekpos(std::streampos pos, std::ios_base::openmode which)
	{
		return seekoff(std::streamoff(pos), std::ios_base::beg, which);
	}
};

}   /* namespace blockframe */

#endif

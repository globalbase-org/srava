#ifndef D5_MESH_H
#define D5_MESH_H
/*
 * d5Mesh — 第5(in-proc mesh 消費)モジュール "d5" の幾何本体 (⑤ cross-module 型変換・P4)。
 *
 *   目的: d3 (単独走行実証) のクローンだが exec_default=THREAD。**別モジュール (manifold) が in-proc で
 *   作った mf mesh を、自型 d5-mesh3d として in-proc 消費する** 2 個目の in-proc mesh モジュールとして、
 *   converted get_body(type) 経路を初めて exercise する。CGAL も Manifold も一切参照しない
 *   (依存は pig/pts/tinyState のみ・全て host 供給)。
 *
 *   mfMesh のミラーだが極小化: 抽象基底 (mfGeom) は挟まず pigDataWireTyped 直継承 (2D 変種なし)。
 *   内部表現は座標/インデックスの生配列だけ (幾何ライブラリ不要)。
 *     - verts_ : 頂点 x,y,z の flat 配列 (size = 3*nv)
 *     - faces_ : 三角形の頂点 index flat 配列 (size = 3*nf)
 *   codec framing (little-endian): [u32 nv][u32 nf] 頂点×nv(double x,y,z) 三角形×nf(u32 i,j,k)。
 *   ★ この framing は **mfMesh の MFM3 と完全同一** なので、create_for_meta が MFM3 も受理し、
 *     同じ decode() で Manifold の mesh を読める = MFM3→d5-mesh3d の cross reader が成立する。
 *   型軸: 型名 "d5-mesh3d" ↔ 4CC タグ "D5M3" (1:1)。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	<stdint.h>
#include	<vector>

/* codec の Sink/Source 抽象 (mfChunkSink/Source と同シグネチャ)。writer/reader が adapter で実装し、
 * encode/decode が chunk()/pull() 越しに D_CHUNK を直接読み書く。 */
struct d5ChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~d5ChunkSink()   {} };
struct d5ChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~d5ChunkSource() {} };

class d5Mesh : public pigDataWireTyped {   /* rev4 型軸 marker 基底 */
public:
	d5Mesh(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}

	virtual sPtr<stdString> get_str();                    /* 表示用 "d5mesh(nv,nf)" */
	virtual const char* type_name() { return "d5-mesh3d"; }   /* rev4 実装型名 (D5M3 と 1:1) */
	const char* meta_tag() { return "D5M3"; }             /* D_META 4CC (pigData virtual ではない) */

	/* codec: raw double 頂点 + u32 三角形 (little-endian)。 */
	void encode(d5ChunkSink&   sink);
	void decode(d5ChunkSource& src);

	uint32_t nv() const { return (uint32_t)(verts_.size() / 3); }
	uint32_t nf() const { return (uint32_t)(faces_.size() / 3); }

	std::vector<double>&   verts() { return verts_; }
	std::vector<uint32_t>& faces() { return faces_; }

	/* ---- primitive / 合成 (幾何ライブラリ不要の自前実装) ---- */
	static sPtr<d5Mesh> cube(double s);                   /* 原点隅の s×s×s 立方体 (8v/12f) */
	static sPtr<d5Mesh> merge(sPtr<d5Mesh> a, sPtr<d5Mesh> b);   /* 頂点/面を連結 (ブールなし) */

	/* reader 用ファクトリ: D_META タグから具体型を生成 (未知タグは null)。 */
	static sPtr<d5Mesh> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この階層への配線先**。op の OPS 行が OPWIRE(Calc, d5Mesh) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの階層の stream reader を起こす。定義は d5CacheCodec.cpp。 */
	static const pigWireClass WIRE;

protected:
	std::vector<double>   verts_;
	std::vector<uint32_t> faces_;
};

#endif /* D5_MESH_H */

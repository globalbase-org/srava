#ifndef D3_MESH_H
#define D3_MESH_H
/*
 * d3Mesh — 第3(mesh 出力)カーネル "d3" の幾何本体 (rev4 Phase D-3・単独走行実証用)。
 *
 *   目的: value-only の demo.so に続き、**mesh (cacheable typed body) を出力する**第3カーネルを
 *   ホスト無改修で走らせ、rev4 の最終形「planner + 第3カーネル 1 個だけで走る (エージェント非依存)」を
 *   実証する。CGAL も Manifold も一切参照しない (依存は pig/pts/tinyState のみ・全て host 供給)。
 *
 *   mfMesh のミラーだが極小化: 抽象基底 (mfGeom) は挟まず pigDataWireTyped 直継承 (2D 変種なし)。
 *   内部表現は座標/インデックスの生配列だけ (幾何ライブラリ不要)。
 *     - verts_ : 頂点 x,y,z の flat 配列 (size = 3*nv)
 *     - faces_ : 三角形の頂点 index flat 配列 (size = 3*nf)
 *   codec framing (little-endian): [u32 nv][u32 nf] 頂点×nv(double x,y,z) 三角形×nf(u32 i,j,k)。
 *   型軸: 型名 "d3-mesh3d" ↔ 4CC タグ "D3M3" (1:1)。
 */
#include	"pig/c++/pigData.h"
#include	<stdint.h>
#include	<vector>

/* codec の Sink/Source 抽象 (mfChunkSink/Source と同シグネチャ)。writer/reader が adapter で実装し、
 * encode/decode が chunk()/pull() 越しに D_CHUNK を直接読み書く。 */
struct d3ChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~d3ChunkSink()   {} };
struct d3ChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~d3ChunkSource() {} };

class d3Mesh : public pigDataWireTyped {   /* rev4 型軸 marker 基底 */
public:
	d3Mesh(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}

	virtual sPtr<stdString> get_str();                    /* 表示用 "d3mesh(nv,nf)" */
	virtual const char* type_name() { return "d3-mesh3d"; }   /* rev4 実装型名 (D3M3 と 1:1) */
	const char* meta_tag() { return "D3M3"; }             /* D_META 4CC (pigData virtual ではない) */

	/* codec: raw double 頂点 + u32 三角形 (little-endian)。 */
	void encode(d3ChunkSink&   sink);
	void decode(d3ChunkSource& src);

	uint32_t nv() const { return (uint32_t)(verts_.size() / 3); }
	uint32_t nf() const { return (uint32_t)(faces_.size() / 3); }

	std::vector<double>&   verts() { return verts_; }
	std::vector<uint32_t>& faces() { return faces_; }

	/* ---- primitive / 合成 (幾何ライブラリ不要の自前実装) ---- */
	static sPtr<d3Mesh> cube(double s);                   /* 原点隅の s×s×s 立方体 (8v/12f) */
	static sPtr<d3Mesh> merge(sPtr<d3Mesh> a, sPtr<d3Mesh> b);   /* 頂点/面を連結 (ブールなし) */

	/* reader 用ファクトリ: D_META タグから具体型を生成 (未知タグは null)。 */
	static sPtr<d3Mesh> create_for_meta(const uint8_t *meta, int len);

protected:
	std::vector<double>   verts_;
	std::vector<uint32_t> faces_;
};

#endif /* D3_MESH_H */

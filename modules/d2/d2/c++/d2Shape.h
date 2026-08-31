#ifndef D2_SHAPE_H
#define D2_SHAPE_H
/*
 * d2Shape — 第2(2D)カーネル "d2" の幾何本体 (rev4「同一形式を 2 カーネルが次元分担で I/O」実演用)。
 *
 *   目的: d3 (3D 専用) と対になる **2D 専用**の第2カーネル。両者は同じ「生配列 mesh 形式」を共有しつつ
 *   **異なる次元型** (d3-mesh3d / d2-shape2d) を持つ。共有 op `dcount` を両カーネルが自分の次元型で
 *   申告し、decide_executor が入力型 (次元) で正しいカーネルへ振ることを実証する (§9.4/§9.7 Q-E)。
 *   d3Mesh のミラー (2D 版)。CGAL/Manifold 非依存。
 *
 *   内部表現: pts_ = 2D 点 x,y の flat 配列 (size = 2*np)。
 *   codec framing (little-endian): [u32 np] 点×np(double x,y)。
 *   型軸: 型名 "d2-shape2d" ↔ 4CC タグ "D2S2" (1:1)。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	<stdint.h>
#include	<vector>

struct d2ChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~d2ChunkSink()   {} };
struct d2ChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~d2ChunkSource() {} };

class d2Shape : public pigDataWireTyped {
public:
	d2Shape(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}

	virtual sPtr<stdString> get_str();                     /* 表示用 "d2shape(np)" */
	virtual const char* type_name() { return "d2-shape2d"; }   /* rev4 実装型名 (D2S2 と 1:1) */
	const char* meta_tag() { return "D2S2"; }

	void encode(d2ChunkSink&   sink);
	void decode(d2ChunkSource& src);

	uint32_t np() const { return (uint32_t)(pts_.size() / 2); }
	std::vector<double>& pts() { return pts_; }

	static sPtr<d2Shape> square(double s);                 /* 原点隅の s×s 正方形 (4 点) */
	static sPtr<d2Shape> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この階層への配線先**。op の OPS 行が OPWIRE(Calc, d2Shape) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの階層の stream reader を起こす。定義は d2CacheCodec.cpp。 */
	static const pigWireClass WIRE;

protected:
	std::vector<double> pts_;
};

#endif /* D2_SHAPE_H */

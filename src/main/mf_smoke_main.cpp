/* mf_smoke — mfMesh(Manifold ラッパ)の疎通テスト。
 * box/sphere primitive・3ブール・計測・妥当性・STL/OFF 書き出しを mfMesh 経由で叩く。
 * これが通れば mfMesh を mf agent の計算本体(mfaUnion 等)+ codec に載せる。*/
#include <cstdio>
#include <cstring>
#include <vector>
#include "mf/c++/mfMesh.h"
#include "ts2/c++/stdString.h"

/* ラウンドトリップ検証用のインメモリ Sink/Source(本番は wire の D_CHUNK adapter)。*/
struct BufSink : mfChunkSink {
	std::vector<uint8_t> buf;
	void chunk(const uint8_t *d, int n) override { buf.insert(buf.end(), d, d + n); }
};
struct BufSource : mfChunkSource {
	const uint8_t *p; size_t rem;
	BufSource(const std::vector<uint8_t> &b) : p(b.data()), rem(b.size()) {}
	void pull(uint8_t *d, int n) override { std::memcpy(d, p, n); p += n; rem -= n; }
	int  more() override { return rem > 0 ? 1 : 0; }
};

static void report(const char *name, sPtr<mfMesh> m)
{
	std::printf("  %-11s vol=%12.3f area=%11.3f valid=%d  %s\n",
	            name, m->op_volume(), m->op_area(), m->op_valid(), m->get_str()->get_str());
}

int main()
{
	sPtr<mfMesh> cube = mfMesh::box(40.0, 40.0, 40.0);
	sPtr<mfMesh> sph  = mfMesh::sphere(26.0, 64);
	sPtr<mfMesh> uni  = cube->op_union(sph);
	sPtr<mfMesh> dif  = cube->op_difference(sph);
	sPtr<mfMesh> ist  = cube->op_intersection(sph);

	std::printf("MF_SMOKE mfMesh (Manifold-backed):\n");
	report("cube",       cube);
	report("sphere",     sph);
	report("union",      uni);
	report("difference", dif);
	report("intersect",  ist);

	bool ok_stl = dif->write_to("/tmp/mf_diff.stl", "mm");
	bool ok_off = dif->write_to("/tmp/mf_diff.off", "mm");
	std::printf("MF_SMOKE export stl=%d off=%d\n", (int)ok_stl, (int)ok_off);

	/* codec ラウンドトリップ: dif を encode → decode し、volume/tris が一致するか。*/
	BufSink sink;
	dif->encode(sink);
	BufSource src(sink.buf);
	sPtr<mfMesh> rt = thNEW(mfMesh,(manifold::Manifold()));
	rt->decode(src);
	std::printf("MF_ROUNDTRIP bytes=%zu  rt: vol=%.3f tris=%zu valid=%d  orig: vol=%.3f tris=%zu\n",
	            sink.buf.size(), rt->op_volume(), (size_t)rt->manifold().NumTri(), rt->op_valid(),
	            dif->op_volume(), (size_t)dif->manifold().NumTri());
	int rt_ok = ( rt->op_valid() == 1
	           && (size_t)rt->manifold().NumTri() == (size_t)dif->manifold().NumTri() ) ? 1 : 0;
	std::printf("MF_SMOKE_OK stl=%d off=%d roundtrip=%d\n", (int)ok_stl, (int)ok_off, rt_ok);
	return 0;
}

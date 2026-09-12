#ifndef ___common_meshio_h___
#define ___common_meshio_h___

/*
 * meshio.h — 外部メッシュファイル (STL / OFF) の読み手 (ヘッダオンリー・カーネル非依存)。
 *
 * geodesic.h / tube.h / solids.h と同じ方針。実体は manifold モジュールが自前で持っていた
 * パーサ (mfMesh.cpp の MeshBuilder + parse_stl_binary / parse_stl_ascii / parse_off) を
 * そのまま括り出したもので、**CGAL にも Manifold にも依存していなかった** ので丸ごと共有できる。
 *
 * 狙いは import をカーネル間で揃えること (#3474 の続き・2026-09-05)。import が
 * cgal / manifold / occt にしか無いと、STL を読んだ瞬間に式全体のカーネルが決まってしまう
 * (基本立体の欠落と同じ「裏返り」問題)。
 *
 * Sink は geodesic.h / solids.h と同じ形:
 *   struct Sink { int add_vertex(double x,double y,double z); void add_triangle(int a,int b,int c); };
 * ★ **頂点の重複排除はこのヘッダ内で行う** (STL は三角形ごとに座標を持つ形式なので、
 *   そのまま流すと隣接三角形が頂点を共有しない = 位相が閉じない)。座標をキーにして
 *   ユニーク化し、Sink::add_vertex は新頂点のときだけ呼ぶ。
 *
 * ⚠ 形式判定は **拡張子** (.off か否か) と STL のサイズ整合。多くのツールが binary STL にも
 *   "solid" ヘッダを書くので、先頭 5 バイトでは binary/ascii を判別できない
 *   (84 + nt*50 == ファイルサイズ で binary を優先判定し、崩れたら ascii)。
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

namespace srava_geo {

/* 座標でユニーク化しながら Sink へ流す仲介。 */
template<class Sink>
struct MeshIoDedup {
	Sink& sink;
	std::map<std::pair<std::pair<double,double>,double>, int> idx;
	int ntri;
	MeshIoDedup(Sink& s) : sink(s), ntri(0) {}
	int vid(double x, double y, double z) {
		std::pair<std::pair<double,double>,double> key(std::make_pair(x, y), z);
		typename std::map<std::pair<std::pair<double,double>,double>, int>::iterator it = idx.find(key);
		if (it != idx.end()) return it->second;
		int id = sink.add_vertex(x, y, z);
		idx[key] = id;
		return id;
	}
	void tri(int a, int b, int c) { sink.add_triangle(a, b, c); ++ntri; }
};

inline bool meshio_ends_with_ci(const char *s, const char *suf) {
	size_t ls = std::strlen(s), lf = std::strlen(suf);
	if (ls < lf) return false;
	for (size_t i = 0; i < lf; ++i) {
		char a = s[ls - lf + i], b = suf[i];
		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
		if (a != b) return false;
	}
	return true;
}

template<class Sink>
bool meshio_parse_stl_binary(std::FILE *f, MeshIoDedup<Sink> &b) {
	std::fseek(f, 80, SEEK_SET);
	unsigned int nt = 0;
	if (std::fread(&nt, 4, 1, f) != 1) return false;
	for (unsigned int t = 0; t < nt; ++t) {
		float buf[12];
		if (std::fread(buf, 4, 12, f) != 12) return false;
		unsigned short attr;
		if (std::fread(&attr, 2, 1, f) != 1) return false;
		int a = b.vid(buf[3], buf[4],  buf[5]);
		int c = b.vid(buf[6], buf[7],  buf[8]);
		int d = b.vid(buf[9], buf[10], buf[11]);
		b.tri(a, c, d);
	}
	return b.ntri > 0;
}

template<class Sink>
bool meshio_parse_stl_ascii(std::FILE *f, MeshIoDedup<Sink> &b) {
	char tok[128];
	double vv[9];
	int vn = 0;
	while (std::fscanf(f, "%127s", tok) == 1) {
		if (std::strcmp(tok, "vertex") == 0) {
			if (std::fscanf(f, "%lf %lf %lf", &vv[vn], &vv[vn+1], &vv[vn+2]) != 3) return false;
			vn += 3;
			if (vn == 9) {
				int a = b.vid(vv[0], vv[1], vv[2]);
				int c = b.vid(vv[3], vv[4], vv[5]);
				int d = b.vid(vv[6], vv[7], vv[8]);
				b.tri(a, c, d);
				vn = 0;
			}
		}
	}
	return b.ntri > 0;
}

template<class Sink>
bool meshio_parse_off(std::FILE *f, MeshIoDedup<Sink> &b) {
	char line[256];
	if (!std::fgets(line, sizeof line, f)) return false;      /* "OFF" */
	int nv = 0, nf = 0, ne = 0;
	if (std::fscanf(f, "%d %d %d", &nv, &nf, &ne) != 3) return false;
	std::vector<int> vmap((size_t)(nv > 0 ? nv : 0));
	for (int i = 0; i < nv; ++i) {
		double x, y, z;
		if (std::fscanf(f, "%lf %lf %lf", &x, &y, &z) != 3) return false;
		vmap[(size_t)i] = b.vid(x, y, z);
	}
	for (int i = 0; i < nf; ++i) {
		int cnt = 0;
		if (std::fscanf(f, "%d", &cnt) != 1) return false;
		std::vector<int> fv((size_t)(cnt > 0 ? cnt : 0));
		for (int j = 0; j < cnt; ++j)
			if (std::fscanf(f, "%d", &fv[(size_t)j]) != 1) return false;
		for (int j = 1; j + 1 < cnt; ++j)                      /* 三角形ファン分割 */
			b.tri(vmap[(size_t)fv[0]], vmap[(size_t)fv[(size_t)j]], vmap[(size_t)fv[(size_t)j+1]]);
	}
	return b.ntri > 0;
}

/* path を読んで Sink へ三角形を流す。成功なら true。
 * ★ 対応形式は **STL (binary/ascii) と OFF** だけ。各モジュールの import_exts もこれに揃える。 */
template<class Sink>
bool read_mesh_file(const char *path, Sink &sink) {
	std::FILE *f = std::fopen(path, "rb");
	if (!f) return false;
	MeshIoDedup<Sink> b(sink);
	bool ok = false;
	if (meshio_ends_with_ci(path, ".off")) {
		ok = meshio_parse_off(f, b);
	} else {
		std::fseek(f, 0, SEEK_END);
		long sz = std::ftell(f);
		std::fseek(f, 80, SEEK_SET);
		unsigned int nt = 0;
		bool isBin = (std::fread(&nt, 4, 1, f) == 1) && (sz == (long)(84 + (long)nt * 50));
		std::fseek(f, 0, SEEK_SET);
		ok = isBin ? meshio_parse_stl_binary(f, b) : meshio_parse_stl_ascii(f, b);
	}
	std::fclose(f);
	return ok;
}

}  /* namespace srava_geo */

#endif

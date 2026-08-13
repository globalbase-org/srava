#ifndef ___common_mesh3mf_h___
#define ___common_mesh3mf_h___

/*
 * mesh3mf.h — AMF / 3MF 書き出しの共通ライタ (ヘッダオンリー・カーネル非依存)。
 *
 * cgal.so と manifold.so の **両方** が include する (geodesic.h / tube.h と同じ方針・#3415 続き)。
 * どちらの形式も「自前 XML (+ 3MF は自前 zip)」で外部ライブラリに依存せず、幾何カーネルにも
 * 依存しない — 必要なのは「頂点座標の配列・三角形 index の配列・面ごとの色 (任意)」だけなので、
 * 各カーネルは自分の表現からこの TriMesh へ詰め替えて渡す。
 *
 *   - cgal:     Surface_mesh の頂点/面を巡回し、f:color property map を面色に
 *   - manifold: MeshGL64 の vertProperties/triVerts、色は頂点プロパティ ch3..5 (RGB) から面色へ
 *
 * STL/OFF が持てない「単位」を AMF は unit 属性、3MF は <model unit="..."> に刻める。
 * 3MF の色は Materials 拡張の colorgroup で per-triangle 着色。
 *
 * ★出力は決定的 (zip のタイムスタンプを 1980-01-01 固定・無圧縮 STORE)。同一メッシュ→同一バイトなので
 *   content_hash が安定してキャッシュが効く。
 */

#include <string>
#include <vector>
#include <map>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

namespace srava_io {

/* 書き出し対象の最小表現。faceColor が空 = 色なし、非空なら要素数 = 三角形数 (packed 0xRRGGBB)。 */
struct TriMesh {
	std::vector<double>   verts;       /* 3*nv (x,y,z) */
	std::vector<uint32_t> tris;        /* 3*nt (i,j,k) */
	std::vector<uint32_t> faceColor;   /* nt (packed 0xRRGGBB) or 空 */

	size_t nv() const { return verts.size() / 3; }
	size_t nt() const { return tris.size() / 3; }
	bool   has_color() const { return faceColor.size() == nt() && ! faceColor.empty(); }
};

/* AMF の unit 属性語へ正規化 (規格値: inch/millimeter/meter/feet/micron)。
 * 別名 (mm/m/in/ft/um) も許容。未指定/不明は millimeter (3D プリント既定)。 */
inline const char* amf_unit_of(const char* unit)
{
	if ( unit == 0 || unit[0] == '\0' ) return "millimeter";
	if ( ::strcasecmp(unit,"mm")==0 || ::strcasecmp(unit,"millimeter")==0 ) return "millimeter";
	if ( ::strcasecmp(unit,"m")==0  || ::strcasecmp(unit,"meter")==0 )      return "meter";
	if ( ::strcasecmp(unit,"in")==0 || ::strcasecmp(unit,"inch")==0 )       return "inch";
	if ( ::strcasecmp(unit,"ft")==0 || ::strcasecmp(unit,"feet")==0 || ::strcasecmp(unit,"foot")==0 ) return "feet";
	if ( ::strcasecmp(unit,"um")==0 || ::strcasecmp(unit,"micron")==0 || ::strcasecmp(unit,"micrometer")==0 ) return "micron";
	return "millimeter";   /* 不明語は既定 mm (無効な AMF を吐かない) */
}

/* 3MF の単位語へ正規化 (規格値: micron/millimeter/centimeter/inch/foot/meter)。
 * AMF と語彙が少し違う (3MF は foot・centimeter あり)。未指定/不明は millimeter。 */
inline const char* unit_3mf_of(const char *unit)
{
	if ( unit == 0 || unit[0] == '\0' ) return "millimeter";
	if ( ::strcasecmp(unit,"mm")==0 || ::strcasecmp(unit,"millimeter")==0 ) return "millimeter";
	if ( ::strcasecmp(unit,"cm")==0 || ::strcasecmp(unit,"centimeter")==0 ) return "centimeter";
	if ( ::strcasecmp(unit,"m")==0  || ::strcasecmp(unit,"meter")==0 )      return "meter";
	if ( ::strcasecmp(unit,"in")==0 || ::strcasecmp(unit,"inch")==0 )       return "inch";
	if ( ::strcasecmp(unit,"ft")==0 || ::strcasecmp(unit,"feet")==0 || ::strcasecmp(unit,"foot")==0 ) return "foot";
	if ( ::strcasecmp(unit,"um")==0 || ::strcasecmp(unit,"micron")==0 || ::strcasecmp(unit,"micrometer")==0 ) return "micron";
	return "millimeter";
}

/* ---- AMF 書き出し (自前 XML・依存ライブラリなし) ----
 * 三角形メッシュ前提 (全プリミティブは三角化済み)。座標は %.17g で round-trip 桁。 */
inline bool write_amf(const char *path, const TriMesh& m, const char *unit)
{
	FILE *f = ::fopen(path, "wb");
	if ( f == 0 ) return false;
	::fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	::fprintf(f, "<amf unit=\"%s\" version=\"1.1\">\n", amf_unit_of(unit));
	::fprintf(f, "  <object id=\"0\">\n    <mesh>\n      <vertices>\n");
	for ( size_t v = 0 ; v < m.nv() ; ++v ) {
		::fprintf(f, "        <vertex><coordinates>"
		            "<x>%.17g</x><y>%.17g</y><z>%.17g</z></coordinates></vertex>\n",
		          m.verts[3*v], m.verts[3*v+1], m.verts[3*v+2]);
	}
	::fprintf(f, "      </vertices>\n      <volume>\n");
	bool hasColor = m.has_color();
	for ( size_t t = 0 ; t < m.nt() ; ++t ) {
		::fprintf(f, "        <triangle><v1>%u</v1><v2>%u</v2><v3>%u</v3>",
		          (unsigned)m.tris[3*t], (unsigned)m.tris[3*t+1], (unsigned)m.tris[3*t+2]);
		if ( hasColor ) {   /* AMF の色は 0..1 の r/g/b */
			uint32_t c = m.faceColor[t];
			::fprintf(f, "<color><r>%.4g</r><g>%.4g</g><b>%.4g</b></color>",
			          ((c >> 16) & 0xff)/255.0, ((c >> 8) & 0xff)/255.0, (c & 0xff)/255.0);
		}
		::fprintf(f, "</triangle>\n");
	}
	::fprintf(f, "      </volume>\n    </mesh>\n  </object>\n</amf>\n");
	::fclose(f);
	return true;
}

/* ---- 最小 ZIP (STORE=無圧縮) ライタ。3MF は OPC=zip なので外部ライブラリ不要 ----
 * CRC32 自前・タイムスタンプ固定 (1980-01-01) で**決定的出力**。圧縮しないので zlib 等にも依存しない。 */
/* ★ #3427 ④: 旧・遅延初期化 (static T[256] + init フラグ = ヘッダ内の可変 static。初回書込レース
 * + PE のイメージ跨ぎ複製の型) を **constexpr テーブル**へ。コンパイル時計算 = .rodata の
 * 読み取り専用データになり「書き込みを伴わない表」枠に収まる (gnu++2a)。 */
struct mesh3mfCrc32Table {
	uint32_t T[256];
	constexpr mesh3mfCrc32Table() : T() {
		for ( uint32_t i = 0 ; i < 256 ; ++i ) {
			uint32_t c = i;
			for ( int k = 0 ; k < 8 ; ++k ) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			T[i] = c;
		}
	}
};
inline uint32_t crc32_of(const std::string& s) {
	static constexpr mesh3mfCrc32Table tbl{};
	uint32_t c = 0xFFFFFFFFu;
	for ( std::string::const_iterator it = s.begin() ; it != s.end() ; ++it )
		c = tbl.T[(c ^ (unsigned char)*it) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}
struct ZipEntry { std::string name; std::string data; uint32_t crc; uint32_t off; };
inline void zip_put16(std::string& o, uint32_t v) { o.push_back(char(v & 0xFF)); o.push_back(char((v >> 8) & 0xFF)); }
inline void zip_put32(std::string& o, uint32_t v) {
	o.push_back(char(v & 0xFF));         o.push_back(char((v >> 8) & 0xFF));
	o.push_back(char((v >> 16) & 0xFF)); o.push_back(char((v >> 24) & 0xFF));
}
inline bool write_zip_store(const char *path, std::vector<ZipEntry>& es) {
	std::string out;
	for ( size_t i = 0 ; i < es.size() ; ++i ) {
		ZipEntry& e = es[i];
		e.crc = crc32_of(e.data);
		e.off = (uint32_t)out.size();
		zip_put32(out, 0x04034b50u); zip_put16(out, 20); zip_put16(out, 0); zip_put16(out, 0);   /* sig/ver/flag/method=store */
		zip_put16(out, 0); zip_put16(out, 0x0021);                                               /* time / date=1980-01-01 */
		zip_put32(out, e.crc); zip_put32(out, (uint32_t)e.data.size()); zip_put32(out, (uint32_t)e.data.size());
		zip_put16(out, (uint32_t)e.name.size()); zip_put16(out, 0);
		out += e.name; out += e.data;
	}
	uint32_t cdStart = (uint32_t)out.size();
	for ( size_t i = 0 ; i < es.size() ; ++i ) {
		ZipEntry& e = es[i];
		zip_put32(out, 0x02014b50u); zip_put16(out, 20); zip_put16(out, 20); zip_put16(out, 0); zip_put16(out, 0);
		zip_put16(out, 0); zip_put16(out, 0x0021);
		zip_put32(out, e.crc); zip_put32(out, (uint32_t)e.data.size()); zip_put32(out, (uint32_t)e.data.size());
		zip_put16(out, (uint32_t)e.name.size()); zip_put16(out, 0); zip_put16(out, 0);
		zip_put16(out, 0); zip_put16(out, 0); zip_put32(out, 0); zip_put32(out, e.off);
		out += e.name;
	}
	uint32_t cdSize = (uint32_t)out.size() - cdStart;
	zip_put32(out, 0x06054b50u); zip_put16(out, 0); zip_put16(out, 0);
	zip_put16(out, (uint32_t)es.size()); zip_put16(out, (uint32_t)es.size());
	zip_put32(out, cdSize); zip_put32(out, cdStart); zip_put16(out, 0);
	FILE *f = ::fopen(path, "wb");
	if ( f == 0 ) return false;
	size_t n = ::fwrite(out.data(), 1, out.size(), f);
	::fclose(f);
	return n == out.size();
}

/* ---- 3MF 書き出し (自前 XML + zip・lib3mf 不要・全環境) ----
 * 3MF は OPC コンテナ (zip) に 3 つの XML パートを詰めたもの。AMF 同様に自前で組む。
 * STL/OFF と違い <model unit="..."> で単位を持てる → unit 引数を反映 (既定 mm)。 */
inline bool write_3mf(const char *path, const TriMesh& m, const char *unit)
{
	char buf[192];
	bool hasColor = m.has_color();
	/* 面色 → パレット (一意色)。3MF Materials 拡張の colorgroup で per-triangle 着色。 */
	std::vector<uint32_t> palette;
	std::map<uint32_t, int> palIdx;
	if ( hasColor )
		for ( size_t t = 0 ; t < m.nt() ; ++t ) {
			uint32_t k = m.faceColor[t];
			if ( palIdx.find(k) == palIdx.end() ) { palIdx[k] = (int)palette.size(); palette.push_back(k); }
		}

	std::string model;
	model += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	model += "<model unit=\"";  model += unit_3mf_of(unit);
	model += "\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\"";
	if ( hasColor )
		model += " xmlns:m=\"http://schemas.microsoft.com/3dmanufacturing/material/2015/02\"";
	model += ">\n <resources>\n";
	if ( hasColor ) {
		model += "  <m:colorgroup id=\"2\">\n";
		for ( size_t i = 0 ; i < palette.size() ; ++i ) {
			::snprintf(buf, sizeof buf, "   <m:color color=\"#%02X%02X%02XFF\"/>\n",
			          (palette[i] >> 16) & 0xff, (palette[i] >> 8) & 0xff, palette[i] & 0xff);
			model += buf;
		}
		model += "  </m:colorgroup>\n";
	}
	model += "  <object id=\"1\" type=\"model\">\n   <mesh>\n    <vertices>\n";
	for ( size_t v = 0 ; v < m.nv() ; ++v ) {
		::snprintf(buf, sizeof buf, "     <vertex x=\"%.17g\" y=\"%.17g\" z=\"%.17g\"/>\n",
		          m.verts[3*v], m.verts[3*v+1], m.verts[3*v+2]);
		model += buf;
	}
	model += "    </vertices>\n    <triangles>\n";
	for ( size_t t = 0 ; t < m.nt() ; ++t ) {
		unsigned v1 = (unsigned)m.tris[3*t], v2 = (unsigned)m.tris[3*t+1], v3 = (unsigned)m.tris[3*t+2];
		if ( hasColor ) {
			::snprintf(buf, sizeof buf, "     <triangle v1=\"%u\" v2=\"%u\" v3=\"%u\" pid=\"2\" p1=\"%d\"/>\n",
			          v1, v2, v3, palIdx[m.faceColor[t]]);
		} else {
			::snprintf(buf, sizeof buf, "     <triangle v1=\"%u\" v2=\"%u\" v3=\"%u\"/>\n", v1, v2, v3);
		}
		model += buf;
	}
	model += "    </triangles>\n   </mesh>\n  </object>\n </resources>\n"
	         " <build>\n  <item objectid=\"1\"/>\n </build>\n</model>\n";

	static const char *CONTENT_TYPES =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
	    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
	    "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>"
	    "</Types>";
	static const char *RELS =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
	    "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
	    "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>"
	    "</Relationships>";

	std::vector<ZipEntry> es;
	ZipEntry e0; e0.name = "[Content_Types].xml"; e0.data = CONTENT_TYPES; e0.crc = 0; e0.off = 0;
	ZipEntry e1; e1.name = "_rels/.rels";         e1.data = RELS;          e1.crc = 0; e1.off = 0;
	ZipEntry e2; e2.name = "3D/3dmodel.model";    e2.data = model;         e2.crc = 0; e2.off = 0;
	es.push_back(e0); es.push_back(e1); es.push_back(e2);
	return write_zip_store(path, es);
}

}  /* namespace srava_io */

#endif

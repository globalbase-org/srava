/*
 * cgaImport — 外部メッシュファイルを読み込む計算本体(ptsCalcBody 派生)。args=[path](INLINE)。
 * 形式は **拡張子で自動判別**(CGAL::Polygon_mesh_processing::IO::read_polygon_mesh): OFF/STL/OBJ/PLY。
 * STL は三角形スープ(頂点重複・連結情報なし)なので PMP の read が de-dup/orient/repair を内部で行い
 * 多様体メッシュに変換する。結果は普通のメッシュキャッシュ(box 等と同じ repr_type=1)で、以降は
 * DAG の葉として union 等に使える。
 * キャッシュキーの content-addressing はプランナー側(pigDataFileRef::get_hashkey がファイル内容を
 * ハッシュ)で行うので、ここはパスを受けて読むだけ。読めなければ空メッシュ(nv=0)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"   /* 基底 ptsObject の sPtr<ptsApplication> メンバ用 */
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaImport_.h"

#include	<CGAL/Polygon_mesh_processing/IO/polygon_mesh_io.h>   /* 拡張子判別 + soup repair */
#include	<string>
#include	<vector>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

CLASS_TINYSTATE(cg/c++/cgaImport,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaImport_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;     /* compute() が読んだ 3D/2D mesh(get_writer が writer へ渡す) */
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
class cgMesh;
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


cgaImport_::cgaImport_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* path のファイルを読み、cgMesh に decode。拡張子で OFF/STL/OBJ/PLY 判別、STL は repair される。
 * 読めなければ(不在/未対応形式/パース失敗/空)result に pigDataError を立てる → cgatsAgent が
 * 検出して A_ERROR をプランナーへ返す(従来はサイレント空メッシュ nv=0 だった)。 */
/* SVG の path data(M/L/Z・絶対座標のみ。曲線 C/Q/A は非対応)を 2D 多角形に。我々の出力を round-trip
 * できる最小パーサ。1 つの <path> = 1 領域、その中の M..Z サブパスが 外周(先頭)+ 穴。 */
static bool parse_svg(const char *path, sPtr<cgMesh2D> out)
{
	typedef cgMesh::K K;
	FILE *f = ::fopen(path, "rb");
	if ( f == 0 ) return false;
	std::string s;
	char buf[65536]; size_t n;
	while ( (n = ::fread(buf, 1, sizeof buf, f)) > 0 ) s.append(buf, n);
	::fclose(f);

	bool any = false;
	size_t pos = 0;
	while ( (pos = s.find("d=\"", pos)) != std::string::npos ) {
		pos += 3;
		size_t end = s.find('"', pos);
		if ( end == std::string::npos ) break;
		std::string d = s.substr(pos, end - pos);
		pos = end + 1;

		std::vector<cgMesh2D::Polygon_2> rings;
		cgMesh2D::Polygon_2 cur;
		const char *c = d.c_str();
		while ( *c ) {
			if ( *c == 'M' || *c == 'L' ) {
				char cmd = *c++;
				char *p2;
				double x = ::strtod(c, &p2); c = p2;
				while ( *c == ',' || *c == ' ' || *c == '\t' || *c == '\n' ) c++;
				double y = ::strtod(c, &p2); c = p2;
				if ( cmd == 'M' && cur.size() >= 3 ) { rings.push_back(cur); cur.clear(); }
				else if ( cmd == 'M' ) cur.clear();
				cur.push_back(K::Point_2(K::FT(x), K::FT(y)));
			} else if ( *c == 'Z' || *c == 'z' ) {
				c++;
				if ( cur.size() >= 3 ) { rings.push_back(cur); cur.clear(); }
			} else {
				c++;
			}
		}
		if ( cur.size() >= 3 ) rings.push_back(cur);
		if ( rings.empty() ) continue;

		cgMesh2D::Polygon_2 outer = rings[0];
		if ( outer.is_simple() && outer.is_clockwise_oriented() ) outer.reverse_orientation();
		std::vector<cgMesh2D::Polygon_2> holes;
		for ( std::size_t i = 1 ; i < rings.size() ; ++i ) {
			cgMesh2D::Polygon_2 hr = rings[i];
			if ( hr.is_simple() && hr.is_counterclockwise_oriented() ) hr.reverse_orientation();
			holes.push_back(hr);
		}
		out->regions().push_back(cgMesh2D::Pwh_2(outer, holes.begin(), holes.end()));
		any = true;
	}
	return any;
}

static const char* import_ext(const char* path) {
	const char* dot = ::strrchr(path, '.');
	return dot ? dot + 1 : "";
}

/* DXF(ASCII)の LWPOLYLINE を 2D 多角形に。group code/value のペア列を読み、code 10/20 で頂点 x/y。
 * 我々の DXF 出力 + 単純な DXF を round-trip。閉ポリライン群を包含関係で外周/穴に nest(偶数=外周/奇数=穴)。
 * 曲線/円弧(bulge)・他エンティティは無視。 */
static bool parse_dxf(const char *path, sPtr<cgMesh2D> out)
{
	typedef cgMesh::K K;
	typedef cgMesh2D::Polygon_2 Poly2;
	FILE *f = ::fopen(path, "rb");
	if ( f == 0 ) return false;
	std::string s;
	char rbuf[65536]; size_t rn;
	while ( (rn = ::fread(rbuf, 1, sizeof rbuf, f)) > 0 ) s.append(rbuf, rn);
	::fclose(f);

	/* 行をトリムして取り出すヘルパ(行頭末の空白を除く)。 */
	std::vector<std::string> lines;
	{
		std::size_t i = 0;
		while ( i < s.size() ) {
			std::size_t j = s.find('\n', i);
			if ( j == std::string::npos ) j = s.size();
			std::string ln = s.substr(i, j - i);
			while ( ! ln.empty() && (ln[ln.size()-1]=='\r' || ln[ln.size()-1]==' ' || ln[ln.size()-1]=='\t') )
				ln.erase(ln.size()-1);
			std::size_t b = 0; while ( b < ln.size() && (ln[b]==' '||ln[b]=='\t') ) b++;
			lines.push_back(ln.substr(b));
			i = j + 1;
		}
	}

	std::vector<Poly2> polys;
	bool inPoly = false;
	Poly2 cur;
	int pendingCode = -99999;
	double px = 0; bool haveX = false;
	for ( std::size_t li = 0 ; li + 1 < lines.size() ; li += 2 ) {
		int code = ::atoi(lines[li].c_str());
		const std::string& val = lines[li+1];
		if ( code == 0 ) {                       /* エンティティ境界 */
			if ( inPoly && cur.size() >= 3 ) polys.push_back(cur);
			cur.clear(); haveX = false;
			inPoly = ( val == "LWPOLYLINE" || val == "POLYLINE" );
		} else if ( inPoly && code == 10 ) {     /* 頂点 x */
			px = ::strtod(val.c_str(), 0); haveX = true;
		} else if ( inPoly && code == 20 ) {     /* 頂点 y(直前の x と対) */
			double py = ::strtod(val.c_str(), 0);
			if ( haveX ) { cur.push_back(K::Point_2(K::FT(px), K::FT(py))); haveX = false; }
		}
		(void)pendingCode;
	}
	if ( inPoly && cur.size() >= 3 ) polys.push_back(cur);
	if ( polys.empty() ) return false;

	/* 包含で nest: 各多角形の「内側にある他多角形」の数 = depth。偶数=外周、奇数=直近外周の穴。 */
	int n = (int)polys.size();
	std::vector<int> depth(n, 0);
	for ( int i = 0 ; i < n ; ++i ) {
		if ( ! polys[i].is_simple() ) continue;
		K::Point_2 pt = *polys[i].vertices_begin();
		for ( int j = 0 ; j < n ; ++j ) {
			if ( j == i || ! polys[j].is_simple() ) continue;
			if ( polys[j].bounded_side(pt) == CGAL::ON_BOUNDED_SIDE )
				depth[i]++;
		}
	}
	for ( int i = 0 ; i < n ; ++i ) {
		if ( ! polys[i].is_simple() ) continue;
		if ( depth[i] % 2 != 0 ) continue;       /* 穴は外周側で拾う */
		Poly2 outer = polys[i];
		if ( outer.is_clockwise_oriented() ) outer.reverse_orientation();
		std::vector<Poly2> holes;
		for ( int k = 0 ; k < n ; ++k ) {        /* depth = i.depth+1 で i に含まれる = 直近の穴 */
			if ( k == i || ! polys[k].is_simple() || depth[k] != depth[i] + 1 ) continue;
			K::Point_2 kp = *polys[k].vertices_begin();
			if ( polys[i].bounded_side(kp) != CGAL::ON_BOUNDED_SIDE ) continue;
			Poly2 hr = polys[k];
			if ( hr.is_counterclockwise_oriented() ) hr.reverse_orientation();
			holes.push_back(hr);
		}
		out->regions().push_back(cgMesh2D::Pwh_2(outer, holes.begin(), holes.end()));
	}
	return ! out->regions().empty();
}

void
cgaImport_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<stdString> path = ( na > 0 ) ? (*args)[0]->get_str()
	                                  : sPtr<stdString>(thNEW(stdString,("")));
	const char *p = path->get_str();
	const char *e = import_ext(p);

	if ( ::strcasecmp(e, "svg") == 0 ) {                 /* 2D: SVG → cgMesh2D */
		sPtr<cgMesh2D> m2 = thNEW(cgMesh2D,());
		if ( ! parse_svg(p, m2) ) {
			sPtr<stdString> msg = thNEW(stdString,("import: failed to read SVG "));
			result = thNEW(pigDataError,(msg->add(path)));
		}
		mesh = m2;
		return;
	}
	if ( ::strcasecmp(e, "dxf") == 0 ) {                 /* 2D: DXF(LWPOLYLINE)→ cgMesh2D */
		sPtr<cgMesh2D> m2 = thNEW(cgMesh2D,());
		if ( ! parse_dxf(p, m2) ) {
			sPtr<stdString> msg = thNEW(stdString,("import: failed to read DXF "));
			result = thNEW(pigDataError,(msg->add(path)));
		}
		mesh = m2;
		return;
	}

	/* 3D: OFF/STL/OBJ/PLY → cgMesh3D(従来) */
	sPtr<cgMesh3D> m3 = thNEW(cgMesh3D,());
	bool ok = CGAL::Polygon_mesh_processing::IO::read_polygon_mesh(std::string(p), m3->mesh());
	if ( ! ok || m3->mesh().number_of_vertices() == 0 ) {
		sPtr<stdString> msg = thNEW(stdString,("import: failed to read "));
		result = thNEW(pigDataError,(msg->add(path)));
	}
	mesh = m3;
}

/* mesh 出力: box 等と同じく WriterMesh で cgMesh を D_CHUNK にストリーム。 */
sPtr<ptsWireCacheStreamWriter>
cgaImport_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}

/*
 * cgaExport — mesh をファイルへ書き出す計算本体(ptsCalcBody 派生)。args=[path, mesh]。
 *   path = INLINE 文字列、mesh = CACHE(ReaderMesh が cgMesh に decode)。
 * 出力形式は **拡張子で自動振り分け**(CGAL::IO::write_polygon_mesh): .off/.stl/.obj/.ply/.ts 等。
 * compute() で書き出し後、書いた **ファイルの中身を舐めて content_hash(FNV-1a/64)**、
 * size/mtime を取り、**結果 pigData = D_REF OUTPUT 表現**(pigDataRef.h)を返す。
 * 出力キャッシュは mesh バイナリではなく D_REF レコード(catalog §7。mesh 二重保存を避ける)で、
 * 実際の書き込みは agent の set_body → ptsDataCache → codec が選ぶ WriterRef が行う
 * (#3406, 2026-07-31 メモ 1./2.: calc は結果を返すだけ・writer を自前で起こさない)。
 * content_hash が権威キー(mtime 偽装等の false-hit 回避)、(path,size,mtime)は安いゲート。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"pig/c++/pigDataRef.h"   /* 結果 = D_REF の pigData 表現 (#3406, 2026-07-31 メモ 2.) */
#include	"ts2/c++/stdString.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/cgaExport_.h"

#include	<CGAL/boost/graph/IO/polygon_mesh_io.h>   /* 拡張子で OFF/STL/OBJ/PLY 振り分け */
#include	<string>
#include	<string.h>     /* strrchr */
#include	<strings.h>    /* strcasecmp */
#include	<stdio.h>
#include	<stdint.h>
#include	<sys/stat.h>

CLASS_TINYSTATE(cg/c++/cgaExport,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaExport_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

protected:
	virtual void	compute();
	sPtr<stdString>	refPath;    /* 書いた OFF のパス */
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"pig/c++/pigData.h"
class ptsObject;
class pigData;
class stdString;
class cgMesh;
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


cgaExport_::cgaExport_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* write_to が false を返したときの診断メッセージを組む。
 * 多くの失敗は「次元と出力形式の不一致」(2D 形状を STL に・3D 形状を SVG に等)なので、
 * 拡張子と mesh の dim() を突き合わせ、何を使えばよいかまで具体的に示す。
 * 純粋な IO 失敗(権限/パス不正)なら原因不明なので素の "cannot write" に落とす。 */
static sPtr<stdString> export_write_error_msg(const char *path, int dim)
{
	const char *dot = ::strrchr(path, '.');
	const char *e = dot ? dot + 1 : "";
	int is3dFmt = ( ::strcasecmp(e,"off")==0 || ::strcasecmp(e,"stl")==0
	             || ::strcasecmp(e,"obj")==0 || ::strcasecmp(e,"ply")==0
	             || ::strcasecmp(e,"amf")==0 || ::strcasecmp(e,"3mf")==0 );
	int is2dFmt = ( ::strcasecmp(e,"svg")==0 || ::strcasecmp(e,"dxf")==0 );
	char buf[512];
	if ( dim == 2 && is3dFmt )
		::snprintf(buf, sizeof buf,
			"export: '%s' is a 3D-only format but this shape is 2D - "
			"write SVG/DXF instead, or make it 3D with extrude/revolve before writing STL/OFF/OBJ/PLY (%s)",
			e, path);
	else if ( dim == 3 && is2dFmt )
		::snprintf(buf, sizeof buf,
			"export: '%s' is a 2D-only format but this shape is 3D - "
			"write STL/OFF/OBJ/PLY instead, or take a cross-section with section before writing SVG/DXF (%s)",
			e, path);
	else if ( ! is3dFmt && ! is2dFmt )
		::snprintf(buf, sizeof buf,
			"export: unknown extension '%s'. supported: 3D=OFF/STL/OBJ/PLY/AMF/3MF, 2D=SVG/DXF (%s)",
			e, path);
	else   /* 形式と次元は合っているのに失敗 → IO(権限/パス)系。原因は特定できない */
		::snprintf(buf, sizeof buf, "export: cannot write %s", path);
	return thNEW(stdString,(buf));
}

/* out.off の中身を舐めて FNV-1a/64 content_hash を計算。 */
static pHashKeyType hash_file(const char *path)
{
	uint64_t h = 1469598103934665603ULL;
	const uint64_t prime = 1099511628211ULL;
	FILE *f = ::fopen(path, "rb");
	if ( f == 0 )
		return (pHashKeyType)0;
	uint8_t buf[65536];
	size_t n;
	while ( (n = ::fread(buf, 1, sizeof buf, f)) > 0 )
		for ( size_t i = 0 ; i < n ; ++i ) { h ^= buf[i]; h *= prime; }
	::fclose(f);
	return (pHashKeyType)h;
}

void
cgaExport_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	refPath = ( na > 0 ) ? (*args)[0]->get_str()
	                     : sPtr<stdString>(thNEW(stdString,("/tmp/srava-out.off")));
	/* 多態: 3D=OFF/STL/.. / 2D=SVG/DXF を mesh 自身が拡張子で書き分ける。 */
	sPtr<cgMesh> mIn = ( na > 1 ) ? sPtr<cgMesh>::d_cast((*args)[1]) : sPtr<cgMesh>();
	const char *p = refPath->get_str();
	if ( ! mIn.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("export: no mesh to write"))));
		return;
	}
	/* 単位(任意の 3 番目の引数)。SVG=width/height、DXF=$INSUNITS に反映。空 / 非対応形式は無視。 */
	sPtr<stdString> unitS = ( na > 2 ) ? (*args)[2]->get_str()
	                                   : sPtr<stdString>(thNEW(stdString,("")));
	if ( ! mIn->write_to(p, unitS->get_str()) ) {
		result = thNEW(pigDataError,(export_write_error_msg(p, mIn->dim())));
		return;
	}

	/* 書いた中身から D_REF の権威キー(content_hash)+ 安いゲート(size/mtime)を取る。 */
	pHashKeyType refHash = hash_file(p);
	INTEGER64 refSize = 0, refMtime = 0;
	struct stat st;
	if ( ::stat(p, &st) == 0 ) {
		refSize  = (INTEGER64)st.st_size;
		refMtime = (INTEGER64)st.st_mtime;
	}
	/* ★ 結果 = D_REF の pigData 表現(#3406, 2026-07-31 メモ 1./2.)。
	 * 他の演算子と同じく「結果 pigData を返すだけ」。キャッシュへの書き込みは agent の
	 * set_body → ptsDataCache → pigCacheCodec が WriterRef を選ぶ、という一本道で行われる
	 * (旧: calc が ACT_FINISH で自前の WriterRef を起こしていた escape hatch は撤去)。 */
	result = pig_data_ref_make(PIG_DREF_OUTPUT, refPath, refSize, refMtime, refHash);
}

/*
 * ocaExport — export(path, shape, unit) の計算本体 (#3437)。nfaExport のミラー。
 * ★ **STEP (.step/.stp) と OCCT の .brep** を書く。三角形へは落とさない。
 *   B-rep のまま他の CAD へ渡せる出口があることが、この型の実用上の価値。
 *   三角形が要るときは triangulate(oc, deflection) を挟んでメッシュ系で書く (明示 op)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"pig/c++/pigDataRef.h"   /* 結果 = D_REF の pigData 表現 */
#include	"ts2/c++/stdString.h"
#include	"ts2/c++/stdEvent.h"
#include	<stdio.h>
#include	<stdint.h>
#include	<sys/stat.h>

/* 書いた中身を舐めて FNV-1a/64 content_hash を計算 (cgaExport / mfaExport と同一)。 */
static pHashKeyType oc_hash_file(const char *path)
{
	uint64_t h = 1469598103934665603ULL;
	const uint64_t prime = 1099511628211ULL;
	FILE *f = ::fopen(path, "rb");
	if ( f == 0 ) return (pHashKeyType)0;
	uint8_t buf[65536];
	size_t n;
	while ( (n = ::fread(buf, 1, sizeof buf, f)) > 0 )
		for ( size_t i = 0 ; i < n ; ++i ) { h ^= buf[i]; h *= prime; }
	::fclose(f);
	return (pHashKeyType)h;
}
#include	"_ts2/c++/ocaExport_.h"

CLASS_TINYSTATE(oc/c++/ocaExport,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaExport_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

protected:
	virtual void	compute();
	sPtr<stdString>	refPath;
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
class ocShape;
TS_END_INTERFACE

#endif


ocaExport_::ocaExport_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaExport_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	refPath = ( na > 0 ) ? (*args)[0]->get_str()
	                     : sPtr<stdString>(thNEW(stdString,("/tmp/srava-out.step")));
	/* ★★ #3544 段 3: **2D も受ける**。
	 *   ⚠ ここは sig ではなく **d_cast が効くガード**なので、sig に型を足すだけでは通らない
	 *     (pigfModuleAgent の export の近道)。⇒ 受ける型ごとに d_cast を書く。
	 *   ⚠ ocFace2D は ocShape の派生では **ない** (どちらも ocGeom 派生) ので、
	 *     3D 用の d_cast は 2D に当たらない。2026-09-17 まで 2D は **1 形式も書けなかった**。 */
	const char *p = refPath->get_str();
	sPtr<ocShape>  mIn = ( na > 1 ) ? sPtr<ocShape>::d_cast((*args)[1])  : sPtr<ocShape>();
	sPtr<ocFace2D> f2  = ( na > 1 ) ? sPtr<ocFace2D>::d_cast((*args)[1]) : sPtr<ocFace2D>();
	if ( ! mIn.is_notNull() && ! f2.is_notNull() ) {
		result = oca_err(thNEW(stdString,("export: no shape to write")));
		return;
	}
	sPtr<stdString> unitS = ( na > 2 ) ? (*args)[2]->get_str()
	                                   : sPtr<stdString>(thNEW(stdString,("")));
	/* ★ 2D は中断の口を持たない (DXF / SVG / BinTools に進捗が無い) ので brk を渡さない。
	 *   ⚠ 代わりに **理由**を受け取る — 「z=0 に居ないから断った」が呼び手に届かないと、
	 *     利用者には「知らない拡張子」と区別がつかない。 */
	char web[320]; web[0] = '\0';
	const bool okWrite = mIn.is_notNull() ? mIn->write_to(p, unitS->get_str(), &brk_)
	                                      : f2->write_to(p, unitS->get_str(), web, (int)sizeof web);
	if ( ! okWrite ) {   /* ★ #3503 続き */
		/* ★ 中断も「書けなかった」として返ってくるので、先に旗を見る。 */
		if ( (result = oc_abort_err(brk_, "export")) != thNULL ) return;
		/* ★ #3544 段 3: 幾何側の理由を丸ごと載せるので 256 では切れる。 */
		char b[512];
		/* ★ #3544 段 3: 2D は dxf / svg も書ける。3D は図面ではないので出ない。
		 *   ★ 幾何側が理由を残していれば **それを言う** (形式の一覧より役に立つ)。 */
		if ( web[0] != '\0' ) {
			::snprintf(b, sizeof b, "export: cannot write %s — %s", p, web);
			result = oca_err(thNEW(stdString,(b)));
			return;
		}
#ifdef SRAVA_OCCT_STEP
		::snprintf(b, sizeof b, "export: cannot write %s (occt writes step/stp/brep%s)", p,
		           f2.is_notNull() ? "/dxf/svg for a 2D drawing" : "");
#else
		::snprintf(b, sizeof b, "export: cannot write %s (this occt build writes brep%s)", p,
		           f2.is_notNull() ? "/dxf/svg for a 2D drawing" : "");
#endif
		result = oca_err(thNEW(stdString,(b)));
		return;
	}
	pHashKeyType refHash = oc_hash_file(p);
	INTEGER64 refSize = 0, refMtime = 0;
	struct stat st;
	if ( ::stat(p, &st) == 0 ) {
		refSize  = (INTEGER64)st.st_size;
		refMtime = (INTEGER64)st.st_mtime;
	}
	result = pig_data_ref_make(PIG_DREF_OUTPUT, refPath, refSize, refMtime, refHash);
}

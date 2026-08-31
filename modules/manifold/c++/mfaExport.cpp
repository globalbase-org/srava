/*
 * mfaExport — mesh をファイルへ書き出す計算本体(mf 版・cgaExport のミラー)。args=[path, mesh, unit]。
 * mfMesh::write_to が拡張子で STL/OFF を書き分ける。結果 pigData は D_REF OUTPUT 表現
 * (path+size+mtime+content_hash)で、書き込みは agent の set_body → ptsDataCache → WriterRef
 * (pig 層・カーネル非依存。#3406, 2026-07-31 メモ 1./2.)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"pig/c++/pigDataRef.h"   /* 結果 = D_REF の pigData 表現 */
#include	"ts2/c++/stdString.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/mfaExport_.h"

#include	<stdio.h>
#include	<stdint.h>
#include	<sys/stat.h>

CLASS_TINYSTATE(mf/c++/mfaExport,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaExport_(
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
#include	"pig/c++/pigData.h"
class ptsObject;
class pigData;
class stdString;
class mfMesh;
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


mfaExport_::mfaExport_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 書いた中身を舐めて FNV-1a/64 content_hash を計算(cgaExport と同一)。 */
static pHashKeyType hash_file(const char *path)
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

void
mfaExport_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	refPath = ( na > 0 ) ? (*args)[0]->get_str()
	                     : sPtr<stdString>(thNEW(stdString,("/tmp/srava-out.stl")));
	sPtr<mfMesh> mIn = ( na > 1 ) ? sPtr<mfMesh>::d_cast((*args)[1]) : sPtr<mfMesh>();
	const char *p = refPath->get_str();
	if ( ! mIn.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("export: no mesh to write"))));
		return;
	}
	sPtr<stdString> unitS = ( na > 2 ) ? (*args)[2]->get_str()
	                                   : sPtr<stdString>(thNEW(stdString,("")));
	if ( ! mIn->write_to(p, unitS->get_str()) ) {
		char b[256];
		::snprintf(b, sizeof b, "export: cannot write %s (mf supports STL/OFF)", p);
		result = thNEW(pigDataError,(thNEW(stdString,(b))));
		return;
	}
	pHashKeyType refHash = hash_file(p);
	INTEGER64 refSize = 0, refMtime = 0;
	struct stat st;
	if ( ::stat(p, &st) == 0 ) {
		refSize  = (INTEGER64)st.st_size;
		refMtime = (INTEGER64)st.st_mtime;
	}
	/* 結果 = D_REF OUTPUT の pigData 表現。cgaExport のミラー(#3406, 2026-07-31 メモ 1./2.)。
	 * 書き込みは agent の set_body → ptsDataCache → codec が選ぶ WriterRef。 */
	result = pig_data_ref_make(PIG_DREF_OUTPUT, refPath, refSize, refMtime, refHash);
}

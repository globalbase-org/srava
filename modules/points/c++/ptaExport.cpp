/*
 * ptaExport — export("out.xyz", p [, unit]) の計算本体 (#3528)。結果は D_REF。
 *
 * ★ 2D / 3D の**どちらも受ける** (sig が "(pt-cloud3d)->ref;(pt-cloud2d)->ref")。
 *   ⚠⚠ 2D を書くと **往復で型が変わる** (pt-cloud2d → pt-cloud3d) — .xyz に「2D である」
 *     ことを書く場所が無いため。⇒ ひさ判断 (#3582 ・ 2026-09-22) で **受け入れる**ことにした。
 *     黙って起きるわけではなく、文書に書いてある。
 *   ★ 列数と 2D+法線の扱い (6 列 ・ z=0 / nz=0) は ptCloud::write_to の冒頭に書いた。
 * ★ 法線があれば **6 列**で出る。往復で法線と印が保たれる (#3528 の検証②)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"pig/c++/pigDataRef.h"   /* 結果 = D_REF の pigData 表現 */
#include	"ts2/c++/stdEvent.h"
#include	<stdio.h>
#include	<stdint.h>
#include	<sys/stat.h>
#include	"_ts2/c++/ptaExport_.h"

CLASS_TINYSTATE(pt/c++/ptaExport,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaExport_(
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
class ptCloud;
TS_END_INTERFACE

#endif


ptaExport_::ptaExport_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 書いた中身を舐めて FNV-1a/64 content_hash を計算 (cgaExport / ggaExport と同一)。 */
static pHashKeyType pt_hash_file(const char *path)
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
ptaExport_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	refPath = ( na > 0 ) ? (*args)[0]->get_str()
	                     : sPtr<stdString>(thNEW(stdString,("/tmp/srava-out.xyz")));
	sPtr<ptCloud> in = ( na > 1 ) ? sPtr<ptCloud>::d_cast((*args)[1]) : sPtr<ptCloud>();
	const char *p = refPath->get_str();
	if ( ! in.is_notNull() ) {
		result = pta_err(thNEW(stdString,("export: no point cloud to write")));
		return;
	}
	/* ★ #3582: 2D の拒否は **外した** (ひさ判断 2026-09-22)。dim の場合分けは write_to が持つ。
	 *   ⚠ ここに在った「2D は往復で型が変わるから受けない」という枝と、その根拠として
	 *     書いてあった「export の routing ① は sig を見ない」という説明は **どちらも古い** —
	 *     規約① は #3554 最後の段 4/5 で撤去され、いまは sig が素直に効く。 */
	sPtr<stdString> unitS = ( na > 2 ) ? (*args)[2]->get_str()
	                                   : sPtr<stdString>(thNEW(stdString,("")));
	if ( ! in->write_to(p, unitS->get_str()) ) {
		char b[256];
		::snprintf(b, sizeof b, "export: cannot write %s (points supports xyz)", p);
		result = pta_err(thNEW(stdString,(b)));
		return;
	}
	pHashKeyType refHash = pt_hash_file(p);
	INTEGER64 refSize = 0, refMtime = 0;
	struct stat st;
	if ( ::stat(p, &st) == 0 ) {
		refSize  = (INTEGER64)st.st_size;
		refMtime = (INTEGER64)st.st_mtime;
	}
	result = pig_data_ref_make(PIG_DREF_OUTPUT, refPath, refSize, refMtime, refHash);
}

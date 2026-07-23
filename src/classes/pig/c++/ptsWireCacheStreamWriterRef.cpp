/*
 * ptsWireCacheStreamWriterRef — 結果出力 D_REF キャッシュ用の writer 派生(export 等)。
 * mesh バイナリは持たず、INIT gate で D_REF OUTPUT レコード(path + size + mtime + content_hash)を
 * 1 つ書くだけ。基底が streamhdr / TSE_ASSERT / W_END 番兵 / TSE_RETURN を担う。
 * これにより export のキャッシュは「どのファイルへ何の中身(content_hash)を書いたか」の参照記録になり、
 * mesh の二重保存を避ける(catalog §7 OUTPUT)。
 */
#include	"pig/c++/ptsObject.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptsWireCacheStreamWriterRef_.h"

CLASS_TINYSTATE(pig/c++/ptsWireCacheStreamWriterRef,pig/c++/ptsWireCacheStreamWriter)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsWireCacheStreamWriterRef_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName,
		sPtr<stdString> _refPath,
		INTEGER64 _size,
		INTEGER64 _mtime,
		pHashKeyType _chash);

	sRptr<ptsObject,tinyState>		parent;
protected:
	sPtr<stdString>	refPath;
	INTEGER64	refSize;
	INTEGER64	refMtime;
	pHashKeyType	refHash;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"pig/c++/pigData.h"   /* pHashKeyType */
class ptsObject;
class stdString;
TS_END_INTERFACE

#endif


ptsWireCacheStreamWriterRef_::ptsWireCacheStreamWriterRef_(TS_ARGS0)
        : ptsWireCacheStreamWriter_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    refPath  = _refPath;
    refSize  = _size;
    refMtime = _mtime;
    refHash  = _chash;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* D_REF OUTPUT を 1 レコード書く(本体なし) */
{
	d_ref_output(refPath, refSize, refMtime, refHash);
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}

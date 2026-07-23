/*
 * ptsCalcBody — エージェント計算本体の基底(ptsObject 派生・piggybackTurtle 汎用)。
 * cgatsAgent が dispatch して mkCalc で生成する。解決済み入力(args)と目標キャッシュパス(target)を
 * 受け取り、ACT_START(TS_THREAD)で compute() を回し、FIN で parent(cgatsAgent)へ TSE_RETURN を送る。
 * 結果は get_result()(テキスト出力時 = 中身 pigData)で引く。各演算が compute() を override する。
 * 出力シリアライズ(確認事項1): テキスト出力は result(pigData)を返し cgatsAgent が WriterText で保存。
 *   バイナリ/mesh 出力は派生 compute() 内で Writer を parent=cgatsAgent として生成し、別途引き渡す
 *   (Stage2)。本骨格はテキスト経路。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsWireCacheStreamWriter.h"   /* get_writer() 戻り値 sPtr の完全型(thNULL 構築) */
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/ptsCalcBody_.h"

CLASS_TINYSTATE(pig/c++/ptsCalcBody,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsCalcBody_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *args,
		sPtr<stdString> target);

	sRptr<ptsObject,tinyState>		parent;

	sPtr<pigData>	get_result();
	/* バイナリ/mesh 出力: 計算本体が parent=cgatsAgent で生成した Writer を引き渡す(確認①)。
	 * 基底は thNULL(= テキスト経路。cgatsAgent が get_result() を WriterText で保存)。
	 * mesh 出力する派生(cgaBox/cgaUnion)が override して Writer(...WriterMesh)を返す。 */
	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
protected:
	sPtr<pigData>		result;
	virtual void	compute();
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
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


ptsCalcBody_::ptsCalcBody_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

sPtr<pigData>
ptsCalcBody_::get_result()
{
	return result;
}

/* 基底はテキスト経路(Writer なし)。mesh 出力する派生が override。 */
sPtr<ptsWireCacheStreamWriter>
ptsCalcBody_::get_writer()
{
	return thNULL;
}

/* 基底は no-op(派生 cgaBox 等が override)。 */
void
ptsCalcBody_::compute()
{
}


/*******************************************
	STATE MACHINE
********************************************/

TS_THREAD(ACT_START)   /* 重い計算は専用スレッドで(基底 ptsObject の idle ACT_START を上書き) */
{
	compute();
	return rDO|FIN_START;
}
TS_STATE(FIN_START)
{
	/* 完了を parent(cgatsAgent)へ通知。結果は get_result() で引く。 */
	parent->eventHandler(thNEW(stdEvent,(TSE_RETURN, ifThis, (INTEGER64)0)));
	return rDO|FIN_ptsObject_START;
}

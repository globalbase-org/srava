/*
 * ptsCalcBody — エージェント計算本体の基底(ptsObject 派生・piggybackTurtle 汎用)。
 * cgatsAgent が dispatch して mkCalc で生成する。解決済み入力(args)と目標キャッシュパス(target)を
 * 受け取り、ACT_START(TS_THREAD)で compute() を回し、ACT_FINISH(後処理フック)を経て FIN で
 * parent(cgatsAgent)へ TSE_RETURN を送る。結果もエラーも **get_result() 1 本**で引く
 * (#3406, 2026-07-30 メモ: 旧 get_result()/get_body() の 2 メソッド分割は冗長だった —
 * 値返し op は元々どちらも result を返すだけで完全に同一、mesh op は「エラー時は get_result()
 * だけ非 NULL・成功時は get_body() だけ非 NULL」という**排他的**な関係だったので、1 本にして
 * 呼び出し側が優先順位 (エラーがあればエラー、無ければ本文) を持つだけで済む)。
 * 基底は result をそのまま返す(値返し op はこれで足りる)。mesh 出力する派生(cgaBox 等)は
 * override して「result があれば result(エラー)、無ければ mesh/cross/geom(本文)」を返す。
 *
 * ★ ACT_FINISH (#3406, 2026-07-30 メモ): compute() 完了後・FIN の前に挟む後処理フック。基底は
 *   素通り (rDO|FIN_START)。
 *   経緯: 参照レコード系 (export/voxelize) の D_REF 保存をどこでやるかで 2 転している。
 *     (a) 旧: get_writer() で agent (cgatsAgent) 側へ Writer を渡す escape hatch →
 *         「agent が欲しいのは結果であって Writer ではない」と却下 (ひさ 2026-07-30)。get_writer() 削除。
 *     (b) 2026-07-30: export/voxelize が **この ACT_FINISH で自前 Writer を起こす**形にした。
 *     (c) 2026-07-31 (現行・ひさ再決定): D_REF も pigData で表せるようにした (pigDataRef.h) ため、
 *         export/voxelize は**他の演算子と同じく結果 pigData を返すだけ**になり、書き込みは
 *         agent の set_body → ptsDataCache → codec (WriterRef) の一本道に載った。
 *   ⇒ **現時点で ACT_FINISH を override している派生は無い**。「compute() 後・FIN 前」に何かを
 *      挟みたくなった時のために将来用として温存する (ひさ判断 2026-07-31)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/ptsCalcBody_.h"

#include	<stdexcept>
#include	<stdio.h>

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

	/* 結果 (#3406, 2026-07-30 メモ: get_body 統合)。基底=result(値返し op)。mesh 出力する派生は
	 * override して「result があればそれ(エラー)、無ければ mesh/cross/geom(本文)」を返す。
	 * 保存(Writer 起動)は agent が出力 pigDataCache の set_body 経由で行う。 */
	virtual sPtr<pigData>	get_result();
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
TS_END_INTERFACE

#endif


ptsCalcBody_::ptsCalcBody_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

/* 基底は result をそのまま返す(値返し op はこれで足りる)。mesh op は override。 */
sPtr<pigData>
ptsCalcBody_::get_result()
{
	return result;
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
	/* ★★ モジュール非依存の安全網 (ひさ判断 2026-08-26)。ここは **全モジュールの compute() が
	 * 通る唯一の入口**なので、モジュールが取りこぼした例外はここで受ける。
	 *
	 * ★なぜ要るか — **in-proc (EXEC_THREAD) では planner 自身が死ぬ**から:
	 *   process 実行なら agent プロセスが terminate → SIGABRT で死ぬだけで、planner は
	 *   生き残って理由を報告できる (agent の stderr を ptsErrSink が拾う)。ところが in-proc は
	 *   同一プロセスの専用スレッドなので、抜けた例外は **planner ごと terminate** させる。
	 *   manifold は既定が in-proc、openvdb 系も切替可 — そしてこの 2 者は catch を持たない。
	 *   ⇒ 「まだ検証していないモジュールが何かを投げても、エラーとして分かり、planner は死なない」
	 *
	 * ⚠ ここで出せるのは **一般的な文言**まで。良いメッセージはモジュール側の catch が出す
	 *   (cgal / nef / geogram / occt はそれぞれ持っている)。ここは最後の砦。
	 * ⚠ OCCT の Standard_Failure は std::exception 派生ではないので下の catch(...) に落ちる
	 *   = 型名も出せない。occt は自前の catch で理由を出している。
	 * ⚠ 捕まえるのは例外だけ。SIGSEGV 等はここでは受けない (受けてはいけない)。 */
	try {
		compute();
	} catch ( const std::exception &e ) {
		char b[600];
		::snprintf(b, sizeof b, "module threw an uncaught exception: %s", e.what());
		result = thNEW(pigDataError,(thNEW(stdString,(b))));
	} catch ( ... ) {
		result = thNEW(pigDataError,(thNEW(stdString,
		    ("module threw an uncaught exception (not a std::exception)"))));
	}
	return rDO|ACT_FINISH;
}
/* 後処理フック(#3406)。基底=素通り。現在 override している派生は無い(将来用に温存。
 * 経緯はファイル冒頭の ★ACT_FINISH を参照)。 */
TS_STATE(ACT_FINISH)
{
	return rDO|FIN_START;
}
TS_STATE(FIN_START)
{
	/* 完了を parent(cgatsAgent)へ通知。結果は get_result() で引く。
	 * ★ §9 の例外: result はここで thNULL にしない — **pull 型** (親が TSE_RETURN を受けてから
	 * get_result() で読む) なので FIN 後もメンバが読まれる。親 (cgats/mfatsAgent) が読み終えた後
	 * calc = thNULL で参照ごと手放す約束 (agents の FIN_START)。 */
	parent->eventHandler(thNEW(stdEvent,(TSE_RETURN, ifThis, (INTEGER64)0)));
	return rDO|FIN_ptsObject_START;
}

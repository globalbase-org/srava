/*
 * ptsFireAndForget — 「起動して、待たずに置いていく」ための薄い状態機械。
 * ⚠ `_front` を持たない (pigData ノードに紐づく helper ではない) ので `pigf*` ではなく `pts*`。
 * ★ 基底に pigfFunction を使うのは **env を持つため**だけ (ptsObject の get_env は null)。
 *
 * ★ #3419 (ひさ設計 2026-08-24): srava の pigData は
 *     「参照したら解決値が返る。無理なら sException。明示解決は compact()」
 *   という 1 本の契約でできている。ところが `trigger()` (= 起動だけ蹴る) はこの契約の外にあり、
 *   **意味論が宙に浮いていた**。実際、起動の入口が `trigger()` と `_start()` の 2 つに割れて
 *   二重管理になり、片方に入れ忘れた op (Math) が最悪の症状を残した。
 *
 * ⇒ **「待たない」は呼び手の都合であって、ノードの性質ではない。**
 *   待たずに済ませたい側が **この helper を 1 個生やして、その中で普通に compact() する**。
 *   呼び手は helper を作るだけで先へ進み、待つのはこの helper が引き受けて、終わったら死ぬ。
 *
 *   ACT_START : _node->compact()  (未解決なら yield → 再走。値は捨てる)
 *   FIN       : そのまま終了
 *
 * ⚠ env は **実態親から継承**する。`_front` を持たないので基底 pigfFunction の INI は
 *   ルート env を作ってしまう。INI_pigfFunction_START で親の env に差し替える
 *   (`gate(x, print(v))` の `v` のように、中身が呼び手の変数を参照するため)。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型 */
#include	"pig/c++/pigData.h"
#include	"_ts2/c++/ptsFireAndForget_.h"

CLASS_TINYSTATE(pig/c++/ptsFireAndForget,pig/c++/pigfFunction)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	/* @param _node        起動して待つ対象。**値**は使わない
	 * @param _reportError  値が **エラー**だったときの扱い (2026-08-26・ひさ指摘):
	 *                      1 = ptsApplication へ報告して全体を終了させる (gate の側効果など、
	 *                          **他に誰も結果を見ない**呼び手はこちら)
	 *                      0 = 何もしない (async のように **呼び手が別途 drain して報告する**場合)
	 * ★ 既定を 1 にしないのは、意味が呼び手ごとに違うため。**呼び手に明示させる**。 */
	ptsFireAndForget_(
		sPtr<ptsObject> parent,
		sPtr<pigData> _node,
		int _reportError);

	sRptr<ptsObject,tinyState>		parent;
protected:
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"pig/c++/pigData.h"   /* sPtr<pigData> _node 値メンバの完全型 */
class ptsObject;
class pigData;
TS_END_INTERFACE

#endif


ptsFireAndForget_::ptsFireAndForget_(TS_ARGS0)
        : pigfFunction_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_pigfFunction_START)
{
	/* ⚠ 基底は _front が無いとルート env を作る。呼び手 (実態親) の env を継承し直す。 */
	if ( pigfFunction_::parent.is_notNull() )
		env = pigfFunction_::parent->get_env();
	return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
	/* ★ 普通に compact する。未解決なら preprocess が自分を listener に登録して yield し、
	 * 完了で起こされて再走する。**呼び手は待たない**(呼び手はこの helper を作っただけ)。 */
	if ( _node.is_notNull() ) {
		sPtr<pigData> v = _node->compact();
		/* ★★ 値は使わないが **エラーは捨ててはいけない** (ひさ指摘 2026-08-26)。
		 * 誰も結果を見ない呼び手 (gate の第 2 引数) だと、side effect の失敗が
		 * **完全に無音**になっていた: `gate(box(1,1,1), volume(1))` が
		 * エラーを出さず rc=0 で成功していた。
		 * ⚠ async は呼び手 (planner) が drain して報告するので、ここで報告すると
		 *   **二重報告**になる。だから扱いは呼び手が _reportError で指定する。 */
		if ( _reportError && v.is_notNull() && v->is_error() && ptsApp.is_notNull() )
			ptsApp->set_agentError(v);
	}
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_ptsFireAndForget_START;
}

TS_STATE(FIN_ptsFireAndForget_START)
{
	return rDO|FIN_pigfFunction_START;
}

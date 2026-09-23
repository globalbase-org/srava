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
	/* ★★ #3482 段 4: **自分が属する try** (根の見えない try も含めて必ず在る)。
	 * async / gate の本体は「起動して待たない」ので、待つ相手として **この helper 自身**を
	 * try の待ちリストへ入れる。⇒ try は async の完了も見送り、destroy() は async も畳める。
	 * ⚠ 登録は **生成時 (INI) = 同期**。本体の agent が出来るのを待たないので、
	 *   「catch に入った瞬間にはまだ登録されていない」という段 2 の穴がここで閉じる。 */
	sPtr<pigDataTryCatch>	myTry;
	int			ffDestroyed;   /* _node へ destroy を転送済み(1 回だけ) */
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
    ffDestroyed = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_pigfFunction_START)
{
	/* ⚠ 基底は _front が無いとルート env を作る。呼び手 (実態親) の env を継承し直す。 */
	if ( pigfFunction_::parent.is_notNull() )
		env = pigfFunction_::parent->get_env();
	/* ★★ #3482 段 4: 囲む try の待ちリストへ入る (pigfAgent と同じ作法)。 */
	/* ★★ #3482 (ひさ 2026-09-19): 入るのは **2 つだけ・chain は辿らない**。
	 *   myTry   … 自分を囲む最も内側の try (= その try が待つ相手)
	 *   ledger  … 根の見えない try (= プログラム全体の台帳。countAgent / wake-all の置き換え)
	 * ⚠ env から引けない縁 (pigf 文脈の外・単体テスト) では myTry を根に落とす。
	 * ⚠⚠ 根へ直接登録しに行くのは **やめた** (ひさ 2026-09-20)。入るのは囲む try 1 つだけ。
	 *   理由は pigfAgent.cpp の同じ所に書いてある (try は自分の待ちリストが空になるまで
	 *   終わらないので、入れ子は「try が try を待つ」で閉じる)。
	 * ★ 外側の try から内側を畳むのは **destroy の木の伝播**が担う (登録は伝播させない)。 */
	/* ⚠⚠ #3482 (ひさ 2026-09-19): env から try が引けないのは **配線のバグ**。env を作る側は
	 *   全部 try をリレーする約束なので、引けないならどこかがリレーしていない。
	 *   **黙って根へ落とさない** — 台帳に穴が空いたまま進んでしまうため。
	 *   ここは _front を持たず値で返す先が無いので、撤収の理由として上げて畳む。 */
	if ( ! env.is_notNull() || ! env->get_try().is_notNull() ) {
		if ( ptsApp.is_notNull() )
			ptsApp->set_agentError(thNEW(pigDataError,
			    ("internal: no enclosing try (env relay is broken)", thNULL, PE_FATAL)));
		return rDO|FIN_START;
	}
	myTry = env->get_try();
	myTry->agent_enter(ifThis);
	return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
	/* ★★ #3482: **撤収を _node へ転送する**。ここが「起動して待たない」役の唯一の接点なので、
	 * 転送しないと **async の中身に撤収が届かない** (畳めと言われたのに走り続ける)。
	 * ⚠ 起動しているのは自分なので止めるのも自分の責任 — pigfSequence / pigfApply が
	 *   自分の持ち物へ転送しているのとまったく同じ理屈 (#3541②)。1 度だけ送る。
	 * ⇒ これで destroy は **pigData の木を通って**内側の try の待ちリストまで届く。 */
	if ( is_destroyed() && ! ffDestroyed ) {
		ffDestroyed = 1;
		if ( _node.is_notNull() ) _node->destroy();
	}
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
		/* ★★ #3482 段 4: **利用者が書いた try の中なら、その try へ渡す** (= catch で捕まる)。
		 * ⚠ 渡したら planner には報告しない — try が報告経路になる (catch が無ければ
		 *   2.2 でそのエラーが try の値になる) ので、両方やると二重報告になる。
		 * ⚠ 畳まれた跡 (PE_DERIVED) は渡さない — 新しい失敗ではない。
		 * 根の見えない try の下 (= トップレベル) は従来どおり: _reportError が 1 なら
		 * ptsApp へ、0 なら呼び手 (planner の drain_async) が報告する。 */
		int owned = 0;
		if ( v.is_notNull() && v->is_error() && ! v->is_derived() && myTry.is_notNull() ) {
			myTry->agent_error(v);   /* ★ 根の try も含めて **必ず try が持つ** */
			owned = 1;
		}
		/* ⚠ 根の下 (= トップレベル) の分は末尾で planner (drain_async) が根の try から読んで
		 *   報告する。⇒ ここで ptsApp へ上げるのは「try が受け取らなかった」ときだけ。 */
		if ( ! owned && _reportError && v.is_notNull() && v->is_error() && ptsApp.is_notNull() )
			ptsApp->set_agentError(v);
	}
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	/* ★ #3482 段 4: 待ちリストから抜ける (冪等)。⚠ ここが唯一の終点。 */
	if ( myTry.is_notNull() ) {
		myTry->agent_leave(ifThis);
		myTry = thNULL;
	}
	return rDO|FIN_ptsFireAndForget_START;
}

TS_STATE(FIN_ptsFireAndForget_START)
{
	return rDO|FIN_pigfFunction_START;
}

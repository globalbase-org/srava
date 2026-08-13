/*
 * ptsObject — piggybackTurtle の tinyState 系 元祖クラス。
 * すべての pts オブジェクトの祖先。実態元祖 ptsApplication へのハンドル ptsApp を持ち、
 * 実態親(ptsObject)から継承する。ptsApplication は自分自身を ptsApp に立てる。
 * (CODING_CONVENTIONS.md §3 の元祖/実態元祖 パターン)
 */
#include	"pig/c++/ptsObject.h"
#include	"_ts2/c++/ptsObject_.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp の ptsApplication(完全型)。ssObject の流儀 */

CLASS_TINYSTATE(pig/c++/ptsObject,ts2/c++/tinyState)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsObject_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;

	// pig フレームワークの実態元祖(ptsApplication)へのハンドル。public メンバとして
	// 公開(インタフェースは参照フォワーダ sPtr<ptsApplication>& なので前方宣言で済む)。
	sPtr<ptsApplication>	ptsApp;
	virtual sPtr<pigEnvironment>	get_env();
private:
protected:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class tinyState;
class ptsApplication;
class pigEnvironment;
TS_END_INTERFACE

#endif


ptsObject_::ptsObject_(TS_ARGS0)
        : tinyState_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

/* 実装ブロック内で inline body を書くと tscpp2 が閉じ } を漏らすため、定義は外に出す */
sPtr<pigEnvironment>
ptsObject_::get_env()
{
	return thNULL;   // 基底は env を持たない(pigfFunction/pigfMain が override)
}



/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_START)
{
	// 実態親が ptsObject なら ptsApp を継承(= 実態元祖ハンドルの伝播)。
	// ptsApplication は実態親が tsApplication で継承されないので、
	// 自身の INI_ptsObject_START 上書きで ptsApp=自分 を立てる。
	sPtr<ptsObject> p = sPtr<ptsObject>::d_cast(parent);
	if ( p.is_notNull() )
		ptsApp = p->ptsApp;
	return rDO|INI_ptsObject_START;
}
TS_STATE(INI_ptsObject_START)   // 派生がここを上書きして初期化を挿入する
{
	return rDO|ACT_START;
}
TS_STATE(ACT_START)             // 基底はアイドル(派生が上書き)
{
	return 0;
}
TS_STATE(FIN_START)             // 派生がここを上書きして後片付けを挿入する
{
	return rDO|FIN_ptsObject_START;
}
TS_STATE(FIN_ptsObject_START)
{
	/* ★ §9: 終了時点で app 参照を手放す。ptsApplication 自身は ptsApp = ifThis の**自己参照**
	 * なので、ここで切らないと参照が残り続ける。派生の FIN は全てここへ畳まれる前に自分の
	 * ptsApp 利用 (gate_release / agent_leave 等) を済ませている。 */
	ptsApp = thNULL;
	return rDO|FIN_TINYSTATE_START;
}

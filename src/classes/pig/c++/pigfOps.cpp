/*
 * pigfOps — pigData 演算子のうち pigf(tinyState)を必要とするものの定義。
 * pigDataOperatorVariable::_start は caller の env を引くため ptsObject を完全型で要する。
 * (pigData.h ↔ pigfFunction の循環依存を、この .cpp に出すことで回避)
 * これは状態機械ではない普通の C++ 定義(CLASS_TINYSTATE 無し → tscpp2 不要)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	<stdio.h>

void
pigDataOperatorVariable::_start()
{
	if ( args.length() == 0 ) {
		result = thNEW(pigDataError,("variable name required",get_info()));
		return;
	}
	sPtr<ptsObject> f = sPtr<ptsObject>::d_cast(sCallSection::key->caller());
	if ( !f.is_notNull() ) {           // caller が ptsObject でない(pigf 文脈外)
		result = thNEW(pigDataError,("variable read outside pigf context",get_info()));
		return;
	}
	sPtr<pigEnvironment> e = f->get_env();
	if ( !e.is_notNull() ) {
		result = thNEW(pigDataError,("no environment",get_info()));
		return;
	}
	result = e->get_var(args[0]->get_str());   // 束縛値(未 compact のことが多い)をそのまま
	// 未定義変数エラー等は env が作る(位置情報なし)→ この varref の位置(IDENT 由来)を刻む。
	if ( result->is_error() && !result->get_info().is_notNull() && get_info().is_notNull() )
		result->set_info(get_info());
}

/* lambda リテラル評価 = 定義時環境(caller env)を捕捉して pigDataLambda 値を作る。 */
void
pigDataLambdaExpr::_start()
{
	sPtr<ptsObject> f = sPtr<ptsObject>::d_cast(sCallSection::key->caller());
	if ( !f.is_notNull() ) {
		result = thNEW(pigDataError,("lambda defined outside pigf context",get_info()));
		return;
	}
	sPtr<pigEnvironment> e = f->get_env();   // 捕捉環境(root では null のことも)
	/* ★ 値捕捉(by-value クロージャ): 生成時点の可視束縛を frozen フレームに値コピーする。
	 *   - frozen は get_var で先に当たるので、後の `base = …`(set_var)は **caller env 側だけ** 書き換え、
	 *     クロージャは凍結値を読む → late-binding の footgun を解消(map の前後どちらの再代入も影響しない)。
	 *   - frozen->parent = e にするので、生成時まだ未束縛の名前(自己再帰 `var f = \(…){…f…}` の f 等)は
	 *     frozen を素通りして apply 時に e で遅延解決 → **再帰は維持**(「解決できるものだけ凍る」を shadow で実現)。
	 *   - スカラ/メッシュは不変なので force 不要(ポインタ捕捉=値捕捉)。配列/ハッシュの要素破壊代入は別途検討。
	 *   e==null(pigf 文脈外の縁)は従来どおり素の参照(捕捉対象なし)。 */
	sPtr<pigEnvironment> cap = e;
	if ( e != thNULL ) {
		sPtr<pigEnvironment> frozen = thNEW(pigEnvironment,(e));   // parent=e(再帰フォールバック)
		e->snapshot_into(frozen);
		cap = frozen;
	}
	sPtr<pigDataLambda> lam = thNEW(pigDataLambda,(cap, get_info()));
	for ( int i = 0 ; i < params.length() ; ++i )
		lam->push_param(params[i]);
	lam->set_body(bodyT);                    // body は **テンプレ**(apply 時に clone)
	result = lam;
}

sPtr<stdString>
pigDataLambda::get_str()
{
	char buf[32];
	::snprintf(buf, sizeof buf, "<lambda/%d>", params.length());
	return thNEW(stdString,(buf));
}

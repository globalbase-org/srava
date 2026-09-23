/*
 * pigfOps — pigData 演算子のうち pigf(tinyState)を必要とするものの定義。
 * pigDataOperatorVariable::_start は caller の env を引くため ptsObject を完全型で要する。
 * (pigData.h ↔ pigfFunction の循環依存を、この .cpp に出すことで回避)
 * これは状態機械ではない普通の C++ 定義(CLASS_TINYSTATE 無し → tscpp2 不要)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigfTryCatch.h"     /* ★ #3482: try/catch の helper(pigDataTryCatch が起動する) */
#include	<stdio.h>
#include	<string.h>   /* record_reason の文言重複排除 (#3482) */

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
	 *   - ⚠ frozen は **親を持たない** (下の #3450 を見よ)。旧版はここに「frozen->parent = e にするので
	 *     未束縛の名前は apply 時に遅延解決 → 再帰は維持」と書いてあったが、**2026-08-29 に撤去された
	 *     設計の説明**が残っていたもの (#3482 §4.5 で訂正)。いまは前方参照は「未定義変数」の明示エラー。
	 *   - ★ #3482: frozen は **try も持たない** (tryPtr は写さない)。try の帰属は動的なので、
	 *     捕捉時点の try に縛ると「try の外で定義して中で呼ぶ」が効かなくなる。呼び出し時に
	 *     pigfApply が caller env から引き継ぐ。
	 *   - スカラ/メッシュは不変なので force 不要(ポインタ捕捉=値捕捉)。配列/ハッシュの要素破壊代入は別途検討。
	 *   e==null(pigf 文脈外の縁)は従来どおり素の参照(捕捉対象なし)。 */
	sPtr<pigEnvironment> cap = e;
	if ( e != thNULL ) {
		/* ★ #3450 (ひさ設計 2026-08-29): **frozen は親を持たない** (完全な値スナップショット)。
		 * 旧: parent=e で「未束縛の名前は apply 時に定義元 env で遅延解決」としていたが、
		 *   ① 前方参照だけ凍らない非対称 (定義の並べ替えで挙動が変わる footgun) と
		 *   ② env ⇄ lambda の参照循環 (capturedEnv->parent が定義元 env に戻る。参照カウントでは
		 *      永遠に落ちず、束縛された継続 pair ごと中間結果がプロセス終了まで残る。実測 N+3 個)
		 * の両方の根が同じこの 1 辺だった。前方参照は「未定義変数」の明示エラーになる。
		 * 再帰は自己適用で書く: var f = \(f,x){ … f(f,x-1) … }; f(f,5)
		 * (引数は後方束縛なので凍結と干渉しない。リポジトリ内の .sra に名前による自己再帰は 0 件)。 */
		sPtr<pigEnvironment> frozen = thNEW(pigEnvironment,(thNULL));
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


/* ------------------------------------------------------------------ */
/* ★★ #3482: try/catch                                                */
/* ------------------------------------------------------------------ */

/* try 文の起動 = helper(pigfTryCatch)を生やす。pigDataFunction<T>::_start と同じ作法で
 * **親を辿って最初の ptsObject** を実態親にする (#3419: ts2Parallel の worker は ptsObject では
 * ないので、そこから helper を作ると parent が null になって落ちる)。 */
void
pigDataTryCatch::_start()
{
	sPtr<ptsObject> pp;
	for ( sPtr<tinyState> p = sCallSection::key->caller() ; p.is_notNull() ; p = p->parent ) {
		pp = sPtr<ptsObject>::d_cast(p);
		if ( pp.is_notNull() ) break;
	}
	helper = thNEW(pigfTryCatch, (pp, thThis));
}

/* エラーの収集。★ agent 側からも呼ばれるので **発生順**に積む (集約して 1 件にしない)。 */
void
pigDataTryCatch::push_error(sPtr<pigData> e)
{
	if ( e.is_notNull() )
		errs.push(e);
}

/* ---- 待ちリスト (#3482 段 2) ------------------------------------------------ */

/* pigfAgent の INI から。この try のスコープで起動した agent を覚える。 */
void
pigDataTryCatch::agent_enter(sPtr<tinyState> who)
{
	waiters.push(who);
	waitLive++;
}

/* pigfAgent の FIN (MEDWAIT) から。★ **冪等** — 明示 leave と ZOM 通知の両方が飛んでも
 * 二重に外さない (§3.1 ③)。外した所は thNULL にして参照を落とす (ptsApplication と同じ作法。
 * ここを詰めないのは、走査側が is_notNull() を見るので穴が空いても安全なため)。 */
/* ⚠ 探索は **線形走査** (穴は thNULL のまま詰めない)。総計では O(N²) になるが、#3482 §5.2 の
 *   「N=100 規模で実測が要る」を測った結果 **問題にならない** (2026-09-19・build-par。
 *    数値は Redmine #3482 のコメントに):
 *   ・1 件あたりの費用は **N が増えるほど下がる** (固定費の償却) = 超線形の項は見えない
 *   ・try で囲んでも囲まなくても **差は測定限界以下** (登録が 2 本になる分は見えない)
 *   ★ §5.2 が心配していたのは (3) TSE_DESTROY 方式の「listen が N 本」だが、**その方式は
 *     採っていない** (明示 leave = listen 0 本) ので、そちらの懸念は成立しない。
 *   ⚠ 詰めない設計は ptsApplication::agent_leave から引き継いだもの (走査側が is_notNull()
 *     を見るので穴が空いても安全・詰め直し不要)。 */
void
pigDataTryCatch::agent_leave(sPtr<tinyState> who)
{
	for ( int i = 0 ; i < waiters.length() ; ++i ) {
		if ( waiters[i] == who ) {
			waiters[i] = thNULL;
			if ( --waitLive < 0 ) waitLive = 0;
			wake_waiters();
			return;
		}
	}
	/* 見つからなければ何もしない = 既に外した (冪等) か、この try の計算ではない。 */
}

/* 待ち中の agent が出したエラー。★ ここへ来るのは **継続を既に返した後** のエラー (§4.2 ①) だけ。
 * ②/③ は評価チェーンを直列に上がって statement1 のエラーになるので、ここで積むと二重になる。 */
void
pigDataTryCatch::agent_error(sPtr<pigData> e)
{
	push_error(e);
	wake_waiters();
}

/* この try で走っているものを畳む。★ **終了は待たない** (待つのは error() / try の WAIT 状態)。
 *
 * ★★ 送るのは **待ちリストだけ**。そこから先は **pigData の木を通って**伝わる
 *   (ptsFireAndForget → async の front → pigfAsync → 内側の try → その待ちリスト → map …)。
 *   ⇒ @map@ ・ @while@ ・ @system()@ のように **待ちリストに載らないもの**にも、
 *     それを起動した async / agent を畳めば届く。
 *
 * ⚠⚠ かつてここで **statement1 の木にも** destroy を送っていた (args[0]->destroy()) が、
 *   **外した** (2026-09-19)。理由は 2 つ:
 *     ① 効いていなかった — 外して ctest 全本 (566/567) が緑。catch が走る時点で statement1 は
 *        既に終わっており、そこから生きているものへ辿り着く経路は待ちリスト側にしかない
 *        (statement1 が終わってもなお走っていられるのは async / gate = ptsFireAndForget と
 *         継続を返した agent だけで、どちらも待ちリストに居る)
 *     ② **try の外へ出られる唯一の経路だった** — varref (pigDataOperatorVariable) の result は
 *        「変数が指しているノードそのもの」なので、try の外で束縛され外で走っているノードを
 *        指していると、木を辿った destroy がそこまで届いてしまう。
 *   ⇒ 外したことで「**catch の destroy() はその try で生成したもの以外を壊さない**」(ひさ要件)
 *     が **構造として**成り立つ。 */
void
pigDataTryCatch::destroy_agents()
{
	/* ★ 「畳めと言った」印を先に立てる — 畳まれた agent はこれを見て、自分の「aborted」を
	 * PE_DERIVED で立てる (= 新しい失敗ではないので集約・報告・終了コードから外れる)。
	 * ⚠ 先に立てるのが肝心: destroy() は同期に届きうるので、後から立てると取りこぼす。 */
	tearingDown = 1;
	for ( int i = 0 ; i < waiters.length() ; ++i )
		if ( waiters[i].is_notNull() && ! waiters[i]->is_destroyed() )
			waiters[i]->destroy();
}

/* 撤収の理由が立ったときの一撃。⚠ destroy ではなく **起こすだけ** — 各自が待ち状態の頭で
 * SHOULD_ABORT を見て自分で畳む、という既存の作法を変えない。 */
void
pigDataTryCatch::wake_agents()
{
	for ( int i = 0 ; i < waiters.length() ; ++i )
		if ( waiters[i].is_notNull() && ! waiters[i]->is_destroyed() )
			waiters[i]->wakeup();
}

void
pigDataTryCatch::register_waiter(sPtr<pigDataDelay> w)
{
	if ( w.is_notNull() )
		errWaiters.push(w);
}

void
pigDataTryCatch::register_drain_waiter(sPtr<pigDataDelay> w)
{
	if ( w.is_notNull() )
		drainWaiters.push(w);
}

/* 状態が動いた ⇒ 答えられるようになった error() を解決し、try 自身の待ちも起こす。
 * ★ **通知駆動**。総なめのポーリング (compact) は採らない — teardown 座り込みの決着 (#3414 系)
 *   で「FIN poll タイマの自己競合」が真因と分かり、この家の最終形は通知駆動になっている。 */
void
pigDataTryCatch::wake_waiters()
{
	for ( int i = 0 ; i < errWaiters.length() ; ++i ) {
		if ( ! errWaiters[i].is_notNull() ) continue;
		sPtr<pigData> v = take_error();
		if ( ! v.is_notNull() ) break;        /* まだ答えられない ⇒ 後ろの待ち手も同じ */
		errWaiters[i]->set_result(v);
		errWaiters[i] = thNULL;
	}
	/* ★ flush() は「この try の待ちが **全部**なくなること」だけを待つ。空になった時点で解く。 */
	if ( waitLive == 0 ) {
		for ( int i = 0 ; i < drainWaiters.length() ; ++i ) {
			if ( ! drainWaiters[i].is_notNull() ) continue;
			drainWaiters[i]->set_result(thNEW(pigDataInteger,((INTEGER64)0)));
			drainWaiters[i] = thNULL;
		}
	}
	if ( helper.is_notNull() )
		helper->wakeup();                     /* try の WAIT 状態を進める */
	if ( waker.is_notNull() )
		waker->wakeup();                      /* 根の try: app (planner の WAITAGENTS) を起こす */
}

/* error(): 未読のエラーがあれば **中身のハッシュ** を 1 件返し、無ければ 0。
 * ⚠ **pigDataError そのものは返さない** — error 値はあらゆる演算を吸収して上方へ伝播するので、
 *   catch の中で変数に入れた瞬間に catch 自身がそのエラーで抜けてしまう。
 * ★ ハッシュなら位置・クラス・モジュール名を **別々の鍵**で読めて、`throw error();` で
 *   元のエラーをそのまま再現できる (pigDataError::to_hash ⇄ pig_err_from_hash)。 */
sPtr<pigData>
pigDataTryCatch::take_error()
{
	if ( errTaken < errs.length() ) {
		sPtr<pigData> e = errs[errTaken++];
		/* ⚠ 型判定に d_cast を使わない規約はあるが、ここは **自分が push_error で積んだ物**
		 *   (pigDataError のみ) を取り出す所なので、中身を取るための cast はこれで正しい。
		 *   万一エラーでない物が積まれていたら、文言だけのハッシュに落とす。 */
		sPtr<pigDataError> ee = sPtr<pigDataError>::d_cast(e);
		if ( ee.is_notNull() ) return ee->to_hash();
		sPtr<pigDataHash> h = thNEW(pigDataHash,());
		h->set_ix(thNEW(pigDataString,("message")), thNEW(pigDataString,(e->get_str())));
		h->set_ix(thNEW(pigDataString,("class")),   thNEW(pigDataString,("normal")));
		h->set_ix(thNEW(pigDataString,("file")),    thNEW(pigDataString,("")));
		h->set_ix(thNEW(pigDataString,("line")),    thNEW(pigDataInteger,((INTEGER64)0)));
		return h;
	}
	/* ★ まだ待つ相手が居るなら **答えない** (thNULL)。呼び手 (error()) はこれを見て待ちに入り、
	 *   エラーが出るか全員終わるかしたときに wake_waiters が起こす。
	 *   ⇒ 「エラーが出るまで / 全員終わるまで」のブロックは **error() に集約**される (§2-A)。 */
	if ( waitLive > 0 )
		return thNULL;
	return thNEW(pigDataInteger,((INTEGER64)0));
}

/* error() の評価 = 囲む try を env から **動的に**引き、1 件取り出す。 */
void
pigDataOperatorError::_start()
{
	sPtr<ptsObject> f = sPtr<ptsObject>::d_cast(sCallSection::key->caller());
	if ( !f.is_notNull() ) {
		result = thNEW(pigDataError,("error() outside pigf context",get_info()));
		return;
	}
	sPtr<pigEnvironment> e = f->get_env();
	/* ★★ #3564: **sPtr で受ける** (理由は cgptsPlanner の flush と同じ)。 */
	sPtr<pigDataTryCatch> t = e.is_notNull() ? e->get_try() : sPtr<pigDataTryCatch>();
	/* ★★ #3482 段 3: 根の見えない try が入ったので「try が無い」はもう起きない。
	 *   代わりに **catch を持たない try** を「catch の外」と読む — catch 本体が無い try は
	 *   catch を実行しえないので、そこで error() が動いていることはありえない。
	 *   (根の try は文を持たないので当然 catch も無い。⇒ 判定は既存の状態から導ける
	 *    = 「根かどうか」の新しい印を足さずに済む。) */
	if ( t.is_notNull() && ! t->get_has_catch() )
		t = thNULL;
	if ( ! t.is_notNull() ) {
		/* ⚠ 通常はパース時に弾かれている (ns_sravaParser.y の mk_program)。ここに来るのは、
		 *   catch 本体で定義した lambda を try の外へ持ち出して呼んだ場合など、構文では
		 *   見切れない経路だけ。
		 * ★ 文言を **パース時と別にする**: 同じ文言だと「静的に弾けたのか、走ってから
		 *   弾いたのか」を出力から区別できず、静的検査の回帰テストが検定にならない
		 *   (実際、文言が同じだった間は静的検査を外しても緑のままだった)。 */
		result = thNEW(pigDataError,("error(): no enclosing catch block at run time",get_info()));
		return;
	}
	sPtr<pigData> v = t->take_error();
	if ( v.is_notNull() ) { result = v; return; }
	/* ★ まだ答えが出ない (待ちリストに agent が残っている) ⇒ **待ちに入る**。
	 *   result を立てず helper を持つと、pigDataDelay::preprocess が呼び手を helper の
	 *   listener にして yield する (この家の標準の待ち方)。起こすのは wake_waiters の
	 *   set_result で、そこで listener へ TSE_UPDATED が飛ぶ。 */
	sPtr<tinyState> th = t->try_helper();
	/* ★ #3562: flush() と同じ — 預け先が無いのは配線の異常。黙って 0 を返すと
	 * 「まだ走っているのに もう何も無い」と嘘をつく (catch の中の 2 回目の error() で実測)。 */
	if ( ! th.is_notNull() )
		stdObject::panic("error(): try_helper() is NULL (try helper not published)");
	t->register_waiter(sPtr<pigDataDelay>::d_cast(thThis));
	helper = th;
}

/* destroy() — 待ちリストの agent へ撤収を送る。★ 終了は待たない (§2-B)。戻り値は送った数。 */
void
pigDataOperatorDestroyAgents::_start()
{
	sPtr<ptsObject> f = sPtr<ptsObject>::d_cast(sCallSection::key->caller());
	sPtr<pigEnvironment> e = f.is_notNull() ? f->get_env() : sPtr<pigEnvironment>(thNULL);
	sPtr<pigDataTryCatch> t = e.is_notNull() ? e->get_try() : sPtr<pigDataTryCatch>();
	if ( ! t.is_notNull() ) {
		result = thNEW(pigDataError,("destroy(): no enclosing try block "
		                             "(there is nothing to send a teardown to)",get_info()));
		return;
	}
	int n = t->agent_live();
	t->destroy_agents();
	result = thNEW(pigDataInteger,((INTEGER64)n));
}

/* throw 式; — 値から **エラーを復元して発生させる**。
 * ★ `throw error();` が「捕まえたエラーの再送」になるので、`try { s }` は
 *   `try { s } catch { throw error(); }` と同じ意味になる (ひさ 2026-09-18)。
 * ⚠ 復元できない値 (0・非ハッシュ・鍵不足) は **「復元できない」というエラー**になる。
 *   黙って何もしない/黙って別のエラーにする、は採らない。 */
void
pigDataOperatorThrow::_start()
{
	if ( args.length() < 1 ) {
		result = thNEW(pigDataError,("throw needs a value",get_info()));
		return;
	}
	sPtr<pigData> v = args[0]->compact();
	/* 引数の評価自体が失敗したら、それをそのまま上げる (復元の話ではない)。 */
	if ( v->is_error() ) { result = v; return; }
	result = pig_err_from_hash(v, get_info());
}

/* ---- プログラム全体の撤収の指標と診断台帳 (#3482・根の try だけが使う) ----------------
 * ⚠ 中身は ptsApplication から **そのまま**移したもの。意味論は 1 つも変えていない。 */

int
pigDataTryCatch::set_teardown_reason(sPtr<pigData> e)
{
	int first = ( tdReason == thNULL );
	if ( first )                     /* 先勝ち (最初の理由を保持) */
		tdReason = e;
	/* ★★ #3503: **PE_PANIC だけは先勝ちを覆す**。panic は必ず *撤収の途中* で起きるので、
	 * その時点の理由は既に "interrupted by SIGINT" 等になっている。先勝ちのままだと panic が
	 * 捨てられ、planner が **行き先 (子プロセスの消滅を待って abort) を選べず永久に待つ**。
	 * ⚠ panic どうしの上書きはしない (2 本目が固まっても理由は変わらない)。 */
	if ( ! first && e != thNULL && e->is_panic()
	  && ( tdReason == thNULL || ! tdReason->is_panic() ) )
		tdReason = e;
	return first;
}

void
pigDataTryCatch::record_reason(sPtr<pigData> e)
{
	if ( ! e.is_notNull() || ! e->is_error() )
		return;
	/* ★ #3482: 畳まれた跡 (PE_DERIVED) は記録しない — 新しい失敗ではないので、末尾の列挙が
	 * 畳まれた跡で埋まると原因の行が読めなくなる。 */
	if ( e->is_derived() )
		return;
	if ( reasons.length() >= 16 )    /* 壊れ方が「大量出力」にならないように上限を置く */
		return;
	sPtr<stdString> m = e->get_str();
	if ( ! m.is_notNull() )
		return;
	for ( int i = 0 ; i < reasons.length() ; ++i ) {
		sPtr<stdString> o = reasons[i]->get_str();
		if ( o.is_notNull() && ::strcmp(o->get_str(), m->get_str()) == 0 )
			return;                  /* 同じ文言は 1 度だけ (撤収で多数が同じ "aborted" を出す) */
	}
	reasons.push(e);
}

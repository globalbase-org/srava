/*
 * ns_sravaParser.y — srava 文法(v1: 式 + var/代入)。lemon_cpp が ns_sravaParser.cpp/.h を生成。
 * トークン値は sPtr<pigData>。アクションで pigData ツリー(= S 式相当の DAG)を構築する。
 *   - 関数呼び box/union/intersection/difference/export は名前で dispatch(mk_call)
 *   - mesh 演算子 |||(union) &&&(intersection) ---(difference)
 *   - 四則 + - * / は pigDataOperator(整数/実数)
 *   - var 宣言/代入は pigDataFunction<pigfAssign>、変数参照は pigDataOperatorVariable
 *   - 文の並びは pigDataFunction<pigfSequence>(順に評価し最後の値を返す)
 * レキサ/ドライバは cgptsLemonParser(手書き get_token + ParseAlloc/Parse/ParseFree)。
 */
%include{

#include	"pig/c++/pigData.h"
#include	"cg/c++/pigcgOperators.h"   /* export/export_async/flush 演算子(srava I/O シンク・pigcg 命名) */
/* PROGRAM モードの文/関数ノードは pigf* に依存。VALUE 専用ビルド(エージェント側で
 * インライン引数を value-parse するだけ)では pigf* をリンクしないので、これらの
 * include と PROGRAM 専用ヘルパを SRAVA_VALUE_ONLY で落とす(= 同一文法を 2 用途で共有)。 */
#ifndef SRAVA_VALUE_ONLY
#include	"cg/c++/pigfSravaAgent.h"
#include	"pig/c++/pigfAssign.h"
#include	"pig/c++/pigfSequence.h"
#include	"pig/c++/pigfIf.h"
#include	"pig/c++/pigfApply.h"
#include	"pig/c++/pigfWhile.h"
#include	"pig/c++/pigfSystem.h"
#include	"pig/c++/pigfArrayFold.h"   /* union/intersection/combine(配列1引数)を eval 時に二分木化 */
#include	"pig/c++/pigfMap.h"         /* map(array, fn): 各要素に lambda を適用し配列を返す */
#include	"pig/c++/pigfMapOp.h"       /* transform 演算子(>>> <> *** @)の配列対応(broadcast/zip) */
#include	"pig/c++/pigfGate.h"        /* gate(inp1, inp2): inp1 完了時に inp2 を起動(完了フック) */
#include	"pig/c++/pigfPluginAgent.h"    /* 登録済みプラグイン op を別プロセスへ委ねる薄い agent */
#include	"pig/c++/pigPluginRegistry.h"  /* op→bin 照会(プラグインレジストリ) */
#endif
#include	"cg/c++/cgptsLemonParser.h"
#include	<string.h>
#include	<assert.h>

#define DEF_NAMESPACE ns_sravaParser

/* lemon_cpp テンプレート(lempar.c)がトークン値代入に使う(sPtr 対応の代入)。 */
#define REF_SET(x,y)	(x) = (y)

/* ---- ツリー構築ヘルパ(ファイルスコープ static。アクションから unqualified で呼ぶ) ---- */

/* PROGRAM 専用ヘルパ(pigf* に依存)。VALUE 専用ビルドでは未使用なのでスタブ化。 */
#ifndef SRAVA_VALUE_ONLY

/* 文の並び(pigDataArray)→ pigfSequence ノード。 */
static sPtr<pigData> mk_seq(sPtr<pigData> arr) {
	sPtr<pigDataFunction<pigfSequence> > s = thNEW(pigDataFunction<pigfSequence>,());
	sPtr<pigDataArray> a = sPtr<pigDataArray>::d_cast(arr);
	if ( a.is_notNull() )
		for ( int i = 0 ; i < a->length() ; ++i )
			s->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
	return s;
}

/* var 宣言(DEF)/ 代入(SET)。name=pigDataString、val 省略可(null)。 */
static sPtr<pigData> mk_assign(int mode, sPtr<pigData> name, sPtr<pigData> val) {
	sPtr<pigDataFunction<pigfAssign> > n = thNEW(pigDataFunction<pigfAssign>,());
	n->set_mode(mode);
	n->pushArg(name);
	if ( val.is_notNull() )
		n->pushArg(val);
	return n;
}

/* async 文 `async { body...; sync: S }`。body 文配列 + 省略可 sync 文 → pigcgOperatorAsync。
 * args = [body0,...,(sync)]、syncStmt があれば末尾に積み mode=1(hasSync)。実体は pigfAsync。 */
static sPtr<pigData> mk_async(sPtr<pigData> arr, sPtr<pigData> syncStmt) {
	sPtr<pigcgOperatorAsync> n = thNEW(pigcgOperatorAsync,());
	sPtr<pigDataArray> a = sPtr<pigDataArray>::d_cast(arr);
	if ( a.is_notNull() )
		for ( int i = 0 ; i < a->length() ; ++i )
			n->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
	if ( syncStmt.is_notNull() ) {
		n->pushArg(syncStmt);
		n->set_mode(1);   /* 末尾文が sync 文 */
	}
	return n;
}

#else  /* SRAVA_VALUE_ONLY: PROGRAM ルールは到達不能だがアクション本体は生成されるためスタブが要る。 */
static sPtr<pigData> mk_seq(sPtr<pigData>) { return thNULL; }
static sPtr<pigData> mk_assign(int, sPtr<pigData>, sPtr<pigData>) { return thNULL; }
static sPtr<pigData> mk_async(sPtr<pigData>, sPtr<pigData>) { return thNULL; }
#endif

/* 添字/メンバ参照 a[ix] / a.key → pigDataOperatorIndex(base, key)。 */
static sPtr<pigData> mk_index(sPtr<pigData> base, sPtr<pigData> key) {
	sPtr<pigDataOperatorIndex> n = thNEW(pigDataOperatorIndex,());
	n->pushArg(base);
	n->pushArg(key);
	/* エラー位置(範囲外添字等): 被参照 or キーの位置を刻む。 */
	if ( base.is_notNull() && base->get_info().is_notNull() )    n->set_info(base->get_info());
	else if ( key.is_notNull() && key->get_info().is_notNull() ) n->set_info(key->get_info());
	return n;
}

/* 添字/メンバへの代入 a[ix] = val / a.key = val → pigDataOperatorSetIndex(base, key, val)。
 * 評価時に base を解決して set_ix で破壊的代入(`screw[i] = …`)。 */
static sPtr<pigData> mk_setindex(sPtr<pigData> base, sPtr<pigData> key, sPtr<pigData> val) {
	sPtr<pigDataOperatorSetIndex> n = thNEW(pigDataOperatorSetIndex,());
	n->pushArg(base);
	n->pushArg(key);
	n->pushArg(val);
	if ( base.is_notNull() && base->get_info().is_notNull() ) n->set_info(base->get_info());
	return n;
}

/* 配列構築 `[e0,e1,...]`(式中)= pigDataOperatorArray(可変長引数 → compact で値配列)。
 * arglist(=要素式を集めた pigDataArray)を受け、その要素を演算子の args に積む。
 * 値ノードではなく演算子なので、評価地点の env で各要素(varref 含む)が解決される。 */
static sPtr<pigData> mk_arrayop(sPtr<pigData> arglist) {
	sPtr<pigDataArray> a = sPtr<pigDataArray>::d_cast(arglist);
	sPtr<pigDataOperatorArray> n = thNEW(pigDataOperatorArray,());
	int na = a.is_notNull() ? a->length() : 0;
	for ( int i = 0 ; i < na ; ++i )
		n->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
	return n;
}

/* hash エントリ追加(key は pigDataString)。VALUE モード(実値ハッシュ)用。 */
static sPtr<pigData> hash_put(sPtr<pigData> h, sPtr<pigData> key, sPtr<pigData> val) {
	sPtr<pigDataHash>::d_cast(h)->set_ix(key, val);
	return h;
}

/* hash 構築演算子へのエントリ追加(PROGRAM モード)。args に key, val をインターリーブで積む。
 * 値式は compact 時(評価地点 env)で解決される(配列構築と同じ。varref を正しい env で解決)。 */
static sPtr<pigData> hashop_put(sPtr<pigData> h, sPtr<pigData> key, sPtr<pigData> val) {
	sPtr<pigDataOperatorHash> n = sPtr<pigDataOperatorHash>::d_cast(h);
	n->pushArg(key);
	n->pushArg(val);
	return h;
}

/* 制御文。return expr → 演算子(評価地点で expr を compact)、return/break/continue → 値ノード。 */
static sPtr<pigData> mk_return(sPtr<pigData> expr) {
	sPtr<pigDataOperatorReturn> n = thNEW(pigDataOperatorReturn,());
	if ( expr.is_notNull() )
		n->pushArg(expr);
	return n;
}
static sPtr<pigData> mk_control(int kind) {   /* break / continue(値なし) */
	return thNEW(pigDataControl,(kind, thNULL));
}
/* exit 文。メッセージ式(省略可)を持つ → 評価地点で compact → CTRL_EXIT(プログラム終了)。 */
static sPtr<pigData> mk_exit(sPtr<pigData> expr) {
	sPtr<pigDataOperatorExit> n = thNEW(pigDataOperatorExit,());
	if ( expr.is_notNull() )
		n->pushArg(expr);
	return n;
}
/* for の body を catch_continue で包む(continue で step を飛ばさない)。 */
[[maybe_unused]] static sPtr<pigData> mk_catch_continue(sPtr<pigData> body) {
	sPtr<pigDataOperatorCatchContinue> n = thNEW(pigDataOperatorCatchContinue,());
	n->pushArg(body);
	return n;
}

/* 変数参照。 */
static sPtr<pigData> mk_varref(sPtr<pigData> name) {
	sPtr<pigDataOperatorVariable> n = thNEW(pigDataOperatorVariable,());
	n->pushArg(name);
	n->set_info(name->get_info());   /* 未定義変数等のエラーに位置を付ける(IDENT トークン由来) */
	return n;
}

/* 連鎖代入の 1 段: name = rhs を評価し、**name の値**を返す(pigfAssign は名前を返すので、
 * seq[ name=rhs ; varref(name) ] にして末尾 varref で値に解決する)。SET なので宣言済みの外側
 * 変数を scope chain を辿って更新する(pigfSequence が子スコープでも届く)。 */
static sPtr<pigData> mk_chain_assign(sPtr<pigData> name, sPtr<pigData> rhs) {
	sPtr<pigDataArray> seq = thNEW(pigDataArray,());
	seq->push( mk_assign(PIG_ASSIGN_SET, name, rhs) );
	seq->push( mk_varref(name) );
	return mk_seq(seq);
}

/* ★ 兄弟キー参照付き hash リテラルを pigfSequence(逐次スコープ)へ展開する(let* 相当)。
 *   { k0:v0, k1:v1, … } → [ var k0=v0;  var k1=v1;(v1 は k0 を参照可) … ; { k0:k0, k1:k1, … } ]
 * 同じリテラル内で、後のキー値式が先に書いたキーを名前で参照できる(例 { w:10, half: w/2 } → half=5)。
 * pigfSequence は子 env を作って逐次束縛するだけ(クロージャ捕捉でない=deep-copy なし・外側 var の値共有も
 * 従来どおり)。最後の hash の値は varref なので兄弟参照は不要(無限展開しない)。空 hash {} は別規則なので不変。
 * 注: `a.b`(まだ束縛されていない自分自身 a 経由)は従来どおりエラー(これは兄弟参照=キー名直接 参照とは別物)。
 * hop = hashop_put で [k0,v0,k1,v1,…] を積んだ pigDataOperatorHash。 */
#ifndef SRAVA_VALUE_ONLY
static sPtr<pigData> hash_scoped(sPtr<pigData> hop) {
	sPtr<pigDataOperator> h = sPtr<pigDataOperator>::d_cast(hop);
	if ( ! h.is_notNull() ) return hop;   /* 念のため(hashbody は常に演算子) */
	sPtr<pigDataArray> stmts = thNEW(pigDataArray,());
	sPtr<pigDataOperatorHash> fin = thNEW(pigDataOperatorHash,());
	for ( int i = 0 ; i + 1 < h->argc() ; i += 2 ) {
		sPtr<pigData> k = h->arg(i);
		stmts->push( mk_assign(PIG_ASSIGN_DEF, k, h->arg(i+1)) );   /* var <key> = <値式>(逐次スコープ) */
		fin->pushArg(k);
		fin->pushArg( mk_varref(k) );                              /* <key>: <varref key> */
	}
	stmts->push(fin);
	return mk_seq(stmts);
}
#else
static sPtr<pigData> hash_scoped(sPtr<pigData> hop) { return hop; }
#endif

/* 二項 mesh op の演算子位置(opinfo)を取り出すヘルパ。演算子トークン(|||/&&&/---/+++)は pigDataNull(tok_info)
 * を値に運ぶ。文法アクションから呼ぶので **value-only ビルドでも要る** → guard の外で定義。 */
static sPtr<pigInfo> opinfo_of(sPtr<pigData> o) { return o.is_notNull() ? o->get_info() : sPtr<pigInfo>(); }

/* mesh 演算子 → agent op(キャッシュ出力)。 */
#ifndef SRAVA_VALUE_ONLY
/* 単一の二項 mesh op ノードを作る(分配なし)。opinfo があれば **演算子/呼び出しの位置** を優先採用。 */
static sPtr<pigData> mk_meshop_node(const char* op, sPtr<pigData> a, sPtr<pigData> b, sPtr<pigInfo> opinfo = thNULL) {
	sPtr<pigDataFunction<pigfSravaAgent> > n = thNEW(pigDataFunction<pigfSravaAgent>,());
	n->pushArg(a);
	n->pushArg(b);
	n->set_op_name(thNEW(stdString,(op)));
	n->set_out_cache(1);
	/* ソース位置(エラー表示用): opinfo(演算子 |||/---/… or union(…) 呼びの位置)を優先。無ければ
	 * 従来通り左被演算子(無ければ右)の位置。可変 union の二分木ノードや difference が、中身の引数行
	 * (ハッシュ順依存)でなく演算子/呼び出しの行を指すようになる(ひさ要望)。 */
	if ( opinfo.is_notNull() )                              n->set_info(opinfo);
	else if ( a.is_notNull() && a->get_info().is_notNull() )      n->set_info(a->get_info());
	else if ( b.is_notNull() && b->get_info().is_notNull() ) n->set_info(b->get_info());
	return n;
}
/* ノードが `+++`(combine)演算子か。combine の被演算子だけ分配対象にする(演算子は最適化しない方針:
 * 自己交差をランタイム検出して分配…ではなく、+++ という**構文**に結びついた書き換えにする)。 */
static int meshop_is_combine(sPtr<pigData> n) {
	sPtr<pigDataOperator> op = sPtr<pigDataOperator>::d_cast(n);
	if ( ! op.is_notNull() ) return 0;
	sPtr<stdString> nm = op->get_op_name();
	return ( nm.is_notNull() && nm->cmp("combine") == 0 ) ? 1 : 0;
}
/* combine をその葉部分に平展開(入れ子 a+++b+++c → [a,b,c])。combine 以外はそのまま 1 要素。 */
static void meshop_combine_parts(sPtr<pigData> n, sArray<sPtr<pigData> >& out) {
	if ( meshop_is_combine(n) ) {
		sPtr<pigDataOperator> op = sPtr<pigDataOperator>::d_cast(n);
		for ( int i = 0 ; i < op->argc() ; ++i ) meshop_combine_parts(op->arg(i), out);
	} else {
		out.push(n);
	}
}
/* mesh 演算子。被演算子が `+++`(combine)なら、その演算子に限り**構文的に分配**する:
 *   (a+++b) &&& c = (a&&&c) +++ (b&&&c)      [積は左右どちらの combine も成分積を束ねる]
 *   (a+++b) --- c = (a---c) +++ (b---c)      [左 combine は成分ごとに引いて束ね]
 *   c --- (a+++b) = c --- a --- b            [右 combine は逐次差]
 * combine は閉立体を解決せず束ねた形で corefinement に渡せない(3D で (a+++b)---c がエラーになる)ため。
 * |||(union)結果など妥当なメッシュは combine ではないので分配しない。 */
static sPtr<pigData> mk_meshop(const char* op, sPtr<pigData> a, sPtr<pigData> b, sPtr<pigInfo> opinfo = thNULL) {
	int isect = ( ::strcmp(op, "intersection") == 0 );
	int diff  = ( ::strcmp(op, "difference")   == 0 );
	if ( ( isect || diff ) && ( meshop_is_combine(a) || meshop_is_combine(b) ) ) {
		sArray<sPtr<pigData> > pa, pb;
		meshop_combine_parts(a, pa);
		meshop_combine_parts(b, pb);
		sPtr<pigData> acc;   /* 結果を combine で束ねていく(null = まだ無し) */
		if ( isect ) {
			for ( int i = 0 ; i < pa.length() ; ++i )
			for ( int j = 0 ; j < pb.length() ; ++j ) {
				sPtr<pigData> piece = mk_meshop_node("intersection", pa[i], pb[j], opinfo);
				acc = acc.is_notNull() ? mk_meshop_node("combine", acc, piece, opinfo) : piece;
			}
		} else {   /* difference: 左成分ごとに、全右成分で逐次差 */
			for ( int i = 0 ; i < pa.length() ; ++i ) {
				sPtr<pigData> di = pa[i];
				for ( int j = 0 ; j < pb.length() ; ++j )
					di = mk_meshop_node("difference", di, pb[j], opinfo);
				acc = acc.is_notNull() ? mk_meshop_node("combine", acc, di, opinfo) : di;
			}
		}
		return acc;
	}
	return mk_meshop_node(op, a, b, opinfo);
}
#else
static sPtr<pigData> mk_meshop(const char*, sPtr<pigData>, sPtr<pigData>, sPtr<pigInfo> = thNULL) { return thNULL; }
#endif

/* 実行木分解(1.2.3/coding_plan §4): n-ary 可換呼び出し(union(a,b,c,..))を二項 op の木に分解。
 * agent は常に二項だけ見ればよい(平展開しない)。中置 a|||b|||c は文法で既に二項なので触らない。 */
#ifndef SRAVA_VALUE_ONLY
/* 可換: ops は recipe_hash 昇順済み前提 → 均衡二分木(中間 op がキャッシュ共有されやすい)。 */
static sPtr<pigData> build_bintree(const char* op, sArray<sPtr<pigData> >& ops, int lo, int hi, sPtr<pigInfo> opinfo = thNULL) {
	if ( hi - lo == 1 )
		return ops[lo];
	int mid = (lo + hi) / 2;
	return mk_meshop(op, build_bintree(op, ops, lo, mid, opinfo), build_bintree(op, ops, mid, hi, opinfo), opinfo);
}
/* 非可換(difference): 左 fold (((a op b) op c)…)。順序保持・ソートしない。 */
static sPtr<pigData> build_leftfold(const char* op, sArray<sPtr<pigData> >& ops, int n, sPtr<pigInfo> opinfo = thNULL) {
	sPtr<pigData> acc = ops[0];
	for ( int i = 1 ; i < n ; ++i )
		acc = mk_meshop(op, acc, ops[i], opinfo);
	return acc;
}
#endif

/* 比較演算子 → pigDataOperator(結果は 0/1 の整数)。 */
static sPtr<pigData> mk_cmp(int op, sPtr<pigData> a, sPtr<pigData> b) {
	sPtr<pigDataOperator> n;
	switch ( op ) {
	case 0: n = thNEW(pigDataOperatorEq,()); break;   /* == */
	case 1: n = thNEW(pigDataOperatorNe,()); break;   /* != */
	case 2: n = thNEW(pigDataOperatorLt,()); break;   /* <  */
	case 3: n = thNEW(pigDataOperatorGt,()); break;   /* >  */
	case 4: n = thNEW(pigDataOperatorLe,()); break;   /* <= */
	default:n = thNEW(pigDataOperatorGe,()); break;   /* >= */
	}
	n->pushArg(a);
	n->pushArg(b);
	/* 演算子ノードに行情報を継ぐ(型エラー時に式の行を出すため。mk_logic/mk_arith と同作法)。 */
	if ( a.is_notNull() && a->get_info().is_notNull() )      n->set_info(a->get_info());
	else if ( b.is_notNull() && b->get_info().is_notNull() ) n->set_info(b->get_info());
	return n;
}

/* 論理演算子 → 既存の boolean op ノード(真偽は get_bool・結果 0/1 整数)。pigf* 非依存なので
 * VALUE_ONLY ビルドでもそのまま使える(serialize は論理 op を出さないので VALUE 文法には不要)。
 * NB: 短絡評価はしない(両辺評価)。比較結果同士なら 0/1 で安全。 */
static sPtr<pigData> mk_logic(int op, sPtr<pigData> a, sPtr<pigData> b) {
	sPtr<pigDataOperator> n = ( op == 0 )
	    ? (sPtr<pigDataOperator>)thNEW(pigDataOperatorBor,())     /* || */
	    : (sPtr<pigDataOperator>)thNEW(pigDataOperatorBand,());   /* && */
	n->pushArg(a);
	n->pushArg(b);
	if ( a.is_notNull() && a->get_info().is_notNull() )      n->set_info(a->get_info());
	else if ( b.is_notNull() && b->get_info().is_notNull() ) n->set_info(b->get_info());
	return n;
}
static sPtr<pigData> mk_not(sPtr<pigData> a) {   /* 単項 ! = 論理否定 */
	sPtr<pigDataOperatorBnot> n = thNEW(pigDataOperatorBnot,());
	n->pushArg(a);
	if ( a.is_notNull() && a->get_info().is_notNull() ) n->set_info(a->get_info());
	return n;
}

/* if/else → pigfIf(cond, then, [else])。枝は文ノード。 */
#ifndef SRAVA_VALUE_ONLY
static sPtr<pigData> mk_if(sPtr<pigData> cond, sPtr<pigData> thenS, sPtr<pigData> elseS) {
	sPtr<pigDataFunction<pigfIf> > n = thNEW(pigDataFunction<pigfIf>,());
	n->pushArg(cond);
	n->pushArg(thenS);
	if ( elseS.is_notNull() )
		n->pushArg(elseS);
	return n;
}
#else
static sPtr<pigData> mk_if(sPtr<pigData>, sPtr<pigData>, sPtr<pigData>) { return thNULL; }
#endif

/* while (cond) body → pigfWhile(cond, body)。毎周 clone して再評価(pigfWhile 側)。 */
#ifndef SRAVA_VALUE_ONLY
static sPtr<pigData> mk_while(sPtr<pigData> cond, sPtr<pigData> body) {
	sPtr<pigDataFunction<pigfWhile> > n = thNEW(pigDataFunction<pigfWhile>,());
	n->pushArg(cond);
	n->pushArg(body);
	return n;
}
#else
static sPtr<pigData> mk_while(sPtr<pigData>, sPtr<pigData>) { return thNULL; }
#endif

/* for (init; cond; step) body → { init; while (cond) { body; step; } } に desugar。
 * init/step は省略可(thNULL)。ブロックスコープは作らない(既存のブロック=env 共有方針に合わせ、
 * init の var は囲みスコープに入る)。 */
#ifndef SRAVA_VALUE_ONLY
static sPtr<pigData> mk_for(sPtr<pigData> init, sPtr<pigData> cond,
                            sPtr<pigData> step, sPtr<pigData> body) {
	/* while の body = seq(catch_continue(body), step)。catch_continue で continue を握りつぶし、
	 * step を必ず実行する(continue が step を飛ばして無限ループになるのを防ぐ)。break は素通り。 */
	sPtr<pigDataArray> inner = thNEW(pigDataArray,());
	inner->push(mk_catch_continue(body));
	if ( step.is_notNull() )
		inner->push(step);
	sPtr<pigData> wh = mk_while(cond, mk_seq(inner));
	/* 全体 = seq(init, while) */
	sPtr<pigDataArray> outer = thNEW(pigDataArray,());
	if ( init.is_notNull() )
		outer->push(init);
	outer->push(wh);
	return mk_seq(outer);
}
#else
static sPtr<pigData> mk_for(sPtr<pigData>, sPtr<pigData>, sPtr<pigData>, sPtr<pigData>) { return thNULL; }
#endif

/* 四則(整数/実数)。pigDataOperator は args を畳む。 */
static sPtr<pigData> mk_arith(char op, sPtr<pigData> a, sPtr<pigData> b) {
	sPtr<pigDataOperator> n;
	switch ( op ) {
	case '+': n = thNEW(pigDataOperatorAdd,()); break;
	case '-': n = thNEW(pigDataOperatorSub,()); break;
	case '*': n = thNEW(pigDataOperatorMul,()); break;
	default:  n = thNEW(pigDataOperatorDiv,()); break;
	}
	n->pushArg(a);
	n->pushArg(b);
	/* 演算子ノードに行情報を継ぐ(左被演算子優先・無ければ右)。型エラー時に式の行を出すため。 */
	if ( a.is_notNull() && a->get_info().is_notNull() )      n->set_info(a->get_info());
	else if ( b.is_notNull() && b->get_info().is_notNull() ) n->set_info(b->get_info());
	return n;
}

/* 単項マイナス。数値リテラルは即畳み込み(negative literal)→ serialize が "-1" になり VALUE 側でも
 * round-trip 可。それ以外(変数・式)は 0 - x に desugar(既存の減算ノードを再利用)。 */
static sPtr<pigData> mk_neg(sPtr<pigData> a) {
	sPtr<pigDataInteger> ai = sPtr<pigDataInteger>::d_cast(a);
	if ( ai.is_notNull() )
		return thNEW(pigDataInteger,((INTEGER64)(- ai->get_int())));
	sPtr<pigDataFloat> af = sPtr<pigDataFloat>::d_cast(a);
	if ( af.is_notNull() )
		return thNEW(pigDataFloat,((double)(- af->get_flt())));
	return mk_arith('-', thNEW(pigDataInteger,((INTEGER64)0)), a);
}

/* lambda リテラル `\(params){body}` → pigDataLambdaExpr(評価時に env 捕捉)。
 * params=名前文字列の pigDataArray、body=mk_seq した文ブロック。 */
#ifndef SRAVA_VALUE_ONLY
static sPtr<pigData> mk_lambda(sPtr<pigData> params, sPtr<pigData> body) {
	sPtr<pigDataLambdaExpr> n = thNEW(pigDataLambdaExpr,());
	sPtr<pigDataArray> a = sPtr<pigDataArray>::d_cast(params);
	if ( a.is_notNull() )
		for ( int i = 0 ; i < a->length() ; ++i )
			n->push_param(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->get_str());
	n->set_body(body);
	return n;
}
#else
static sPtr<pigData> mk_lambda(sPtr<pigData>, sPtr<pigData>) { return thNULL; }
#endif

/* 関数呼び。名前で builtin を dispatch。 */
#ifndef SRAVA_VALUE_ONLY
static sPtr<pigData> mk_call(sPtr<pigData> name, sPtr<pigData> arglist) {
	const char* nm = name->get_str()->get_str();
	sPtr<pigDataArray> a = sPtr<pigDataArray>::d_cast(arglist);
	int na = a.is_notNull() ? a->length() : 0;
	/* 呼び名トークンのソース位置(エラー ERROR[file,line] 用)。生成する op ノードに刻む。 */
	sPtr<pigInfo> ci = name->get_info();
	/* length(x): array/hash の要素数(値)を返す planner 側 op(agent 不要)。 */
	if ( ::strcmp(nm, "length") == 0 ) {
		sPtr<pigDataOperatorLength> f = thNEW(pigDataOperatorLength,(ci));
		if ( na >= 1 ) f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));
		return f;
	}
	/* float(x): 文字列/整数/浮動小数を浮動小数へ変換(planner 側 op・agent 不要)。 */
	if ( ::strcmp(nm, "float") == 0 ) {
		sPtr<pigDataOperatorToFloat> f = thNEW(pigDataOperatorToFloat,(ci));
		if ( na >= 1 ) f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));
		return f;
	}
	/* int(x): 文字列/浮動小数/整数を整数へ変換(planner 側 op・agent 不要・浮動小数は 0 方向へ切り捨て)。 */
	if ( ::strcmp(nm, "int") == 0 ) {
		sPtr<pigDataOperatorToInt> f = thNEW(pigDataOperatorToInt,(ci));
		if ( na >= 1 ) f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));
		return f;
	}
	/* concat(a, b, ...): 配列連結(planner 側 op・agent 不要)。配列は要素展開、非配列は 1 要素追加。 */
	if ( ::strcmp(nm, "concat") == 0 ) {
		sPtr<pigDataOperatorConcat> f = thNEW(pigDataOperatorConcat,(ci));
		for ( int i = 0 ; i < na ; ++i )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		return f;
	}
	/* print(x, ...): 各引数を print() ゲートウェイで解決して表示、最後の値を返す(passthrough)。
	 * planner 側 operator(agent 不要)。print() は遅延ノードでは compact ゲート → 未解決なら yield
	 * (_start 再走)。pigDataPair::print() が mesh の継続(promise)を辿るので、mesh を print すると
	 * agent 完了後に pigDataCache のハッシュファイル名(パス)が出る。値はそのまま表示。 */
	if ( ::strcmp(nm, "print") == 0 ) {
		sPtr<pigDataOperatorPrint> f = thNEW(pigDataOperatorPrint,(ci));
		for ( int i = 0 ; i < na ; ++i )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		return f;
	}
	/* par(...) は撤去。配列リテラル [a,b,c] が要素を並列評価する(pigDataOperatorArray)ので等価。 */
	/* print_async(x, ...): async への唯一のシュガー。設計 §5.1 の desugar:
	 *   async { var __pa = [x, ...]; sync: print(__pa[0], ...); }
	 * 引数を配列リテラル(並列)で 1 束縛に hoist し、sync 文で発行順に出力する(計算は並列・出力は順序)。
	 * 素朴な直列 var 展開だと引数が直列 force されて並列性が死ぬので、必ず [..] でまとめる。 */
	if ( ::strcmp(nm, "print_async") == 0 ) {
		sPtr<pigDataOperatorArray> arr = thNEW(pigDataOperatorArray,());
		for ( int i = 0 ; i < na ; ++i )
			arr->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		sPtr<pigData> bodyAssign = mk_assign(PIG_ASSIGN_DEF, thNEW(pigDataString,("__print_async_tmp")), arr);
		sPtr<pigDataOperatorPrint> pn = thNEW(pigDataOperatorPrint,(ci));
		for ( int i = 0 ; i < na ; ++i )
			pn->pushArg( mk_index( mk_varref(thNEW(pigDataString,("__print_async_tmp"))),
			                       thNEW(pigDataInteger,((INTEGER64)i)) ) );
		sPtr<pigcgOperatorAsync> as = thNEW(pigcgOperatorAsync,());
		as->pushArg(bodyAssign);   /* body: var __pa = [args] */
		as->pushArg(pn);           /* sync: print(__pa[0..]) */
		as->set_mode(1);           /* hasSync */
		as->set_info(ci);
		return as;
	}
	/* gate(inp1, inp2): inp1 をそのまま返しつつ、inp1 の計算完了時に inp2 を起動(完了フック)。
	 * tinyState helper pigfGate(mid-life 継続で起動と完了のギャップを跨ぐ)。 */
	if ( ::strcmp(nm, "gate") == 0 ) {
		sPtr<pigDataFunction<pigfGate> > f = thNEW(pigDataFunction<pigfGate>,());
		for ( int i = 0 ; i < na ; ++i )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		f->set_info(ci);
		return f;
	}
	if ( ::strcmp(nm, "export") == 0 ) {
		if ( na >= 2 ) {
			/* export(path, mesh[, unit]): agent op で書き出し。root 観測が継続解決まで待てるよう
			 * pigcgOperatorExport で包む(agent の継続 pair を実値に剥がす)。
			 * 引数は常に 3 本に揃える(path, mesh, unit)。unit 省略時は空文字(= 無単位)。 */
			sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* path */
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)1))));   /* mesh */
			if ( na >= 3 )
				f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)2))));   /* unit */
			else
				f->pushArg(thNEW(pigDataString,("")));                          /* 無単位 */
			f->set_op_name(thNEW(stdString,("export")));
			f->set_out_cache(1);
		f->set_info(ci);
			sPtr<pigcgOperatorExport> e = thNEW(pigcgOperatorExport,());
			e->pushArg(f);
			return e;
		}
		/* export(mesh): ファイル書き出しなしの passthrough(継続を実値に剥がすだけ)。 */
		sPtr<pigcgOperatorExport> e = thNEW(pigcgOperatorExport,());
		for ( int i = 0 ; i < na ; ++i )
			e->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		return e;
	}
	/* export_vox(path, params, mesh…可変): voxel 化して vox.h5 を書く agent op。引数は全部
	 * そのまま渡す(path/params=inline・mesh=cache はフレームワークが型で振り分ける)。export と同じく
	 * pigcgOperatorExport で包み root 観測が完了まで待つ。 */
	if ( ::strcmp(nm, "export_vox") == 0 && na >= 3 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		for ( int i = 0 ; i < na ; ++i )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		f->set_op_name(thNEW(stdString,("export_vox")));
		f->set_out_cache(1);
		f->set_info(ci);
		sPtr<pigcgOperatorExport> e = thNEW(pigcgOperatorExport,());
		e->pushArg(f);
		return e;
	}
	/* export_async(path, mesh[, unit]): async への糖衣。`async { export(path, mesh, unit); }` に desugar。
	 * blocking な export を async body に入れることで非ブロック起動 + 末尾 drain になる(従来の専用
	 * pigcgOperatorExportAsync/asyncExports を統一プリミティブ async に畳む)。複数 export_async は並列、
	 * 完了は flush() かプログラム末尾(drain_async)で待つ。 */
	if ( ::strcmp(nm, "export_async") == 0 && na >= 2 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* path */
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)1))));   /* mesh */
		if ( na >= 3 )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)2))));   /* unit */
		else
			f->pushArg(thNEW(pigDataString,("")));
		f->set_op_name(thNEW(stdString,("export")));   /* agent op 自体は export(書き出し)と同一 */
		f->set_out_cache(1);
		f->set_info(ci);
		sPtr<pigcgOperatorExport> e = thNEW(pigcgOperatorExport,());   /* root 観測(継続→実値) */
		e->pushArg(f);
		sPtr<pigcgOperatorAsync> as = thNEW(pigcgOperatorAsync,());    /* async { export(...); } */
		as->pushArg(e);
		as->set_mode(0);   /* sync 無し */
		as->set_info(ci);
		return as;
	}
	/* flush(): 未完了の async(export_async 含む)を全部待つ明示バリア(planner 側 op・agent 不要)。 */
	if ( ::strcmp(nm, "flush") == 0 ) {
		return thNEW(pigcgOperatorFlush,(ci));
	}
	/* map(array, fn): 配列の各要素に lambda を適用し配列を返す(要素ごと pigfApply・遅延=並列可)。 */
	if ( ::strcmp(nm, "map") == 0 && na == 2 ) {
		sPtr<pigDataFunction<pigfMap> > f = thNEW(pigDataFunction<pigfMap>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* array */
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)1))));   /* fn */
		f->set_info(ci);
		return f;
	}
	/* transpose / cumsum / sum(planner 側 op・1引数・数値/配列)。curve の vectorized 計算の土台。 */
	if ( ::strcmp(nm, "transpose") == 0 && na == 1 ) {
		sPtr<pigDataOperatorTranspose> f = thNEW(pigDataOperatorTranspose,(ci));
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));
		return f;
	}
	if ( ::strcmp(nm, "cumsum") == 0 && na == 1 ) {
		sPtr<pigDataOperatorCumsum> f = thNEW(pigDataOperatorCumsum,(ci));
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));
		return f;
	}
	if ( ::strcmp(nm, "sum") == 0 && na == 1 ) {
		sPtr<pigDataOperatorSum> f = thNEW(pigDataOperatorSum,(ci));
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));
		return f;
	}
	/* 初等関数(planner 側 op・ベクトル化・角度ラジアン)。unary=1引数 / binary=2引数。 */
	{
		static const char* MATH1[] = { "sin","cos","tan","asin","acos","atan","sqrt",
		                               "exp","log","abs","floor","ceil","round","sign", 0 };
		static const char* MATH2[] = { "atan2","pow","mod","min","max", 0 };
		int want = 0;
		for ( int i = 0 ; MATH1[i] ; ++i ) if ( ::strcmp(nm, MATH1[i]) == 0 ) { want = 1; break; }
		if ( ! want ) for ( int i = 0 ; MATH2[i] ; ++i ) if ( ::strcmp(nm, MATH2[i]) == 0 ) { want = 2; break; }
		if ( want && na == want ) {
			sPtr<pigDataOperatorMath> f = thNEW(pigDataOperatorMath,(ci));
			f->set_op_name(thNEW(stdString,(nm)));
			for ( int i = 0 ; i < na ; ++i )
				f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
			return f;
		}
	}
	if ( ::strcmp(nm, "system") == 0 ) {
		/* system(cmd): シェルコマンドを **ts2System で非同期実行**(時間のかかるコマンドでも
		 * イベントループを塞がない)。完了まで待って終了相当を返す。pigfFunction ヘルパ pigfSystem。 */
		sPtr<pigDataFunction<pigfSystem> > f = thNEW(pigDataFunction<pigfSystem>,());
		for ( int i = 0 ; i < na ; ++i )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		return f;
	}
	if ( ::strcmp(nm, "import") == 0 ) {
		/* import(path): 結果は普通のメッシュキャッシュ。引数パスを pigDataFileRef で包み、
		 * プランナーが **ファイル内容ハッシュ**でキャッシュキーを作る(content-addressed)。
		 * agent へは serialize=パスが渡り read_polygon_mesh で読む。 */
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		if ( na >= 1 )
			f->pushArg(thNEW(pigDataFileRef,(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))))));
		f->set_op_name(thNEW(stdString,("import")));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* translate は内部的に (mesh, vector) に統一。translate(m,x,y,z) はここで [x,y,z] に梱包し、
	 * translate(m, v)(v=ベクトル式。演算子 m>>>v もこの形)はそのまま通す。 */
	if ( ::strcmp(nm, "translate") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* mesh */
		if ( na == 4 ) {
			sPtr<pigDataArray> vec = thNEW(pigDataArray,());
			for ( int i = 1 ; i < 4 ; ++i )
				vec->push(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
			f->pushArg(vec);
		} else {                                                      /* (mesh, vector) */
			for ( int i = 1 ; i < na ; ++i )
				f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		}
		f->set_op_name(thNEW(stdString,("translate")));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* offset は球細分化省略可: offset(m, d)(2 引数)は subdiv 既定 1 を補う。offset(m, d, n) はそのまま
	 * (n=3D 球の細分化レベル。大=滑らか・重い。2D は無視)。 */
	if ( ::strcmp(nm, "offset") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* mesh */
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)1))));   /* d */
		if ( na >= 3 )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)2))));   /* subdiv */
		else
			f->pushArg(thNEW(pigDataInteger,((INTEGER64)1)));            /* 既定 subdiv=1 */
		f->set_op_name(thNEW(stdString,("offset")));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* revolve(m[, angle[, segs]]): angle 省略=360(全周)、segs 省略=32(全周の分割数=回転ピッチ)。
	 * 内部は常に (mesh, angle, segs) の 3 引数に統一。 */
	if ( ::strcmp(nm, "revolve") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* mesh */
		f->pushArg( na >= 2 ? a->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))   /* angle */
		                    : sPtr<pigData>(thNEW(pigDataInteger,((INTEGER64)360))) );
		f->pushArg( na >= 3 ? a->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))   /* segs */
		                    : sPtr<pigData>(thNEW(pigDataInteger,((INTEGER64)32))) );
		f->set_op_name(thNEW(stdString,("revolve")));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* rotate は軸省略可: rotate(m, deg)(2 引数)は軸 "z"(2D 面内回転 / 3D は z 軸)を補う。
	 * rotate(m, axis, deg)(3 引数)はそのまま。演算子 m@(deg) も 2 引数経由でここに来る。 */
	if ( ::strcmp(nm, "rotate") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* mesh */
		if ( na == 2 ) {
			f->pushArg(thNEW(pigDataString,("z")));                   /* 既定軸 */
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)1))));  /* deg */
		} else {
			for ( int i = 1 ; i < na ; ++i )                          /* (axis, deg) */
				f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		}
		f->set_op_name(thNEW(stdString,("rotate")));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* scale は内部 (mesh, X)。scale(m,sx,sy,sz) は [sx,sy,sz] に梱包、scale(m,s)(均等)/
	 * scale(m,[sx,sy,sz])/ 演算子 m***s はそのまま通す(計算本体が スカラ/配列 を判別)。 */
	if ( ::strcmp(nm, "scale") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* mesh */
		if ( na == 4 ) {
			sPtr<pigDataArray> vec = thNEW(pigDataArray,());
			for ( int i = 1 ; i < 4 ; ++i )
				vec->push(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
			f->pushArg(vec);
		} else {                                                      /* (mesh, scalar|vector) */
			for ( int i = 1 ; i < na ; ++i )
				f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		}
		f->set_op_name(thNEW(stdString,("scale")));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* 単一引数 union/intersection/combine: 引数が **mesh 配列** なら eval 時に均衡二分木へ分解して
	 * 並列に畳む(pigfArrayFold)。`union(concat(...))` の直列 fold 回避(ユーザ案・実測 ~10x)。
	 * 配列長は実行時にしか分からないのでパース時でなく評価時に木を組む。単一 mesh は素通り(=その mesh)。 */
	if ( ( ::strcmp(nm, "union") == 0 || ::strcmp(nm, "intersection") == 0
	    || ::strcmp(nm, "combine") == 0 ) && na == 1 ) {
		sPtr<pigDataFunction<pigfArrayFold> > f = thNEW(pigDataFunction<pigfArrayFold>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));
		f->set_op_name(thNEW(stdString,(nm)));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* n-ary 可換 op(3 引数以上): recipe_hash 昇順にソート → 均衡二分木へ分解。
	 * union(a,b,c) と union(c,b,a) が同一木に正準化(キャッシュ共有)。 */
	if ( ( ::strcmp(nm, "union") == 0 || ::strcmp(nm, "intersection") == 0
	    || ::strcmp(nm, "combine") == 0 ) && na > 2 ) {
		sArray<sPtr<pigData> > ops;
		for ( int i = 0 ; i < na ; ++i )
			ops.push(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		for ( int i = 1 ; i < na ; ++i )   /* recipe_hash 昇順 挿入ソート(normalize と同じ uint64 比較) */
			for ( int j = i ; j > 0 && (uint64_t)ops[j-1]->recipe_hash() > (uint64_t)ops[j]->recipe_hash() ; --j ) {
				sPtr<pigData> t = ops[j-1]; ops[j-1] = ops[j]; ops[j] = t;
			}
		return build_bintree(nm, ops, 0, na, ci);   /* 二分木ノードは union(…) 呼びの行を指す */
	}
	/* n-ary 非可換 op(difference, 3 引数以上): 左 fold(順序保持)。 */
	if ( ::strcmp(nm, "difference") == 0 && na > 2 ) {
		sArray<sPtr<pigData> > ops;
		for ( int i = 0 ; i < na ; ++i )
			ops.push(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		return build_leftfold(nm, ops, na, ci);
	}
	/* circle / sphere は精度ピッチ省略可。circle(r)→(r, 32 辺)、sphere(r)→(r, subdiv 0)。
	 * circle(r, segs) / sphere(r, subdiv) はそのまま。内部は常に 2 引数 (r, pitch) に統一。 */
	if ( ::strcmp(nm, "circle") == 0 || ::strcmp(nm, "sphere") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* r */
		if ( na >= 2 )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)1))));   /* pitch(segs / subdiv) */
		else
			f->pushArg(thNEW(pigDataInteger,( ::strcmp(nm,"circle")==0 ? (INTEGER64)32 : (INTEGER64)0 )));
		f->set_op_name(thNEW(stdString,(nm)));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* 計測(値返し op): area(m) は mesh 1 個を取り数値を返す。out_cache(0)=値(インライン)出力 →
	 * cgatsAgent が WriterText で保存、プランナが VALUE パースで構造化 → 式で観測可能。 */
	/* 肉厚(値返し): thin_spots(m, t_min [, rays [, cone]]) は mesh + 閾値 + (任意)レイ本数 + (任意)コーン全角(度)
	 * を取り [[x,y,z,thk],..] を返す。rays 省略=25 / cone 省略=45°。agent は常に 4 引数(THIN_IN)で受けるので補う。 */
	if ( ::strcmp(nm, "thin_spots") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* mesh */
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)1))));   /* t_min */
		if ( na > 2 ) f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)2))));   /* rays */
		else          f->pushArg(thNEW(pigDataInteger,((INTEGER64)25)));             /* 既定 rays=25 */
		if ( na > 3 ) f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)3))));   /* cone(度) */
		else          f->pushArg(thNEW(pigDataInteger,((INTEGER64)45)));             /* 既定 cone=45° */
		f->set_op_name(thNEW(stdString,(nm)));
		f->set_out_cache(0);   /* 値返し */
		f->set_info(ci);
		return f;
	}
	/* 近接(値返し・二項): distance/closest/farthest は 2 mesh を取り 値/配列 を返す。out_cache(0)=値出力。 */
	if ( ::strcmp(nm, "distance") == 0 || ::strcmp(nm, "closest") == 0
	  || ::strcmp(nm, "farthest") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		for ( int i = 0 ; i < na ; ++i )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		f->set_op_name(thNEW(stdString,(nm)));
		f->set_out_cache(0);   /* 値返し */
		f->set_info(ci);
		return f;
	}
	if ( ::strcmp(nm, "area") == 0 || ::strcmp(nm, "valid") == 0
	  || ::strcmp(nm, "volume") == 0 || ::strcmp(nm, "perimeter") == 0
	  || ::strcmp(nm, "centroid") == 0 || ::strcmp(nm, "bbox") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* mesh */
		f->set_op_name(thNEW(stdString,(nm)));
		f->set_out_cache(0);   /* 値返し */
		f->set_info(ci);
		return f;
	}
	/* polygon / line: 点列を取る 2D op。2 形式を許す(点の配列 1 個 / 点を別々の引数で)。
	 * polygon=塗り多角形、line=ガイド(開ポリライン)。内部は常に「点の配列 1 個」を agent へ。 */
	if ( ::strcmp(nm, "polygon") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		if ( na == 1 ) {
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));
		} else {
			sPtr<pigDataOperatorArray> pts = thNEW(pigDataOperatorArray,());
			for ( int i = 0 ; i < na ; ++i )
				pts->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
			f->pushArg(pts);
		}
		f->set_op_name(thNEW(stdString,("polygon")));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* line: 2D ガイド(寸法線・開ポリライン)。2 つの形式を許す:
	 *   line([[x0,y0],[x1,y1],...])  点の配列 1 個
	 *   line([x0,y0], [x1,y1], ...)  点を別々の引数で(>= 2 引数 → 1 つの配列に梱包)
	 * 内部は常に「点の配列 1 個」を agent(cgaLine)へ渡す。 */
	if ( ::strcmp(nm, "line") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		if ( na == 1 ) {
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* 既に点の配列 */
		} else {
			sPtr<pigDataOperatorArray> pts = thNEW(pigDataOperatorArray,());   /* 各引数=点 → 配列に梱包 */
			for ( int i = 0 ; i < na ; ++i )
				pts->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
			f->pushArg(pts);
		}
		f->set_op_name(thNEW(stdString,("line")));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* tube(path[, segs]): path=[[[x,y,z],r],...] を 3D 折れ線まわりに掃引した管。segs=断面円の辺数
	 * (精度ピッチ。省略=32)。内部は常に 2 引数 (path, segs)。path は構造 inline 値。 */
	if ( ::strcmp(nm, "tube") == 0 ) {
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));   /* path */
		if ( na >= 2 )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)1))));   /* segs */
		else
			f->pushArg(thNEW(pigDataInteger,((INTEGER64)32)));
		f->set_op_name(thNEW(stdString,("tube")));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	if ( ::strcmp(nm, "box") == 0 || ::strcmp(nm, "prism") == 0
	  || ::strcmp(nm, "pyramid") == 0
	  || ::strcmp(nm, "boxa") == 0     /* boxa([w,h,d]): 寸法を配列(構造 inline)で渡す */
	  || ::strcmp(nm, "rect") == 0     /* 2D プリミティブ */
	  || ::strcmp(nm, "ngon") == 0
	  || ::strcmp(nm, "extrude") == 0  /* 2D→3D */
	  || ::strcmp(nm, "repair") == 0   /* 修復(mesh 1 個→mesh) */
	  || ::strcmp(nm, "section") == 0  /* 3D→2D 断面(mesh, 点, 法線) */
	  || ::strcmp(nm, "mirror") == 0  /* transform 系: mesh + パラメータ */
	  || ::strcmp(nm, "transform") == 0
	  || ::strcmp(nm, "color") == 0   /* 着色: mesh + 色指定(名前/#RRGGBB/[r,g,b]) */
	  || ::strcmp(nm, "union") == 0
	  || ::strcmp(nm, "intersection") == 0 || ::strcmp(nm, "difference") == 0
	  || ::strcmp(nm, "combine") == 0 ) {  /* combine: 交差許容のメッシュ合体(viewer 用・演算子 +++) */
		sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
		for ( int i = 0 ; i < na ; ++i )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		f->set_op_name(thNEW(stdString,(nm)));
		f->set_out_cache(1);
		f->set_info(ci);
		return f;
	}
	/* 登録済みプラグイン op(pig プラグインレジストリ)→ プラグインエージェントノード(pig 層)。
	 * builtin でも lambda でもない名前を、別プロセスのプラグインバイナリへ委ねる。 */
	if ( pigplugin_is_op(nm) ) {
		sPtr<pigDataFunction<pigfPluginAgent> > f = thNEW(pigDataFunction<pigfPluginAgent>,());
		for ( int i = 0 ; i < na ; ++i )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		f->set_op_name(thNEW(stdString,(nm)));
		f->set_out_cache( pigplugin_op_out_mesh(nm) ? 1 : 0 );   /* value=0(インライン) / mesh=1 */
		f->set_info(ci);
		return f;
	}
	/* builtin でない名前 → lambda 変数の apply。callee=変数参照(評価で lambda 値)、続けて実引数。 */
	{
		sPtr<pigDataFunction<pigfApply> > f = thNEW(pigDataFunction<pigfApply>,());
		f->set_info(ci);
		f->pushArg(mk_varref(name));
		for ( int i = 0 ; i < na ; ++i )
			f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		return f;
	}
}
#else
[[maybe_unused]] static sPtr<pigData> mk_call(sPtr<pigData>, sPtr<pigData>) { return thNULL; }
#endif

/* 一般の呼び出し `callee(args)`。callee は任意の式。
 *  - callee が変数参照(IDENT)なら名前で dispatch(mk_call: builtin or lambda 変数 apply)。
 *  - それ以外(lambda リテラル直接適用 `(\(x){..})(5)`・別呼び出しの返り値 `f(1)(2)` 等)は
 *    pigfApply に callee 式をそのまま渡す。 */
#ifndef SRAVA_VALUE_ONLY
static sPtr<pigData> mk_apply(sPtr<pigData> callee, sPtr<pigData> arglist) {
	sPtr<pigDataOperatorVariable> vr = sPtr<pigDataOperatorVariable>::d_cast(callee);
	if ( vr.is_notNull() )
		return mk_call(vr->arg(0), arglist);   /* 名前ノード経由(builtin/名前付き lambda) */
	sPtr<pigDataArray> a = sPtr<pigDataArray>::d_cast(arglist);
	int na = a.is_notNull() ? a->length() : 0;
	sPtr<pigDataFunction<pigfApply> > f = thNEW(pigDataFunction<pigfApply>,());
	f->pushArg(callee);
	for ( int i = 0 ; i < na ; ++i )
		f->pushArg(a->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
	return f;
}
#else
static sPtr<pigData> mk_apply(sPtr<pigData>, sPtr<pigData>) { return thNULL; }
#endif

/* transform 系の中置演算子シュガー → 対応する builtin 呼び出しに desugar(mk_call 経由)。
 *   m >>> v       → translate(m, v)
 *   m <> v        → mirror(m, v)
 *   m @ (axis, d) → rotate(m, axis, d)   (右辺は arglist。m を先頭に積む) */
#ifndef SRAVA_VALUE_ONLY
/* transform 演算子を **配列対応** の pigfMapOp ノードにする(broadcast/zip/インスタンス化)。
 * info は被演算子 m の位置を継ぐ(連鎖でも ERROR[file,line] が出るように)。 */
static sPtr<pigData> mk_mapop(const char* op, sPtr<pigData> m, sPtr<pigDataArray> args) {
	sPtr<pigDataFunction<pigfMapOp> > f = thNEW(pigDataFunction<pigfMapOp>,());
	int na = args.is_notNull() ? args->length() : 0;
	for ( int i = 0 ; i < na ; ++i )
		f->pushArg(args->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
	f->set_op_name(thNEW(stdString,(op)));
	f->set_out_cache(1);
	if ( m.is_notNull() && m->get_info().is_notNull() )
		f->set_info(m->get_info());
	return f;
}
static sPtr<pigData> mk_xlate_op(sPtr<pigData> m, sPtr<pigData> v) {
	sPtr<pigDataArray> args = thNEW(pigDataArray,());
	args->push(m); args->push(v);
	return mk_mapop("translate", m, args);
}
static sPtr<pigData> mk_mirror_op(sPtr<pigData> m, sPtr<pigData> v) {
	sPtr<pigDataArray> args = thNEW(pigDataArray,());
	args->push(m); args->push(v);
	return mk_mapop("mirror", m, args);
}
static sPtr<pigData> mk_scale_op(sPtr<pigData> m, sPtr<pigData> v) {
	sPtr<pigDataArray> args = thNEW(pigDataArray,());
	args->push(m); args->push(v);
	return mk_mapop("scale", m, args);
}
static sPtr<pigData> mk_rot_op(sPtr<pigData> m, sPtr<pigData> arglist) {
	sPtr<pigDataArray> al = sPtr<pigDataArray>::d_cast(arglist);
	int na = al.is_notNull() ? al->length() : 0;
	sPtr<pigDataArray> args = thNEW(pigDataArray,());
	args->push(m);
	if ( na == 1 ) {                      /* m @ (deg) → 軸 "z" 既定 */
		args->push(thNEW(pigDataString,("z")));
		args->push(al->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));
	} else {                             /* m @ (axis, deg) */
		for ( int i = 0 ; i < na ; ++i )
			args->push(al->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
	}
	return mk_mapop("rotate", m, args);
}
#else
static sPtr<pigData> mk_xlate_op(sPtr<pigData>, sPtr<pigData>)  { return thNULL; }
static sPtr<pigData> mk_mirror_op(sPtr<pigData>, sPtr<pigData>) { return thNULL; }
static sPtr<pigData> mk_scale_op(sPtr<pigData>, sPtr<pigData>)  { return thNULL; }
static sPtr<pigData> mk_rot_op(sPtr<pigData>, sPtr<pigData>)    { return thNULL; }
#endif

}

%token_type {sPtr<pigData>}
%token_destructor { $$.clear(); }
%extra_argument {sPtr<cgptsLemonParser> periArg}
%parse_failure { periArg->parseFailure(); }
%syntax_error  { periArg->parseFailure(); }

%nonassoc IFX.    /* else なし if(dangling-else 解決: ELSE を shift させる) */
%nonassoc ELSE.
%left OR2.        /* 論理 or: 最弱(mesh ブール ||| より弱く結合。`a|||b && c`=`(a|||b)&&c`) */
%left AND2.       /* 論理 and: || より強く、mesh ブールより弱い */
%left OR3 AND3 SUB3 COMB3.
%left XLATEOP MIRROROP ATOP SCALEOP.   /* transform 系シュガー: mesh ブールより強く結合(operand に効かせる) */
%left EQ NE LT GT LE GE.
%left PLUS MINUS.
%left STAR SLASH.
%right UMINUS BANG.   /* 単項マイナス / 論理否定(二項より強く、後置より弱い) */
%left DOT LBRACK LPAREN.   /* 後置の添字/メンバ/呼び出しが最も強く結合 */

/* 2 入口: 先頭センチネルトークン(レキサが mode に応じて注入)で切替。
 * MODE_PROGRAM → 文の並び(ソース)。MODE_VALUE → 単一値(ワイヤ/キャッシュの値リテラル)。 */
input ::= MODE_PROGRAM stmt_list(L).
		{ periArg->parseAccept(mk_seq(L)); }
input ::= MODE_VALUE value(V).
		{ periArg->parseAccept(V); }

stmt_list(A) ::= .
		{ A = thNEW(pigDataArray,()); }
stmt_list(A) ::= stmt_list(L) stmt(S).
		{ sPtr<pigDataArray>::d_cast(L)->push(S); A = L; }

stmt(A) ::= VAR IDENT(N) ASSIGN arhs(E) SEMI.
		{ A = mk_assign(PIG_ASSIGN_DEF, N, E); }
stmt(A) ::= VAR IDENT(N) SEMI.
		{ A = mk_assign(PIG_ASSIGN_DEF, N, thNULL); }
stmt(A) ::= IDENT(N) ASSIGN arhs(E) SEMI.
		{ A = mk_assign(PIG_ASSIGN_SET, N, E); }

/* 連鎖代入 a = b = c = expr(右結合)。内側 IDENT=... は SET 代入で、評価すると代入先 varref を
 * 返す(pigfAssign の set_result)ので、外側はその値を受け取る → a,b,c すべてに expr の値が入る。
 * expr は一度だけ評価される。 */
arhs(A) ::= expr(E).                  { A = E; }
arhs(A) ::= IDENT(N) ASSIGN arhs(R).  { A = mk_chain_assign(N, R); }
stmt(A) ::= expr_ns(B) LBRACK expr(K) RBRACK ASSIGN expr(V) SEMI.   /* 添字代入 a[i] = v */
		{ A = mk_setindex(B, K, V); }
stmt(A) ::= expr_ns(B) DOT IDENT(N) ASSIGN expr(V) SEMI.            /* メンバ代入 a.key = v */
		{ A = mk_setindex(B, N, V); }
stmt(A) ::= RETURN expr(E) SEMI.    { A = mk_return(E); }       /* return 式 */
stmt(A) ::= RETURN SEMI.            { A = mk_return(thNULL); }   /* return(値なし=null) */
stmt(A) ::= BREAK SEMI.             { A = mk_control(CTRL_BREAK); }
stmt(A) ::= CONTINUE SEMI.          { A = mk_control(CTRL_CONTINUE); }
stmt(A) ::= EXIT expr(E) SEMI.      { A = mk_exit(E); }          /* exit 式(メッセージ)→ プログラム終了 */
stmt(A) ::= EXIT SEMI.              { A = mk_exit(thNULL); }     /* exit(メッセージなし) */
stmt(A) ::= expr_ns(E) SEMI.                      /* 式文。先頭 { 不可(= ブロックに回す) */
		{ A = E; }
stmt(A) ::= LBRACE stmt_list(L) RBRACE.            /* ブロック = シーケンス(env はスコープ共有) */
		{ A = mk_seq(L); }
stmt(A) ::= ASYNC LBRACE stmt_list(L) RBRACE.      /* async 文(sync 無し): body を並列起動 */
		{ A = mk_async(L, thNULL); }
stmt(A) ::= ASYNC LBRACE stmt_list(L) SYNC COLON stmt(S) RBRACE.   /* async 文 + sync:(末尾 1 文・発行順整列) */
		{ A = mk_async(L, S); }
stmt(A) ::= IF LPAREN expr(C) RPAREN stmt(T). [IFX]
		{ A = mk_if(C, T, thNULL); }
stmt(A) ::= IF LPAREN expr(C) RPAREN stmt(T) ELSE stmt(E).
		{ A = mk_if(C, T, E); }
stmt(A) ::= WHILE LPAREN expr(C) RPAREN stmt(B).      /* while ループ。body は通常ブロック */
		{ A = mk_while(C, B); }
stmt(A) ::= FOR LPAREN simplestmt(I) SEMI optcond(C) SEMI simplestmt(S) RPAREN stmt(B).
		{ A = mk_for(I, C, S, B); }      /* for(init;cond;step) body → while へ desugar */

/* for の init/step = セミコロン無しの簡易文(空可)。var 宣言/代入/式。 */
simplestmt(A) ::= .                            { A = thNULL; }
simplestmt(A) ::= VAR IDENT(N) ASSIGN expr(E). { A = mk_assign(PIG_ASSIGN_DEF, N, E); }
simplestmt(A) ::= IDENT(N) ASSIGN expr(E).     { A = mk_assign(PIG_ASSIGN_SET, N, E); }
simplestmt(A) ::= expr(B) LBRACK expr(K) RBRACK ASSIGN expr(V). { A = mk_setindex(B, K, V); }  /* a[i]=v */
simplestmt(A) ::= expr(B) DOT IDENT(N) ASSIGN expr(V).          { A = mk_setindex(B, N, V); }  /* a.key=v */
simplestmt(A) ::= expr(E).                     { A = E; }
/* for の cond は省略可(空=常に真)。 */
optcond(A) ::= .                               { A = thNEW(pigDataInteger,((INTEGER64)1)); }
optcond(A) ::= expr(E).                        { A = E; }

/* ソース式(PROGRAM)。array リテラル `[..]` と hash リテラル `{k:v,..}` を両方持つ。
 * 文頭 `{` のブロック衝突は層化で解消: 式文は expr_ns(先頭 { 不可)を使い、文頭 `{` は
 * 常にブロック(stmt 規則)へ回る。expr(full)は hash プライマリを持ち、代入右辺・引数・添字・
 * 括弧内・二項右辺など「文頭でない位置」で hash を書ける。 */
expr(A) ::= expr(B) OR3(O)  expr(C).   { A = mk_meshop("union",        B, C, opinfo_of(O)); }
expr(A) ::= expr(B) AND3(O) expr(C).   { A = mk_meshop("intersection", B, C, opinfo_of(O)); }
expr(A) ::= expr(B) SUB3(O) expr(C).   { A = mk_meshop("difference",   B, C, opinfo_of(O)); }
expr(A) ::= expr(B) COMB3(O) expr(C).  { A = mk_meshop("combine",      B, C, opinfo_of(O)); }
expr(A) ::= expr(B) XLATEOP  expr(C).  { A = mk_xlate_op(B, C); }   /* m >>> v = translate */
expr(A) ::= expr(B) MIRROROP expr(C).  { A = mk_mirror_op(B, C); }  /* m <> v  = mirror */
expr(A) ::= expr(B) SCALEOP  expr(C).  { A = mk_scale_op(B, C); }   /* m *** s = scale */
expr(A) ::= expr(B) ATOP LPAREN arglist(L) RPAREN.  { A = mk_rot_op(B, L); }  /* m @(axis,d) = rotate */
expr(A) ::= expr(B) PLUS  expr(C).  { A = mk_arith('+', B, C); }
expr(A) ::= expr(B) MINUS expr(C).  { A = mk_arith('-', B, C); }
expr(A) ::= expr(B) STAR  expr(C).  { A = mk_arith('*', B, C); }
expr(A) ::= expr(B) SLASH expr(C).  { A = mk_arith('/', B, C); }
expr(A) ::= MINUS expr(B). [UMINUS] { A = mk_neg(B); }   /* 単項マイナス */
expr(A) ::= expr(B) EQ expr(C).     { A = mk_cmp(0, B, C); }
expr(A) ::= expr(B) NE expr(C).     { A = mk_cmp(1, B, C); }
expr(A) ::= expr(B) LT expr(C).     { A = mk_cmp(2, B, C); }
expr(A) ::= expr(B) GT expr(C).     { A = mk_cmp(3, B, C); }
expr(A) ::= expr(B) LE expr(C).     { A = mk_cmp(4, B, C); }
expr(A) ::= expr(B) GE expr(C).     { A = mk_cmp(5, B, C); }
expr(A) ::= expr(B) OR2  expr(C).   { A = mk_logic(0, B, C); }   /* || 論理 or */
expr(A) ::= expr(B) AND2 expr(C).   { A = mk_logic(1, B, C); }   /* && 論理 and */
expr(A) ::= BANG expr(B). [BANG]    { A = mk_not(B); }           /* ! 論理否定 */
expr(A) ::= expr(B) LBRACK expr(K) RBRACK. { A = mk_index(B, K); }    /* 添字 a[i] */
expr(A) ::= expr(B) DOT IDENT(N).          { A = mk_index(B, N); }    /* メンバ a.key */
expr(A) ::= LPAREN arhs(B) RPAREN.  { A = B; }   /* 括弧内は代入も可: (c = 5) は c に代入し値 5 を返す式 */
expr(A) ::= expr(F) LPAREN arglist(L) RPAREN.  { A = mk_apply(F, L); }   /* 一般呼び出し */
expr(A) ::= IDENT(N).               { A = mk_varref(N); }
expr(A) ::= INT(V).                 { A = V; }
expr(A) ::= FLOAT(V).               { A = V; }
expr(A) ::= STRING(V).              { A = V; }
expr(A) ::= LBRACK arglist(L) RBRACK.  { A = mk_arrayop(L); }   /* array 構築(要素を評価地点 env で解決) */
expr(A) ::= LBRACE RBRACE.             { A = thNEW(pigDataOperatorHash,()); }   /* 空 hash {} */
expr(A) ::= LBRACE hashbody(B) RBRACE. { A = hash_scoped(B); }   /* hash 構築 {k:式,..}。兄弟キー参照可(逐次スコープ) */
/* lambda リテラル `\(a,b){ ... }`。body はブロック(= 文の並びを mk_seq)。先頭 \ なので
 * expr のみで持てば足りる(expr_ns 不要: 文頭 lambda 式文は実用上不要、var f = \... で書ける)。 */
expr(A) ::= LAMBDA LPAREN paramlist(P) RPAREN LBRACE stmt_list(L) RBRACE.
		{ A = mk_lambda(P, mk_seq(L)); }

/* hash リテラル本体。値は full expr(式を書ける)。キーは IDENT か STRING。 */
hashbody(A) ::= hashkey(K) COLON expr(V).
		{ A = hashop_put(thNEW(pigDataOperatorHash,()), K, V); }
hashbody(A) ::= hashbody(L) COMMA hashkey(K) COLON expr(V).
		{ A = hashop_put(L, K, V); }
hashkey(A) ::= IDENT(N).   { A = N; }
hashkey(A) ::= STRING(N).  { A = N; }

/* expr_ns: 文の先頭に来られる式(先頭が `{` でない = hash プライマリを持たない)。
 * expr との差は hash リテラルが無いことだけ。左再帰の左被演算子は expr_ns、それ以外
 * (右被演算子・添字キー・引数等)は full expr。これで文頭 `{` は一意にブロックへ。 */
expr_ns(A) ::= expr_ns(B) OR3(O)  expr(C).   { A = mk_meshop("union",        B, C, opinfo_of(O)); }
expr_ns(A) ::= expr_ns(B) AND3(O) expr(C).   { A = mk_meshop("intersection", B, C, opinfo_of(O)); }
expr_ns(A) ::= expr_ns(B) SUB3(O) expr(C).   { A = mk_meshop("difference",   B, C, opinfo_of(O)); }
expr_ns(A) ::= expr_ns(B) COMB3(O) expr(C).  { A = mk_meshop("combine",      B, C, opinfo_of(O)); }
expr_ns(A) ::= expr_ns(B) XLATEOP  expr(C).  { A = mk_xlate_op(B, C); }
expr_ns(A) ::= expr_ns(B) MIRROROP expr(C).  { A = mk_mirror_op(B, C); }
expr_ns(A) ::= expr_ns(B) SCALEOP  expr(C).  { A = mk_scale_op(B, C); }
expr_ns(A) ::= expr_ns(B) ATOP LPAREN arglist(L) RPAREN.  { A = mk_rot_op(B, L); }
expr_ns(A) ::= expr_ns(B) PLUS  expr(C).  { A = mk_arith('+', B, C); }
expr_ns(A) ::= expr_ns(B) MINUS expr(C).  { A = mk_arith('-', B, C); }
expr_ns(A) ::= expr_ns(B) STAR  expr(C).  { A = mk_arith('*', B, C); }
expr_ns(A) ::= expr_ns(B) SLASH expr(C).  { A = mk_arith('/', B, C); }
expr_ns(A) ::= MINUS expr(B). [UMINUS] { A = mk_neg(B); }   /* 文頭の単項マイナス */
expr_ns(A) ::= expr_ns(B) EQ expr(C).     { A = mk_cmp(0, B, C); }
expr_ns(A) ::= expr_ns(B) NE expr(C).     { A = mk_cmp(1, B, C); }
expr_ns(A) ::= expr_ns(B) LT expr(C).     { A = mk_cmp(2, B, C); }
expr_ns(A) ::= expr_ns(B) GT expr(C).     { A = mk_cmp(3, B, C); }
expr_ns(A) ::= expr_ns(B) LE expr(C).     { A = mk_cmp(4, B, C); }
expr_ns(A) ::= expr_ns(B) GE expr(C).     { A = mk_cmp(5, B, C); }
expr_ns(A) ::= expr_ns(B) OR2  expr(C).   { A = mk_logic(0, B, C); }   /* || 論理 or */
expr_ns(A) ::= expr_ns(B) AND2 expr(C).   { A = mk_logic(1, B, C); }   /* && 論理 and */
expr_ns(A) ::= BANG expr(B). [BANG]       { A = mk_not(B); }           /* 文頭 ! 論理否定 */
expr_ns(A) ::= expr_ns(B) LBRACK expr(K) RBRACK. { A = mk_index(B, K); }
expr_ns(A) ::= expr_ns(B) DOT IDENT(N).          { A = mk_index(B, N); }
expr_ns(A) ::= LPAREN expr(B) RPAREN.  { A = B; }
expr_ns(A) ::= expr_ns(F) LPAREN arglist(L) RPAREN.  { A = mk_apply(F, L); }   /* 一般呼び出し */
expr_ns(A) ::= IDENT(N).               { A = mk_varref(N); }
expr_ns(A) ::= INT(V).                 { A = V; }
expr_ns(A) ::= FLOAT(V).               { A = V; }
expr_ns(A) ::= STRING(V).              { A = V; }
expr_ns(A) ::= LBRACK arglist(L) RBRACK.  { A = mk_arrayop(L); }   /* array 構築(先頭 [ は曖昧でない) */
expr_ns(A) ::= LAMBDA LPAREN paramlist(P) RPAREN LBRACE stmt_list(L) RBRACE.   /* 先頭 \ は曖昧でない */
		{ A = mk_lambda(P, mk_seq(L)); }

/* VALUE モード: ワイヤ/キャッシュの値リテラル(文/ブロックなし → {}=hash 確定で衝突なし)。
 * serialize() の出力をそのまま読み戻せる正準形。array/hash は再帰的に value。 */
value(A) ::= INT(V).     { A = V; }
value(A) ::= FLOAT(V).   { A = V; }
value(A) ::= MINUS INT(V).   { A = mk_neg(V); }     /* 負数リテラル(serialize の "-1" を round-trip) */
value(A) ::= MINUS FLOAT(V). { A = mk_neg(V); }
value(A) ::= STRING(V).  { A = V; }
value(A) ::= IDENT(N).   { A = mk_varref(N); }      /* null/true 等の素トークンも一応(将来) */
value(A) ::= LBRACK vlist(L) RBRACK.       { A = L; }
value(A) ::= LBRACE RBRACE.                { A = thNEW(pigDataHash,()); }
value(A) ::= LBRACE vhash(B) RBRACE.       { A = B; }
vlist(A) ::= .                  { A = thNEW(pigDataArray,()); }
vlist(A) ::= vlist_ne(L).       { A = L; }
vlist_ne(A) ::= value(E).
		{ A = thNEW(pigDataArray,()); sPtr<pigDataArray>::d_cast(A)->push(E); }
vlist_ne(A) ::= vlist_ne(L) COMMA value(E).
		{ sPtr<pigDataArray>::d_cast(L)->push(E); A = L; }
vhash(A) ::= vkey(K) COLON value(V).
		{ A = hash_put(thNEW(pigDataHash,()), K, V); }
vhash(A) ::= vhash(L) COMMA vkey(K) COLON value(V).
		{ A = hash_put(L, K, V); }
vkey(A) ::= IDENT(N).   { A = N; }
vkey(A) ::= STRING(N).  { A = N; }

/* lambda の仮引数名リスト → 名前文字列(IDENT)の pigDataArray。 */
paramlist(A) ::= .                       { A = thNEW(pigDataArray,()); }
paramlist(A) ::= paramlist_ne(L).        { A = L; }
paramlist_ne(A) ::= IDENT(N).
		{ A = thNEW(pigDataArray,()); sPtr<pigDataArray>::d_cast(A)->push(N); }
paramlist_ne(A) ::= paramlist_ne(L) COMMA IDENT(N).
		{ sPtr<pigDataArray>::d_cast(L)->push(N); A = L; }

arglist(A) ::= .                    { A = thNEW(pigDataArray,()); }
arglist(A) ::= arglist_ne(L).       { A = L; }
arglist_ne(A) ::= expr(E).
		{ A = thNEW(pigDataArray,()); sPtr<pigDataArray>::d_cast(A)->push(E); }
arglist_ne(A) ::= arglist_ne(L) COMMA expr(E).
		{ sPtr<pigDataArray>::d_cast(L)->push(E); A = L; }

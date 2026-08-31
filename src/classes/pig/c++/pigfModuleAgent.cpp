/*
 * pigfModuleAgent — pigfAgent の srava 専用派生。状態機械は pigfAgent をそのまま継承し、
 * agent_cmd() のみ override して srava-agent(env SRAVA_AGENT で差し替え可)を供給する。
 *
 * 狙い(ひさレビュー 2026-06-05): pigfAgent は piggybackTurtle 汎用で特定 agent に非依存。
 * 「どの外部プロセスを起動するか」だけを薄い派生クラスに閉じ込めることで、将来 video 編集
 * agent / 巨大テクスチャ agent 等を別派生として足し、同一プランナ内で混在できるようにする。
 *
 * 使い方: pigDataFunction<pigfModuleAgent> ノードを作る(pigDataFunction<pigfAgent> の代わり)。
 */
#include	"pig/c++/pigfAgent.h"
#include	"pig/c++/pigBuildStamp.h"   /* planner/agent の版突き合わせ */
#include	"pig/c++/pigInstallPaths.h"   /* srava_agent のパス解決 (env → exe 相対 → install 既定) */
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"ts2/c++/stdString.h"
/* 基底 pigfAgent_ の sPtr<不完全型> メンバ(ts2System/ptsWirePipe/ts2Parallel/reader/ts2IO)を
 * 派生のデストラクタ実体化で扱うため、完全型を取り込む。 */
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWireCacheStreamReaderText.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/ts2Parallel.h"
#include	"ts2/c++/ts2IO.h"
#include	"pig/c++/pigModuleRegistry.h"   /* .so 化 Phase 2: 記述子登録・カーネル属性クエリ */
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigSigGrammar.h"   /* ★ #3436 P4: sig の文法と照合規則 (単体テスト可能なヘッダ) */
#include	"_ts2/c++/pigfModuleAgent_.h"

#include	<stdlib.h>   /* getenv */
#include	<stdio.h>    /* snprintf */
#include	<string.h>   /* strrchr(ファイル名の basename) */
#include	<strings.h>  /* strcasecmp(DEFAULT_OUTPUT 判定) */
#include	<sys/stat.h> /* stat(カーネル .so の探索) */
#include	"pig/c++/osglue.h"   /* モジュール拡張子 (.so/.dll) とパスリスト区切りの OS 差 */
#include	<string>     /* rev4 B-2b: 型シグネチャ parse */
#include	<vector>
#include	<climits>

#ifndef SRAVA_MODULE_SYSDIR
#define SRAVA_MODULE_SYSDIR "/usr/local/lib/srava/modules"   /* install 既定 (CMake で上書き) */
#endif

CLASS_TINYSTATE(pig/c++/pigfModuleAgent,pig/c++/pigfAgent)

/* ★ rev4 Phase D-1 (2026-08-09): 旧 cgal メタ記述子 (placeholder) の静的自己登録はここから撤去した。
 * planner (srava) は起動時に pigModuleLoader::load_search_path で cgal.so をロードし、そこで実
 * cgatsAgent_descriptor が register_descriptor される (このファイルの placeholder は冗長だった)。
 * cgal.so も実 cgatsAgent も link しない単体テスト (test_pigfagent / test_cgatsagent) 用の最小
 * cgal メタは src/main/cgal_test_fixture.cpp が各 main() から自前登録する。
 * → これで pigfModuleAgent.cpp からカーネル名リテラル "cgal" が完全に消えた (カーネル中立)。 */

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfModuleAgent_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
protected:
	virtual sPtr<stdString>	agent_cmd();
	/* ★ in-process 実行 (#3406 4.3): thread 可能 (exec_caps) かつ既定 (exec_default) が THREAD の
	 * カーネル (manifold) なら name を返し planner 内 thread (ptsMediatorInternal) で実行する。
	 * .so 化 Phase 4c: 旧 env SRAVA_INPROC は撤去。実行方式は descriptor.exec_default +
	 * agent(so,{exec_default}) 上書きで決まる。 */
	virtual sPtr<stdString>	agent_module_name();
	/* ★ pig/srava 境界フック(基底 pigfAgent の汎用フローから virtual で呼ばれる)。
	 *   try_shortcircuit: srava 演算子の単位元 {} 代数で CGAL を呼ばず畳む。
	 *   decide_out_module: 入力カーネル伝播 + 既定カーネル (priority 最大) で CGAL/Manifold を選ぶ。 */
	virtual int		try_shortcircuit();
	/* ★ #3436 P4: n 項ノードを k 項の木へ分解する (docs/sig_grammar_design.md §5)。 */
	virtual int		try_decompose();
	virtual int		decide_out_module();
	int			decide_executor(const char *op);   /* rev4 B-2b: 型ディスパッチ (解決不能 -1) */
	/* routing 不能のエラー文 (入力型 + その op が受け付ける sig の列挙)。 */
	std::string		unroutable_message(const sPtr<pigModuleRegistry> &reg, const char *op);
	/* ★ #3436 P4 §6.2: 引数の種別/個数を op 表 (in[]/nin/variadic) と突き合わせる。合致なら空。 */
	std::string		arg_kind_violation(const sPtr<pigModuleRegistry> &reg, int module_id, const char *op);
private:
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
class stdString;
TS_END_INTERFACE

#endif


pigfModuleAgent_::pigfModuleAgent_(TS_ARGS0)
        : pigfAgent_(parent,_front),
	  parent(tinyState_::parent)
{
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* in-process 実行 (#3406 4.3): thread 可能なカーネルの op は planner 内 thread に切り替える。
 * ★ .so 化 Phase 4c: 実行方式は descriptor.exec_default (+ agent(so,{exec_default}) 上書き) で決定。
 *   旧 env SRAVA_INPROC は撤去。manifold は exec_default=THREAD なので既定で in-proc。
 * ★ .so 化 Phase2-2: 「どのカーネルが thread 可能か」は **exec_caps で表現** (カーネル固有名を消す)。
 *   CGAL は exec_caps に EXEC_THREAD が立たない (=thread 不可) ので自然に External へ落ちる。
 *   thread 可能なら registry の名前 (= pigAgentRegistry のキー) を返す。実行体が未リンクなら
 *   基底 LAUNCH が External へフォールバックする。 */
sPtr<stdString>
pigfModuleAgent_::agent_module_name()
{
	/* ★ .so 化 Phase 4c: SRAVA_INPROC env を撤去。実行方式は **descriptor.exec_default** (+ 言語
	 *   agent(so,{exec_default}) 上書き) で決める。thread 可能かつ既定が THREAD のときだけ in-proc。 */
	sPtr<pigModuleRegistry> reg = ( ptsApp != thNULL ) ? ptsApp->module_registry
	                                                   : sPtr<pigModuleRegistry>(thNULL);   /* ★ #3427 ③ */
	if ( reg == thNULL )
		return thNULL;
	if ( ( reg->exec_caps(outModule) & EXEC_THREAD ) == 0 )
		return thNULL;   /* このカーネルは thread 不可 (CGAL 等) */
	if ( reg->exec_default(outModule) != EXEC_THREAD )
		return thNULL;   /* 既定 process 起動 (agent(so,{exec_default:"process"}) で切替) */
	return thNEW(stdString,(reg->name_of_id(outModule)));
}


/* カーネル名 → .so パスを解決 (.so 化 Phase 3c・探索路 docs §1.3)。存在するものを優先:
 *   ① $SRAVA_MODULE_PATH の各 dir (':' 区切り)   ② agent バイナリと同じ dir (ビルドツリー簡便)
 *   ③ $PREFIX/lib/srava/modules (install 既定)。どれも無ければ ② の形を best-effort で返す
 *   (srava_agent 側が dlopen 失敗を明示エラーにする)。 */
static void
resolve_module_so(const char *agent_bin, const char *kname, char *out, int outsz)
{
	char cand[512];
	struct stat st;

	const char *mp = ::getenv("SRAVA_MODULE_PATH");
	if ( mp != 0 && mp[0] != '\0' ) {
		const char *p = mp;
		while ( *p ) {
			const char *colon = ::strchr(p, OSGLUE_PATHLIST_SEP);
			int len = colon ? (int)(colon - p) : (int)::strlen(p);
			if ( len > 0 && len < (int)sizeof(cand) - 64 ) {
				::snprintf(cand, sizeof cand, "%.*s/%s" OSGLUE_MODULE_SUFFIX, len, p, kname);
				if ( ::stat(cand, &st) == 0 ) { ::snprintf(out, outsz, "%s", cand); return; }
			}
			if ( ! colon ) break;
			p = colon + 1;
		}
	}

	/* ② agent バイナリと同じ dir (build tree では cgal.so/manifold.so が srava_agent と同居)。 */
	const char *slash = ::strrchr(agent_bin, '/');
	if ( slash != 0 ) {
		int dlen = (int)(slash - agent_bin);
		::snprintf(cand, sizeof cand, "%.*s/%s" OSGLUE_MODULE_SUFFIX, dlen, agent_bin, kname);
		if ( ::stat(cand, &st) == 0 ) { ::snprintf(out, outsz, "%s", cand); return; }
	} else {
		::snprintf(cand, sizeof cand, "%s" OSGLUE_MODULE_SUFFIX, kname);   /* agent が相対名のみ = カレント */
		if ( ::stat(cand, &st) == 0 ) { ::snprintf(out, outsz, "%s", cand); return; }
	}

	/* ③ install 既定。 */
	::snprintf(cand, sizeof cand, "%s/%s" OSGLUE_MODULE_SUFFIX, SRAVA_MODULE_SYSDIR, kname);
	if ( ::stat(cand, &st) == 0 ) { ::snprintf(out, outsz, "%s", cand); return; }

	/* best-effort: ② の形 (存在しなくても明示エラー用に返す)。 */
	if ( slash != 0 )
		::snprintf(out, outsz, "%.*s/%s" OSGLUE_MODULE_SUFFIX, (int)(slash - agent_bin), agent_bin, kname);
	else
		::snprintf(out, outsz, "%s/%s" OSGLUE_MODULE_SUFFIX, SRAVA_MODULE_SYSDIR, kname);
}

/* srava_agent を起動。テスト/差し替え用に env SRAVA_AGENT があればそれを優先。
 * 未定義なら install 先の既定 /usr/local/bin/srava_agent(cmake --install で配置)。 */
sPtr<stdString>
pigfModuleAgent_::agent_cmd()
{
	/* ★ .so 化 Phase 3c: 起動は **単一 srava_agent + カーネル .so 引数** に集約 (旧 srava_agent /
	 *   srava_agent_mf の 2 択を廃止・docs §1.2)。outModule を .so 名に写像し resolve_module_so で
	 *   パスを解く。agent バイナリは env SRAVA_AGENT 優先・未定義なら install 先。 */
	/* ★ #3431: env SRAVA_AGENT → <実行体と同じ dir>/srava_agent → configure 時の install 既定、
	 *   の順で解く (pigInstallPaths)。従来は env が無いと **configure 時の prefix** を焼き込んだ
	 *   絶対パスしか見なかったため、install ツリーを別の場所へ置くと自分の兄弟の agent ではなく
	 *   その機械の /usr/local の agent を起動していた (版が違えば下の突き合わせで弾かれる)。 */
	const char *cmd = srava_agent_path();
	/* ★ 版の突き合わせ (2026-08-15): planner と agent が別ビルドだと、症状が「素の式が誤ったエラーで
	 *   落ちる」「沈黙ハング」など分かりにくい形で出る。自分のビルド識別子を渡し、agent 側で
	 *   食い違いを検出して即座に終了させる (pigBuildStamp.cpp のコメント参照)。 */
	const char *bstamp = srava_build_stamp();
	const char *kname = ( ptsApp != thNULL && ptsApp->module_registry != thNULL )
	    ? ptsApp->module_registry->name_of_id(outModule) : "delayed";   /* ★ #3427 ③ */
	char sopath[512];
	/* ★ agent へ渡す .so は「**planner が実際に計画に使ったもの**」でなければならない
	 * (2026-08-16 bench が真因として特定)。従来は resolve_module_so() が **agent バイナリの隣**を
	 * 見て解決していたため、planner がビルドツリーの cgal.so で計画したのに agent には
	 * /usr/local の別世代を渡す、という食い違いが起きた。しかも突き合わせが無いので、症状は
	 * 「引数の数が違う op で agent が落ちて planner が待ち続ける」等の分かりにくい形で出る。
	 * レジストリは登録時に出所を控えている (#3425①) ので、それをそのまま渡す。 */
	sopath[0] = '\0';
	if ( ptsApp != thNULL && ptsApp->module_registry != thNULL ) {
		/* ★ 2026-08-28: ここが「planner がこのモジュールに仕事を託す」確定点。以後アンロード不可。 */
		ptsApp->module_registry->mark_used(outModule);
		const char *dp = ptsApp->module_registry->descriptor_path(outModule);
		if ( dp != 0 && dp[0] != '\0' )
			::snprintf(sopath, sizeof sopath, "%s", dp);
	}
	if ( sopath[0] == '\0' )   /* 出所不明 (組込登録・診断用のローカル registry 等) は従来の探索 */
		resolve_module_so(cmd, kname, sopath, sizeof sopath);
	/* 起動コマンドに op 名と元ソース行番号を **引数として** 付ける(agent は無視するが ps/top -c や
	 * agentwatch で「どの op がどの行から走っているか」が見えるようになる)。comm は "srava_agent"。
	 * ts2System は通常文字列を sh -c で起動する。
	 * ★ 2026-08-11: 先頭 '#'(直接 execvp)を **既定** にした。sh 孫が消えてプロセス半減・kill 直達。
	 *   かつて "agent closed before handshake" の間欠 race で見送っていたが、tinyState 側で解消済みと
	 *   判断 (下の #else のコメント参照)。SRAVA_DIRECT_EXEC=0 で従来の sh -c に戻せる。 */
	const char *op = "op";
	sPtr<stdString> opn = ( _front.is_notNull() ) ? _front->get_op_name() : sPtr<stdString>();
	if ( opn.is_notNull() )
		op = opn->get_str();
	int line = ( _front.is_notNull() && _front->get_info().is_notNull() )
	         ? _front->get_info()->get_lineno() : 0;
	/* 元ソースのファイル名(basename)も付ける(agentwatch で「演算名 ファイル名 行番号」表示用)。
	 * include されたファイルの op を区別できる。 */
	const char *fnsrc = "-";
	if ( _front.is_notNull() && _front->get_info().is_notNull()
	     && _front->get_info()->get_filename().is_notNull() ) {
		fnsrc = _front->get_info()->get_filename()->get_str();
		const char *slash = ::strrchr(fnsrc, '/');
		if ( slash != 0 ) fnsrc = slash + 1;   /* basename */
	}
	/* ★ ファイル名を **シェル安全文字 [A-Za-z0-9._-] だけ** に正規化(他は '_')。コマンドは sh -c で
	 *   起動されるので、env ソースの "<source>" のように '<' '>' を含むと **リダイレクトと誤解釈**され、
	 *   agent の stdin が pipe でなくなり "agent closed before handshake" で死ぬ(グロブ '?' '*' も同様)。
	 *   ps/agentwatch 表示用の飾りなので置換で十分(実 .sra 名は通常そのまま残る)。 */
	char fn[128];
	int k = 0;
	for ( const char *q = fnsrc ; *q && k < (int)sizeof(fn) - 1 ; ++q ) {
		char c = *q;
		int ok = ( (c>='A'&&c<='Z') || (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='.' || c=='_' || c=='-' );
		fn[k++] = ok ? c : '_';
	}
	if ( k == 0 ) fn[k++] = '-';
	fn[k] = 0;
	/* 引数の並び: <so> op file line(so=カーネル .so パス=argv[1]・agent が dlopen する。
	 * op/file/line は agentwatch/ps 表示用で agent は無視・line を末尾=数字にしてパースを単純に保つ)。 */
	char buf[1024];
#ifdef _WIN32
	/* Windows: ts2System の sh -c 経路が機能しない(native に sh が無い/CreateProcess の解釈)。
	 * 先頭 '#' で ts2System を **直接 exec(CreateProcess 直起動)** モードにする。以降は空白区切りで
	 * argv 化され argv[0]=agent パス argv[1]=.so。SRAVA_AGENT/.so が空白を含まない前提。 */
	::snprintf(buf, sizeof buf, "#%s %s %s %s %d b=%s", cmd, sopath, op, fn, line, bstamp);
#else
	/* ★ 2026-08-11: POSIX でも **直接 exec を既定** にした (ひさ判断)。
	 *   利点: sh 孫が消えてプロセス半減 + **ts2System が追う子 = 実 agent 本人**になり、
	 *   teardown の待ち (FIN_AGENTWAIT) が sh の wait 挙動に依存せず実 agent に直達する。
	 *   見送っていた理由 ("agent closed before handshake" の間欠 race) は tinyState 側で解消済みと
	 *   判断 (ctest 220/220 を両モード 10 巡ずつ・race 0 件、重い op の SIGTERM 撤収も確認)。
	 *   SRAVA_DIRECT_EXEC=0 で従来の sh -c に戻せる (race 再発時の切り分け用)。 */
	{
		const char *de = ::getenv("SRAVA_DIRECT_EXEC");
		if ( de != 0 && de[0] == '0' )
			::snprintf(buf, sizeof buf, "%s %s %s %s %d b=%s", cmd, sopath, op, fn, line, bstamp);
		else
			::snprintf(buf, sizeof buf, "#%s %s %s %s %d b=%s", cmd, sopath, op, fn, line, bstamp);
	}
#endif
	return thNEW(stdString,(buf));
}


/* fold 単位元 {}(空ハッシュ)判定。継続(mesh; car=="delayed")は car() 覗き見で非ブロッキングに弾く。
 * 空ハッシュ = 単位元(型分離: `{}`=単位元 / `[]`(配列)=コレクション)。 */
static int srava_is_identity(sPtr<pigData> a)
{
	if ( pig_is_delayed(a) ) return 0;   /* mesh 継続 = 単位元でない */
	sPtr<pigDataHash> h = a->obt_hash();
	return ( h.is_notNull() && h->length() == 0 ) ? 1 : 0;
}

/* ★ srava 演算子の代数的短絡(基底 pigfAgent::ACT_START の旧 1.5 から移設)。
 *   mesh ブール union/intersection/difference/combine の単位元 {}:
 *     a |||/&&&/+++ {} = a,  {} |||/&&&/+++ a = a,  a --- {} = a,  {} --- a = {}(差は左fold)。
 *   export({}) は実体化不能 → 明示エラー。値返し valid({})=0 / volume・area・perimeter({})=0。
 *   → CGAL を呼ばず畳めるので `var acc={}; for(..) acc = acc ||| x;` が書ける。
 *   戻り 0=非該当(agent 起動へ) / 1=_front に結果セット済み / 2=err セット済み。 */
int
pigfModuleAgent_::try_shortcircuit()
{
	sPtr<stdString> opn = agent_op_name();
	const char *op = ( opn != thNULL ) ? opn->get_str() : "";
	int isbool = ( ::strcmp(op,"union")==0 || ::strcmp(op,"intersection")==0
	            || ::strcmp(op,"difference")==0 || ::strcmp(op,"combine")==0 );
	/* ★ mesh を取る op に **配列**が来たら、その場で読めるエラーにする (2026-08-15 bench 提案)。
	 * section(m,P,N) が 3 要素配列を返すようになったので、移行し忘れた `a +++ section(...)` が
	 * 「配列を mesh 演算に渡す」形になる。従来はそのまま agent へ送られて
	 * "pig_value_parse: malformed value" になり、行番号以外に手掛かりが無かった。 */
	if ( ( isbool || ::strcmp(op,"export")==0 || ::strcmp(op,"volume")==0
	    || ::strcmp(op,"area")==0 || ::strcmp(op,"perimeter")==0 || ::strcmp(op,"valid")==0 ) ) {
		for ( int i = 0 ; i < args.length() ; ++i ) {
			if ( pig_is_delayed(args[i]) ) continue;          /* mesh 継続 = 正常 */
			sPtr<pigDataArray> av = args[i]->obt_array();
			if ( av == thNULL ) continue;
			char buf[200];
			::snprintf(buf, sizeof buf,
			    "%s: 配列が来ました (mesh が必要)。section() は 3 要素配列を返します。"
			    "断面 1 枚なら section(m,P,N,0)", op);
			err = thNEW(pigDataError,(buf, _front->get_info()));
			return 2;
		}
	}
	/* ★ #3436 P4: n 項ノードが dispatch に来るようになったので、単位元 {} の短絡も n 項で書く
	 *   (旧実装は 2 項専用。パーサが木に分解していたので 2 項しか来なかった)。
	 *     可換 (union/intersection/combine) … {} を全部落とす。残り 0 個なら {}・1 個ならそれ
	 *     非可換 (difference)               … 左 fold なので args[0] が {} なら {}。それ以外の {} を落とす */
	if ( isbool && args.length() >= 2 ) {
		int isdiff = ( ::strcmp(op,"difference") == 0 );
		if ( isdiff && srava_is_identity(args[0]) ) {   /* {} --- a --- b = {} */
			_front->set_result(args[0]);
			return 1;
		}
		sArray<sPtr<pigData> > keep;
		for ( int i = 0 ; i < args.length() ; ++i )
			if ( ! srava_is_identity(args[i]) ) keep.push(args[i]);
		if ( keep.length() != args.length() ) {         /* 単位元があった */
			if ( keep.length() == 0 ) {                 /* 全部 {} → {} */
				_front->set_result(args[0]);
				return 1;
			}
			if ( keep.length() == 1 ) {                 /* 1 個だけ残った → それ自身 */
				_front->set_result(keep[0]);
				return 1;
			}
			/* 2 個以上残った: 単位元を落とした n 項ノードへ置き換える。 */
			sPtr<pigDataFunction<pigfModuleAgent> > f = thNEW(pigDataFunction<pigfModuleAgent>,());
			for ( int i = 0 ; i < keep.length() ; ++i ) f->pushArg(keep[i]);
			f->set_op_name(agent_op_name());
			f->set_out_cache(1);
			f->set_info(_front->get_info());
			_front->set_result(f);
			return 1;
		}
	}
	if ( ::strcmp(op,"export")==0 && args.length() >= 2 && srava_is_identity(args[1]) ) {
		err = thNEW(pigDataError,("export: empty mesh {} cannot be exported", _front->get_info()));
		return 2;
	}
	/* 値返し op に {}: 空集合の自然な値で短絡(ガード `if(valid(acc)==1)` を書けるように)。 */
	if ( args.length() == 1 && srava_is_identity(args[0]) ) {
		if ( ::strcmp(op,"valid")==0 ) {
			_front->set_result(thNEW(pigDataInteger,((INTEGER64)0)));
			return 1;
		}
		if ( ::strcmp(op,"volume")==0 || ::strcmp(op,"area")==0 || ::strcmp(op,"perimeter")==0 ) {
			_front->set_result(thNEW(pigDataFloat,((double)0.0)));
			return 1;
		}
	}
	return 0;
}


/* ★ カーネル選択(#3404・memo 2.1/2.2)。
 *   2.2 入力にキャッシュ(mesh)がある場合 = 入力カーネルから伝播:
 *     - 一つでも CGAL(厳密)があれば CGAL(混在は厳密側に寄せる。float 入力は無損失で厳密昇格される。
 *       唯一の損失方向 exact→float は cast の明示時のみ = ここでは起きない)。
 *     - 全て Manifold なら Manifold。
 *   2.1 入力に mesh キャッシュが無い(leaf primitive: box/sphere 等)= 変数 DEFAULT_OUTPUT に従う。
 *     未設定/不正なら CGAL(後方互換・安全側。Manifold は watertight 前提でサイレント破綻し得るため
 *     明示 opt-in にする)。値="manifold" で Manifold。
 *   引数のカーネルは基底 arg_module() が継続 pair のスタンプ / HIT キャッシュ先頭から非ブロッキングに読む。 */
/* ★ .so 化 Phase2-2: decide_out_module はカーネル固有名 (MODULE_MANIFOLD/CGAL) を名指しせず、
 *   モジュールレジストリで属性を引く。基準となる 2 つの id を registry から取る。 */

/* ─────────────────────────────────────────────────────────────────────
 * rev4 Phase B-2b: 型ディスパッチ (decide_executor)。
 *   routing の一次キーを kernel→型へ。「(op, 入力型[]) → その組を実行できる handler (module)」で振る。
 *   plan 時の入力型は arg_type_set が非ブロッキングに取る (継続の型リストスタンプ / HIT cache の 4CC)。
 * ───────────────────────────────────────────────────────────────────── */

/* 引数 1 個の **候補型集合** (CSV)。型の出どころは **型スタンプだけ**:
 *   継続 (同じプラン内の前段) … pair の car に載るスタンプ
 *   キャッシュ                … pigDataCache::type_stamp() (プランナが生成時に載せた同じ文字列)
 *   値/スカラ                 … 型なし (ディスパッチ対象外)
 *
 * ★ 2026-08-19 (ひさ設計): キャッシュの型を **4CC から引き直すフォールバックを廃止**した。
 *   4CC → 型は「同じ 4CC を複数モジュールが名乗ったら先勝ち」という曖昧さを持つので、
 *   素性の分かっている値の型を決める根拠にならない。実際、それに頼っていたときは
 *   **cold と warm で routing が変わり** (MISS=スタンプ / HIT=4CC)、priority で指定したのとは
 *   別のカーネルが計算してしまう状態だった (しかも答えは正しく見える)。
 *   スタンプが載っていないキャッシュは *stampless=1 を立てて呼び手にエラーを出させる
 *   (「たまたま引けた型」で走らせない)。 */
static std::string
arg_type_set(sPtr<pigData> v, int *stampless = 0)
{
	if ( pig_is_delayed(v) )
		return v->car()->get_str()->get_str();   /* 型名リスト CSV (B-2a スタンプ)。非ブロッキング */
	if ( v->is_cache() ) {
		sPtr<pigDataCache> c = sPtr<pigDataCache>::d_cast(v->compact());
		if ( c.is_notNull() ) {
			sPtr<stdString> st = c->type_stamp();
			if ( st.is_notNull() )
				return std::string(st->get_str());
			/* 値キャッシュ (D_META "TEXT") は型を持たないのが正常 = 型なし扱い。
			 * ストリーム本体 (mesh 等) なのにスタンプが無いのは planner の不整合 → エラー。 */
			if ( stampless != 0 && c->is_stream_cache() )
				*stampless = 1;
		}
		return std::string();
	}
	return std::string();                         /* 値/スカラ = 型なし (ディスパッチ対象外) */
}

/* ★ rev4 Phase C: 型付き import_exts ("stl:cg-mesh3d,svg:cg-cross2d") から ext の **出力型** を取る。
 *   一致 ext の ':' 以降。無型/未一致は空。 */
static std::string
ext_type_in_csv(const char* csv, const char* ext)
{
	if ( csv == 0 || ext == 0 ) return std::string();
	if ( *ext == '.' ) ++ext;
	size_t el = ::strlen(ext);
	for ( const char* p = csv ; *p ; ) {
		const char* c = ::strchr(p, ',');
		size_t seg = c ? (size_t)(c - p) : ::strlen(p);
		const char* colon = (const char*)::memchr(p, ':', seg);
		size_t extlen = colon ? (size_t)(colon - p) : seg;
		if ( extlen == el && ::strncasecmp(p, ext, el) == 0 )
			return colon ? std::string(colon + 1, (p + seg) - (colon + 1)) : std::string();
		if ( ! c ) break;
		p = c + 1;
	}
	return std::string();
}

/* ★ Stage 2 (export sig 化): モジュール m の op sig が入力型 type を受理するか (どれかの sig の
 *   どれかの入力スロット == type)。export の「その mesh を読めるか」を旧 can_read_module の代わりに
 *   **型軸**で判定する (export sig に foreign 入力型を明示列挙してある)。単一 mesh 入力の op 向け。 */
static int
sig_accepts_input(const sPtr<pigModuleRegistry> &reg, int m, const char *op, const std::string& type)
{
	const char *sig = reg->op_sig(m, op);
	if ( sig == 0 || type.empty() ) return 0;
	std::string all = sig; size_t sp = 0;
	while ( sp <= all.size() ) {
		size_t sc = all.find(';', sp);
		std::string one = all.substr(sp, ( sc == std::string::npos ? all.size() : sc ) - sp);
		pigSigLine L; parse_sigline(one, L);
		for ( size_t i = 0 ; i < L.fixed.size() ; ++i )
			if ( L.fixed[i] == type ) return 1;
		for ( size_t i = 0 ; i < L.set.size() ; ++i )   /* ★ 可変部の型集合も入力スロット */
			if ( L.set[i] == type ) return 1;
		if ( sc == std::string::npos ) break;
		sp = sc + 1;
	}
	return 0;
}

/* ★ P2c (cast 型軸化): モジュール m の op sig が出力型 type を **産出**するか (どれかの sig の出力 == type)。
 *   cast は「目標型を産出できるモジュール」へ振るのに使う (旧 module_of_tag(tag_of_type) 撤去)。
 *   各モジュールの cast 出力型は自カーネル型に閉じている (cgal→cg-*・mf→mf-*) ので目標型で一意に決まる。 */
static int
sig_str_produces(const char *sig, const char *type)
{
	if ( sig == 0 || type == 0 || type[0] == '\0' ) return 0;
	std::string all = sig; size_t sp = 0;
	while ( sp <= all.size() ) {
		size_t sc = all.find(';', sp);
		std::string one = all.substr(sp, ( sc == std::string::npos ? all.size() : sc ) - sp);
		pigSigLine L; parse_sigline(one, L);
		if ( L.out == type ) return 1;
		if ( sc == std::string::npos ) break;
		sp = sc + 1;
	}
	return 0;
}

static int
sig_produces(const sPtr<pigModuleRegistry> &reg, int m, const char *op, const char *type)
{
	return sig_str_produces(reg->op_sig(m, op), type);
}

/* ★ P2d (⑤ 型軸化): 型 T を **産出する** module を返す (priority 最大・無ければ -1)。判定は
 *   「その module の *いずれかの* op sig の出力が T か」= op sig の出力集合から導く。cgal は cg-* を
 *   出力し mf を出力しない (mf は読むだけ)・manifold は mf-* を出力する、と sig が disjoint なので
 *   型ごとに産出 module は一意。
 *   ★これは旧 module_of_tag(tag_of_type(T)) の **型軸置換**: 「型 → その型を産む home module」を
 *   codec_tags (owner 表) でなく op sig の出力から導く。routing は入力の *型* (arg_type_set) を読み、
 *   その型を産む module を引く (値に格納された module id を読む旧 arg_module ではない)。 */
static int
module_of_type(const sPtr<pigModuleRegistry> &reg, const char *type)
{
	if ( type == 0 || type[0] == '\0' ) return -1;
	int best = -1; long bestPrio = LONG_MIN;
	int nmod = reg->count();
	for ( int m = 1 ; m < nmod ; ++m ) {
		const srava_module_descriptor *d = reg->descriptor(m);
		if ( d == 0 || d->ops == 0 ) continue;
		for ( int i = 0 ; i < d->n_ops ; ++i ) {
			if ( sig_str_produces(d->ops[i].sig, type) ) {
				long pr = reg->priority(m);
				if ( pr > bestPrio ) { bestPrio = pr; best = m; }
				break;
			}
		}
	}
	return best;
}

/* ★ decide_executor: (op, 入力型集合[]) を実行できる module を返す。解決不能/対象外 = -1
 *   (呼び元 decide_out_module が既存カーネルロジックへフォールバック)。解決時は outTypeList に出力型を memo。
 *   allow_coerce=false: 直接型一致のみ / true: 1 ホップ coerce を許す (2 パスで直接優先)。 */
/* ★ #3436 P4: 型列 insets を受ける照合コア (decide_executor / try_decompose が共有)。
 *   マッチした module id を返す (無ければ -1)。outType にはその行の出力型、foldN には
 *   **fold 形の行なら申告された N** (上限なし = INT_MAX・fold 形でなければ -1) を返す。 */
/* ★ 2026-08-28: wantOut != 0 なら **出力型がそれに一致する行だけ**を候補にする (cast 用)。
 *   0 なら従来どおり入力型の照合のみ。 */
static int
sig_dispatch(const sPtr<pigModuleRegistry> &reg, const char *op,
             const std::vector<std::string>& insets, std::string *outType, int *foldN,
             const char *wantOut = 0)
{
	if ( reg == thNULL ) return -1;
	int nmod = reg->count();
	int best = -1; long bestPrio = LONG_MIN; std::string bestOut; int bestN = -1;
	for ( int m = 1 ; m < nmod ; ++m ) {
		const char *sig = reg->op_sig(m, op);
		if ( sig == 0 ) continue;               /* この module は op 未注釈 */
		std::string all = sig;
		size_t sp = 0;
		while ( sp <= all.size() ) {
			size_t sc = all.find(';', sp);
			std::string one = all.substr(sp, ( sc == std::string::npos ? all.size() : sc ) - sp);
			pigSigLine L; parse_sigline(one, L);
			if ( sigline_matches(L, insets) &&
			     ( wantOut == 0 || L.out == wantOut ) ) {
				long pr = reg->priority(m);
				if ( pr > bestPrio ) {
					bestPrio = pr; best = m; bestOut = L.out;
					bestN = ( L.kind == SK_FOLD ) ? ( L.arity < 0 ? INT_MAX : L.arity ) : -1;
				}
			}
			if ( sc == std::string::npos ) break;
			sp = sc + 1;
		}
	}
	if ( outType != 0 ) *outType = bestOut;
	if ( foldN   != 0 ) *foldN   = bestN;
	return best;
}

int
pigfModuleAgent_::decide_executor(const char *op)
{
	/* 文字列引数でカーネルが決まる op (cast=目標カーネル名・import/export=拡張子) は型では振れない
	 *   → -1 で既存の専用ロジックへ委ねる。 */
	/* ★ 2026-08-19: export_vox はここから外した。可変長は sig の "T..." で表現できるように
	 *   なったので、型ディスパッチで解決できる (専用ロジックが要らない)。 */
	if ( ::strcmp(op, "cast") == 0 || ::strcmp(op, "import") == 0 ||
	     ::strcmp(op, "export") == 0 )
		return -1;

	/* 幾何入力の候補型集合を集める (値/スカラは除外)。 */
	std::vector<std::string> insets;
	for ( int k = 0 ; k < args.length() ; ++k ) {
		int stampless = 0;
		std::string ts = arg_type_set(args[k], &stampless);
		if ( stampless )
			return -2;   /* 型スタンプの無いストリームキャッシュ = 呼び手が明示エラーにする */
		if ( ts.empty() || ts == "value" || ts == "ref" ) continue;   /* ★ 非幾何型は sig の入力に現れない */
		if ( ts.find(',') != std::string::npos )
			return -1;   /* 多候補 (未確定/polymorphic 上流) = 型が絞れない → 保守的にフォールバック */
		insets.push_back(ts);
	}

	sPtr<pigModuleRegistry> reg = ( ptsApp != thNULL ) ? ptsApp->module_registry
	                                                   : sPtr<pigModuleRegistry>(thNULL);   /* ★ #3427 ③ */
	if ( reg == thNULL )
		return -1;
	/* ★ 直接一致のみの単パス (旧 pass 1 の coercion は撤去)。クロスカーネルの受理は各 op の sig に
	 *   foreign 入力型を **明示列挙** する方式へ移行 (cgal は universal reader なので (cg,mf)/(mf,cg) 等を
	 *   直接持つ・all-foreign は自型カーネルが持つので書かない = sig が disjoint で priority 曖昧なし)。 */
	std::string bestOut;
	int best = sig_dispatch(reg, op, insets, &bestOut, 0);
	if ( best >= 0 ) {
		/* ★ 2026-08-19: sig の出力トークンを **そのまま** memo する。以前は "value" を thNULL へ
		 *   畳んでいたが、それだと「値出力だから型が無い」と「型が絞れなかった」が同じ thNULL に
		 *   なり、下流のスタンプが両者を区別できなかった (値キャッシュに mesh 型リストが載っていた)。
		 *   値も参照も **組込モジュール "pig" が申告する型** ("value" / "ref") なので、そのまま載る。 */
		outTypeList = bestOut.empty() ? thNULL : thNEW(stdString,(bestOut.c_str()));
		return best;
	}
	return -1;   /* 型ディスパッチで解決できず → 既存ロジックへ */
}

/* ─────────────────────────────────────────────────────────────────────
 * ★ #3436 P4: n 項ノードの **評価時**分解 (docs/sig_grammar_design.md §5)
 *
 *   n 項ノードが dispatch に来る
 *     (a) n 項のまま受けられるモジュールがある → そのまま投げる (= 0 を返す)
 *     (b) 無い → k 項の木に分解して _front をその根に解決する (= 1 を返す)
 *
 *   k = min( N'  … モジュールの方針 (module(so,{arity:k}) / 記述子・既定 2)
 *            N   … op の sig が申告する上限 (**正しさ**の上限)
 *            群を受けられる執行者が許す最大 )
 *   ★ N' は「最大」であって「固定」ではないので、受け手が居なければ群を縮めて引き直す (最小 2)。
 *     別カーネルへ黙って逃げるフォールバックではない (申告された能力の内側で項数を決めるだけ)。
 * ───────────────────────────────────────────────────────────────────── */

/* この op を **fold 形かつ固定部なし**で申告しているモジュールがあるか (§5.1)。
 * ★ op 名による判定 (strcmp(nm,"union") 等) はこれで全廃した。 */
static bool
op_is_decomposable(const sPtr<pigModuleRegistry> &reg, const char *op)
{
	if ( reg == thNULL ) return false;
	int nmod = reg->count();
	for ( int m = 1 ; m < nmod ; ++m ) {
		const char *sig = reg->op_sig(m, op);
		if ( sig == 0 ) continue;
		std::string all = sig; size_t sp = 0;
		while ( sp <= all.size() ) {
			size_t sc = all.find(';', sp);
			std::string one = all.substr(sp, ( sc == std::string::npos ? all.size() : sc ) - sp);
			pigSigLine L; parse_sigline(one, L);
			if ( ! L.bad && L.kind == SK_FOLD && L.fixed.empty() ) return true;
			if ( sc == std::string::npos ) break;
			sp = sc + 1;
		}
	}
	return false;
}

/* 均衡 k 分木 (可換 op)。**下から k 個ずつまとめて**上げる (docs/sig_grammar_design.md §5.2:
 *   union(a..h) を k=4 で切ると union(union(a,b,c,d), union(e,f,g,h)))。
 *   節点数は (n-1)/(k-1) 程度で、k を上げると単調に減る = 掃引のつまみとして素直。
 * ⚠ k=2 のとき、旧パーサの中央分割とは **端数の組み方だけ**違う (節点数は同じ n-1・深さも同じ)。
 *   n が 2 の冪なら完全に同じ木。中間キャッシュのキーが動くだけで、計算量は変わらない。 */
static sPtr<pigData>
build_ktree(sPtr<stdString> op, sPtr<pigInfo> info, sArray<sPtr<pigData> >& e, int n, int k)
{
	sArray<sPtr<pigData> > cur;
	cur.length(n);
	for ( int i = 0 ; i < n ; ++i ) cur[i] = e[i];
	while ( cur.length() > 1 ) {
		int m = 0;
		for ( int i = 0 ; i < cur.length() ; i += k ) {
			int take = cur.length() - i;
			if ( take > k ) take = k;
			if ( take == 1 ) {                     /* 端数 1 個はそのまま上の段へ */
				cur[m++] = cur[i];
				continue;
			}
			sPtr<pigDataFunction<pigfModuleAgent> > node = thNEW(pigDataFunction<pigfModuleAgent>,());
			for ( int j = 0 ; j < take ; ++j ) node->pushArg(cur[i + j]);
			node->set_op_name(op);
			node->set_out_cache(1);
			node->set_info(info);
			cur[m++] = node;                       /* ★ 前詰め (m <= i なので読み書きが衝突しない) */
		}
		cur.length(m);
	}
	return cur[0];
}

/* 順序保持の左 fold を k 個ずつ (非可換 op = difference)。
 * ⚠ k=2 のとき旧 build_leftfold と同じ木 (((a-b)-c)-d)。 */
static sPtr<pigData>
build_kleftfold(sPtr<stdString> op, sPtr<pigInfo> info, sArray<sPtr<pigData> >& e, int n, int k)
{
	sPtr<pigData> acc = thNULL;
	int i = 0;
	while ( i < n ) {
		sPtr<pigDataFunction<pigfModuleAgent> > node = thNEW(pigDataFunction<pigfModuleAgent>,());
		int take;
		if ( acc == thNULL ) {
			take = ( k < n ) ? k : n;
		} else {
			node->pushArg(acc);
			take = k - 1;
			if ( take > n - i ) take = n - i;
		}
		for ( int j = 0 ; j < take ; ++j )
			node->pushArg(e[i + j]);
		i += take;
		node->set_op_name(op);
		node->set_out_cache(1);
		node->set_info(info);
		acc = node;
	}
	return acc;
}

int
pigfModuleAgent_::try_decompose()
{
	sPtr<stdString> opn = agent_op_name();
	const char *op = ( opn != thNULL ) ? opn->get_str() : 0;
	int n = args.length();
	if ( op == 0 || n <= 2 )
		return 0;

	sPtr<pigModuleRegistry> reg = ( ptsApp != thNULL ) ? ptsApp->module_registry
	                                                   : sPtr<pigModuleRegistry>(thNULL);
	if ( reg == thNULL )
		return 0;
	if ( ! op_is_decomposable(reg, op) )
		return 0;

	/* 幾何引数の型列。★ 値引数が 1 つでもあれば「固定部あり」= 分解しない (§5.1)。
	 *   固定引数を各群へ複製すると意味が変わるため (export_vox の path など)。 */
	std::vector<std::string> insets;
	for ( int i = 0 ; i < n ; ++i ) {
		int stampless = 0;
		std::string ts = arg_type_set(args[i], &stampless);
		if ( stampless || ts.empty() || ts == "value" || ts == "ref" )
			return 0;
		if ( ts.find(',') != std::string::npos )
			return 0;                          /* 型が絞れない = 分解の根拠が無い */
		insets.push_back(ts);
	}

	/* (a) n 項のまま投げてよいか。★ 「受けられる」(capability = sig の N) だけでは足りない —
	 *   何項で投げるかは **N' (policy)** が決める。両方が n を許すときだけそのまま投げる。 */
	{
		std::string ot; int foldN = -1;
		int m0 = sig_dispatch(reg, op, insets, &ot, &foldN);
		if ( m0 >= 0 ) {
			int lim0 = reg->arity(m0);                        /* N' */
			if ( foldN >= 2 && foldN < lim0 ) lim0 = foldN;   /* N  */
			if ( n <= lim0 ) return 0;
		}
	}

	/* (b) 群の項数 k を決める。まず 2 項で執行者を引き、その N' と N から上限を取る。
	 * ⚠ 現状の probe は **先頭の型**で行う。全オペランドが同じ型なら厳密で、混成呼び出しでは
	 *   群ごとに型が偏りうる (docs §5.2 / §9-4: 群のサイズ決定は混成でしか出ないので後回し可)。 */
	std::vector<std::string> probe(insets.begin(), insets.begin() + 2);
	std::string ot; int foldN = -1;
	int m = sig_dispatch(reg, op, probe, &ot, &foldN);
	if ( m < 0 )
		return 0;                                  /* 2 項でも行き先が無い → 通常経路が明示エラー */
	int lim = reg->arity(m);                       /* N' (policy) */
	if ( foldN >= 2 && foldN < lim ) lim = foldN;  /* N  (capability・正しさの上限) */
	if ( lim > n - 1 ) lim = n - 1;                /* 分解する以上、群は n より必ず小さい */
	int k = lim;
	while ( k > 2 ) {                              /* 受け手が居なければ群を縮めて引き直す */
		std::vector<std::string> pk(insets.begin(), insets.begin() + k);
		if ( sig_dispatch(reg, op, pk, 0, 0) >= 0 ) break;
		--k;
	}
	if ( k < 2 )
		return 0;

	/* 木の形は **可換フラグ**が決める (§5.3)。★ ここは eval 時 (= モジュールが実際に dlopen 済み)
	 * なので op_commutative() は正しい値を返せる。可換なら自前で get_hashkey() 昇順に並べ替える
	 * (旧 normalize() の parse 時ソートに依存していたが、#3452 でモジュール登録が起動時 eager-load
	 * から eval 時の module() 呼び出しへ移り、parse 直後には 1 本もロードされていないため
	 * normalize() 側の op_commutative() は常に false を返す回帰が発生した。normalize() は撤去し、
	 * ここと compute_arg_hash() の 2 箇所へソートを移設する)。
	 * 引数は 1) の is_error() で compact ゲートを通っている (継続は delayed pair) ので
	 * get_hashkey() は非ブロッキングに評価できる (pigfAgent::compute_arg_hash と同じ前提)。 */
	int commutative = reg->op_commutative(op);
	sArray<sPtr<pigData> > e;
	e.length(n);
	for ( int i = 0 ; i < n ; ++i ) e[i] = args[i];
	if ( commutative ) {
		for ( int i = 1 ; i < n ; ++i )
			for ( int j = i ; j > 0 && (uint64_t)( pig_is_delayed(e[j-1]) ? e[j-1]->cdr()->cdr() : e[j-1] )->get_hashkey()
			                        >  (uint64_t)( pig_is_delayed(e[j])   ? e[j]->cdr()->cdr()   : e[j]   )->get_hashkey() ; --j ) {
				sPtr<pigData> t = e[j-1]; e[j-1] = e[j]; e[j] = t;
			}
	}
	sPtr<pigData> root = commutative
	    ? build_ktree(opn, _front->get_info(), e, n, k)
	    : build_kleftfold(opn, _front->get_info(), e, n, k);
	_front->set_result(root);
	return 1;
}


/* ★ #3436 P4 §6.2: 引数の **種別** (幾何か値か) と **個数** を op 表の in[]/nin/variadic と
 *   突き合わせる。合っていれば空文字列。agent 側 (ptsGenericAgent) と同じ判定を planner 側にも
 *   置いて、**計算が走る前に**同じエラーを出す。
 *   幾何かどうかは「型スタンプが読めるか」で見る (arg_type_set が非空 = 幾何・空 = 値/スカラ)。 */
static std::string
arg_kind_violation_impl(const pigOpEntry *e, const std::vector<int>& isGeom, const char *op)
{
	char buf[224];
	int n = (int)isGeom.size();
	if ( n > e->nin && ! e->variadic ) {
		::snprintf(buf, sizeof buf, "%s: too many arguments (takes %d)", op, e->nin);
		return std::string(buf);
	}
	if ( e->variadic ? ( n < e->nin ) : ( n != e->nin ) ) {
		::snprintf(buf, sizeof buf, "%s: expected %d argument(s), got %d", op, e->nin, n);
		return std::string(buf);
	}
	for ( int i = 0 ; i < n ; ++i ) {
		pigArgKind want = ( i < e->nin && e->in != 0 ) ? e->in[i]
		                : ( e->vtail_value ? AK_INLINE : AK_CACHE );   /* ★ 可変部の種別は申告から */
		pigArgKind got  = isGeom[(size_t)i] ? AK_CACHE : AK_INLINE;
		if ( want != got ) {
			::snprintf(buf, sizeof buf, "%s: argument %d should be %s, got %s",
			    op, i + 1,
			    ( want == AK_CACHE ) ? "a mesh" : "a value (number/array)",
			    ( got  == AK_CACHE ) ? "a mesh" : "a value");
			return std::string(buf);
		}
	}
	return std::string();
}

/* ★ 2026-08-19: routing 不能のエラー文。**入力型を名指し**し、さらに **その op を受け付ける
 *   シグネチャを sig から列挙**する。手書きの説明 ("2D には体積が無い" 等) と違い、sig から
 *   機械的に作るので **古びない**し、新しいモジュール/型が載れば自動的に反映される。 */
std::string
pigfModuleAgent_::unroutable_message(const sPtr<pigModuleRegistry> &reg, const char *op)
{
	std::string ts;
	for ( int k = 0 ; k < args.length() ; ++k ) {
		std::string t = arg_type_set(args[k]);
		if ( t.empty() || t == "value" || t == "ref" ) continue;   /* 幾何型のみ挙げる */
		if ( ! ts.empty() ) ts += ",";
		ts += t;
	}
	/* 列挙は **入力の個数が一致する** シグネチャだけに絞る (arity 違いを並べても手掛かりにならない)。 */
	int nin = 0;
	for ( int k = 0 ; k < args.length() ; ++k ) {
		std::string t = arg_type_set(args[k]);
		if ( ! ( t.empty() || t == "value" || t == "ref" ) ) ++nin;
	}
	std::string accepted;
	int nAcc = 0;                       /* 見つかった総数 (表示は先頭 8 件まで) */
	const int ACC_SHOW = 8;
	int nmod = ( reg != thNULL ) ? reg->count() : 0;
	for ( int m = 1 ; m < nmod ; ++m ) {
		const char *sig = reg->op_sig(m, op);
		if ( sig == 0 || sig[0] == '\0' ) continue;
		std::string all = sig;
		size_t sp = 0;
		while ( sp <= all.size() ) {
			size_t sc = all.find(';', sp);
			std::string one = all.substr(sp, ( sc == std::string::npos ? all.size() : sc ) - sp);
			pigSigLine L1; parse_sigline(one, L1);
			/* ★ 可変長 (繰り返し形 / fold 形) は個数が幅を持つので、行の受けうる個数で絞る。 */
			if ( ! one.empty() && sigline_arity_ok(L1, nin)
			     && accepted.find(one) == std::string::npos ) {
				if ( nAcc < ACC_SHOW ) {
					if ( ! accepted.empty() ) accepted += " ";
					accepted += one;
				}
				++nAcc;
			}
			if ( sc == std::string::npos ) break;
			sp = sc + 1;
		}
	}
	char buf[1024];
	if ( accepted.empty() )
		::snprintf(buf, sizeof buf,
		    "no module can execute op '%s' on input types (%s) "
		    "(no module declares this op / not loaded / disabled by module(so,\"off\"))",
		    op, ts.empty() ? "none" : ts.c_str());
	else if ( nAcc > ACC_SHOW )
		::snprintf(buf, sizeof buf,
		    "no module can execute op '%s' on input types (%s) — accepted: %s ... (+%d more)",
		    op, ts.empty() ? "none" : ts.c_str(), accepted.c_str(), nAcc - ACC_SHOW);
	else
		::snprintf(buf, sizeof buf,
		    "no module can execute op '%s' on input types (%s) — accepted: %s",
		    op, ts.empty() ? "none" : ts.c_str(), accepted.c_str());
	return std::string(buf);
}

/* args から「幾何か値か」を作って §6.2 の検査へ渡す (メンバ側)。 */
std::string
pigfModuleAgent_::arg_kind_violation(const sPtr<pigModuleRegistry> &reg, int module_id, const char *op)
{
	const pigOpEntry *e = ( reg != thNULL ) ? reg->op_entry(module_id, op) : 0;
	if ( e == 0 ) return std::string();          /* 未注釈 = 検査しない */
	/* ★ in[] が無い記述子は **引数の種別を何も申告していない** (nin だけあっても意味を持たない)。
	 *   申告が無いものを検査すると、sig だけ書いた最小記述子 (テスト fixture・値専用 op) を
	 *   弾いてしまう。「申告されたものだけ検査する」= 記述子の書き足しで検査が強くなる形にする。 */
	if ( e->in == 0 ) return std::string();
	std::vector<int> isGeom;
	for ( int k = 0 ; k < args.length() ; ++k ) {
		std::string ts = arg_type_set(args[k]);
		isGeom.push_back( ( ts.empty() || ts == "value" ) ? 0 : 1 );
	}
	return arg_kind_violation_impl(e, isGeom, op);
}

int
pigfModuleAgent_::decide_out_module()
{
	/* ★ rev4 Phase C 最終: cgal 万能フォールバック (idCgal) を **撤去**。routing 不能は「無ければ cgal」を
	 *   やめ **明示エラー**にする。decide_executor + coercion が対応 (op,型) を全て解決するので、ここ
	 *   (旧カーネル軸フォールバック) に落ちて解決できないのは真に非対応 = エラーが正。→ この関数から
	 *   カーネル固有名 (idCgal/idMani) が完全に消えた。 */

	sPtr<stdString> opn = agent_op_name();
	const char *op = ( opn != thNULL ) ? opn->get_str() : "";

	/* ★ #3427 ③: レジストリは app 所有。無い (app 未設定 = 想定外) なら routing 不能エラー。 */
	sPtr<pigModuleRegistry> reg = ( ptsApp != thNULL ) ? ptsApp->module_registry
	                                                   : sPtr<pigModuleRegistry>(thNULL);
	if ( reg == thNULL ) {
		char buf[160];
		::snprintf(buf, sizeof buf, "no module registry (no app) for op '%s'", op);
		err = thNEW(pigDataError,(buf, _front->get_info(), 1));   /* fatal */
		return MODULE_NONE;
	}

	/* ★ rev4 Phase C: cast(target_type, mesh) — **目標型**への明示変換 (§9.3-5)。旧 cast("exact"/"manifold")
	 *   のカーネル名指しを廃止 (エイリアス互換なし・ひさ判断)。args[0] = 目標型名 ("cg-mesh3d"/"mf-mesh3d"
	 *   /"cg-cross2d"/"mf-cross2d")。その型をサポートするカーネル (tag→module_of_tag) へ振り、出力型を目標型に固定。
	 *   cast の calc body は identity で、変換 (別カーネル型入力の昇格読み等) は owner のリーダが担う。
	 *   入力型に依存せずカーネルを明示指定するので decide_executor をバイパスする (この block が先行)。 */
	if ( ::strcmp(op, "cast") == 0 && args.length() >= 1 ) {
		sPtr<pigData> tv = args[0]->compact();
		if ( tv.is_notNull() && ! tv->is_error() ) {
			const char *tname = tv->get_str()->get_str();
			/* ★ P2c: cast は「目標型を **産出できる** モジュール」へ振る (旧 module_of_tag(tag) 撤去)。
			 *   cast sig の出力型が目標型に一致するモジュールを選ぶ = 型軸。cast の calc body は
			 *   identity で、foreign 型入力の昇格読みは行き先モジュールの reader が担う。
			 *
			 * ★★ 2026-08-28 (ひさ指摘): ここは出力型 **だけ** を見ていた。sig は「その op が受ける
			 *   幾何引数の型」の申告なのに、cast だけ入力側を読まずに振っていたため、
			 *   **sig が「受けられない」と申告している型が入力に来ても通っていた**
			 *   (manifold の cast sig は (mf-mesh3d)/(mf-cross2d) しか申告していないのに
			 *    cg-mesh3d を受けて動いていた = 申告と実態の食い違いが検出されない)。
			 *   → sig 本来の照合 (入力型) を通し、**かつ** 出力型が目標型である行に限定する。
			 *   ⚠ 入力型が確定しないとき (型スタンプの無いストリーム / 多候補の上流) は照合材料が
			 *     無いので従来どおり出力型だけで振る。その場合も agent 入口の codec 検査
			 *     (ptsGenericAgent の consumable types × reader_for) が読めない形式を明示エラーに
			 *     するので、黙って別の型が返ることはない。 */
			std::vector<std::string> insets;
			int insets_known = 1;
			for ( int k = 0 ; k < args.length() ; ++k ) {
				int stampless = 0;
				std::string ts = arg_type_set(args[k], &stampless);
				if ( stampless || ts.find(',') != std::string::npos ) { insets_known = 0; break; }
				if ( ts.empty() || ts == "value" || ts == "ref" ) continue;   /* 非幾何 (目標型名の文字列を含む) */
				insets.push_back(ts);
			}
			int nmod = reg->count();
			int m = -1;
			if ( insets_known ) {
				m = sig_dispatch(reg, "cast", insets, 0, 0, tname);
			} else {
				for ( int mm = 1 ; mm < nmod ; ++mm )
					if ( sig_produces(reg, mm, "cast", tname) ) { m = mm; break; }
			}
			if ( m >= 0 ) {
				outTypeList = thNEW(stdString,(tname));   /* 出力型 = 目標型 */
				return m;
			}
			/* ★ 目標型を産出できるモジュールは在る = 失敗の原因は **入力型**。両者を切り分けて言う
			 *   (「その型は作れない」と「その型へは変換できない」は利用者の直す場所が違う)。 */
			if ( insets_known ) {
				for ( int mm = 1 ; mm < nmod ; ++mm ) {
					if ( ! sig_produces(reg, mm, "cast", tname) ) continue;
					/* ★ 2026-08-28 (ひさ指摘): ここで **形式 (4CC) を出さない**。planner は
					 * 「その型がディスクに落ちたら何の 4CC になるか」を知っている立場ではない —
					 * in-proc ならその値はまだメモリ上の body でしかなく、シリアライズされる
					 * 保証も無い。形式は pigDataCache の都合であって、routing の言うことではない。
					 * 形式に踏み込んだ診断が要る場面 (実際に読めなかった) では、読む側の
					 * ptsGenericAgent が "cannot convert format 'XXXX'" を出す。 */
					std::string ins;
					for ( size_t i = 0 ; i < insets.size() ; ++i )
						{ if ( i ) ins += ","; ins += insets[i]; }
					char b2[320];
					::snprintf(b2, sizeof b2,
					    "cast: no module declares a conversion from %s to '%s' "
					    "(module '%s' produces '%s' but its cast sig does not accept that input type)",
					    ins.empty() ? "(no geometry input)" : ins.c_str(), tname,
					    reg->name_of_id(mm) ? reg->name_of_id(mm) : "?", tname);
					err = thNEW(pigDataError,(b2, _front->get_info()));
					return MODULE_NONE;
				}
			}
			/* ★ #3439 ①: 産出できるモジュールが無いなら **明示エラー**。
			 *   旧実装はここで一般ロジックへフォールバックしていたが、そうすると cast が
			 *   identity として実行され **要求した型と違う型が黙って返る**:
			 *     cast("zz-mesh3d", box)              誰も申告していない型名なのに通っていた
			 *     module("cgal.so","off") 下で
			 *       cast("cg-mesh3d", mf の箱)        → mf-mesh3d が返っていた
			 *   「表現できないならエラー」(ひさ) に反するので撤廃する。
			 *   ※ args[0] が未解決/エラーのときは下の一般ロジックへ委ねる (引数自体の問題)。 */
			char buf[224];
			::snprintf(buf, sizeof buf,
			    "cast: 型 '%s' を産出できるモジュールが無い "
			    "(型名の誤り / その型を持つモジュールが未ロード / module(so,\"off\") で無効化)",
			    tname);
			err = thNEW(pigDataError,(buf, _front->get_info()));
			return MODULE_NONE;
		}
		/* args[0] が未解決/エラー → 下の一般ロジックへ。 */
	}

	/* ★ import/export: 対象拡張子を扱えないカーネルは万能側 (cgal) に振る (import_exts/export_exts)。
	 *   #3404: mf の write_to/import は STL/OFF だけ。旧実装は export の .svg/.dxf だけ固定で、
	 *   manifold 既定の .3mf が無言で STL 化していた (2026-08-06 修正) + import は拡張子未検査で
	 *   .obj/.ply が失敗する潜在バグがあった → 両方 registry の拡張子申告で対称に塞ぐ。
	 *   引数 path はどちらも args[0]。 */
	/* ★ rev4 Phase C: import(path) を **形式 → (出力型, reader をサポートするカーネル)** で N カーネル routing。
	 *   拡張子を import できるカーネルのうち priority 最大へ。出力型は型付き import_exts から取り継続
	 *   スタンプに載せる (下流が型で振れる = polymorphic import の型不明を解消)。idMani/idCgal 名指し撤去。 */
	if ( ::strcmp(op, "import") == 0 && args.length() >= 1 ) {
		sPtr<pigData> pv = args[0]->compact();
		if ( pv.is_notNull() && ! pv->is_error() ) {
			const char *p = pv->get_str()->get_str();
			const char *dot = ::strrchr(p, '.');
			const char *ext = dot ? dot : "";
			int best = -1; long bestPrio = LONG_MIN; std::string bestType;
			int nmod = reg->count();
			for ( int m = 1 ; m < nmod ; ++m ) {
				if ( reg->can_import_ext(m, ext) != 1 ) continue;   /* 読めない/不明は除外 */
				long pr = reg->priority(m);
				if ( pr > bestPrio ) {
					bestPrio = pr; best = m;
					const srava_module_descriptor *d = reg->descriptor(m);
					bestType = ext_type_in_csv(d ? d->import_exts : 0, ext);
				}
			}
			if ( best >= 0 ) {
				outTypeList = bestType.empty() ? thNULL : thNEW(stdString,(bestType.c_str()));
				return best;
			}
			/* ★ #3439 ⑦: 一般ロジックへ落とさず明示エラー。落とすと leaf → 既定カーネルへ振られ、
			 *   実行時に「読めない」以外の的外れなエラーになりうる (cast と同じ構造の欠陥)。
			 *   ★ ⑦前半で「import op を持つのに import_exts 未申告」はロード時に拒否されるので、
			 *   ここへ来るのは本当にどのモジュールも読めない拡張子だけ。 */
			char buf[288];
			::snprintf(buf, sizeof buf,
			    "import: 拡張子 '%s' を読めるモジュールが無い "
			    "(未対応の形式 / そのモジュールが未ロード / module(so,\"off\") で無効化)",
			    ( ext[0] == '.' ) ? ext + 1 : "(無し)");
			err = thNEW(pigDataError,(buf, _front->get_info()));
			return MODULE_NONE;
		}
	}

	/* ★ rev4 Phase C: export(path, mesh, unit) を **形式 capability × mesh 読解性** で N カーネル routing。
	 *   grammar (ns_sravaParser.y) が export を常に 3 引数 (path, mesh, unit) に正規化するので args[0]=path・
	 *   args[1]=mesh が確定 (export(mesh) 単独は passthrough で routing に来ない)。
	 *   規則: ① mesh の自カーネルが拡張子を書けるならそこ (不要な昇格をしない) ② そうでなければ拡張子を
	 *   書けるカーネルのうち **export sig が mesh の入力型を受理する** (=読める) priority 最大へ (Stage 2 で
	 *   旧 can_read_module を sig 判定に置換)。形式と mesh 次元の整合は cgaExport が実行時に明示エラーにする。 */
	if ( ::strcmp(op, "export") == 0 && args.length() >= 2 ) {
		sPtr<pigData> pv = args[0]->compact();
		if ( pv.is_notNull() && ! pv->is_error() ) {
			const char *p = pv->get_str()->get_str();
			const char *dot = ::strrchr(p, '.');
			const char *ext = dot ? dot : "";
			std::string inType = arg_type_set(args[1]);
			/* ① mesh を **産出する** module (= その型の home) が拡張子を書けるならそこ (不要な昇格をしない)。
			 *   P2d: 旧 arg_module (値に格納された module id を読む) を module_of_type (入力の型 → 産出 module)
			 *   の型軸判定へ置換。自型の mesh 型はその module が自明に読めるので export できる。 */
			int meshK = module_of_type(reg, inType.c_str());
			if ( meshK > 0 && reg->can_export_ext(meshK, ext) == 1 ) {
				outTypeList = thNEW(stdString,("ref"));          /* 出力 = D_REF レコード */
				return meshK;                                   /* ① 自カーネルが書ける */
			}
			/* ② 書ける & mesh を **読める** priority 最大。読解 capability は旧 can_read_module を廃し、
			 *   export sig が入力型を受理するかで判定 (Stage 2・型軸)。入力型が不定/多候補なら保守的に読める扱い。 */
			bool typed = ! inType.empty() && inType.find(',') == std::string::npos;
			int best = -1; long bestPrio = LONG_MIN;
			int nExtOk = 0;   /* 拡張子は書けるが型で弾かれた、を区別するため (#3439 ⑦) */
			int nmod = reg->count();
			for ( int m = 1 ; m < nmod ; ++m ) {
				if ( reg->can_export_ext(m, ext) != 1 ) continue;
				++nExtOk;
				if ( typed && ! sig_accepts_input(reg, m, "export", inType) ) continue;
				long pr = reg->priority(m);
				if ( pr > bestPrio ) { bestPrio = pr; best = m; }
			}
			if ( best >= 0 ) {
				outTypeList = thNEW(stdString,("ref"));          /* 出力 = D_REF レコード */
				return best;
			}
			/* ★ #3439 ⑦: 一般ロジックへ落とさず明示エラー。落とすと mesh の自カーネルへ振られ、
			 *   実行時に「書けない」ではない的外れなエラーになる (実例: cgal を off にして .svg を
			 *   export すると rect が manifold へ落ち、export が "no mesh to write" と言っていた)。
			 *   ★ ⑦前半で「export op を持つのに export_exts 未申告」はロード時に拒否されるので、
			 *   ここへ来るのは本当に扱えない形式か、型が合わない場合だけ。両者は原因が違うので分ける。 */
			char buf[288];
			if ( nExtOk == 0 )
				::snprintf(buf, sizeof buf,
				    "export: 拡張子 '%s' を書けるモジュールが無い "
				    "(未対応の形式 / そのモジュールが未ロード / module(so,\"off\") で無効化)",
				    ( ext[0] == '.' ) ? ext + 1 : "(無し)");
			else
				::snprintf(buf, sizeof buf,
				    "export: 拡張子 '%s' は書けるが、入力の型 '%s' を受け取れるモジュールが無い",
				    ( ext[0] == '.' ) ? ext + 1 : "(無し)", inType.c_str());
			err = thNEW(pigDataError,(buf, _front->get_info()));
			return MODULE_NONE;
		}
	}

	/* ★ rev4 Phase B-2b: 型ディスパッチを先に試す。(op, 入力型[]) が注釈済み handler で確定できれば
	 *   そのモジュールへ (offset の次元・単一モジュールの novel op・既定カーネルを型で統一的に解決)。解決不能 (未注釈 op /
	 *   入力型が多候補で未確定 / cast・import・export) は -1 が返り、下の既存カーネルロジックへフォールバック
	 *   (op 単位 coexistence)。全 op が精密な単一型を伝播できるようになれば下のロジックと名指しは撤去可。 */
	{
		int te = decide_executor(op);
		if ( te >= 0 ) {
			/* ★ #3436 P4 §6.2: モジュールが決まった直後に **引数の種別と個数**を
			 *   in[]/nin/variadic と突き合わせる。従来この検査は agent 側 (ptsGenericAgent) に
			 *   しか無く、**計算が全部走ってから**落ちていた
			 *   (実測: export_vox("h5","t",{dx},box,box) は正しくエラーになるが、その時点で
			 *    box 2 個の cache が完成している)。sig は幾何引数の *型* しか見ないので、
			 *   値引数の取り違えはここでしか捕まらない。 */
			std::string ae = arg_kind_violation(reg, te, op);
			if ( ! ae.empty() ) {
				err = thNEW(pigDataError,(ae.c_str(), _front->get_info(), 1));
				return MODULE_NONE;
			}
			return te;
		}
		if ( te == -2 ) {
			/* ★ 2026-08-19: 型スタンプの無いストリームキャッシュが入力に来た。4CC から型を
			 *   引き直す旧経路は廃止したので、ここは黙って進まず明示エラーにする
			 *   (planner が型を載せ忘れている = 直すべきはこちら側)。 */
			err = thNEW(pigDataError,(
			    "internal: an input cache carries no type stamp (planner did not record the "
			    "planned output type; routing must not guess it from the file format)",
			    _front->get_info(), 1));
			return MODULE_NONE;
		}
	}

	/* ★ 2026-08-19 (ひさ判断): sig で解決できない呼び出しは **ここで明示エラー**にする。
	 *   以前は下の home 伝播へ落とし、入力型を産むモジュールの op 実装まで配送して、実装側に
	 *   親切なエラーを出させていた (案Y の「良いエラー配送」)。しかし全 op が sig を持つように
	 *   なった今、ここへ落ちるのは **本当に実装が無い組み合わせ**だけで (294 テストの実測でも
	 *   2D ||| 3D の 1 件のみ)、フォールバックを残すと「たまたま動く」経路が生き残る。
	 *   エラー文は **入力型を名指しする** — 「2D と 3D は混ぜられない」より一般的だが、
	 *   どの型の組で失敗したかが読めるので、新しい型が増えても文言が古びない。
	 *   ★ 例外は無い。以前は export_vox (可変長) と cast (args[0] 未解決) を外していたが、
	 *   前者は sig の "T..." で表現できるようになり、後者は上の cast block が自分でエラーを出す
	 *   (args[0] が未解決なら、その引数自体のエラーが先に伝播する)。 */
	{
		err = thNEW(pigDataError,(unroutable_message(reg, op).c_str(), _front->get_info(), 1));
		return MODULE_NONE;
	}

	/* ★ #3440: 「op 名を実装するモジュールが 1 つだけならそこへ直送」という経路を **撤去** した
	 *   (ひさ判断 2026-08-17)。ディスパッチは **型** で決まるのが設計であって、「その op の持ち主」
	 *   という概念は srava に無い。名前で振る経路があると、sig の宣言が実態と食い違っていても
	 *   たまたま動いてしまい (nef の minkowski が (mf,cg) の組を書いていないのに通っていた)、
	 *   宣言と実態を一致させる方針 (#3439) が検証不能になる。
	 *   → sig で解決できない呼び出しは、下の home 伝播で行き先が決まらなければ **明示エラー**。
	 *   ★撤去に伴い、sig 未申告だった値専用 op (pipe_proximity の 5 本・demo の 2 本) に
	 *     "->value" を申告させた (幾何型入力を取らない op は入力 0 個の sig で routing される)。 */

	/* ★ 入力型の home module 伝播 (decide_executor が -1 = 未型付/多候補入力のフォールバックのみ)。
	 *   typed 入力 (混在含む) は上の decide_executor が op sig の foreign 入力型で解決済。ここは:
	 *     - 空 (leaf)            → 既定カーネル (priority 最大)
	 *     - 全て同一 home K      → K (その型を産む module へ = 案Y の「良いエラー配送」: 型不一致でも
	 *                              入力型を産む module の op 実装まで届け、実装が親切エラーを出す)
	 *     - 混在                 → 既定カーネル (priority 最大)。
	 *   ★ P2d: 旧 arg_module (値に格納された module id を読む) を module_of_type(arg_type_set(arg))
	 *     (入力の *型* → その型を産む home module) の型軸判定へ置換。 */
	int inModules[16]; int ninMod = 0;
	for ( int k = 0 ; k < args.length() ; ++k ) {
		int ak = module_of_type(reg, arg_type_set(args[k]).c_str());
		if ( ak <= 0 ) continue;                        /* 非 mesh (値/leaf) / 型不明 */
		int seen = 0;
		for ( int j = 0 ; j < ninMod ; ++j ) if ( inModules[j] == ak ) { seen = 1; break; }
		if ( ! seen && ninMod < 16 ) inModules[ninMod++] = ak;
	}

	if ( ::strcmp(op, "export_vox") == 0 )
		outTypeList = thNEW(stdString,("ref"));   /* ★ 出力 = D_REF レコード (sig と同じ・variadic 迂回) */

	int modId;
	if ( ninMod == 0 ) {
		/* leaf: 既定カーネル = priority 最大 (agent("…so",{priority}) で切替・cast で個別指定)。
		 *   モジュール未登録なら modId<=0 → 下でエラー化 (旧 idCgal フォールバックは撤去)。 */
		modId = reg->id_of_name(reg->default_module_name());
	} else if ( ninMod == 1 ) {
		modId = inModules[0];                              /* 全入力同一 home → その module (module_of_type は >0) */
	} else {
		/* 混在 (未型付/多候補で decide_executor が解けなかった稀ケース): 既定カーネル (priority 最大)
		 *   へ寄せる。旧 can_read_module による「全入力を読める候補」絞りは撤去 (typed 混在は上の
		 *   decide_executor が foreign sig で解決済・ここは untyped フォールバックのみ)。 */
		modId = reg->id_of_name(reg->default_module_name());
	}

	/* ★ rev4 Phase C 最終: routing 不能 (対応カーネル無し) は **明示エラー** (旧 cgal 万能フォールバック撤去)。
	 *   ①leaf で既定カーネル無し / ②混在で全入力を読める候補無し = 真に実行できない → 「無ければ cgal」を
	 *   やめてここでエラーにする。③「優先候補が op 未対応」は撤去 = modId そのままで下流 agent の op 検索が
	 *   A_ERROR にする (cgal 名指し不要)。err をセットして MODULE_NONE を返し、基底 ACT_START が ERROR へ。 */
	if ( modId <= 0 ) {
		char buf[160];
		::snprintf(buf, sizeof buf, "no module can execute op '%s' on the given input types", op);
		err = thNEW(pigDataError,(buf, _front->get_info(), 1));   /* fatal */
		return MODULE_NONE;
	}
	/* ★ #3440: 行き先が op を実装していないなら **ここで明示エラー**にする。
	 *   旧実装は modId のまま送り、行き先 agent の op 検索が "unknown op: X" を返していたが、
	 *   これは誤解を招く — op 自体は (別のモジュールに) 存在し、**その入力型の組を受ける sig が
	 *   無い**のが本当の理由だから。sig の書き漏らしを「未知の op」と誤診させない。 */
	if ( reg->supports_op(modId, op) != 1 ) {
		std::string ts;
		for ( int k = 0 ; k < args.length() ; ++k ) {
			std::string t = arg_type_set(args[k]);
			if ( t.empty() ) continue;
			if ( ! ts.empty() ) ts += ",";
			ts += t;
		}
		char buf[288];
		::snprintf(buf, sizeof buf,
		    "no module can execute op '%s' on input types (%s) "
		    "(no sig declares this combination / the module is not loaded / disabled by module(so,\"off\"))",
		    op, ts.empty() ? "none" : ts.c_str());
		err = thNEW(pigDataError,(buf, _front->get_info(), 1));   /* fatal */
		return MODULE_NONE;
	}
	return modId;
}

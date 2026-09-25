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
#include	"pig/c++/pigOpEntry.h"   /* ★ #3554 段1: 行 (op / op#変種) の完全型 */
#include	"pig/c++/pigfApply.h"   /* ★ #3555 段3: 擬似モジュールの match / body (ラムダ適用) */
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
#include	<algorithm>   /* ★ #3477: 内省 op の priority 降順 stable_sort */

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
	/* ★ #3482: 表示用のカーネル名 (実行方式に関係なく返す。agent_module_name との違いは基底の宣言参照)。 */
	virtual sPtr<stdString>	agent_kernel_name();
	/* ★ pig/srava 境界フック(基底 pigfAgent の汎用フローから virtual で呼ばれる)。
	 *   try_shortcircuit: srava 演算子の単位元 {} 代数で CGAL を呼ばず畳む。
	 *   decide_out_module: 入力カーネル伝播 + 既定カーネル (priority 最大) で CGAL/Manifold を選ぶ。 */
	virtual int		try_shortcircuit();
	/* ★ #3436 P4: n 項ノードを k 項の木へ分解する (docs/sig_grammar_design.md §5)。 */
	virtual int		try_decompose();
	/* ★★ #3555 段3: 候補列の中の **擬似モジュール** (定義ハッシュ) を順番どおりに見る。 */
	virtual int		try_pseudo_module();
	virtual int		decide_out_module();
	/* rev4 B-2b: 型ディスパッチ (解決不能 -1)。
	 * ★★ #3555 段1: cands != 0 なら **その並びを候補列**として順に見る (`module::op` の指名)。
	 *   0 なら従来どおり **全モジュール × priority 最大**。#3467 の onlyModule は
	 *   「要素 1 個の候補列」として吸収されたので引数から消えた。
	 *   ★ 指名は sig の代わりではなく候補の絞り込みなので、絞ったうえで sig 照合はそのまま走る。 */
	/* ★ #3554 最後の段 2/5: matchedButSig は **-1 を返したときの言い分け**
	 *   (0 = マッチする行が無い / 1 = 行は在ったが sig が外れた)。要らなければ 0 を渡す。 */
	int			decide_executor(const char *op, const std::vector<int> *cands,
					int *matchedButSig = 0);
	/* ★★ #3555 段2: 予約変数 @USE_MODULES@ の値 (指名が無いときの候補列)。束縛が無ければ thNULL。
	 *   ★ env は親チェーンで引けるので、ブロック / lambda の中の代入が **その中だけ**効く。 */
	sPtr<pigData>		use_modules_var();
	/* routing 不能のエラー文 (入力型 + その op が受け付ける sig の列挙)。 */
	std::string		unroutable_message(const sPtr<pigModuleRegistry> &reg, const char *op);
	/* ★ #3436 P4 §6.2: 引数の種別/個数を op 表 (in[]/nin/variadic) と突き合わせる。合致なら空。 */
	std::string		arg_kind_violation(const sPtr<pigModuleRegistry> &reg, int module_id, const char *op,
				                   const char *rowName = 0);
private:
	/* ★★ 2026-09-18: 配列 1 個の n 項展開 (#3511) を **1 度しか行わない**ための印。
	 *   ⚠⚠ @ACT_START@ は compact ゲートで yield すると **状態の頭から再走する**。
	 *     try_shortcircuit はその 1.5) にあり、**args を書き換える唯一の処理**なので、
	 *     印が無いと再走のたびに 1 段ずつ展開が進む:
	 *         loft_ruled([[a,b]])  1 回目 → args=[[a,b]] (要素 1)  ⇒ 型が合わずエラー
	 *                              2 回目 → args=[a,b]            ⇒ **通ってしまう**
	 *     = 同じ式が「何回再走したか」で別の意味になる。⇒ 状態関数の中身は再入可能でなければ
	 *       ならない、という tinyState の約束に反していた。 */
	int			scArrayExpanded;
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
	  parent(tinyState_::parent),
	  scArrayExpanded(0)
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


/* ★★ #3482: **表示用**のカーネル名。⚠ agent_module_name と違い実行方式を見ない —
 * エラー文の前置き (#3475 の module/op: message) は process 実行のカーネルでも要る。
 * 型ディスパッチ前 (outModule 未確定) は thNULL = 前置き無しで素の文言になる。 */
sPtr<stdString>
pigfModuleAgent_::agent_kernel_name()
{
	sPtr<pigModuleRegistry> reg = ( ptsApp != thNULL ) ? ptsApp->module_registry
	                                                   : sPtr<pigModuleRegistry>(thNULL);
	if ( reg == thNULL || outModule < 0 )
		return thNULL;
	const char *n = reg->name_of_id(outModule);
	return ( n != 0 && n[0] != '\0' ) ? sPtr<stdString>(thNEW(stdString,(n))) : sPtr<stdString>(thNULL);
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
/* ★★ #3511 (2026-09-13): この op を **cache の配列 1 個**で呼べると宣言しているか。
 *   sig の可変部の末尾 "[]" (pigSigGrammar.h) を見る。どれか 1 つのモジュールが宣言していれば真。
 *   ⇒ パーサは op 名を知らないままでよい (従来 union/intersection/combine はパーサに直書きだった)。 */
static int
op_takes_array(const sPtr<pigModuleRegistry> &reg, const char *op)
{
	if ( reg == thNULL || op == 0 || op[0] == '\0' ) return 0;
	int nmod = reg->count();
	for ( int m = 1 ; m < nmod ; ++m ) {
	  /* ★ #3554 段1: ここも「どれかの行が "[]" を持つか」なので **全候補行**を見る。 */
	  for ( int ci = 0 ; ; ++ci ) {
		const pigOpEntry *row = reg->op_row(m, op, ci);
		if ( row == 0 ) break;
		const char *sig = row->sig;
		if ( sig == 0 ) continue;
		std::string all = sig; size_t sp = 0;
		while ( sp <= all.size() ) {
			size_t sc = all.find(';', sp);
			std::string one = all.substr(sp, ( sc == std::string::npos ? all.size() : sc ) - sp);
			pigSigLine L; parse_sigline(one, L);
			if ( ! L.bad && L.array_ok ) return 1;
			if ( sc == std::string::npos ) break;
			sp = sc + 1;
		}
	  }
	}
	return 0;
}

int
pigfModuleAgent_::try_shortcircuit()
{
	sPtr<stdString> opn = agent_op_name();
	const char *op = ( opn != thNULL ) ? opn->get_str() : "";
	/* ★★ #3511 (2026-09-13): **cache の配列 1 個を n 項へ展開する**。
	 *   sig の可変部が "[]" を宣言している op だけ (loft / loft_ruled 等)。
	 *     loft(A)  →  loft(A[0], A[1], …)
	 *   ★ ここでやる理由: パース時にはモジュールが未ロードで sig を引けない。評価時なら
	 *     記述子が読めるので **op 名をパーサに直書きしなくて済む**
	 *     (従来 union/intersection/combine は ns_sravaParser.y に直書きされていた)。
	 *   ★ 展開後は普通の n 項呼び出しと **完全に同じ** — キャッシュキーも routing も
	 *     loft(a,b,c) と一致する (別の書き方が別のキャッシュ実体を作らない)。
	 *   ⚠ 空配列は展開しない (引数 0 個になって sig の arity 検査が読めない形で落ちる)。
	 *   ⚠ 要素が mesh でない配列はそのまま展開し、型の不一致は routing が名指しで断る。 */
	/* ⚠⚠ **1 度しか展開しない** (scArrayExpanded の宣言のところに理由)。この状態は
	 *   compact ゲートの yield で頭から再走するので、印が無いと展開が段々進んでしまう。 */
	if ( /*[cal]*/ args.length() == 1 && ! pig_is_delayed(args[0]) ) {
		sPtr<pigDataArray> av = args[0]->obt_array();
		if ( av != thNULL && av->length() > 0 ) {
			sPtr<pigModuleRegistry> areg = ( ptsApp != thNULL ) ? ptsApp->module_registry
			                                                    : sPtr<pigModuleRegistry>();
			if ( op_takes_array(areg, op) ) {
				int n = av->length();
				scArrayExpanded = 1;
				/* ⚠ sArray は代入できないので、長さを変えてから詰める。
				 *   av は args[0] とは別に握ってあるので、args を書き換えても生きている。 */
				args.length(n);
				for ( int i = 0 ; i < n ; ++i )
					args[i] = av->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
			}
		}
	}
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

/* ⚠ #3554 最後の段 3/5: 旧 ext_type_in_csv (型付き import_exts から出力型を引く) は
 *   ここから消えた。import の専用ブロックが唯一の利用者で、判定は pigModuleRegistry.h の
 *   @pig_ext_out_type@ (マッチ関数と共有) へ一本化してある — **同じ判定を 2 つ持たない**。 */

/* ★ Stage 2 (export sig 化): モジュール m の op sig が入力型 type を受理するか (どれかの sig の
 *   どれかの入力スロット == type)。export の「その mesh を読めるか」を旧 can_read_module の代わりに
 *   **型軸**で判定する (export sig に foreign 入力型を明示列挙してある)。単一 mesh 入力の op 向け。 */
/* ★ #3554 最後の段 4/5 (2026-09-19): routing の利用者は居なくなった (cast/import/export の
 *   専用ブロックが全部 AK_MATCH へ移ったため)。残る利用者は **内省 op `which`** だけ。
 *   ⚠⚠ そのため **行を全部見る**ようにした — @op_sig()@ は *最初の候補行*しか返さないので、
 *     変種行に分かれた op (cast / import) では which が「受け付けない」と嘘をつく。
 *     ⚠ routing が正しくても *診断が嘘をつく*状態は、routing のバグより見つけにくい。 */
static int
sig_accepts_input(const sPtr<pigModuleRegistry> &reg, int m, const char *op, const std::string& type)
{
	if ( type.empty() ) return 0;
	for ( int ci = 0 ; ; ++ci ) {                       /* ★ 候補行を全部見る */
		const pigOpEntry *row = reg->op_row(m, op, ci);
		if ( row == 0 ) break;
		const char *sig = row->sig;
		if ( sig == 0 ) continue;
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
	}
	return 0;
}

/* ★ #3554 最後の段 2/5: モジュール m の op が **どれかの行で** 型 type を産出するか。
 *   ⚠⚠ 旧 sig_produces は @op_sig()@ (= **最初の候補行**の sig) を見ていた。cast が目標型ごとの
 *     変種行に分かれた以上それでは *2 行目以降が見えない* ので、@op_row()@ で 1 行ずつ引く。
 *   ★ いまの用途は **診断で「その型を作れるモジュール」を名指しする**ことだけ (routing は
 *     行のマッチ関数がやる)。 */
static int
any_row_produces(const sPtr<pigModuleRegistry> &reg, int m, const char *op, const char *type)
{
	if ( reg == thNULL || type == 0 || type[0] == '\0' ) return 0;
	for ( int ci = 0 ; ; ++ci ) {
		const pigOpEntry *row = reg->op_row(m, op, ci);
		if ( row == 0 ) break;
		if ( pig_sig_produces(row->sig, type) ) return 1;
	}
	return 0;
}

/* ⚠ #3554 最後の段 5/5 (2026-09-19): ここに在った sig_str_produces / module_of_type
 *   (= 「型 T を産出する module」) を **撤去**した。
 *   ★ 最後の利用者は export の規約① (入力型の home カーネルを優先する) と、その下の
 *     到達不能な home 伝播だけで、4/5 で規約① を撤去した時点で **生きた呼び出しは 0** に
 *     なっていた。⇒ 「型 → その型を産む module」という問いは routing にもう無い
 *     (行き先は priority × sig × マッチ関数だけで決まる)。
 *   ⚠ 診断で「その型を作れるモジュール」を名指しするのは上の any_row_produces の仕事で、
 *     あちらは *op を指定して行を全部見る* 別物 (同じ問いではない)。 */

/* ★ decide_executor: (op, 入力型集合[]) を実行できる module を返す。解決不能/対象外 = -1
 *   (呼び元 decide_out_module が既存カーネルロジックへフォールバック)。解決時は outTypeList に出力型を memo。
 *   allow_coerce=false: 直接型一致のみ / true: 1 ホップ coerce を許す (2 パスで直接優先)。 */
/* ★ #3436 P4: 型列 insets を受ける照合コア (decide_executor / try_decompose が共有)。
 *   マッチした module id を返す (無ければ -1)。outType にはその行の出力型、foldN には
 *   **分解してよい行なら申告された N** (上限なし = INT_MAX) を返す。
 *   ★ #3528: **分解できない行は -1**。fold 形でない行 (固定形・繰り返し形) と、
 *     "(N!)" = 分解禁止の fold 行がこれに当たる。 */
/* ★★ #3554 最後の段 2/5 (2026-09-19): **wantOut は廃止した**。cast 専用の第 5 引数で
 *   「出力型がこれの行だけ」と絞っていたが、その判定は行の **マッチ関数** (pig_match_cast_target)
 *   へ移り、cast も普通の検索 (priority × sig × マッチ) で決まるようになった。
 * ★ matchedButSig != 0 なら **失敗の言い分け**を返す (ひさ 2026-09-19):
 *       0 … マッチする行が **無かった**        (cast なら「その型を産出できるモジュールが無い」)
 *       1 … 行は在ったが **sig が外れた**      (cast なら「その型は作れるが入力型を受けない」)
 *   ⚠ 呼び手が見てよいのは **戻り値が -1 のとき**だけ (成立したときの中身は未定義)。 */
/* ★ #3467: onlyModule >= 0 なら **その module だけ**を候補にする (`module::op` の指名)。
 *   指名は候補の絞り込みであって sig の代わりではないので、絞ってから通常どおり sig を照合する
 *   ⇒ 指名したモジュールの sig が入力型を受けなければ -1 = エラー (暗黙の cast は入れない)。 */
/* ★★ #3554 段2 (2026-09-19): 行の **マッチ関数**を呼ぶ。全スロット真なら 1 (= その行は成立)。
 *   ⚠ 値を待つのは **マッチ関数の中** — どの部分が要るかは関数しか知らず、routing が代わりに
 *     待つと必ず過剰になる (ひさ 2026-09-19)。
 *   ★ ただし *マッチ関数が @compact()@ を明示的に呼ぶ必要は無い* — 値を読む口
 *     (@get_int@ / @get_str@ / @is_error@ …) が既にゲートウェイで、@pigDataDelay@ 側が
 *     @compact()->…@ に委譲している (pigData.h:728-735)。⇒ **読めば自動的に待つ**。
 *     ⚠ 配列 / ハッシュの **要素**は eager に解決されないので、要素を見るなら要素ごとに口を通す。
 *   ⚠ マッチ関数は **純粋**と決めてあるので、候補ごとに何度呼んでも同じ答えになる。
 *   ⚠⚠ 待ちが入ると ACT_START は **頭から再走する**。routing より前 (1.5 try_shortcircuit /
 *     1.6 try_decompose) が再入に耐えることが前提 (2026-09-18 に #3511 で 1 件直した)。 */
static int
row_matches_values(const srava_module_descriptor *d, const pigOpEntry *row,
                   sArray<sPtr<pigData> > *args)
{
	if ( row == 0 || row->match == 0 ) return 1;     /* 無条件の行 */
	/* ⚠ args が無い文脈 (分解の可否を見るだけの照合など) は **引数 0 個と同じ**に扱う
	 *   ⇒ ループが回らず成立する (ひさ 2026-09-19)。*見えなくする*より、甘く見ておいて
	 *     実際の routing で値を見て外すほうが見落としが無い。 */
	int n = ( args != 0 ) ? args->length() : 0;
	for ( int i = 0 ; i < n ; ++i )                  /* ★ 引数 1 個ずつ。可変部の尾部にも当たる */
		if ( ! row->match(d, row, i, (*args)[i]) ) return 0;
	return 1;
}

/* ═════════════════════════════════════════════════════════════════════
 * ★★ #3555 段3 (2026-09-20): **擬似モジュール** — srava のラムダへ配線する候補。
 *
 *   ⚠⚠ これは **値**の話であって **.so** の話ではない (ひさ 2026-09-19)。対応する .so が
 *     無いので、@srava_module_descriptor@ の合成も @register_descriptor@ への登録も、
 *     C の関数ポインタへ繋ぐ trampoline も寿命管理も **全部要らない**。候補列の要素として
 *     *値のまま*扱い、当たったら @body@ を呼んで @_front->set_result@ するだけ。
 *   ★ 登録の口 (@module({type:"pseudo_module",…})@) は **作らない** (ひさ 2026-09-20)。
 *     ハッシュをそのまま配列に置く / 直に指名する。
 *     ⇒ 記述子の **ロード時検査に当たるものが無い** ので、*op 名が一致した行の形が壊れていたら
 *       飛ばさずエラー*にする。飛ばすと「その op を使ったときだけ静かに別のカーネルへ行く」
 *       = 定義の誤りが緑と同じ顔になる。
 *
 *   定義ハッシュ:
 *       { type:"pseudo_module", name:…,
 *         ops:[ { name:"op名", in:[…], variadic:0|1, nreq:…, vtail:"value"|"cache",
 *                 sig:["(a)->b", …], body:\(…){…} }, … ] }
 *
 *   ★★ 欄の仕様 (ひさ 2026-09-21 確定):
 *
 *     モジュール階層
 *       type / priority / *_exts   **読まない** (書いてあっても無視する)
 *       name                       省略可。在れば **エラー表示の札**・無ければ `{pseudo}`
 *                                  ⚠ *指名には使えない* / 実モジュールと同名なら {pseudo} に倒す
 *       ops 無し                   無視 (組込 pig と同じく ops を持たない記述子は在りうる)
 *
 *     行 (ops の 1 要素)
 *       name / body / in           **必須**。無ければ明示エラー
 *       sig                        省略可。**無い / "" / [] の 3 つは同じ = 入力型で絞らない**
 *       nreq                       既定 = in の長さ (全部必須)。nreq..in の長さ が許される個数で、
 *                                  **足りない末尾は null で埋める** ⇒ body は `== null` で読む
 *       variadic                   1 なら in の最後のスロットが繰り返す。body は **配列 1 個**を受ける
 *       vtail                      "value" | "cache" ・既定 "cache" (可変部の種別)
 *       body の引数の数             in の長さ (variadic なら 1) と一致。不一致は明示エラー
 *
 *     行の選択   name ∧ 述語 ∧ sig ∧ **引数の数と種別**。外れたら **次の行** →
 *                同名が尽きたら **黙って通常の routing へ降りる**
 *     違反の報告 routing でも解けなかったときだけ・**候補を全部並べる** (decide_out_module)
 *
 *   in[] のスロット:
 *       "value"  値/スカラ (= AK_INLINE)     "cache"  幾何 (= AK_CACHE)
 *       ラムダ   **述語**。その引数を渡して返り値を見る。種別は問わない
 *
 *   ★ @op#変種@ は作らない — 変種は *記述子側*で「同名 2 行が静かに死ぬ」を避ける仕掛けで、
 *     擬似モジュールは **同名エントリを許す**。行は頭から見るが、**引数の数と種別で振り分かる**
 *     ので「1 行目で必ず止まる」ことはない (それが変種の代わりになる)。
 *   ★ キャッシュは作らない ⇒ ソルトも版も要らない。下流が読むのは *返ってきた値が自分で
 *     持っている型* なので、@body@ の返り値と sig は照合しない (sig は候補選びにだけ使う)。
 * ═════════════════════════════════════════════════════════════════════ */

/* ハッシュからキーを引く。無ければ thNULL。
 * ⚠ @pigDataHash::get_ix@ は **無いキーに pigDataError を返す** ので、それを「無い」に畳む。 */
static sPtr<pigData>
pig_hash_get(sPtr<pigData> h, const char *key)
{
	sPtr<pigDataHash> hh = h.is_notNull() ? h->obt_hash() : sPtr<pigDataHash>(thNULL);
	if ( ! hh.is_notNull() ) return sPtr<pigData>(thNULL);
	sPtr<pigData> v = hh->get_ix(thNEW(pigDataString,(key)));
	if ( ! v.is_notNull() || v->is_error() ) return sPtr<pigData>(thNULL);
	return v;
}

/* ハッシュの整数欄 (無ければ dflt)。 */
static int
pig_hash_int(sPtr<pigData> h, const char *key, int dflt)
{
	sPtr<pigData> v = pig_hash_get(h, key);
	return v.is_notNull() ? (int)v->get_int() : dflt;
}

/* ラムダに args[from..from+n) を渡す **適用ノード** を作る (まだ評価しない)。
 * ★ ラムダ適用の現物は @pigfApply@。@pigDataFunction::_start@ が caller を辿って実態親を
 *   見つけるので、状態関数の中から作ってよい (pigfMap と同じ形)。 */
static sPtr<pigData>
pig_apply_node(sPtr<pigData> fn, sArray<sPtr<pigData> > *args, int from, int n, sPtr<pigInfo> info)
{
	sPtr<pigDataFunction<pigfApply> > app = thNEW(pigDataFunction<pigfApply>,(info));
	app->pushArg(fn);
	for ( int i = 0 ; i < n ; ++i ) app->pushArg((*args)[from + i]);
	return app;
}

/* ops 配列 (無ければ thNULL)。 */
static sPtr<pigDataArray>
pig_pseudo_ops(sPtr<pigData> pseudo)
{
	sPtr<pigData> ov = pig_hash_get(pseudo, "ops");
	return ov.is_notNull() ? ov->obt_array() : sPtr<pigDataArray>(thNULL);
}

/* その擬似モジュールが op 名を宣言しているか (診断の言い分けに使う安いフィルタ)。 */
static int
pig_pseudo_declares_op(sPtr<pigData> pseudo, const char *op)
{
	sPtr<pigDataArray> oa = pig_pseudo_ops(pseudo);
	if ( ! oa.is_notNull() ) return 0;
	for ( int i = 0 ; i < oa->length() ; ++i ) {
		sPtr<pigData> e  = oa->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
		sPtr<pigData> nv = pig_hash_get(e, "name");
		sPtr<stdString> ns = nv.is_notNull() ? nv->get_str() : sPtr<stdString>(thNULL);
		if ( ns.is_notNull() && ::strcmp(ns->get_str(), op) == 0 ) return 1;
	}
	return 0;
}

/* ★★ #3555 (ひさ 2026-09-21 確定): 擬似モジュールの **札** — 診断に出す名前。
 *
 *   欄 `name` は **省略可**。在ればエラー表示の札として使い、無ければ `{pseudo}`。
 *   ⚠ 札は **指名には使えない** (`"glue"::op` と書いても、擬似モジュールは値なので
 *     名前からは引けない)。⇒ *実モジュールと同名なら `{pseudo}` に倒して黙認する* —
 *     倒さずにそのまま出すと「その名前で指名できる」と読まれ、指名すると実モジュールの
 *     方へ行く、という一番たちの悪い食い違いになる。
 *   ★ 2026-09-19 に「名乗らない (エラー文にも出ない)」と決めたのを **改訂**した:
 *     候補列に擬似を 2 つ以上置けるので、どれの話かを言えないと直す場所が指せない。 */
static std::string
pig_pseudo_label(const sPtr<pigModuleRegistry> &reg, sPtr<pigData> pseudo)
{
	sPtr<pigData> nv = pig_hash_get(pseudo, "name");
	sPtr<stdString> ns = nv.is_notNull() ? nv->get_str() : sPtr<stdString>(thNULL);
	if ( ! ns.is_notNull() || ns->get_str()[0] == '\0' ) return std::string("{pseudo}");
	const char *nm = ns->get_str();
	if ( reg != thNULL && reg->id_of_name(nm) > 0 ) return std::string("{pseudo}");
	return std::string(nm);
}

/* ★★ #3555 (ひさ 2026-09-21 確定): 擬似 op の 1 行が **選ばれるか**。
 *     1 = 選ばれる / 0 = 選ばれない (**次の行へ**) / -1 = 定義が壊れている (*emsg に理由)
 *
 *   ⚠⚠ この関数の要は「**選ばれない**」と「**壊れている**」を分けることにある:
 *
 *       選ばれない   引数の数 / 種別 / 述語 / sig が合わない
 *                    ⇒ 次の行 → 同名の行が尽きたら **黙って通常の routing へ降りる**
 *                      (同名 2 行で引数の数を振り分けられる、が (b) の狙い)
 *       壊れている   欄が無い / 綴りが違う / body の引数の数が in[] と食い違う
 *                    ⇒ **明示エラー**。擬似モジュールには記述子のロード時検査に当たるものが
 *                      無いので、ここで言わないと「その op を使ったときだけ静かに別のカーネルへ
 *                      行く」= 定義の誤りが緑と同じ顔になる
 *
 *   欄の仕様 (ひさ 2026-09-21):
 *       name      **必須**。この行がどの op の行かを言う (無ければ判定すらできない)
 *       body      **必須**。ラムダ。引数の数は in[] の長さ (variadic:1 なら 1) と一致
 *       in        **必須**。スロットの並び。"value" / "cache" / 述語ラムダ
 *       sig       省略可 (無い / "" / [] の 3 つは同じ = **入力型で絞らない**)
 *       nreq      省略可 (既定 = in の長さ = 全部必須)。nreq..in の長さ が許される個数で、
 *                 **足りない末尾は null で埋める** ⇒ body は `== null` で省略を読む (#3567)
 *       vtail     "value" | "cache" ・既定 "cache" (可変部の種別)
 *
 *   ⚠ 述語ラムダの適用は **待ちうる** ⇒ ACT_START が頭から再走する (#3554 と同じ前提)。
 *     述語は **純粋**であること — 候補ごとに何度でも呼ばれうる。 */
static int
pig_pseudo_row_fit(sPtr<pigData> row, const char *op,
                   const std::vector<std::string>& insets,
                   sArray<sPtr<pigData> > *args, sPtr<pigInfo> info,
                   int *pnin, int *pvariadic, std::string *emsg)
{
	char buf[288];
	if ( pnin != 0 )      *pnin = 0;
	if ( pvariadic != 0 ) *pvariadic = 0;

	/* ---- 行 name。★ 仕様変更: 無ければ **エラー** (以前は「別の op の行」として飛ばしていた) ---- */
	sPtr<pigData> nv = pig_hash_get(row, "name");
	sPtr<stdString> ns = nv.is_notNull() ? nv->get_str() : sPtr<stdString>(thNULL);
	if ( ! ns.is_notNull() || ns->get_str()[0] == '\0' ) {
		*emsg = "pseudo module: an 'ops' row has no 'name' "
		        "(every row must name the op it implements)";
		return -1;
	}
	if ( ::strcmp(ns->get_str(), op) != 0 ) return 0;      /* 別の op の行 */

	/* ★ ここから先は **op 名が一致した行**。形が壊れていたら飛ばさずエラーにする。 */

	/* ---- body ---- */
	sPtr<pigDataLambda> bl = sPtr<pigDataLambda>::d_cast(pig_hash_get(row, "body"));
	if ( ! bl.is_notNull() )
		{ *emsg = std::string("pseudo module: op '") + op + "' has no lambda 'body'"; return -1; }

	/* ---- in[]。★ 仕様変更: 無ければ **エラー** (以前は「種別も個数も問わない」で素通しだった) ---- */
	sPtr<pigData> iv = pig_hash_get(row, "in");
	sPtr<pigDataArray> ia = iv.is_notNull() ? iv->obt_array() : sPtr<pigDataArray>(thNULL);
	if ( ! ia.is_notNull() ) {
		::snprintf(buf, sizeof buf,
		    "pseudo module: op '%s' has no 'in' "
		    "(list the argument slots: \"value\" / \"cache\" / a match lambda)", op);
		*emsg = buf; return -1;
	}
	int nin      = ia->length();
	int variadic = pig_hash_int(row, "variadic", 0);
	if ( pnin != 0 )      *pnin = nin;
	if ( pvariadic != 0 ) *pvariadic = variadic;

	/* ---- body の引数の数 = in[] の長さ (variadic なら **配列 1 個**) ---- */
	int wantp = variadic ? 1 : nin;
	if ( bl->paramc() != wantp ) {
		::snprintf(buf, sizeof buf,
		    "pseudo module: op '%s' body takes %d argument(s) but 'in' declares %d%s",
		    op, bl->paramc(), nin,
		    variadic ? " (variadic:1 ⇒ the body takes exactly 1 argument: the array)" : "");
		*emsg = buf; return -1;
	}

	/* ---- nreq。★ 既定は **in の長さ** (全部必須)。範囲外は定義の誤り ---- */
	int nreq = pig_hash_int(row, "nreq", nin);
	if ( nreq < 0 || nreq > nin ) {
		::snprintf(buf, sizeof buf,
		    "pseudo module: op '%s' has nreq=%d, which is outside 0..%d (the length of 'in')",
		    op, nreq, nin);
		*emsg = buf; return -1;
	}

	/* ---- vtail。★ 仕様変更: 擬似モジュールの欄は 旧 `vtail_value` (整数 0/1) から
	 *   **`vtail` = "value" / "cache" の文字列**へ (ひさ 2026-09-21)。in[] のスロットが
	 *   同じ 2 語で書かれているので、可変部だけ 0/1 なのは綴りが揃っていなかった。
	 *   ⚠ **C++ の記述子側 (@pigOpEntry::vtail_value@) は そのまま** — あちらは OPS[] の
	 *     位置指定初期化子で書かれていて、改名は ABI の話になる (ここは値の話)。 */
	int vtailCache = 1;
	{
		sPtr<pigData> vv = pig_hash_get(row, "vtail");
		if ( vv.is_notNull() ) {
			sPtr<stdString> vs = vv->get_str();
			const char *v = vs.is_notNull() ? vs->get_str() : "";
			if      ( ::strcmp(v, "value") == 0 ) vtailCache = 0;
			else if ( ::strcmp(v, "cache") == 0 ) vtailCache = 1;
			else {
				::snprintf(buf, sizeof buf,
				    "pseudo module: op '%s' has vtail='%s' (must be \"value\" or \"cache\")", op, v);
				*emsg = buf; return -1;
			}
		}
	}

	/* ════ ここから下は「選ばれるか」 — 外れても **エラーにしない** ════ */

	int n = ( args != 0 ) ? args->length() : 0;

	/* ---- ① 引数の **数** ---- */
	if ( variadic ) { if ( n < nin ) return 0; }
	else            { if ( n < nreq || n > nin ) return 0; }

	/* ---- ② 引数の **種別** (幾何か値か)。★ ラムダのスロットは *述語* なので種別は見ない ---- */
	for ( int i = 0 ; i < n ; ++i ) {
		int wantCache;
		if ( i < nin ) {
			sPtr<pigData> slot = ia->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
			if ( sPtr<pigDataLambda>::d_cast(slot).is_notNull() ) continue;   /* 述語 */
			sPtr<stdString> ss = slot.is_notNull() ? slot->get_str() : sPtr<stdString>(thNULL);
			const char *sk = ss.is_notNull() ? ss->get_str() : "";
			if      ( ::strcmp(sk, "cache") == 0 ) wantCache = 1;
			else if ( ::strcmp(sk, "value") == 0 ) wantCache = 0;
			else {
				/* ★ スロットの綴りが違うのは **定義の誤り** (選ばれる / 選ばれない の話ではない) */
				::snprintf(buf, sizeof buf,
				    "pseudo module: op '%s' in[%d] must be \"value\", \"cache\" or a match lambda "
				    "(got '%s')", op, i, sk);
				*emsg = buf; return -1;
			}
		} else {
			wantCache = vtailCache;          /* ★ 可変部の種別は vtail の申告から */
		}
		std::string ts = arg_type_set((*args)[i]);
		int gotCache = ( ! ts.empty() && ts != "value" && ts != "ref" ) ? 1 : 0;
		if ( wantCache != gotCache ) return 0;       /* ★ 合わない = **次の行へ** */
	}

	/* ---- ③ 述語ラムダ ---- */
	if ( args != 0 && nin > 0 ) {
		for ( int i = 0 ; i < n ; ++i ) {
			int si = i;
			if ( si >= nin ) {
				if ( ! variadic ) break;
				si = nin - 1;                  /* 可変部は **最後のスロットが繰り返す** */
			}
			sPtr<pigData> slot = ia->get_ix(thNEW(pigDataInteger,((INTEGER64)si)));
			if ( ! sPtr<pigDataLambda>::d_cast(slot).is_notNull() ) continue;   /* 種別の申告 */
			sPtr<pigData> r = pig_apply_node(slot, args, i, 1, info)->compact();
			if ( r.is_notNull() && r->is_error() ) {
				sPtr<stdString> em = r->error_message();
				*emsg = std::string("pseudo module: op '") + op + "' match lambda for argument "
				      + std::to_string(i + 1) + " failed: " + ( em.is_notNull() ? em->get_str() : "?" );
				return -1;
			}
			if ( ! r.is_notNull() || r->get_bool() == 0 ) return 0;             /* 述語が偽 */
		}
	}

	/* ---- ④ sig。★ 仕様変更: **無い / "" / [] の 3 つは同じ = 入力型で絞らない** ----
	 *   以前は「無い = エラー」「"" = 入力 0 個として当たる」「[] = 候補 0 本で当たらない」と
	 *   3 通りに割れていた。擬似モジュールの sig は *入力型の並びしか見ない* (出力型は照合にも
	 *   下流にも使われない ⇒ 実測で `->cg-mesh3d` / `->zzz-nonsense` / `->value` が同結果) ので、
	 *   「書かない」を **絞らない** に倒すのが素直で、3 つの綴りも揃う。 */
	sPtr<pigData> sv = pig_hash_get(row, "sig");
	if ( ! sv.is_notNull() ) return 1;                      /* 欄が無い = 絞らない */
	sPtr<pigDataArray> sa = sv->obt_array();
	if ( sa.is_notNull() && sa->length() == 0 ) return 1;   /* [] = 絞らない */
	if ( ! sa.is_notNull() ) {
		sPtr<stdString> ss = sv->get_str();
		if ( ! ss.is_notNull() || ss->get_str()[0] == '\0' ) return 1;   /* "" = 絞らない */
	}
	int nsig = sa.is_notNull() ? sa->length() : 1;
	for ( int k = 0 ; k < nsig ; ++k ) {
		sPtr<pigData> one = sa.is_notNull() ? sa->get_ix(thNEW(pigDataInteger,((INTEGER64)k))) : sv;
		sPtr<stdString> os = one.is_notNull() ? one->get_str() : sPtr<stdString>(thNULL);
		if ( ! os.is_notNull() ) continue;
		/* ★ 1 本の中の ';' 区切りも C++ 側と同じに読む (書き方を揃えられるように)。 */
		std::string all = os->get_str();
		size_t sp = 0;
		while ( sp <= all.size() ) {
			size_t sc = all.find(';', sp);
			std::string t = all.substr(sp, ( sc == std::string::npos ? all.size() : sc ) - sp);
			pigSigLine L; parse_sigline(t, L);
			if ( sigline_matches(L, insets) ) return 1;
			if ( sc == std::string::npos ) break;
			sp = sc + 1;
		}
	}
	return 0;
}

/* ★ #3555 (c): その擬似モジュールが op について **受けられる引数の個数**。
 *   見つかれば 1 を返し lo..hi を入れる (variadic は hi = -1 = 上限なし)。
 *   ⚠ 同名の行が複数あるときは **それぞれの範囲の和** を lo..hi に丸める (診断に出すだけなので
 *     穴の空いた集合を正確に持つ必要はない)。定義が壊れている行はここでは言わない
 *     (言うのは選ぶ側 = pig_pseudo_row_fit の仕事で、二重に口を持たせない)。 */
static int
pig_pseudo_arity_range(sPtr<pigData> pseudo, const char *op, int *plo, int *phi)
{
	sPtr<pigDataArray> oa = pig_pseudo_ops(pseudo);
	if ( ! oa.is_notNull() ) return 0;
	int found = 0, lo = 0, hi = 0;
	for ( int j = 0 ; j < oa->length() ; ++j ) {
		sPtr<pigData> row = oa->get_ix(thNEW(pigDataInteger,((INTEGER64)j)));
		sPtr<pigData> nv = pig_hash_get(row, "name");
		sPtr<stdString> ns = nv.is_notNull() ? nv->get_str() : sPtr<stdString>(thNULL);
		if ( ! ns.is_notNull() || ::strcmp(ns->get_str(), op) != 0 ) continue;
		sPtr<pigData> iv = pig_hash_get(row, "in");
		sPtr<pigDataArray> ia = iv.is_notNull() ? iv->obt_array() : sPtr<pigDataArray>(thNULL);
		if ( ! ia.is_notNull() ) continue;
		int nin  = ia->length();
		int vari = pig_hash_int(row, "variadic", 0);
		int nreq = pig_hash_int(row, "nreq", nin);
		if ( nreq < 0 || nreq > nin ) nreq = nin;
		int rlo = vari ? nin : nreq;
		int rhi = vari ? -1  : nin;
		if ( ! found ) { lo = rlo; hi = rhi; found = 1; continue; }
		if ( rlo < lo ) lo = rlo;
		if ( hi >= 0 ) { if ( rhi < 0 ) hi = -1; else if ( rhi > hi ) hi = rhi; }
	}
	if ( found ) { *plo = lo; *phi = hi; }
	return found;
}

/* ★ 個数の範囲を "takes 2" / "takes 1 to 3" / "takes 2 or more" に整える (診断用)。 */
static std::string
pig_arity_phrase(int lo, int hi)
{
	char buf[64];
	if ( hi < 0 )       ::snprintf(buf, sizeof buf, "takes %d or more", lo);
	else if ( lo == hi) ::snprintf(buf, sizeof buf, "takes %d", lo);
	else                ::snprintf(buf, sizeof buf, "takes %d to %d", lo, hi);
	return std::string(buf);
}

/* ═════════════════════════════════════════════════════════════════════
 * ★★ #3555 段1 (2026-09-19): **候補列** — `module::op` の指名を「並び」として読む。
 *
 *   決着規則 (ひさ 2026-09-19):
 *
 *       候補列なし (指名も無し)   モジュール間は **priority 最大** (従来どおり)
 *       候補列あり                **候補列の順に先勝ち** ・ priority は **見ない**
 *                                 ⚠ *列に無いモジュールは呼ばれない*
 *
 *   ★ 同じモジュールの中で行を OPS 順に先勝ちする規則は **どちらでも変わらない**。
 *
 *   ★★ **どちらが正常な形か** (ひさ 2026-09-23): **候補列を書くのが原則**である。
 *     「候補列なし = priority 最大」は *従来どおり* であって *あるべき姿ではない* —
 *     モジュールは増え続けているので、絞らなければ **その op がどの .so に当たるかは
 *     ロード順と priority 任せ**になり、書いていないモジュールへ黙って配線されたまま
 *     値が返る。⇒ 「列に無いモジュールは呼ばれない」は**強すぎる制限ではなく狙い**である。
 *     絞った結果 op が見つからず落ちるのは、絞り込みが効いている証拠として扱う
 *     (エラー文は候補を全部並べ、`srava --module-info <name>` を案内する)。
 *
 *   指名式の型:
 *       テキスト "cgal"   候補列 = [cgal]  (= 従来の onlyModule)。"" は **指名なし**と同一 (#3467)
 *       配列 ["a","b"]    候補列 = その並びそのもの
 *
 *   ★★ #3555 段2: **指名が無ければ予約変数 @USE_MODULES@** を同じ規則で読む
 *     (= `<変数>::op` と書いたのと同じ意味)。それも無ければ候補列なし = 従来どおり priority 順。
 *
 *   ⚠ 解決できない名前は **飛ばす** (ひさ 2026-09-19)。all.sra が既定の順を書くと、
 *     nef off / occt 無しの構成では列の一部が必ず未ロードになるため。
 *     ★ ただし **全要素が未解決なら**エラーにする ⇒ `["ocdt"]` の誤字は `"ocdt"::op` と
 *       同じ顔で落ち、誤字が黙って priority 順に戻ることはない。
 * ═════════════════════════════════════════════════════════════════════ */
/* 候補列の 1 要素。★ 段3: **実モジュール**か **擬似モジュール (値)** のどちらか。 */
struct pigModCand {
	int           id;       /* 実モジュール id ・ -1 = 擬似 */
	sPtr<pigData> pseudo;   /* 擬似モジュールの定義ハッシュ (id < 0 のとき) */
	std::string   label;    /* ★ #3555: 診断に出す札 (実 = モジュール名 / 擬似 = name 欄 or "{pseudo}") */
	pigModCand() : id(-1) {}
};

struct pigModCands {
	int                      given;   /* 1 = 候補列が与えられた (未指定と "" は 0) */
	int                      nelem;   /* 列の要素数 (0 = 空配列 ⇒ 呼べる候補がゼロ)。★ 段5: **穴を除いた**数 */
	int                      nholes;  /* ★ 段5: 読み飛ばした穴 (null / "" / 0) の個数。診断の言い方に使う */
	std::vector<sPtr<pigData> > vals; /* 書かれたとおりの要素 (文字列 / ハッシュ)・順序どおり */
	std::vector<std::string> names;   /* その表示名 (擬似は "{pseudo}")・診断用 */
	std::vector<pigModCand>  elems;   /* 解決できた候補。**並びが優先順位** */
	std::vector<int>         ids;     /* そのうち実モジュールの id (sig_dispatch へ渡す形) */
	std::vector<int>         withOp;  /* そのうち op を持つもの (診断の言い分けに使う) */
	int                      nPseudo; /* 擬似モジュールの要素数 (0 なら段3 の枝に入らない) */
	int                      nPsWithOp; /* そのうち op 名を宣言しているもの */
	int                      fromVar; /* ★ 段2: 1 = 指名ではなく USE_MODULES から来た (診断の言い方) */
	pigModCands() : given(0), nelem(0), nholes(0), nPseudo(0), nPsWithOp(0), fromVar(0) {}
	/* sig_dispatch へ渡す形。指名が無ければ 0 = 全モジュール × priority */
	const std::vector<int> *list() const { return given ? &ids : 0; }
	/* エラー文の主語 (どこに書いた列の話かで直す場所が違う)。 */
	std::string where() const { return fromVar ? "USE_MODULES" : "module candidate list"; }
};

/* 名前の列を "'a', 'b'" に整える (エラー文用)。 */
static std::string
cand_names_str(const pigModCands &c)
{
	std::string r;
	for ( size_t i = 0 ; i < c.names.size() ; ++i ) {
		if ( ! r.empty() ) r += ", ";
		r += "'"; r += c.names[i]; r += "'";
	}
	return r;
}

/* ★★ #3555 段5 (ひさ 2026-09-21): 候補列の **穴** = *書かなかったのと同じ*に扱う要素。
 *   `null` / `""` / 数値 `0` の 3 つ。狙いは **枠を先に敷いて、条件で埋めた所だけ使う**こと:
 *
 *       var u = ["", "", "cgal"];                     // 優先順位の枠を先に作る
 *       if ( want_occt )     u[0] = "occt";
 *       if ( want_manifold ) u[1] = "manifold";
 *       use u;                                        // 埋めなかった枠は消える
 *
 *       var a = []; a[2] = "occt";  use a;            // 添字伸長が空けた穴 (= null) が混ざる形
 *
 *   ⚠ srava に三項演算子は無く、`&&` / `||` は **1/0 を返す** (被演算子を返さない) ので、
 *     「条件で要素を 1 つ落とす」はこの枠方式でしか書けない。配列は伸ばす (`a[length(a)]=v`)
 *     ことしかできず **途中を詰められない**ので、穴を飛ばす側で吸収する必要がある。
 *
 *   ⚠ 判定は言語の**真偽そのもの** (@get_bool@) に乗せてある — 規則が 1 つで済み、
 *     「偽なら書かなかったのと同じ」で読み手も覚えることが増えない。
 *   ⚠ 文字列 `"0"` は **穴ではない** (非空文字列 = 真)。名前として扱い、解決できなければ
 *     従来どおり「そんなモジュールは無い」で落ちる ⇒ 数値 0 と書き分けられる。
 *   ⚠ ハッシュは **擬似モジュール**なので先に除く。空ハッシュ `{}` は get_bool が偽だが、
 *     これは「op を 1 つも宣言していない擬似モジュール」であって穴ではない
 *     (黙って消すと、定義を書き損じた擬似モジュールが *無言で* 無視される)。 */
static int
cand_is_hole(sPtr<pigData> v)
{
	if ( ! v.is_notNull() ) return 1;                   /* null (添字伸長の穴・値なしの var) */
	if ( v->obt_hash().is_notNull() ) return 0;         /* 擬似モジュール */
	return v->get_bool() ? 0 : 1;
}

/* 1 要素の **表示名**。★ 段3: ハッシュなら **擬似モジュール**なので名前を引かない。 */
static std::string
cand_elem_name(sPtr<pigData> ev)
{
	if ( ev.is_notNull() && ev->obt_hash().is_notNull() ) return std::string("{pseudo}");
	sPtr<stdString> es = ev.is_notNull() ? ev->get_str() : sPtr<stdString>(thNULL);
	return es.is_notNull() ? std::string(es->get_str()) : std::string();
}

/* 1 要素ぶんを積む (列そのものがスカラだった場合に使う)。 */
static void
cand_push_elem(pigModCands &o, sPtr<pigData> ev)
{
	o.vals.push_back(ev);
	o.names.push_back(cand_elem_name(ev));
}

/* ★★ #3573 (2026-09-22): 候補列の **入れ子は平坦化**する。
 *     ["cgal", ["occt","manifold"], "geogram"]  ≡  ["cgal", "occt", "manifold", "geogram"]
 *
 *   ★ 狙いは **既にある列を部品として並べられる**ようにすること。候補列は「優先順位表」なので、
 *     使う側は自然に *表の断片に名前を付けて並べる* 書き方をする:
 *
 *         var mesh_first = ["cgal", "manifold"];
 *         use [ mesh_first, "occt" ];            // ← 平坦化が無いと配列の配列になる
 *
 *     ⇒ これが #3574 (擬似モジュール集) の前提でもある。擬似モジュールは
 *       「擬似 + 落ち先の実名」を **組**で書きたいが、組を並べた瞬間に入れ子になる。
 *
 *   ⚠ 平坦化前は入れ子が *1 つの名前* として読まれていた (要素の get_str が
 *     `[occt,manifold]` を返し、「そんなモジュールは無い」で落ちる)。⇒ 黙って無視は
 *     していなかったので、この変更で **通るようになるのは今まで落ちていた形だけ**。
 *
 *   ⚠ 深さは数えて止める — 自己参照する配列 (`var a = []; a[0] = a;`) を言語が禁じて
 *     いないので、ここで止めないとスタックを食い潰す。★ 打ち切るときは **黙って捨てず**に
 *     エラーにする (捨てると「書いた候補が消える」= 穴と同じ顔になり、追えなくなる)。 */
#define CAND_MAX_DEPTH 8

static int flatten_cand_array(sPtr<pigDataArray> ar, std::vector<pigCandItem> *out,
                              sPtr<pigData> *errv, int depth);

/* ★★ #3595: 平坦化の **実体はここ 1 本**。`use` / 指名 / @module(配列)@ が同じ規則で読む
 *   (理由と分担は pigData.h の @pigCandItem@ を見よ)。
 *   ⚠ **穴も積む** (hole=1) — 落とすか位置を保つかは *呼び手* が決める。ここで落とすと
 *     @module(配列)@ が出力を入力と 1:1 にできない。 */
static int
flatten_cand_array(sPtr<pigDataArray> ar, std::vector<pigCandItem> *out,
                   sPtr<pigData> *errv, int depth)
{
	if ( depth > CAND_MAX_DEPTH ) {
		if ( errv != 0 ) {
			char buf[192];
			::snprintf(buf, sizeof buf,
			    "module candidate list: nested more than %d levels deep "
			    "(a list that contains itself?)", CAND_MAX_DEPTH);
			*errv = thNEW(pigDataError,(buf, thNULL, 1));
		}
		return 0;
	}
	int n = ar->length();
	for ( int i = 0 ; i < n ; ++i ) {
		sPtr<pigData> ev = ar->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
		if ( ev.is_notNull() && ev->is_error() ) { if ( errv != 0 ) *errv = ev; return 0; }
		/* ⚠ 要素が遅延ノードなら compact して実体を取る (ハッシュ / 配列の判定に要る)。 */
		if ( ev.is_notNull() ) ev = ev->compact();
		if ( ev.is_notNull() && ev->is_error() ) { if ( errv != 0 ) *errv = ev; return 0; }
		/* ★ #3573: 入れ子は **その場に開く**。⚠ 穴の判定より **先に**見る —
		 *   空配列 `[]` は get_bool が偽なので、順が逆だと穴として数えられ、
		 *   「全部穴だった (null / "" / 0)」という *別件の* 文言が出てしまう。
		 *   平坦化した空配列は「要素 0 個」であって穴ではない。 */
		sPtr<pigDataArray> sub = ev.is_notNull() ? ev->obt_array() : sPtr<pigDataArray>(thNULL);
		if ( sub.is_notNull() ) {
			if ( ! flatten_cand_array(sub, out, errv, depth + 1) ) return 0;
			continue;
		}
		pigCandItem it;
		it.val    = ev;
		it.hole   = cand_is_hole(ev);
		it.pseudo = ( ev.is_notNull() && ev->obt_hash().is_notNull() ) ? 1 : 0;
		if ( ! it.hole ) it.name = cand_elem_name(ev);
		out->push_back(it);
	}
	return 1;
}

/* 平坦化の公開口 (pigData.cpp の @module(配列)@ から呼ぶ)。 */
int
pig_flatten_cand_array(sPtr<pigDataArray> ar, std::vector<pigCandItem> *out, sPtr<pigData> *errv)
{
	return flatten_cand_array(ar, out, errv, 0);
}

/* 候補列として読む側 — ★ 段5: 穴は **積まない**。⇒ 名前も診断に出ない = 書かなかったのと同じ顔になる。
 *   ⚠ 「未ロードだから飛ばす」とは別の枝。未ロード名は names に残って「どれも解決できなかった」の
 *     文言に出るが、穴は最初から候補ではない。 */
static int
cand_read_array(sPtr<pigDataArray> ar, pigModCands &out, sPtr<pigData> *errv)
{
	std::vector<pigCandItem> items;
	if ( ! flatten_cand_array(ar, &items, errv, 0) ) return 0;
	for ( size_t i = 0 ; i < items.size() ; ++i ) {
		if ( items[i].hole ) { ++out.nholes; continue; }
		out.vals.push_back(items[i].val);
		out.names.push_back(items[i].name);
	}
	return 1;
}

/* 1 つの値を候補列として読む (テキスト / 配列)。読めたら out.given を立てる。
 * ⚠ その値がエラーなら *errv に入れて 0 を返す。★ 名前の解決 (id 引き) はまだしない。 */
static int
read_cand_list(sPtr<pigData> v, pigModCands &out, sPtr<pigData> *errv)
{
	if ( ! v.is_notNull() ) return 1;
	/* ⚠ 値を読む口はゲートウェイなので、ここで **待ちうる** (ACT_START の再走前提は #3554 と同じ)。 */
	sPtr<pigData> mv = v->compact();
	if ( ! mv.is_notNull() ) return 1;
	if ( mv->is_error() ) { if ( errv != 0 ) *errv = mv; return 0; }

	/* ★ 配列 = 候補列そのもの。⚠ 要素は eager に解決されないので **要素ごとに口を通す**。
	 *   ★ #3573: 入れ子は平坦化する (cand_read_array が再帰する)。 */
	sPtr<pigDataArray> ar = mv->obt_array();
	if ( ar.is_notNull() ) {
		out.given = 1;
		if ( ! cand_read_array(ar, out, errv) ) return 0;
		out.nelem = (int)out.vals.size();      /* ★ 段5: 穴を除いた数 (全部穴なら 0 = 空配列と同じ) */
		return 1;
	}
	/* ★ 段3: ハッシュそのもの = **擬似モジュール単体**の指名。 */
	if ( mv->obt_hash().is_notNull() ) {
		out.given = 1;
		out.nelem = 1;
		cand_push_elem(out, mv);
		return 1;
	}
	/* ★ "" は **指名なし**と完全に同一 (#3467)。★ 段5: `null` / `0` も同じ穴として揃えた
	 *   — ⚠ ここは *要素* ではなく **列そのもの** なので、飛ばすのではなく「書かなかった」に倒す
	 *   (`[""]` は候補ゼロ = エラー、`""` は planner に任せる。スカラと配列で意味が違う)。 */
	if ( cand_is_hole(mv) ) return 1;
	out.given = 1;
	out.nelem = 1;
	cand_push_elem(out, mv);
	return 1;
}

/* 指名式 (無ければ USE_MODULES) → 候補列。★ *解決だけ*を行い、エラーにするかどうかは呼び手が決める
 *   (decide_out_module は明示エラー / try_decompose は黙って分解をやめる)。
 * ⚠ 指名式そのものがエラー値なら *errv に入れて 0 を返す。 */
static int
resolve_cands(const sPtr<pigModuleRegistry> &reg, sPtr<pigData> me, sPtr<pigData> useModules,
              const char *op, pigModCands &out, sPtr<pigData> *errv)
{
	if ( errv != 0 ) *errv = sPtr<pigData>(thNULL);
	if ( ! read_cand_list(me, out, errv) ) return 0;
	if ( ! out.given ) {
		/* ★ 段2: 指名なし (書いていない / "") なら予約変数を見る。"" は「planner に任せる」
		 *   という意味なので、**USE_MODULES まで含めて**同じ扱いにする。 */
		if ( ! read_cand_list(useModules, out, errv) ) return 0;
		if ( out.given ) out.fromVar = 1;
	}
	if ( ! out.given ) return 1;

	for ( size_t i = 0 ; i < out.vals.size() ; ++i ) {
		sPtr<pigDataHash> ph = out.vals[i].is_notNull() ? out.vals[i]->obt_hash()
		                                                : sPtr<pigDataHash>(thNULL);
		if ( ph.is_notNull() ) {                               /* ★ 段3: 擬似モジュール */
			/* ★ #3555: 札 (欄 name)。read_cand_list は reg を持たないのでここで入れ直す。 */
			out.names[i] = pig_pseudo_label(reg, out.vals[i]);
			pigModCand c; c.id = -1; c.pseudo = out.vals[i]; c.label = out.names[i];
			out.elems.push_back(c);
			++out.nPseudo;
			if ( op != 0 && pig_pseudo_declares_op(out.vals[i], op) ) ++out.nPsWithOp;
			continue;
		}
		int id = reg->id_of_name(out.names[i].c_str());
		if ( id <= 0 || reg->descriptor(id) == 0 ) continue;   /* ★ 未ロードは飛ばす */
		pigModCand c; c.id = id; c.label = out.names[i];
		out.elems.push_back(c);
		out.ids.push_back(id);
		/* ★★ #3555 段5: **`== 1` (持つと分かっているもの) だけ**数える。
		 *   ⚠ @supports_op@ は ops 表を持たない記述子に **-1 (不明・万能フォールバック扱い)**
		 *     を返すが、同じ条件で @op_row@ は **0 (行なし)** を返す ⇒ そういう記述子は
		 *     sig_dispatch で **構造的に絶対に勝てない**。-1 を「持つ」側に数えると、
		 *     ② の「どれもその op を持たない」という *正確な*診断が握り潰され、
		 *     代わりに「入力型を受けられない (pig: '(none)')」という筋違いの文言が出る
		 *     (組込 "pig" を列に入れると実際にそうなった)。
		 *   ★ 勝てないものを候補の数に入れない = 診断が「実際にどう振られるか」と一致する。 */
		if ( op != 0 && reg->supports_op(id, op) == 1 ) out.withOp.push_back(id);
	}
	return 1;
}

/* ★★ #3570 段2 (2026-09-21): **行が引数の数を受けられるか** の純粋な述語。
 *   判定規則は記述子に書いてあるもの (pigOpEntry.nreq のコメント) と同じ:
 *       n > nin かつ可変長でない → 多すぎ
 *       n < (nreq ? nreq : nin)  → 少なすぎ   (可変長は n < nin が少なすぎ)
 *   ⚠ **種別 (幾何/値) は見ない** — 種別違いは「どれも受けない」ではなく *この行に対する誤り*
 *     なので、隣へ降りずに @arg_kind_violation@ が名指しで言うべきもの。ここで降ろすと
 *     「argument 1 should be a mesh」が出なくなる ⇒ [[i3568]] で直した診断の劣化になる。
 *   ★ 引数の**総数**で見る (insets は幾何型だけなので使えない)。 */
static int
row_arity_fits(const pigOpEntry *e, int n)
{
	if ( e == 0 ) return 1;                       /* 行が無ければ判定しない (従来どおり) */
	/* ★★ #3570 段4: **個数を申告していない行**は弾かない。
	 *   @in[]@ も @nin@ も無く、計算本体の配線 (@wiring@) も持たない行 = *sig だけの行*
	 *   (テスト fixture の @{ "box", 0, 0, (pigArgKind)0, 0, 0, "->cg-mesh3d" }@ 等)。
	 *   ⚠ 本当に 0 引数の op (@empty2d@ / @empty3d@) とは **wiring の有無**で分かれる —
	 *     あちらは @OPWIRE(cgaEmpty3D)@ を持つので「0 個だけを受ける」が正しい申告になる。
	 *   ⚠ 可変長 (@loft@ / @loft_ruled@ は nin=0 + variadic=1) は下の式が n>=0 を常に許す。
	 *   ⇒ これを入れないと fixture の box/union が routing から落ちる (pigfagent / cgatsagent
	 *     が全滅して気づいた。**申告していないものを申告 0 と読んではいけない**)。 */
	if ( pig_op_row_declares_nothing(e) ) return 1;   /* ★ #3572: 判定は pigOpEntry.h に 1 本 */
	if ( n > e->nin && ! e->variadic ) return 0;
	int req = ( e->nreq > 0 ) ? e->nreq : e->nin;
	if ( e->variadic ? ( n < e->nin ) : ( n < req ) ) return 0;
	return 1;
}

/* ★★ #3554 段1 (2026-09-19): 候補は **行** になった。基底名 op に対して `op` と `op#変種` を
 *   OPS の並び順で 1 行ずつ引き (@op_row@)、*頭から先勝ち*で最初に成立した行を採る。
 *   ⇒ 勝った行の名前を @outRow@ で返し、呼び手がそれを C_OP に載せる (agent 側の @lookup_op@ は
 *     完全一致のままで当たる)。
 *   ⚠ 行の順序が効くのは *同じモジュールの中*だけ。
	 * ★★ #3555 段1 (2026-09-19): **外側ループを候補列で駆動する**。cands != 0 なら
	 *   その並びを順に見て **先に成立したモジュールを採る** (priority は見ない)。
	 *   cands == 0 なら従来どおり全モジュールを回して priority 最大を採る。 */
static int
sig_dispatch(const sPtr<pigModuleRegistry> &reg, const char *op,
             const std::vector<std::string>& insets, std::string *outType, int *foldN,
             const std::vector<int> *cands = 0, std::string *outRow = 0,
             sArray<sPtr<pigData> > *args = 0, int *matchedButSig = 0, int checkArity = 0,
             /* ★★ #3580: **勝った行そのもの**の出口。名前では足りない —
              *   @op_entry(id, 基底名)@ は **OPS の最初の行**を返す規則なので、同じモジュールに
              *   @op@ と @op#変種@ が並び、かつ *基底行が勝った* とき、名前で引き直すと
              *   変種の申告 (nin / in[]) で検査してしまう。#3570 のコメントが
              *   「いまそういう組は 0 件だが、書いた瞬間に静かに誤判定になる」と予告していた
              *   組を #3580 (openvdb の intersection と intersection#pt) で初めて作った。
              *   ⇒ 名前を往復させず **行を持ち回る**。 */
             const pigOpEntry **outRowPtr = 0)
{
	if ( outRow != 0 ) outRow->clear();
	if ( outRowPtr != 0 ) *outRowPtr = 0;
	if ( matchedButSig != 0 ) *matchedButSig = 0;
	if ( reg == thNULL ) return -1;
	int nmod = reg->count();
	int best = -1; long bestPrio = LONG_MIN; std::string bestOut; int bestN = -1;
	std::string bestRow;
	const pigOpEntry *bestRowPtr = 0;
	int nloop = ( cands != 0 ) ? (int)cands->size() : nmod - 1;
	for ( int ii = 0 ; ii < nloop ; ++ii ) {
		int m = ( cands != 0 ) ? (*cands)[ii] : ii + 1;
		if ( m <= 0 || m >= nmod ) continue;          /* 念のため (候補列は解決済みのはず) */
		for ( int ci = 0 ; ; ++ci ) {                         /* ★ OPS に書かれた順 = 先勝ち */
			const pigOpEntry *row = reg->op_row(m, op, ci);
			if ( row == 0 ) break;                        /* 候補行を尽くした */
			/* ★ #3554 段2: **マッチ関数 ∧ sig** で行を選ぶ。どちらか一方でも外れれば不成立。
			 *   ⚠ args が無い文脈では引数 0 個と同じ = マッチ関数は呼ばれず成立する。 */
			if ( ! row_matches_values(reg->descriptor(m), row, args) ) continue;
			/* ★ #3570 段2: **引数の数**も行の成立条件に入れる (checkArity のときだけ)。
			 *   ⇒ 数が合わない実モジュールは候補から外れ、**隣 (擬似) へ降りられる**。
			 *   ⚠ 段4 で全経路に広げる。それまでは通常 routing は 1 バイトも変わらない。 */
			if ( checkArity && args != 0 && ! row_arity_fits(row, args->length()) ) continue;
			const char *sig = row->sig;
			if ( sig == 0 ) continue;           /* その行は未注釈 */
			std::string all = sig;
			size_t sp = 0;
			int hit = 0;
			while ( sp <= all.size() ) {
				size_t sc = all.find(';', sp);
				std::string one = all.substr(sp, ( sc == std::string::npos ? all.size() : sc ) - sp);
				pigSigLine L; parse_sigline(one, L);
				if ( sigline_matches(L, insets) ) {
					hit = 1;
					long pr = reg->priority(m);
					/* ★ #3555 段1: 候補列があるときは **先勝ち** (priority を見ない)。
					 *   外側ループが下で break するので、ここへ来るのは列の中で
					 *   最初に成立したモジュールだけ。 */
					if ( cands != 0 || pr > bestPrio ) {
						bestPrio = pr; best = m; bestOut = L.out;
						bestRow = ( row->op != 0 ) ? row->op : op;
						bestRowPtr = row;
						/* ★ #3528: **分解してよい行なら N / そうでなければ -1**。
						 *   fold 形でない行 (固定形・繰り返し形) に加え、"(N!)" = 分解禁止も -1。 */
						bestN = ( L.kind == SK_FOLD && ! L.nosplit )
						        ? ( L.arity < 0 ? INT_MAX : L.arity ) : -1;
					}
					break;                      /* この行で成立 = これ以上 sigline を見ない */
				}
				if ( sc == std::string::npos ) break;
				sp = sc + 1;
			}
			/* ★ 最後の段 2/5: **値では成立したのに sig で落ちた** 行が在った、と覚えておく。
			 *   呼び手 (cast/import/export の診断) はこれで原因を 2 つに分けて言える。 */
			if ( ! hit && matchedButSig != 0 ) *matchedButSig = 1;
			if ( hit ) break;                   /* ★ このモジュールでは **先に成立した行**を採る */
		}
		if ( cands != 0 && best >= 0 ) break;        /* ★ #3555 段1: 候補列は **先勝ち** */
	}
	if ( outType != 0 ) *outType = bestOut;
	if ( foldN   != 0 ) *foldN   = bestN;
	if ( outRow  != 0 ) *outRow  = bestRow;
	if ( outRowPtr != 0 ) *outRowPtr = bestRowPtr;
	return best;
}

/* ★★ #3555 段2: 予約変数 @USE_MODULES@ を引く。
 * ⚠ **束縛の有無**で判断する (@has_var@) — @get_var@ は未定義のとき pigDataError を返すので、
 *   is_error() で倒すと「未定義」と「エラー値を代入した」が同じ顔になり、後者が黙って
 *   priority 順へ落ちる。⇒ 値がエラーなら **そのまま返し**、呼び手に伝播させる。 */
sPtr<pigData>
pigfModuleAgent_::use_modules_var()
{
	if ( ! env.is_notNull() ) return sPtr<pigData>(thNULL);
	sPtr<stdString> nm = thNEW(stdString,("USE_MODULES"));
	if ( ! env->has_var(nm) ) return sPtr<pigData>(thNULL);
	return env->get_var(nm);
}

int
pigfModuleAgent_::decide_executor(const char *op, const std::vector<int> *cands, int *matchedButSig)
{
	/* ★★ #3554 最後の段 4/5 (2026-09-19): **「型では振れない op」の門は無くなった**。
	 *   ここには長らく cast / import / export の 3 つが並んでいた — どれも「文字列引数で
	 *   行き先が決まる」ので型ディスパッチから外す、という特例だった。
	 *   いまは 3 つとも **行のマッチ関数**が値を見る (目標型 / 拡張子) ので、普通の検索で決まる:
	 *       cast    pig_match_cast_target   第 1 引数の型名が この行の sig の出力型か
	 *       import  pig_match_import_ext    拡張子が産む型 (import_exts) が この行の sig の出力型か
	 *       export  pig_match_export_ext    拡張子を この行のモジュールが書けるか (export_exts)
	 *   ★ 2026-08-19 に export_vox が先に外れており (可変長は sig の "T..." で書けるように
	 *     なった)、これで **特例は 1 つも残っていない**。
	 *   ⚠ 門を消すのは「op 名で routing を変える最後の場所」を消すことでもある。
	 *     新しい op で同じことをしたくなったら、**ここではなくマッチ関数**を書く。 */

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
	std::string bestOut, bestRow;
	/* ★ #3554 段2: ここが **実際の routing** なので args を渡してマッチ関数を効かせる。
	 *   ⚠ try_decompose 側の sig_dispatch には渡していない — あちらは「この型の組を受けられる
	 *     モジュールがあるか」を見るだけで、args の並びが実引数と違うことがあるため。
	 *     ⇒ *マッチ関数を持つ行は分解の判定からは見えない*。分解する op (union 等) に変種行を
	 *       足すときは、ここを見直すこと。 */
	/* ★★ #3570 段4 (2026-09-21): **引数の数を行の成立条件に入れる** (checkArity=1)。
	 *   ⇒ 数が合わない行は候補から外れ、隣のモジュールへ降りる。段2 では候補列の中だけ
	 *     だったものを、ここ (通常の routing) へ広げた。
	 *   例: sphere(1,32,0.05) は cgal/manifold が 2 個までなので外れ、**openvdb だけ**が受ける。
	 *   ⚠ どれも受けないときの文言は decide_out_module の「no candidate takes N argument(s)」
	 *     に集約した (以前は勝った行に対する arg_kind_violation が言っていた)。 */
	const pigOpEntry *bestRowPtr = 0;
	int best = sig_dispatch(reg, op, insets, &bestOut, 0, cands, &bestRow, &args, matchedButSig, 1,
	                        &bestRowPtr);
	routedRow = bestRowPtr;   /* ★ #3580: 検査は **勝った行そのもの**で行う (名前で引き直さない) */
	if ( best >= 0 ) {
		/* ★ #3554 段1: **勝った行の名前**を memo する。以後の op 名 (ハッシュ・C_OP・ps 表示) は
		 *   agent_op_name() がこれを返す。⚠ 基底行が勝ったときは基底名そのものなので機能不変。
		 *   ⚠ _front は書き換えない (再走で候補集合が変わらないようにするため)。 */
		routedOpName = ( ! bestRow.empty() && bestRow != op )
		             ? thNEW(stdString,(bestRow.c_str())) : sPtr<stdString>(thNULL);
		/* ★ 2026-08-19: sig の出力トークンを **そのまま** memo する。以前は "value" を thNULL へ
		 *   畳んでいたが、それだと「値出力だから型が無い」と「型が絞れなかった」が同じ thNULL に
		 *   なり、下流のスタンプが両者を区別できなかった (値キャッシュに mesh 型リストが載っていた)。
		 *   値も参照も **組込モジュール "pig" が申告する型** ("value" / "ref") なので、そのまま載る。 */
		outTypeList = bestOut.empty() ? thNULL : thNEW(stdString,(bestOut.c_str()));
		return best;
	}
	return -1;   /* 型ディスパッチで解決できず → 既存ロジックへ */
}

/* ★★ #3555 段3 (2026-09-20): 候補列に **擬似モジュール**が居るとき、候補列の順どおりに
 *   先勝ちを決める。0 = 非該当 (通常の routing へ) / 1 = _front 解決済み / 2 = err 済み。
 *
 * ★ 実モジュールが先に当たったら **0 を返して通常の routing に任せる** — 順序の判定を
 *   ここ 1 か所に閉じ込め、routing (sig_dispatch) は擬似を知らないまま同じ勝者に辿り着く。
 *   ⇒ 擬似が居ないときの経路は 1 バイトも変わらない (この関数は即 0 を返す)。
 * ⚠ 述語ラムダは **待ちうる** ⇒ ACT_START が頭から再走する。この関数は再入に耐える
 *   (args を書き換えない・印を持たない・純粋な述語しか呼ばない)。 */
int
pigfModuleAgent_::try_pseudo_module()
{
	sPtr<stdString> opn = agent_op_name();
	const char *op = ( opn != thNULL ) ? opn->get_str() : 0;
	if ( op == 0 ) return 0;

	sPtr<pigModuleRegistry> reg = ( ptsApp != thNULL ) ? ptsApp->module_registry
	                                                   : sPtr<pigModuleRegistry>(thNULL);
	if ( reg == thNULL ) return 0;

	pigModCands cands;
	{
		sPtr<pigData> me = ( _front.is_notNull() ) ? _front->get_module_expr()
		                                           : sPtr<pigData>(thNULL);
		sPtr<pigData> cerr;
		/* ⚠ 指名式のエラーはここでは言わない — 分解と同じく decide_out_module が必ず走って
		 *   同じ入力で言い分ける (エラー文の持ち主を 1 か所にする)。 */
		if ( ! resolve_cands(reg, me, use_modules_var(), op, cands, &cerr) ) return 0;
	}
	if ( ! cands.given || cands.nPseudo == 0 ) return 0;   /* ★ 擬似が居なければ何もしない */

	/* 入力型列 (decide_executor と同じ作り方)。★ 型が絞れないなら擬似にも振らない。 */
	std::vector<std::string> insets;
	for ( int k = 0 ; k < args.length() ; ++k ) {
		int stampless = 0;
		std::string ts = arg_type_set(args[k], &stampless);
		if ( stampless ) return 0;
		if ( ts.empty() || ts == "value" || ts == "ref" ) continue;
		if ( ts.find(',') != std::string::npos ) return 0;
		insets.push_back(ts);
	}

	for ( size_t i = 0 ; i < cands.elems.size() ; ++i ) {
		if ( cands.elems[i].id >= 0 ) {
			/* 実モジュール: この 1 個だけで引いて当たれば **そちらが勝ち** ⇒ 通常の routing へ。 */
			std::vector<int> one(1, cands.elems[i].id);
			std::string ot;
			/* ★ #3570 段2: **引数の数が合う行が在るときだけ** 実モジュールが勝つ。
			 *   以前は sig だけを見ていたので、@cgal@ が受けられない個数で呼んでも
			 *   「実モジュールが居る」で降りてしまい、擬似に届かなかった。 */
			if ( sig_dispatch(reg, op, insets, &ot, 0, &one, 0, &args, 0, 1) >= 0 ) return 0;
			continue;
		}
		sPtr<pigDataArray> oa = pig_pseudo_ops(cands.elems[i].pseudo);
		if ( ! oa.is_notNull() ) continue;
		for ( int j = 0 ; j < oa->length() ; ++j ) {
			sPtr<pigData> row = oa->get_ix(thNEW(pigDataInteger,((INTEGER64)j)));
			std::string emsg;
			int nin = 0, vari = 0;
			int r = pig_pseudo_row_fit(row, op, insets, &args, _front->get_info(),
			                           &nin, &vari, &emsg);
			if ( r < 0 ) {
				err = thNEW(pigDataError,(emsg.c_str(), _front->get_info(), 1));
				return 2;
			}
			/* ★★ #3555 (b): 外れたら **次の行**。同名の行が尽きたらこのループを抜け、
			 *   候補が尽きたら 0 を返して **黙って通常の routing へ降りる**。
			 *   ⇒ 同名 2 行で「引数の数と種別」を振り分けられる (以前は 1 行目で止まり、
			 *     どちらの順に書いても片方だけが通った)。 */
			if ( r == 0 ) continue;

			/* ★ キャッシュを作らず @body@ を呼び、返った値を @_front@ に載せる。
			 *   前例: try_decompose が分解した木を @_front->set_result(root)@ している。
			 *   ⚠ 継続にスタンプを刻まないので、下流が読むのは **返ってきた値が自分で持つ型**
			 *     ⇒ 「sig が嘘をつく」状態が構造的に作れない (だから返り値と sig は照合しない)。 */
			sPtr<pigData> bv = pig_hash_get(row, "body");
			sPtr<pigData> call;
			if ( vari ) {
				/* 可変長は **引数配列をそのまま** 1 個で渡す (定義ハッシュの約束)。 */
				sPtr<pigDataArray> av = thNEW(pigDataArray,());
				for ( int k = 0 ; k < args.length() ; ++k ) av->push_nocheck(args[k]);
				sArray<sPtr<pigData> > one;
				one.length(1);
				one[0] = av;
				call = pig_apply_node(bv, &one, 0, 1, _front->get_info());
			} else if ( args.length() == nin ) {
				call = pig_apply_node(bv, &args, 0, nin, _front->get_info());
			} else {
				/* ★★ #3555: @nreq@ で省略された **末尾を null で埋める** (ひさ 2026-09-21)。
				 *   body の引数の数は in[] の長さに固定されている (定義のときに照合済み) ので、
				 *   呼ぶ側がここで揃える。
				 *   ⇒ body は `if ( n == null )` で「省略された」を読む (#3567 でリテラルと
				 *     `null == null` が入った)。⚠ 真偽 `if (n)` では **0 を渡した人も**
				 *     省略扱いになるので、埋め値を下流へ流す行では == null で見ること。 */
				sArray<sPtr<pigData> > full;
				full.length(nin);
				for ( int k = 0 ; k < nin ; ++k )
					full[k] = ( k < args.length() ) ? args[k]
					                                : sPtr<pigData>(thNEW(pigDataNull,()));
				call = pig_apply_node(bv, &full, 0, nin, _front->get_info());
			}
			_front->set_result(call);
			return 1;
		}
	}
	return 0;
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
	  /* ★ #3554 段1: 「**どれかの行が**そう申告しているか」を問うので、候補行を全部見る。
	   *   ⚠ op_sig() は最初の候補行しか返さない ⇒ 変種行が増えると *黙って「持っていない」と
	   *     答える* (2026-09-19 に洗い出した・ひさ指摘)。 */
	  for ( int ci = 0 ; ; ++ci ) {
		const pigOpEntry *row = reg->op_row(m, op, ci);
		if ( row == 0 ) break;
		const char *sig = row->sig;
		if ( sig == 0 ) continue;
		std::string all = sig; size_t sp = 0;
		while ( sp <= all.size() ) {
			size_t sc = all.find(';', sp);
			std::string one = all.substr(sp, ( sc == std::string::npos ? all.size() : sc ) - sp);
			pigSigLine L; parse_sigline(one, L);
			/* ★ #3528: "(N!)" の行は分解禁止なので、この関門でも数えない。 */
			if ( ! L.bad && L.kind == SK_FOLD && L.fixed.empty() && ! L.nosplit ) return true;
			if ( sc == std::string::npos ) break;
			sp = sc + 1;
		}
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
build_ktree(sPtr<stdString> op, sPtr<pigInfo> info, sArray<sPtr<pigData> >& e, int n, int k,
            sPtr<pigData> modexpr)
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
			/* ★ #3467: 分解して作った節点にも指名を継がせる。継がせないと
			 *   `"occt"::union(a,b,c)` が **指名なしの二項 union** に分解され、指名が黙って
			 *   消える (n 項と 2 項で行き先が変わる = 最も見つけにくい種類の食い違い)。 */
			if ( modexpr.is_notNull() ) node->set_module_expr(modexpr->clone());
			cur[m++] = node;                       /* ★ 前詰め (m <= i なので読み書きが衝突しない) */
		}
		cur.length(m);
	}
	return cur[0];
}

/* 順序保持の左 fold を k 個ずつ (非可換 op = difference)。
 * ⚠ k=2 のとき旧 build_leftfold と同じ木 (((a-b)-c)-d)。 */
static sPtr<pigData>
build_kleftfold(sPtr<stdString> op, sPtr<pigInfo> info, sArray<sPtr<pigData> >& e, int n, int k,
                sPtr<pigData> modexpr)
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
		if ( modexpr.is_notNull() ) node->set_module_expr(modexpr->clone());   /* ★ #3467 */
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

	/* ★ #3467: 指名があれば **群のサイズ決定 (k) も指名したモジュールの N/N' で決める**。
	 *   ここでは解決できないケースを黙って -1 に倒す (エラーの出し分けは decide_out_module の
	 *   仕事で、分解しなければそちらが同じ入力で必ず走る)。 */
	pigModCands cands;
	{
		sPtr<pigData> me = ( _front.is_notNull() ) ? _front->get_module_expr()
		                                           : sPtr<pigData>(thNULL);
		sPtr<pigData> cerr;
		/* ★ #3555 段1: 指名は **候補列**。ここで出せるエラーは無い (分解しなければ
		 *   decide_out_module が同じ入力で必ず走り、そちらが言い分ける)。 */
		if ( ! resolve_cands(reg, me, use_modules_var(), op, cands, &cerr) ) return 0;
		if ( cands.given && cands.ids.empty() ) return 0;
	}

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
		int m0 = sig_dispatch(reg, op, insets, &ot, &foldN, cands.list());
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
	int m = sig_dispatch(reg, op, probe, &ot, &foldN, cands.list());
	if ( m < 0 )
		return 0;                                  /* 2 項でも行き先が無い → 通常経路が明示エラー */
	/* ★★ #3528: **実際にマッチした行**が分解可でなければ分解しない。
	 *   仕様 (docs/sig_grammar_design.md §5.1) は「fold 形**の行**を持つ op」と行の話なのに、
	 *   上の op_is_decomposable() は **op 単位**の安いフィルタでしかない
	 *   (どれか 1 モジュールの 1 行が fold なら true)。⇒ 繰り返し形の行にマッチした呼び出しまで
	 *   ここへ来て、N' で群を切られていた。sig_dispatch は既に foldN でこれを答えているので拾う。
	 *   ⚠ この取りこぼしは "(N!)" を入れる前から在った (繰り返し形の「分解しない」という
	 *     約束が、op 単位の関門が開いている限り守られていなかった)。 */
	if ( foldN < 0 )
		return 0;
	int lim = reg->arity(m);                       /* N' (policy) */
	if ( foldN >= 2 && foldN < lim ) lim = foldN;  /* N  (capability・正しさの上限) */
	if ( lim > n - 1 ) lim = n - 1;                /* 分解する以上、群は n より必ず小さい */
	int k = lim;
	while ( k > 2 ) {                              /* 受け手が居なければ群を縮めて引き直す */
		std::vector<std::string> pk(insets.begin(), insets.begin() + k);
		if ( sig_dispatch(reg, op, pk, 0, 0, cands.list()) >= 0 ) break;
		--k;
	}
	if ( k < 2 )
		return 0;

	/* 木の形は **可換フラグ**が決める (§5.3)。★ ここは eval 時 (= モジュールが実際に dlopen 済み)
	 * なので op_commutative() は正しい値を返せる。可換なら均衡木 (build_ktree)、非可換なら
	 * 左 fold (build_kleftfold)。
	 * ★ #3500 (2026-09-07): **引数は並べ替えない**。以前はここで get_hashkey() 昇順に
	 *   ソートしていた (旧 normalize() の parse 時ソートの移設先) が、外した。理由:
	 *     1) 予測できない … 並び順が各引数の結果ハッシュに依存し、そのハッシュはモジュール
	 *        記述子の指紋 (名前 + cache_version) を含むので、**幾何に無関係な版を上げるだけで
	 *        木の形が変わる** (#3492 で実測)。ソースを見ても中間データの大きさ = かかる時間が
	 *        読めない。
	 *     2) 得ているものが小さい … 目的は a|||b と b|||a の正規化だが、順が違えば中間データが
	 *        大きく異なるので、実際に共有できるのは最終ノードだけ。
	 *     3) 失っているものが大きい … 同じ模型・同じ結果でも、畳む順が違うだけで実行時間が
	 *        **倍の桁で**変わる (メッシュのブールを直接やるカーネルで顕著・実測は #3500)。
	 *   ⇒ **ソースに書かれた順**でそのまま畳む。キャッシュキーの正規化 (二項ノードで
	 *   a|||b と b|||a を同一キーにする) は pigfAgent::compute_arg_hash 側に残してある。 */
	int commutative = reg->op_commutative(op);
	sArray<sPtr<pigData> > e;
	e.length(n);
	for ( int i = 0 ; i < n ; ++i ) e[i] = args[i];
	sPtr<pigData> me2 = ( _front.is_notNull() ) ? _front->get_module_expr() : sPtr<pigData>(thNULL);
	sPtr<pigData> root = commutative
	    ? build_ktree(opn, _front->get_info(), e, n, k, me2)
	    : build_kleftfold(opn, _front->get_info(), e, n, k, me2);
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
	/* ★ #3474 続き: **必須の個数** は nreq (0 = 全部必須 = 従来どおり)。nreq..nin の範囲は
	 *   省略形として通し、**既定値は op の compute() が入れる** (どの op も自前で持っている)。
	 *   パーサが固定 arity へ組み直す必要が無くなり、余分な引数の握り潰しも起きない。 */
	int req = ( e->nreq > 0 ) ? e->nreq : e->nin;
	if ( e->variadic ? ( n < e->nin ) : ( n < req ) ) {
		if ( req == e->nin )
			::snprintf(buf, sizeof buf, "%s: expected %d argument(s), got %d", op, e->nin, n);
		else
			::snprintf(buf, sizeof buf, "%s: expected %d to %d argument(s), got %d",
			    op, req, e->nin, n);
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
pigfModuleAgent_::arg_kind_violation(const sPtr<pigModuleRegistry> &reg, int module_id, const char *op,
                                     const char *rowName)
{
	/* ★★ #3570 (一緒に直す小さいもの): **勝った行**で検査する。
	 *   @op_entry(id, op)@ は基底名で引くと *OPS の最初の行* を返すので、同じモジュールに
	 *   @op@ と @op#変種@ が並んでいて **nin / in[] が違う**場合、*別の行の申告*で検査して
	 *   いた (いまそういう組は 0 件だが、書いた瞬間に静かに誤判定になる)。
	 *   ⇒ 呼び手が持っている *勝った行の名前* (routedOpName) を渡す。完全一致で引ける。
	 *   ⚠ 文言に使う名前は **基底名** のまま (利用者は @sphere#vox@ とは書いていない)。 */
	/* ★★ #3580: **勝った行そのもの**が分かっているならそれを使う。名前で引き直すと
	 *   @op_entry(id, 基底名)@ が *OPS の最初の行* を返すので、同じモジュールに @op@ と
	 *   @op#変種@ が並び、かつ **基底行が勝った**とき (= routedOpName が空のとき) に
	 *   変種の申告で検査してしまう。#3570 のコメントが予告していた組を openvdb の
	 *   @intersection@ / @intersection#pt@ で初めて作り、grid 同士の 2 項 intersection が
	 *   「expected 3 argument(s), got 2」で落ちた (2026-09-22 実測)。 */
	const pigOpEntry *e = routedRow;
	if ( e == 0 ) {
		const char *key = ( rowName != 0 && rowName[0] != '\0' ) ? rowName : op;
		e = ( reg != thNULL ) ? reg->op_entry(module_id, key) : 0;
	}
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

	/* ★ #3467: `モジュール::op(…)` の指名を解決する。**cast/import/export の特別扱いより前**。
	 *
	 *   ★ 指名は **候補の絞り込み**であって sig の代わりではない。ここでは名前 → module id の
	 *     解決と「その op を持つか」までを見て、**入力型が合うか (sig 照合) は通常の routing に
	 *     そのまま任せる**。暗黙の cast は入れない ⇒ 「op sig がディスパッチの唯一の真実」を保つ。
	 *
	 *   ★ `""::op` は「planner に任せる」= 指名なしと**完全に同一**。ループ内で条件分岐なしに
	 *     書けるようにするため (空文字を特別扱いせず素通しする)。
	 *
	 *   ⚠ 指名は args に入っていないので compute_arg_hash には現れない。キーに現れるのは
	 *     **解決結果 (outModule) から作られるソルト** (#3466) だけ ⇒ 「同じモジュールに解決されれば
	 *     同じキー」が自動的に成立する。 */
	/* ★★ #3555 段1 (2026-09-19): 指名は **候補列** (テキスト = 要素 1 個 / 配列 = その並び)。
	 *   ⚠ 未ロードの名前は *飛ばす* が、**どれも解決できなければ**エラー (ひさ 2026-09-19)
	 *     ⇒ 要素 1 個の誤字は従来の `"ocdt"::op` と同じ文言で落ちる。 */
	pigModCands cands;
	{
		sPtr<pigData> me = ( _front.is_notNull() ) ? _front->get_module_expr()
		                                           : sPtr<pigData>(thNULL);
		sPtr<pigData> cerr;
		if ( ! resolve_cands(reg, me, use_modules_var(), op, cands, &cerr) )
			{ err = cerr; return MODULE_NONE; }      /* 指名式 / USE_MODULES のエラーを伝播 */
		if ( cands.given ) {
			std::string ns = cand_names_str(cands);
			/* ⓪ 空の候補列 = 呼べるものがゼロ (ひさ 2026-09-19)。「planner に任せる」は
			 *   従来どおり "" で書く ⇒ 空配列を指名なしへ倒すと口が 2 つになる。 */
			if ( cands.nelem == 0 ) {
				std::string m = cands.where() + " is empty: nothing can run op '" + op + "' (";
				/* ★ 段5: 書いた要素が全部 **穴** だった場合は、そう言わないと「[] なんて
				 *   書いていない」と読まれる (画面には空配列に見えないため)。 */
				if ( cands.nholes > 0 ) {
					char nb[64];
					::snprintf(nb, sizeof nb, "all %d element(s) are holes (null / \"\" / 0) "
					                          "and were skipped; ", cands.nholes);
					m += nb;
				}
				m += "an empty list selects no module; write \"\" to let the "
				     "planner choose)";
				err = thNEW(pigDataError,(m.c_str(), _front->get_info(), 1));
				return MODULE_NONE;
			}
			/* ① どれも未ロード / 名前が違う (★ 擬似モジュールは常に「解決できた」側)。 */
			if ( cands.elems.empty() ) {
				if ( cands.nelem == 1 && ! cands.fromVar ) {
					char buf[256];
					::snprintf(buf, sizeof buf,
					    "module qualifier '%s': no such module is loaded "
					    "(check the name against `srava --modules`, or load it with module(\"%s.so\", {}))",
					    cands.names[0].c_str(), cands.names[0].c_str());
					err = thNEW(pigDataError,(buf, _front->get_info(), 1));
				} else {
					std::string m = cands.where() + " (" + ns + "): no such module is loaded "
					                "(names that are not loaded are skipped, but here none of them "
					                "resolved; check them against `srava --modules`)";
					err = thNEW(pigDataError,(m.c_str(), _front->get_info(), 1));
				}
				return MODULE_NONE;
			}
			/* ② ロードされてはいるが、どれもその op を持たない。
			 *   ⚠ *一部*が持たないだけなら飛ばす (エラーではない) — 候補列は優先順位表であって
			 *     「全員がこの op を持て」という要求ではない。 */
			if ( cands.withOp.empty() && cands.nPsWithOp == 0 ) {
				if ( cands.nelem == 1 && ! cands.fromVar ) {
					char buf[256];
					::snprintf(buf, sizeof buf,
					    "module qualifier '%s': that module does not implement op '%s' "
					    "(see `srava --module-info %s` for the ops it declares)",
					    cands.names[0].c_str(), op, cands.names[0].c_str());
					err = thNEW(pigDataError,(buf, _front->get_info(), 1));
				} else {
					std::string m = cands.where() + " (" + ns + "): none of them implements op '"
					              + op + "' (see `srava --module-info <name>` for the ops each declares)";
					err = thNEW(pigDataError,(m.c_str(), _front->get_info(), 1));
				}
				return MODULE_NONE;
			}
		}
	}

	/* ★★ #3554 最後の段 2/5 (2026-09-19): **cast の専用ブロックは撤去した**。
	 *   「目標型を産出できるモジュールへ振る」判定は行の **マッチ関数**
	 *   (@pig_match_cast_target@ = 第 1 引数の型名が @e->sig@ の出力型か) へ移り、cast は
	 *   下の decide_executor が **普通の検索** (priority × sig × マッチ) で解く。
	 *   ⇒ 根拠は行の申告 (sig) そのものなので、行名と二重帳簿にならない。
	 *   ⚠ cast の行は **1 行 1 出力型**でなければならない — 行の中に出力型が 2 つあると
	 *     マッチは成立するのに *入力型で先に当たった sigline* が選ばれ、**要求と違う型が返る**
	 *     (= routing と計算本体が別の述語で判定する形)。これは記述子のロード時に
	 *     pig_descriptor_violation が弾く。
	 *   ★ 失敗したときの **言い分け**だけは下に残っている (「cast の診断」)。 */

	/* ★ export: 対象拡張子を扱えないカーネルは万能側 (cgal) に振る (export_exts)。
	 *   #3404: mf の write_to/import は STL/OFF だけ。旧実装は export の .svg/.dxf だけ固定で、
	 *   manifold 既定の .3mf が無言で STL 化していた (2026-08-06 修正)。引数 path は args[0]。 */
	/* ★★ #3554 最後の段 3/5 (2026-09-19): **import の専用ブロックは撤去した**。
	 *   「拡張子を読めるカーネルのうち priority 最大へ・出力型は import_exts から」は
	 *   そのまま普通の検索になる — 行のマッチ関数 @pig_match_import_ext@ が
	 *   *拡張子が産む型 = この行の sig の出力型か* を見て行を選び、モジュール間は priority、
	 *   出力型は **その行の sig** から載る。
	 *   ⚠⚠ import は cast と違い **出力型が引数の型名ではなく拡張子で決まる**ので、
	 *     行を出力型ごとに分けないと *入力型 0 個の sigline が先頭から当たる* ＝
	 *     .svg を読んでも cg-mesh3d を名乗る。⇒ 1 行 1 出力型 (ロード時に弾く)。
	 *   ★ 申告の一致 (import_exts の型 ⊆ どれかの行の sig の出力型) もロード時に見る
	 *     ⇒ 「読めると言うのに その型を産まない」記述子の嘘が routing より前に外れる。
	 *   ★ 失敗の言い分けだけは下に残っている (「import の診断」)。 */

	/* ★★ #3554 最後の段 4/5 (2026-09-19): **export の専用ブロックは撤去した**。
	 *   行のマッチ関数 @pig_match_export_ext@ (= 第 1 引数の拡張子を @d->export_exts@ が
	 *   書けるか) と sig (= その入力型を受け取れるか) で、普通の検索が同じことをする。
	 *   出力は常に @ref@ なので **行を分ける必要は無い** (cast / import と違う点)。
	 *
	 *   ⚠⚠ 同時に **規約① (自型優先) を撤去した** (ひさ明言)。
	 *     「入力型の home カーネル (当時の @module_of_type(inType)@ ・ 5/5 で撤去) が
	 *      拡張子を書けるならそこへ振る」
	 *     という特例で、*sig でも記述子でもない 3 つめの規則*だった。⇒ いまは
	 *     **priority × sig × 拡張子** の普通の決着になる。
	 *   ⇒ 実際の行き先が変わる例: @export("a.stl", <mf-mesh3d>)@ は manifold (10) ではなく
	 *     **cgal (20)** が書く (cgal の export sig は mf-mesh3d を受けると申告している)。
	 *     同じ立体でもバイト列は違う (STL ヘッダ: manifold は空・cgal は "FileType: Binary")。
	 *   ★ 失敗の言い分けだけは下に残っている (「export の診断」)。 */



	/* ★ rev4 Phase B-2b: 型ディスパッチを先に試す。(op, 入力型[]) が注釈済み handler で確定できれば
	 *   そのモジュールへ (offset の次元・単一モジュールの novel op・既定カーネルを型で統一的に解決)。解決不能 (未注釈 op /
	 *   入力型が多候補で未確定 / cast・import・export) は -1 が返り、下の既存カーネルロジックへフォールバック
	 *   (op 単位 coexistence)。全 op が精密な単一型を伝播できるようになれば下のロジックと名指しは撤去可。 */
	{
		int matchedButSig = 0;
		int te = decide_executor(op, cands.list(), &matchedButSig);
		/* ★ #3555 (c): 執行者は決まったが **引数の種別 / 個数**が外れたときの文言を控えておく。
		 *   候補列があるときは (c) の列挙を優先し、それが言えなければこちらを使う。 */
		std::string kindViolation;
		if ( te >= 0 ) {
			/* ★ #3436 P4 §6.2: モジュールが決まった直後に **引数の種別と個数**を
			 *   in[]/nin/variadic と突き合わせる。従来この検査は agent 側 (ptsGenericAgent) に
			 *   しか無く、**計算が全部走ってから**落ちていた
			 *   (実測: export_vox("h5","t",{dx},box,box) は正しくエラーになるが、その時点で
			 *    box 2 個の cache が完成している)。sig は幾何引数の *型* しか見ないので、
			 *   値引数の取り違えはここでしか捕まらない。 */
			std::string ae = arg_kind_violation(reg, te, op,
			                     routedOpName.is_notNull() ? routedOpName->get_str() : 0);
			if ( ! ae.empty() ) {
				/* ★★ #3555 (c): **候補列があるなら 1 つだけ名指しせず候補を全部並べる**。
				 *   sig が当たったモジュールが 1 つ見つかった (te >= 0) だけで「takes 1」と
				 *   言い切ると、列の他の候補が何個を受けるのかが見えない ⇒ 「では何個ならよいか」
				 *   が読めない。下の (c) が言えなければ、この文言をそのまま使う。 */
				if ( ! cands.given ) {
					err = thNEW(pigDataError,(ae.c_str(), _front->get_info(), 1));
					return MODULE_NONE;
				}
				kindViolation = ae;
				te = -1;              /* ★ 以降の診断へ流す (執行者としては採らない) */
			} else {
				return te;
			}
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

		/* ★★ #3554 最後の段 2/5 (2026-09-19): **cast の診断**。振り分け自体は上の
		 *   decide_executor が普通の検索で行う (専用ブロックは撤去) が、*失敗したときに何を
		 *   直せばよいか*は op ごとに違うので、ここで言い分ける (ひさ 2026-09-19):
		 *       matchedButSig=0 … 目標型を産出する行が **無い**   → 型名の誤り / 未ロード / off
		 *       matchedButSig=1 … 行は在るが **入力型を受けない** → その変換が申告されていない
		 *   ⚠ 2 つは利用者の直す場所が違う (型名を直す / 先に別の型を経由する)。
		 *   ★ prod は「その型を作れるモジュール」の **名指し**にだけ使う。matchedButSig が
		 *     立たないまま prod が見つかる経路 (入力型が絞れず sig 照合まで届かなかったとき)
		 *     もあるので、どちらかが真なら入力側の問題として言う。 */
		if ( te == -1 && ::strcmp(op, "cast") == 0 && args.length() >= 1 ) {
			sPtr<pigData> tv = args[0]->compact();
			sPtr<stdString> tsv = ( tv.is_notNull() ) ? tv->get_str() : sPtr<stdString>(thNULL);
			const char *tname = ( tsv.is_notNull() ) ? tsv->get_str() : "";
			int prod = -1;
			int nmod = reg->count();
			/* ★ #3555 段1: 「その型を作れるモジュール」の名指しも **候補列の中だけ**で探す。 */
			int nc = cands.given ? (int)cands.ids.size() : nmod - 1;
			for ( int jj = 0 ; jj < nc && prod < 0 ; ++jj ) {
				int mm = cands.given ? cands.ids[jj] : jj + 1;
				if ( mm <= 0 || mm >= nmod ) continue;
				if ( any_row_produces(reg, mm, "cast", tname) ) prod = mm;
			}
			char buf[416];
			if ( matchedButSig != 0 || prod > 0 ) {
				/* ★ 2026-08-28 (ひさ指摘): ここで **形式 (4CC) を出さない** — planner は
				 *   「その型がディスクに落ちたら何の 4CC になるか」を知っている立場ではない。
				 *   形式に踏み込んだ診断は、実際に読めなかった側 (ptsGenericAgent) が出す。 */
				std::string ins;
				for ( int k = 0 ; k < args.length() ; ++k ) {
					std::string ts = arg_type_set(args[k]);
					if ( ts.empty() || ts == "value" || ts == "ref" ) continue;
					if ( ! ins.empty() ) ins += ",";
					ins += ts;
				}
				const char *pn = ( prod > 0 ) ? reg->name_of_id(prod) : 0;
				::snprintf(buf, sizeof buf,
				    "cast: no module declares a conversion from %s to '%s' "
				    "(module '%s' produces '%s' but its cast sig does not accept that input type)",
				    ins.empty() ? "(no geometry input)" : ins.c_str(), tname,
				    pn ? pn : "?", tname);
			} else {
				::snprintf(buf, sizeof buf,
				    "cast: 型 '%s' を産出できるモジュールが無い "
				    "(型名の誤り / その型を持つモジュールが未ロード / module(so,\"off\") で無効化)",
				    tname);
			}
			err = thNEW(pigDataError,(buf, _front->get_info()));
			return MODULE_NONE;
		}

		/* ★★ #3554 最後の段 3/5 (2026-09-19): **import の診断**。
		 *   ⚠ import の sig は入力 0 個なので、行が成立すれば sig も必ず当たる
		 *     ⇒ matchedButSig は立たず、失敗は **「その拡張子を読める行が無い」の一択**。
		 *   ★ 「CSV は読めると言うのに sig がその型を産まない」(記述子の嘘) もここへ落ちるが、
		 *     そちらは pig_descriptor_violation が **ロード時に**弾くので、ここへ来るのは
		 *     本当にどのモジュールも読めない拡張子だけ (#3439 ⑦ と同じ立て付け)。 */
		if ( te == -1 && ::strcmp(op, "import") == 0 && args.length() >= 1 ) {
			sPtr<pigData> pv = args[0]->compact();
			sPtr<stdString> ps = ( pv.is_notNull() ) ? pv->get_str() : sPtr<stdString>(thNULL);
			const char *pth = ( ps.is_notNull() ) ? ps->get_str() : "";
			const char *dot = ::strrchr(pth, '.');
			const char *ext = dot ? dot : "";
			char ibuf[288];
			::snprintf(ibuf, sizeof ibuf,
			    "import: 拡張子 '%s' を読めるモジュールが無い "
			    "(未対応の形式 / そのモジュールが未ロード / module(so,\"off\") で無効化)",
			    ( ext[0] == '.' ) ? ext + 1 : "(無し)");
			err = thNEW(pigDataError,(ibuf, _front->get_info()));
			return MODULE_NONE;
		}

		/* ★★ #3554 最後の段 4/5 (2026-09-19): **export の診断**。
		 *     matchedButSig=0 … 拡張子を書ける行が **無い**   → 未対応の形式 / 未ロード / off
		 *     matchedButSig=1 … 書けるが **入力型を受けない** → その型の読み手が居ない
		 *   ⚠ 2 つは利用者の直す場所が違う (形式を変える / 先に cast する)。旧ブロックの
		 *     nExtOk による言い分けが、そのまま matchedButSig に乗り換わった形。 */
		if ( te == -1 && ::strcmp(op, "export") == 0 && args.length() >= 2 ) {
			sPtr<pigData> pv = args[0]->compact();
			sPtr<stdString> ps = ( pv.is_notNull() ) ? pv->get_str() : sPtr<stdString>(thNULL);
			const char *pth = ( ps.is_notNull() ) ? ps->get_str() : "";
			const char *dot = ::strrchr(pth, '.');
			const char *ext = dot ? dot : "";
			std::string inType = arg_type_set(args[1]);
			char ebuf[288];
			if ( matchedButSig == 0 )
				::snprintf(ebuf, sizeof ebuf,
				    "export: 拡張子 '%s' を書けるモジュールが無い "
				    "(未対応の形式 / そのモジュールが未ロード / module(so,\"off\") で無効化)",
				    ( ext[0] == '.' ) ? ext + 1 : "(無し)");
			else
				::snprintf(ebuf, sizeof ebuf,
				    "export: 拡張子 '%s' は書けるが、入力の型 '%s' を受け取れるモジュールが無い",
				    ( ext[0] == '.' ) ? ext + 1 : "(無し)", inType.c_str());
			err = thNEW(pigDataError,(ebuf, _front->get_info()));
			return MODULE_NONE;
		}

		/* ★★ #3568 (2026-09-21): 候補列の **汎用**診断は cast / import / export の
		 *   専用診断より **後ろ**に置く。以前はここが (decide_executor の直後に) 在り、
		 *   候補列が与えられているだけで早期に return していたので、専用診断には
		 *   **決して到達しなかった** — 実測: 存在しない型への cast が「入力型を受け付け
		 *   ない」と言われる ＝ 要求は *出力型* の話なのに、返る文言が *入力型* の話になる
		 *   (import に至っては入力 0 個なのに "(no geometry input) を受け付けない" と言う)。
		 *   ⚠ 専用ブロックは **自分の op のときだけ** return するので、それ以外の op が
		 *     ここへ落ちる経路はこの並べ替えで変わらない。 */
		/* ★★ #3555 (c) (ひさ 2026-09-21): 候補が **どれも引数の数を受けられない** なら、
		 *   入力型の話をする前にそう言う。⇒ 「違反の報告は routing でも解けなかったときだけ・
		 *   候補を全部並べる」。並べるのは *その op を持つ候補* だけ (持たない候補の個数を
		 *   並べても手掛かりにならない)。
		 *
		 *       op 'tube' — no candidate takes 4 argument(s)
		 *         (glue: takes 1; {pseudo}: takes 3; occt: takes 2)
		 *
		 *   ⚠ **個数が合う候補が 1 つでもあれば言わない** — その場合の失敗は入力型か述語の話で、
		 *     個数を並べると直す場所を取り違える。
		 *   ⚠ cast / import / export はこれより **前**に専用診断で返る (#3568) ので、ここへは
		 *     来ない。 */
		/* ★★ #3570 段4: 候補列が **無い**ときも同じ形で言う。段4 で「数が合わない行は
		 *   routing から外れる」ようにしたので、以前ここで効いていた
		 *   arg_kind_violation (勝った行に対する "too many arguments") には *勝つ行が無い*
		 *   ため到達しない。⇒ その op を宣言している **全モジュール**を並べて同じ文言を出す。
		 *   ⚠ 並べるのは *その op を持つもの* だけ (持たないものの個数は手掛かりにならない)。 */
		std::vector<pigModCand> alist;
		if ( te == -1 && ! cands.given ) {
			int nmod = reg->count();
			for ( int mm = 1 ; mm < nmod ; ++mm ) {
				if ( reg->op_row(mm, op, 0) == 0 ) continue;
				pigModCand c; c.id = mm;
				const char *nm = reg->name_of_id(mm);
				c.label = ( nm != 0 ) ? nm : "?";
				alist.push_back(c);
			}
		}
		const std::vector<pigModCand> &elems = cands.given ? cands.elems : alist;
		if ( te == -1 && ( cands.given || ! alist.empty() ) ) {
			int n = args.length();
			std::string list;
			int nCand = 0, anyTakes = 0;
			for ( size_t i = 0 ; i < elems.size() ; ++i ) {
				int lo = 0, hi = 0, have = 0;
				if ( elems[i].id >= 0 ) {
					/* ★ 実モジュール: 同名の行 (op#変種) を全部見て範囲の和を取る。 */
					for ( int ri = 0 ; ; ++ri ) {
						const pigOpEntry *e = reg->op_row(elems[i].id, op, ri);
						if ( e == 0 ) break;
						int rlo = e->variadic ? e->nin : ( ( e->nreq > 0 ) ? e->nreq : e->nin );
						int rhi = e->variadic ? -1 : e->nin;
						if ( ! have ) { lo = rlo; hi = rhi; have = 1; continue; }
						if ( rlo < lo ) lo = rlo;
						if ( hi >= 0 ) { if ( rhi < 0 ) hi = -1; else if ( rhi > hi ) hi = rhi; }
					}
				} else {
					have = pig_pseudo_arity_range(elems[i].pseudo, op, &lo, &hi);
				}
				if ( ! have ) continue;
				++nCand;
				if ( n >= lo && ( hi < 0 || n <= hi ) ) anyTakes = 1;
				if ( ! list.empty() ) list += "; ";
				list += elems[i].label + ": " + pig_arity_phrase(lo, hi);
			}
			if ( nCand > 0 && ! anyTakes ) {
				char nb[32];
				::snprintf(nb, sizeof nb, "%d", n);
				std::string m = std::string("op '") + op + "' — no candidate takes " + nb
				              + " argument(s) (" + list + ")";
				err = thNEW(pigDataError,(m.c_str(), _front->get_info(), 1));
				return MODULE_NONE;
			}
			/* ★ 個数では説明がつかなかった (種別違い等)。控えておいた文言で言う。 */
			if ( ! kindViolation.empty() ) {
				err = thNEW(pigDataError,(kindViolation.c_str(), _front->get_info(), 1));
				return MODULE_NONE;
			}
		}

		/* ★ #3467 ③: 候補は op を持つ (② で確認済) のに解決できなかった
		 *   = その op の **sig が入力型を受け付けない**。①② と直す場所が違うので分けて言う。
		 * ★ #3555 段1: 候補列のときは **各候補の sig を並べる** — どれを直せばよいかは
		 *   候補ごとに違うので、1 つだけ名指しすると読み手が残りを推測することになる。 */
		if ( te == -1 && cands.given && ! cands.withOp.empty() ) {
			std::string ins;
			for ( int k = 0 ; k < args.length() ; ++k ) {
				std::string ts = arg_type_set(args[k]);
				if ( ts.empty() || ts == "value" || ts == "ref" ) continue;
				if ( ! ins.empty() ) ins += ",";
				ins += ts;
			}
			if ( ins.empty() ) ins = "(no geometry input)";
			std::string m;
			if ( cands.withOp.size() == 1 && ! cands.fromVar ) {
				const char *mn = reg->name_of_id(cands.withOp[0]);
				const char *sg = reg->op_sig(cands.withOp[0], op);
				m = std::string("module qualifier '") + ( mn ? mn : "?" ) + "': op '" + op
				  + "' does not accept input type(s) " + ins + " (its sig is '"
				  + ( sg ? sg : "(none)" ) + "'; ";
			} else {
				m = cands.where() + ": op '" + std::string(op)
				  + "' does not accept input type(s) " + ins + " in any candidate (";
				for ( size_t i = 0 ; i < cands.withOp.size() ; ++i ) {
					const char *mn = reg->name_of_id(cands.withOp[i]);
					const char *sg = reg->op_sig(cands.withOp[i], op);
					if ( i != 0 ) m += "; ";
					m += std::string(mn ? mn : "?") + ": '" + ( sg ? sg : "(none)" ) + "'";
				}
				m += "); ";
			}
			/* ★ 候補列を絞ると暗黙のキャストも止まる。これは**狙いどおり** — 絞らなければ
			 *   この値は列に無いモジュールへ黙って渡り、そこでキャストされて値を返す
			 *   (値が返るので気づく手掛かりが無い)。明示して落とす方を採る。 */
			m += "qualification narrows the candidates, it does not insert a cast — "
			     "convert explicitly with cast(<target type>, ...))";
			err = thNEW(pigDataError,(m.c_str(), _front->get_info(), 1));
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
}

/* ═════════════════════════════════════════════════════════════════════
 * ★ #3477: 実行時の内省 op 3 本 (modules / type_of / which)
 *
 *   いまどのモジュールが載っていて、ある式がどのカーネルで走るのかを **スクリプトから問う**。
 *   ここに置くのは、sig の解析 (parse_sigline) と型スタンプの読み方 (arg_type_set) が
 *   このファイルにあるため — **dispatch と同じ判定**を使わないと内省の意味がない
 *   (別実装の答えは「実際にどう走るか」を保証しない)。
 * ═════════════════════════════════════════════════════════════════════ */

/* priority 降順・同点は登録順 (= dispatch が候補を見る順) に module id を並べる。 */
static void
pig_modules_by_priority(const sPtr<pigModuleRegistry> &reg, std::vector<int> &out)
{
	int n = reg->count();
	for ( int i = 0 ; i < n ; ++i ) out.push_back(i);
	/* 安定ソート = 同点のとき登録順が保たれる (tie-break が「どの行を書いたか」で揺れない)。 */
	std::stable_sort(out.begin(), out.end(),
	    [&](int a, int b) { return reg->priority(a) > reg->priority(b); });
}

/* ★★ #3555 段5 (ひさ 2026-09-21): @modules()@ は **引数で 2 つの顔**を持つ。
 *
 *     modules()            → **名前の配列** (dispatch 順)。`use modules();` にそのまま食わせる形
 *     modules("priority")  → 従来の `"name:priority"` 空白区切り文字列 (priority の確認用)
 *
 *   ⚠ 既定 (引数なし) を配列にしたのは、**候補列へ渡すのが主用途**になったため。文字列は
 *     「priority がどう効いたか」を目で見る道具として引数つきに残した。
 *   ★★ 配列の中身は **sig_dispatch が選びうるもの** (`id > 0`・記述子あり・ops 行あり)。
 *     ⇒ `use modules();` は **挙動を変えない** —
 *     並びが priority 降順なので「列の先勝ち」= 従来の「priority 最大」と同じ結論になる
 *     (同点は両方とも登録順)。これが崩れると「内省の答えで配線したら結果が変わる」ことになる。
 *   ⚠ 2 つの顔は *同じ並びの別表現ではない*。文字列版は **番兵 `delayed` と組込 `pig` も
 *     隠さずに出す** (#3477 の決め) が、配列版は **op 行を持つもの** だけを返す。
 *     ⇒ 「載っているものを全部見せる」道具と「候補列へ渡す値」を分けた。 */
/* 1 つの値を「候補列として」読む (配列は平坦化 / スカラは 1 要素)。0 = エラー。
 * ★ @mod_only@ 用。@read_cand_list@ と違い **穴も擬似も落とさずに積む** (落とすのは呼び手)。 */
static int
modonly_read(sPtr<pigData> v, std::vector<pigCandItem> *out, sPtr<pigData> *errv)
{
	if ( ! v.is_notNull() ) return 1;
	sPtr<pigData> mv = v->compact();
	if ( ! mv.is_notNull() ) return 1;
	if ( mv->is_error() ) { if ( errv != 0 ) *errv = mv; return 0; }
	sPtr<pigDataArray> ar = mv->obt_array();
	if ( ar.is_notNull() ) return pig_flatten_cand_array(ar, out, errv);
	pigCandItem it;
	it.val    = mv;
	it.hole   = cand_is_hole(mv);
	it.pseudo = ( mv->obt_hash().is_notNull() ) ? 1 : 0;
	if ( ! it.hole ) it.name = cand_elem_name(mv);
	out->push_back(it);
	return 1;
}

/* ═════════════════════════════════════════════════════════════════════
 *  ★★ #3595 の続き (ひさ 2026-09-24): `mod_only(a, b)` — **候補列の積**
 *
 *   a のうち b に在る名前だけを **a の順のまま** 返す (= 優先順位を変えずに絞る)。
 *   ⚠ 穴 (null / 0 / "") は **両辺とも落とす** — 名前で比較できないため。
 *     a の重複は残す (列は集合ではなく優先順位表)。
 *   ★★ **a の擬似モジュール (ハッシュ) は a の位置のまま通す** (ひさ 2026-09-25)。
 *     b 側の擬似は従来どおり落とす (名前が無いので「許す名前」になれない)。
 *     ⇒ 返るのは「文字列 + 擬似」の配列。
 *     ⚠ これが無いと **粒度の既定値がライブラリ関数の境界で消える**。実測 (改訂前):
 *         use [ pm_cgal({seg:64}) ];      op 直呼び          nfaces 384   ← 64 が効く
 *                                          lib 関数経由       nfaces 192   ← 32 に戻る
 *                                          擬似なしの対照     nfaces 192   ← 上と同じ
 *       関数の内側で候補列が置き換わり擬似が居なくなるため。**落ちずに値が返る**ので、
 *       擬似モジュールリファレンスが警告している「書き漏らしは静かに既定値で通る」と同じ形
 *       になっていた (spiral の指摘・#3595)。
 *     ★ 擬似を「対の実モジュールが b に残ったときだけ通す」とはしない — 擬似は
 *       *呼び手が選んだ粒度* であって、どの実モジュールが生き残るかとは別の軸だから。
 *   ★ 読み方は候補列と同じ 1 本 (@pig_flatten_cand_array@) を通す ⇒ use / module(配列) と
 *     入れ子・穴の扱いがずれない。
 * ═════════════════════════════════════════════════════════════════════ */
/* 積の本体。a の順のまま、b に在る名前だけを積む。 */
static sPtr<pigData>
modonly_apply(const std::vector<pigCandItem> &ai, const std::vector<pigCandItem> &bi, int keepPseudo)
{
	sPtr<pigDataArray> out = thNEW(pigDataArray,());
	for ( size_t i = 0 ; i < ai.size() ; ++i ) {
		if ( ai[i].hole ) continue;                         /* 穴は書かなかったのと同じ */
		if ( ai[i].pseudo ) {                               /* ★ 擬似は **位置のまま通す** */
			if ( keepPseudo ) out->push(ai[i].val);         /* mod_only_names は落とす */
			continue;
		}
		for ( size_t j = 0 ; j < bi.size() ; ++j ) {
			if ( bi[j].hole || bi[j].pseudo ) continue;
			if ( ai[i].name == bi[j].name ) { out->push(thNEW(pigDataString,(ai[i].name.c_str()))); break; }
		}
	}
	return out;
}

/* 比較できる名前 (穴でも擬似でもないもの) が 1 つでもあるか。 */
static int
modonly_has_name(const std::vector<pigCandItem> &v)
{
	for ( size_t i = 0 ; i < v.size() ; ++i )
		if ( ! v[i].hole && ! v[i].pseudo ) return 1;
	return 0;
}

void
pigDataOperatorModOnly::_start()
{
	if ( args.length() < 2 ) {
		result = thNEW(pigDataError,("mod_only needs two lists: mod_only(<candidates>, <allowed>)",
		                             info, PE_FATAL));
		return;
	}
	std::vector<pigCandItem> ai, bi;
	sPtr<pigData> err;
	if ( ! modonly_read(args[0], &ai, &err) ) { result = err; return; }
	if ( ! modonly_read(args[1], &bi, &err) ) { result = err; return; }
	result = modonly_apply(ai, bi, keep_pseudo());
}

/* ═════════════════════════════════════════════════════════════════════
 *  ★★ 1 引数形 `mod_only(sup)` (ひさ 2026-09-24) — 左辺を **補う**だけの違い
 *
 *      呼び手が `use` で宣言している   → その列 (USE_MODULES) ∩ sup
 *      呼び手が何も宣言していない      → **modules()** ∩ sup (載っているもの・dispatch 順)
 *
 *   ⇒ ライブラリ関数は `use mod_only(sup);` の 1 行で「呼び手の選択を尊重し、交差しなければ
 *     エラー、何も言われていなければ載っているものから選ぶ」を宣言できる。
 *   ⚠ 「宣言していない」の判定は **比較できる名前が 1 つも無いこと**。`""` / `null` / `0` /
 *     `[]` / 全部穴 が全部ここへ落ちる (*未定義* を別扱いしない — 既定値が "" である以上、
 *     「未定義」と「空」を区別しても読み手に意味がないため)。
 *   ⚠ 第 1 引数はパーサが埋めた USE_MODULES の **変数参照**。
 * ═════════════════════════════════════════════════════════════════════ */
void
pigDataOperatorModOnlyUse::_start()
{
	if ( args.length() < 2 ) {
		result = thNEW(pigDataError,("mod_only needs a list: mod_only(<supported>)", info, PE_FATAL));
		return;
	}
	std::vector<pigCandItem> ai, bi;
	sPtr<pigData> err;
	if ( ! modonly_read(args[0], &ai, &err) ) { result = err; return; }
	if ( ! modonly_read(args[1], &bi, &err) ) { result = err; return; }

	if ( ! modonly_has_name(ai) ) {
		/* 宣言なし ⇒ **載っているもの** (modules() と同じ並び = dispatch 順) を左辺にする。 */
		ai.clear();
		sPtr<pigModuleRegistry> reg = pig_current_registry();
		if ( reg == thNULL ) {
			result = thNEW(pigDataError,("mod_only: no module registry (no app)", info, PE_FATAL));
			return;
		}
		std::vector<int> ids;
		pig_modules_by_priority(reg, ids);
		for ( size_t i = 0 ; i < ids.size() ; ++i ) {
			const char *nm = reg->name_of_id(ids[i]);
			if ( nm == 0 || nm[0] == '\0' ) continue;
			pigCandItem it;
			it.val  = thNEW(pigDataString,(nm));
			it.name = nm;
			ai.push_back(it);
		}
	}
	result = modonly_apply(ai, bi, keep_pseudo());
}

/* ═════════════════════════════════════════════════════════════════════
 *  ★★ #3595 (ひさ確定仕様 2026-09-24): `use 式;` の **検査**
 *
 *   従来 use は純粋なパーサの糖衣 (`var USE_MODULES = 式;`) で、列が正しいかは
 *   **候補列を実際に引くとき** = 最初の幾何 op の振り分けでしか見られなかった。
 *   ⇒ その回に幾何 op が 1 つも走らないと、列が丸ごと空振りしていても黙って通る。
 *
 *   ここでは **use の行**で同じことを見る。★ op 実行時点の検査は **そのまま残す** (二重)。
 *
 *   ⚠⚠ 見るのは「**1 本も解決しない**」ことだけ。`use ["cgal","nosush"]` は cgal が居れば通る
 *     (「未ロード名は飛ばす」は既存規則・変えない) ⇒ *綴り間違いの検出器ではない*。
 *   ★ 値は **そのまま返す** (恒等)。束縛は従来どおり pigfAssign が DEF で行うので、
 *     「ブロック / lambda を抜けると外の値へ戻る」という use の肝は変わらない。
 *   ★ 判定は @read_cand_list@ + 解決の規則を **振り分けと共有**している。別に書くと、
 *     use が通したのに op で落ちる (またはその逆) という二重帳簿になる。
 * ═════════════════════════════════════════════════════════════════════ */
void
pigDataOperatorUse::_start()
{
	if ( args.length() < 1 ) {
		result = thNEW(pigDataError,("use needs a module candidate list", info, PE_FATAL));
		return;
	}
	sPtr<pigData> v = args[0]->compact();
	if ( v->is_error() ) { result = v; return; }

	pigModCands c;
	sPtr<pigData> cerr;
	if ( ! read_cand_list(v, c, &cerr) ) { result = cerr; return; }
	/* ★ `use ""` / `use null` / `use 0` は **指名なしへ戻す**の意 ⇒ 検査するものが無い。
	 *   ⚠ 列そのものが穴の場合の話で、*要素* の穴 (["", "cgal"]) とは別の枝 (read_cand_list)。 */
	if ( ! c.given ) { result = v; return; }

	sPtr<pigModuleRegistry> reg = pig_current_registry();
	if ( reg == thNULL ) {
		result = thNEW(pigDataError,("use: no module registry (no app)", info, PE_FATAL));
		return;
	}
	/* ★ 解決は振り分けと同じ規則: 擬似モジュール (ハッシュ) は常に解決できた側 /
	 *   実名は **ロード済み** (id > 0 かつ記述子あり) だけ。op 名は見ない (まだ op が無い)。 */
	int nres = 0;
	for ( size_t i = 0 ; i < c.vals.size() ; ++i ) {
		if ( c.vals[i].is_notNull() && c.vals[i]->obt_hash().is_notNull() ) { ++nres; continue; }
		int id = reg->id_of_name(c.names[i].c_str());
		if ( id > 0 && reg->descriptor(id) != 0 ) ++nres;
	}
	if ( nres > 0 ) { result = v; return; }

	/* ⓪ 要素が 1 つも無い (空配列 / 全部穴)。文言は振り分け側と揃える。
	 *   ★★ ただし **列が @mod_only@ から来た**ときは、原因も直し方も別物なので言い分ける:
	 *     「空の列を書いた」のではなく「**いま効いている列と、この場所が対応する集合が交差しない**」。
	 *     `""` を書け、という助言はここでは的外れになる (ライブラリ関数の宣言でこの形が普通に出る)。
	 *   ⚠ 判定は *値* ではなく **引数ノードの種類**で行う — 値 (空配列) からは出どころが分からない。 */
	{
		sPtr<pigDataOperatorModOnlyUse> m1 = sPtr<pigDataOperatorModOnlyUse>::d_cast(args[0]);
		sPtr<pigDataOperatorModOnly>    m2 = sPtr<pigDataOperatorModOnly>::d_cast(args[0]);
		if ( c.nelem == 0 && ( m1.is_notNull() || m2.is_notNull() ) ) {
			/* ★ 札は **実際に書かれた op 名**にする。@mod_only_names@ は上の 2 つの派生なので
			 *   d_cast は両方に当たる ⇒ 先に派生を見ないと文言が `mod_only` に化ける。 */
			int names = ( sPtr<pigDataOperatorModOnlyNames>::d_cast(args[0]).is_notNull() ||
			              sPtr<pigDataOperatorModOnlyNamesUse>::d_cast(args[0]).is_notNull() );
			std::string m = std::string("use ") + ( names ? "mod_only_names" : "mod_only" )
			    + "(...): none of the modules in effect is supported here "
			      "(the candidate list in effect and the supported set do not overlap). "
			      "Either widen the `use` list at the call site, or load a module this code supports "
			      "(`srava --modules` shows what is loaded)";
			result = thNEW(pigDataError,(m.c_str(), info, PE_FATAL));
			return;
		}
	}
	if ( c.nelem == 0 ) {
		std::string m = "use: the module candidate list is empty (";
		if ( c.nholes > 0 ) {
			char nb[96];
			::snprintf(nb, sizeof nb, "all %d element(s) are holes (null / \"\" / 0) "
			                          "and were skipped; ", c.nholes);
			m += nb;
		}
		m += "an empty list selects no module; write \"\" to let the planner choose)";
		result = thNEW(pigDataError,(m.c_str(), info, PE_FATAL));
		return;
	}
	/* ① 書いてはあるが **どれもロードされていない**。 */
	std::string m = "use (" + cand_names_str(c) + "): no such module is loaded "
	                "(names that are not loaded are skipped, but here none of them resolved; "
	                "check them against `srava --modules`, or load them with "
	                "module([\"<name>\", ...], {}))";
	result = thNEW(pigDataError,(m.c_str(), info, PE_FATAL));
}

void
pigDataOperatorModules::_start()
{
	/* 形の指定 (省略 = 配列)。★ 名前の文字列なので compact してよい (幾何を force しない)。 */
	int wantPriority = 0;
	if ( args.length() >= 1 ) {
		sPtr<pigData> fv = args[0]->compact();
		if ( fv->is_error() ) { result = fv; return; }
		sPtr<stdString> fs = fv->get_str();
		std::string f = fs.is_notNull() ? fs->get_str() : "";
		if ( f == "priority" ) wantPriority = 1;
		else {
			std::string m = "modules: unknown form '" + f
			              + "' (modules() = array of module names in dispatch order; "
			                "modules(\"priority\") = \"name:priority\" string)";
			result = thNEW(pigDataError,(m.c_str(), info, PE_FATAL));
			return;
		}
	}
	sPtr<pigModuleRegistry> reg = pig_current_registry();
	if ( reg == thNULL ) {
		result = thNEW(pigDataError,("modules: no module registry (no app)", info, PE_FATAL));
		return;
	}
	std::vector<int> ids;
	pig_modules_by_priority(reg, ids);
	if ( wantPriority ) {
		std::string s;
		for ( size_t i = 0 ; i < ids.size() ; ++i ) {
			const char *nm = reg->name_of_id(ids[i]);
			if ( nm == 0 ) continue;
			char buf[128];
			::snprintf(buf, sizeof buf, "%s%s:%d", s.empty() ? "" : " ", nm, reg->priority(ids[i]));
			s += buf;
		}
		result = thNEW(pigDataString,(s.c_str()));
		return;
	}
	sPtr<pigDataArray> a = thNEW(pigDataArray,());
	for ( size_t i = 0 ; i < ids.size() ; ++i ) {
		int m = ids[i];
		/* ★★ 述語は「候補列に書いて **勝ちうる**もの」= **op 行を 1 つでも持つ**こと。
		 *   落ちるのは ① 番兵 (id 0 = "delayed") ② 記述子を持たない登録
		 *   ③ ops 表を持たない記述子 — 組込の "pig" (D_REF codec 専用) がこれに当たる。
		 *   ⚠ ③ を落として安全なのは、@op_row@ が同じ条件で 0 を返す = **sig_dispatch が
		 *     構造的に選べない**ため。⇒ 配列から抜いても解決結果は 1 つも変わらない
		 *     (codec の検索は記述子走査で、候補列を通らない — export も cache も無傷)。
		 *   ★ 逆に **入れると害がある**: ops 表が無いと supports_op が -1 を返すので、
		 *     「どれもその op を持たない」の診断が握り潰される (上の withOp の注記)。 */
		const srava_module_descriptor *d = ( m > 0 ) ? reg->descriptor(m) : 0;
		if ( d == 0 || d->ops == 0 || d->n_ops <= 0 ) continue;
		const char *nm = reg->name_of_id(m);
		if ( nm == 0 ) continue;
		a->push_nocheck(thNEW(pigDataString,(nm)));
	}
	result = a;
}

/* ★★ 内省 op (type_of / kind_of) の共通前処理 (ひさ 2026-09-21):
 *   **compact して、エラーなら伝播する**。それだけ。
 *
 * ⚠⚠ これが無かったときの実害: エラーは **作られていたのに読み捨てられて**いた。
 *   arg_type_set は @is_cache()@ を訊く — これは pigDataDelay の compact ゲートウェイなので、
 *   引数は以前から暗黙に compact されていた。にもかかわらず @is_error()@ を誰も訊かないので、
 *   エラー値の is_cache() が 0 を返し、"" → **"value"** に落ちていた
 *   (実測: @type_of(nosuchvar)@ → "value" ・ エラー表示なし ・ 終了コードも正常)。
 *   ⇒ 「compact していないからエラーが出ない」のではなく、**compact の結果を見ていなかった**。
 *
 * ★★ **継続の実値までは辿らない** (ひさ判断 2026-09-21: 案③は今回なし)。
 *   @cdr()->cdr()->compact()@ まで待っても **型の答えは 1 文字も変わらない** — 継続 pair の car と
 *   pigDataCache::type_stamp() には *同じ文字列* が載るため (pigfAgent::stamp_out_cache)。
 *   実測でも cold / warm ・ 10 値すべて一致した。⇒ 得る物が無いのに、内省 op を挿しただけで
 *   **同期点ができる** (上流の agent の完了を待つ) 代償だけが残る。
 *   ⚠ 引き換えに残る限界: **agent の中で失敗した計算には、宣言された型を答える**
 *     (@type_of(points3d("not an array"))@ → "pt-cloud3d")。待たない以上、失敗をまだ観測できない。
 *     ここを塞ぐなら案③ (実値まで待つ) になるが、その判断は今回見送った。
 * 戻り: compact 済みの値。err が非 thNULL ならそれを結果にして返すこと。 */
static sPtr<pigData>
introspect_compact(sPtr<pigData> a, sPtr<pigData> &err)
{
	err = thNULL;
	sPtr<pigData> v = a->compact();
	if ( v->is_error() ) err = v;
	return v;
}

void
pigDataOperatorTypeOf::_start()
{
	if ( args.length() < 1 ) {
		result = thNEW(pigDataError,("type_of needs one argument", info, PE_FATAL));
		return;
	}
	sPtr<pigData> err;
	sPtr<pigData> v = introspect_compact(args[0], err);
	if ( err != thNULL ) { result = err; return; }
	/* ★ 型の出どころは arg_type_set 1 本 — 継続なら car ・ キャッシュなら type_stamp()。
	 *   **どちらも同じ文字列**なので、cold と warm で答えが変わらない。 */
	std::string t = arg_type_set(v);
	if ( t.empty() && v->type_name() != 0 )
		t = v->type_name();                            /* in-proc で実体化済みの本体 */
	if ( t.empty() ) t = "value";                          /* スカラ・文字列・配列は幾何型を持たない */
	result = thNEW(pigDataString,(t.c_str()));
}

void
pigDataOperatorKindOf::_start()
{
	if ( args.length() < 1 ) {
		result = thNEW(pigDataError,("kind_of needs one argument", info, PE_FATAL));
		return;
	}
	sPtr<pigData> err;
	sPtr<pigData> v = introspect_compact(args[0], err);
	if ( err != thNULL ) { result = err; return; }   /* エラーは種別に化けさせず伝播 */

	/* ★★ 型名そのものはここでは返さない — それは type_of() の軸。種別としては
	 *   pt-cloud3d も oc-brep3d も ref も等しく **キャッシュハンドル** (ひさ 2026-09-21)。
	 * ★ 幾何は type_of と同じく **待たない** — 種別は型スタンプだけで決まるので、
	 *   実値を待っても "cache" は "cache" のまま。 */
	if ( ! arg_type_set(v).empty() ) {
		result = thNEW(pigDataString,("cache"));
		return;
	}
	const char *k;
	if      ( v->is_cache() || v->type_name() != 0 ) k = "cache";
	else if ( v->is_int() )                      k = "int";
	else if ( v->is_flt() )                      k = "float";
	else if ( v->obt_array().is_notNull() )      k = "array";
	else if ( v->obt_hash().is_notNull() )       k = "hash";
	/* ⚠ ここから下は pigData に述語が無いので d_cast。**libpig の中**なので 1 イメージに閉じ、
	 *   しかも **compact 済み**の値に対して訊いているので、遅延ノードで嘘をつく穴も無い
	 *   (モジュールから訊く口ではないため、述語を増やして ABI を上げるには当たらないと判断した)。 */
	else if ( sPtr<pigDataString>::d_cast(v).is_notNull() ) k = "string";
	else if ( sPtr<pigDataLambda>::d_cast(v).is_notNull() ) k = "function";
	else if ( sPtr<pigDataNull>::d_cast(v).is_notNull() )   k = "null";
	else                                         k = "unknown";   /* ⚠ 黙って "value" に寄せない */
	result = thNEW(pigDataString,(k));
}

void
pigDataOperatorWhich::_start()
{
	if ( args.length() < 1 ) {
		result = thNEW(pigDataError,("which needs an op name", info, PE_FATAL));
		return;
	}
	sPtr<pigData> ov = args[0]->compact();
	if ( ov->is_error() ) { result = ov; return; }
	std::string op = ov->get_str()->get_str();
	/* 2 番目以降は絞り込みの入力型 (省略可)。★ ここは型 **名** の文字列なので compact してよい。 */
	std::vector<std::string> want;
	for ( int k = 1 ; k < args.length() ; ++k ) {
		sPtr<pigData> tv = args[k]->compact();
		if ( tv->is_error() ) { result = tv; return; }
		std::string t = tv->get_str()->get_str();
		if ( ! t.empty() ) want.push_back(t);
	}
	sPtr<pigModuleRegistry> reg = pig_current_registry();
	if ( reg == thNULL ) {
		result = thNEW(pigDataError,("which: no module registry (no app)", info, PE_FATAL));
		return;
	}
	std::vector<int> ids;
	pig_modules_by_priority(reg, ids);
	std::string s;
	for ( size_t i = 0 ; i < ids.size() ; ++i ) {
		int m = ids[i];
		if ( reg->supports_op(m, op.c_str()) != 1 ) continue;
		/* 入力型が指定されていれば、**すべて**を受理する候補だけ残す。 */
		int ok = 1;
		for ( size_t j = 0 ; ok && j < want.size() ; ++j )
			if ( ! sig_accepts_input(reg, m, op.c_str(), want[j]) ) ok = 0;
		if ( ! ok ) continue;
		const char *nm = reg->name_of_id(m);
		/* ★ #3554 最後の段 4/5: **行を全部つなぐ**。op_sig() は最初の候補行しか返さないので、
		 *   変種行に分かれた op (cast / import) では which が申告の一部しか見せない。
		 *   ⚠ 表示の形 (module:priority:sig) は変えない — 検定と利用者の目が当てている。
		 *     複数行は `;` でつなぐ = sig 自身の区切りと同じなので読み方が増えない。 */
		std::string sg;
		for ( int ci = 0 ; ; ++ci ) {
			const pigOpEntry *row = reg->op_row(m, op.c_str(), ci);
			if ( row == 0 ) break;
			if ( row->sig == 0 || row->sig[0] == '\0' ) continue;
			if ( ! sg.empty() ) sg += ";";
			sg += row->sig;
		}
		char buf[512];
		::snprintf(buf, sizeof buf, "%s%s:%d:%s", s.empty() ? "" : " ",
		    nm ? nm : "?", reg->priority(m), sg.empty() ? "(none)" : sg.c_str());
		s += buf;
	}
	result = thNEW(pigDataString,(s.c_str()));
}

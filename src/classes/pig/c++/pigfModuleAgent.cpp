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
#include	"pig/c++/pigTypeRegistry.h"     /* rev4 Phase B-2b: 型ディスパッチ (op_sig の型と入力型の照合) */
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
	virtual int		decide_out_module();
	int			decide_executor(const char *op);   /* rev4 B-2b: 型ディスパッチ (解決不能 -1) */
private:
	TS_DEFARGS
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
    TS_CPARGS0
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
	const char *cmd = ::getenv("SRAVA_AGENT");
	if ( cmd == 0 )
		cmd = SRAVA_AGENT_DEFAULT;   /* install 先 (prefix から生成・Windows は .exe 付き) */
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
	sPtr<stdString> opn = ( front.is_notNull() ) ? front->get_op_name() : sPtr<stdString>();
	if ( opn.is_notNull() )
		op = opn->get_str();
	int line = ( front.is_notNull() && front->get_info().is_notNull() )
	         ? front->get_info()->get_lineno() : 0;
	/* 元ソースのファイル名(basename)も付ける(agentwatch で「演算名 ファイル名 行番号」表示用)。
	 * include されたファイルの op を区別できる。 */
	const char *fnsrc = "-";
	if ( front.is_notNull() && front->get_info().is_notNull()
	     && front->get_info()->get_filename().is_notNull() ) {
		fnsrc = front->get_info()->get_filename()->get_str();
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
 *   戻り 0=非該当(agent 起動へ) / 1=front に結果セット済み / 2=err セット済み。 */
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
			err = thNEW(pigDataError,(buf, front->get_info()));
			return 2;
		}
	}
	if ( isbool && args.length() == 2 ) {
		if ( srava_is_identity(args[1]) ) {            /* a op {} = a ({} op {} = {}) */
			front->set_result(args[0]);
			return 1;
		}
		if ( srava_is_identity(args[0]) ) {            /* {} op a */
			front->set_result( ::strcmp(op,"difference")==0 ? args[0]    /* {} --- a = {} */
			                                                : args[1] ); /* {} |||/&&&/+++ a = a */
			return 1;
		}
	}
	if ( ::strcmp(op,"export")==0 && args.length() >= 2 && srava_is_identity(args[1]) ) {
		err = thNEW(pigDataError,("export: empty mesh {} cannot be exported", front->get_info()));
		return 2;
	}
	/* 値返し op に {}: 空集合の自然な値で短絡(ガード `if(valid(acc)==1)` を書けるように)。 */
	if ( args.length() == 1 && srava_is_identity(args[0]) ) {
		if ( ::strcmp(op,"valid")==0 ) {
			front->set_result(thNEW(pigDataInteger,((INTEGER64)0)));
			return 1;
		}
		if ( ::strcmp(op,"volume")==0 || ::strcmp(op,"area")==0 || ::strcmp(op,"perimeter")==0 ) {
			front->set_result(thNEW(pigDataFloat,((double)0.0)));
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

/* 引数 1 個の **候補型集合** (CSV)。継続 = スタンプ型リスト・HIT cache = 4CC→型・値/スカラ = 空。 */
static std::string
arg_type_set(sPtr<pigData> v)
{
	if ( pig_is_delayed(v) )
		return v->car()->get_str()->get_str();   /* 型名リスト CSV (B-2a スタンプ)。非ブロッキング */
	if ( v->is_cache() ) {
		const char *t = v->type_name();           /* pigDataCache override: 先頭 D_META 4CC→型名 */
		return t ? std::string(t) : std::string();
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

/* CSV ("a,b,c") に tok が含まれるか。 */
static bool
csv_has(const std::string& csv, const std::string& tok)
{
	size_t p = 0;
	while ( p <= csv.size() ) {
		size_t c = csv.find(',', p);
		size_t e = ( c == std::string::npos ) ? csv.size() : c;
		if ( csv.compare(p, e - p, tok) == 0 ) return true;
		if ( c == std::string::npos ) break;
		p = c + 1;
	}
	return false;
}

/* 1 シグネチャ "(a,b)->c" を入力型ベクタと出力型に parse。先頭に '(' が無い ("->c") = 入力 0。 */
static void
parse_sig(const std::string& s, std::vector<std::string>& ins, std::string& out)
{
	ins.clear(); out.clear();
	size_t arrow = s.find("->");
	std::string lhs = ( arrow == std::string::npos ) ? s : s.substr(0, arrow);
	out = ( arrow == std::string::npos ) ? std::string() : s.substr(arrow + 2);
	/* lhs = "(a,b)" or "" */
	size_t lp = lhs.find('('), rp = lhs.find(')');
	if ( lp == std::string::npos || rp == std::string::npos || rp <= lp + 1 ) return;   /* 入力 0 */
	std::string inner = lhs.substr(lp + 1, rp - lp - 1);
	size_t p = 0;
	while ( p <= inner.size() ) {
		size_t c = inner.find(',', p);
		size_t e = ( c == std::string::npos ) ? inner.size() : c;
		ins.push_back(inner.substr(p, e - p));
		if ( c == std::string::npos ) break;
		p = c + 1;
	}
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
		std::vector<std::string> ins; std::string out;
		parse_sig(one, ins, out);
		for ( size_t i = 0 ; i < ins.size() ; ++i )
			if ( ins[i] == type ) return 1;
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
		std::vector<std::string> ins; std::string out;
		parse_sig(one, ins, out);
		if ( out == type ) return 1;
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
		if ( ! reg->is_enabled(m) ) continue;
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
int
pigfModuleAgent_::decide_executor(const char *op)
{
	/* 文字列引数でカーネルが決まる op (cast=目標カーネル名・import/export=拡張子) は型では振れない
	 *   → -1 で既存の専用ロジックへ委ねる。 */
	if ( ::strcmp(op, "cast") == 0 || ::strcmp(op, "import") == 0 ||
	     ::strcmp(op, "export") == 0 || ::strcmp(op, "export_vox") == 0 )
		return -1;

	/* 幾何入力の候補型集合を集める (値/スカラは除外)。 */
	std::vector<std::string> insets;
	for ( int k = 0 ; k < args.length() ; ++k ) {
		std::string ts = arg_type_set(args[k]);
		if ( ts.empty() ) continue;
		if ( ts.find(',') != std::string::npos )
			return -1;   /* 多候補 (未確定/polymorphic 上流) = 型が絞れない → 保守的にフォールバック */
		insets.push_back(ts);
	}

	sPtr<pigModuleRegistry> reg = ( ptsApp != thNULL ) ? ptsApp->module_registry
	                                                   : sPtr<pigModuleRegistry>(thNULL);   /* ★ #3427 ③ */
	if ( reg == thNULL )
		return -1;
	int nmod = reg->count();
	/* ★ 直接一致のみの単パス (旧 pass 1 の coercion は撤去)。クロスカーネルの受理は各 op の sig に
	 *   foreign 入力型を **明示列挙** する方式へ移行 (cgal は universal reader なので (cg,mf)/(mf,cg) 等を
	 *   直接持つ・all-foreign は自型カーネルが持つので書かない = sig が disjoint で priority 曖昧なし)。 */
	int best = -1; long bestPrio = LONG_MIN; std::string bestOut;
	for ( int m = 1 ; m < nmod ; ++m ) {
		if ( ! reg->is_enabled(m) ) continue;   /* agent("so","off") で無効化 = 候補外 */
		const char *sig = reg->op_sig(m, op);
		if ( sig == 0 ) continue;   /* この module は op 未注釈 */
		/* sig を ';' で分割し各シグネチャを照合。 */
		std::string all = sig;
		size_t sp = 0;
		while ( sp <= all.size() ) {
			size_t sc = all.find(';', sp);
			std::string one = all.substr(sp, ( sc == std::string::npos ? all.size() : sc ) - sp);
			std::vector<std::string> ins; std::string out;
			parse_sig(one, ins, out);
			if ( (int)ins.size() == (int)insets.size() ) {
				bool ok = true;
				for ( size_t i = 0 ; i < ins.size() ; ++i )
					if ( ! csv_has(insets[i], ins[i]) ) { ok = false; break; }   /* 直接一致のみ */
				if ( ok ) {
					long pr = reg->priority(m);
					if ( pr > bestPrio ) { bestPrio = pr; best = m; bestOut = out; }
				}
			}
			if ( sc == std::string::npos ) break;
			sp = sc + 1;
		}
	}
	if ( best >= 0 ) {
		outTypeList = ( bestOut.empty() || bestOut == "value" )
		              ? thNULL                                    /* 値出力 = 型なし → 継続スタンプ非対象 */
		              : thNEW(stdString,(bestOut.c_str()));
		return best;
	}
	return -1;   /* 型ディスパッチで解決できず → 既存ロジックへ */
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
		err = thNEW(pigDataError,(buf, front->get_info(), 1));   /* fatal */
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
			 *   cast sig の出力型が目標型に一致するモジュールを選ぶ = 型軸。各モジュールの cast 出力型は
			 *   自カーネル型に閉じている (cgal→cg-*・mf→mf-*) ので目標型で一意に決まる。cast の calc body は
			 *   identity で、foreign 型入力の昇格読みは行き先モジュールの reader が担う。 */
			int nmod = reg->count();
			for ( int m = 1 ; m < nmod ; ++m ) {
				if ( ! reg->is_enabled(m) ) continue;
				if ( sig_produces(reg, m, "cast", tname) ) {
					outTypeList = thNEW(stdString,(tname));   /* 出力型 = 目標型 */
					return m;
				}
			}
		}
		/* 未知の目標型名 / 産出できるモジュール無し → 下の一般ロジックへ (フォールバック)。 */
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
				if ( ! reg->is_enabled(m) ) continue;
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
			/* どのカーネルも読めない拡張子 → 下の一般ロジック (leaf → 既定カーネル) へ。cgaImport が
			 *   実行時に明示エラーを出す (従来 idCgal フォールバックと同じ行き先)。 */
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
			if ( meshK > 0 && reg->can_export_ext(meshK, ext) == 1 )
				return meshK;                                   /* ① 自カーネルが書ける */
			/* ② 書ける & mesh を **読める** priority 最大。読解 capability は旧 can_read_module を廃し、
			 *   export sig が入力型を受理するかで判定 (Stage 2・型軸)。入力型が不定/多候補なら保守的に読める扱い。 */
			bool typed = ! inType.empty() && inType.find(',') == std::string::npos;
			int best = -1; long bestPrio = LONG_MIN;
			int nmod = reg->count();
			for ( int m = 1 ; m < nmod ; ++m ) {
				if ( ! reg->is_enabled(m) ) continue;
				if ( reg->can_export_ext(m, ext) != 1 ) continue;
				if ( typed && ! sig_accepts_input(reg, m, "export", inType) ) continue;
				long pr = reg->priority(m);
				if ( pr > bestPrio ) { bestPrio = pr; best = m; }
			}
			if ( best >= 0 )
				return best;
			/* 書けるカーネルが無い → 下の一般ロジック (mesh の自カーネルへ) → cgaExport が実行時エラー。 */
		}
	}

	/* ★ rev4 Phase B-2b: 型ディスパッチを先に試す。(op, 入力型[]) が注釈済み handler で確定できれば
	 *   そのモジュールへ (offset の次元・op-owner・既定カーネルを型で統一的に解決)。解決不能 (未注釈 op /
	 *   入力型が多候補で未確定 / cast・import・export) は -1 が返り、下の既存カーネルロジックへフォールバック
	 *   (op 単位 coexistence)。全 op が精密な単一型を伝播できるようになれば下のロジックと名指しは撤去可。 */
	{
		int te = decide_executor(op);
		if ( te >= 0 )
			return te;
	}

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

	/* ★ op-owner ルーティング (単一モジュール op の owner 直送・保留解除 2026-08-08)。
	 *   op を **明示的に持つ** (supports_op==1) モジュールがちょうど 1 つなら、下の入力タグ伝播より
	 *   優先してそのモジュールへ流す。狙い:
	 *     - 新カーネルの novel op を owner へ確実に届ける (leaf でも既定カーネルに奪われない)。
	 *     - **複数カーネルが同一メッシュ形式 (同 4CC) を共有** する場合、op を呼んだ側のカーネルへ
	 *       正しく振る (メッシュのタグ owner ではなく **op の owner** で決める)。例: k3/k4 が同じ
	 *       NEWM を I/O するとき、k4op(k3 が作った NEWM) は k4 へ (タグ owner の k3 ではなく)。
	 *   ここは decide_executor が -1 (未型付/多候補) のフォールバック。typed 入力は上で解決済。
	 *     - cgal/manifold 共通 op (box/union/volume 等) は owner 2 個 → 発火せず入力伝播へ。
	 *     - cgal 専用 op (tube/pyramid/perimeter 等) は owner 1 個で行き先も元々 cgal (結果不変)。 */
	{
		int owner = -1, nowner = 0;
		int nmod = reg->count();
		for ( int m = 1 ; m < nmod ; ++m ) {
			if ( ! reg->is_enabled(m) ) continue;   /* 無効カーネルは owner 候補外 */
			if ( reg->supports_op(m, op) == 1 ) { owner = m; ++nowner; }
		}
		/* op を持つのが 1 モジュールだけなら owner へ直送 (novel op を確実に owner へ・共有形式でも op の
		 *   owner で決める)。読解可否の事前判定 (旧 can_read_module) は撤去 = owner が読めなければ owner の
		 *   agent が実行時にエラーにする (typed 入力は上の decide_executor が既に解決済なのでここは -1 の
		 *   フォールバック=未型付/多候補入力のみ)。 */
		if ( nowner == 1 )
			return owner;
	}

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
		err = thNEW(pigDataError,(buf, front->get_info(), 1));   /* fatal */
		return MODULE_NONE;
	}
	return modId;
}

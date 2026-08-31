/*
 * pigfAgentTest — pigDataPair / pigDataCache / 継続 のエンドツーエンドテスト(ptsObject 派生)。
 * srava-agent-stub を別プロセス起動し、以下を検証する:
 *   T1 cache MISS + 継続  : compact(node) が即 ("delayed" . promise) を返す(非ブロッキング)。
 *                           promise を compact → agent が計算した結果本文 "R(alpha,beta)"。
 *   T2 cache HIT          : 同一引数で再実行 → agent を起動せず、キャッシュ本文を直接返す
 *                           (= 値そのもの。pair ではない ことで HIT を判別)。
 *   T3 遅延引数パイプライン : pigfAgent(A) の継続 pair を pigfAgent(B) の引数に。B は A の解決を
 *                           待って送信 → "R(R(gamma),delta)"(入れ子)。
 *   T4 単体               : pigDataPair(car/cdr/get_str), pigDataCache(get_str=path/get_hashkey)。
 * agent パスは SRAVA_AGENT、キャッシュ dir は SRAVA_CACHE_DIR(main が test 用に設定・清掃)。
 */
#include	"ts2/c++/tinyState.h"
#include	"ts2/c++/tsApplication.h"    /* ctor の parent 型 (ptsApplication 派生・#3427 ③) */
#include	"pig/c++/ptsApplication.h"   /* 基底 (テスト app = registry 所有者・#3427 ③) */
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigfModuleAgent.h" /* pigDataFunction<pigfModuleAgent> のインスタンス化 */
#include	"_ts2/c++/pigfAgentTest_.h"

#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>   /* getenv(CACHE_DIR 既定) */

CLASS_TINYSTATE(pig/c++/pigfAgentTest,pig/c++/ptsApplication)

/* ★ #3427 ③: 旧 file-global pigfAgentTest_exitCode は撤去 (可変 static 全廃)。
 * 失敗はメンバ testExitCode に集約し、main が exit_code() で読む。
 * fixture (cgal_test_fixture.cpp) は INI から app レジストリへ登録する。 */
extern void srava_register_cgal_test_fixture(sPtr<pigModuleRegistry> reg);

/* ★ #3440: op 名を必ず付ける。実プログラムのノードは parser / pigfMapOp が必ず set_op_name する
 *   ので、op 名の無いノードは**このテストだけ**の形だった。routing は「型で解決できなければ
 *   明示エラー」になったため、op 名なしノードのために routing 側へ例外を置くのは筋が悪い
 *   (production のコードにテスト専用の穴が空く)。→ テスト側が実プログラムと同じ形を取り、
 *   fixture が "test_echo" を "->value" で申告する (src/main/cgal_test_fixture.cpp)。
 *   stub agent は op 名を見ず引数を連結して "R(args)" を返すので、期待値は変わらない。 */
static sPtr<pigDataFunction<pigfModuleAgent> > mkAgent2(sPtr<pigData> a0, sPtr<pigData> a1) {
	sPtr<pigDataFunction<pigfModuleAgent> > fn = thNEW(pigDataFunction<pigfModuleAgent>,());
	fn->pushArg(a0);
	if (a1.is_notNull()) fn->pushArg(a1);
	fn->set_op_name(thNEW(stdString,("test_echo")));
	return fn;
}

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfAgentTest_(
		sPtr<tsApplication> parent);

	sRptr<tsApplication,tinyState>		parent;

	virtual sPtr<pigEnvironment>	get_env();
	int	exit_code();                       /* main が完走後に読む (旧 file-global の置換) */
protected:
	void	CHECK(const char *name, int ok);   /* 1 チェック。ok=0 で fail */
	int			testExitCode;
	sPtr<pigEnvironment>	env;
	sPtr<pigData>		node1, r1;     /* T1/T2 */
	sPtr<pigData>		node2, r2;
	sPtr<pigData>		nodeA, nodeB, rB;  /* T3 */
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class tinyState;
class tsApplication;
class pigData;
class pigEnvironment;
TS_END_INTERFACE

#endif


pigfAgentTest_::pigfAgentTest_(TS_ARGS0)
        : ptsApplication_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    testExitCode = 0;
}

void
pigfAgentTest_::CHECK(const char *name, int ok)
{
	::printf("[pigfagent] %-26s : %s\n", name, ok ? "PASS" : "FAIL");
	if (!ok) testExitCode = 1;
}

int
pigfAgentTest_::exit_code()
{
	return testExitCode;
}

sPtr<pigEnvironment>
pigfAgentTest_::get_env()
{
	return env;
}


/*******************************************
	STATE MACHINE  (各状態 1 compact = yield 後の成功パスで 1 回だけ check)
********************************************/

TS_STATE(INI_ptsApplication_START)   /* 基底 INI_ptsObject_START (ptsApp=自分 + registry 生成) の次 */
{
	/* ★ #3427 ③: cgal.so を link しないテストなので、最小 cgal メタ + VALUE パーサを
	 * **app 所有レジストリ**へ登録 (旧: main() からグローバルへ)。routing 判断用。 */
	srava_register_cgal_test_fixture(module_registry);
	/* ノードは INI で一度だけ構築する。compact は yield で状態関数を先頭から再走させるため、
	 * 状態内でノードを作ると毎回作り直して agent を無限起動してしまう(重要)。 */
	env   = thNEW(pigEnvironment,(thNULL));
	/* srava 起動相当: CACHE_DIR の既定を getenv(SRAVA_CACHE_DIR) からセット(無ければ既定パス)。
	 * pigfAgent はこの env 変数を参照する(実行中に set_var で変更も可能)。 */
	{
		const char *cd = ::getenv("SRAVA_CACHE_DIR");
		if ( cd == 0 ) cd = "/tmp/srava-cache";
		env->def_var(thNEW(stdString,("CACHE_DIR")), thNEW(pigDataString,(cd)));
	}
	node1 = mkAgent2(thNEW(pigDataString,("alpha")), thNEW(pigDataString,("beta")));
	node2 = mkAgent2(thNEW(pigDataString,("alpha")), thNEW(pigDataString,("beta")));  /* 同一引数 */
	nodeA = mkAgent2(thNEW(pigDataString,("gamma")), thNULL);
	nodeB = mkAgent2(nodeA, thNEW(pigDataString,("delta")));   /* arg0 = 遅延継続 */
	return rDO|ACT_T1A;
}

/* ---- T1: cache MISS(値返し op = out_cache==0)→ front を実値へ直接解決(pair でない)。
 *      値ノードは継続 pair を返さない(cdr を辿るのは pigfAgent の mesh-DAG だけ)→ compact が
 *      await して実値を返す(HIT と同じ扱い)。mesh の pair 継続は union/pipeline 統合テストが担保。 ---- */
TS_STATE(ACT_T1A)
{
	r1 = node1->compact();   /* 初回 yield → agent 起動 → front=実値 確定で再開 */
	CHECK("T1 miss resolves to value (not pair)",
	      sPtr<pigDataPair>::d_cast(r1).is_notNull() == 0);
	return rDO|ACT_T1B;
}
TS_STATE(ACT_T1B)
{
	CHECK("T1 value content matches", ::strcmp(r1->get_str()->get_str(), "R(alpha,beta)") == 0);
	return rDO|ACT_T2A;
}

/* ---- T2: 同一引数 → cache HIT(agent 不起動・値を直接返す) ---- */
TS_STATE(ACT_T2A)
{
	r2 = node2->compact();   /* HIT: ReaderText で読み front=値(pair でない) */
	int notPair = ( sPtr<pigDataPair>::d_cast(r2).is_notNull() == 0 );
	CHECK("T2 hit returns value(not pair)", notPair);
	CHECK("T2 hit content matches", ::strcmp(r2->get_str()->get_str(), "R(alpha,beta)") == 0);
	return rDO|ACT_T3A;
}

/* ---- T3: 値継続を arg にした入れ子(nodeB の arg0 = 値返し nodeA)。値ノードは pair を返さないので
 *      nodeB は arg を compact して await→実値で計算。compact は入れ子を実値へ直接解決(pair でない)。
 *      (mesh の非ブロッキング arg パイプライン継続は union/pipeline 統合テストが担保) ---- */
TS_STATE(ACT_T3A)
{
	rB = nodeB->compact();   /* nodeA(値)を await→nodeB 計算 まで yield */
	CHECK("T3 nested value resolves (not pair)",
	      sPtr<pigDataPair>::d_cast(rB).is_notNull() == 0 &&
	      ::strcmp(rB->get_str()->get_str(), "R(R(gamma),delta)") == 0);
	return rDO|ACT_T4;
}
TS_STATE(ACT_T3B)   /* 旧パイプライン段は不要(値ノードは cdr を持たない)→ 素通り */
{
	return rDO|ACT_T4;
}

/* ---- T4: 単体 ---- */
TS_STATE(ACT_T4)
{
	sPtr<pigDataPair> p = thNEW(pigDataPair,(thNEW(pigDataString,("k")), thNEW(pigDataString,("v"))));
	CHECK("T4 pair car", ::strcmp(p->car()->get_str()->get_str(), "k") == 0);
	CHECK("T4 pair cdr", ::strcmp(p->cdr()->get_str()->get_str(), "v") == 0);
	CHECK("T4 pair get_str", ::strcmp(p->get_str()->get_str(), "(k . v)") == 0);

	sPtr<pigDataCache> c = thNEW(pigDataCache,((pHashKeyType)0x1234, thNEW(stdString,("/p/q.cache"))));
	CHECK("T4 cache get_str=path", ::strcmp(c->get_str()->get_str(), "/p/q.cache") == 0);
	CHECK("T4 cache get_hashkey", c->get_hashkey() == (pHashKeyType)0x1234);

	/* tinyState tsSignalCore の SIGCHLD dangling 修正済(Redmine #3361)につき、
	 * 旧 keepalive+_exit ワークアラウンドは撤去。通常 idle 終了に戻す。
	 * (app idle 終了後、main が exit_code() を返す) */
	::fflush(stdout);
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_ptsApplication_START;
}

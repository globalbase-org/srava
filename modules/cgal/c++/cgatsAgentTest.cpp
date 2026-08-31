/*
 * cgatsAgentTest — cgatsAgent(実エージェント srava_agent)の end-to-end テスト(ptsObject 派生)。
 * プランナー側 pigDataFunction<pigfModuleAgent> ノードに op="box" を載せて compact し、
 * 別プロセス srava_agent が dispatch→cgaBox→保存 した結果が往復するか検証する。
 *   T1 cache MISS+継続 : box(2,3,4) compact → ("delayed".promise) → promise = "box(2,3,4)#8v6f"
 *   T2 cache HIT       : 同一引数 → agent 不起動・キャッシュ値を直接返す
 * agent パスは SRAVA_AGENT、キャッシュ dir は SRAVA_CACHE_DIR(main が設定・清掃)。
 */
#include	"ts2/c++/tinyState.h"
#include	"ts2/c++/tsApplication.h"    /* ctor の parent 型 (ptsApplication 派生・#3427 ③) */
#include	"pig/c++/ptsApplication.h"   /* 基底 (テスト app = registry 所有者・#3427 ③) */
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigwire.h"          /* バイナリ mesh キャッシュの検証(レコード歩き) */
#include	"pig/c++/pigfModuleAgent.h"   /* pigDataFunction<pigfModuleAgent> のインスタンス化 */
#include	"_ts2/c++/cgatsAgentTest_.h"

#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<stdint.h>
#include	<unistd.h>

CLASS_TINYSTATE(cg/c++/cgatsAgentTest,pig/c++/ptsApplication)

/* ★ #3427 ③: 旧 file-global cgatsAgentTest_exitCode は撤去 (可変 static 全廃)。
 * 失敗はメンバ testExitCode に集約し、main が exit_code() で読む。 */
extern void srava_register_cgal_test_fixture(sPtr<pigModuleRegistry> reg);

/* バイナリ mesh キャッシュ(pigwire ストリーム: streamhdr + D_META "MESH" + D_CHUNK*)を歩いて
 * 先頭 D_CHUNK の先頭 8 バイト = [u32 nv][u32 nf](cgaMeshCodec のフレーミング)を読む。
 * CGAL 不要でプランナ側(本テスト)から頂点/面数を検証できる。1=取得, 0=失敗。
 *
 * NB: A_SAVE_BEGIN は writer の body 書込**前**に送られる(read-while-write 順序, step15a)。
 *   本検証は streaming reader ではなく一発読みなので、D_CHUNK が書かれるまで**ポーリング待ち**する
 *   (実下流の cgaUnion は ReaderMesh が正しくポーリングする。これはテストの out-of-band 検証用)。 */
static int read_mesh_counts(const char *path, uint32_t *nv, uint32_t *nf) {
	for ( int attempt = 0 ; attempt < 500 ; ++attempt ) {   /* 最大 ~1s 待つ */
		FILE *f = ::fopen(path, "rb");
		if ( f != 0 ) {
			uint8_t buf[65536];
			size_t n = ::fread(buf, 1, sizeof buf, f);
			::fclose(f);
			size_t off = WIRE_STREAMHDR_SIZE;
			while ( n >= (size_t)WIRE_STREAMHDR_SIZE && off + WIRE_RECHDR_SIZE <= n ) {
				uint16_t type, flags; uint32_t len;
				wire_get_rechdr(buf + off, &type, &flags, &len);
				off += WIRE_RECHDR_SIZE;
				if ( type == W_END ) break;
				if ( type == D_CHUNK ) {
					if ( off + 8 <= n ) {            /* D_CHUNK 本体着信(nv/nf は先頭) */
						const uint8_t *p = buf + off;
						*nv = (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
						*nf = (uint32_t)p[4] | ((uint32_t)p[5]<<8) | ((uint32_t)p[6]<<16) | ((uint32_t)p[7]<<24);
						return 1;
					}
					break;   /* ヘッダのみ着・本体未着 → 再試行 */
				}
				off += len;
			}
		}
		::usleep(2000);   /* 2ms */
	}
	return 0;
}

static sPtr<pigDataFunction<pigfModuleAgent> > mkBox(int w, int h, int d) {
	sPtr<pigDataFunction<pigfModuleAgent> > fn = thNEW(pigDataFunction<pigfModuleAgent>,());
	fn->pushArg(thNEW(pigDataInteger,((INTEGER64)w)));
	fn->pushArg(thNEW(pigDataInteger,((INTEGER64)h)));
	fn->pushArg(thNEW(pigDataInteger,((INTEGER64)d)));
	fn->set_op_name(thNEW(stdString,("box")));
	fn->set_out_cache(1);   /* mesh 出力 = キャッシュハンドル */
	return fn;
}

static sPtr<pigDataFunction<pigfModuleAgent> > mkUnion(sPtr<pigData> a, sPtr<pigData> b) {
	sPtr<pigDataFunction<pigfModuleAgent> > fn = thNEW(pigDataFunction<pigfModuleAgent>,());
	fn->pushArg(a);
	fn->pushArg(b);
	fn->set_op_name(thNEW(stdString,("union")));
	fn->set_out_cache(1);   /* mesh 出力 = キャッシュハンドル */
	return fn;
}

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgatsAgentTest_(
		sPtr<tsApplication> parent);

	sRptr<tsApplication,tinyState>		parent;

	virtual sPtr<pigEnvironment>	get_env();
	int	exit_code();                       /* main が完走後に読む (旧 file-global の置換) */
protected:
	void	CHECK(const char *name, int ok);   /* 1 チェック。ok=0 で fail */
	int			testExitCode;
	sPtr<pigEnvironment>	env;
	sPtr<pigData>		node1, r1;
	sPtr<pigData>		node2, r2;
	sPtr<pigData>		node3, r3;
	sPtr<stdString>		r1path;
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
class stdString;
TS_END_INTERFACE

#endif


cgatsAgentTest_::cgatsAgentTest_(TS_ARGS0)
        : ptsApplication_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    testExitCode = 0;
}

void
cgatsAgentTest_::CHECK(const char *name, int ok)
{
	::printf("[cgatsagent] %-30s : %s\n", name, ok ? "PASS" : "FAIL");
	if (!ok) testExitCode = 1;
}

int
cgatsAgentTest_::exit_code()
{
	return testExitCode;
}

sPtr<pigEnvironment>
cgatsAgentTest_::get_env()
{
	return env;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsApplication_START)   /* 基底 INI_ptsObject_START (ptsApp=自分 + registry 生成) の次 */
{
	/* ★ #3427 ③: cgal.so を link しないテストなので、最小 cgal メタ + VALUE パーサを
	 * **app 所有レジストリ**へ登録 (旧: main() からグローバルへ)。 */
	srava_register_cgal_test_fixture(module_registry);
	env = thNEW(pigEnvironment,(thNULL));
	{
		const char *cd = ::getenv("SRAVA_CACHE_DIR");
		if ( cd == 0 ) cd = "/tmp/srava-cache";
		env->def_var(thNEW(stdString,("CACHE_DIR")), thNEW(pigDataString,(cd)));
	}
	node1 = mkBox(2,3,4);
	node2 = mkBox(2,3,4);   /* 同一引数 → cache HIT */
	/* union(box(2,2,2), box(1,1,3)): 原点・サイズ違いの 2 箱を corefinement 和 → 25 頂点 46 面 */
	node3 = mkUnion(mkBox(2,2,2), mkBox(1,1,3));
	return rDO|ACT_T1A;
}

TS_STATE(ACT_T1A)
{
	r1 = node1->compact();   /* 初回 yield → agent 起動 → front=pair 確定で再開 */
	int isPair = sPtr<pigDataPair>::d_cast(r1).is_notNull();
	CHECK("T1 box miss returns pair", isPair &&
	      pig_is_delayed(r1));   /* car は **型スタンプ** ("cg-mesh3d")。2026-08-19 の型スタンプ化で
	                              * カーネル名 ("cgal") から変わった (旧コメントは 2026-07-29 のもの)。 */
	return rDO|ACT_T1B;
}
TS_STATE(ACT_T1B)
{
	sPtr<pigData> v = r1->cdr()->cdr()->compact();   /* 継続解決(agent 計算完了)まで yield */
	/* mesh 出力 → pigDataCache ハンドル。get_str()=キャッシュパス。中身はバイナリ mesh(8v 12f)。 */
	CHECK("T1 box resolves to cache handle", v->is_cache());
	r1path = v->get_str();
	uint32_t nv = 0, nf = 0;
	int got = read_mesh_counts(r1path->get_str(), &nv, &nf);
	CHECK("T1 box cache content (8v 12f)", got && nv == 8 && nf == 12);
	return rDO|ACT_T2;
}
TS_STATE(ACT_T2)
{
	r2 = node2->compact();   /* HIT: agent 不起動・同一ハッシュのキャッシュハンドルを返す */
	int notPair = ( sPtr<pigDataPair>::d_cast(r2).is_notNull() == 0 );
	CHECK("T2 box cache HIT (handle)", notPair && r2->is_cache());
	CHECK("T2 box hit same path", ::strcmp(r2->get_str()->get_str(), r1path->get_str()) == 0);
	return rDO|ACT_T3A;
}

/* ---- T3: union(box,box) = 本物 corefinement。box の結果は cache ハンドルで届き、
 *         cgatsAgent が CacheStreamReader で別プロセスのキャッシュを読んで CGAL union する ---- */
TS_STATE(ACT_T3A)
{
	r3 = node3->compact();   /* 初回 yield → agent 起動 → front=pair */
	int isPair = sPtr<pigDataPair>::d_cast(r3).is_notNull();
	CHECK("T3 union returns pair", isPair);
	return rDO|ACT_T3B;
}
TS_STATE(ACT_T3B)
{
	sPtr<pigData> v = r3->cdr()->cdr()->compact();   /* box×2 解決 → union 計算完了まで yield */
	/* union も mesh 出力 → cache ハンドル。中身バイナリ: box(2,2,2)∪box(1,1,3)=25 頂点 46 面。
	 * box の出力(バイナリ mesh blob)を別プロセスの cgatsAgent が ReaderMesh で読み、cgaMeshCodec で
	 * 厳密復元 → corefinement → 再びバイナリ blob で保存。厳密往復が崩れなければ 25/46 になる。 */
	CHECK("T3 union resolves to cache handle", v->is_cache());
	uint32_t nv = 0, nf = 0;
	int got = read_mesh_counts(v->get_str()->get_str(), &nv, &nf);
	CHECK("T3 union cache content (25v 46f)", got && nv == 25 && nf == 46);
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_ptsApplication_START;
}

/*
 * pigsig_test — op の幾何型シグネチャ (pigOpEntry.sig) の文法と照合規則の単体テスト。
 *   仕様は docs/sig_grammar_design.md (#3436 P4)。★ §7.1 の「現行 6 モジュールを翻訳して
 *   振り分けが一意のままであることを検算した」表を、散文でなく **機械検査**として置く。
 *
 * ⚠ この単体テストがある理由: 実装時に「外括弧なしの fold 形 ("[a,b](2)->a")」を落とす穴を作り、
 *   全スイート (320 本) を回すまで気づけなかった。文法は文法だけで検査できる。
 */
#include "pig/c++/pigSigGrammar.h"
#include <stdio.h>
#include <string.h>

static int nfail = 0, ntest = 0;

static void
ck(bool cond, const char *what)
{
	++ntest;
	if ( ! cond ) { ++nfail; ::printf("FAIL: %s\n", what); }
}

/* "cg-mesh3d,mf-mesh3d" のような CSV を引数型列にする (各引数の候補型は 1 個)。 */
static std::vector<std::string>
argv_of(const char *csv)
{
	std::vector<std::string> a;
	if ( csv != 0 && csv[0] != '\0' ) split_csv(csv, a);
	return a;
}

static bool
matches(const char *sig, const char *args)
{
	pigSigLine L; parse_sigline(sig, L);
	return sigline_matches(L, argv_of(args));
}

int
main(void)
{
	pigSigLine L;

	/* ── §3.1 固定形 ─────────────────────────────────────────── */
	parse_sigline("(cg-mesh3d,cg-mesh3d)->cg-mesh3d", L);
	ck(!L.bad && L.kind == SK_FIXED && L.fixed.size() == 2 && L.out == "cg-mesh3d", "固定形 parse");
	ck( matches("(cg-mesh3d,cg-mesh3d)->cg-mesh3d", "cg-mesh3d,cg-mesh3d"), "固定形 一致");
	ck(!matches("(cg-mesh3d,cg-mesh3d)->cg-mesh3d", "cg-mesh3d"),           "固定形 個数違い");
	ck(!matches("(cg-mesh3d,cg-mesh3d)->cg-mesh3d", "cg-mesh3d,mf-mesh3d"), "固定形 型違い");

	/* 幾何入力なし (leaf 生成 op)。 */
	parse_sigline("->cg-mesh3d", L);
	ck(!L.bad && L.kind == SK_FIXED && L.fixed.empty() && L.out == "cg-mesh3d", "入力 0 parse");
	ck( matches("->cg-mesh3d", ""),          "入力 0 一致");
	ck(!matches("->cg-mesh3d", "cg-mesh3d"), "入力 0 に引数を渡すと不一致");

	/* ── §3.2 / §7 旧記法 "T..." は "{T}..." の糖衣 ───────────── */
	parse_sigline("(cg-mesh3d...)->ref", L);
	ck(!L.bad && L.kind == SK_REPEAT && L.fixed.empty() && L.set.size() == 1
	   && L.set[0] == "cg-mesh3d" && L.out == "ref", "旧 T... = {T}... の糖衣");
	ck( matches("(cg-mesh3d...)->ref", "cg-mesh3d"),                     "T... 1 個");
	ck( matches("(cg-mesh3d...)->ref", "cg-mesh3d,cg-mesh3d,cg-mesh3d"), "T... 3 個");
	ck(!matches("(cg-mesh3d...)->ref", ""),                              "T... 0 個は不可");
	ck(!matches("(cg-mesh3d...)->ref", "cg-mesh3d,mf-mesh3d"),           "T... は集合外を受けない");

	/* 固定部の前置。 */
	ck( matches("(cg-cross2d,cg-mesh3d...)->ref", "cg-cross2d,cg-mesh3d,cg-mesh3d"), "前置固定部 + 繰り返し");
	ck(!matches("(cg-cross2d,cg-mesh3d...)->ref", "cg-cross2d"),                     "繰り返し部は 1 個以上");

	/* ── §3.4 集合繰り返し形 ─────────────────────────────────── */
	const char *EV = "({cg-mesh3d,mf-mesh3d,gg-mesh3d}...)->ref";
	parse_sigline(EV, L);
	ck(!L.bad && L.kind == SK_REPEAT && L.set.size() == 3 && L.out == "ref", "{…}… parse");
	ck( matches(EV, "cg-mesh3d"),                     "{…}… 単一");
	ck( matches(EV, "cg-mesh3d,mf-mesh3d"),           "{…}… 型混在 (旧 sig_repeat_accepts の行跨ぎ)");
	ck( matches(EV, "mf-mesh3d,gg-mesh3d,cg-mesh3d"), "{…}… 3 型混在");
	ck(!matches(EV, "nf-mesh3d"),                     "{…}… 集合外");
	ck(!matches(EV, ""),                              "{…}… 0 個は不可");
	/* ★ 主型の概念が無い = 全部 foreign でも受ける (fold 形との決定的な違い)。 */
	ck( matches(EV, "mf-mesh3d,mf-mesh3d"), "{…}… は主型を要求しない");

	/* ── §3.3 fold 形 ───────────────────────────────────────── */
	/* ★ 外括弧なしで書ける (§7 の書き換え例の形)。ここが実装で落とした穴。 */
	const char *CG = "[cg-mesh3d,mf-mesh3d,gg-mesh3d](2)->cg-mesh3d";
	parse_sigline(CG, L);
	ck(!L.bad && L.kind == SK_FOLD && L.fixed.empty() && L.set.size() == 3
	   && L.set[0] == "cg-mesh3d" && L.arity == 2 && L.out == "cg-mesh3d", "外括弧なし fold 形 parse");
	parse_sigline("([cg-mesh3d,mf-mesh3d](8))->cg-mesh3d", L);
	ck(!L.bad && L.kind == SK_FOLD && L.arity == 8, "外括弧ありでも同じ");
	parse_sigline("[mf-mesh3d](*)->mf-mesh3d", L);
	ck(!L.bad && L.kind == SK_FOLD && L.arity == -1 && L.set.size() == 1, "n=1 と (*) = 上限なし");

	/* 項数の上限 (正しさの上限)。 */
	ck( matches("[a](2)->a", "a,a"),   "fold N=2 は 2 項");
	ck(!matches("[a](2)->a", "a,a,a"), "fold N=2 は 3 項を受けない");
	ck(!matches("[a](2)->a", "a"),     "fold は 1 項を受けない (2 項以上)");
	ck( matches("[a](*)->a", "a,a,a,a,a"), "fold (*) は 5 項も受ける");
	ck( matches("[a](4)->a", "a,a,a,a"),   "fold N=4 ちょうど");
	ck(!matches("[a](4)->a", "a,a,a,a,a"), "fold N=4 は 5 項を受けない");

	/* ★★ §4.3 規則 3: 主型 (set[0]) が最低 1 個 = disjoint 原則を記法に埋めたもの。 */
	ck( matches(CG, "cg-mesh3d,cg-mesh3d"), "主型のみ");
	ck( matches(CG, "cg-mesh3d,mf-mesh3d"), "主型 + foreign");
	ck( matches(CG, "mf-mesh3d,cg-mesh3d"), "順序は問わない");
	ck(!matches(CG, "mf-mesh3d,mf-mesh3d"), "★ 主型が無いので cgal は (mf,mf) を取らない");
	ck(!matches(CG, "gg-mesh3d,mf-mesh3d"), "★ 主型が無いので cgal は (gg,mf) を取らない");
	ck(!matches(CG, "cg-mesh3d,nf-mesh3d"), "集合外の型を含む");

	/* ── ★★ §7.1 検算表: 現行 6 モジュールの翻訳で振り分けが一意のままか ───── */
	struct Row { const char *args; const char *winner; };
	static const char *MOD[]  = { "cgal", "geogram", "nef", "manifold" };
	static const char *MSIG[] = {
		"[cg-mesh3d,mf-mesh3d,gg-mesh3d](*)->cg-mesh3d",
		"[gg-mesh3d,mf-mesh3d](*)->gg-mesh3d",
		"[nf-mesh3d,cg-mesh3d,mf-mesh3d,gg-mesh3d](*)->nf-mesh3d",
		"[mf-mesh3d](*)->mf-mesh3d",
	};
	static const Row ROWS[] = {
		{ "cg-mesh3d,mf-mesh3d", "cgal"     },
		{ "gg-mesh3d,mf-mesh3d", "geogram"  },   /* ★ cgal は両型を含むが主型 cg が無いので取らない */
		{ "cg-mesh3d,gg-mesh3d", "cgal"     },
		{ "nf-mesh3d,cg-mesh3d", "nef"      },   /* cgal は nf を集合外にしている = 約束② */
		{ "mf-mesh3d,mf-mesh3d", "manifold" },   /* ★ 規則 3 の効き目 */
		{ "cg-mesh3d,cg-mesh3d", "cgal"     },
		{ "gg-mesh3d,gg-mesh3d", "geogram"  },
		{ "nf-mesh3d,nf-mesh3d", "nef"      },
	};
	for ( size_t r = 0 ; r < sizeof ROWS / sizeof ROWS[0] ; ++r ) {
		std::string hit;
		int nhit = 0;
		for ( size_t m = 0 ; m < 4 ; ++m )
			if ( matches(MSIG[m], ROWS[r].args) ) { hit = MOD[m]; ++nhit; }
		char what[256];
		::snprintf(what, sizeof what, "§7.1 (%s) → %s (一意)", ROWS[r].args, ROWS[r].winner);
		++ntest;
		if ( nhit != 1 || hit != ROWS[r].winner ) {
			++nfail;
			::printf("FAIL: %s — 実際: %d 件マッチ (%s)\n", what, nhit, nhit ? hit.c_str() : "なし");
		}
	}

	/* ── 記法エラー (bad = 照合対象外) ───────────────────────── */
	parse_sigline("[a,b](1)->a", L);   ck(L.bad, "fold N<2 は記法エラー");
	parse_sigline("[a,b]->a",    L);   ck(L.bad, "fold の (N) 欠落");
	parse_sigline("{a,b}->ref",  L);   ck(L.bad, "繰り返し形の ... 欠落");
	parse_sigline("([a,b](2),c)->a", L); ck(L.bad, "可変部は末尾のみ");
	parse_sigline("[a,b](2)->a", L);   ck(!L.bad, "正しい fold 形は bad でない");

	/* ── 個数だけの判定 (エラー文の候補列挙用) ─────────────────── */
	parse_sigline("[a,b](4)->a", L);
	ck(!sigline_arity_ok(L, 1) && sigline_arity_ok(L, 2) && sigline_arity_ok(L, 4)
	   && !sigline_arity_ok(L, 5), "sigline_arity_ok (fold)");
	parse_sigline("({a,b}...)->ref", L);
	ck(!sigline_arity_ok(L, 0) && sigline_arity_ok(L, 1) && sigline_arity_ok(L, 99),
	   "sigline_arity_ok (繰り返し)");

	::printf("pigsig: %d/%d ok\n", ntest - nfail, ntest);
	if ( nfail ) { ::printf("PIGSIG-FAIL\n"); return 1; }
	::printf("PIGSIG-OK\n");
	return 0;
}

#ifndef ___pigSigGrammar_H___
#define ___pigSigGrammar_H___
/*
 * pigSigGrammar — op の**幾何型シグネチャ** (pigOpEntry.sig) の文法と照合規則。
 *   形式化と設計の根拠は docs/sig_grammar_design.md (#3436 P4)。
 *
 * ★ ヘッダに切り出してあるのは **単体テストから叩けるようにするため** (test/pigsig_test.cpp)。
 *   本体の唯一の利用者は pigfModuleAgent::decide_executor (routing の型ディスパッチ)。
 *   ⚠ 実装時に「外括弧なしの fold 形」を落とす穴を作り、全スイートを回すまで気づけなかった。
 *     文法は表で機械検査できる形にしておく。
 *
 * 依存は <string> / <vector> / <stdlib.h> だけ (srava の型に依存しない)。
 */
#include <string>
#include <vector>
#include <stdlib.h>   /* atoi */

/* CSV ("a,b,c") に tok が含まれるか。 */
inline bool
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

/* CSV ("a,b,c") を型名ベクタへ分解。 */
inline void
split_csv(const std::string& s, std::vector<std::string>& v)
{
	size_t p = 0;
	while ( p <= s.size() ) {
		size_t c = s.find(',', p);
		size_t e = ( c == std::string::npos ) ? s.size() : c;
		v.push_back(s.substr(p, e - p));
		if ( c == std::string::npos ) break;
		p = c + 1;
	}
}

/* ─────────────────────────────────────────────────────────────────────
 * ★ #3436 P4: sig 1 行の文法 (docs/sig_grammar_design.md §3)
 *
 *   固定形     "(a,b)->c"           SK_FIXED   位置と個数が確定
 *   繰り返し形 "(f…,{a,b}...)->r"   SK_REPEAT  末尾スロットが 1 個以上。**分解も昇格もしない**
 *   fold 形    "(f…,[a,b](N))->a"   SK_FOLD    2〜N 項。set[0] = **主型**。木に分解してよい
 *
 * ★ 旧記法 "T..." は "{T}..." の糖衣 (§7) → 既存の sig は 1 文字も書き換えずに通る。
 * ⚠ fold 形と繰り返し形は **統合できない** (§3.4)。構文が似ているだけで、分解の可否・主型の有無・
 *   昇格を宣言するかどうか・出力型が集合の中かどうか、が全部違う。
 * ───────────────────────────────────────────────────────────────────── */
enum pigSigKind { SK_FIXED = 0, SK_REPEAT = 1, SK_FOLD = 2 };

struct pigSigLine {
	int                      kind;
	std::vector<std::string> fixed;   /* 前置の固定部 (位置で照合) */
	std::vector<std::string> set;     /* bracket/brace の型集合。★ SK_FOLD は set[0] が主型 */
	int                      arity;   /* SK_FOLD の N。-1 = '*' (上限なし) */
	std::string              out;     /* 出力型 ("value" / "ref" もここに来る) */
	bool                     bad;     /* 記法エラー = 照合対象外 (ロード時検査が名指しする) */
};

/* 1 シグネチャを parse。先頭に '(' が無い ("->c") = 幾何入力 0 (leaf 生成 op)。 */
inline void
parse_sigline(const std::string& s, pigSigLine& L)
{
	L.kind = SK_FIXED; L.fixed.clear(); L.set.clear(); L.arity = -1; L.out.clear(); L.bad = false;
	size_t arrow = s.find("->");
	std::string lhs = ( arrow == std::string::npos ) ? s : s.substr(0, arrow);
	L.out = ( arrow == std::string::npos ) ? std::string() : s.substr(arrow + 2);
	/* 入力リストの外側 '(' … ')' を剥がす。★ 可変部が単独のときは外括弧を書かない
	 *   ("[a,b](8)->a" / "{a,b}...->ref")。固定部を前置するときだけ "(x,[a,b](8))->a" と書く。 */
	std::string inner;
	if ( ! lhs.empty() && lhs[0] == '(' ) {
		int d = 0; size_t close = std::string::npos;
		for ( size_t i = 0 ; i < lhs.size() ; ++i ) {
			if ( lhs[i] == '(' || lhs[i] == '[' || lhs[i] == '{' ) ++d;
			else if ( lhs[i] == ')' || lhs[i] == ']' || lhs[i] == '}' ) {
				if ( --d == 0 ) { close = i; break; }
			}
		}
		if ( close == std::string::npos ) { L.bad = true; return; }
		if ( close <= 1 ) return;                       /* "()" = 幾何入力 0 */
		inner = lhs.substr(1, close - 1);
	} else if ( ! lhs.empty() && ( lhs[0] == '[' || lhs[0] == '{' ) ) {
		inner = lhs;                                    /* 外括弧なしの可変部単独形 */
	} else
		return;                                         /* 入力 0 (leaf 生成 op) */

	/* 深さ 0 のカンマで分割 ('[' '{' '(' の中のカンマは区切りではない)。 */
	std::vector<std::string> toks;
	int depth = 0; size_t st = 0;
	for ( size_t i = 0 ; i <= inner.size() ; ++i ) {
		char c = ( i < inner.size() ) ? inner[i] : ',';
		if ( c == '[' || c == '{' || c == '(' ) ++depth;
		else if ( c == ']' || c == '}' || c == ')' ) --depth;
		else if ( c == ',' && depth == 0 ) { toks.push_back(inner.substr(st, i - st)); st = i + 1; }
	}
	if ( depth != 0 ) { L.bad = true; return; }

	for ( size_t t = 0 ; t < toks.size() ; ++t ) {
		const std::string& tk = toks[t];
		bool last = ( t + 1 == toks.size() );
		if ( tk.empty() ) { L.bad = true; return; }
		if ( tk[0] == '[' || tk[0] == '{' ) {
			if ( ! last ) { L.bad = true; return; }        /* 可変部は末尾のみ */
			char close = ( tk[0] == '[' ) ? ']' : '}';
			size_t cp = tk.find(close);
			if ( cp == std::string::npos || cp < 2 ) { L.bad = true; return; }
			split_csv(tk.substr(1, cp - 1), L.set);
			std::string tail = tk.substr(cp + 1);
			if ( tk[0] == '{' ) {
				if ( tail != "..." ) { L.bad = true; return; }
				L.kind = SK_REPEAT;
			} else {
				/* fold 形の "(N)": N = 2 以上の整数、または '*' = 上限なし。 */
				if ( tail.size() < 3 || tail[0] != '(' || tail[tail.size() - 1] != ')' ) { L.bad = true; return; }
				std::string n = tail.substr(1, tail.size() - 2);
				if ( n == "*" )
					L.arity = -1;
				else {
					L.arity = ::atoi(n.c_str());
					if ( L.arity < 2 ) { L.bad = true; return; }
				}
				L.kind = SK_FOLD;
			}
			for ( size_t i = 0 ; i < L.set.size() ; ++i )
				if ( L.set[i].empty() ) { L.bad = true; return; }
		} else if ( tk.size() > 3 && tk.compare(tk.size() - 3, 3, "...") == 0 ) {
			if ( ! last ) { L.bad = true; return; }
			L.set.push_back(tk.substr(0, tk.size() - 3));   /* ★ "T..." = "{T}..." の糖衣 */
			L.kind = SK_REPEAT;
		} else {
			L.fixed.push_back(tk);
		}
	}
}

/* 型集合 set に、引数 1 個の候補型 CSV a のどれかが入っているか。 */
inline bool
set_has_csv(const std::vector<std::string>& set, const std::string& a)
{
	for ( size_t i = 0 ; i < set.size() ; ++i )
		if ( csv_has(a, set[i]) ) return true;
	return false;
}

/* ★ §4 マッチ規則: 引数の幾何型列 A[0..m-1] (各要素は候補型 CSV) と 1 行を照合。 */
inline bool
sigline_matches(const pigSigLine& L, const std::vector<std::string>& A)
{
	if ( L.bad ) return false;
	size_t f = L.fixed.size(), m = A.size();
	for ( size_t i = 0 ; i < f && i < m ; ++i )
		if ( ! csv_has(A[i], L.fixed[i]) ) return false;
	if ( L.kind == SK_FIXED )
		return m == f;                                  /* §4.1 */
	if ( m <= f ) return false;                         /* 可変部は 1 個以上 */
	for ( size_t i = f ; i < m ; ++i )
		if ( ! set_has_csv(L.set, A[i]) ) return false;
	if ( L.kind == SK_REPEAT )
		return true;                                    /* §4.2 */
	/* §4.3 fold 形: 2 ≤ 可変部 ≤ N かつ **主型 (set[0]) が最低 1 個**。
	 * ★ 主型の規則が disjoint 原則 (all-foreign を書かない) を記法に埋めたもの:
	 *   cgal [cg,mf,gg] は (mf,mf) を **主型 cg が無い** ので取らず、manifold へ落ちる。 */
	size_t r = m - f;
	if ( r < 2 ) return false;
	if ( L.arity >= 0 && (int)r > L.arity ) return false;
	for ( size_t i = f ; i < m ; ++i )
		if ( csv_has(A[i], L.set[0]) ) return true;
	return false;
}

/* 幾何入力 nin 個をこの行が受けうるか (個数だけの判定・エラー文の候補列挙で使う)。 */
inline bool
sigline_arity_ok(const pigSigLine& L, int nin)
{
	if ( L.bad ) return false;
	int f = (int)L.fixed.size();
	if ( L.kind == SK_FIXED ) return nin == f;
	if ( nin <= f ) return false;
	if ( L.kind == SK_FOLD ) {
		int r = nin - f;
		if ( r < 2 ) return false;
		if ( L.arity >= 0 && r > L.arity ) return false;
	}
	return true;
}

#endif

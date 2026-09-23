/*
 * srava_module_probe — カーネル .so の dlopen 検証プローブ (.so 化 Phase 3b・§8 の並走確認)。
 *
 *   srava_module_probe <module.so>
 *
 * .so を dlopen し (pigModuleLoader)、記述子を registry へ配線し、内容を 1 行で出力する。
 * RTLD_NOW で読むので、.so の未解決シンボル (pig/pts/tinyState) が **この host から解決できる**
 * ことも同時に検証する (= Phase 3c の単一 srava_agent + in-proc thread が成立する前提の証明)。
 * この host は pts* 実行体基盤 (AGENT_HOST_SRC) + libpig をリンクし、-rdynamic で export する。
 *
 * 実行体本体は起こさない (Mediator/pipe 不要)。記述子の読取と全シンボル解決までを検証範囲とする。
 */
#include "pig/c++/pigModule.h"
#include "pig/c++/pigModuleRegistry.h"   /* ★ #3427 ③: probe 専用ローカルレジストリ */
#include "pig/c++/pigData.h"             /* ★ #3554: マッチ関数の引数型 (自己テストの見本) */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
	if ( argc < 2 ) {
		std::fprintf(stderr, "usage: %s <module.so> | --selftest\n", argv[0]);
		return 2;
	}

	/* ★ #3439 ⑦: 記述子の自己矛盾検査 (pig_descriptor_violation) の自己テスト。
	 *   import/export op を持つのに対応する exts を申告しない記述子は**違反**で、load_file が
	 *   ABI 不一致と同じくロードを拒否する。壊れた .so をビルドせずに規則だけを検証する。 */
	if ( std::string(argv[1]) == "--selftest" ) {
		/* ★ #3554: 「マッチ関数を持つ行」の見本。中身は要らない (ポインタが 0 でないことだけが効く)。 */
		struct S {
			static int match(const srava_module_descriptor*, const pigOpEntry*, int, sPtr<pigData>)
			{ return 1; }
		};
		static const pigOpEntry ops_exp[] = { { "export", 0, 0, (pigArgKind)0, 0, 0, 0 } };
		/* ★ #3554 最後の段 3/5: import 行は **sig の出力型**が拡張子表 (import_exts) の型と
		 *   突き合わされるので、雛形にも sig が要る (旧 fixture は sig=0 だった)。 */
		static const pigOpEntry ops_imp[] = { { "import", 0, 0, (pigArgKind)0, 0, 0, "->x" } };
		static const pigOpEntry ops_box[] = { { "box",    0, 0, (pigArgKind)0, 0, 0, 0 } };
		/* ★★ #3554 段1 (2026-09-19): **行の並び**の検査。無条件の行 (`#` 無し) を変種より
		 *   前に置くと *変種が永久に選ばれない* — しかも動くので気づけない ⇒ ロード時に弾く。
		 *   ⚠ この検査は「壊れた .so をビルドせずに規則だけを検証する」ここでしか試せない
		 *     (demo.so に不正な並びを入れたら demo 自体がロードできなくなる)。 */
		static const pigOpEntry ops_ord_bad[] = {           /* 基底が先 = 覆い隠す */
			{ "t",    0, 0, (pigArgKind)0, 0, 0, 0 },
			{ "t#xy", 0, 0, (pigArgKind)0, 0, 0, 0 },
		};
		static const pigOpEntry ops_ord_ok[] = {            /* 変種が先 = 正しい */
			{ "t#xy", 0, 0, (pigArgKind)0, 0, 0, 0 },
			{ "t",    0, 0, (pigArgKind)0, 0, 0, 0 },
		};
		static const pigOpEntry ops_ord_var[] = {           /* 基底行が無い (規則③) */
			{ "t#xy", 0, 0, (pigArgKind)0, 0, 0, 0 },
			{ "t#yz", 0, 0, (pigArgKind)0, 0, 0, 0 },
		};
		static const pigOpEntry ops_ord_other[] = {         /* 別の基底名なので無関係 */
			{ "u",    0, 0, (pigArgKind)0, 0, 0, 0 },
			{ "t#xy", 0, 0, (pigArgKind)0, 0, 0, 0 },
		};
		/* ★★ #3554 最後の段 2/5: **基底名の行でもマッチ関数を持てば無条件ではない**
		 *   (cast の出力型が 1 つしかないモジュールがこの形)。前に在っても変種を覆い隠さない。
		 *   ⚠ この 1 件が無いと、上の並び検査を「基底名なら常に違反」と書き過ぎても緑のままになる。 */
		static const pigOpEntry ops_ord_cond[] = {
			{ "t",    0, 0, (pigArgKind)0, 0, 0, 0, 0, 0, 0, &S::match },
			{ "t#xy", 0, 0, (pigArgKind)0, 0, 0, 0 },
		};
		/* ★★ #3554 最後の段 2/5: **cast の行は 1 行 1 出力型**。2 つ書くと、行が成立するか
		 *   (どれかの sigline が目標型を産むか) と実際に名乗る型 (入力型で先に当たった sigline)
		 *   が食い違い、要求と違う型が黙って返る ⇒ ロード時に弾く。 */
		static const pigOpEntry ops_cast_bad[] = {
			{ "cast", 0, 0, (pigArgKind)0, 0, 0, "(a)->x;(b)->y" },
		};
		static const pigOpEntry ops_cast_ok[] = {          /* 目標型ごとに分けてある */
			{ "cast#x", 0, 0, (pigArgKind)0, 0, 0, "(a)->x;(b)->x" },
			{ "cast#y", 0, 0, (pigArgKind)0, 0, 0, "(a)->y" },
		};
		static const pigOpEntry ops_cast_one[] = {         /* 出力型が 1 つなら分けなくてよい */
			{ "cast", 0, 0, (pigArgKind)0, 0, 0, "(a)->x;(b)->x" },
		};
		/* ★★ #3554 最後の段 3/5: import は **拡張子 → 型** (import_exts) と
		 *   **行 → 出力型** (sig) を突き合わせて行を選ぶ。両者がずれるとその拡張子は
		 *   *永久に読めない*のに、出る文言は「読めるモジュールが無い」= 別の原因を指す。
		 *   ⇒ 記述子だけで突き合わせられるのでロード時に弾く。 */
		static const pigOpEntry ops_imp_split[] = {        /* 出力型ごとに分けた正しい形 */
			{ "import#x", 0, 0, (pigArgKind)0, 0, 0, "->x" },
			{ "import#y", 0, 0, (pigArgKind)0, 0, 0, "->y" },
		};
		static const pigOpEntry ops_imp_two[] = {          /* 1 行に出力型が 2 つ */
			{ "import", 0, 0, (pigArgKind)0, 0, 0, "->x;->y" },
		};
		struct Case { const char *what; const pigOpEntry *ops; int nops;
		              const char *imp; const char *exp; bool bad; };
		static const Case cases[] = {
			{ "export op + exts 無し",  ops_exp, 1, 0,     0,      true  },
			{ "export op + exts 空",    ops_exp, 1, 0,     "",     true  },
			{ "export op + exts 申告",  ops_exp, 1, 0,     "stl",  false },
			{ "import op + exts 無し",  ops_imp, 1, 0,     0,      true  },
			/* ★ #3554 最後の段 3/5: import_exts は **型付き**でなければならなくなった
			 *   (旧 "stl" のままだと「型で照合する」マッチ関数が誰も選べない) ⇒ "stl:x" に直した。 */
			{ "import op + exts 申告",  ops_imp, 1, "stl:x", 0,    false },
			{ "無関係な op のみ",       ops_box, 1, 0,     0,      false },
			/* ★ #3554 段1: 行の並び */
			{ "基底が変種より前",       ops_ord_bad,   2, 0, 0, true  },
			{ "変種が先",               ops_ord_ok,    2, 0, 0, false },
			{ "基底行が無い",           ops_ord_var,   2, 0, 0, false },
			{ "別の基底名は無関係",     ops_ord_other, 2, 0, 0, false },
			{ "基底行でもマッチ付きなら覆わない", ops_ord_cond, 2, 0, 0, false },
			/* ★ #3554 最後の段 2/5: cast の 1 行 1 出力型 */
			{ "cast の行に出力型が 2 つ", ops_cast_bad, 1, 0, 0, true  },
			{ "cast を目標型ごとに分けた", ops_cast_ok,  2, 0, 0, false },
			{ "cast の出力型が 1 つ",     ops_cast_one, 1, 0, 0, false },
			/* ★ #3554 最後の段 3/5: import_exts の型と sig の出力型の突き合わせ */
			{ "import の行に出力型が 2 つ",   ops_imp_two,   1, "stl:x",       0, true  },
			{ "import を出力型ごとに分けた",  ops_imp_split, 2, "stl:x,svg:y", 0, false },
			{ "import_exts が無型",           ops_imp,       1, "stl",         0, true  },
			{ "import_exts の型を誰も産まない", ops_imp,     1, "stl:z",       0, true  },
			/* ⚠ 変種行しか無くても「import op を持つ」と数えること (基底名で数える) */
			{ "変種行だけで exts 未申告",     ops_imp_split, 2, 0,             0, true  },
		};
		int ng = 0;
		for ( size_t i = 0 ; i < sizeof cases / sizeof cases[0] ; ++i ) {
			/* ★ #3466: 位置指定初期化子をやめた。旧 { …, 0, 0, 0, 0, 0 } は provides 以降の
			 *   5 個を埋めていたが、ABI v17 で hash_salt が消えて **初期化子が多すぎる**という
			 *   コンパイルエラーになった (= 記述子のレイアウト変更を型検査が捕えた)。
			 *   名前で書けば以後フィールドが増減しても静かにずれない。 */
			srava_module_descriptor d = {
				.abi_version  = SRAVA_MODULE_ABI,
				.name         = "selftest",
				.priority     = 0,
				.make_agent   = 0,
				.exec_caps    = 0u,
				.exec_default = 0,
				.ops          = cases[i].ops,
				.n_ops        = cases[i].nops,
				.import_exts  = cases[i].imp,
				.export_exts  = cases[i].exp,
				.provides     = 0,
				.cache_version = 1,   /* ★ v18 (#3466): 結果の版 (手で上げる) */
				.arity        = 0,
				.initialize   = 0,
				.configure    = 0,
			};
			bool got = ! pig_descriptor_violation(&d).empty();
			if ( got != cases[i].bad ) {
				std::fprintf(stderr, "SELFTEST-FAIL: %s (violation=%d, 期待 %d)\n",
				             cases[i].what, (int)got, (int)cases[i].bad);
				++ng;
			}
		}
		if ( ng != 0 ) return 1;
		std::printf("DESCRIPTOR-VIOLATION-SELFTEST-OK\n");
		return 0;
	}

	/* ★ #3427 ③: レジストリは app 所有になった。probe は app を起こさず、ローカルレジストリへ
	 * 配線して記述子と型登録を検証する (実行系と同じ register_descriptor 経路)。 */
	sPtr<pigModuleRegistry> reg = thNEW(pigModuleRegistry,());
	std::string err;
	const srava_module_descriptor *d = reg->load_file(argv[1], &err);
	if ( d == 0 ) {
		std::fprintf(stderr, "srava_module_probe: load failed: %s\n", err.c_str());
		return 1;
	}

	std::printf("MODULE_OK name=%s abi=%d priority=%d exec_caps=%u exec_default=%d "
	            "n_ops=%d make_agent=%s import=%s export=%s\n",
	            d->name ? d->name : "(null)", d->abi_version, d->priority,
	            d->exec_caps, d->exec_default, d->n_ops,
	            d->make_agent ? "yes" : "no",
	            d->import_exts ? d->import_exts : "",
	            d->export_exts ? d->export_exts : "");

	/* ★ 2026-08-28 (ひさ指摘): codec 表は **型名の登録簿**になったので、probe は registry を
	 *   経由せず **記述子の codecs をそのまま網羅表示**する。
	 *   ★ 旧実装は registry->types_of_module(名前) を呼んでいたが、名前でモジュールを引き直し
	 *     is_enabled まで見ていた。probe は記述子を手に持っており、module(so,"off") のような実行時の
	 *     有効/無効という概念も無いので、その 2 段は意味を持たなかった。
	 *   CODEC 行 = 表の 1 行そのまま (省略も絞り込みもしない)。TYPES 行 = 全行の型名を重複除去した
	 *   もので、既存の回帰 (srava_type_*) が読むのはこちら。 */
	std::string tlist;
	if (d->provides != 0) {
		for (const pigModuleType *c = d->provides ; c->wire != 0 ; ++c) {
			std::printf("CODEC %s types=%s tags=%s\n",
			            c->wire->name ? c->wire->name : "(unnamed)",
			            c->types ? c->types : "", c->tags ? c->tags : "");
			for (const char *q = ( c->types ? c->types : "" ) ; *q != '\0' ; ) {
				const char *comma = std::strchr(q, ',');
				std::string t(q, comma ? (size_t)(comma - q) : std::strlen(q));
				if (!t.empty() && !pig_type_is_nongeometric(t.c_str())
				    && tlist.find(t) == std::string::npos) {
					if (!tlist.empty()) tlist += ",";
					tlist += t;
				}
				if (comma == 0) break;
				q = comma + 1;
			}
		}
	}
	std::printf("TYPES %s\n", tlist.c_str());

	/* ★ 本体クラス階層 (wires) の能力。表からは reader/writer/match が消えたので、
	 *   「このモジュールは書けるのか / 読めるのか」はここを見る。 */
	if (d->provides != 0) {
		for (const pigModuleType *c = d->provides ; c->wire != 0 ; ++c)
			std::printf("WIRE %s create=%s reader=%s writer=%s match=%s\n",
			            c->wire->name ? c->wire->name : "(unnamed)",
			            c->wire->create   ? "yes" : "no", c->wire->mkReader ? "yes" : "no",
			            c->wire->mkWriter ? "yes" : "no", c->wire->match    ? "yes" : "no");
	}

	/* ★ 非幾何型 (計算の行き先にならない型) は libpig が 1 本のリストで持つ。agent の
	 *   「消費できる型リスト」はこれを除いて作る。ここはその唯一のリストの回帰
	 *   (どの .so でも同じ = 幾何型が 1 つも混ざらないこと)。 */
	std::string oplist;
	for (int i = 0 ; pig_nongeometric_types[i] != 0 ; ++i) {
		if (!oplist.empty()) oplist += ",";
		oplist += pig_nongeometric_types[i];
	}
	std::printf("OPLESS %s\n", oplist.c_str());
	return 0;
}

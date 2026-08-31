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
		static const pigOpEntry ops_exp[] = { { "export", 0, 0, (pigArgKind)0, 0, 0, 0 } };
		static const pigOpEntry ops_imp[] = { { "import", 0, 0, (pigArgKind)0, 0, 0, 0 } };
		static const pigOpEntry ops_box[] = { { "box",    0, 0, (pigArgKind)0, 0, 0, 0 } };
		struct Case { const char *what; const pigOpEntry *ops; const char *imp; const char *exp; bool bad; };
		static const Case cases[] = {
			{ "export op + exts 無し",  ops_exp, 0,     0,      true  },
			{ "export op + exts 空",    ops_exp, 0,     "",     true  },
			{ "export op + exts 申告",  ops_exp, 0,     "stl",  false },
			{ "import op + exts 無し",  ops_imp, 0,     0,      true  },
			{ "import op + exts 申告",  ops_imp, "stl", 0,      false },
			{ "無関係な op のみ",       ops_box, 0,     0,      false },
		};
		int ng = 0;
		for ( size_t i = 0 ; i < sizeof cases / sizeof cases[0] ; ++i ) {
			srava_module_descriptor d = { SRAVA_MODULE_ABI, "selftest", 0, 0, 0u, 0,
			                              cases[i].ops, 1, cases[i].imp, cases[i].exp,
			                              0, 0, 0, 0, 0 };
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

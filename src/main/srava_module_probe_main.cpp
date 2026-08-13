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
#include <string>

int main(int argc, char **argv)
{
	if ( argc < 2 ) {
		std::fprintf(stderr, "usage: %s <module.so>\n", argv[0]);
		return 2;
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

	/* ★ #3427 ①③: 記述子 (types×type_tags) から register_descriptor が型登録したか検証。
	 *   登録先はこの probe のローカルレジストリ (types メンバ)。登録順で 1 行に列挙。 */
	std::string tlist;
	for (int i = 0; i < reg->types.type_count(); ++i) {
		if (i) tlist += ",";
		const char *n = reg->types.name_of_type_id(i);
		tlist += (n ? n : "(null)");
	}
	std::printf("TYPES %s\n", tlist.c_str());
	return 0;
}

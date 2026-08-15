/*
 * pigModuleLoader — .so 動的ロードの実装 (.so 化 Phase 3・docs §3.1)。
 * ★ #3427 ③: 旧 namespace pigModuleLoader を廃し、**pigModuleRegistry のメソッド**として実装する
 *   (load_file / load_search_path)。ロード記録 (旧 g_log / g_dirs) もレジストリのメンバ
 *   (log_v / dirs_v) へ移り、この TU の可変 static は消えた。
 */
#include "pig/c++/pigModuleRegistry.h"
#include "pig/c++/pigModule.h"

#include "pig/c++/osglue.h"   /* dlopen/dlsym の OS 差 + モジュール拡張子 (.so/.dll) */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <utility>
#include <dirent.h>
#include <unistd.h>

#ifndef SRAVA_MODULE_SYSDIR
#define SRAVA_MODULE_SYSDIR "/usr/local/lib/srava/modules"   /* install 既定 (CMake で上書き) */
#endif

void
pigModuleRegistry::record(const char *path, const srava_module_descriptor *d,
                          const std::string &err, bool not_a_module)
{
	pigModuleLoadEvent ev;
	ev.path = path ? path : "";
	ev.ok   = ( d != 0 );
	ev.not_a_module = not_a_module;
	ev.shadowed = false;
	if ( d != 0 && d->name != 0 ) ev.name = d->name;
	else                          ev.err  = err;
	log_v.push_back(ev);
}

/* 同名の勝者が後の探索路に在るので **dlopen しなかった** 候補の記録 (診断用)。 */
void
pigModuleRegistry::record_shadowed(const std::string &path, const std::string &winner)
{
	pigModuleLoadEvent ev;
	ev.path = path;
	ev.ok = false;
	ev.not_a_module = false;
	ev.shadowed = true;
	ev.err = winner;            /* 勝者のパスを入れておく (診断表示で使う) */
	log_v.push_back(ev);
}

/* 実体。記録は下の load_file ラッパが一本で行う (return 箇所ごとに書かない)。 */
const srava_module_descriptor*
pigModuleRegistry::load_file_impl(const char *path, std::string *err, bool lazy, bool *not_a_module)
{
	if ( path == 0 || path[0] == '\0' ) {
		if ( err ) *err = "empty module path";
		return 0;
	}

	/* RTLD_GLOBAL: .so のシンボルを以降ロードする他モジュール/host から見えるように。
	 * RTLD_NOW/LAZY: 未解決シンボル (pig/pts/tinyState) の解決タイミング (lazy=関数は呼出時)。
	 * 失敗理由は呼び手バッファ ebuf へ (#3427 ③: osglue の static バッファ廃止に伴う API)。 */
	char ebuf[512];
	ebuf[0] = '\0';
	void *h = osglue_dlopen(path, lazy ? 1 : 0, ebuf, sizeof ebuf);
	if ( h == 0 ) {
		if ( err ) *err = ( ebuf[0] != '\0' ) ? ebuf : "dlopen failed";
		return 0;
	}

	ebuf[0] = '\0';
	srava_module_fn fn = (srava_module_fn)osglue_dlsym(h, SRAVA_MODULE_SYM, ebuf, sizeof ebuf);
	if ( fn == 0 ) {
		/* SRAVA_MODULE_SYM が無い = そもそもモジュールでない .so (libpig.so 等)。エラーではない。 */
		if ( not_a_module ) *not_a_module = true;
		if ( err ) *err = ( ebuf[0] != '\0' ) ? ebuf : "symbol '" SRAVA_MODULE_SYM "' not found";
		osglue_dlclose(h);
		return 0;
	}

	const srava_module_descriptor *d = fn();
	if ( d == 0 ) {
		if ( err ) *err = "srava_module() returned null";
		osglue_dlclose(h);
		return 0;
	}

	if ( d->abi_version != SRAVA_MODULE_ABI ) {
		if ( err ) {
			char buf[160];
			::snprintf(buf, sizeof buf,
			           "ABI mismatch: module '%s' abi=%d, host expects %d",
			           d->name ? d->name : "(null)", d->abi_version, SRAVA_MODULE_ABI);
			*err = buf;
		}
		osglue_dlclose(h);   /* 記述子を捨てる (docs §2.1: 不一致はエラーで拒否) */
		return 0;
	}

	/* 記述子から agent/型/codec/salt を **このレジストリへ** 一括登録 (#3427 ①: 登録経路 1 本)。
	 * .so は「記述子を返すだけ」の受け身 — 静的自己登録は全廃済みなので、dlopen が
	 * プロセス全体の状態を書き換えることはない (per-registry = per-app・リエントラント)。 */
	loadingPath_ = ( path != 0 ) ? path : "";   /* この記述子の出所を控える (#3425 ①) */
	register_descriptor(d);
	loadingPath_.clear();

	/* 成功: ハンドルは意図的にリークさせる (登録した実行体・記述子が生き続ける必要がある)。 */
	return d;
}

/* 公開 API = 実体 + ロード記録。診断 (`srava --modules`) が「試したパスと理由」を全件持てるように、
 * 成否にかかわらずここで 1 件記録する。 */
const srava_module_descriptor*
pigModuleRegistry::load_file(const char *path, std::string *err, bool lazy)
{
	std::string e;
	bool nam = false;
	const srava_module_descriptor *d = load_file_impl(path, &e, lazy, &nam);
	if ( d == 0 && err != 0 )
		*err = e;
	record(path, d, e, nam);
	return d;
}

/* dir 内の module 拡張子ファイルを **列挙するだけ** (dlopen しない)。走査した dir は診断へ記録する
 * (存在しなくても記録する。「探したが無かった」が見えることが重要)。
 * ★ 2026-08-13: 以前はここで即 dlopen していたが、同名モジュールが複数の探索路に在ると
 *   **両方が dlopen され、静的自己登録が混ざる** 問題があった (下の load_search_path の注記)。 */
void
pigModuleRegistry::scan_dir(const char *dir, const char *origin,
                            std::vector<std::pair<std::string,std::string> > *out)
{
	DIR *d = ::opendir(dir);
	{
		pigModuleSearchDir sd;
		sd.dir = dir ? dir : "";
		sd.origin = origin;
		sd.exists = ( d != 0 );
		dirs_v.push_back(sd);
	}
	if ( d == 0 )
		return;
	struct dirent *e;
	while ( ( e = ::readdir(d) ) != 0 ) {
		const char *nm = e->d_name;
		size_t L = ::strlen(nm);
		/* 拡張子は OS 依存 (Linux/macOS=.so / MinGW・Cygwin=.dll)。osglue が持つ。 */
		const size_t SL = ::strlen(OSGLUE_MODULE_SUFFIX);
		if ( L > SL && ::strcmp(nm + L - SL, OSGLUE_MODULE_SUFFIX) == 0 ) {
			char path[1024];
			::snprintf(path, sizeof path, "%s/%s", dir, nm);
			out->push_back(std::make_pair(std::string(nm), std::string(path)));
		}
	}
	::closedir(d);
}

int
pigModuleRegistry::load_search_path(std::string *err)
{
	/* ★ 走査順 (ロードが後の方が「後勝ち」で優先):
	 *     sysdir < user config < exe dir < $SRAVA_MODULE_PATH
	 *   従来は exe dir を最初に読んでいたため **install 済み (sysdir) がビルドツリーに勝つ** 状態で、
	 *   ビルドし直しても古い install のモジュールが使われ続ける取り違えが起きた (ctest も同様に
	 *   install の状態に汚染されていた)。「より具体的な場所が勝つ」に揃える。
	 *   env による明示指定は引き続き最優先 (最後)。
	 *
	 *   ★★ 2026-08-13: **勝者だけを dlopen する 2 パス**に変更した。
	 *   走査順を直しただけでは足りなかった — 見つけた .so を全部 dlopen していたため、同名
	 *   モジュールが 2 つ (install 済み + ビルドツリー) プロセスに載り、各 .so が dlopen 時に行う
	 *   **静的自己登録の意味論の差**でちぐはぐな混成になっていた:
	 *       register_descriptor … 後勝ち → 記述子 (ops 表) は新しい方
	 *       register_agent      … 先勝ち → 実行体は **古い方**
	 *       register_codec      … 先勝ち → codec も **古い方**
	 *   結果、新しい ops 表で振り分けて古いコードを実行し、`srava --modules` は新しい方を勝者と
	 *   表示するので気づけない (実際に古い vendored ソルバが走ってヒープ破壊した)。
	 *   → 「1 モジュール名 (= 同じファイル名) につき dlopen は 1 回だけ」を不変条件にする。
	 *      こうすれば各レジストリの先勝ち/後勝ちの差は**そもそも問題にならない**。
	 *   読まなかった候補は shadowed として診断に残す (`srava --modules`)。 */

	/* ---- パス 1: 全探索路を順に **列挙のみ** (dlopen しない) ---- */
	std::vector<std::pair<std::string,std::string> > cands;   /* (ファイル名, フルパス) 走査順 */

	scan_dir(SRAVA_MODULE_SYSDIR, "sysdir", &cands);           /* ① install 既定 (最弱) */

	const char *home = ::getenv("HOME");                       /* ② ユーザ個人の上書き */
	if ( home != 0 && home[0] != '\0' ) {
		char p[1024];
		::snprintf(p, sizeof p, "%s/.config/srava/modules", home);
		scan_dir(p, "user config", &cands);
	}

	/* ③ 実行体と同じ dir (ビルドツリーでは .so が planner と同居 = install より具体的)。
	 *    取得は osglue_exe_path 経由 (旧: /proc/self/exe 直読み = Linux 専用)。 */
	char self[1024];
	if ( osglue_exe_path(self, (unsigned long)sizeof self) == 0 ) {
		char *slash = ::strrchr(self, '/');
		char *bslash = ::strrchr(self, '\\');
		if ( bslash != 0 && ( slash == 0 || bslash > slash ) ) slash = bslash;
		if ( slash != 0 ) { *slash = '\0'; scan_dir(self, "exe dir", &cands); }
	}

	const char *mp = ::getenv("SRAVA_MODULE_PATH");            /* ④ 明示指定 (最優先) */
	if ( mp != 0 && mp[0] != '\0' ) {
		const char *p = mp;
		while ( *p ) {
			/* 区切りは OS 依存 (POSIX=':' / Windows native=';')。':' で切ると "C:/..." を壊す。 */
			const char *colon = ::strchr(p, OSGLUE_PATHLIST_SEP);
			int len = colon ? (int)(colon - p) : (int)::strlen(p);
			if ( len > 0 && len < 1000 ) {
				char dir[1024];
				::snprintf(dir, sizeof dir, "%.*s", len, p);
				scan_dir(dir, "$SRAVA_MODULE_PATH", &cands);
			}
			if ( ! colon ) break;
			p = colon + 1;
		}
	}

	/* ---- パス 2: ファイル名ごとの勝者 (= 最後に現れたもの) を決める ---- */
	std::map<std::string,std::string> winner;                  /* ファイル名 → 勝者フルパス */
	for ( size_t i = 0 ; i < cands.size() ; ++i )
		winner[cands[i].first] = cands[i].second;              /* 後勝ちで上書き */

	/* ---- パス 3: 勝者だけを走査順に dlopen (負けた候補は shadowed 記録のみ) ---- */
	int n = 0;
	for ( size_t i = 0 ; i < cands.size() ; ++i ) {
		const std::string &nm = cands[i].first, &path = cands[i].second;
		std::map<std::string,std::string>::iterator w = winner.find(nm);
		if ( w == winner.end() || w->second != path ) {
			record_shadowed(path, ( w != winner.end() ) ? w->second : std::string());
			continue;
		}
		std::string e1;
		if ( load_file(path.c_str(), &e1, /*lazy=*/true) != 0 )
			++n;
		else if ( err )
			*err = e1;
	}

	return n;
}

/*
 * pigModuleLoader — .so 動的ロードの実装 (.so 化 Phase 3・docs §3.1)。
 * ★ #3427 ③: 旧 namespace pigModuleLoader を廃し、**pigModuleRegistry のメソッド**として実装する
 *   (load_file / load_search_path)。ロード記録 (旧 g_log / g_dirs) もレジストリのメンバ
 *   (log_v / dirs_v) へ移り、この TU の可変 static は消えた。
 */
#include "pig/c++/pigModuleRegistry.h"
#include "pig/c++/pigModule.h"

#include "pig/c++/osglue.h"   /* dlopen/dlsym の OS 差 + モジュール拡張子 (.so/.dll) */
#include "pig/c++/pigInstallPaths.h"   /* install レイアウトを実行体相対で解く (#3431) */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <utility>
#include <dirent.h>
#include <sys/stat.h>   /* stat: 探索路上の実ファイル確認 (#3431) */
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

	/* ★ #3439 ⑦: 記述子の**自己矛盾**を ABI 不一致と同じ扱いで弾く (fail-fast)。
	 *   例: export op を持つのに export_exts を申告しない → routing は拡張子で振るので
	 *   「誰も書けない」のに一般ロジックへ落ち、実行時に的外れなエラーになる。 */
	{
		std::string verr = pig_descriptor_violation(d);
		if ( ! verr.empty() ) {
			if ( err ) *err = verr;
			osglue_dlclose(h);
			return 0;
		}
	}

	/* 記述子から agent/型/codec/salt を **このレジストリへ** 一括登録 (#3427 ①: 登録経路 1 本)。
	 * .so は「記述子を返すだけ」の受け身 — 静的自己登録は全廃済みなので、dlopen が
	 * プロセス全体の状態を書き換えることはない (per-registry = per-app・リエントラント)。 */
	loadingPath_ = ( path != 0 ) ? path : "";   /* この記述子の出所を控える (#3425 ①) */
	register_descriptor(d);
	loadingPath_.clear();

	/* ★ 2026-08-28 (ひさ設計): ハンドルを **id ごとに保持**する。以前は意図的に捨てていたが
	 *   (登録した記述子が生き続ける必要があるため)、module(so,"off") を実アンロードにするので
	 *   dlclose するための控えが要る。 */
	{
		int id = id_of_name(d->name);
		if ( id >= 0 ) {
			if ( (size_t)id >= handle_v.size() ) handle_v.resize(id + 1, 0);
			handle_v[(size_t)id] = h;
		}
	}
	return d;
}

/* ★ #3431: 区切りを含まない名前 ("manifold.so") を **srava 自身の探索路**から解決する。
 *
 *   従来は `module("manifold.so")` が名前をそのまま dlopen へ渡していた。区切りの無い名前を
 *   渡された dlopen は「ライブラリ名」とみなして **ld.so の探索** (RUNPATH / LD_LIBRARY_PATH /
 *   ld.so.cache) に行くので、srava の探索路には一切当たらない。それでも動いていたのは、
 *   ビルドツリーの libpig.so の RUNPATH が空成分だけの ":::::…" になっていて、空成分 =
 *   **カレントディレクトリ** と解釈されるためで、要するに「ctest が build dir を cwd にして
 *   走らせているから」という偶然だった。install ツリーで別 dir から実行すると
 *   `module: manifold.so: cannot open shared object file` で落ちる (#3431 の実測で判明)。
 *
 *   → 名前だけ渡されたときは探索路 (dirs_v) の **強い順** に実ファイルを探し、見つかった
 *     フルパスへ解決する。load_search_path が採った勝者と同じ規則なので、
 *     「1 モジュール名につき dlopen は 1 回」(#3425) の不変条件も保たれる。 */
std::string
pigModuleRegistry::resolve_module_file(const char *path) const
{
	if ( path == 0 || path[0] == '\0' )
		return std::string();
	if ( ::strchr(path, '/') != 0 || ::strchr(path, '\\') != 0 )
		return std::string(path);        /* パス指定はそのまま (相対も呼び手の意図どおり) */

	/* dirs_v は走査順 (弱→強)。後勝ちなので **後ろから**見て最初に在ったものが勝者。 */
	for ( size_t i = dirs_v.size() ; i > 0 ; --i ) {
		const std::string &dir = dirs_v[i-1].dir;
		if ( dir.empty() )
			continue;
		std::string cand = dir + "/" + path;
		struct stat st;
		if ( ::stat(cand.c_str(), &st) == 0 )
			return cand;
	}
	return std::string(path);            /* 見つからない → 従来どおり dlopen に投げてエラーを出させる */
}

/* パスの末尾成分 (ファイル名)。区切りは '/' と '\\' の両方を見る (Windows)。 */
static const char *
base_name(const char *p)
{
	if ( p == 0 ) return "";
	const char *slash  = ::strrchr(p, '/');
	const char *bslash = ::strrchr(p, '\\');
	if ( bslash != 0 && ( slash == 0 || bslash > slash ) ) slash = bslash;
	return ( slash != 0 ) ? slash + 1 : p;
}

/* 2 つのパスが **同じ実ファイル**を指すか。文字列が違っても同じ実体でありうる
 * (`./cgal.so` と絶対パス / symlink / 同じ dir を指す 2 本の探索路) ので (dev,ino) で見る。
 * ★ 判定できないときは「違う」と答える — 誤って「同じ」と答えると二重ロードを黙って
 *   見逃す側 (= この検査を入れた意味が無い側) に倒れるため。Windows の stat は st_ino を
 *   0 にすることがあり、そのときは文字列一致だけが頼りになる。 */
static bool
same_file(const char *a, const char *b)
{
	if ( ::strcmp(a, b) == 0 )
		return true;
	struct stat sa, sb;
	if ( ::stat(a, &sa) != 0 || ::stat(b, &sb) != 0 )
		return false;
	if ( sa.st_ino == 0 || sb.st_ino == 0 )
		return false;              /* inode が無効な OS: 文字列一致で拾えなかった時点で諦める */
	return ( sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino );
}

int
pigModuleRegistry::id_of_loaded_file(const char *path) const
{
	if ( path == 0 || path[0] == '\0' ) return -1;
	const char *want = base_name(path);
	for ( size_t i = 0 ; i < descPath_v.size() ; ++i ) {
		if ( descPath_v[i].empty() ) continue;                 /* 組込 ("pig") はファイル由来でない */
		if ( i < descs_v.size() && descs_v[i] == 0 ) continue;
		if ( ::strcmp(base_name(descPath_v[i].c_str()), want) == 0 )
			return (int)i;
	}
	return -1;
}

/* 公開 API = 実体 + ロード記録。診断 (`srava --modules`) が「試したパスと理由」を全件持てるように、
 * 成否にかかわらずここで 1 件記録する。
 *
 * ★ **ロード済みなら何もしない** (冪等・2026-08-18)。起動時の探索路走査で全モジュールは
 *   ロード済みなので、`load()` / `module()` が指すのは普通ロード済みのものになる。ここで
 *   dlopen し直すと register_descriptor が走り、**ロード順 (seq) が書き換わって**しまう。
 *   「ロードはロード・module は記述子の上書き」であって、上書き op がロード順を動かすのは誤り。
 *   (dlopen 自体も、同じファイルなら同じハンドルが返るだけで意味が無い。)
 *
 * ★ ただし冪等なのは **同じ実ファイル**を指したときだけ。同じファイル名で別の実ファイルを
 *   指したら拒否する (下の same_file 判定)。 */
const srava_module_descriptor*
pigModuleRegistry::load_file(const char *path, std::string *err, bool lazy, bool *conflict)
{
	std::string e;
	bool nam = false;
	std::string resolved = resolve_module_file(path);   /* #3431: 名前だけなら探索路から解く */
	path = resolved.c_str();
	int already = id_of_loaded_file(path);
	if ( already >= 0 ) {
		/* ★ 2026-08-28 (ひさ指摘): 同じファイル名で **別の実ファイル**を指していたら明示エラー。
		 *   「1 モジュール名につき dlopen は 1 回」(#3425) が不変条件なので後から来た方は載せ
		 *   られないが、黙って先勝ちにすると `module("/tmp/experimental/cgal.so")` と書いたのに
		 *   探索路の cgal.so が動いている、という**最悪の形** (指定したのに効いていない) になる。
		 *   同じ実体を指す再指定 (`module(so)` の重複・include の二度読み) は従来どおり冪等。 */
		const char *had = descriptor_path(already);
		if ( had != 0 && had[0] != '\0' && ! same_file(had, path) ) {
			char buf[900];
			::snprintf(buf, sizeof buf,
			           "module file '%s' is already loaded from '%s'; refusing to load a "
			           "different file of the same name ('%s'). A module file name can be "
			           "loaded only once per process. Rename the file, or make the one you "
			           "want win the module search path (see `srava --modules`).",
			           base_name(path), had, path);
			if ( err != 0 )      *err = buf;
			if ( conflict != 0 ) *conflict = true;
			record(path, 0, buf, /*not_a_module=*/false);
			return 0;
		}
		return descriptor(already);
	}
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
	 *     sysdir < exe 相対 install dir < user config < exe dir < $SRAVA_MODULE_PATH
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
	enumerate_dirs(&cands);

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

/* ★ #3452: パス 1 (列挙のみ・dlopen しない) を load_search_path/enumerate_search_path で共有。
 * dirs_v へ走査した dir を記録しつつ、見つかった *.so 候補を *cands へ積む。 */
void
pigModuleRegistry::enumerate_dirs(std::vector<std::pair<std::string,std::string> > *cands)
{
	scan_dir(SRAVA_MODULE_SYSDIR, "sysdir", cands);           /* ① install 既定 (最弱) */

	/* ② ★ #3431: **実行体から見た** install モジュール dir (<exe dir>/../lib/srava/modules)。
	 *    ① は configure 時の prefix を焼き込んだ絶対パスなので、install ツリーを別の場所へ
	 *    置いた瞬間に「自分の兄弟の .so」を一切見なくなり、その機械の /usr/local にある
	 *    **別世代の install** を読んでしまう (実測で踏んだ)。焼き込みより「自分の隣」を強くする。
	 *    ビルドツリーには ../lib/srava/modules が無いので、そこでは何も拾わない = 挙動不変。 */
	scan_dir(srava_module_dir_relative_to_exe(), "exe-rel install", cands);   /* ラベルは ASCII: --modules の桁揃えが崩れる */

	const char *home = ::getenv("HOME");                       /* ③ ユーザ個人の上書き */
	if ( home != 0 && home[0] != '\0' ) {
		char p[1024];
		::snprintf(p, sizeof p, "%s/.config/srava/modules", home);
		scan_dir(p, "user config", cands);
	}

	/* ④ 実行体と同じ dir (ビルドツリーでは .so が planner と同居 = install より具体的)。
	 *    dir の解決は pigInstallPaths へ集約 (旧: ここで /proc/self/exe を直読み = Linux 専用)。 */
	const char *exedir = srava_exe_dir();
	if ( exedir[0] != '\0' )
		scan_dir(exedir, "exe dir", cands);

	const char *mp = ::getenv("SRAVA_MODULE_PATH");            /* ⑤ 明示指定 (最優先) */
	if ( mp != 0 && mp[0] != '\0' ) {
		const char *p = mp;
		while ( *p ) {
			/* 区切りは OS 依存 (POSIX=':' / Windows native=';')。':' で切ると "C:/..." を壊す。 */
			const char *colon = ::strchr(p, OSGLUE_PATHLIST_SEP);
			int len = colon ? (int)(colon - p) : (int)::strlen(p);
			if ( len > 0 && len < 1000 ) {
				char dir[1024];
				::snprintf(dir, sizeof dir, "%.*s", len, p);
				scan_dir(dir, "$SRAVA_MODULE_PATH", cands);
			}
			if ( ! colon ) break;
			p = colon + 1;
		}
	}
}

/* ★ #3452: 起動時の軽量初期化。dirs_v を埋めるだけで dlopen はしない — module() op が
 * resolve_module_file() 経由で名前を引けるようにするのが唯一の目的。実ロード (dlopen・
 * 静的初期化) は script が module(so) を呼んだ時点まで遅延する。 */
void
pigModuleRegistry::enumerate_search_path()
{
	std::vector<std::pair<std::string,std::string> > cands;   /* 使い捨て (dlopen しない) */
	enumerate_dirs(&cands);
}

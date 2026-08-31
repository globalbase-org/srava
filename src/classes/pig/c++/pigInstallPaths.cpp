/*
 * pigInstallPaths — install レイアウトを実行体からの相対で解く (#3431 P0-a)。
 * 設計の背景はヘッダ (pig/c++/pigInstallPaths.h) 冒頭を参照。
 */
#include	"pig/c++/pigInstallPaths.h"
#include	"pig/c++/osglue.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<sys/stat.h>

#ifndef SRAVA_AGENT_DEFAULT
#define SRAVA_AGENT_DEFAULT "/usr/local/bin/srava_agent"   /* install 既定 (CMake で上書き) */
#endif

/* パスの末尾成分の直前にある区切りを探す ('/' と '\\' の後勝ち・Windows は両方あり得る)。 */
static char *
last_sep(char *p)
{
	char *slash  = ::strrchr(p, '/');
	char *bslash = ::strrchr(p, '\\');
	if ( bslash != 0 && ( slash == 0 || bslash > slash ) ) slash = bslash;
	return slash;
}

const char *
srava_exe_dir()
{
	static char dir[1024];
	static int  done = 0;
	if ( ! done ) {
		done = 1;
		char self[1024];
		if ( osglue_exe_path(self, (unsigned long)sizeof self) == 0 ) {
			char *slash = last_sep(self);
			if ( slash != 0 ) {
				*slash = '\0';
				::snprintf(dir, sizeof dir, "%s", self);
			}
		}
	}
	return dir;
}

const char *
srava_module_dir_relative_to_exe()
{
	static char dir[1024];
	static int  done = 0;
	if ( ! done ) {
		done = 1;
		const char *ed = srava_exe_dir();
		if ( ed[0] != '\0' ) {
			/* "<exe dir>/../lib/..." ではなく **末尾成分を落として**組み立てる。診断表示
			 * (`srava --modules` / shadowed 一覧) に ".." が出ないので読み比べやすい。 */
			char up[1024];
			::snprintf(up, sizeof up, "%s", ed);
			char *slash = last_sep(up);
			if ( slash != 0 && slash != up ) {
				*slash = '\0';
				::snprintf(dir, sizeof dir, "%s/lib/srava/modules", up);
			} else {
				::snprintf(dir, sizeof dir, "%s/../lib/srava/modules", ed);
			}
		}
	}
	return dir;
}

const char *
srava_agent_path()
{
	static char path[1024];
	static int  done = 0;
	if ( ! done ) {
		done = 1;

		/* ① 明示指定 (最優先)。存在確認はしない — 指定が間違っているなら起動失敗で分かるべきで、
		 *    黙って別の agent へ落とすと「指定したのに効いていない」という最悪の形になる。 */
		const char *env = ::getenv("SRAVA_AGENT");
		if ( env != 0 && env[0] != '\0' ) {
			::snprintf(path, sizeof path, "%s", env);
			return path;
		}

		/* ② 自分の兄弟 <exe dir>/srava_agent[.exe]。実行ファイル名は SRAVA_AGENT_DEFAULT の
		 *    末尾成分から取る (CMake が付ける CMAKE_EXECUTABLE_SUFFIX に自動で追随する)。 */
		const char *ed = srava_exe_dir();
		if ( ed[0] != '\0' ) {
			char def[1024];
			::snprintf(def, sizeof def, "%s", SRAVA_AGENT_DEFAULT);
			char *slash = last_sep(def);
			const char *base = ( slash != 0 ) ? slash + 1 : def;
			char cand[1024];
			::snprintf(cand, sizeof cand, "%s/%s", ed, base);
			struct stat st;
			if ( ::stat(cand, &st) == 0 ) {
				::snprintf(path, sizeof path, "%s", cand);
				return path;
			}
		}

		/* ③ configure 時の install 既定 (従来の挙動)。 */
		::snprintf(path, sizeof path, "%s", SRAVA_AGENT_DEFAULT);
	}
	return path;
}

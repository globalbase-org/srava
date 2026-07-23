/*
 * pigPluginRegistry — マニフェスト(*.plugin)読み込みと op→bin 照会。pig 層・std のみ。
 */
#include "pig/c++/pigPluginRegistry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <vector>

#ifndef PIG_PLUGIN_SYSDIR
#define PIG_PLUGIN_SYSDIR "/usr/local/share/srava/plugins"
#endif

namespace {

struct Entry { std::string op, bin; int outMesh; };

std::vector<Entry> g_entries;
int g_loaded = 0;

/* "key = value" 行から key と value(前後空白除去)を取る。1=取得 / 0=非該当。 */
int parse_kv(const char *line, std::string& key, std::string& val) {
	const char *eq = ::strchr(line, '=');
	if ( eq == 0 ) return 0;
	const char *ks = line; const char *ke = eq;
	while ( ks < ke && (*ks==' '||*ks=='\t') ) ++ks;
	while ( ke > ks && (ke[-1]==' '||ke[-1]=='\t') ) --ke;
	const char *vs = eq + 1; const char *ve = vs + ::strlen(vs);
	while ( ve > vs && (ve[-1]=='\n'||ve[-1]=='\r'||ve[-1]==' '||ve[-1]=='\t') ) --ve;
	while ( vs < ve && (*vs==' '||*vs=='\t') ) ++vs;
	key.assign(ks, (size_t)(ke - ks));
	val.assign(vs, (size_t)(ve - vs));
	return ! key.empty();
}

/* 同名 op は **先勝ち**(先に読んだ探索パスが優先)。読み込み順は
 *   PIG_PLUGIN_PATH → ~/.config/srava/plugins → sysdir($PREFIX/share) なので、
 *   開発中の PIG_PLUGIN_PATH がインストール版(sysdir)を上書きできる(仕様どおり)。
 *   ※以前は後勝ちで sysdir が常に勝ち、PIG_PLUGIN_PATH を指定しても無視される不具合だった。 */
void register_entry(const Entry& e) {
	if ( e.op.empty() || e.bin.empty() ) return;
	for ( size_t i = 0 ; i < g_entries.size() ; ++i )
		if ( g_entries[i].op == e.op ) return;   /* 既に登録済み(高優先)→ そのまま */
	g_entries.push_back(e);
}

/* 1 マニフェストファイルを読む。 */
void load_file(const std::string& path) {
	FILE *f = ::fopen(path.c_str(), "rb");
	if ( f == 0 ) return;
	Entry e; e.outMesh = 0;
	char line[1024];
	while ( ::fgets(line, sizeof line, f) ) {
		const char *p = line;
		while ( *p==' '||*p=='\t' ) ++p;
		if ( *p=='#' || *p=='\n' || *p=='\r' || *p==0 ) continue;
		std::string k, v;
		if ( ! parse_kv(p, k, v) ) continue;
		if      ( k == "op" )   e.op  = v;
		else if ( k == "bin" )  e.bin = v;
		else if ( k == "out" )  e.outMesh = ( v == "mesh" ) ? 1 : 0;
		/* args 等は現状無視 */
	}
	::fclose(f);
	register_entry(e);
}

/* ディレクトリ内の *.plugin を読む。 */
void load_dir(const std::string& dir) {
	DIR *d = ::opendir(dir.c_str());
	if ( d == 0 ) return;
	struct dirent *de;
	while ( (de = ::readdir(d)) != 0 ) {
		const char *nm = de->d_name;
		size_t L = ::strlen(nm);
		if ( L < 7 || ::strcmp(nm + L - 7, ".plugin") != 0 ) continue;
		std::string path = dir;
		if ( ! path.empty() && path[path.size()-1] != '/' ) path += "/";
		path += nm;
		struct stat st;
		if ( ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) )
			load_file(path);
	}
	::closedir(d);
}

} /* anonymous namespace */

int pigplugin_registry_load()
{
	if ( g_loaded ) return (int)g_entries.size();
	g_loaded = 1;

	/* 1) $PIG_PLUGIN_PATH (':' 区切り) */
	const char *pp = ::getenv("PIG_PLUGIN_PATH");
	if ( pp != 0 && pp[0] != 0 ) {
		std::string s(pp), cur;
		for ( size_t i = 0 ; i <= s.size() ; ++i ) {
			/* Windows のドライブレター colon(例 "C:/...")は区切りにしない。
			 * Cygwin/MinGW では test が cygpath -m で native パスを渡すため、素朴な ':' split だと
			 * "C" と "/..." に割れて manifest(*.plugin)を見失う(plugin_echo が undefined variable 化)。 */
			bool drive = ( i + 1 < s.size() && s[i] == ':' && cur.size() == 1
			  && ((cur[0] >= 'A' && cur[0] <= 'Z') || (cur[0] >= 'a' && cur[0] <= 'z'))
			  && (s[i+1] == '/' || s[i+1] == '\\') );
			if ( ( i == s.size() || s[i] == ':' ) && ! drive ) {
				if ( ! cur.empty() ) load_dir(cur); cur.clear();
			} else cur += s[i];
		}
	}
	/* 2) ~/.config/srava/plugins/ */
	const char *home = ::getenv("HOME");
	if ( home != 0 && home[0] != 0 )
		load_dir(std::string(home) + "/.config/srava/plugins");
	/* 3) システム既定 */
	load_dir(PIG_PLUGIN_SYSDIR);

	return (int)g_entries.size();
}

static const Entry* find(const char *op) {
	if ( op == 0 ) return 0;
	for ( size_t i = 0 ; i < g_entries.size() ; ++i )
		if ( g_entries[i].op == op ) return &g_entries[i];
	return 0;
}

int pigplugin_is_op(const char *op)
{
	pigplugin_registry_load();
	return find(op) != 0 ? 1 : 0;
}

const char* pigplugin_op_bin(const char *op)
{
	pigplugin_registry_load();
	const Entry *e = find(op);
	return e ? e->bin.c_str() : 0;
}

int pigplugin_op_out_mesh(const char *op)
{
	pigplugin_registry_load();
	const Entry *e = find(op);
	return e ? e->outMesh : 0;
}

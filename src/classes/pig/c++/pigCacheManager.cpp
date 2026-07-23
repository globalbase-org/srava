/*
 * pigCacheManager — キャッシュディレクトリ機構(pig 層)。元は cgptsPlanner.cpp のファイルスコープ
 * helper だったが、pigwire のキャッシュ wire 形式だけに依存し CGAL/srava を知らない純 pig ロジック
 * なので、こちらへ移設(pig/srava の境界整理)。状態は持たず(1 回フラグ/版数指紋は ptsApplication 所有)、
 * 全て static メソッド。
 */
#include	"pig/c++/pigCacheManager.h"
#include	"pig/c++/pigwire.h"   /* キャッシュ wire 形式: streamhdr/W_END/rechdr/D_REF */
#include	"pig/c++/osglue.h"    /* osglue_pid_exists(writer 生存確認) */

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdint.h>
#include	<unistd.h>
#include	<dirent.h>
#include	<sys/stat.h>
#include	<utime.h>   /* used キャッシュの mtime touch(期日保持モード) */

/* 1 ファイルを開いて末尾の W_END 番兵まで歩く。
 *   戻り 1=完了(番兵あり)、0=未完(番兵なし)、-1=不正(PWIG ヘッダでない/短すぎ)。
 *   pid_out!=0 なら streamhdr の writer_pid を返す(完了/未完のとき有効)。 */
static int cache_has_end(FILE *f, uint32_t *pid_out)
{
	uint8_t hdr[WIRE_STREAMHDR_SIZE];
	if ( ::fread(hdr, 1, WIRE_STREAMHDR_SIZE, f) != (size_t)WIRE_STREAMHDR_SIZE )
		return -1;
	uint32_t pid = 0;
	if ( wire_check_streamhdr(hdr, &pid) != WIRE_OK )
		return -1;
	if ( pid_out ) *pid_out = pid;
	for ( ;; ) {
		uint8_t rh[WIRE_RECHDR_SIZE];
		if ( ::fread(rh, 1, WIRE_RECHDR_SIZE, f) != (size_t)WIRE_RECHDR_SIZE )
			return 0;   /* レコード境界で尽きた = 番兵未到達 */
		uint16_t type, flags; uint32_t len;
		wire_get_rechdr(rh, &type, &flags, &len);
		if ( type == W_END )
			return 1;
		if ( ::fseek(f, (long)len, SEEK_CUR) != 0 )
			return 0;
	}
}

/* ファイル名 "<16hex>.cache" から hash を取り出す。1=取得(*h に格納), 0=形式不一致。 */
static int parse_cache_hash(const char *nm, INTEGER64 *h)
{
	char *end = 0;
	unsigned long long v = ::strtoull(nm, &end, 16);
	if ( end != nm + 16 || ::strcmp(end, ".cache") != 0 )
		return 0;
	*h = (INTEGER64)v;
	return 1;
}

static int hash_in_used(INTEGER64 h, const INTEGER64 *used, int nused)
{
	for ( int i = 0 ; i < nused ; ++i )
		if ( used[i] == h )
			return 1;
	return 0;
}

/* 参照キャッシュ(D_REF: export 出力 / import 入力)の妥当性チェック。
 * 先頭レコードが D_REF で、その path が指す実ファイルが存在しなければ 1(= stale)。
 * D_REF でない(mesh/data)キャッシュや読めない場合は 0(触らない)。
 * D_REF payload: kind(u8) + path_len(u16 LE) + path(bytes) + size/mtime/chash(各 i64)。 */
static int cache_ref_file_missing(const char *cachePath)
{
	FILE *f = ::fopen(cachePath, "rb");
	if ( f == 0 ) return 0;
	uint8_t hdr[WIRE_STREAMHDR_SIZE];
	uint8_t rh[WIRE_RECHDR_SIZE];
	if ( ::fread(hdr, 1, WIRE_STREAMHDR_SIZE, f) != (size_t)WIRE_STREAMHDR_SIZE
	  || ::fread(rh,  1, WIRE_RECHDR_SIZE,  f) != (size_t)WIRE_RECHDR_SIZE ) { ::fclose(f); return 0; }
	uint16_t type, flags; uint32_t len;
	wire_get_rechdr(rh, &type, &flags, &len);
	if ( type != D_REF || len < 3 ) { ::fclose(f); return 0; }
	uint8_t pre[3];
	if ( ::fread(pre, 1, 3, f) != 3 ) { ::fclose(f); return 0; }
	uint32_t pl = (uint32_t)pre[1] | ((uint32_t)pre[2] << 8);   /* path_len(LE) */
	if ( pl == 0 || pl > 4000 ) { ::fclose(f); return 0; }
	char pathbuf[4096];
	if ( ::fread(pathbuf, 1, pl, f) != pl ) { ::fclose(f); return 0; }
	pathbuf[pl] = 0;
	::fclose(f);
	return ( ::access(pathbuf, F_OK) != 0 ) ? 1 : 0;   /* 参照先が消えていたら stale */
}

/* 掃除の **per-file** ログは既定で出さない(数が多くて煩い)。PIG_DEBUG が立っている時だけ出す。
 * 件数サマリ([pig] startup sweep / exit cleanup: N 個)は常に出す。 */
static int sweep_verbose() { static int v = ( ::getenv("PIG_DEBUG") != 0 ); return v; }

/* CACHE_DIR の *.cache を掃除。戻り = 削除数。
 *   used==0 (起動時スイープ): 死体(番兵なし & writer_pid not live / 不正ヘッダ)のみ削除。完了は残す。
 *   used!=0 (終了時クリーンアップ): 上記に加え「この run で未使用の完了キャッシュ」を方針 retain_mode に従って削除。
 *     retain_mode 0=未使用の完了を全削除(従来) / 1=完了は mtime < cutoff のものだけ削除 / 2=完了は削除しない。
 *     retain_mode != 0 のとき used のキャッシュは mtime を now に touch して延命する。
 * いずれも used に載る hash は最優先で残し、writer_pid が live の未完(別プロセス書込中)は消さない。 */
static int sweep_cache_dir(const char *dir, const INTEGER64 *used, int nused,
                           int retain_mode, INTEGER64 cutoff)
{
	DIR *d = ::opendir(dir);
	if ( d == 0 )
		return 0;   /* dir なし → 掃除対象なし */
	int removed = 0;
	struct dirent *e;
	while ( (e = ::readdir(d)) != 0 ) {
		const char *nm = e->d_name;
		size_t L = ::strlen(nm);
		if ( L < 6 || ::strcmp(nm + L - 6, ".cache") != 0 )
			continue;   /* *.cache 以外(info.txt 等)は触らない */
		char path[1024];
		::snprintf(path, sizeof path, "%s/%s", dir, nm);
		INTEGER64 h = 0;
		int hash_ok = parse_cache_hash(nm, &h);
		if ( used != 0 && hash_ok && hash_in_used(h, used, nused) ) {
			if ( retain_mode != 0 )
				::utime(path, 0);   /* 使用したキャッシュは now に touch して延命(期日/全保持モード) */
			continue;   /* この run で使用 → 最優先で残す */
		}
		struct stat st;
		if ( ::stat(path, &st) != 0 || !S_ISREG(st.st_mode) )
			continue;
		FILE *f = ::fopen(path, "rb");
		if ( f == 0 )
			continue;
		uint32_t pid = 0;
		int hasEnd = cache_has_end(f, &pid);
		::fclose(f);
		if ( hasEnd == 1 ) {
			/* 完了キャッシュ。 */
			if ( used == 0 ) {
				/* 起動時: 原則残す。ただし参照キャッシュ(D_REF: export 出力/import 入力)が指す
				 * 実ファイルが消えていたら削除 → 次の評価で export/import が MISS して再生成/再解決する
				 * (例: 生成した STL を手で消して再実行 → ちゃんと作り直される)。 */
				if ( cache_ref_file_missing(path) && ::unlink(path) == 0 ) {
					removed++;
					if ( sweep_verbose() )
						::fprintf(stderr, "[pig] swept stale ref cache (referenced file gone): %s\n", path);
				}
				continue;
			}
			/* 終了時(used!=0)で未使用の完了キャッシュ。retain_mode で扱いを分ける。 */
			if ( retain_mode == 2 )
				continue;   /* 全保持 → 完了キャッシュは消さない */
			if ( retain_mode == 1 && (INTEGER64)st.st_mtime >= cutoff )
				continue;   /* 期日保持 → cutoff より新しいものは残す(revert 用) */
			if ( ::unlink(path) == 0 ) {
				removed++;
				if ( sweep_verbose() )
					::fprintf(stderr, "[pig] swept unused cache: %s\n", path);
			}
			continue;
		}
		/* 未完(0)/不正ヘッダ(-1)= 死体候補。未完は writer_pid live なら別プロセス書込中 → 残す。 */
		int alive = ( hasEnd == 0 ) ? osglue_pid_exists(pid) : 0;
		if ( alive == 1 )
			continue;
		if ( ::unlink(path) == 0 ) {
			removed++;
			if ( sweep_verbose() )
				::fprintf(stderr, "[pig] swept corpse cache: %s\n", path);
		}
	}
	::closedir(d);
	return removed;
}

/* dir 内の *.cache を全削除(版ずれ時の全クリア)。戻り=削除数。 */
static int clear_all_cache_files(const char *dir)
{
	DIR *d = ::opendir(dir);
	if ( d == 0 ) return 0;
	int removed = 0;
	struct dirent *e;
	while ( (e = ::readdir(d)) != 0 ) {
		const char *nm = e->d_name;
		size_t L = ::strlen(nm);
		if ( L < 6 || ::strcmp(nm + L - 6, ".cache") != 0 ) continue;
		char path[1024];
		::snprintf(path, sizeof path, "%s/%s", dir, nm);
		if ( ::unlink(path) == 0 ) removed++;
	}
	::closedir(d);
	return removed;
}

/* ---- キャッシュ版数ゲート(機構のみ・pig) ----
 * 「何が変わったらキャッシュ無効か」の指紋(want)は呼び出し側=srava が決めて渡す。ここは info.txt と
 * 比較して不一致なら dir の全 *.cache を消し info.txt を書き直すだけ。戻り=クリアしたか(1/0)。 */
static int version_gate(const char *dir, const char *want)
{
	char info[1024];
	::snprintf(info, sizeof info, "%s/info.txt", dir);
	char have[512] = "";
	FILE *f = ::fopen(info, "rb");
	if ( f != 0 ) {
		size_t n = ::fread(have, 1, sizeof have - 1, f);
		have[n] = 0;
		::fclose(f);
	}
	if ( ::strcmp(have, want) == 0 )
		return 0;   /* 一致 → 何もしない */

	int cleared = clear_all_cache_files(dir);
	if ( cleared > 0 )
		::fprintf(stderr, "[pig] cache version changed (agent/OS) → cleared %d cache(s) in %s\n", cleared, dir);
	f = ::fopen(info, "wb");
	if ( f != 0 ) { ::fwrite(want, 1, ::strlen(want), f); ::fclose(f); }
	return 1;
}


/*******************************************
	PUBLIC (static)
********************************************/

void pigCacheManager::startup_sweep(const char *dir, const char *fingerprint)
{
	if ( fingerprint != 0 && fingerprint[0] != 0 )
		version_gate(dir, fingerprint);   /* 版ずれなら全クリア(以降の sweep は新版前提) */
	int swept = sweep_cache_dir(dir, 0, 0, 0, 0);   /* used==0: 死体のみ掃除(完了キャッシュは残す) */
	::fprintf(stderr, "[pig] startup sweep: %d corpse(s) removed in %s\n", swept, dir);
}

int pigCacheManager::exit_sweep(const char *dir, const INTEGER64 *used, int nused,
                                int retain_mode, INTEGER64 cutoff_epoch)
{
	return sweep_cache_dir(dir, used, nused, retain_mode, cutoff_epoch);
}

void pigCacheManager::make_path(char *buf, size_t bufsz, const char *dir, INTEGER64 h)
{
	::snprintf(buf, bufsz, "%s/%016llx.cache", dir, (unsigned long long)(uint64_t)h);
}

#ifndef ___pigCacheManager_H___
#define ___pigCacheManager_H___

/* pigCacheManager — 内容アドレス化キャッシュ「ディレクトリ」機構(pig 層・CGAL/srava 非依存)。
 * pigwire のキャッシュ wire 形式(W_END 番兵 / streamhdr / writer_pid)だけに依存し、ディレクトリを
 * 舐めて「死体(書込中に死んだ writer の未完キャッシュ)」「参照先が消えた D_REF」「未使用キャッシュ」
 * を掃除する。**状態は持たない**(1 回フラグ/版数指紋は呼び出し側=ptsApplication が保持して渡す)。
 * 「何が変わったらキャッシュ無効か(版数指紋)」を決めるのは srava(cgptsPlanner)側 → 指紋文字列で渡す。
 *   - pig: ディレクトリを舐めて消す機構 / srava: いつ呼ぶか + 指紋の中身。 */

#include	<stddef.h>
#include	"ts2/c++/ts_types.h"   /* INTEGER64 (tinyState v2: ts2/c/ 廃止→c++/ に inline) */

class pigCacheManager {
public:
	/* 起動時スイープ: fingerprint が非空かつ dir/info.txt と不一致なら全 *.cache をクリア(版ゲート)。
	 * 続いて死体(番兵なし & writer 死亡)と参照先が消えた D_REF を除去。完了キャッシュは残す。
	 * fingerprint が空(NULL/"")なら版ゲートは skip(死体掃除だけ)。 */
	static void	startup_sweep(const char *dir, const char *fingerprint);

	/* 終了時スイープ: used[] に載らない完了キャッシュ + 死体を除去。戻り=削除数。
	 *   retain_mode: 0=即削除(未使用の完了キャッシュを全削除・従来既定)
	 *                1=期日保持(完了キャッシュは mtime < cutoff_epoch のものだけ削除。残すものは温存)
	 *                2=全保持(完了キャッシュは一切削除しない)
	 *   いずれのモードでも死体(番兵なし & writer 死亡)は削除する。
	 *   retain_mode != 0 のときは used[] のキャッシュの mtime を「今」に更新(touch)して延命する。
	 *   「何日残すか/いつ消すか」の方針(cutoff の算出)は呼び出し側=srava が決める(env 解釈は planner)。 */
	static int	exit_sweep(const char *dir, const INTEGER64 *used, int nused,
	                       int retain_mode, INTEGER64 cutoff_epoch);

	/* dir 内の *.cache を **数えるだけ**(掃除も touch もしない)。戻り = 完了キャッシュ数。
	 *   complete   : W_END 番兵まで到達したもの(= 次の run が HIT で拾える「完成品」)
	 *   incomplete : 番兵未到達(writer が書きかけ / 中断で死んだ)
	 *   broken     : PWIG ヘッダでない・短すぎて判定できない
	 * 各ポインタは 0 可(要らないものは渡さなくてよい)。
	 * ★ 存在理由(#3419 ゲート入場順序の実験・2026-08-23): 「中断時に完成キャッシュが何個残ったか」を
	 *   測るのに、**判定基準を外部スクリプトへ再実装させない**ため。ファイル数を数えるだけでは
	 *   書きかけの死体まで数えてしまい、指標が嘘になる。判定は sweep と同じ cache_has_end を使う。 */
	static int	count_caches(const char *dir, int *complete, int *incomplete, int *broken);

	/* <dir>/<16hex(h)>.cache を buf へ書く(キャッシュファイル名の正規形)。 */
	static void	make_path(char *buf, size_t bufsz, const char *dir, INTEGER64 h);
};

#endif

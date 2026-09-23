/*
 * pipeprox_break_thread — pipeprox_break_test の**並行部分だけ**を持つ小さな TU。
 *
 * ★★ なぜ分けるか (Linux + libstdc++ の C++20):
 *   vendor は @c namespace pipe を名乗る。一方 libstdc++ の C++20 版 @c <atomic> は
 *   @c bits/atomic_wait.h 経由で @c <unistd.h> を引き込み、そこに @c ::pipe(int[2]) が居る。
 *   名前空間名と関数名は同じ宣言領域なので、**同じ TU に同居できない**:
 *       error: 'namespace pipe { }' redeclared as different kind of entity
 *   ⚠ **include の順では直らない** — どちらが先でも衝突する。TU を分けるしかない。
 *   ⚠ C++17 では起きない (@c atomic_wait.h が入らない)。macOS の libc++ でも起きないので、
 *     macOS 側のビルドでは見えていなかった (2026-09-12 の統合で Linux 側が踏んだ)。
 *   ⇒ @c <atomic> / @c <thread> を使う側をこちらへ寄せ、テスト本体は vendor だけを見る。
 *     pipe_proximity_compute.cpp が「pipe ヘッダは触らない」と書いているのと同じ分け方。
 *
 * ★ 旗は 1 本の大域。テストは逐次に 1 検定ずつ回すので、これで足りる。
 */
#ifndef	PIPEPROX_BREAK_THREAD_H
#define	PIPEPROX_BREAK_THREAD_H

bool	ppbrk_get();			/* 述語: 旗が立っているか (cp.cancelled から呼ぶ) */
void	ppbrk_set();			/* 旗を立てる */
void	ppbrk_clear();			/* 旗を倒す (次の検定の前に) */

/* ★ arm → go の 2 段にしてあるのは、**測り始めてから sec 後**に旗が立つようにするため。
 *   arm でスレッドを起こし、そのスレッドは go を待ってから sec 眠る。スレッド生成の
 *   ばらつき (数十 µs) が「走行中の何秒で止めたか」に混ざらない。元の実装の @c go 相当。 */
void	ppbrk_arm(double sec);		/* 別スレッドを起こし、go を待たせる */
void	ppbrk_go();			/* そのスレッドの計時を開始させる */
void	ppbrk_join();			/* 畳む (立っていなければ立ててから待つ) */

#endif	/* PIPEPROX_BREAK_THREAD_H */

/*
 * pipeprox_break_thread — 実装。★ **vendor の pipe ヘッダを include しないこと** (理由はヘッダ冒頭)。
 */
#include	"pipeprox_break_thread.h"

#include	<atomic>
#include	<chrono>
#include	<thread>

static std::atomic<int>	g_flag(0);
static std::atomic<int>	g_go(0);
static std::thread	g_th;

bool	ppbrk_get()	{ return g_flag.load() != 0; }
void	ppbrk_set()	{ g_flag.store(1); }
void	ppbrk_clear()	{ g_flag.store(0); g_go.store(0); }

void
ppbrk_arm(double sec)
{
	g_th = std::thread([sec]{
		while ( ! g_go.load() ) std::this_thread::yield();
		std::this_thread::sleep_for(std::chrono::duration<double>(sec));
		g_flag.store(1);
	});
}

void	ppbrk_go()	{ g_go.store(1); }

void
ppbrk_join()
{
	if ( ! g_th.joinable() ) return;
	g_go.store(1);			/* arm したまま go を忘れても止まらないように */
	g_th.join();
	g_th = std::thread();
}

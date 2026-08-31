// cd の並列プリミティブ実装。**namespace pipe を開かない**(POSIX pipe() 衝突回避のため別 TU)。
#include "pipe/cd_parallel.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <exception>

namespace pipe_par {

namespace {

// 常駐ワーカープール(akira-project #3419)。
//
// 以前は parallel_for が呼び出しのたびに std::thread を生成して join していた。座標降下の
// ホットループ底から呼ばれるため大量のスレッドが生成・破棄され、
//   - in-proc(既定 EXEC_THREAD)では planner プロセス内で 8MB スタックの mmap/munmap が churn し、
//   - tid を基準にした観測(agentwatch)を壊し、
//   - バリアコストが常駐プール比で大きくなる
// という害があった。並列そのものは効いているので、機構だけ替える。
// (実測値はソースに置かない・ひさ裁定 2026-08-27。数字は Redmine #3419。)
//
// ★ プールは**呼び出しスレッドごと**(thread_local)。in-proc では複数の agent スレッドが
//   同時に parallel_for へ入るため、プロセス共有の 1 プールにすると op 間で直列化してしまう。
// ★ 呼び出し元自身も 1 本ぶんの仕事をする。ワーカーは T-1 本。
// ★ 所有スレッドの終了時に **join しない**。停止フラグを立てて detach するだけ。
//   ⚠ 旧実装はここで join しており、**MinGW で 4 件のテストがハングした** (25% 再現)。
//     Windows では `thread_local` のデストラクタが **`LdrShutdownThread` の中 = ローダロックを
//     保持した状態**で走る (mingw-w64 の tls_atexit.c から呼ばれる)。そこで join すると、
//     待つ相手のワーカーもスレッド離脱に同じローダロックを要するため、互いに進めなくなる。
//     「DllMain でスレッドを join してはいけない」という規則が thread_local にも及ぶ。
//     旧コメントの「ワーカーは自前の条件変数でしか待たないので join は即返る」は誤り。
//     条件変数からは抜けるが、その後のスレッド離脱処理で止まる。
//   ★ Linux/macOS には この制約が無いので旧実装でも動くが、**分岐させない**。
//     片方でしか通らない経路を作ると、そちらだけで壊れたときに気づけない。
class Pool {
public:
	Pool() {}
	// ワーカーに終了を指示して手を放す。**待たない** (上記の理由)。
	// 停止フラグは立てるのでワーカーは確実に抜ける = スレッドは溜まらない。
	// detach でスレッドハンドルも解放する。
	void quiesce(){
		{ std::lock_guard<std::mutex> lk(mtx_); stop_ = true; }
		cvWork_.notify_all();
		for(std::thread& t : workers_) if(t.joinable()) t.detach();
	}

	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;

	// ワーカーを want 本まで増やす(減らさない。nthreads は 1 回の座標降下の中では不変)。
	void grow(int want){
		while((int)workers_.size() < want) workers_.emplace_back([this]{ loop(); });
	}

	// [0,count) を「呼び出し元 + ワーカー全員」で消化し、全員の完了を待つ。
	// fn が投げたら最初の 1 つを呼び出し元へ送り直す(以前はワーカー上の throw で std::terminate だった)。
	void run(int count, const std::function<void(int)>& fn){
		{
			std::lock_guard<std::mutex> lk(mtx_);
			job_   = &fn;
			total_ = count;
			next_.store(0, std::memory_order_relaxed);
			err_   = nullptr;
			busy_  = (int)workers_.size();     // 呼び出し元は数えない
			++epoch_;
		}
		cvWork_.notify_all();
		drain();                               // ★ 呼び出し元も働く
		std::unique_lock<std::mutex> lk(mtx_);
		cvDone_.wait(lk, [this]{ return busy_ == 0; });
		job_ = 0;
		if(err_){ std::exception_ptr e = err_; err_ = nullptr; lk.unlock(); std::rethrow_exception(e); }
	}

private:
	// 添字を atomic で取り合う(work-steal)。どの添字も**ちょうど 1 回**実行されるので、
	// 試行結果を添字ごとに書き分ける呼び出し側の決定性は保たれる。
	void drain(){
		const std::function<void(int)>* fn = job_;
		for(;;){
			int i = next_.fetch_add(1, std::memory_order_relaxed);
			if(i >= total_) return;
			try { (*fn)(i); }
			catch(...){
				std::lock_guard<std::mutex> lk(mtx_);
				if(!err_) err_ = std::current_exception();
				next_.store(total_, std::memory_order_relaxed);   // 残りは配らない
				return;
			}
		}
	}

	void loop(){
		unsigned long seen = 0;
		for(;;){
			std::unique_lock<std::mutex> lk(mtx_);
			cvWork_.wait(lk, [this,&seen]{ return stop_ || epoch_ != seen; });
			if(stop_) return;
			seen = epoch_;
			lk.unlock();
			drain();
			lk.lock();
			if(--busy_ == 0){ lk.unlock(); cvDone_.notify_one(); }
		}
	}

	std::vector<std::thread>	workers_;
	std::mutex					mtx_;
	std::condition_variable		cvWork_, cvDone_;
	const std::function<void(int)>* job_ = 0;
	std::atomic<int>			next_{0};
	int							total_ = 0;
	int							busy_  = 0;
	unsigned long				epoch_ = 0;
	bool						stop_  = false;
	std::exception_ptr			err_;
};

// ★ Pool を **意図的にリークさせる** (delete しない) 保持者。
//   quiesce() の後もワーカーは mtx_ / cvWork_ / stop_ を触りうる (条件変数から抜けて
//   loop() を返るまでの間)。join で待てない以上、**実体を解放してはならない**。
//   リーク量は「parallel_for を呼んだスレッド 1 本につき Pool 1 個」で頭打ちになる。
//   ts2 のワーカースレッドは使い回されるため (op 数を 10 倍にしても種類は増えないことを実測)、
//   op を何度呼んでも増え続けることはない。プロセス終了時に OS が回収する。
struct PoolHolder {
	Pool* p;
	PoolHolder() : p(new Pool()) {}
	~PoolHolder(){ p->quiesce(); }        // ★ delete しない・join しない
	PoolHolder(const PoolHolder&) = delete;
	PoolHolder& operator=(const PoolHolder&) = delete;
};

} // namespace

void parallel_for(int count, int nthreads, const std::function<void(int)>& fn){
	if(nthreads <= 1 || count <= 1){ for(int i=0;i<count;i++) fn(i); return; }
	int T = std::min(nthreads, count);
	static thread_local PoolHolder holder;
	holder.p->grow(T - 1);     // 呼び出し元が 1 本ぶんを持つ
	holder.p->run(count, fn);
}

unsigned hw_threads(){ return std::thread::hardware_concurrency(); }

} // namespace pipe_par

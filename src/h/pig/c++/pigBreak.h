#ifndef ___pigBreak_H___
#define ___pigBreak_H___
/*
 * pigBreak.h — 計算中の op に「もう要らない」を伝えるための旗 (#3498 / 親 #3417)。
 *
 * #3417 で destroy は planner から **全モジュールの calc オブジェクトまで**届くようになった。
 * 残っていたのは「calc の中で中断要求を見て、ライブラリの中断機構を叩く」ことだけで、
 * occt (Message_ProgressIndicator) / openvdb (InterruptT) / manifold (ExecutionContext) は
 * どれも *ポーリング型* — 「計算スレッドが定期的に問い合わせ、別スレッドが旗を立てる」
 * という同じ形をしている。違うのは器を誰が用意するかだけなので、旗そのものは 1 つで足りる。
 *
 * ★ **なぜ is_destroyed() を直接見ないのか**
 *   計算中に中断を知る手が is_destroyed() であることは #3417 で実測した通り (destroy() は
 *   呼び手のスレッドで直にフラグを立てるので即座に真になる。TSE_DESTROY はキューに溜まって
 *   計算中は配送されない)。ただしその旗は tinyState_ の **ビットフィールド** (destroy_flag:1)
 *   で、隣のビットと同じワードに同居している。ライブラリの中断機構は
 *
 *     ・occt   Message_ProgressIndicator::UserBreak — 「並行に呼ばれうるのでスレッド安全に
 *              実装すること」とヘッダに明記がある
 *     ・openvdb / manifold も内部でスレッドを起こす
 *
 *   ので、**ワーカースレッドから引かれる**ことを前提にしなければならない。ビットフィールドへの
 *   非同期アクセスは隣のビットまで巻き込む競合になる。⇒ 自前の std::atomic を 1 枚挟み、
 *   ライブラリ側にはこちらだけを見せる。is_destroyed() は今までどおり *tinyState のスレッド*
 *   から見る (ptsCalcBody::destroy が両者を繋ぐ)。
 *
 * ★ 旗は ptsCalcBody が持ち、ptsCalcBody::destroy() が立てる。モジュール側は
 *   compute() から protected メンバ brk_ を op へ渡すだけでよい (destroy の override は不要)。
 *
 * ⚠ **中断を成功として返さないこと**。中断は「答えが出なかった」であって「答えは空」ではない。
 *   空や途中の結果を set_body するとキャッシュに焼き付き、次回以降 *正しい答えとして引かれる*
 *   (#3489 で cache_version を上げ忘れて古い誤値が返ったのと同じ形の事故になる)。
 *   ⇒ 中断したら必ず pigDataError を返す。ptsGenericAgent は err 経路では set_body しない。
 */
#include	<atomic>
#include	<functional>
#include	<mutex>

class pigBreak {
public:
	/* 立てる側 (tinyState のスレッド)。冪等。
	 * ★ 旗を立てたあと、登録があれば押し出し口 (下の pigBreakHook) を呼ぶ。 */
	void	cancel();
	/* 見る側 (ライブラリのワーカースレッドを含む)。
	 * ★ relaxed で足りる — 運ぶのは「中断されたか」という 1 bit だけで、これに伴って
	 *   別のメモリを読ませる約束が無い。中断は本質的に遅延を許す (次のポーリング点までは
	 *   走り続ける) ので、順序を強めても速くならない。 */
	int	cancelled() const	{ return f_.load(std::memory_order_relaxed); }

	/* ---- ★ 押し出し口 (#3498・manifold のために足した) --------------------------
	 * ポーリング型のライブラリ (occt / openvdb) は「こちらの旗を見に来る」ので旗だけで足りる。
	 * ところが **manifold は見に来ない** — 中断の口が @c ExecutionContext::Cancel() という
	 * *こちらから撃つ* 関数しかない。計算スレッドは Status() の中で止まっているので、
	 * 撃てるのは destroy() を呼んだ側のスレッドしかない。⇒ cancel() のときに呼ぶ関数を
	 * 登録できるようにする。
	 *
	 * ⚠ 呼ばれるのは **cancel() を呼んだスレッド** (= tinyState 側)。計算スレッドではない。
	 *   manifold の Cancel() は "Can be called from any thread. Idempotent." なので適合する。
	 * ⚠ 寿命が肝: フックが掴む ctx は計算スレッドのローカルなので、**計算が抜けた後に
	 *   呼ばれてはいけない**。登録と解除と呼び出しを同じ mutex で直列化し、解除が返った
	 *   時点で「もう呼ばれない」を保証する。⇒ 生の set/clear は公開せず、
	 *   スコープ物 (pigBreakHook) 経由でだけ使わせる。
	 * ⚠ 登録時に既に旗が立っていたら **その場で呼ぶ**。でないと「destroy が先・計算が後」の
	 *   順で中断を取りこぼす。 */
	friend class pigBreakHook;
private:
	void	set_hook(std::function<void()> h)
	{
		int already;
		{
			std::lock_guard<std::mutex> lk(mu_);
			hook_ = h;
			already = cancelled();
		}
		if ( already && h ) h();
	}
	void	clear_hook()
	{
		std::lock_guard<std::mutex> lk(mu_);
		hook_ = std::function<void()>();
	}

	std::atomic<int>	f_{0};
	mutable std::mutex	mu_;
	std::function<void()>	hook_;
};

/* cancel() で hook を呼ぶための実体 (宣言順の都合でクラス外に置く)。 */
inline void
pigBreak::cancel()
{
	f_.store(1, std::memory_order_relaxed);
	std::lock_guard<std::mutex> lk(mu_);
	if ( hook_ ) hook_();
}

/* ★ フックのスコープ物。ctor で登録・dtor で解除。brk==0 なら何もしない。
 *   使い方 (mfMesh::force_eval):
 *       manifold::ExecutionContext ctx;
 *       pigBreakHook hook(brk, [&]{ ctx.Cancel(); });
 *       ... m_.WithContext(ctx).Status() ...
 *   dtor が返った時点でフックは二度と呼ばれないので、ctx をこの後に壊してよい。 */
class pigBreakHook {
public:
	pigBreakHook(const pigBreak *b, std::function<void()> h) : b_(const_cast<pigBreak *>(b))
	{ if ( b_ ) b_->set_hook(h); }
	~pigBreakHook()	{ if ( b_ ) b_->clear_hook(); }
private:
	pigBreakHook(const pigBreakHook&);
	pigBreakHook& operator=(const pigBreakHook&);
	pigBreak *b_;
};

#endif

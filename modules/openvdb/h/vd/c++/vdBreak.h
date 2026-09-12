#ifndef VD_BREAK_H
#define VD_BREAK_H
/*
 * vdBreak — OpenVDB の中断機構 (InterruptT) と srava の旗 (pigBreak) をつなぐ層 (#3498)。
 *
 * OpenVDB の作法は「算法へ interrupter を渡す → 算法が要所で wasInterrupted() を引く」。
 * 器 (util::NullInterrupter の派生) を用意するのは *こちら*。
 *
 * ★★ **NullInterrupter は仮想関数を持つ** (OpenVDB 9 以降)。これが効く:
 *   meshToVolume 等は Interrupter を **テンプレート引数**で取るが、OpenVDB は
 *   OPENVDB_USE_EXPLICIT_INSTANTIATION のもとで @c util::NullInterrupter& の実体だけを
 *   .so に持つ (MeshToVolume.h の末尾)。派生クラスを *その型の参照として* 渡せば、
 *   既存の実体化のまま virtual dispatch でこちらの wasInterrupted() が呼ばれる。
 *   ⇒ 独自の型で実体化し直さない = **コンパイル時間もコードサイズも増やさずに**中断できる。
 *
 * ⚠⚠ **openvdb の中断点は一様ではない**。実装前に数えた結果 (OpenVDB 12・本ツリーの版):
 *
 *     tools/MeshToVolume.h    11 箇所   ← voxelize / renormalize の後半
 *     tools/LevelSetTracker.h  2 箇所   ← offset (LevelSetFilter が内部で使う)
 *     tools/GridTransformer.h  1 箇所   ← affine (resampleToMatch)
 *     tools/LevelSetSphere.h   1 箇所   ← sphere プリミティブ
 *     tools/Composite.h        **0**    ← ★ csgUnion / Intersection / Difference は止まらない
 *     tools/VolumeToMesh.h     **0**    ← ★ isosurface / renormalize の前半は止まらない
 *     tools/LevelSetPlatonic.h **0**    ← box 等のプリミティブ
 *
 *   ⇒ **ブール本体は止まらない**。これは occt (Build がそのまま BOPAlgo へ降りる) との
 *     決定的な差で、#3498 で openvdb を occt の次に置いた理由でもある。止まるのは
 *     voxelize / offset / affine と、srava 自前の計測ループ (vd_measure)。
 *   ⚠ 「openvdb を配線した」を「openvdb の op は中断できる」と読まないこと。
 *     csgUnion の最中の Ctrl+C は **効かない** (process 実行なら #3417 の kill で殺せる)。
 *
 * ⚠ wasInterrupted() は「スレッド安全であること」が前提 (NullInterrupter.h に明記)。
 *   旗は pigBreak の atomic なのでそのまま満たす。
 * ⚠ 同ヘッダは「頻繁に呼びすぎないこと」とも言っている。呼ぶ頻度を決めるのは
 *   OpenVDB 側なのでこちらは何もしないが、**自前のループ (vd_measure) では**
 *   毎ボクセルではなく間引いて見ること。
 */
#include	"pig/c++/pigBreak.h"
#include	<openvdb/util/NullInterrupter.h>

class vdBreakInterrupter : public openvdb::util::NullInterrupter {
public:
	vdBreakInterrupter(const pigBreak *b) : b_(b) {}
	/* ★ 基底の virtual をそのまま上書きする。start()/end() は既定 (何もしない) でよい。 */
	bool wasInterrupted(int /*percent*/ = -1) override
	{ return ( b_ != 0 && b_->cancelled() ) ? true : false; }
private:
	const pigBreak *b_;
};

/* 算法へ渡す参照を作る入れ物。brk==0 なら素の NullInterrupter を貸す
 * (= 従来と同じ挙動。分岐を呼び出し側に書かせない)。 */
class vdBreakScope {
public:
	vdBreakScope(const pigBreak *b) : impl_(b) {}
	openvdb::util::NullInterrupter& ref() { return impl_; }
	openvdb::util::NullInterrupter* ptr() { return &impl_; }
private:
	vdBreakInterrupter impl_;
};

/* 自前ループ用: n 回に 1 回だけ旗を見る間引きカウンタ。
 * ★ 毎周見ても atomic の relaxed load は安いが、**分岐予測とキャッシュ行の共有**が
 *   内側ループに乗るのは避けたい。計測ループは活性ボクセル数ぶん回るので、
 *   1024 に 1 回で「数 ms 以内に気づく」に足りる。 */
class vdBreakPoll {
public:
	vdBreakPoll(const pigBreak *b, unsigned every = 1024) : b_(b), every_(every), n_(0) {}
	int cancelled()
	{
		if ( b_ == 0 ) return 0;
		if ( ++n_ < every_ ) return 0;
		n_ = 0;
		return b_->cancelled();
	}
private:
	const pigBreak *b_;
	unsigned	every_;
	unsigned	n_;
};

#endif /* VD_BREAK_H */

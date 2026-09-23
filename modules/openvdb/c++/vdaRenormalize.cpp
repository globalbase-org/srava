/*
 * vdaRenormalize — renormalize(v) の計算本体 (#3434 P2)。
 *
 * ★ なぜ要るか:
 *   ブールの結果 (max/min の合成) は **真の符号付き距離場ではなくなる** (|∇φ| = 1 が崩れる)。
 *   形そのものは正しいのだが、|∇φ| = 1 を仮定する tools::levelSetVolume が**偏る**。
 *   等値面を取り出して測れば正しい値になる。つまり**場は合っていて測り方が偏る**。
 *   ★ intersection (max(a,b)) は偏らない — 直接 voxelize した箱と bit 一致する。
 *     difference (max(a,-b)) の反転だけが場を崩す、という非対称。
 *
 * 等値面を抜き出して距離場を**作り直す**ので、|∇φ| = 1 が回復する。
 * ★ #3491 (2026-09-06): tools::levelSetRebuild そのものは使わない。中身の
 *   「メッシュ → 距離場」が **内部空洞を埋める**ので、そこだけ vd_mesh_to_levelset へ差し替える。
 * offset (距離場に定数を足す) でも帯が足りなくなるので、そこでも同じ関数が要る。
 *
 * ★ **明示 op にしてある** (ブールの内部で自動的に呼ばない)。作り直しは等値面抽出 + 再構築で
 *   ブール本体より重く、連鎖の途中で毎回払うのは無駄だから。必要なところ (測る直前・帯を
 *   広げたい時) で呼ぶ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	<stdio.h>
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"ts2/c++/stdString.h"
#include	<string>
#include	"_ts2/c++/vdaRenormalize_.h"

#include	<vector>   /* ★ #3491: volumeToMesh の受け皿 */

CLASS_TINYSTATE(vd/c++/vdaRenormalize,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaRenormalize_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<vdGrid>	out;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class ptsObject;
class pigData;
class stdString;
class vdGrid;
TS_END_INTERFACE

#endif


vdaRenormalize_::vdaRenormalize_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaRenormalize_::compute()
{
	/* ★ #3441: op 内並列 (TBB) は **op あたり**の予算で走らせる。予算未指定なら素通し。
	 *   ⚠ 包み忘れるとその op だけ無制限になるので、compute() 単位で一律に包む。 */
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("renormalize", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<vdGrid> in = ( na > 0 ) ? sPtr<vdGrid>::d_cast((*args)[0]) : sPtr<vdGrid>();
	if ( ! in.is_notNull() || ! in->has_grid() ) {
		result = vda_err(thNEW(stdString,("renormalize: needs an openvdb grid")));
		return;
	}
	/* 第 2 引数 = 帯の半幅 (voxel 単位・省略時は OpenVDB の既定 3)。offset を大きく取るときに
	 * 広げる。0 以下は既定扱い。 */
	double hw = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	/* ★ #3545 段 5: 実体 (volumeToMesh → vd_mesh_to_levelset の往復) は **幾何 lib 側** へ移した。
	 *   ⇒ この TU は OpenVDB のヘッダを引かない。
	 *   ⚠ 中断の判定は従来どおりここ — meshToVolume は中断されると **途中までの格子**を返す
	 *     ことがある (null とは限らない) ので、null でなくても旗を見る (#3498)。
	 *   ⚠ 空洞を保つ理由と volumeToMesh に中断点が無い件は vdGrid::op_renormalize の注記。 */
	const char *vwhy = 0;
	out = in->op_renormalize(hw, &brk_, &vwhy);
	if ( (result = vd_abort_err(brk_, "renormalize")) != thNULL ) return;
	if ( ! out.is_notNull() ) {
		result = vda_err(thNEW(stdString,( vwhy ? vwhy : "renormalize: failed" )));
		return;
	}
	out->set_normalized(true);   /* 作り直した = |grad| = 1 が回復した */
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}

sPtr<pigData>
vdaRenormalize_::get_result()
{
	return ( result != thNULL ) ? result : out;
}

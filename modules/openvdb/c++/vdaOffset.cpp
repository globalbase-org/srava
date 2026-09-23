/*
 * vdaOffset — offset(v, d) の計算本体 (#3434 P2)。d>0 で膨張・d<0 で収縮 (srava の規約)。
 *
 * ★ ボリューム表現が「得意」とされる操作。メッシュ系の 3D offset は **半径 d の球との
 *   Minkowski 和** (nef.so・Nef + 凸分解) で重いのに対し、距離場なら等値面を動かすだけ。
 *
 * ★★ ただし「距離場に定数を足すだけ」は **狭帯域では成り立たない**。定数を足すと零等値面が
 *   帯の外へ出てしまうため、OpenVDB の LevelSetFilter::offset は
 *     CFL (= 0.5 voxel) 刻みで動かし、**各ステップで帯を追従させる (track)**
 *   という反復になっている (LevelSetFilter.h:357)。よって計算量は |d| / (0.5*dx) に比例し、
 *   **オフセット量をボクセル間隔で割った回数**だけ回る。
 *   「定数を足すだけ」が本当に成り立つのは帯を持たない密な距離場のときだけ、というのが実装の
 *   実態で、これは #3434 の測定項目 (offset のメッシュ系との比較) の前提として重要。
 *
 * 符号: OpenVDB は phi>0 が外側なので、phi に**足す**と等値面は内側へ動く (収縮)。
 *   srava は d>0 が膨張なので、渡すときに反転する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"ts2/c++/stdString.h"
#include	<string>
#include	"_ts2/c++/vdaOffset_.h"


CLASS_TINYSTATE(vd/c++/vdaOffset,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaOffset_(
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


vdaOffset_::vdaOffset_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaOffset_::compute()
{
	/* ★ #3441: op 内並列 (TBB) は **op あたり**の予算で走らせる。予算未指定なら素通し。
	 *   ⚠ 包み忘れるとその op だけ無制限になるので、compute() 単位で一律に包む。 */
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("offset", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<vdGrid> in = ( na > 0 ) ? sPtr<vdGrid>::d_cast((*args)[0]) : sPtr<vdGrid>();
	if ( ! in.is_notNull() || ! in->has_grid() ) {
		result = vda_err(thNEW(stdString,("offset: needs an openvdb grid")));
		return;
	}
	double d = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	/* 第 3 引数 (subdiv) は**無視する**。メッシュ系の 3D offset が近似球との Minkowski 和で
	 * 実装されているためのパラメータで、距離場には近似球が無い (等値面を動かすだけ)。
	 * ★ #3474 続き (2026-09-05): 記述子で nreq=2 と申告してあるので **省略してよい**
	 * (以前はパーサが常に 3 引数へ正規化していた)。渡されても使わないのが正しい。 */

	/* ★ #3545 段 5: 実体 (deepCopy + LevelSetFilter::offset) は **幾何 lib 側** へ移した。
	 *   ⇒ この TU は OpenVDB のヘッダを引かない。⚠ 中断の判定は従来どおりここで行う —
	 *     中断されると *途中までしか動いていない格子* が返るので、旗を見て捨てる (#3498)。 */
	const char *vwhy = 0;
	out = in->op_offset(d, &brk_, &vwhy);
	if ( (result = vd_abort_err(brk_, "offset")) != thNULL ) return;
	if ( ! out.is_notNull() ) {
		result = vda_err(thNEW(stdString,( vwhy ? vwhy : "offset: failed" )));
		return;
	}
	/* ★★ 印は **false**。「LevelSetFilter::offset は各ステップで track() するので距離場は
	 *   保たれる」と書いていたが、**実測で否定された** (2026-08-20・#3440 の offset 厳密評価)。
	 *   Steiner の公式で真値を出して box(2,2,2) を d=0.1 でオフセットし、dx を振ると:
	 *
	 *   ★ **そのまま測ると dx を細かくしても誤差が減らない**のに対し、作り直してから測ると収束する。
	 *   つまり offset の結果は levelSetVolume が要求する精度の距離場になっていない。
	 *   **形は正しい** (isosurface でメッシュにすると収束する) ので、ずれているのは測り方だけ。
	 *   ★ この種の「収束しない誤差」は **真値を持たない比較では気づけない** (カーネルどうしが
	 *   同じ向きに外していると一致してしまう)。 */
	out->set_normalized(false);
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}

sPtr<pigData>
vdaOffset_::get_result()
{
	return ( result != thNULL ) ? result : out;
}

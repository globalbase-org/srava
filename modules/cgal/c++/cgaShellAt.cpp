/*
 * cgaShellAt — shell_at(m, [x,y,z]) — 点に **いちばん近い殻** を取り出す op (#3527)。
 *
 * ★★ なぜ索引 (shell) と 2 通り要るのか — **番号には指す先が無い**から。
 *   殻の番号は連結成分の走査順で、「列挙のため」のもの。⇒ モデルの書き方を変えると
 *   殻の集合そのものが変わるので、番号は当然別の殻を指す。*順序規約をどう決めても直らない*。
 *   ⇒ 書き換えても同じ殻を指し続けたいなら **位置で指すしかない**。
 *   ★ occt の @face@ / @face_at@ が同じ理由で 2 通りある (#3518 の測定に基づく判断)。
 *
 * ★ 距離は **厳密** (EPECK)。⇒ 「同距離」の判定も厳密に効く。
 * ⚠ 同距離の殻が 2 つ以上あれば **明示エラー**。黙って片方を選ぶと「同じ式に 2 通りの値」に
 *   なり、#3516 / #3518-1 で潰してきた穴と同じものを作る。
 *   ★ 中空の箱の中心はまさにそれ (外殻と空洞が対称) — 検査がその場合を見ている。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"common/affine.h"	/* 点 [x,y,z] の解釈 (拒否の文言もここ) */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaShellAt_.h"
#include	<stdio.h>

CLASS_TINYSTATE(cg/c++/cgaShellAt,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaShellAt_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠ get_result は **public 側**に置く。protected だと codegen が外側クラスへの
	 *   転送を作らず、値は作れているのに format 'TEXT' で保存される (2026-09-17 に踏んだ)。 */
	virtual sPtr<pigData>	get_result();

protected:
	virtual void		compute();
	sPtr<cgMesh>		mesh;
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
class cgMesh;
TS_END_INTERFACE

#endif


cgaShellAt_::cgaShellAt_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaShellAt_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh3D> in = ( na > 0 ) ? sPtr<cgMesh3D>::d_cast((*args)[0]) : sPtr<cgMesh3D>();
	if ( ! in.is_notNull() ) {
		/* ⚠ 2D は殻を持たない。★ ただし **通常経路ではここへ来ない** — sig が
		 *   (cg-mesh3d) しか受けないので、ルータが先に
		 *   "no module can execute op 'shell' on input types (cg-cross2d)" で弾く。
		 *   ⇒ ここは *sig を書き換えた人* への保険。検査は sig 側の文言を見ている。 */
		result = cga_err(thNEW(stdString,("shell_at: needs a 3D mesh (a 2D region has no shells)")));
		return;
	}
	double p[3];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::point3(( na > 1 ) ? (*args)[1] : sPtr<pigData>(),
	                            "shell_at", p, &why, buf, (int)sizeof buf) ) {
		result = cga_err(thNEW(stdString,(why)));
		return;
	}
	why = 0;
	mesh = sPtr<cgMesh>::d_cast(in->op_shell_at(p, &why));
	if ( ! mesh.is_notNull() ) {
		char b[320];
		::snprintf(b, sizeof b, "shell_at: %s", why ? why : "could not extract the shell");
		result = cga_err(thNEW(stdString,(b)));
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残すので result 優先。 */
sPtr<pigData>
cgaShellAt_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}

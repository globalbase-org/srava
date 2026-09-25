/*
 * ocaTube — occt の tube(path[, opts]) (#3470)。path = [[[x,y,z], r], …]。
 *
 * ★★★ #3593 (2026-09-23): 実体は @ocShape::tube_from_args@ へ括り出した
 *   (@tube@ / @tube_ruled@ の 2 つの op で共有・@ruled@ フラグだけが違う)。
 *   @loft@ / @loft_ruled@ とまったく同じ立て付け。⇒ このファイルは薄いラッパ。
 *
 * ★★ **他カーネルの tube とは形が違う** (ひさ判断 2026-09-01)。同じ op 名だが同じ形ではない:
 *
 *     cgal / manifold   折れ線の背骨 + segs 角形近似の断面   (共通ヘッダ src/h/common/tube.h)
 *     occt (これ)       点を通る **C2 の B-spline** の背骨 + **厳密な円**の断面
 *
 *   起票時 (#3470) は「occt の tube がスプラインだと同じ入力から違う形が出るので折れ線のまま
 *   にする」方針だったが、**その懸念を承知のうえで形が変わる側を採った**。理由は、occt を使う
 *   価値がまさに「解析曲面として持てる」ことにあるため:
 *     - offset が厳密になる (occt の offset は Steiner の公式と 9〜10 桁一致する実績)
 *     - fillet / chamfer が効く
 *     - STEP に実物の曲面が載る
 *   ⇒ **cg/mf の tube と厳密に一致させることはできない**。カーネル一致の表には入れない
 *     (#3461 で occt の sphere を入れられなかったのと同じ形)。
 *   ⇒ ★★ #3593 で **occt にも @tube_ruled@ が入った** (折れ線の背骨 + 厳密な円の断面)。
 *     角を尖らせた管が occt のまま欲しければそちらを使う。⚠ ただし断面は厳密な円なので
 *     **cg/mf の tube_ruled とも一致しない** (背骨は同じ折れ線・断面の近似度が違う)。
 *     完全に cg/mf と同じ形が要るときだけ "cgal"::tube_ruled(…) と指名する (#3467)。
 *
 * ★ segs は **無視する**。背骨も断面も滑らかなので分割数に意味が無い
 *   (occt の sphere が seg を無視しているのと同じ扱い)。互換のため受け取るだけ。
 *
 * ★ 自己交差する背骨は **明示エラー**。既存の tube は自己交差を許容する仕様 (とぐろを値として
 *   作れる) だが、OCCT の MakePipeShell は失敗するか不正な B-rep を作るので、黙って壊れた
 *   shape を返さない。★ カーネルによって挙動が変わる点なのでエラー文にもそう書く。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaTube_.h"

#include	<string>

CLASS_TINYSTATE(oc/c++/ocaTube,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaTube_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠ get_result は **public** に書く。protected に書くと外側クラスへの転送 thunk が
	 *   生成されず、基底の get_result が使われて **値 (TEXT) が返る** (実際に踏んだ)。
	 *   ocaTorus 等の既存 op と同じ位置に揃えること。 */
	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocShape>	out;
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
class ocShape;
TS_END_INTERFACE

#endif

ocaTube_::ocaTube_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

void
ocaTube_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	const char *msg = 0;
	char why[512];
	why[0] = '\0';
	out = ocShape::tube_from_args(args, false /* spline spine */, &msg, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		char b[700];
		::snprintf(b, sizeof b, "tube: %s%s%s", msg ? msg : "failed",
		           why[0] ? " — " : "", why[0] ? why : "");
		result = oca_err(thNEW(stdString,(b)));
	}
}

sPtr<pigData>
ocaTube_::get_result()
{
	return ( result != thNULL ) ? result : out;
}

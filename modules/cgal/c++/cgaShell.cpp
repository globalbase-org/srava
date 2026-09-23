/*
 * cgaShell — shell(m, i) — **i 番目の殻** (面の連結成分) を取り出す op (#3527)。
 *
 * ★★ part (塊) との違いは 1 点だけ — **空洞を断らない**。
 *     part  … 値を *分割する* 片。どの空洞がどの塊のものか (入れ子) が要る
 *             ⇒ 空洞が在ると cgal では明示エラー (nef に回す)
 *     shell … **面の連結成分そのもの**。入れ子を知らなくても取り出せる
 *   ⇒ 中空の箱は 塊 1 個 ・ 殻 2 枚 (外側の箱 + 空洞の境界)。
 *
 * ★★★ 向きは **そのまま返す** (案 A・ひさ裁定 2026-09-16)。
 *   空洞の殻は法線が内を向いているので @volume@ が **負**で返る。
 *   ⇒ **符号がそのまま「外殻か空洞か」の判別子**になる。
 *   ⚠ 向きを揃えて空洞を「立体として」返す案 (B) だと、その符号が消えるので
 *     「これは空洞か?」を別に訊く手段が要る。★ さらに A なら
 *     **Σ 符号つき体積 == 全体の体積** が成り立つ (中空の箱 = 外殻 − 空洞)。
 *
 * ⚠ 殻は **「値を分割する片」ではない** ので、#3527 の検定「片の測度の和 == 全体の測度」は
 *   *符号つきで* しか成り立たない。規約にはそう書くこと。
 *
 * ⚠ 索引は **実装依存** (連結成分の走査順)。⇒ 版を跨いで同じ殻を指す保証は無い。
 *   ★ そもそも殻の番号は *指し示す* ためのものではなく **列挙のため** のものなので、
 *     要るのは「全部出る・重複しない」であって「番号の意味」ではない (#3527 の整理)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaShell_.h"
#include	<stdio.h>

CLASS_TINYSTATE(cg/c++/cgaShell,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaShell_(
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


cgaShell_::cgaShell_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaShell_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int idx = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;
	sPtr<cgMesh3D> in = ( na > 0 ) ? sPtr<cgMesh3D>::d_cast((*args)[0]) : sPtr<cgMesh3D>();
	if ( ! in.is_notNull() ) {
		/* ⚠ 2D は殻を持たない。★ ただし **通常経路ではここへ来ない** — sig が
		 *   (cg-mesh3d) しか受けないので、ルータが先に
		 *   "no module can execute op 'shell' on input types (cg-cross2d)" で弾く。
		 *   ⇒ ここは *sig を書き換えた人* への保険。検査は sig 側の文言を見ている。 */
		result = cga_err(thNEW(stdString,("shell: needs a 3D mesh (a 2D region has no shells)")));
		return;
	}
	const char *why = 0;
	mesh = sPtr<cgMesh>::d_cast(in->op_shell(idx, &why));
	if ( ! mesh.is_notNull() ) {
		char b[288];
		::snprintf(b, sizeof b, "shell: %s", why ? why : "could not extract the shell");
		result = cga_err(thNEW(stdString,(b)));
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残すので result 優先。 */
sPtr<pigData>
cgaShell_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}

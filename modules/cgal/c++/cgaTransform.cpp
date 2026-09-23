/*
 * cgaTransform — transform(mesh, matrix) の計算本体(ptsCalcBody 派生)= 低レベル一般アフィン変換。
 * args=[mesh(cgMesh), matrix(array)]。matrix は行優先の 12 要素(3x4 アフィン m00..m23)または
 * 16 要素(4x4。最終行 0,0,0,1 は無視)。各要素 double を K::FT に格納(EPECK 座標のまま近似)。
 * 反射(det<0)は cga_apply_affine が向き反転で補正。要素数不正は result にエラーを立て A_ERROR。
 * 高レベルの translate/rotate/mirror はこの一般変換の特例。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgAffineDemote.h"   /* ★ #3554 段4/5: 平面を保つ変換は 2D のまま返す */
#include	"common/affine.h"   /* アフィン変換の共通規約 (#3486) */
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaTransform_.h"

CLASS_TINYSTATE(cg/c++/cgaTransform,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaTransform_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;
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


cgaTransform_::cgaTransform_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaTransform_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	sPtr<pigData> arg = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();

	/* ★ 引数の解釈と行列の組み立ては **common/affine.h** (カーネル非依存・7 モジュール共通)。
	 *   受け付ける書き方だけでなく **拒否の理由** もそこに集約してある (#3486)。
	 *   理由の受け皿 buf は呼び手が持つ (モジュール側に static を置かない)。 */
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_transform(arg, e, &why, buf, (int)sizeof buf) ) {
		result = cga_err(thNEW(stdString,(why)));
		mesh = thNEW(cgMesh3D,());
		return;
	}
	/* ★★ #3526 (2026-09-13): **2D は枠 (平面) を持つようになった**ので、面外へ出す変換は
	 *   もう断らない — cgMesh2D::apply_affine が z 成分を **枠へ渡す**。
	 *   ⚠ #3518 の但し書きとして 2940662 で「黙って射影する」のを明示エラーにしたが、
	 *     それは *置き場所を持てなかった* からで、持てるなら断る理由は無い。
	 *   ⚠ 線形部が退化して平面が線に潰れる場合だけ null が返る ⇒ 明示エラーにする。 */
	mesh = ( in.is_notNull() ) ? in->apply_affine(e) : sPtr<cgMesh>();
	/* ★★ #3554 段4 (2026-09-19): **xy 平面に帰着する変換は 2D のまま返す**。
	 *   @apply_affine@ は 規約① にしたがって常に @set_placed(1)@ する (= face3d) が、
	 *   *入力が cross2d (枠が既定) で、行列が z=0 平面を平面へ写す*なら、結果は幾何としても
	 *   2D のままなので @placed_@ を下ろす。
	 *   ★★ 判定は **routing が @transform#xy@ の行を選ぶときと同じ述語** (keeps_z_plane)。
	 *     ⇒ *行と計算結果が構造的に一致する* — 行が #xy なら必ずここも真になる。
	 *     ⚠ ここで別の判定を書くと「sig は cross2d と言っているのに face3d を返す」が起きうる。
	 *   ⚠ 入力が face3d (枠が非既定) のときは下ろさない — 枠が任意なので行列だけでは決まらず、
	 *     そちらの降格は 規約② (cast) のまま。 */
	cg_demote_if_flat(in, mesh, e);
	if ( in.is_notNull() && ! mesh.is_notNull() ) {
		result = cga_err(thNEW(stdString,(
		    "transform: this transform flattens the 2D region onto a line (its plane collapses)")));
		mesh = thNEW(cgMesh3D,());
		return;
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaTransform_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}

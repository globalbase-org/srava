#ifndef PT_AFFINE_H
#define PT_AFFINE_H
/*
 * ptAffine — 点群の transform 一族 (#3578) で **routing の行と計算本体が同じ判定を使う**
 *            ための 1 か所。
 *
 * ---- なぜ 1 か所なのか ----
 * ⚠⚠ routing (OPS 行のマッチ関数) と計算本体で判定が割れると、*値は正しいのに型だけ違う*
 *   という一番見つけにくい壊れ方をする (#3554 段5a で実際に踏んだ形)。sig が
 *   @(pt-cloud2d)->pt-cloud2d@ と言っているのに 3 次元の点群が返る、等。
 *   ⇒ 「どの op がどの述語を使うか」を **この表だけ**が持ち、両側がここを呼ぶ。
 *
 * ---- op ごとに述語が違う理由 (pigOpMatch.h の説明の要約) ----
 * マッチ関数は **引数を 1 個ずつしか見られない** ので、*行列を決める値* を 1 個だけ見て
 * 「z=0 平面を平面へ写すか」を答えられる形になっている必要がある。
 *   translate  … ベクトルの z 成分が 0 か          (第 2 引数)
 *   scale      … 対角しか作らないので構造上いつも真 (第 2 引数)
 *   mirror     … 法線が x/y/z 軸なら平面を保つ      (第 2 引数)
 *   transform  … 行列の z 行が (0,0,*,0) か         (第 2 引数)
 *   rotate     … ⚠⚠ **軸が z か** だけ (第 2 引数)。本当に知りたいのは「その回転が平面を
 *                保つか」だが、それは *軸と角度の両方* で決まる。⇒ 保守的に軸だけを見る。
 *                @rotate(p, "x", 180)@ は平面を保つが **3D になる** (救えない・cgal / manifold /
 *                occt の 2D と同じ扱い)。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpMatch.h"   /* 述語の本体 (7 カーネル共通) */
#include	"common/affine.h"        /* 行列の組み立て (7 カーネル共通) */

/* transform 一族のどの op か。★ 述語と行列の対応を決めるのはこの enum と下の 2 つの switch だけ。 */
enum ptAffineOp {
	PT_AFF_TRANSLATE = 0,
	PT_AFF_ROTATE,
	PT_AFF_SCALE,
	PT_AFF_MIRROR,
	PT_AFF_TRANSFORM
};

/* 行列を決める値 (どの op でも **第 2 引数**) が z=0 平面を平面へ写すか。
 *   1 = 保つ (2D のまま) / 0 = 面外へ出る・読めない・特異。
 * ⚠ 純粋 (値を読むだけ)。routing から候補ごとに何度でも呼ばれる。 */
inline int
pt_affine_keeps_xy(int which, sPtr<pigData> arg)
{
	switch ( which ) {
	case PT_AFF_TRANSLATE: return pig_val_translate_keeps_xy(arg);
	case PT_AFF_ROTATE:    return pig_val_axis_is_z(arg);          /* ⚠ 角度は見えない */
	case PT_AFF_SCALE:     return pig_val_scale_keeps_xy(arg);
	case PT_AFF_MIRROR:    return pig_val_mirror_keeps_xy(arg);
	case PT_AFF_TRANSFORM: return pig_val_keeps_xy(arg);
	}
	return 0;
}

/* 結果の次元。**2D 入力 × 平面を保つ変換** のときだけ 2D、それ以外は 3D。
 * ★ 3D 入力は何をしても 3D (平面に潰れても型は変わらない — 型は「空間に在る」ことしか言わない)。 */
inline int
pt_affine_out_dim(int inDim, int which, sPtr<pigData> arg)
{
	return ( inDim == 2 && pt_affine_keeps_xy(which, arg) ) ? 2 : 3;
}

/* 引数から行優先 3x4 を組む (deg は rotate だけが使う)。1 = 成功 / 0 = 失敗 (*why に理由)。
 * ★ 受け付ける書き方と **拒否の理由** は common/affine.h に集約済み (#3486)。ここは振り分けだけ。 */
inline int
pt_affine_matrix(int which, sPtr<pigData> arg, double deg, double e[12],
                 const char **why, char *buf, int bufsz)
{
	switch ( which ) {
	case PT_AFF_TRANSLATE: return srava_affine::matrix_translate(arg, e, why, buf, bufsz);
	case PT_AFF_ROTATE:    return srava_affine::matrix_rotate(arg, deg, e, why, buf, bufsz);
	case PT_AFF_SCALE:     return srava_affine::matrix_scale(arg, e, why, buf, bufsz);
	case PT_AFF_MIRROR:    return srava_affine::matrix_mirror(arg, e, why, buf, bufsz);
	case PT_AFF_TRANSFORM: return srava_affine::matrix_transform(arg, e, why, buf, bufsz);
	}
	::snprintf(buf, bufsz, "internal: unknown affine op %d", which);
	*why = buf;
	return 0;
}

#endif /* PT_AFFINE_H */

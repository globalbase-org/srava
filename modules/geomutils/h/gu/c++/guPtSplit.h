#ifndef ___gu_guPtSplit_h___
#define ___gu_guPtSplit_h___

/*
 * guPtSplit.h — 点群を立体 / 領域に対して分ける共通部 (#3579)。
 *
 * ★★ intersection と difference が **同じ 1 本**を呼ぶ。⚠ 写すと片方だけ直して黙って
 *   ずれる (extract_faces / tri_solid_angle を 1 本にしたのと同じ理由)。
 *
 * ★★★ 境界ちょうどの点は **第 3 の集合**。mode で選ぶ:
 *     mode  0 … 境界ちょうど   / -1 … 内側 (開) / +1 … 外側
 *     difference(A, M) は mode=+1 と **同じもの**。
 *   ⇒ この 3 つは **厳密な分割**なので nverts の和が入力と一致する:
 *        nverts(mode 0) + nverts(mode -1) + nverts(mode +1) == nverts(A)
 *   ★ 「閉集合が欲しい」人は union(mode 0, mode -1) と書く ⇒ 何を含めたかが式に出る。
 *   ⚠ 「境界は含む」と決め打たないこと — 整数格子の点群では **97% が境界に載る**
 *     (mac 実測: rand([0,0,0],[2,2,2],200,7) が 194/200)。決め打つと 97% が黙って倒れる。
 *
 * ★ 分類は 1 点につき 1 回 (op_classify_points が 1 周で決める)。mode はその後の **濾し**
 *   でしかない ⇒ 将来 sig に配列の出力型が入ったら、1 回の分類から 3 つ作れる形のまま使える。
 *
 * ⚠ 出力は **入力点群の部分集合**。点は 1 つも動かさず、**入力の順**を保つ (#3527 の格納順)。
 *   ★ 法線は持っている点だけ同じ並びで写す (捨てると往復で情報が落ちる)。
 */
#include	"gu/c++/guGeom.h"
#include	"pt/c++/ptCloud.h"
#include	<vector>

/* 返り: 分けた点群 / null なら why に理由。
 * ⚠ @dimMismatch@ (X > Y) は op 側が文言を作れるよう why で返す。 */
inline sPtr<ptCloud>
gu_pt_split(const sPtr<ptCloud>& in, const sPtr<guGeom>& geom, int mode, const char **why)
{
	*why = 0;
	if ( ! in.is_notNull() )   { *why = "the first argument must be a point cloud"; return sPtr<ptCloud>(); }
	if ( ! geom.is_notNull() ) { *why = "the second argument must be a mesh or a 2D region"; return sPtr<ptCloud>(); }
	if ( mode != 0 && mode != -1 && mode != 1 ) {
		*why = "mode must be 0 (exactly on the boundary), -1 (inside) or +1 (outside)";
		return sPtr<ptCloud>();
	}
	const int dim = in->dim();
	if ( dim != 2 && dim != 3 ) { *why = "the point cloud must be 2D or 3D"; return sPtr<ptCloud>(); }

	const std::vector<double>& xyz = in->xyz();
	const std::vector<double>& nrm = in->nrm();
	const int npt = (int)( xyz.size() / (size_t)dim );
	/* ★ 法線は「有るか無いか」の 2 択 (半端は作らない) — ptCloud.h の約束。 */
	const int hasN = ( (int)( nrm.size() / (size_t)dim ) == npt && npt > 0 ) ? 1 : 0;

	sPtr<ptCloud> out = thNEW(ptCloud,());
	out->set_dim(dim);
	if ( npt <= 0 ) return out;                     /* 空の点群は空のまま (エラーにしない) */

	std::vector<signed char> cls((size_t)npt, 1);
	if ( ! geom->op_classify_points(xyz.empty() ? 0 : &xyz[0], npt, dim, &cls[0], why) )
		return sPtr<ptCloud>();                     /* why は op_classify_points が書いた */

	for ( int i = 0 ; i < npt ; ++i ) {
		if ( (int)cls[(size_t)i] != mode ) continue;
		for ( int k = 0 ; k < dim ; ++k ) out->xyz().push_back(xyz[(size_t)i*dim + k]);
		if ( hasN )
			for ( int k = 0 ; k < dim ; ++k ) out->nrm().push_back(nrm[(size_t)i*dim + k]);
	}
	return out;
}

#endif /* ___gu_guPtSplit_h___ */

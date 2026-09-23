#ifndef ___vd_vdPtSplit_h___
#define ___vd_vdPtSplit_h___

/*
 * vdPtSplit.h — 点群を距離場に対して分ける共通部 (#3580)。
 *
 * ★★ geomutils の gu/c++/guPtSplit.h と **同じ形・同じ約束**。違うのは判定器だけ
 *   (あちらは巻き数 / point-in-polygon ・ こちらは **距離場の符号**)。
 *   ⚠ 濾しの規則をこちらで変えないこと — 変えると「同じ op がモジュールごとに別のことを
 *     答える」形になる。3 モジュールで s[1]/s[2] が一致することが #3581 の検定。
 *
 * ★★★ 境界ちょうどの点は **第 3 の集合**。mode で選ぶ:
 *     mode  0 … 境界の帯の中 / -1 … 内側 (開) / +1 … 外側
 *     difference(A, M) は mode=+1 と **同じもの**。
 *   ⇒ 厳密な分割: nverts(0) + nverts(-1) + nverts(+1) == nverts(A)
 *
 * ⚠ openvdb の境界は **0.75 ボクセルの帯** (#3491 と同じ幅)。他モジュールより厚い。
 *   これは欠陥ではなくこの表現の精度そのもの (#3575)。⇒ 厳密が要るなら occt か geomutils へ。
 *
 * ⚠ 出力は **入力点群の部分集合**。点は 1 つも動かさず、**入力の順**を保つ。
 */
#include	"vd/c++/vdGrid.h"
#include	"pt/c++/ptCloud.h"
#include	<vector>

/* 返り: 分けた点群 / null なら why に理由。 */
inline sPtr<ptCloud>
vd_pt_split(const sPtr<ptCloud>& in, const sPtr<vdGrid>& grid, int mode, const char **why)
{
	*why = 0;
	if ( ! in.is_notNull() )   { *why = "the first argument must be a point cloud"; return sPtr<ptCloud>(); }
	if ( ! grid.is_notNull() ) { *why = "the second argument must be a level set"; return sPtr<ptCloud>(); }
	if ( mode != 0 && mode != -1 && mode != 1 ) {
		*why = "mode must be 0 (on the boundary band), -1 (inside) or +1 (outside)";
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
	if ( ! grid->op_classify_points(xyz.empty() ? 0 : &xyz[0], npt, dim, &cls[0], why) )
		return sPtr<ptCloud>();

	for ( int i = 0 ; i < npt ; ++i ) {
		if ( (int)cls[(size_t)i] != mode ) continue;
		for ( int k = 0 ; k < dim ; ++k ) out->xyz().push_back(xyz[(size_t)i*dim + k]);
		if ( hasN )
			for ( int k = 0 ; k < dim ; ++k ) out->nrm().push_back(nrm[(size_t)i*dim + k]);
	}
	return out;
}

#endif /* ___vd_vdPtSplit_h___ */

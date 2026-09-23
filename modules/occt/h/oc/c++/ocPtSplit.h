#ifndef ___oc_ocPtSplit_h___
#define ___oc_ocPtSplit_h___

/*
 * ocPtSplit.h — 点群を B-rep に対して分ける共通部 (#3581)。
 *
 * ★★ geomutils の guPtSplit.h ・ openvdb の vdPtSplit.h と **同じ形・同じ約束**。
 *   違うのは判定器だけ (こちらは BRepClass3d_SolidClassifier / 面までの距離)。
 *   ⚠ 濾しの規則をこちらで変えないこと — 変えると「同じ op がモジュールごとに別のことを
 *     答える」形になる。3 モジュールで s[1]/s[2] が一致することが #3581 の検定。
 *
 * ★★★ 境界ちょうどの点は **第 3 の集合**。mode: 0=境界 / -1=内側(開) / +1=外側。
 *   difference(A, M) は mode=+1 と同じもの。厳密な分割なので
 *   nverts(0) + nverts(-1) + nverts(+1) == nverts(A)。
 *
 * ★ occt は **解析曲面のまま**判定するので、球や円柱でメッシュ近似の誤差が無い
 *   = 3 モジュールの中で **一番正確**。境界の厚みは Precision::Confusion。
 *
 * ⚠ 出力は **入力点群の部分集合**。点は 1 つも動かさず、**入力の順**を保つ。
 */
#include	"oc/c++/ocShape.h"
#include	"pt/c++/ptCloud.h"
#include	<vector>

/* ocShape (3D) と ocFace2D (2D) のどちらでも受けられる。
 * ★ テンプレートにするのは **2 クラスに共通の基底が無い**ため (op_classify_points は
 *   それぞれのクラスに在り、仮想ではない)。⚠ 中身を写さないことが目的なので、
 *   ここが 1 本であればよい。 */
template <class GEOM>
inline sPtr<ptCloud>
oc_pt_split(const sPtr<ptCloud>& in, const sPtr<GEOM>& geom, int mode,
            const char **why, char *errbuf, int errbufsz, const pigBreak *brk)
{
	*why = 0;
	if ( ! in.is_notNull() )   { *why = "the first argument must be a point cloud"; return sPtr<ptCloud>(); }
	if ( ! geom.is_notNull() ) { *why = "the second argument must be a shape"; return sPtr<ptCloud>(); }
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
	if ( ! geom->op_classify_points(xyz.empty() ? 0 : &xyz[0], npt, dim, &cls[0], why,
	                                errbuf, errbufsz, brk) )
		return sPtr<ptCloud>();

	for ( int i = 0 ; i < npt ; ++i ) {
		if ( (int)cls[(size_t)i] != mode ) continue;
		for ( int k = 0 ; k < dim ; ++k ) out->xyz().push_back(xyz[(size_t)i*dim + k]);
		if ( hasN )
			for ( int k = 0 ; k < dim ; ++k ) out->nrm().push_back(nrm[(size_t)i*dim + k]);
	}
	return out;
}

#endif /* ___oc_ocPtSplit_h___ */

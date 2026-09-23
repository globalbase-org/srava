/*
 * vdGridVdb.h — ★★ #3545 段 5: **OpenVDB を include する唯一のヘッダ**。
 *
 * ⚠⚠ これを op の .cpp から include してはいけない。読んでよいのは libsrava_vd を作る
 *   .cpp (vdGrid.cpp / vdCacheCodec.cpp / wire の reader・writer) だけ。
 *   ★ 柵は test/srava_op_cgal_free.sh の「上流ごとの柵」が数えている (許容を超えたら赤)。
 *
 * ---- なぜ分けるか (実測・2026-09-18) ----
 * @vdGrid.h@ が @<openvdb/openvdb.h>@ を引いていたため、**それを include しただけ**の op TU に
 * 上流の可変大域が emit されていた:
 *
 *     openvdb::v12_1::math::Mat3<double>::identity()::sIdentity   (型 u = unique global)
 *     openvdb::v12_1::math::Mat4<double>::identity()::sIdentity
 *
 *     ⇒ op TU **30 / 31 本**が保持 (2026-09-18 の実測)
 *
 * ★ 中身は **単位行列の定数**なので、複製されても値は同じで *壊れはしない*。
 *   ⚠ ただし「害が無いから放っておく」と、次に *状態を持つ* 大域が混ざったとき区別が付かない。
 *   ⇒ CGAL / nef と同じ形 (nfMeshCgal.h) に揃えて **0 にする**。
 *
 * ★ nef との違い: CGAL は header-only なので分離の効果がサイズにも大きく出た (145MB → 15MB) が、
 *   OpenVDB は **コンパイル済みの .so** があるので、ここで減るのは主に *可変大域の複製* と
 *   テンプレート実体化の一部。⇒ 動機は正しさ側に置く (#3539 の原則)。
 */
#ifndef ___vdGridVdb_H___
#define ___vdGridVdb_H___

#include	"vd/c++/vdGrid.h"
#include	<openvdb/openvdb.h>

/* ★ OpenVDB の実体を抱える不透明な箱 (nfNefBox と同じ役)。
 *   ⚠ vdGrid.h 側は @class vdGridBox;@ の前方宣言だけを持つ。 */
class vdGridBox {
public:
	vdGridBox() {}
	openvdb::FloatGrid::Ptr g;
};

/* ---- OpenVDB 型を取る API。公開ヘッダに置けないのでここに自由関数として置く ---- */
inline openvdb::FloatGrid::Ptr&       vd_grid(vdGrid &v)       { return v.box().g; }
inline const openvdb::FloatGrid::Ptr& vd_grid(const vdGrid &v) { return v.box().g; }

#endif

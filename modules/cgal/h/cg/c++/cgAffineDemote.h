#ifndef ___cgAffineDemote_H___
#define ___cgAffineDemote_H___
/*
 * cgAffineDemote — ★★ #3554 段4/5: **xy 平面に帰着するアフィン変換は 2D のまま返す**。
 *
 * ---- なぜ要るか ----
 * @cgMesh2D::apply_affine@ は 規約① (transform 系は常に face3d) にしたがって必ず
 * @set_placed(1)@ する。だが AK_MATCH で *行列が平面を保つか* を routing が見られるように
 * なったので、その場合は @placed_@ を下ろして @cg-cross2d@ のまま返す。
 *
 * ---- ★★ 判定は routing と **同じ述語** でなければならない ----
 * routing 側 (マッチ関数) は @srava_affine::keeps_z_plane@ で行を選ぶ。ここで別の判定を
 * 書くと「**sig は cross2d と言っているのに face3d を返す**」が起きる (下流の型スタンプが嘘になる)。
 * ⇒ *同じ関数を呼ぶ*。それが保証になる。
 *
 * ⚠ 下ろすのは **入力が cross2d のときだけ** — face3d は枠が任意なので行列だけでは決まらない
 *   (降格は 規約② = cast が幾何を見る、のまま)。
 */
#include "cg/c++/cgMesh.h"
#include "common/affine.h"

/* in を e で変換した結果が out。in が 2D かつ枠が既定で、e が z=0 平面を平面へ写すなら
 * out の placed_ を下ろす (= cg-cross2d を名乗る)。それ以外は何もしない。 */
inline void
cg_demote_if_flat(sPtr<cgMesh> in, sPtr<cgMesh> out, const double e[12])
{
	sPtr<cgMesh2D> in2 = sPtr<cgMesh2D>::d_cast(in);
	if ( ! out.is_notNull() || ! in2.is_notNull() || in2->is_placed() )
		return;
	sPtr<cgMesh2D> out2 = sPtr<cgMesh2D>::d_cast(out);
	if ( ! out2.is_notNull() )
		return;
	const char *why = 0;
	char buf[256];
	if ( srava_affine::keeps_z_plane(e, "affine", &why, buf, (int)sizeof buf) )
		out2->set_placed(0);
}

#endif

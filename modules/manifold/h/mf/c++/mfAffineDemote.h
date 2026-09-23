#ifndef ___mfAffineDemote_H___
#define ___mfAffineDemote_H___
/*
 * mfAffineDemote — ★★ #3554 段5: **xy 平面に帰着するアフィン変換は 2D のまま返す** (manifold 版)。
 *   cgal の cg/c++/cgAffineDemote.h と同型。設計の理屈はそちらの冒頭コメントが本文。
 *
 * ★★ 判定は **routing (マッチ関数) と同じ述語**でなければならない。別の判定を書くと
 *   「sig は face3d と言っているのに cross2d が返る」= 型スタンプが嘘になる
 *   (2026-09-19 に cgal の rotate で実際に踏んだ)。
 * ⚠ 下ろすのは **入力が cross2d のときだけ** — face3d は枠が任意なので行列だけでは決まらない。
 */
#include "mf/c++/mfMesh.h"
#include "common/affine.h"

inline void
mf_demote_if_flat(sPtr<mfGeom> in, sPtr<mfGeom> out, const double e[12])
{
	sPtr<mfCross> in2 = sPtr<mfCross>::d_cast(in);
	if ( ! out.is_notNull() || ! in2.is_notNull() || in2->is_placed() )
		return;
	sPtr<mfCross> out2 = sPtr<mfCross>::d_cast(out);
	if ( ! out2.is_notNull() )
		return;
	const char *why = 0;
	char buf[256];
	if ( srava_affine::keeps_z_plane(e, "affine", &why, buf, (int)sizeof buf) )
		out2->set_placed(0);
}

#endif

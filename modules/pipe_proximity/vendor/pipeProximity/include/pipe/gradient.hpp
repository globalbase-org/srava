#pragma once
// 接触の gap に対する設計パラメータのヤコビアン（包絡定理版）
//   d(gap)/d(設計点) = (w_A - w_B) * n̂    （接触パラメータ tA,tB,φ,ψ は凍結）
//   半径感度 d(gap)/drA = n̂·d̂A ≈ -1,  d(gap)/drB = -n̂·d̂B ≈ -1
//
// 注: 半径は弧長 s の関数なので、設計点を動かすと弧長経由でも r がわずかに変わる
//     （d r/ds 項）。その寄与は dr/ds が小さければ無視可。ここでは幾何項に集中し、
//     弧長→半径カップリングは含めない（必要なら別途加算）。
#include "pipe/vec3.hpp"
#include "pipe/bezier.hpp"
#include "pipe/radius.hpp"
#include "pipe/proximity.hpp"
#include <vector>

namespace pipe {

struct DOFGrad { int dof; Vec3 grad; };   // dof = 設計点 index、grad = ∂gap/∂(その点)

struct ContactJacobian {
    std::vector<DOFGrad> points;   // スパース（segA/segB に効く設計点のみ）
    double dGap_drA;               // ∂gap/∂rA ≈ -1
    double dGap_drB;               // ∂gap/∂rB ≈ -1
};

// 設計点総数 = セグメント数 + 2  （index: 0=S, 1..m=C_0..C_{m-1}, m+1=E）
int numDesignPoints(const Chain& ch);

// 1接触の gap ヤコビアン（設計点に関するスパース勾配 + 半径感度）
ContactJacobian contactJacobian(const Chain& ch, const Contact& c);

// シーン版：接触 c のうち movableIdx の Body に属する側だけで gap 勾配を作る。
// movChain は movableIdx の Body の中心線。固定側は DOF でないので脱落する。
//   自己接触(両側 movable) → contactJacobian と同じ。交差(片側のみ) → その側だけ。
ContactJacobian contactJacobianScene(const Chain& movChain, int movableIdx,
                                     const Contact& c);

// 弧長 s(seg,t) の設計点に関する勾配（半径カップリング用）。上流全制御点に依存。
std::vector<Vec3> arcLengthGradient(const Chain& ch, int seg, double t);

// dr/ds（半径ラムダの中心差分）
double drds(const RadiusFn& R, double s);

// 完全版ヤコビアン = 中心線項 + 半径感度 + 「弧長→半径」カップリング項
//   （Eulerian な r(s) で厳密。dr/ds が小さければ contactJacobian とほぼ同じ）
ContactJacobian contactJacobianFull(const Chain& ch, const RadiusFn& R, const Contact& c);
ContactJacobian contactJacobianSceneFull(const Chain& movChain, const RadiusFn& R,
                                         int movableIdx, const Contact& c);

} // namespace pipe

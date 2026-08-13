#include "pipe/gradient.hpp"
#include <algorithm>
#include <map>
#include <cmath>
#include <vector>

namespace pipe {

int numDesignPoints(const Chain& ch){ return (int)ch.segs.size() + 2; }

namespace {
// M_seg(t) = Σ_l B_l(t) P_l を設計点で展開し、sign 倍して acc に加算
void accumulate(std::map<int,double>& acc, const Chain& ch,
                int seg, double t, double sign){
    const int m = (int)ch.segs.size();
    const double u = 1.0 - t;
    const double B[3] = { u*u, 2*u*t, t*t };
    auto w = segDesignWeights(m, seg);
    for(int l=0;l<3;l++)
        for(auto& wp : w[l])
            acc[wp.first] += sign * B[l] * wp.second;
}
} // namespace

ContactJacobian contactJacobian(const Chain& ch, const Contact& c){
    ContactJacobian J;

    // 設計点ごとの重み:  w_A(d) - w_B(d)
    std::map<int,double> acc;
    accumulate(acc, ch, c.segA, c.tA, +1.0);   // +∂M_A/∂d
    accumulate(acc, ch, c.segB, c.tB, -1.0);   // -∂M_B/∂d
    for(auto& kv : acc){
        if(std::abs(kv.second) < 1e-15) continue;
        J.points.push_back({ kv.first, kv.second * c.normal });  // (w_A-w_B)·n̂
    }

    // 半径感度  n̂·d̂
    Vec3 X = ch.segs[c.segA].at(c.tA);
    Vec3 Y = ch.segs[c.segB].at(c.tB);
    Vec3 dA = normalize(c.pA - X);   // A 中心 → 接触点（相手側）
    Vec3 dB = normalize(c.pB - Y);
    J.dGap_drA =  dot(c.normal, dA);
    J.dGap_drB = -dot(c.normal, dB);
    return J;
}

ContactJacobian contactJacobianScene(const Chain& movChain, int movableIdx,
                                     const Contact& c){
    ContactJacobian J;
    std::map<int,double> acc;
    if(c.bodyA == movableIdx) accumulate(acc, movChain, c.segA, c.tA, +1.0);
    if(c.bodyB == movableIdx) accumulate(acc, movChain, c.segB, c.tB, -1.0);
    for(auto& kv : acc){
        if(std::abs(kv.second) < 1e-15) continue;
        J.points.push_back({ kv.first, kv.second * c.normal });
    }
    J.dGap_drA = 0; J.dGap_drB = 0;
    if(c.bodyA == movableIdx){
        Vec3 X = movChain.segs[c.segA].at(c.tA);
        J.dGap_drA =  dot(c.normal, normalize(c.pA - X));
    }
    if(c.bodyB == movableIdx){
        Vec3 Y = movChain.segs[c.segB].at(c.tB);
        J.dGap_drB = -dot(c.normal, normalize(c.pB - Y));
    }
    return J;
}

// ---- 半径の弧長カップリング項 -------------------------------------------
std::vector<Vec3> arcLengthGradient(const Chain& ch, int seg, double t){
    const int m = (int)ch.segs.size();
    std::vector<Vec3> g(m+2, Vec3{0,0,0});
    // ∂|M'|/∂P_l = coeff_l(τ) * T̂,  coeff_0=-2(1-τ), coeff_1=2(1-2τ), coeff_2=2τ
    auto coeff=[](int l,double tau){ return l==0? -2.0*(1.0-tau) : (l==1? 2.0*(1.0-2.0*tau) : 2.0*tau); };
    auto addSeg=[&](int k, double upper){
        Vec3 I[3]={Vec3{0,0,0},Vec3{0,0,0},Vec3{0,0,0}};
        for(int q=0;q<7;q++){
            double tau = upper*kGL_X[q];
            Vec3 That = normalize(ch.segs[k].deriv(tau));
            double w = upper*kGL_W[q];
            for(int l=0;l<3;l++) I[l] = I[l] + (w*coeff(l,tau))*That;
        }
        auto wts = segDesignWeights(m, k);
        for(int l=0;l<3;l++) for(auto& wd : wts[l]) g[wd.first] = g[wd.first] + wd.second*I[l];
    };
    for(int k=0;k<seg;k++) addSeg(k, 1.0);   // 上流の全長
    addSeg(seg, t);                          // 当該セグメントは t まで
    return g;
}

double drds(const RadiusFn& R, double s){
    double h = std::max(1e-6, 1e-4*std::abs(s));
    auto rp=R(s+h), rm=R(s-h);
    if(rp && rm) return (*rp - *rm)/(2*h);
    auto r0=R(s);
    if(rp && r0) return (*rp - *r0)/h;
    if(rm && r0) return (*r0 - *rm)/h;
    return 0.0;
}

namespace {
// base のスパース勾配に弧長カップリングを足して再スパース化
ContactJacobian addCoupling(const ContactJacobian& base, int n,
                            double cA, const std::vector<Vec3>* gA,
                            double cB, const std::vector<Vec3>* gB){
    std::vector<Vec3> dense(n, Vec3{0,0,0});
    for(const auto& p : base.points) dense[p.dof] = p.grad;
    for(int d=0; d<n; d++){
        if(gA) dense[d] = dense[d] + cA*(*gA)[d];
        if(gB) dense[d] = dense[d] + cB*(*gB)[d];
    }
    ContactJacobian J; J.dGap_drA=base.dGap_drA; J.dGap_drB=base.dGap_drB;
    for(int d=0; d<n; d++) if(norm(dense[d])>1e-15) J.points.push_back({d, dense[d]});
    return J;
}
} // namespace

ContactJacobian contactJacobianFull(const Chain& ch, const RadiusFn& R, const Contact& c){
    ContactJacobian base = contactJacobian(ch, c);
    const int n = numDesignPoints(ch);
    auto gA = arcLengthGradient(ch, c.segA, c.tA);
    auto gB = arcLengthGradient(ch, c.segB, c.tB);
    // ∂gap += dGap_drA * r'(sA) * dsA/dp  +  dGap_drB * r'(sB) * dsB/dp
    return addCoupling(base, n, base.dGap_drA*drds(R,c.sA), &gA,
                                base.dGap_drB*drds(R,c.sB), &gB);
}

ContactJacobian contactJacobianSceneFull(const Chain& movChain, const RadiusFn& R,
                                         int movableIdx, const Contact& c){
    ContactJacobian base = contactJacobianScene(movChain, movableIdx, c);
    const int n = numDesignPoints(movChain);
    std::vector<Vec3> gA, gB;
    bool hasA = (c.bodyA==movableIdx), hasB = (c.bodyB==movableIdx);
    if(hasA) gA = arcLengthGradient(movChain, c.segA, c.tA);
    if(hasB) gB = arcLengthGradient(movChain, c.segB, c.tB);
    return addCoupling(base, n,
                       hasA ? base.dGap_drA*drds(R,c.sA) : 0.0, hasA ? &gA : nullptr,
                       hasB ? base.dGap_drB*drds(R,c.sB) : 0.0, hasB ? &gB : nullptr);
}

} // namespace pipe

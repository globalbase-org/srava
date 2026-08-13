#include "pipe/bezier.hpp"
#include <array>

namespace pipe {

// 7点 Gauss-Legendre（[0,1] へ写像済み）
const std::array<double,7> kGL_X = {
    0.5000000000000000,
    0.5 + 0.5*0.4058451513773972, 0.5 - 0.5*0.4058451513773972,
    0.5 + 0.5*0.7415311855993945, 0.5 - 0.5*0.7415311855993945,
    0.5 + 0.5*0.9491079123427585, 0.5 - 0.5*0.9491079123427585 };
const std::array<double,7> kGL_W = {
    0.5*0.4179591836734694,
    0.5*0.3818300505051189, 0.5*0.3818300505051189,
    0.5*0.2797053914892766, 0.5*0.2797053914892766,
    0.5*0.1294849661688697, 0.5*0.1294849661688697 };

Chain Chain::build(const Vec3& S, const Vec3& E, const std::vector<Vec3>& C){
    Chain ch;
    const int m = (int)C.size();        // 制御点数 = セグメント数
    if (m == 0) return ch;
    auto mid = [](Vec3 a, Vec3 b){ return (a+b)*0.5; };
    if (m == 1){
        ch.segs.push_back({S, C[0], E});
    } else {
        ch.segs.push_back({S, C[0], mid(C[0], C[1])});                  // 先頭
        for (int i=1;i<m-1;i++)
            ch.segs.push_back({mid(C[i-1],C[i]), C[i], mid(C[i],C[i+1])});
        ch.segs.push_back({mid(C[m-2],C[m-1]), C[m-1], E});            // 末尾
    }
    ch.computeArc();
    return ch;
}

void Chain::computeArc(){
    segLen.resize(segs.size());
    segStartS.resize(segs.size());
    double acc = 0;
    for (size_t i=0;i<segs.size();i++){
        segStartS[i] = acc;
        double L = 0;
        for (int g=0; g<7; g++) L += kGL_W[g] * norm(segs[i].deriv(kGL_X[g]));
        segLen[i] = L;
        acc += L;
    }
}

double Chain::arcAt(int seg, double t) const {
    double L = 0;                        // ∫_0^t |B'| を [0,t] へ写像した GL で
    for (int g=0; g<7; g++) L += kGL_W[g] * norm(segs[seg].deriv(t*kGL_X[g]));
    return segStartS[seg] + t*L;
}

std::array<std::vector<std::pair<int,double>>,3> segDesignWeights(int m, int seg){
    using V = std::vector<std::pair<int,double>>;
    std::array<V,3> w;
    const int S = 0, E = m+1;
    auto Ci = [](int i){ return i+1; };          // C_i の設計点 index
    if (m == 1){
        w[0]={{S,1.0}}; w[1]={{Ci(0),1.0}}; w[2]={{E,1.0}};
    } else if (seg == 0){
        w[0]={{S,1.0}};
        w[1]={{Ci(0),1.0}};
        w[2]={{Ci(0),0.5},{Ci(1),0.5}};
    } else if (seg == m-1){
        w[0]={{Ci(m-2),0.5},{Ci(m-1),0.5}};
        w[1]={{Ci(m-1),1.0}};
        w[2]={{E,1.0}};
    } else {
        w[0]={{Ci(seg-1),0.5},{Ci(seg),0.5}};
        w[1]={{Ci(seg),1.0}};
        w[2]={{Ci(seg),0.5},{Ci(seg+1),0.5}};
    }
    return w;
}

} // namespace pipe

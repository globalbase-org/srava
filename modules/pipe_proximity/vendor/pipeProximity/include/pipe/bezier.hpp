#pragma once
// 2次ベジエ 1 セグメントと、それを連ねたチェーン（中心線）
#include "pipe/vec3.hpp"
#include <vector>
#include <array>
#include <utility>

namespace pipe {

// 2次ベジエ 1 セグメント。at/deriv はホットパスなので inline。
struct QSeg {
    Vec3 P0, P1, P2;
    Vec3 at(double t) const {
        double u = 1.0 - t;
        return (u*u)*P0 + (2*u*t)*P1 + (t*t)*P2;
    }
    Vec3 deriv(double t) const {  // B'(t) = 2(1-t)(P1-P0) + 2t(P2-P1)
        return 2.0*(1.0-t)*(P1-P0) + 2.0*t*(P2-P1);
    }
};

// 開いた 2次ベジエチェーン。
//   両端は通過点 S,E（+ 接線は最初/最後の制御点で制御）
//   内部の通過点は隣接制御点の中点（中点方式）→ C1 連続
struct Chain {
    std::vector<QSeg>   segs;
    std::vector<double> segLen;     // 各セグメント弧長
    std::vector<double> segStartS;  // セグメント始点の累積弧長

    // S(始点) + off-curve 制御点列 C + E(終点) から構築
    static Chain build(const Vec3& S, const Vec3& E, const std::vector<Vec3>& C);

    void   computeArc();                  // segLen / segStartS を Gauss-Legendre で
    double totalLen() const { return segStartS.empty()?0: segStartS.back()+segLen.back(); }
    double arcAt(int seg, double t) const;// (seg,t) における始点からの弧長 s
};

// 7 点 Gauss–Legendre のノード・重み（[0,1] へ写像済み）。弧長・曲げ積分で共用。
extern const std::array<double,7> kGL_X;
extern const std::array<double,7> kGL_W;

// チェーンの設計（最適化変数）。DOF index: 0=S, 1..m=C_0..C_{m-1}, m+1=E。
struct ChainDesign {
    Vec3 S, E;
    std::vector<Vec3> C;
    int  numDOF() const { return (int)C.size() + 2; }
    Vec3&       point(int d){ int m=(int)C.size(); return d==0?S : (d==m+1?E : C[d-1]); }
    const Vec3& point(int d) const { int m=(int)C.size(); return d==0?S : (d==m+1?E : C[d-1]); }
    Chain build() const { return Chain::build(S, E, C); }
};

// セグメント seg の局所制御点 P0,P1,P2 を「設計点」の線形結合で表す重み。
// 設計点インデックス: 0=S, 1..m=C_0..C_{m-1}, m+1=E （m = セグメント数）
// 戻り値[l] = P_l に効く (設計点index, 係数) のリスト（中点方式の 1 と 0.5）
std::array<std::vector<std::pair<int,double>>,3> segDesignWeights(int m, int seg);

} // namespace pipe

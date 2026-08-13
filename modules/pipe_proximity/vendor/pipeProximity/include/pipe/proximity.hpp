#pragma once
// 自己接近（self-proximity）検出のコア API
#include "pipe/vec3.hpp"
#include "pipe/bezier.hpp"
#include "pipe/radius.hpp"
#include <vector>

namespace pipe {

// 2つのリム円の最近接結果（純幾何・再利用可）
struct CC { double dist; Vec3 pA, pB; };

// 円A(中心X,法線TA,半径rA) と 円B(中心Y,法線TB,半径rB) の最近接点ペア。
// φ,ψ を射影反復で閉じる。TA,TB は単位ベクトル想定。
CC circleCircle(Vec3 X, Vec3 TA, double rA, Vec3 Y, Vec3 TB, double rB);

// 1接近箇所の出力
struct Contact {
    int    bodyA=0, bodyB=0;// どの Body か（Scene 用。単一チェーンでは 0）
    int    segA, segB;      // セグメント番号
    double tA, tB;          // セグメント内ローカルパラメータ [0,1]
    double sA, sB;          // 始点からの弧長
    double rA, rB;          // その位置の半径
    Vec3   pA, pB;          // 表面接触点
    double gap;             // |pA - pB| 表面間距離
    double centerDist;      // |X - Y| 中心線間距離
    Vec3   normal;          // (pA - pB)/gap 調整ループ用の接触法線
};

struct Params {
    double kExclude   = 3.0;   // 除外帯 s_exclude = k * r_local（自明な隣接重なりを除く）
    double reportGap  = 1e9;   // この gap 以下のみ報告（しきい値）
    int    gridN      = 11;    // 粗グリッド分割（種探し）
    int    newtonIter = 40;    // 各種の精密化反復
    bool   useBVH     = true;  // broad-phase に BVH を使う（false なら総当たり）
};

// 探索の統計（枝刈り効果の確認用）
struct Stats {
    int crossPairsTested = 0;  // narrow-phase に回した異セグメント対の数
    int crossPairsTotal  = 0;  // n(n-1)/2
};

// 1本の開いた鎖の自己接近をすべて列挙（gap 昇順）。
// stats!=nullptr なら枝刈り統計を書き込む。
std::vector<Contact> findSelfProximities(const Chain& ch, const RadiusFn& R,
                                         const Params& pr, Stats* stats = nullptr);

// 指定 (segA,tA),(segB,tB) における表面 gap の凍結再評価。
// 追跡中サイトの gap 更新や勾配検証に。valid=false は定義域外。
struct GapEval { bool valid=false; double gap=0; Vec3 pA,pB,X,Y; double rA=0,rB=0; };
GapEval evalGapAt(const Chain& ch, const RadiusFn& R,
                  int segA, double tA, int segB, double tB);

} // namespace pipe

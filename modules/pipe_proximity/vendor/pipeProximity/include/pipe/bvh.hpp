#pragma once
// broad-phase 用 AABB-BVH（自己接近の候補セグメント対を枝刈り列挙）
#include "pipe/vec3.hpp"
#include "pipe/bezier.hpp"
#include "pipe/radius.hpp"
#include <vector>
#include <utility>
#include <algorithm>

namespace pipe {

// 軸平行境界箱
struct AABB {
    Vec3 lo{ 1e300, 1e300, 1e300};
    Vec3 hi{-1e300,-1e300,-1e300};
    bool valid() const { return lo.x<=hi.x; }
    void expand(Vec3 p){
        lo.x=std::min(lo.x,p.x); lo.y=std::min(lo.y,p.y); lo.z=std::min(lo.z,p.z);
        hi.x=std::max(hi.x,p.x); hi.y=std::max(hi.y,p.y); hi.z=std::max(hi.z,p.z);
    }
    void expand(const AABB& b){ if(b.valid()){ expand(b.lo); expand(b.hi); } }
    void inflate(double r){ lo=lo-Vec3{r,r,r}; hi=hi+Vec3{r,r,r}; }
    Vec3 center() const { return (lo+hi)*0.5; }
    // 2箱間の最小距離（重なっていれば 0）。無効箱は +inf。
    static double dist(const AABB& a, const AABB& b){
        if(!a.valid()||!b.valid()) return 1e300;
        double dx=std::max({0.0, a.lo.x-b.hi.x, b.lo.x-a.hi.x});
        double dy=std::max({0.0, a.lo.y-b.hi.y, b.lo.y-a.hi.y});
        double dz=std::max({0.0, a.lo.z-b.hi.z, b.lo.z-a.hi.z});
        return std::sqrt(dx*dx+dy*dy+dz*dz);
    }
};

// セグメント単位の BVH。各リーフ箱は「制御点の凸包 + そのセグメントの最大半径」で膨張。
struct BVH {
    struct Node { AABB box; int l=-1, r=-1, seg=-1; }; // seg>=0 ならリーフ
    std::vector<Node> nodes;
    int root = -1;
    std::vector<AABB> leafBox;   // セグメント番号 → 膨張済みリーフ箱

    static BVH build(const Chain& ch, const RadiusFn& R);

    // 任意のリーフ箱列から構築（leaf.seg = 箱の index）。Scene 全体の broad-phase に。
    static BVH buildBoxes(const std::vector<AABB>& boxes);

    // 膨張済み箱どうしが margin 以内のリーフ対 (i<j) を列挙
    void selfPairs(double margin, std::vector<std::pair<int,int>>& out) const;

private:
    int  buildRec(std::vector<int>& idx, int begin, int end);
    void selfRec(int n, double margin, std::vector<std::pair<int,int>>& out) const;
    void pairRec(int a, int b, double margin, std::vector<std::pair<int,int>>& out) const;
};

} // namespace pipe

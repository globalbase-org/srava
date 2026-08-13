#include "pipe/bvh.hpp"
#include <algorithm>

namespace pipe {

BVH BVH::build(const Chain& ch, const RadiusFn& R){
    BVH bvh;
    const int n = (int)ch.segs.size();
    bvh.leafBox.resize(n);

    for(int i=0;i<n;i++){
        const QSeg& s = ch.segs[i];
        AABB box;
        box.expand(s.P0); box.expand(s.P1); box.expand(s.P2);
        // セグメント上を数点サンプルし、定義域内の最大半径で膨張
        double rmax = -1.0;
        const int M = 5;
        for(int k=0;k<=M;k++){
            double t = (double)k/M;
            auto r = R(ch.arcAt(i, t));
            if(r) rmax = std::max(rmax, *r);
        }
        if(rmax < 0.0){ bvh.leafBox[i] = AABB{}; }   // パイプ無し → 無効箱
        else { box.inflate(rmax); bvh.leafBox[i] = box; }
    }

    return buildBoxes(bvh.leafBox);
}

BVH BVH::buildBoxes(const std::vector<AABB>& boxes){
    BVH bvh;
    bvh.leafBox = boxes;
    std::vector<int> idx;
    for(int i=0;i<(int)boxes.size();i++) if(boxes[i].valid()) idx.push_back(i);
    if(!idx.empty()) bvh.root = bvh.buildRec(idx, 0, (int)idx.size());
    return bvh;
}

int BVH::buildRec(std::vector<int>& idx, int begin, int end){
    int node = (int)nodes.size();
    nodes.push_back({});
    AABB b;
    for(int k=begin;k<end;k++) b.expand(leafBox[idx[k]]);

    if(end - begin == 1){
        nodes[node].box = b;
        nodes[node].seg = idx[begin];
        return node;
    }
    // セントロイドの最長軸で median split
    AABB cb;
    for(int k=begin;k<end;k++) cb.expand(leafBox[idx[k]].center());
    Vec3 ext = cb.hi - cb.lo;
    int axis = (ext.x>=ext.y && ext.x>=ext.z) ? 0 : (ext.y>=ext.z ? 1 : 2);
    auto comp = [&](int a, int c){
        Vec3 ca = leafBox[a].center(), cc = leafBox[c].center();
        double va = axis==0?ca.x:axis==1?ca.y:ca.z;
        double vc = axis==0?cc.x:axis==1?cc.y:cc.z;
        return va < vc;
    };
    int mid = (begin + end) / 2;
    std::nth_element(idx.begin()+begin, idx.begin()+mid, idx.begin()+end, comp);

    int L = buildRec(idx, begin, mid);
    int Rn = buildRec(idx, mid, end);
    nodes[node].box = b;          // 子追加で vector が再確保されうるので index 経由
    nodes[node].l = L;
    nodes[node].r = Rn;
    return node;
}

void BVH::selfPairs(double margin, std::vector<std::pair<int,int>>& out) const {
    if(root < 0) return;
    selfRec(root, margin, out);
}

void BVH::selfRec(int n, double margin, std::vector<std::pair<int,int>>& out) const {
    if(nodes[n].seg >= 0) return;                 // リーフ単体は対にならない
    selfRec(nodes[n].l, margin, out);
    selfRec(nodes[n].r, margin, out);
    pairRec(nodes[n].l, nodes[n].r, margin, out);
}

void BVH::pairRec(int a, int b, double margin,
                  std::vector<std::pair<int,int>>& out) const {
    if(AABB::dist(nodes[a].box, nodes[b].box) > margin) return;
    bool la = nodes[a].seg >= 0, lb = nodes[b].seg >= 0;
    if(la && lb){
        int i = nodes[a].seg, j = nodes[b].seg;
        if(i > j) std::swap(i, j);
        out.push_back({i, j});
        return;
    }
    // 内部ノード側を降りる（大きい方）
    auto boxSize = [&](int x){ Vec3 e = nodes[x].box.hi - nodes[x].box.lo;
                               return e.x + e.y + e.z; };
    if(lb || (!la && boxSize(a) >= boxSize(b))){
        pairRec(nodes[a].l, b, margin, out);
        pairRec(nodes[a].r, b, margin, out);
    } else {
        pairRec(a, nodes[b].l, margin, out);
        pairRec(a, nodes[b].r, margin, out);
    }
}

} // namespace pipe

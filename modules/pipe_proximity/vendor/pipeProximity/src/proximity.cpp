#include "pipe/proximity.hpp"
#include "pipe/scene.hpp"
#include "pipe/bvh.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace pipe {

// ---- 2チューブ断面の符号付きクリアランス（改良 capsule・非並行対応） -------
CC circleCircle(Vec3 X, Vec3 TA, double rA, Vec3 Y, Vec3 TB, double rB){
    // チューブを「中心線に垂直な円板の掃引（swept-disk）」と見て、各円板が連結方向 v に
    // 張り出す実効半径 rX*sqrt(1-(v·t)^2) を引く:
    //   gap = |X-Y| - rA*sqrt(1-(v·a)^2) - rB*sqrt(1-(v·b)^2)
    // 横並び（v⊥t）で capsule（中心線距離-半径和）に一致、end-on（v∥t）で半径寄与 0。
    // 旧版（リム円どうしの最近接距離）は深く貫くとリムが交差して距離が 0 へ戻り、深さを
    // 過小評価＋非単調（ペナルティが「もっと刺す」方向へ引き込む）になっていた。これを単調・
    // 符号付き（分離+/貫通-）で返す。表面点 pA,pB は接触法線（≈中心線方向）用。
    Vec3 dXY = X - Y;
    double dc = norm(dXY);
    Vec3 a = (norm(TA)>1e-12) ? normalize(TA) : Vec3{1,0,0};
    Vec3 b = (norm(TB)>1e-12) ? normalize(TB) : Vec3{1,0,0};
    if (dc < 1e-9) return { -(rA+rB), X, X };   // 中心線一致（退化）: 最大貫通
    Vec3 v  = dXY * (1.0/dc);                    // Y→X 方向
    double va = dot(v,a), vb = dot(v,b);
    double sA = std::sqrt(std::max(0.0, 1.0 - va*va));   // A 円板の v 方向 実効率
    double sB = std::sqrt(std::max(0.0, 1.0 - vb*vb));
    double gap = dc - rA*sA - rB*sB;
    Vec3 wA = v - va*a; double nwA = norm(wA); wA = (nwA>1e-9) ? wA*(1.0/nwA) : anyPerp(a);
    Vec3 wB = v - vb*b; double nwB = norm(wB); wB = (nwB>1e-9) ? wB*(1.0/nwB) : anyPerp(b);
    Vec3 pA = X - rA*wA;   // A 表面、B 側（-v）
    Vec3 pB = Y + rB*wB;   // B 表面、A 側（+v）
    return { gap, pA, pB };
}

namespace {

// (chA,segA,tA) と (chB,segB,tB) における表面最近接の評価（2チェーン一般形）
struct Eval {
    bool   has=false;
    double dist=1e300;
    double tA=0,tB=0,sA=0,sB=0,rA=0,rB=0;
    Vec3   X,Y,pA,pB;
};
Eval evalPairX(const Chain& chA, const RadiusFn& RA, int segA, double tA,
               const Chain& chB, const RadiusFn& RB, int segB, double tB){
    Eval e;
    double sA = chA.arcAt(segA, tA), sB = chB.arcAt(segB, tB);
    auto rA = RA(sA), rB = RB(sB);
    if (!rA || !rB) return e;            // 端: パイプ存在せず
    Vec3 X = chA.segs[segA].at(tA), Y = chB.segs[segB].at(tB);
    Vec3 TA = normalize(chA.segs[segA].deriv(tA));
    Vec3 TB = normalize(chB.segs[segB].deriv(tB));
    CC cc = circleCircle(X, TA, *rA, Y, TB, *rB);
    e.has=true; e.dist=cc.dist; e.tA=tA; e.tB=tB; e.sA=sA; e.sB=sB; e.rA=*rA; e.rB=*rB;
    e.X=X; e.Y=Y; e.pA=cc.pA; e.pB=cc.pB;
    return e;
}

// 弧長除外帯（自明な隣接重なりを除く）。同一 Body のときだけ有効。
inline bool excluded(const Eval& e, double k, bool sameBody){
    if(!sameBody) return false;          // 異 Body は弧長原点が違うので比較しない
    double sExcl = k * 0.5*(e.rA + e.rB);
    return std::abs(e.sA - e.sB) < sExcl;
}

// 2D Newton（有限差分）で (tA,tB) を精密化、[0,1]^2 にクランプ
Eval refineX(const Chain& chA, const RadiusFn& RA, int segA,
             const Chain& chB, const RadiusFn& RB, int segB,
             double tA, double tB, const Params& pr, bool sameBody){
    auto clamp01=[](double v){ return std::max(0.0, std::min(1.0, v)); };
    auto f=[&](double a,double b)->double{
        Eval e=evalPairX(chA,RA,segA,a,chB,RB,segB,b);
        if(!e.has || excluded(e,pr.kExclude,sameBody)) return 1e300;
        return e.dist;
    };
    const double h=1e-5;
    for(int it=0; it<pr.newtonIter; it++){
        double f0=f(tA,tB);
        if(f0>=1e299) break;
        double fa1=f(clamp01(tA+h),tB), fa0=f(clamp01(tA-h),tB);
        double fb1=f(tA,clamp01(tB+h)), fb0=f(tA,clamp01(tB-h));
        double ga=(fa1-fa0)/(2*h), gb=(fb1-fb0)/(2*h);
        double Haa=(fa1-2*f0+fa0)/(h*h), Hbb=(fb1-2*f0+fb0)/(h*h);
        double fab=f(clamp01(tA+h),clamp01(tB+h)), fmm=f(clamp01(tA-h),clamp01(tB-h));
        double Hab=(fab - fa1 - fb1 + 2*f0 - fa0 - fb0 + fmm)/(2*h*h);
        double det=Haa*Hbb-Hab*Hab;
        double dA,dB;
        if(std::abs(det)>1e-12 && Haa>0){
            dA=-( Hbb*ga - Hab*gb)/det;
            dB=-(-Hab*ga + Haa*gb)/det;
        } else { dA=-ga; dB=-gb; }
        double step=1.0; bool ok=false;
        for(int ls=0; ls<20; ls++){
            double na=clamp01(tA+step*dA), nb=clamp01(tB+step*dB);
            if(f(na,nb) < f0-1e-12){ tA=na; tB=nb; ok=true; break; }
            step*=0.5;
        }
        if(!ok) break;
        if(std::abs(step*dA)+std::abs(step*dB) < 1e-10) break;
    }
    return evalPairX(chA,RA,segA,tA,chB,RB,segB,tB);
}

// 1つのセグメント対を narrow-phase 処理し、接触を out に追加（body 番号も記録）
void processPairX(const Chain& chA, const RadiusFn& RA, int segA,
                  const Chain& chB, const RadiusFn& RB, int segB,
                  const Params& pr, bool sameBody, int bodyA, int bodyB,
                  std::vector<Contact>& out){
    const int N=pr.gridN;
    std::vector<std::vector<double>> grid(N+1, std::vector<double>(N+1,1e300));
    for(int a=0;a<=N;a++) for(int b=0;b<=N;b++){
        Eval e=evalPairX(chA,RA,segA,(double)a/N, chB,RB,segB,(double)b/N);
        grid[a][b]=(e.has && !excluded(e,pr.kExclude,sameBody)) ? e.dist : 1e300;
    }
    for(int a=0;a<=N;a++) for(int b=0;b<=N;b++){
        double v=grid[a][b];
        if(v>=1e299) continue;
        bool lm=true;
        for(int da=-1;da<=1&&lm;da++) for(int db=-1;db<=1;db++){
            int na=a+da,nb=b+db;
            if(na<0||nb<0||na>N||nb>N||(da==0&&db==0)) continue;
            if(grid[na][nb]<v){ lm=false; break; }
        }
        if(!lm) continue;
        Eval e=refineX(chA,RA,segA, chB,RB,segB, (double)a/N,(double)b/N, pr, sameBody);
        if(!e.has || excluded(e,pr.kExclude,sameBody) || e.dist>pr.reportGap) continue;
        bool dup=false;
        for(auto& c: out){
            if(c.bodyA!=bodyA || c.bodyB!=bodyB) continue;
            if((norm(c.pA-e.pA)<1e-4 && norm(c.pB-e.pB)<1e-4) ||
               (norm(c.pA-e.pB)<1e-4 && norm(c.pB-e.pA)<1e-4)){ dup=true; break; }
        }
        if(dup) continue;
        Contact c;
        c.bodyA=bodyA; c.bodyB=bodyB; c.segA=segA; c.segB=segB;
        c.tA=e.tA; c.tB=e.tB; c.sA=e.sA; c.sB=e.sB; c.rA=e.rA; c.rB=e.rB;
        c.pA=e.pA; c.pB=e.pB; c.gap=e.dist; c.centerDist=norm(e.X-e.Y);
        c.normal = (std::abs(e.dist)>1e-12) ? (e.pA-e.pB)*(1.0/e.dist) : anyPerp(normalize(e.Y-e.X));
        out.push_back(c);
    }
}

} // namespace

// ---- 単一チェーンの自己接近（従来 API） ----------------------------------
std::vector<Contact> findSelfProximities(const Chain& ch, const RadiusFn& R,
                                         const Params& pr, Stats* stats){
    std::vector<Contact> out;
    const int n=(int)ch.segs.size();

    std::vector<std::pair<int,int>> pairs;
    if(pr.useBVH){
        BVH bvh = BVH::build(ch, R);
        bvh.selfPairs(pr.reportGap, pairs);
    } else {
        for(int i=0;i<n;i++) for(int j=i+1;j<n;j++) pairs.push_back({i,j});
    }
    if(stats){ stats->crossPairsTested=(int)pairs.size(); stats->crossPairsTotal=n*(n-1)/2; }

    for(auto& pr2 : pairs) processPairX(ch,R,pr2.first, ch,R,pr2.second, pr, true, 0,0, out);
    for(int i=0;i<n;i++)   processPairX(ch,R,i, ch,R,i, pr, true, 0,0, out);

    std::sort(out.begin(),out.end(),
              [](const Contact&a,const Contact&b){return a.gap<b.gap;});
    return out;
}

GapEval evalGapAt(const Chain& ch, const RadiusFn& R,
                  int segA, double tA, int segB, double tB){
    Eval e = evalPairX(ch, R, segA, tA, ch, R, segB, tB);
    GapEval g;
    g.valid=e.has; g.gap=e.dist; g.pA=e.pA; g.pB=e.pB; g.X=e.X; g.Y=e.Y;
    g.rA=e.rA; g.rB=e.rB;
    return g;
}

// ---- シーン（複数 Body）の近接 -------------------------------------------
std::vector<Contact> findSceneProximities(const Scene& sc, const Params& pr,
                                          Stats* stats){
    std::vector<Contact> out;
    const int nb = (int)sc.bodies.size();
    int tested = 0;

    if(pr.useBVH){
        // 全 Body の全セグメントを 1 つの BVH に（リーフ箱 = 凸包 + セグメント最大半径）
        std::vector<AABB> boxes;
        std::vector<std::pair<int,int>> meta;   // (body, seg)
        for(int b=0;b<nb;b++){
            const Body& B = sc.bodies[b];
            for(int s=0;s<(int)B.chain.segs.size();s++){
                AABB box; const QSeg& q=B.chain.segs[s];
                box.expand(q.P0); box.expand(q.P1); box.expand(q.P2);
                double rmax=-1;
                for(int k=0;k<=5;k++){ auto r=B.radius(B.chain.arcAt(s,(double)k/5));
                    if(r) rmax=std::max(rmax,*r); }
                if(rmax>=0){ box.inflate(rmax); boxes.push_back(box); meta.push_back({b,s}); }
                else { boxes.push_back(AABB{}); meta.push_back({b,s}); } // 無効箱
            }
        }
        BVH bvh = BVH::buildBoxes(boxes);
        std::vector<std::pair<int,int>> pairs;
        bvh.selfPairs(pr.reportGap, pairs);
        for(auto& pp : pairs){
            int ba=meta[pp.first].first,  sa=meta[pp.first].second;
            int bb=meta[pp.second].first, sb=meta[pp.second].second;
            const Body& BA=sc.bodies[ba]; const Body& BB=sc.bodies[bb];
            if(!BA.movable && !BB.movable) continue;
            processPairX(BA.chain,BA.radius,sa, BB.chain,BB.radius,sb,
                         pr, (ba==bb), ba, bb, out);
            ++tested;
        }
        // 同一 Body・同一セグメント内（movable のみ）
        for(int b=0;b<nb;b++) if(sc.bodies[b].movable){
            const Body& B=sc.bodies[b];
            for(int s=0;s<(int)B.chain.segs.size();s++)
                processPairX(B.chain,B.radius,s, B.chain,B.radius,s, pr, true, b,b, out);
        }
    } else {
        for(int a=0;a<nb;a++) for(int b=a;b<nb;b++){
            const Body& BA = sc.bodies[a];
            const Body& BB = sc.bodies[b];
            if(!BA.movable && !BB.movable) continue;   // 固定–固定はスキップ
            bool same = (a==b);
            const int na=(int)BA.chain.segs.size(), nbs=(int)BB.chain.segs.size();
            for(int i=0;i<na;i++){
                int j0 = same ? i : 0;
                for(int j=j0;j<nbs;j++){
                    processPairX(BA.chain,BA.radius,i, BB.chain,BB.radius,j,
                                 pr, same, a, b, out);
                    ++tested;
                }
            }
        }
    }
    if(stats){ stats->crossPairsTested=tested; stats->crossPairsTotal=tested; }
    std::sort(out.begin(),out.end(),
              [](const Contact&a,const Contact&b){return a.gap<b.gap;});
    return out;
}

} // namespace pipe

#include "pipe/controller.hpp"
#include "pipe/gradient.hpp"
#include "pipe/cd_parallel.hpp"   // 並列プリミティブ(別 TU・<thread> を持ち込まない=namespace pipe 衝突回避)
#include <algorithm>
#include <cmath>
#include <array>
#include <utility>
#include <functional>

namespace pipe {

double lengthEnergy(const Chain& ch){ return ch.totalLen(); }

double bendingEnergy(const Chain& ch){
    // ∫κ²ds = Σ_seg ∫_0^1 κ(t)² |M'(t)| dt,  κ = |M'×M''| / |M'|³
    double E = 0;
    for(const QSeg& s : ch.segs){
        Vec3 Mpp = 2.0*(s.P2 - 2.0*s.P1 + s.P0);   // 2次は M'' が定数
        for(int g=0; g<7; g++){
            double t = kGL_X[g];
            Vec3 Mp = s.deriv(t);
            double sp = norm(Mp);
            if(sp < 1e-12) continue;
            double kappa = norm(cross(Mp, Mpp)) / (sp*sp*sp);
            E += kGL_W[g] * kappa*kappa * sp;       // κ² ds
        }
    }
    return E;
}

double externalPotentialEnergy(const Chain& ch, const CtrlParams& cp){
    if(cp.fZ==0 && cp.fAxis==0 && cp.fOrigin==0) return 0.0;
    double E = 0;   // ∫ U(M(t)) |M'(t)| dt,  U = -fZ z + fAxis*rho + fOrigin*r
    for(const QSeg& s : ch.segs){
        for(int g=0; g<7; g++){
            double t = kGL_X[g];
            Vec3 x = s.at(t);
            double sp = norm(s.deriv(t));
            double rho = std::sqrt(x.x*x.x + x.y*x.y);   // z軸からの距離
            double r   = norm(x);                         // 原点からの距離
            double U = -cp.fZ*x.z + cp.fAxis*rho + cp.fOrigin*r;
            E += kGL_W[g] * U * sp;
        }
    }
    return E;
}

namespace {

// 幾何エネルギー（長さ + 曲げ + 軟ピン + 外力）。接触ペナルティ・硬ピンは含まない。
double energyGeom(const ChainDesign& d, const CtrlParams& cp){
    Chain ch = d.build();
    double E = cp.wLen*lengthEnergy(ch) + cp.wBend*bendingEnergy(ch)
             + externalPotentialEnergy(ch, cp);
    for(const Pin& pn : cp.pins){
        if(pn.hard) continue;                       // 硬ピンは拘束で扱う
        Vec3 Mi = (d.C[pn.joint] + d.C[pn.joint+1]) * 0.5;
        Vec3 e  = Mi - pn.target;
        E += cp.wPin * dot(e, e);
    }
    // 制御点間隔の均一化（接線方向スライドのヌルモード抑制）。
    // 全 DOF 折れ線 [S, C..., E] の隣接区間長の差 (|e_i|-|e_{i-1}|)² を罰する＝間隔を揃えるばね。
    if(cp.wSpace > 0){
        int nd = d.numDOF();
        if(nd >= 3){
            double prev = norm(d.point(1) - d.point(0));
            for(int i=2; i<nd; i++){
                double cur = norm(d.point(i) - d.point(i-1));
                double dd  = cur - prev;
                E += cp.wSpace * dd * dd;
                prev = cur;
            }
        }
    }
    return E;
}

// ---- 硬拘束（固定DOF＋硬ピン）: 線形等式 A x = b の零空間射影 ----
struct LinCon {
    int nDOF = 0;
    std::vector<std::vector<std::pair<int,double>>> rows;  // A の各行（スパース）
    std::array<std::vector<double>,3> rhs;                 // b（軸ごと）
    bool empty() const { return rows.empty(); }
};

LinCon buildConstraints(const ChainDesign& d, const CtrlParams& cp){
    LinCon L; L.nDOF = d.numDOF();
    auto addRow=[&](std::vector<std::pair<int,double>> row, Vec3 target){
        L.rows.push_back(std::move(row));
        L.rhs[0].push_back(target.x);
        L.rhs[1].push_back(target.y);
        L.rhs[2].push_back(target.z);
    };
    for(int f : cp.fixedDOF)                         // 固定 = 初期値で固定
        if(f>=0 && f<L.nDOF) addRow({{f,1.0}}, d.point(f));
    for(const Pin& pn : cp.pins) if(pn.hard)         // 硬ピン: 0.5 C_j + 0.5 C_{j+1} = target
        // ★範囲検査 (2026-08-13): joint は DOF j+1, j+2 を参照するので nDOF を越えると
        //   feasibilityProject の std::vector<Vec3> の **外側に書いて**ヒープを壊す
        //   (SIGSEGV / corrupted double-linked list)。上の fixedDOF ループと同じく弾く。
        //   呼び出し側 (srava の parse_params) でも明示エラーにしているが、ライブラリ単体でも安全に。
        if( pn.joint >= 0 && pn.joint+2 < L.nDOF )
            addRow({{pn.joint+1,0.5},{pn.joint+2,0.5}}, pn.target);
    return L;
}

// (M + eps I) y = r を解く（M 対称、部分ピボット GE）
std::vector<double> solveSym(std::vector<std::vector<double>> M,
                             std::vector<double> r, double eps){
    int n = (int)r.size();
    for(int i=0;i<n;i++) M[i][i] += eps;
    for(int col=0;col<n;col++){
        int piv=col;
        for(int k=col+1;k<n;k++) if(std::abs(M[k][col])>std::abs(M[piv][col])) piv=k;
        std::swap(M[col],M[piv]); std::swap(r[col],r[piv]);
        double dv=M[col][col]; if(std::abs(dv)<1e-300) dv=(dv<0?-1e-300:1e-300);
        for(int k=col+1;k<n;k++){ double f=M[k][col]/dv; if(f==0) continue;
            for(int j=col;j<n;j++) M[k][j]-=f*M[col][j]; r[k]-=f*r[col]; }
    }
    std::vector<double> y(n,0);
    for(int i=n-1;i>=0;i--){ double s=r[i];
        for(int j=i+1;j<n;j++) s-=M[i][j]*y[j]; y[i]=s/M[i][i]; }
    return y;
}

// x（DOFごとの Vec3）を拘束へ射影。
//   useRhs=true : feasibility 射影（x を Ax=b へ。返り値=射影後残差ノルム）
//   useRhs=false: 零空間射影（勾配 g から拘束方向成分を除く）
double projectVec(std::vector<Vec3>& x, const LinCon& L, bool useRhs){
    if(L.empty()) return 0.0;
    const int nC=(int)L.rows.size();
    std::vector<std::vector<double>> M(nC, std::vector<double>(nC,0));
    for(int i=0;i<nC;i++) for(int j=0;j<nC;j++){
        double s=0;
        for(auto& ai:L.rows[i]) for(auto& aj:L.rows[j])
            if(ai.first==aj.first) s+=ai.second*aj.second;
        M[i][j]=s;
    }
    double maxdiag=0; for(int i=0;i<nC;i++) maxdiag=std::max(maxdiag,M[i][i]);
    double eps=1e-9*std::max(1.0,maxdiag);
    double residual=0;
    for(int axis=0;axis<3;axis++){
        std::vector<double> r(nC,0);
        for(int i=0;i<nC;i++){ double ax=0;
            for(auto& a:L.rows[i]) ax+=a.second*comp(x[a.first],axis);
            r[i]= useRhs ? (ax - L.rhs[axis][i]) : ax; }
        std::vector<double> y=solveSym(M,r,eps);
        for(int i=0;i<nC;i++) for(auto& a:L.rows[i])
            comp(x[a.first],axis) -= a.second*y[i];
        if(useRhs) for(int i=0;i<nC;i++){ double ax=0;
            for(auto& a:L.rows[i]) ax+=a.second*comp(x[a.first],axis);
            double e=ax-L.rhs[axis][i]; residual+=e*e; }
    }
    return std::sqrt(residual);
}

// ChainDesign を feasibility 射影（残差を返す）
double feasibilityProject(ChainDesign& d, const LinCon& L){
    if(L.empty()) return 0.0;
    std::vector<Vec3> pts(d.numDOF());
    for(int i=0;i<d.numDOF();i++) pts[i]=d.point(i);
    double res=projectVec(pts, L, true);
    for(int i=0;i<d.numDOF();i++) d.point(i)=pts[i];
    return res;
}

// 拡張ラグランジュ乗数ストア。接触を (body対, セグメント対) ＋ ローカル t で対応付ける。
// 弧長 s ではなく (seg, t) で照合（曲線が伸縮しても seg 番号は不変なので頑健）。
struct MultStore {
    struct Rec { int bA,bB,segA,segB; double tA,tB,lam; };
    std::vector<Rec> recs;
    double window = 0.25;   // t 空間の対応付け窓
    double lookup(const Contact& c) const {
        double best=1e18, lam=0;
        for(const auto& m : recs)
            if(m.bA==c.bodyA && m.bB==c.bodyB && m.segA==c.segA && m.segB==c.segB){
                double d=std::abs(m.tA-c.tA)+std::abs(m.tB-c.tB);
                if(d<best){ best=d; lam=m.lam; }
            }
        return best<=window ? lam : 0.0;
    }
};

// PHR 拡張ラグランジュの 1 接触項。energy を返し、力係数 coeff を out に。
//   制約 g=gap-dMin>=0、μ=2*wPenalty、t=max(0, λ-μg)。λ=0 で純ペナルティに一致。
inline double alContact(double gap, double dMin, double wPenalty, double lam, double& coeff){
    double mu = 2.0*wPenalty;
    double g  = gap - dMin;
    double t  = std::max(0.0, lam - mu*g);
    coeff = t;
    return (t*t - lam*lam)/(2.0*mu);
}

// 接触ペナルティ込みの総エネルギー（ラインサーチ評価用、検出を伴う）
double phiFull(const ChainDesign& d, const RadiusFn& R, const CtrlParams& cp,
               const MultStore* ms=nullptr){
    double E = energyGeom(d, cp);
    Chain ch = d.build();
    for(const Contact& c : findSelfProximities(ch, R, cp.det)){
        double lam = ms ? ms->lookup(c) : 0.0, coeff;
        E += alContact(c.gap, cp.dMin, cp.wPenalty, lam, coeff);
    }
    return E;
}

// 総エネルギーの勾配。幾何項は有限差分、接触項は解析（PHR 拡張ラグランジュ）。
std::vector<Vec3> gradient(const ChainDesign& d, const RadiusFn& R,
                           const CtrlParams& cp, const std::vector<char>& fixed,
                           const MultStore* ms=nullptr){
    const int n = d.numDOF();
    std::vector<Vec3> g(n, Vec3{0,0,0});

    // (1) 幾何エネルギー（長さ+曲げ+ピン+外力）を中心差分
    const double h = 1e-6;
    for(int dof=0; dof<n; dof++){
        if(fixed[dof]) continue;
        for(int a=0; a<3; a++){
            ChainDesign dp=d, dm=d;
            comp(dp.point(dof), a) += h;
            comp(dm.point(dof), a) -= h;
            comp(g[dof], a) += (energyGeom(dp,cp) - energyGeom(dm,cp)) / (2*h);
        }
    }

    // (2) 接触項（PHR）：∂/∂P = -coeff * ∂gap/∂P,  coeff=max(0, λ-μ(gap-dMin))
    Chain ch = d.build();
    for(const Contact& c : findSelfProximities(ch, R, cp.det)){
        double lam = ms ? ms->lookup(c) : 0.0, coeff;
        alContact(c.gap, cp.dMin, cp.wPenalty, lam, coeff);
        if(coeff <= 0) continue;
        ContactJacobian J = cp.radiusCoupling ? contactJacobianFull(ch, R, c)
                                              : contactJacobian(ch, c);
        for(const DOFGrad& dg : J.points){
            if(fixed[dg.dof]) continue;
            g[dg.dof] = g[dg.dof] + (-coeff) * dg.grad;
        }
    }
    return g;
}

// 接触集合から乗数を更新（PHR: λ⁺ = max(0, λ-μ g)）。返り値=新ストア。
MultStore updateMultipliers(const MultStore& old, const std::vector<Contact>& cs,
                            double dMin, double wPenalty, double window){
    MultStore ns; ns.window = window;
    double mu = 2.0*wPenalty;
    for(const Contact& c : cs){
        double g = c.gap - dMin;
        double lamN = std::max(0.0, old.lookup(c) - mu*g);
        if(lamN > 0) ns.recs.push_back({c.bodyA, c.bodyB, c.segA, c.segB, c.tA, c.tB, lamN});
    }
    return ns;
}

double maxViolation(const std::vector<Contact>& cs, double dMin){
    double v=0; for(const Contact& c:cs) v=std::max(v, dMin-c.gap); return v;
}

// ---- CCD（連続衝突：すり抜け＝トポロジー破壊の防止） ----
double minSignedClear(const std::vector<Contact>& cs){
    double m = 1e300;
    for(const Contact& c : cs) m = std::min(m, c.centerDist - c.rA - c.rB);
    return m;   // <0 なら貫通（チューブが重なっている）
}

// ---- シーン版（movable 1体 vs 固定 Body 群） ----

// 接触ペナルティ込み総エネルギー（シーン）
double phiFullScene(Scene sc, int mi, const ChainDesign& d, const CtrlParams& cp,
                    const MultStore* ms=nullptr){
    sc.bodies[mi].design = d; sc.bodies[mi].rebuild();
    double E = energyGeom(d, cp);                       // 幾何は movable のみ
    for(const Contact& c : findSceneProximities(sc, cp.det)){
        double lam = ms ? ms->lookup(c) : 0.0, coeff;
        E += alContact(c.gap, cp.dMin, cp.wPenalty, lam, coeff);
    }
    return E;
}

// 勾配（シーン）：幾何は FD（movable のみ）、接触項は解析（可動側のみ・PHR）
std::vector<Vec3> gradientScene(Scene& sc, int mi, const CtrlParams& cp,
                                const std::vector<char>& fixed,
                                const MultStore* ms=nullptr){
    const ChainDesign d = sc.bodies[mi].design;
    const int n = d.numDOF();
    std::vector<Vec3> g(n, Vec3{0,0,0});

    const double h = 1e-6;
    for(int dof=0; dof<n; dof++){
        if(fixed[dof]) continue;
        for(int a=0; a<3; a++){
            ChainDesign dp=d, dm=d;
            comp(dp.point(dof), a) += h;
            comp(dm.point(dof), a) -= h;
            comp(g[dof], a) += (energyGeom(dp,cp) - energyGeom(dm,cp)) / (2*h);
        }
    }

    sc.bodies[mi].design = d; sc.bodies[mi].rebuild();
    for(const Contact& c : findSceneProximities(sc, cp.det)){
        double lam = ms ? ms->lookup(c) : 0.0, coeff;
        alContact(c.gap, cp.dMin, cp.wPenalty, lam, coeff);
        if(coeff <= 0) continue;
        ContactJacobian J = cp.radiusCoupling
            ? contactJacobianSceneFull(sc.bodies[mi].chain, sc.bodies[mi].radius, mi, c)
            : contactJacobianScene(sc.bodies[mi].chain, mi, c);
        for(const DOFGrad& dg : J.points){
            if(fixed[dg.dof]) continue;
            g[dg.dof] = g[dg.dof] + (-coeff) * dg.grad;
        }
    }
    return g;
}

// ===== 座標降下(cd)の並列化サポート(L1=点内6試行並列 / L2=接触グラフ彩色ブロック Jacobi) =====
// 並列プリミティブ pipe_par::parallel_for は別 TU(cd_parallel.cpp)。

struct CDMove { bool found=false; int axis=0, sign=0; double pitch=0, newPhi=0; };

// dof の最良単軸移動を pitch 半減ラインサーチで探す。design は読み取り専用(ブロック Jacobi 用)。
// parTrials=true で各 pitch 段の 6 試行を並列評価(L1)。最良選択は直列と同順=結果一致。
static CDMove cdBestMove(const ChainDesign& design, int dof, double pitch0, double pitchMin,
                         double phi0, const std::function<double(const ChainDesign&)>& evalPhi,
                         bool parTrials, int nthreads){
    double pitch = pitch0;
    while(pitch > pitchMin){
        double pv[6];
        auto one = [&](int idx){
            int ax = idx/2, sg = (idx&1) ? 1 : -1;
            ChainDesign t = design;
            comp(t.point(dof), ax) += sg*pitch;
            pv[idx] = evalPhi(t);
        };
        if(parTrials) pipe_par::parallel_for(6, nthreads, one);
        else          for(int i=0;i<6;i++) one(i);
        double best=phi0; int bi=-1;
        for(int i=0;i<6;i++) if(pv[i] < best - 1e-9){ best=pv[i]; bi=i; }
        if(bi>=0){ CDMove m; m.found=true; m.axis=bi/2; m.sign=(bi&1)?1:-1; m.pitch=pitch; m.newPhi=best; return m; }
        pitch *= 0.5;
    }
    return CDMove{};
}

// 干渉グラフ彩色: 帯(|i-j|<=band の隣接/曲げ) + 接触クロス辺(seg s = 制御点 s,s+1)。固定 DOF は色 -1。
// 同色 = 相互に非干渉 → 同時更新しても直列と同値(長距離の弧長カップリングのみ無視する近似)。
static std::vector<int> cdColor(int n, const std::vector<char>& fixed, bool anyHard, int band,
                                const std::vector<std::array<int,2>>& contactSegs){
    std::vector<std::vector<int>> adj(n);
    auto addE = [&](int a,int b){ if(a>=0&&b>=0&&a<n&&b<n&&a!=b){ adj[a].push_back(b); adj[b].push_back(a); } };
    for(int i=0;i<n;i++) for(int d=1; d<=band; d++) addE(i, i+d);
    for(const auto& cs : contactSegs)
        for(int da=0; da<2; da++) for(int db=0; db<2; db++) addE(cs[0]+da, cs[1]+db);
    std::vector<int> color(n, -1), used;
    for(int i=0;i<n;i++){
        if(!(anyHard || !fixed[i])) continue;
        used.assign(adj[i].size()+1, 0);
        for(int j : adj[i]) if(color[j]>=0 && color[j]<(int)used.size()) used[color[j]]=1;
        int c=0; while(c<(int)used.size() && used[c]) c++;
        color[i]=c;
    }
    return color;
}

// 1 色(非干渉な dofs)のブロック Jacobi 1 ステップ。各 dof の最良単軸移動を返す。
// **点×6試行を一括並列**(粒度を細かく均一に)。各 dof は独立に pitch 半減・最初の改善を採用するので
// 結果は点ごとの cdBestMove と同一。並列度 = 6×(まだ探索中の dof 数)(=6 の倍数・最低 6)。
static std::vector<CDMove> cdColorMoves(const ChainDesign& design, const std::vector<int>& dofs,
                                        double pitch0, double pitchMin, double phiFrozen,
                                        const std::function<double(const ChainDesign&)>& evalPhi,
                                        int nthreads){
    int K = (int)dofs.size();
    std::vector<CDMove> res(K);
    std::vector<double> pitch(K, pitch0);
    std::vector<char> active(K, 1);
    for(;;){
        std::vector<int> act;                       // まだ探索中の dof のインデックス(res/pitch 用)
        for(int k=0;k<K;k++) if(active[k]) act.push_back(k);
        if(act.empty()) break;
        int M = (int)act.size();
        std::vector<double> pv(M*6);
        pipe_par::parallel_for(M*6, nthreads, [&](int idx){
            int a = idx/6, trial = idx%6;
            int k = act[a]; int ax = trial/2, sg = (trial&1)?1:-1;
            ChainDesign t = design;
            comp(t.point(dofs[k]), ax) += sg*pitch[k];
            pv[idx] = evalPhi(t);
        });
        for(int a=0;a<M;a++){                        // 各 dof が独立に判定(直列の cdBestMove と同順)
            int k = act[a];
            double best=phiFrozen; int bi=-1;
            for(int i=0;i<6;i++){ double v=pv[a*6+i]; if(v < best - 1e-9){ best=v; bi=i; } }
            if(bi>=0){ res[k]={true, bi/2, (bi&1)?1:-1, pitch[k], best}; active[k]=0; }
            else { pitch[k] *= 0.5; if(pitch[k] <= pitchMin) active[k]=0; }
        }
    }
    return res;
}

// 座標降下ドライバ(adjust / adjustScene 共用)。evalPhi=総エネルギー(スレッド安全)、
// contactSegsFn=現設計の接触を可動同士の (segA,segB) 群で返す(彩色用)。design を更新する。
static void cdRun(ChainDesign& design, int n, const std::vector<char>& fixed, bool anyHard,
                  const LinCon& L, const CtrlParams& cp, double& phi, int& it,
                  const std::function<double(const ChainDesign&)>& evalPhi,
                  const std::function<std::vector<std::array<int,2>>(const ChainDesign&)>& contactSegsFn){
    int nthreads = cp.cdThreads;
    if(nthreads <= 0){ unsigned hc = pipe_par::hw_threads(); nthreads = (hc>3) ? (int)hc-2 : 1; }
    for(int inner=0; inner<cp.maxIter; inner++, it++){
        bool moved=false;
        if(cp.cdParallel == 1){
            // L2: 彩色ブロック Jacobi。同色を並列更新、色間は Gauss-Seidel(色頭で基準 phi 再計算)。
            std::vector<int> color = cdColor(n, fixed, anyHard, 2, contactSegsFn(design));
            int maxC=-1; for(int c : color) maxC = std::max(maxC, c);
            for(int cc=0; cc<=maxC; cc++){
                std::vector<int> dofs;
                for(int i=0;i<n;i++) if(color[i]==cc) dofs.push_back(i);
                if(dofs.empty()) continue;
                double phiFrozen = evalPhi(design);
                // 点×6試行を一括並列(粒度を細かく・最低6並列)。結果は点ごとの直列ラインサーチと同一。
                std::vector<CDMove> mv = cdColorMoves(design, dofs, cp.cdPitch0, cp.cdPitchMin,
                                                      phiFrozen, evalPhi, nthreads);
                for(size_t k=0;k<dofs.size();k++) if(mv[k].found){
                    comp(design.point(dofs[k]), mv[k].axis) += mv[k].sign*mv[k].pitch; moved=true;
                }
            }
            phi = evalPhi(design);
        } else {
            // L1: 直列 Gauss-Seidel(点順)。各点の 6 試行のみ並列(スレッド有効時)。結果は直列と一致。
            for(int k=0;k<n;k++){
                int dof = cp.cdReverse ? (n-1-k) : k;
                if(!(anyHard || !fixed[dof])) continue;
                double phi0 = evalPhi(design);
                CDMove m = cdBestMove(design, dof, cp.cdPitch0, cp.cdPitchMin, phi0, evalPhi, nthreads>1, nthreads);
                if(m.found){ comp(design.point(dof), m.axis) += m.sign*m.pitch; phi=m.newPhi; moved=true; }
            }
        }
        if(anyHard) feasibilityProject(design, L);
        if(!moved) break;
    }
}

} // namespace

CtrlResult adjust(ChainDesign design, const RadiusFn& R, CtrlParams cp){
    cp.det.reportGap = std::max(cp.det.reportGap, cp.dMin);   // アクティブ集合が見える様に
    const int n = design.numDOF();

    // 硬拘束（固定＋硬ピン）があれば射影、なければ従来の固定DOFゼロ化
    bool anyHard=false; for(const Pin& pn:cp.pins) if(pn.hard) anyHard=true;
    LinCon L; double pinRes=0; bool feasible=true;
    std::vector<char> fixed(n,0);
    if(anyHard){
        L = buildConstraints(design, cp);
        pinRes = feasibilityProject(design, L);      // 初期を実行可能へ
        feasible = (pinRes < 1e-6);
    } else {
        for(int f:cp.fixedDOF) if(f>=0&&f<n) fixed[f]=1;
    }

    MultStore ms; ms.window = cp.alWindow;
    const int outerN = std::max(1, cp.alOuter);
    int it = 0;
    double phi = 0;
    for(int outer=0; outer<outerN; outer++){
        const MultStore* msp = (cp.alOuter>1) ? &ms : nullptr;
        phi = phiFull(design, R, cp, msp);
        if(cp.solver==1){
          // 座標降下（単一チェーン版・cdRun 共通ドライバ）
          auto evalPhi = [&](const ChainDesign& d){ return phiFull(d, R, cp, msp); };
          auto segsFn  = [&](const ChainDesign& d){
              std::vector<std::array<int,2>> out;
              Chain ch = d.build();
              for(const Contact& c : findSelfProximities(ch, R, cp.det)) out.push_back({c.segA, c.segB});
              return out;
          };
          cdRun(design, n, fixed, anyHard, L, cp, phi, it, evalPhi, segsFn);
        } else {
        for(int inner=0; inner<cp.maxIter; inner++, it++){
            std::vector<Vec3> g = gradient(design, R, cp, fixed, msp);
            if(anyHard) projectVec(g, L, false);     // 拘束の零空間へ

            double gmax = 0;
            for(const Vec3& v : g) gmax = std::max(gmax, norm(v));
            if(gmax < cp.tol) break;

            double alpha = cp.stepMax / gmax;
            bool ok = false;
            for(int ls=0; ls<30; ls++){
                ChainDesign trial = design;
                for(int dof=0; dof<n; dof++)
                    if(anyHard || !fixed[dof]) trial.point(dof) = trial.point(dof) - alpha*g[dof];
                double pt = phiFull(trial, R, cp, msp);
                bool safe = !cp.ccd || (cp.ccdConservative
                    ? motionSafeCA(design, trial, R, cp.det, cp.ccdMargin)
                    : motionSafe(design, trial, R, cp.det, cp.ccdSubsteps, cp.ccdMargin));
                if(pt < phi - 1e-12 && safe){
                    design = trial; phi = pt; ok = true; break;
                }
                alpha *= 0.5;
            }
            if(!ok) break;
            if(anyHard) feasibilityProject(design, L);
        }
        }
        if(cp.alOuter>1){   // 乗数更新（PHR）
            auto cs = findSelfProximities(design.build(), R, cp.det);
            ms = updateMultipliers(ms, cs, cp.dMin, cp.wPenalty, cp.alWindow);
        }
    }

    CtrlResult res;
    res.design = design;
    res.chain  = design.build();
    res.iters  = it;
    res.energy = phi;
    res.contacts = findSelfProximities(res.chain, R, cp.det);
    res.constraintsFeasible = feasible;
    res.pinResidual = pinRes;
    res.maxClearViolation = maxViolation(res.contacts, cp.dMin);
    return res;
}

CtrlResult adjustScene(Scene sc, int mi, CtrlParams cp){
    cp.det.reportGap = std::max(cp.det.reportGap, cp.dMin);
    sc.rebuildAll();

    ChainDesign design = sc.bodies[mi].design;
    const int n = design.numDOF();

    bool anyHard=false; for(const Pin& pn:cp.pins) if(pn.hard) anyHard=true;
    LinCon L; double pinRes=0; bool feasible=true;
    std::vector<char> fixed(n,0);
    if(anyHard){
        L = buildConstraints(design, cp);
        pinRes = feasibilityProject(design, L);
        feasible = (pinRes < 1e-6);
    } else {
        for(int f:cp.fixedDOF) if(f>=0&&f<n) fixed[f]=1;
    }

    MultStore ms; ms.window = cp.alWindow;
    const int outerN = std::max(1, cp.alOuter);
    int it = 0;
    double phi = 0;
    for(int outer=0; outer<outerN; outer++){
        const MultStore* msp = (cp.alOuter>1) ? &ms : nullptr;
        sc.bodies[mi].design = design; sc.bodies[mi].rebuild();
        phi = phiFullScene(sc, mi, design, cp, msp);
        if(cp.solver==1){
          // 座標降下（シーン版・cdRun 共通ドライバ）。evalPhi は phiFullScene が sc を値コピーするのでスレッド安全。
          auto evalPhi = [&](const ChainDesign& d){ return phiFullScene(sc, mi, d, cp, msp); };
          auto segsFn  = [&](const ChainDesign& d){
              Scene s2 = sc; s2.bodies[mi].design = d; s2.bodies[mi].rebuild();
              std::vector<std::array<int,2>> out;
              for(const Contact& c : findSceneProximities(s2, cp.det))
                  if(c.bodyA==mi && c.bodyB==mi) out.push_back({c.segA, c.segB});   // 可動同士の自己接触のみ
              return out;
          };
          cdRun(design, n, fixed, anyHard, L, cp, phi, it, evalPhi, segsFn);
          sc.bodies[mi].design = design; sc.bodies[mi].rebuild();   // 後段(乗数更新等)のため sc を同期
        } else {
        for(int inner=0; inner<cp.maxIter; inner++, it++){
            sc.bodies[mi].design = design; sc.bodies[mi].rebuild();
            std::vector<Vec3> g = gradientScene(sc, mi, cp, fixed, msp);
            if(anyHard) projectVec(g, L, false);

            double gmax = 0;
            for(const Vec3& v : g) gmax = std::max(gmax, norm(v));
            if(gmax < cp.tol) break;

            double alpha = cp.stepMax / gmax;
            bool ok = false;
            for(int ls=0; ls<30; ls++){
                ChainDesign trial = design;
                for(int dof=0; dof<n; dof++)
                    if(anyHard || !fixed[dof]) trial.point(dof) = trial.point(dof) - alpha*g[dof];
                double pt = phiFullScene(sc, mi, trial, cp, msp);
                bool safe = !cp.ccd || (cp.ccdConservative
                    ? motionSafeSceneCA(sc, mi, design, trial, cp.det, cp.ccdMargin)
                    : motionSafeScene(sc, mi, design, trial, cp.det, cp.ccdSubsteps, cp.ccdMargin));
                if(pt < phi - 1e-12 && safe){
                    design = trial; phi = pt; ok = true; break;
                }
                alpha *= 0.5;
            }
            if(!ok) break;
            if(anyHard) feasibilityProject(design, L);
        }
        }
        if(cp.alOuter>1){
            sc.bodies[mi].design = design; sc.bodies[mi].rebuild();
            auto cs = findSceneProximities(sc, cp.det);
            ms = updateMultipliers(ms, cs, cp.dMin, cp.wPenalty, cp.alWindow);
        }
    }

    sc.bodies[mi].design = design; sc.bodies[mi].rebuild();
    CtrlResult res;
    res.design = design;
    res.chain  = sc.bodies[mi].chain;
    res.iters  = it;
    res.energy = phi;
    res.contacts = findSceneProximities(sc, cp.det);
    res.constraintsFeasible = feasible;
    res.pinResidual = pinRes;
    res.maxClearViolation = maxViolation(res.contacts, cp.dMin);
    return res;
}

bool motionSafe(const ChainDesign& from, const ChainDesign& to, const RadiusFn& R,
                const Params& det, int substeps, double margin){
    const int n = from.numDOF();
    for(int s=1; s<=substeps; s++){
        double tau = (double)s/substeps;
        ChainDesign d = from;
        for(int i=0;i<n;i++) d.point(i) = from.point(i)*(1-tau) + to.point(i)*tau;
        if(minSignedClear(findSelfProximities(d.build(), R, det)) < margin) return false;
    }
    return true;
}

bool motionSafeScene(Scene sc, int mi,
                     const ChainDesign& from, const ChainDesign& to,
                     const Params& det, int substeps, double margin){
    const int n = from.numDOF();
    for(int s=1; s<=substeps; s++){
        double tau = (double)s/substeps;
        ChainDesign d = from;
        for(int i=0;i<n;i++) d.point(i) = from.point(i)*(1-tau) + to.point(i)*tau;
        sc.bodies[mi].design = d; sc.bodies[mi].rebuild();
        if(minSignedClear(findSceneProximities(sc, det)) < margin) return false;
    }
    return true;
}

// ---- 保守的前進（conservative advancement）：見逃しのない CCD ----
// 各体の半径を「範囲内の最大」で上界 → 保守的クリアランス centerDist-rMaxA-rMaxB は
// 真のクリアランス以下。中心線点の速度は制御点変位の最大で上界（凸結合）。
// よって安全刻み Δτ = (保守クリアランス - margin)/V で進めれば衝突を跨がない。
namespace {
double bodyRadiusMax(const RadiusFn& R, double Lmax){
    double rmax = 0; const int M = 64;
    for(int k=0;k<=M;k++){ auto r = R(Lmax * (double)k/M); if(r) rmax = std::max(rmax, *r); }
    return rmax;
}
double maxCtrlDisp(const ChainDesign& a, const ChainDesign& b){
    double m=0; for(int i=0;i<a.numDOF();i++) m=std::max(m, norm(b.point(i)-a.point(i)));
    return m;
}
}

bool motionSafeCA(const ChainDesign& from, const ChainDesign& to, const RadiusFn& R,
                  const Params& det, double margin){
    double Lmax = 1.5*std::max(from.build().totalLen(), to.build().totalLen());
    double rMax = bodyRadiusMax(R, Lmax);
    double V = 2.0*maxCtrlDisp(from,to);          // 自己: 両側が動く → ×2
    if(V < 1e-12) return true;                     // 動かない＝安全
    const int n=from.numDOF();
    double tau=0; int guard=0;
    while(tau < 1.0 && guard++ < 100000){
        ChainDesign d=from;
        for(int i=0;i<n;i++) d.point(i)= from.point(i)*(1-tau)+to.point(i)*tau;
        double minCons=1e300;
        for(const Contact& c : findSelfProximities(d.build(), R, det))
            minCons = std::min(minCons, c.centerDist - 2.0*rMax);
        if(minCons <= margin) return false;        // TOI（保守側で接触）
        tau += (minCons - margin)/V;
    }
    return true;
}

bool motionSafeSceneCA(Scene sc, int mi,
                       const ChainDesign& from, const ChainDesign& to,
                       const Params& det, double margin){
    const int nb=(int)sc.bodies.size();
    std::vector<double> rMax(nb,0);
    for(int b=0;b<nb;b++){
        double L = (b==mi) ? 1.5*std::max(from.build().totalLen(), to.build().totalLen())
                           : sc.bodies[b].chain.totalLen();
        rMax[b] = bodyRadiusMax(sc.bodies[b].radius, std::max(L,1e-9));
    }
    double V = 2.0*maxCtrlDisp(from,to);
    if(V < 1e-12) return true;
    const int n=from.numDOF();
    double tau=0; int guard=0;
    while(tau < 1.0 && guard++ < 100000){
        ChainDesign d=from;
        for(int i=0;i<n;i++) d.point(i)= from.point(i)*(1-tau)+to.point(i)*tau;
        sc.bodies[mi].design=d; sc.bodies[mi].rebuild();
        double minCons=1e300;
        for(const Contact& c : findSceneProximities(sc, det))
            minCons = std::min(minCons, c.centerDist - rMax[c.bodyA] - rMax[c.bodyB]);
        if(minCons <= margin) return false;
        tau += (minCons - margin)/V;
    }
    return true;
}

} // namespace pipe

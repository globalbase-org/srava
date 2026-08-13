// 回帰テスト（依存なしの軽量フレームワーク）。失敗数を終了コードに返す。
#include "pipe/bezier.hpp"
#include "pipe/radius.hpp"
#include "pipe/proximity.hpp"
#include "pipe/gradient.hpp"
#include "pipe/controller.hpp"
#include "pipe/scene.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
#include <optional>

using namespace pipe;

static int g_fail = 0;
#define CHECK(cond, msg) do{ if(!(cond)){ \
    printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++g_fail; } \
    else printf("  ok  : %s\n", msg); }while(0)
#define CHECK_NEAR(a,b,tol,msg) do{ double _d=std::abs((a)-(b)); \
    if(_d>(tol)){ printf("  FAIL: %s  |%.3e-%.3e|=%.3e > %.1e (%s:%d)\n", \
        msg,(double)(a),(double)(b),_d,(double)(tol),__FILE__,__LINE__); ++g_fail; } \
    else printf("  ok  : %s (diff=%.2e)\n", msg, _d); }while(0)

// テスト用: 自分自身に近づいて戻る開いた鎖
static Chain makeCoil(){
    const int K=10; const double Rc=5.0; const double PI=3.14159265358979;
    std::vector<Vec3> C;
    for(int k=0;k<K;k++){
        double a = 2*PI * (0.05 + 0.92*k/(K-1));
        C.push_back({Rc*std::cos(a), Rc*std::sin(a), 0.15*k});
    }
    Vec3 S = {Rc+0.3, 0.0, -0.2};
    Vec3 E = {Rc*std::cos(2*PI*0.99), Rc*std::sin(2*PI*0.99), 1.5};
    return Chain::build(S, E, C);
}
static RadiusFn coilRadius(const Chain& ch){
    double Smax = ch.totalLen();
    return [=](double s)->std::optional<double>{
        if(s<0.0||s>Smax) return std::nullopt;
        return 0.4*std::exp(0.015*s);
    };
}

// ---- 個別テスト ----
static void test_arclength(){
    printf("[arc length]\n");
    // 直線 1 セグメント: 長さ = ユークリッド距離
    Chain ch = Chain::build({0,0,0}, {3,4,0}, {{1.5,2,0}});  // S,C,E 共線
    CHECK_NEAR(ch.totalLen(), 5.0, 1e-9, "straight segment length == |E-S|");
}

static void test_partition_of_unity(){
    printf("[segDesignWeights partition of unity]\n");
    for(int m=1;m<=5;m++) for(int seg=0;seg<m;seg++){
        auto w = segDesignWeights(m, seg);
        for(int l=0;l<3;l++){
            double sum=0; for(auto& p: w[l]) sum+=p.second;
            CHECK_NEAR(sum, 1.0, 1e-12, "weights sum to 1");
        }
    }
}

static void test_circleCircle_symmetry(){
    printf("[circleCircle symmetry]\n");
    Vec3 X{0,0,0}, TA = normalize(Vec3{0,0,1});
    Vec3 Y{3,0,1}, TB = normalize(Vec3{0,1,1});
    CC ab = circleCircle(X,TA,0.5, Y,TB,0.7);
    CC ba = circleCircle(Y,TB,0.7, X,TA,0.5);
    CHECK_NEAR(ab.dist, ba.dist, 1e-9, "dist(A,B)==dist(B,A)");
    CHECK_NEAR(norm(ab.pA-ba.pB), 0.0, 1e-7, "pA(A,B)==pB(B,A)");
    CHECK_NEAR(norm(ab.pB-ba.pA), 0.0, 1e-7, "pB(A,B)==pA(B,A)");
}

static void test_bvh_equals_brute(){
    printf("[BVH == brute]\n");
    Chain ch = makeCoil(); RadiusFn R = coilRadius(ch);
    Params p; p.reportGap=2.0; p.gridN=11;
    p.useBVH=true;  auto a = findSelfProximities(ch,R,p);
    p.useBVH=false; auto b = findSelfProximities(ch,R,p);
    CHECK(a.size()==b.size(), "same contact count");
    size_t n = std::min(a.size(), b.size());
    double maxd=0;
    for(size_t i=0;i<n;i++) maxd=std::max(maxd, std::abs(a[i].gap-b[i].gap));
    CHECK_NEAR(maxd, 0.0, 1e-9, "matching gaps (sorted)");
}

static void test_jacobian_fd(){
    printf("[Jacobian vs finite difference]\n");
    const int K=10; const double Rc=5.0; const double PI=3.14159265358979;
    std::vector<Vec3> C;
    for(int k=0;k<K;k++){ double a=2*PI*(0.05+0.92*k/(K-1));
        C.push_back({Rc*std::cos(a), Rc*std::sin(a), 0.15*k}); }
    Vec3 S{Rc+0.3,0,-0.2}, E{Rc*std::cos(2*PI*0.99), Rc*std::sin(2*PI*0.99), 1.5};
    Chain ch = Chain::build(S,E,C); RadiusFn R = coilRadius(ch);

    Params p; p.reportGap=2.0; p.gridN=11;
    auto cs = findSelfProximities(ch,R,p);
    CHECK(!cs.empty(), "found at least one contact");
    if(cs.empty()) return;
    const Contact& c0 = cs[0];
    ContactJacobian J = contactJacobian(ch, c0);

    auto rebuild=[&](int dof, Vec3 d)->Chain{
        Vec3 s2=S,e2=E; std::vector<Vec3> c2=C;
        if(dof==0) s2=s2+d; else if(dof==(int)C.size()+1) e2=e2+d; else c2[dof-1]=c2[dof-1]+d;
        return Chain::build(s2,e2,c2);
    };
    // (A) 厳密: 解析が計算している量 = n̂·(X_A - X_B)（中心移動）の FD と機械精度一致
    auto centerProj=[&](const Chain& cc)->double{
        Vec3 X=cc.segs[c0.segA].at(c0.tA), Y=cc.segs[c0.segB].at(c0.tB);
        return dot(c0.normal, X - Y);
    };
    // (B) 近似品質: 真の表面 gap（円-円再解）の FD と ~1% 一致
    auto gapGeom=[&](const Chain& cc)->double{
        Vec3 X=cc.segs[c0.segA].at(c0.tA), Y=cc.segs[c0.segB].at(c0.tB);
        Vec3 TA=normalize(cc.segs[c0.segA].deriv(c0.tA)), TB=normalize(cc.segs[c0.segB].deriv(c0.tB));
        return circleCircle(X,TA,c0.rA,Y,TB,c0.rB).dist;
    };
    const double eps=1e-5;
    double maxApproxErr=0;
    for(auto& g: J.points){
        for(int a=0;a<3;a++){
            Vec3 d{a==0?eps:0,a==1?eps:0,a==2?eps:0};
            double an=a==0?g.grad.x:a==1?g.grad.y:g.grad.z;
            double fdC=(centerProj(rebuild(g.dof,d))-centerProj(rebuild(g.dof,d*-1)))/(2*eps);
            CHECK_NEAR(an, fdC, 1e-6, "analytic == center-projection FD (exact)");
            double fdG=(gapGeom(rebuild(g.dof,d))-gapGeom(rebuild(g.dof,d*-1)))/(2*eps);
            maxApproxErr=std::max(maxApproxErr, std::abs(an-fdG));
        }
    }
    // 真の gap 微分との差（落とした方向項）が小さいこと
    CHECK(maxApproxErr < 1e-2, "geometry-Jacobian approximates true gap-grad within 1e-2");
    printf("  (max |analytic - true-gap FD| = %.2e)\n", maxApproxErr);
    CHECK_NEAR(J.dGap_drA, -1.0, 2e-2, "dGap/drA ~= -1");
    CHECK_NEAR(J.dGap_drB, -1.0, 2e-2, "dGap/drB ~= -1");
}

static void test_controller(){
    printf("[controller: penalty pushes gap toward dMin]\n");
    const int K=10; const double Rc=5.0; const double PI=3.14159265358979;
    ChainDesign d;
    for(int k=0;k<K;k++){ double a=2*PI*(0.05+0.92*k/(K-1));
        d.C.push_back({Rc*std::cos(a), Rc*std::sin(a), 0.15*k}); }
    d.S = {Rc+0.3,0,-0.2};
    d.E = {Rc*std::cos(2*PI*0.99), Rc*std::sin(2*PI*0.99), 1.5};
    Vec3 S0=d.S, E0=d.E;
    RadiusFn R = [](double s)->std::optional<double>{
        if(s<0) return std::nullopt; return 0.4*std::exp(0.015*s); };

    CtrlParams cp;
    cp.dMin=0.9; cp.wLen=0.5; cp.wBend=0.2; cp.wPenalty=2e3;
    cp.fixedDOF={0, (int)d.C.size()+1};   // S と E を固定
    cp.stepMax=0.04; cp.maxIter=400; cp.det.gridN=11; cp.det.reportGap=0.9;

    auto before = findSelfProximities(d.build(), R, cp.det);
    double minBefore = before.empty()?1e9:before.front().gap;
    auto res = adjust(d, R, cp);
    double minAfter = res.contacts.empty()?1e9:res.contacts.front().gap;

    printf("  minGap %.4f -> %.4f (dMin=%.2f), iters=%d\n",
           minBefore, minAfter, cp.dMin, res.iters);
    CHECK(minAfter > minBefore, "min gap increased");
    CHECK(minAfter > cp.dMin - 0.15, "min gap reached near dMin");
    CHECK_NEAR(norm(res.design.S - S0), 0.0, 1e-12, "fixed S unchanged");
    CHECK_NEAR(norm(res.design.E - E0), 0.0, 1e-12, "fixed E unchanged");
}

static void test_external_force(){
    printf("[external force: toward-origin gathers the curve]\n");
    const int K=10; const double Rc=5.0; const double PI=3.14159265358979;
    ChainDesign d;
    for(int k=0;k<K;k++){ double a=2*PI*(0.05+0.92*k/(K-1));
        d.C.push_back({Rc*std::cos(a), Rc*std::sin(a), 0.15*k}); }
    d.S = {Rc+0.3,0,-0.2};
    d.E = {Rc*std::cos(2*PI*0.99), Rc*std::sin(2*PI*0.99), 1.5};
    RadiusFn R = [](double s)->std::optional<double>{
        if(s<0) return std::nullopt; return 0.4*std::exp(0.015*s); };
    // 自由制御点の平均原点距離
    auto meanR=[&](const ChainDesign& g){ double s=0; int n=0;
        for(int i=1;i<=(int)g.C.size();i++){ s+=norm(g.C[i-1]); n++; } return s/n; };

    CtrlParams cp;
    cp.dMin=0.9; cp.wLen=0.2; cp.wBend=0.2; cp.wPenalty=2e3;
    cp.fOrigin=0.15;                          // 原点へ束ねる
    cp.fixedDOF={0,(int)d.C.size()+1};
    cp.stepMax=0.04; cp.maxIter=400; cp.det.gridN=11; cp.det.reportGap=0.9;

    double before = meanR(d);
    auto res = adjust(d, R, cp);
    double after = meanR(res.design);
    printf("  mean |C| (origin dist): %.4f -> %.4f\n", before, after);
    CHECK(after < before, "toward-origin force gathered the curve");
    double minAfter = res.contacts.empty()?1e9:res.contacts.front().gap;
    CHECK(minAfter > cp.dMin - 0.2, "clearance still ~maintained under force");
}

static void test_scene(){
    printf("[scene: movable line pushed off a fixed line]\n");
    auto rconst = [](double s)->std::optional<double>{
        if(s<0) return std::nullopt; return 0.3; };
    Scene sc;
    Body fixed; fixed.movable=false;            // body 0: 固定直線 z=0
    fixed.design.S={-5,0,0}; fixed.design.E={5,0,0}; fixed.design.C={{0,0,0}};
    fixed.radius=rconst;
    Body mov; mov.movable=true;                  // body 1: 可動直線 z=0.8
    mov.design.S={-5,0,0.8}; mov.design.E={5,0,0.8}; mov.design.C={{0,0,0.8}};
    mov.radius=rconst;
    sc.bodies={fixed,mov};
    sc.rebuildAll();

    Params det; det.gridN=11; det.reportGap=1.0;
    auto before = findSceneProximities(sc, det);
    bool hasCross=false; double gb=1e9;
    for(auto& c: before) if(c.bodyA!=c.bodyB){ hasCross=true; gb=std::min(gb,c.gap); }
    CHECK(hasCross, "cross-body contact detected");
    printf("  before cross min gap = %.4f\n", gb);

    CtrlParams cp; cp.dMin=0.5; cp.wLen=0.0; cp.wBend=0.01; cp.wPenalty=2e3;
    cp.stepMax=0.02; cp.maxIter=400; cp.det.gridN=11; cp.det.reportGap=1.0;
    auto res = adjustScene(sc, 1, cp);
    double ga=1e9; for(auto& c: res.contacts) if(c.bodyA!=c.bodyB) ga=std::min(ga,c.gap);
    printf("  after  cross min gap = %.4f (dMin=%.2f), iters=%d, movable S.z=%.3f\n",
           ga, cp.dMin, res.iters, res.design.S.z);
    CHECK(ga > gb, "cross gap increased");
    CHECK(ga > cp.dMin - 0.12, "cross gap reached near dMin");
    CHECK(res.design.S.z > 0.8, "movable line pushed away (z increased)");

    // Scene BVH == brute（接近を 2 体で複数作る配置で照合）
    Scene s2;
    Body f2; f2.movable=false; f2.design.S={-5,0,0}; f2.design.E={5,0,0};
    f2.design.C={{0,0,0}}; f2.radius=rconst;
    Body m2; m2.movable=true;  m2.design.S={-5,0,0.7}; m2.design.E={5,0,0.7};
    m2.design.C={{-2,0,0.7},{0,1,0.7},{2,0,0.7}}; m2.radius=rconst;
    s2.bodies={f2,m2}; s2.rebuildAll();
    Params db; db.gridN=11; db.reportGap=1.0; db.useBVH=false;
    Params dv=db; dv.useBVH=true;
    auto cb=findSceneProximities(s2,db), cv=findSceneProximities(s2,dv);
    CHECK(cb.size()==cv.size(), "scene BVH == brute (count)");
    double md=0; size_t nn=std::min(cb.size(),cv.size());
    for(size_t i=0;i<nn;i++) md=std::max(md,std::abs(cb[i].gap-cv[i].gap));
    CHECK_NEAR(md,0.0,1e-9,"scene BVH == brute (gaps)");
}

static void test_hard_pin(){
    printf("[hard pin: through-point held exactly]\n");
    const int K=10; const double Rc=5.0; const double PI=3.14159265358979;
    ChainDesign d;
    for(int k=0;k<K;k++){ double a=2*PI*(0.05+0.92*k/(K-1));
        d.C.push_back({Rc*std::cos(a), Rc*std::sin(a), 0.15*k}); }
    d.S={Rc+0.3,0,-0.2}; d.E={Rc*std::cos(2*PI*0.99),Rc*std::sin(2*PI*0.99),1.5};
    RadiusFn R=[](double s)->std::optional<double>{ if(s<0)return std::nullopt; return 0.4*std::exp(0.015*s); };

    CtrlParams cp; cp.dMin=0.9; cp.wLen=0.5; cp.wBend=0.2; cp.wPenalty=2e3;
    cp.fixedDOF={0,(int)d.C.size()+1};
    Vec3 target{1.0, 1.0, 0.5};
    cp.pins.push_back({3, target, true});          // 中点 M_3 を厳密に target へ
    cp.stepMax=0.04; cp.maxIter=400; cp.det.gridN=11; cp.det.reportGap=0.9;

    auto res = adjust(d, R, cp);
    Vec3 M3 = (res.design.C[3]+res.design.C[4])*0.5;
    printf("  M3 = (%.4f,%.4f,%.4f) target (%.2f,%.2f,%.2f), |err|=%.2e, feasible=%d\n",
           M3.x,M3.y,M3.z, target.x,target.y,target.z, norm(M3-target), (int)res.constraintsFeasible);
    CHECK_NEAR(norm(M3 - target), 0.0, 1e-6, "hard pin satisfied exactly");
    CHECK(res.constraintsFeasible, "constraints feasible");

    // 過拘束（実行不能）検出: 同じ中点に 2 つの異なる硬ピン
    CtrlParams cp2=cp; cp2.pins.clear();
    cp2.pins.push_back({3, Vec3{1,1,0.5}, true});
    cp2.pins.push_back({3, Vec3{2,0,0.5}, true});  // 矛盾
    auto res2 = adjust(d, R, cp2);
    printf("  conflicting pins: feasible=%d residual=%.3e\n",
           (int)res2.constraintsFeasible, res2.pinResidual);
    CHECK(!res2.constraintsFeasible, "infeasibility detected for conflicting pins");
}

static void test_radius_coupling(){
    printf("[radius coupling: full Jacobian beats base under steep taper]\n");
    const int K=10; const double Rc=5.0; const double PI=3.14159265358979;
    std::vector<Vec3> C;
    for(int k=0;k<K;k++){ double a=2*PI*(0.05+0.92*k/(K-1));
        C.push_back({Rc*std::cos(a), Rc*std::sin(a), 0.15*k}); }
    Vec3 S{Rc+0.3,0,-0.2}, E{Rc*std::cos(2*PI*0.99),Rc*std::sin(2*PI*0.99),1.5};
    Chain ch=Chain::build(S,E,C);
    // 急テーパー（dr/ds 大）→ 弧長カップリングが効く
    RadiusFn R=[](double s)->std::optional<double>{ if(s<0)return std::nullopt; return 0.25*std::exp(0.06*s); };

    Params p; p.reportGap=2.0; p.gridN=11;
    auto cs=findSelfProximities(ch,R,p);
    CHECK(!cs.empty(), "found a contact");
    if(cs.empty()) return;
    const Contact& c0=cs[0];

    int n=numDesignPoints(ch);
    auto dense=[&](const ContactJacobian& J){ std::vector<Vec3> v(n,Vec3{0,0,0});
        for(auto&pt:J.points) v[pt.dof]=pt.grad; return v; };
    auto base=dense(contactJacobian(ch,c0));
    auto full=dense(contactJacobianFull(ch,R,c0));

    // FD: 凍結 (segA,tA),(segB,tB) で evalGapAt（半径は R(arcAt) で弧長変化込み）
    auto rebuild=[&](int dof, Vec3 d)->Chain{
        Vec3 s2=S,e2=E; std::vector<Vec3> c2=C;
        if(dof==0) s2=s2+d; else if(dof==(int)C.size()+1) e2=e2+d; else c2[dof-1]=c2[dof-1]+d;
        return Chain::build(s2,e2,c2);
    };
    const double h=1e-5; double errBase=0, errFull=0;
    for(int dof=0;dof<n;dof++) for(int a=0;a<3;a++){
        Vec3 d{a==0?h:0,a==1?h:0,a==2?h:0};
        double gp=evalGapAt(rebuild(dof,d),  R, c0.segA,c0.tA,c0.segB,c0.tB).gap;
        double gm=evalGapAt(rebuild(dof,d*-1),R, c0.segA,c0.tA,c0.segB,c0.tB).gap;
        double fd=(gp-gm)/(2*h);
        errBase += std::abs(comp(base[dof],a)-fd);
        errFull += std::abs(comp(full[dof],a)-fd);
    }
    printf("  sum|J-FD|: base=%.4e  full=%.4e\n", errBase, errFull);
    CHECK(errFull < errBase, "full Jacobian closer to FD than base (coupling helps)");
}

static void test_ccd(){
    printf("[CCD: detects mid-motion tunneling]\n");
    auto rconst=[](double s)->std::optional<double>{ if(s<0)return std::nullopt; return 0.3; };
    Scene sc;
    Body fx; fx.movable=false; fx.design.S={-5,0,0}; fx.design.E={5,0,0};
    fx.design.C={{0,0,0}}; fx.radius=rconst;
    Body mv; mv.movable=true; mv.design.S={-5,0,1}; mv.design.E={5,0,1};
    mv.design.C={{0,0,1}}; mv.radius=rconst;
    sc.bodies={fx,mv}; sc.rebuildAll();
    Params det; det.gridN=11; det.reportGap=1.0;

    ChainDesign from = mv.design;                 // z=+1（固定線の上、gap>0）
    ChainDesign through = from;                    // z=-1（反対側、gap>0）だが途中で貫通
    through.S.z=-1; through.E.z=-1; through.C[0].z=-1;
    CHECK(!motionSafeScene(sc,1, from,through, det,16,0.0), "tunneling motion flagged unsafe");

    ChainDesign small = from;                      // z=+1 -> +1.1（安全）
    small.S.z=1.1; small.E.z=1.1; small.C[0].z=1.1;
    CHECK(motionSafeScene(sc,1, from,small, det,16,0.0), "small move flagged safe");

    // 保守的前進版（見逃しなし）
    CHECK(!motionSafeSceneCA(sc,1, from,through, det,0.0), "CA: tunneling flagged unsafe");
    CHECK(motionSafeSceneCA(sc,1, from,small, det,0.0),     "CA: small move flagged safe");
}

static void test_augmented_lagrangian(){
    printf("[augmented Lagrangian: clearance violation -> ~0]\n");
    auto rconst=[](double s)->std::optional<double>{ if(s<0)return std::nullopt; return 0.3; };
    auto makeScene=[&](){ Scene sc;
        Body fx; fx.movable=false; fx.design.S={-5,0,0}; fx.design.E={5,0,0};
        fx.design.C={{0,0,0}}; fx.radius=rconst;                 // 固定線 x軸 z=0
        Body mv; mv.movable=true;  mv.design.S={-2,0,1.2}; mv.design.E={2,0,1.2};
        mv.design.C={{0,0,0.9}}; mv.radius=rconst;               // 可動弧（両端固定・中央自由）
        sc.bodies={fx,mv}; sc.rebuildAll(); return sc; };

    // 重力で可動弧の中央(唯一の自由DOF)を固定線へ押し付ける → 単一接触で AL が効く
    CtrlParams base; base.dMin=0.5; base.wLen=0.0; base.wBend=0.01; base.wPenalty=100;
    base.fZ=-2.0; base.fixedDOF={0,2};   // 可動の S,E 固定（中央 C0=dof1 のみ自由）
    base.stepMax=0.01; base.maxIter=600; base.det.gridN=15; base.det.reportGap=1.0;

    auto pen = adjustScene(makeScene(),1, base);            // 純ペナルティ
    CtrlParams al=base; al.alOuter=8; al.maxIter=150;        // 拡張ラグランジュ
    auto res = adjustScene(makeScene(),1, al);

    printf("  clearance violation: penalty=%.4e  AL=%.4e\n",
           pen.maxClearViolation, res.maxClearViolation);
    CHECK(pen.maxClearViolation > 5e-3, "penalty leaves a measurable violation");
    CHECK(res.maxClearViolation < 0.4*pen.maxClearViolation, "AL cuts violation >=2.5x");
    CHECK(res.maxClearViolation < 0.01, "AL drives violation near 0");
}

int main(){
    test_arclength();
    test_partition_of_unity();
    test_circleCircle_symmetry();
    test_bvh_equals_brute();
    test_jacobian_fd();
    test_controller();
    test_external_force();
    test_scene();
    test_hard_pin();
    test_radius_coupling();
    test_ccd();
    test_augmented_lagrangian();
    printf("\n%s (%d failure(s))\n", g_fail==0?"ALL PASS":"FAILED", g_fail);
    return g_fail==0 ? 0 : 1;
}

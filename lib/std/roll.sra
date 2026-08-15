// roll.sra — 螺旋巻きつけ(トロッカス v2 BLH)アルゴリズムの関数ライブラリ。
//   利用側は include "std/roll.sra";。ドライバ例は examples/roll_sample.sra。
//   リファレンス: docs/srava_roll_reference.md。
//
// ★必須モジュール: pipe_proximity
//   **本ライブラリは std で唯一、モジュールに依存する**(他の std は幾何カーネルだけで動く)。
//   ロードされているかは `srava --modules` の loaded に pipe_proximity が出るかで確認できる。
//   無い場合は -DSRAVA_MODULE_PIPEPROX=ON でビルドし直す(既定 ON)。
//
// ★推奨 params(知らないと事故る 2 つ):
//   parallel: 1   … 事実上必須。0 だと 1 solve が ~150 秒かかり、実用サイズで 1 時間級になる。
//   wSpace:   0.1 … 区間長の正則化。入れないと自由尾の制御点が 1 点に寄り、gap が 1mm 級まで潰れる。
//   例: {maxIter:100, threads:21, parallel:1, wSpace:0.1}
//
// 公開関数:
//   roll_initial(core, pipe, d, N, L, params)                       → {ctrl, radius, movable, raw, pp0}
//   roll_step   (core, pipe, d, N, L, beta, alpha, params, fz, fax) → {ok, ctrl, ...} / {ok:0, err, ctrl}
//   respace(ctrl, radius, npts) / respace_range(ctrl,radius,lo,hi)  … 制御点の等弧長整形ツール(任意)
//
// 規約(重要):
//   - pipe.radius=[r,m](指数ホーン: 半径(s)=r*exp(m*弧長))→ 基準径 rp=pipe.radius[0]。core.radius はスカラ。
//   - 螺旋半径 rho=rp+rc+d、ピッチ(z/周) Pz=2*rp+d。螺旋部・尾部とも z=Pz*i/N。
//   - 緩和は pipe_scene_adjust。solver "cd"、fixEnds:1、dMin:d は solve が強制。等間隔化は params.wSpace。
//   - 外力 fz(重力,z,負=下)/fax(締込,z軸へ,正=内)は roll_step のみに渡す(roll_initial は 0,0 で無重力)。
//   - 角度単位: alpha,beta は「度」。gamma は atan2(ラジアン)、方位は gamma+rad(beta)。
include "std/math.sra";
include "std/curve.sra";   // arclen

// ---- 小物 ----
var cross3 = \(a, b){
    [ a[1]*b[2] - a[2]*b[1],
      a[2]*b[0] - a[0]*b[2],
      a[0]*b[1] - a[1]*b[0] ];
};
var clamp1 = \(c){ if (c > 1.0) { return 1.0; } if (c < -1.0) { return -1.0; } c; };
var zdist  = \(p){ sqrt(p[0]*p[0] + p[1]*p[1]); };   // z軸距離(xy 射影の原点距離)

// ---- respace: 制御点を今の中心線(連続3点ベジエ=pipe_sample が忠実再現)上で等弧長に取り直す整形ツール ----
//   dense 曲線点を「ちょうど npts 点・等弧長・端点保持・重複なし」に再サンプル(線形補間)。
var resample_n = \(dense, npts){
    var n = length(dense); var S = arclen(dense); var total = S[n - 1];
    if ( total < 0.000001 ) { return dense; }
    var out = []; var k;
    for ( k = 0 ; k < npts ; k = k + 1 ) {
        var target = total * k / (npts - 1);
        var j = 1; var q;
        for ( q = 1 ; q < n ; q = q + 1 ) { if ( S[q] < target ) { j = q + 1; } }
        if ( j > n - 1 ) { j = n - 1; }
        var seglen = S[j] - S[j - 1];
        var f = 0.0; if ( seglen > 0.000001 ) { f = (target - S[j - 1]) / seglen; }
        out = concat(out, [ vadd(dense[j - 1], vscale(vsub(dense[j], dense[j - 1]), f)) ]);
    }
    out;
};
var respace = \(ctrl, radius, npts){
    if ( length(ctrl) < 3 ) { return ctrl; }
    var dense = map(pipe_sample(ctrl, radius, 0), \(p){ p[0]; });
    resample_n(dense, npts);
};
var respace_range = \(ctrl, radius, lo, hi){   // [lo..hi] だけ等弧長化(同数・両端不動)
    if ( hi - lo < 2 ) { return ctrl; }
    var rseg = respace( slice(ctrl, lo, hi + 1), radius, hi - lo + 1 );
    concat( slice(ctrl, 0, lo), rseg, slice(ctrl, hi + 1, length(ctrl)) );
};

// ---- solve: 強制パラメータをその場でセットして緩和。fz/fax は呼び出し側が渡す ----
var solve = \(core, ctrl, radius, d, params, fz, fax){
    params.fixEnds = 1;
    params.dMin    = d;
    params.solver  = "cd";
    params.fZ      = fz;    // 重力(z, 負=下向き)
    params.fAxis   = fax;   // 軸方向(正=z軸へ=芯へ締込)
    var bodies = [ {ctrl: ctrl,      radius: radius,      movable: 1},
                   {ctrl: core.ctrl, radius: core.radius, movable: 0} ];
    pipe_scene_adjust(bodies, 0, params);
};

// ================================================================
// 1. roll_initial — 1周目 + 引っ張り尾を生成して無重力で最適化
// ================================================================
var roll_initial = \(core, pipe, d, N, L, params){
    var rp  = pipe.radius[0];
    var rc  = core.radius;
    var rho = rp + rc + d;      // 螺旋半径
    var Pz  = 2.0*rp + d;       // ピッチ(z/周)

    if (L <= rho) { print("  [roll_initial] WARN: L <= rho (", L, "<=", rho, ") 尾がほぼ0点。Lを大きく。"); }

    // 螺旋部 i=0..N : [rho*cos, rho*sin, Pz*i/N]
    var helix = map(range2(0, N + 1), \(i){
        var th = TAU * i / N;
        [ cos(th)*rho, sin(th)*rho, Pz * i / N ];
    });
    // 尾部 i=N+1.. : [rho, (2pi/N)*rho*(i-N), Pz*i/N] ; zdist>L の点を含めて終端
    var tail = [];
    var i = N + 1; var done = 0;
    for ( i = N + 1 ; done == 0 ; i = i + 1 ) {
        var y = (TAU / N) * rho * (i - N);
        tail  = concat(tail, [[ rho, y, Pz * i / N ]]);
        if ( sqrt(rho*rho + y*y) > L ) { done = 1; }
        if ( i > N + 5000 )            { done = 1; }   // 安全弁
    }

    var pp  = concat(helix, tail);
    print("  [roll_initial] rho=", rho, " Pz=", Pz, " ctrl=", length(pp), " (helix", N+1, "+ tail", length(tail), ")");
    params.fixed = [];          // 1周目は内部ピンなし(端は fixEnds)
    var res = solve(core, pp, pipe.radius, d, params, 0.0, 0.0);   // 無重力
    var out = { ctrl: res.ctrl, radius: pipe.radius, movable: 1, raw: res, pp0: pp };
    out;
};

// ================================================================
// 2. roll_step — 既存の巻きから beta 分だけ進めて再緩和(fz/fax の外力あり)
// ================================================================
var roll_step = \(core, pipe, d, N, L, beta, alpha, params, fz, fax){
    var ctrl = pipe.ctrl;
    var N0   = length(ctrl) - 1;

    // 末尾から後退探索。なす角 = ctrl[i] のz軸内向き垂線 [-x,-y,0] と ctrl[N0]-ctrl[i]。
    //   尾(放射外向き)=大角, 巻き部=小角。thi=90+alpha / tlo=90-alpha を初めて下回る点で解放帯を切る。
    var thi = 90.0 + alpha; var tlo = 90.0 - alpha;
    var k0 = -1; var k1 = -1;
    var i;
    for ( i = N0 - 1 ; i >= 1 ; i = i - 1 ) {
        var p  = ctrl[i];
        var rad_v = [ -p[0], -p[1], 0.0 ];
        var rl = vlen(rad_v);
        var v  = vsub(ctrl[N0], p);
        var vl = vlen(v);
        if ( rl > 0.000001 ) { if ( vl > 0.000001 ) {
            var ang = deg(acos(clamp1( vdot(rad_v, v) / (rl * vl) )));
            if ( k0 < 0 ) { if ( ang < thi ) { k0 = i; } }
            if ( k1 < 0 ) { if ( ang < tlo ) { k1 = i; } }
        } }
    }
    if ( k0 < 0 ) { print("  [roll_step] 尾リザーブ枯渇(k0未発見)→終了信号"); var e0 = { ok: 0, err: "reserve_depleted", ctrl: ctrl }; return e0; }
    if ( k1 < 1 ) { print("  [roll_step] 尾リザーブ枯渇(k1<1)→終了信号");      var e1 = { ok: 0, err: "reserve_depleted", ctrl: ctrl }; return e1; }
    var i0 = k0 + 1;   // 「一つ手前」(末尾側)
    var i1 = k1;
    if ( i0 > N0 ) { i0 = N0; }

    // 平面 S: ctrl[i1] のz軸垂線(ctrl[i1] とその足)と ctrl[i1-1] を含む平面
    var Pi1 = ctrl[i1];
    var F   = [ 0.0, 0.0, Pi1[2] ];
    var n   = cross3( vsub(F, Pi1), vsub(ctrl[i1 - 1], Pi1) );
    if ( vlen(n) < 0.000001 ) { print("  [roll_step] 平面S縮退→直前状態を返す(C-8)"); var e2 = { ok: 0, err: "planeS_degenerate", ctrl: ctrl }; return e2; }

    // 終点 P: 方位 gamma+beta・z軸距離 L の鉛直線 ∩ 平面S
    var gamma = atan2( ctrl[N0][1], ctrl[N0][0] );
    var phi   = gamma + rad(beta);
    var cx = cos(phi)*L; var cy = sin(phi)*L;
    if ( abs(n[2]) < 0.000001 ) { print("  [roll_step] 鉛直線がSと平行→解なし・直前状態を返す(C-9)"); var e3 = { ok: 0, err: "P_parallel", ctrl: ctrl }; return e3; }
    var t = ( vdot(n, Pi1) - n[0]*cx - n[1]*cy ) / n[2];
    var P = [ cx, cy, t ];

    // ctrl[i0]→P を直線・等間隔(xy射影間隔 2pi*L0/N)で敷設。P は越さない(旧 i0+1..N0 は破棄)
    var L0   = zdist( ctrl[i0] );
    var step = (TAU * L0) / N;
    var D    = vsub(P, ctrl[i0]);
    var proj = sqrt( D[0]*D[0] + D[1]*D[1] );
    var K    = floor( (proj - 0.000001) / step );
    if ( K < 0 ) { K = 0; }
    var mids = map(range2(1, K + 1), \(kk){ vadd(ctrl[i0], vscale(D, (kk*step)/proj)); });

    var newctrl = concat( slice(ctrl, 0, i0 + 1), mids, [P] );
    print("  [roll_step] N0=", N0, " i1=", i1, " i0=", i0, " gamma=", round(deg(gamma)), "deg mids=", K, " newctrl=", length(newctrl));

    // 0..i1 を固定して緩和(外力 fz/fax)
    params.fixed = range2(0, i1 + 1);
    var res = solve(core, newctrl, pipe.radius, d, params, fz, fax);
    var out = { ok: 1, ctrl: res.ctrl, pre: newctrl, radius: pipe.radius, movable: 1, raw: res, diag: { i0: i0, i1: i1, gamma: gamma, P: P, N0: N0 } };
    out;
};

#!/usr/bin/env srava
// pipe_adjust.sra — pipe_proximity の「距離調整コントローラ」デモ。
//
//   pipe_proximity 本体には自己接近を *検出* するだけでなく、それを *解消* する
//   コントローラ pipe::adjust(ペナルティ/拡張ラグランジュ法)が入っている。これを
//   プラグイン op `pipe_adjust` として露出した。U ターンが詰まって自己接近する配管を、
//   端点(取り合い)を固定したまま、一様クリアランス gap >= dMin を満たすよう
//   制御点を自動で動かす。
//
//   pipe_adjust(ctrl_pts, radius, params)
//     params = ハッシュ {dMin, maxIter, fixEnds, wBend, fZ, fAxis, fOrigin, fixed[], pins[]}
//     → {"ctrl":[[x,y,z],...], "iters", "energy", "clearViolation", "feasible"}
//        ctrl = 調整後の制御点(入力と同じ並び)。そのまま tube / pipe_proximity に渡せる。
//
//   ※ adjust は勾配降下なので、初期形が「クリアランス違反だが非フック」なときに素直に
//     開く(自己交差レベルのフックは局所最適に落ちうる)。
//
// 実行(プラグイン opt-in ビルドが要る。pipe_clearance.sra 冒頭の手順を参照):
//   srava pipe_adjust.sra  →  /tmp/srava-pipe-adjust.3mf
//     橙 = 調整前(自己接近・赤マーカ) / 緑 = 調整後(z=+6 に持ち上げて比較)

include "std/math.sra";

// ---- 設計 -------------------------------------------------------------------
var R0   = 0.8;     // パイプ半径(一定)
var DMIN = 0.6;     // 目標クリアランス(両壁面間 gap >= DMIN)

// U ターンの apex を引き込んで折り返しを詰めた形(gap < DMIN で違反)。端点 = 取り合い。
var ctrl0 = [[0,0,0], [12,0,0], [10,2,0], [12,4,0], [0,4,0]];

// ---- 中心線サンプラ(plugin の Chain::build と同じ中点法 2 次ベジエ鎖) ---------
var midp = \(a, b) { (a + b) * 0.5; };
var qbez = \(P, t) { var u = 1.0 - t; P[0]*(u*u) + P[1]*(2.0*u*t) + P[2]*(t*t); };
var centerline = \(ctrl, steps) {
    var nc = length(ctrl);
    var Spt = ctrl[0]; var Ept = ctrl[nc - 1];
    var C = []; var k = 0; var i;
    for (i = 1; i < nc - 1; i = i + 1) { C[k] = ctrl[i]; k = k + 1; }
    var mc = length(C);
    var segs = [];
    if (mc == 1) {
        segs[0] = [Spt, C[0], Ept];
    } else {
        segs[0] = [Spt, C[0], midp(C[0], C[1])];
        for (i = 1; i < mc - 1; i = i + 1) {
            segs[i] = [midp(C[i-1], C[i]), C[i], midp(C[i], C[i+1])];
        }
        segs[mc-1] = [midp(C[mc-2], C[mc-1]), C[mc-1], Ept];
    }
    var cl = []; var n = 0; var si; var ts; var j;
    for (si = 0; si < length(segs); si = si + 1) {
        ts = linspace(0.0, 1.0, steps);
        for (j = 0; j < length(ts); j = j + 1) {
            if (si == 0 || j > 0) { cl[n] = qbez(segs[si], ts[j]); n = n + 1; }
        }
    }
    cl;
};
var make_pipe = \(ctrl) { tube(map(centerline(ctrl, 16), \(p){ [p, R0]; }), 24); };

// ---- 1) 調整前: 自己接近を検出 ----------------------------------------------
var before = pipe_proximity(ctrl0, [R0, 0.0], 8.0);
print("BEFORE: min gap =", before[0][0], "(target dMin =", DMIN, ")");

// ---- 2) コントローラで調整(端点固定・gap >= DMIN へ) ------------------------
var res = pipe_adjust(ctrl0, R0, {dMin: DMIN, maxIter: 400, fixEnds: 1, wBend: 0.1});
print("ADJUST: iters =", res.iters, " feasible =", res.feasible,
      " clearViolation =", res.clearViolation);

// ---- 3) 調整後を検証 --------------------------------------------------------
var after = pipe_proximity(res.ctrl, [R0, 0.0], 8.0);
if (length(after) > 0) { print("AFTER : min gap =", after[0][0]); }
else { print("AFTER : clear (no contact within window)"); }

// ---- 4) 可視化: 橙=調整前(赤マーカ付き) / 緑=調整後(z=+6 へ) ----------------
var markers = combine(map(before, \(h){
    (sphere(0.4, 2) >>> h[1]) +++ (sphere(0.4, 2) >>> h[2]);
}));
export("/tmp/srava-pipe-adjust.3mf",
    color(make_pipe(ctrl0),   "orange") +++
    color(markers,            "red")    +++
    color(make_pipe(res.ctrl) >>> [0,0,6], "green"));

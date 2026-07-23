#!/usr/bin/env srava
// pipe_scene.sra — pipe_proximity の N 体(Scene)デモ。
//
//   複数配管を同時に扱う。1 本の「可動配管」を、他の「固定配管(障害物)」に当たらないよう
//   adjustScene で動かす。検出(pipe_scene_proximity)も調整(pipe_scene_adjust)も
//   body = {ctrl, radius, movable} の配列を渡す。
//
//   ※ adjustScene は「可動 1 本」モデル(movableIdx)。固定 body 群は障害物。
//
// 実行(プラグイン opt-in ビルド要・pipe_clearance.sra 冒頭の手順):
//   srava pipe_scene.sra  →  /tmp/srava-pipe-scene.3mf
//     青 = 固定障害物 / 橙 = 可動(調整前・赤マーカ) / 緑 = 可動(調整後・z=+6 へ)

include "std/math.sra";

var R0   = 0.8;
var DMIN = 0.5;

// body 0 = 動かす配管(障害物に近接する細い折り返し), body 1 = 固定障害物(直管)
var bodies = [
  {ctrl: [[0,2,0],[14,2,0],[0,2.2,0]], radius: R0, movable: 1},
  {ctrl: [[0,4,0],[14,4,0]],           radius: R0, movable: 0}
];

// ---- 中心線サンプラ(plugin の Chain::build と同じ中点法 2 次ベジエ鎖) ----
var midp = \(a, b) { (a + b) * 0.5; };
var qbez = \(P, t) { var u = 1.0 - t; P[0]*(u*u) + P[1]*(2.0*u*t) + P[2]*(t*t); };
var centerline = \(ctrl, steps) {
    var nc = length(ctrl); var Spt = ctrl[0]; var Ept = ctrl[nc - 1];
    var C = []; var k = 0; var i;
    for (i = 1; i < nc - 1; i = i + 1) { C[k] = ctrl[i]; k = k + 1; }
    var mc = length(C); var segs = [];
    if (mc == 0) { segs[0] = [Spt, midp(Spt,Ept), Ept]; }       // 制御点なし=直管(近似)
    else { if (mc == 1) { segs[0] = [Spt, C[0], Ept]; }
    else {
        segs[0] = [Spt, C[0], midp(C[0], C[1])];
        for (i = 1; i < mc - 1; i = i + 1) { segs[i] = [midp(C[i-1],C[i]), C[i], midp(C[i],C[i+1])]; }
        segs[mc-1] = [midp(C[mc-2], C[mc-1]), C[mc-1], Ept];
    } }
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

// ---- 1) N 体近接検出 --------------------------------------------------------
var hits = pipe_scene_proximity(bodies, 8.0);
print("scene contacts:", length(hits));
var i;
for (i = 0; i < length(hits); i = i + 1) {
    print("  gap", hits[i][0], " body", hits[i][8], "<->", hits[i][9]);
}

// ---- 2) 可動 body0 を固定 body1 を避けて調整 --------------------------------
var res = pipe_scene_adjust(bodies, 0, {dMin: DMIN, maxIter: 400, fixEnds: 1});
print("adjust: iters", res.iters, " feasible", res.feasible, " clearViolation", res.clearViolation);

// ---- 3) 可視化 --------------------------------------------------------------
var markers = combine(map(hits, \(h){
    (sphere(0.4, 2) >>> h[1]) +++ (sphere(0.4, 2) >>> h[2]);
}));
export("/tmp/srava-pipe-scene.3mf",
    color(make_pipe(bodies[1].ctrl), "blue")            +++   // 固定障害物
    color(make_pipe(bodies[0].ctrl), "orange")          +++   // 可動(調整前)
    color(markers,                   "red")             +++
    color(make_pipe(res.ctrl) >>> [0,0,6], "green"));         // 可動(調整後)

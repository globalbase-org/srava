#!/usr/bin/env srava
// pipe_clearance.sra — pipe_proximity プラグインのデモ。
//
//   可変太さパイプ(中心線 = 中点法 2 次ベジエ鎖)が「自分自身にどれだけ近づくか」を
//   別 repo のプラグイン pipe_proximity(MIT・CGAL 非依存)に計算させ、その結果を
//   srava 側で可視化する。U 字に折り返したパイプは、曲げがきついと折り返しの内側で
//   壁どうしが接近(あるいは貫通)する — それをプラグインに検出させる。
//
//   このスクリプトは std/inspect.sra の thin_markers と同じ作法:
//     「解析 op の戻り値(座標の入れ子配列)を球マーカに変換して元モデルに重ねる」。
//
// 実行(プラグインは opt-in ビルドが要る):
//   1) cmake -DSRAVA_PLUGIN_PIPEPROX=ON ... && cmake --build . でプラグインを作る
//   2) pipe_proximity.plugin の bin を実体に向けて $PIG_PLUGIN_PATH か
//      ~/.config/srava/plugins/ に置く(CMake test は絶対パスで自動生成)
//   3) srava pipe_clearance.sra  →  /tmp/srava-pipe-clearance.3mf
//      (3MF はパイプ=灰・接近マーカ=赤を面色つきで保持。STL は色非対応)

include "std/math.sra";

// ---- 設計パラメータ ----------------------------------------------------------
var R0    = 0.8;     // パイプ半径(一定)。r(s) = R0 * exp(SLOPE * s)
var SLOPE = 0.0;     // 半径の指数成長率 m(0 = 一定太さ)
var GAP   = 1.0;     // 表面間 gap がこの値以下の接近のみ報告

// 制御点列: 先頭 = 始点 S / 末尾 = 終点 E / 中間 = off-curve 制御点 C。
// 横に伸びて U ターンで折り返す形。折り返し(x≈15)が一番きつく、そこで自己接近する。
var ctrl = [[0,0,0], [14,0,0], [16,1.5,0], [14,3,0], [0,3,0]];

// ---- 1) 解析: プラグイン呼び出し --------------------------------------------
//   返り = [[gap, pA, pB, normal, sA, sB, rA, rB], ...]  (gap 昇順 / 無ければ [])
var hits = pipe_proximity(ctrl, [R0, SLOPE], GAP);
print("self-approach contacts (gap <=", GAP, "):", length(hits));
var i;
for (i = 0; i < length(hits); i = i + 1) {
    print("  #", i, "gap=", hits[i][0], "at", hits[i][1]);
}

// ---- 2) 同じ中心線を srava 側で再構成(中点法 2 次ベジエ鎖) -----------------
//   plugin の Chain::build と同じ: 通過点 = S, mid(Cᵢ,Cᵢ₊₁)…, E / 各区間 = 2 次ベジエ。
//   これでプラグインが解析した曲線とまったく同じ中心線を tube で掃引できる。
var midp = \(a, b) { (a + b) * 0.5; };
var qbez = \(P, t) { var u = 1.0 - t; P[0]*(u*u) + P[1]*(2.0*u*t) + P[2]*(t*t); };

var nctrl = length(ctrl);
var Spt   = ctrl[0];
var Ept   = ctrl[nctrl - 1];
var C = []; var k = 0;                                   // off-curve 制御点だけ集める
for (i = 1; i < nctrl - 1; i = i + 1) { C[k] = ctrl[i]; k = k + 1; }
var m = length(C);                                       // 制御点数 = セグメント数

var segs = [];                                           // 各区間の [P0, P1, P2]
if (m == 1) {
    segs[0] = [Spt, C[0], Ept];
} else {
    segs[0] = [Spt, C[0], midp(C[0], C[1])];
    for (i = 1; i < m - 1; i = i + 1) {
        segs[i] = [midp(C[i-1], C[i]), C[i], midp(C[i], C[i+1])];
    }
    segs[m-1] = [midp(C[m-2], C[m-1]), C[m-1], Ept];
}

var STEPS = 16;                                          // 区間あたりのサンプル数
var cl = []; var n = 0; var si; var ts; var j;
for (si = 0; si < length(segs); si = si + 1) {
    ts = linspace(0.0, 1.0, STEPS);
    for (j = 0; j < length(ts); j = j + 1) {
        if (si == 0 || j > 0) {                          // 区間の継ぎ目は重複させない
            cl[n] = qbez(segs[si], ts[j]); n = n + 1;
        }
    }
}

var path = map(cl, \(p){ [p, R0]; });                    // [x,y,z] → [[x,y,z], R0]
var pipe = tube(path, 24);

// ---- 3) プラグインが報告した接近点に球マーカ ---------------------------------
//   各 hit の pA / pB(接近している両壁面の点)に赤い小球を置く。
var markers = combine(map(hits, \(h){
    (sphere(0.4, 2) >>> h[1]) +++ (sphere(0.4, 2) >>> h[2]);
}));

// ---- 4) 出力: 灰のパイプ + 赤の接近マーカ(3MF は面色を保持) ------------------
export("/tmp/srava-pipe-clearance.3mf",
    color(pipe, "gray") +++ color(markers, "red"));

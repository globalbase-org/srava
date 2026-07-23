#!/usr/bin/env srava
// pipe_coil.sra — 自己接近するコイルを pipe_adjust の分離パスで「食い込み分だけ」開く例。
//
//   ピッチ = 2r のコイルは隣接ターンの表面間隔が ~0(接触/食い込み)。energy 法では開けないが、
//   分離パス(既定 ON)が中心線間方向へ食い込み量だけ押し離して gap >= dMin にする。設定はそのまま。
//
//   ★ 半径一定のときは tube の経路に **制御点をそのまま(ポリライン)** 渡すのが確実(有効メッシュ)。
//     pipe_sample(弧長等間隔・ベジエ再標本化)は可変太さ用。多周コイルではベジエ中心線に
//     局所自己接近が残って自己交差することがあるため、一定太さのコイルはポリライン推奨。
//
// 実行(プラグイン opt-in ビルド要・pipe_clearance.sra 冒頭の手順):
//   srava pipe_coil.sra  →  /tmp/srava-pipe-coil.3mf
//     橙 = 調整前(ピッチ 2r で接触) / 緑 = 調整後(x=+400 へ離して比較)

include "std/math.sra";

var N    = 3;       // 巻き数
var ctr  = 8;       // 1 周の分割
var a     = 100.0;  // 中心(マンドレル)半径相当
var r     = 25.0;   // パイプ半径(一定)
var dd    = 8.0;    // すき間
var DMIN  = 2.0;    // 目標クリアランス
var ctr_r = (a + r + dd) / cos(PI/ctr);

// ピッチ = 2r でちょうど接触するコイル(先頭=S, 末尾=E)
var sp = linspace(PI/ctr, 2*PI*N + PI/ctr, N*ctr);
var coil = transpose([ cos(sp)*ctr_r, sin(sp)*ctr_r, linspace(-r/ctr, -2*r*N - r/ctr, N*ctr) ]);

// 一定太さ → tube にはポリライン(制御点 + 半径)を直接渡す
var pipe_of = \(c) { tube(map(c, \(p){ [p, r]; }), 32); };

// ---- 分離(food cutting を gap>=DMIN に開く・端点固定のまま) -----------------
print("BEFORE: min gap =", pipe_proximity(coil, r, 4*r)[0][0]);
var res = pipe_adjust(coil, r, {dMin: DMIN, fixEnds: 1});
print("AFTER : min gap =", pipe_proximity(res.ctrl, r, 4*r)[0][0],
      " clearViolation =", res.clearViolation);

// ---- 出力(橙=調整前 / 緑=調整後を x=+400 へ並べる) --------------------------
export("/tmp/srava-pipe-coil.3mf",
    color(pipe_of(coil),                "orange") +++
    color(pipe_of(res.ctrl) >>> [400,0,0], "green"));

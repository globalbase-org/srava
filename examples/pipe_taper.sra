#!/usr/bin/env srava
// pipe_taper.sra — pipe_adjust + pipe_sample の組み合わせ例(可変太さ配管)。
//
//   テーパ(先太→先細)の配管が U ターンの折り返しで自己接近して印刷不能になっている。
//     1) pipe_proximity … 可変半径のまま自己接近を検出(クリアランス違反)
//     2) pipe_adjust    … 端点を固定したまま gap >= dMin を満たすよう制御点を動かして解消
//     3) pipe_sample    … 調整前後の中心線を弧長等間隔で [pos, r] に展開 → tube 化
//   可変半径なので「弧長 s → 半径 R(s)」の対応が要るが、pipe_sample がライブラリの正確な
//   弧長で R(s) を評価して per-vertex 半径つきで返すため、srava 側の弧長計算は不要。
//
// 実行(プラグイン opt-in ビルド要・pipe_clearance.sra 冒頭の手順):
//   srava pipe_taper.sra  →  /tmp/srava-pipe-taper.3mf
//     橙 = 調整前(自己接近・赤マーカ) / 緑 = 調整後(z=+6 へ持ち上げて比較)

// テーパ半径: 始点 r=1.0 → 終点 r=0.35 へ弧長線形補間(s は弧長・末尾≈管長)
var RAD  = [[0, 1.0], [26, 0.35]];
var PIT  = 0.5;     // tube 化の弧長ピッチ(mm・小さいほど滑らか)
var DMIN = 0.5;     // 目標クリアランス

// 折り返しが詰まり気味の U(太い根元側が接近してクリアランス違反)
var ctrl0 = [[0,0,0], [12,0,0], [10.5,2,0], [12,4,0], [0,4,0]];

// pipe_sample の戻り [[pos,r],...] はそのまま tube に渡せる(per-vertex 半径つき)
var make_pipe = \(ctrl) { tube(pipe_sample(ctrl, RAD, PIT), 24); };

// ---- 1) 検出(可変半径・gap<=DMIN の違反だけ返す) ----------------------------
var before = pipe_proximity(ctrl0, RAD, DMIN);
print("BEFORE: violations =", length(before), " min gap =", before[0][0], "(dMin =", DMIN, ")");

// ---- 2) 調整(可変半径のまま gap>=DMIN へ) -----------------------------------
var res = pipe_adjust(ctrl0, RAD, {dMin: DMIN, maxIter: 400, fixEnds: 1});
print("ADJUST: iters", res.iters, " feasible", res.feasible, " clearViolation", res.clearViolation);

// ---- 3) 検証(違反が 0 になれば解消) -----------------------------------------
print("AFTER : violations =", length(pipe_proximity(res.ctrl, RAD, DMIN)));

// ---- 4) 可視化: 橙=調整前(赤=接近点) / 緑=調整後 ----------------------------
var marks = combine(map(before, \(h){ (sphere(0.3, 2) >>> h[1]) +++ (sphere(0.3, 2) >>> h[2]); }));
export("/tmp/srava-pipe-taper.3mf",
    color(make_pipe(ctrl0),   "orange") +++
    color(marks,              "red")    +++
    color(make_pipe(res.ctrl) >>> [0,0,6], "green"));

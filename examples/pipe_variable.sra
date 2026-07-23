#!/usr/bin/env srava
// pipe_variable.sra — 太さが弧長に沿って変わる配管(可変太さ)のサンプル。
//
//   pipe_proximity の中心線(中点法 2 次ベジエ鎖)に「弧長 s → 半径」のプロファイルを載せ、
//   pipe_sample で弧長等間隔に [pos, r] へ展開して tube 化する。半径は s の関数なので、
//   太さの増減がパイプに沿って滑らかに乗る(srava 側で弧長計算は不要)。
//
//   radius の与え方(どれも pipe_sample / pipe_proximity 共通):
//     [r0, m]      指数 r(s)=r0*exp(m*s)         … 全長で滑らかに太く/細く(長さに依存せず安定)
//     [[s,r],...]  弧長キーポイントの線形補間     … 任意形状(s は弧長・末尾は管長 Smax 付近に置く)
//
// 実行(プラグイン opt-in ビルド要・pipe_clearance.sra 冒頭の手順):
//   srava pipe_variable.sra  →  /tmp/srava-pipe-variable.3mf

var PIT = 0.4;     // tube 化の弧長ピッチ(小さいほど滑らか)

// 3D に巻く曲線(象の鼻/触手風)。先頭=S, 末尾=E, 中間=off-curve 制御点。
var path = [[0,0,0], [6,0,1], [10,4,2], [8,9,3], [2,11,4], [-2,8,5]];

// 太さ: 細い根元 → 太い先へ指数フレア r(s)=0.22*exp(0.05*s)
var horn = pipe_sample(path, [0.22, 0.05], PIT);
print("horn samples =", length(horn),
      "  r: base", horn[0][1], "→ tip", horn[length(horn)-1][1]);

export("/tmp/srava-pipe-variable.3mf", color(tube(horn, 28), "orange"));

// --- 別形状の例(コメント): 中央が膨らむ紡錘形。s は弧長なので末尾 s≈管長に置く ---
//   var spindle = pipe_sample(path, [[0, 0.25], [13, 1.1], [26, 0.25]], PIT);
//   export("/tmp/srava-pipe-spindle.3mf", color(tube(spindle, 28), "cyan"));

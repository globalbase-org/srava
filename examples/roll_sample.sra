// roll_sample.sra — 最小ドライバ(利用例)。パラメータ設定 → roll_initial → roll_step ループ → 真半径ホーンを出力。
//   アルゴリズムは std/roll.sra。実行: srava examples/roll_sample.sra
//   出力: spiral_horn.3mf (テーパ反映の実形状。毎手上書き=最終形。カレントディレクトリ相対)。
//
// ★必須モジュール: pipe_proximity (`srava --modules` で確認できる)
// ★所要時間: **real 8m48s / user 75m17s** (threads:21, parallel:1, 弧長 3000mm 到達=15手・実測値)
//   重いので ctest には入れていない。回帰確認用の短縮版は test/ の roll_min (2手・約 1 分)。
include "std/roll.sra";

// ================= パラメータ =================
var a = 100.0;      // 芯(マンドレル)半径 [mm]
var r = 25.0;       // 巻き管 基準半径(=radius[0]) [mm]
var m = 0.0008;     // 巻き管テーパ増加レート(指数): 半径(s)=r*exp(m*弧長)。3m ホーンなら ~0.0008
var d = 8.0;        // マージン [mm]
var N = 12;         // 一周コントロールポイント分割数
var L = 500.0;      // 引っ張り距離(> rho=r+a+d) [mm]
var beta  = 30.0;   // 1手の巻き進み角 [度] (12手で1周)
var alpha = 20.0;   // 解放帯角 [度]
var FZ    = -0.003; // 重力(z平行力, 負=下向き, roll_step のみ)。0 で無重力
var FAXIS =  0.05;  // 締込(z軸へ寄せる力, roll_step のみ)。重力を掛けるなら発散防止に併用
var n_steps  = 30;      // roll_step 最大回数
var ARC_STOP = 3000.0;  // 目標弧長[mm]到達で停止(0 で無効=n_steps まで)
var pitch_viz = 3.0;    // 出力チューブのサンプリング間隔

// ================= シーン =================
var core = { ctrl: [[0,0,-60],[0,0,0],[0,0,700]], radius: a, movable: 0 };
var pipe = { radius: [r, m], movable: 1 };
var params = { maxIter: 100, wBend: 0.1, threads: 21, parallel: 1, wSpace: 0.1 };  // wSpace=制御点均一化
var mandrel = tube([ [core.ctrl[0], a], [core.ctrl[length(core.ctrl)-1], a] ]);

// ================= 出力ヘルパ =================
var arc_of = \(ctrl){ var S = arclen(map(pipe_sample(ctrl, [r,m], 0), \(p){ p[0]; })); S[length(S)-1]; };
var export_horn = \(ctrl, path){   // 真半径(テーパ反映)ホーン + 芯マンドレル
    export(path, color(tube(pipe_sample(ctrl, [r,m], pitch_viz), 32), "orange") +++ color(mandrel, "gray"));
};

// ================= 実行 =================
print("=== roll_initial ===");
var st = roll_initial(core, pipe, d, N, L, params);
print("initial ctrl=", length(st.ctrl), " arc=", round(arc_of(st.ctrl)),
      " feasible=", st.raw.feasible, " clearViol=", round(st.raw.clearViolation*1000)/1000);
export_horn(st.ctrl, "work/spiral_horn.3mf");

var stop = 0; var s;
for ( s = 1 ; s <= n_steps ; s = s + 1 ) {
    if ( stop == 0 ) {
        var nx = roll_step(core, st, d, N, L, beta, alpha, params, FZ, FAXIS);
        if ( nx.ok == 0 ) { print("STOP at step", s, ":", nx.err); stop = 1; }
        if ( nx.ok == 1 ) {
            st = nx;
            var arc = arc_of(st.ctrl);
            print("step", s, " ctrl=", length(st.ctrl), " arc=", round(arc),
                  " P.z=", round(st.diag.P[2]), " clearViol=", round(st.raw.clearViolation*1000)/1000,
                  " feasible=", st.raw.feasible);
            export_horn(st.ctrl, "work/spiral_horn.3mf");
            if ( ARC_STOP > 0 ) { if ( arc >= ARC_STOP ) { print("REACHED arc>=", ARC_STOP, "mm at step", s); stop = 1; } }
        }
    }
}
print("=== done -> work/spiral_horn.3mf (真半径ホーン) ===");

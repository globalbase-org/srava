// roll_sample_min.sra — 回帰テスト用の縮小ドライバ（roll_sample.sra のフル版に対する短縮版）。
//   roll_initial + roll_step×2 だけを回す。roll_sample.sra と同一パラメータなので、
//   螺旋生成・解放帯検出・pipe_scene_adjust 呼び出し・テーパ付き export まで一通り踏む。
//
//   実行: srava test/roll_min.sra   （include は $PREFIX/share/srava/lib か $SRAVA_PATH から解決）
//   所要: 重い (threads:21, parallel:1。cold cache からでも通る)
//   必須モジュール: pipe_proximity  （`srava --modules` でロード確認できる）
//
//   期待値（この 5 行が出れば PASS）:
//     initial  ctrl=20  arc=1407  feasible=1  clearViol=0.003
//     step 1   ctrl=20  arc=1497  feasible=1  clearViol=0.003
//     step 2   ctrl=21  arc=1583  feasible=1  clearViol=0.003
//   ※ arc は round() 済みの整数。feasible / clearViol / ctrl は環境非依存に一致するはず。
include "std/roll.sra";

// ================= パラメータ（roll_sample.sra と同一。n_steps だけ 2 に短縮）=================
var a = 100.0;      // 芯(マンドレル)半径 [mm]
var r = 25.0;       // 巻き管 基準半径(=radius[0]) [mm]
var m = 0.0008;     // 巻き管テーパ増加レート(指数): 半径(s)=r*exp(m*弧長)
var d = 8.0;        // マージン [mm]
var N = 12;         // 一周コントロールポイント分割数
var L = 500.0;      // 引っ張り距離(> rho=r+a+d) [mm]
var beta  = 30.0;   // 1手の巻き進み角 [度]
var alpha = 20.0;   // 解放帯角 [度]
var FZ    = -0.003; // 重力(z平行力, 負=下向き, roll_step のみ)
var FAXIS =  0.05;  // 締込(z軸へ寄せる力, roll_step のみ)
var n_steps  = 2;       // ← 短縮点。フル版は 30(arc 3000mm 到達=15手で停止)
var pitch_viz = 3.0;    // 出力チューブのサンプリング間隔
var OUT = "roll_min_out.3mf";   // ctest は作業 dir を掘って cd してから走らせる   // 出力先(テスト harness 側で書き換え可)

// ================= シーン =================
var core = { ctrl: [[0,0,-60],[0,0,0],[0,0,700]], radius: a, movable: 0 };
var pipe = { radius: [r, m], movable: 1 };
var params = { maxIter: 100, wBend: 0.1, threads: 21, parallel: 1, wSpace: 0.1 };  // parallel:1 は事実上必須
var mandrel = tube([ [core.ctrl[0], a], [core.ctrl[length(core.ctrl)-1], a] ]);

// ================= 出力ヘルパ =================
var arc_of = \(ctrl){ var S = arclen(map(pipe_sample(ctrl, [r,m], 0), \(p){ p[0]; })); S[length(S)-1]; };
var export_horn = \(ctrl, path){
    export(path, color(tube(pipe_sample(ctrl, [r,m], pitch_viz), 32), "orange") +++ color(mandrel, "gray"));
};

// ================= 実行 =================
print("=== roll_initial ===");
var st = roll_initial(core, pipe, d, N, L, params);
print("initial ctrl=", length(st.ctrl), " arc=", round(arc_of(st.ctrl)),
      " feasible=", st.raw.feasible, " clearViol=", round(st.raw.clearViolation*1000)/1000);
export_horn(st.ctrl, OUT);

var stop = 0; var s;
for ( s = 1 ; s <= n_steps ; s = s + 1 ) {
    if ( stop == 0 ) {
        var nx = roll_step(core, st, d, N, L, beta, alpha, params, FZ, FAXIS);
        if ( nx.ok == 0 ) { print("STOP at step", s, ":", nx.err); stop = 1; }
        if ( nx.ok == 1 ) {
            st = nx;
            print("step", s, " ctrl=", length(st.ctrl), " arc=", round(arc_of(st.ctrl)),
                  " P.z=", round(st.diag.P[2]), " clearViol=", round(st.raw.clearViolation*1000)/1000,
                  " feasible=", st.raw.feasible);
            export_horn(st.ctrl, OUT);
        }
    }
}
print("=== done (min) -> ", OUT, " ===");

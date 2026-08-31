#!/usr/bin/env srava
include "module/all.sra";   // #3452: 実カーネルを明示ロード
// ワッシャ(座金): 2D の穴あき円盤を revolve せず extrude。
//   外円 - 内円 = 穴あき領域 → 厚み 0.5 で立体化
var disk = circle(3) --- circle(1.2);
export("/tmp/srava-washer.stl", extrude(disk, 0.5));

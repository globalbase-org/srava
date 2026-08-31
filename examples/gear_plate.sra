#!/usr/bin/env srava
include "module/all.sra";   // #3452: 実カーネルを明示ロード (module("cgal.so",{}) 等の一括版)
// 2D 作図 → SVG 出力(レーザ/CNC 向け): 六角板に中央穴と肉抜き
var plate = ngon(6, 4) --- circle(1);          // 六角形 - 中央円
plate = plate --- (circle(0.5) >>> [2.2, 0, 0]);  // 肉抜き穴を 1 つ
export("/tmp/srava-plate.svg", plate);
export("/tmp/srava-plate.dxf", plate);
// そのまま 3D 板にも
export("/tmp/srava-plate.stl", extrude(plate, 0.3));

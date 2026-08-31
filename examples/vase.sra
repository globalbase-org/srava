#!/usr/bin/env srava
include "module/all.sra";   // #3452: 実カーネルを明示ロード
// 花瓶: 断面プロファイル(明示点列)を Y 軸まわりに全周 revolve(旋盤)
//   x = 半径(>=0), y = 高さ
var profile = polygon([[0,0], [3,0], [3,0.5], [1.2,1], [1.5,4], [2.2,6], [2,6.3], [0,6.3]]);
// 第2引数=回転角(360=全周), 第3引数=分割数(回転ピッチ。大きいほど滑らか。既定 32)
export("/tmp/srava-vase.stl", revolve(profile, 360, 64));

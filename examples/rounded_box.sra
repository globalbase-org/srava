#!/usr/bin/env srava
// 角丸の箱(3D Minkowski offset。第3引数 = 球の細分化レベル = 滑らかさ)
export("/tmp/srava-rounded.stl", offset(box(3, 2, 1), 0.3, 2));

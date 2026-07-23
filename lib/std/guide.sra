// std/guide.sra — 計測ガイド/注釈(ものさし・ルーラー)。include "std/guide.sra"; で取り込む。
//   3D の三角形フォーマット(STL/3MF/OFF/AMF)はエッジ(細線)を表現できないので、ガイドは **細い tube
//   ソリッド** として作り、`part +++ ruler(...)` で重ねる(combine=corefinement なし・軽い)。
//   決定的なので内容アドレスキャッシュが効き、生成コストは初回のみ。カーネル非依存(tube/combine の合成)。

// ruler(axis, len, step, r): 軸 axis(0=x / 1=y / 2=z)方向に長さ len の ものさし。原点から +axis 方向へ
//   主線(細い tube)を引き、step 間隔で直交方向に短い目盛(tick)を出す。r = 線の半径(細く)。tick 長 = step*0.4。
//   例: var part = box(80,40,30);  export("p.off", part +++ ruler(0, 80, 10, 0.4));
//   3 軸ぶん欲しいときは part +++ ruler(0,L,s,r) +++ ruler(1,L,s,r) +++ ruler(2,L,s,r)。
var ruler = \(axis, len, step, r) {
    var p0 = [0, 0, 0];
    var p1 = [0, 0, 0];  p1[axis] = len;
    var parts = [ tube([[p0, r], [p1, r]]) ];        // 主線
    var ta = axis + 1;  if ( ta > 2 ) { ta = 0; }    // 目盛を出す直交軸
    var t;
    for ( t = 0 ; t <= len + step * 0.001 ; t = t + step ) {   // 端点を含める微小 eps
        var c  = [0, 0, 0];  c[axis]  = t;
        var c2 = [0, 0, 0];  c2[axis] = t;  c2[ta] = step * 0.4;
        parts[length(parts)] = tube([[c, r], [c2, r]]);   // 添字伸長で末尾に追加
    }
    combine(parts);   // 可視化ガイド → corefinement 不要の combine で束ねる
};

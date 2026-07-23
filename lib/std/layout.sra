// std/layout.sra — mesh 配列のレイアウト(整列・配置)関数。
//   include "std/layout.sra"; で取り込む。
//   すべて bbox + map + transform 配列演算(>>>) の組み合わせで書かれた **ライブラリ**(カーネル非依存)。
//   2D/3D 両対応(bbox の隅の次元 length(bb[0]) で 2/3 を判定し、移動ベクトルを合わせる)。
//   返り値は **配列**(reduce しない)。1 つにまとめたいなら union(...) を呼ぶ。

// 次元に応じたゼロベクトル([0,0] or [0,0,0])を作る内部ヘルパ。
var _zero = \(dim) {
    if ( dim == 2 ) { [0,0]; } else { [0,0,0]; }
};

// stack(arr, axis, gap): 指定軸(0=x,1=y,2=z)に、各 mesh の bbox 幅 + gap で隙間なく並べる。
//   各 mesh をその軸の min が連続位置に来るよう移動 → 重ならない。
var stack = \(arr, axis, gap) {
    var n = length(arr);
    var off = [];
    var cur = 0;
    var i;
    for ( i = 0 ; i < n ; i = i + 1 ) {
        var bb = bbox(arr[i]);
        var v = _zero(length(bb[0]));
        v[axis] = cur - bb[0][axis];          // その軸の min を cur に合わせる
        off[i] = v;
        cur = cur + (bb[1][axis] - bb[0][axis]) + gap;
    }
    return arr >>> off;                       // zip(各 mesh に各オフセット)
};

// row(arr, gap): X 軸に横並び(stack の軸 0)。
var row = \(arr, gap) { stack(arr, 0, gap); };
// column(arr, gap): Y 軸に縦並び。
var column = \(arr, gap) { stack(arr, 1, gap); };

// _gapvec(gap, ndim): gap を ndim 要素のベクトルに正規化する内部ヘルパ。スカラなら全軸同一、配列なら
//   軸別、足りない軸は最後の値で埋める。concat(gap,[]) でスカラ→[g]/配列→[g0,..] に一様化して取り出す。
var _gapvec = \(gap, ndim) {
    var g = concat(gap, []);
    var out = []; var i;
    for ( i = 0 ; i < ndim ; i = i + 1 ) {
        if ( i < length(g) ) { out[i] = g[i]; } else { out[i] = g[length(g) - 1]; }
    }
    out;
};

// grid(arr, cols, gap): cols 列の 2D グリッド配置。**gap は格子のピッチ(原点間隔)**。
//   要素 i をそのまま格子点 (c*gx, r*gy) へ平行移動するだけ(bbox を一切見ない=単純で予測しやすい)。
//   **gap はスカラ(全軸同一)または [gx, gy](軸別)**。
//   **要素 0 が原点(0,0)、行内は x が右へ・行が進むと y が上へ伸びる(第1象限)**。
//   例: grid(m, 2, 1) → m[0]>>>[0,0], m[1]>>>[1,0], m[2]>>>[0,1], m[3]>>>[1,1]。
//   col = i - (i/cols)*cols(行内位置, x) / row = i/cols(行, y)(整数除算)。3D も可(z 不変)。
var grid = \(arr, cols, gap) {
    var g = _gapvec(gap, 2);                  // [gx, gy] = 格子ピッチ(原点間隔)
    return map(arr, \(m, i) {
        var c = i - (i / cols) * cols;  var r = i / cols;
        m >>> [c * g[0], r * g[1], 0];
    });
};

// grid3(arr, cols, rows, gap): 3D グリッド配置。cols 列(X)× rows 行(Y)で 1 層を埋め、層は **Z 方向に自動で
//   伸ばす**(要素数が cols*rows を超えたら次の層へ)。**gap は格子のピッチ(原点間隔)**で、要素 i をそのまま
//   格子点 (c*gx, r*gy, L*gz) へ平行移動するだけ(bbox を一切見ない)。
//   **gap はスカラ(全軸同一)または [gx, gy, gz](軸別)**。
//   **要素 0 が原点(0,0,0)、x(行内)→ y(行)→ z(層)の順に、いずれも正方向へ伸びる**。
var grid3 = \(arr, cols, rows, gap) {
    var g = _gapvec(gap, 3);                  // [gx, gy, gz] = 格子ピッチ(原点間隔)
    var per = cols * rows;                    // 1 層あたりの要素数
    return map(arr, \(m, i) {
        var L = i / per;  var w0 = i - L * per;
        var c = w0 - (w0 / cols) * cols;  var r = w0 / cols;
        m >>> [c * g[0], r * g[1], L * g[2]];
    });
};

// align(arr, axis, mode): 指定軸で全 mesh を一直線に揃える(位置の他成分は保つ)。
//   mode = "min" / "center" / "max"。基準は先頭要素のアンカー。
var align = \(arr, axis, mode) {
    var n = length(arr);
    if ( n == 0 ) { return arr; }
    var bb0 = bbox(arr[0]);
    var target = bb0[0][axis];                         // "min"
    if ( mode == "center" ) { target = (bb0[0][axis] + bb0[1][axis]) / 2; }
    if ( mode == "max" )    { target = bb0[1][axis]; }
    var off = [];
    var i;
    for ( i = 0 ; i < n ; i = i + 1 ) {
        var bb = bbox(arr[i]);
        var anc = bb[0][axis];
        if ( mode == "center" ) { anc = (bb[0][axis] + bb[1][axis]) / 2; }
        if ( mode == "max" )    { anc = bb[1][axis]; }
        var v = _zero(length(bb[0]));
        v[axis] = target - anc;
        off[i] = v;
    }
    return arr >>> off;
};

// std/math.sra — 数学の定数とちょっとした数値ヘルパ。
//   初等関数(sin/cos/tan/asin/acos/atan/atan2/sqrt/exp/log/pow/abs/floor/ceil/round/sign/mod/min/max)
//   は **カーネル組込**(planner 側・ベクトル化・角度ラジアン)。ここはその上の薄い層。

var PI  = 3.14159265358979323846;
var TAU = 6.28318530717958647692;   // 2*PI(一周)
var E   = 2.71828182845904523536;

// 度 ⇄ ラジアン(初等関数はラジアン入力なので変換ヘルパを用意)
var rad = \(d){ d * 0.0174532925199432958; };   // d * PI/180
var deg = \(r){ r * 57.295779513082320877; };    // r * 180/PI

// range(n) → [0,1,…,n-1]
var range = \(n){
    var a = [];
    var i;
    for ( i = 0 ; i < n ; i = i + 1 ) { a[i] = i; }
    a;
};

// range2(lo, hi) → [lo, …, hi-1]
var range2 = \(lo, hi){
    var a = [];
    var i;
    for ( i = lo ; i < hi ; i = i + 1 ) { a[i - lo] = i; }
    a;
};

// linspace(lo, hi, n) → n 個の等間隔サンプル(両端含む)。曲線の媒介変数生成に使う。
var linspace = \(lo, hi, n){
    var a = [];
    var i;
    if ( n <= 1 ) { a[0] = lo; return a; }
    var step = 1.0 * (hi - lo) / (n - 1);    // 1.0* で整数除算を回避
    for ( i = 0 ; i < n ; i = i + 1 ) { a[i] = lo + i * step; }
    a;
};

// ---- ベクトル/行列ヘルパ(点 = 数値配列。2D/3D 共通。すべて「配列を左に」書く) ----
var vadd   = \(a, b){ a + b; };             // 要素和(同次元)
var vsub   = \(a, b){ a - b; };             // 要素差
var vscale = \(a, s){ a * s; };             // スカラ倍(s が配列なら軸別)
var vdot   = \(a, b){ sum(a * b); };        // 内積(= Σ aᵢbᵢ)
var vlen   = \(a){ sqrt(sum(a * a)); };     // ノルム |a|
var vnorm  = \(a){ a * (1.0 / vlen(a)); };  // 単位ベクトル a/|a|

// reverse(a): 配列を逆順に。
var reverse = \(a){
    var n = length(a); var b = []; var i;
    for ( i = 0 ; i < n ; i = i + 1 ) { b[i] = a[n - 1 - i]; }
    b;
};

// slice(a, lo, hi): 部分配列 a[lo], …, a[hi-1]（lo 以上 hi 未満の半開区間）。
//   range2 と同じ半開規約。範囲は [0, length(a)] にクランプし、lo>=hi なら空配列 []。
//   例: slice([10,11,12,13,14], 1, 4) → [11,12,13]（= ary[10..20] 相当・上端は含まない）。
var slice = \(a, lo, hi){
    var n = length(a);
    var L = max(0, min(lo, n));
    var H = max(L, min(hi, n));
    map(range2(L, H), \(i){ a[i]; });
};

// matvec(M, p): 行列 M(= 行ベクトルの配列) × 列ベクトル p。result[i] = M[i]·p。
var matvec = \(M, p){ map(M, \(row){ sum(row * p); }); };

// 回転行列(rotate_pts に渡す)。th はラジアン。2D=rotmat2 / 3D=各軸 rotmat_x/y/z。
var rotmat2  = \(th){ [[cos(th), sin(th)*(-1)], [sin(th), cos(th)]]; };
var rotmat_z = \(th){ [[cos(th), sin(th)*(-1), 0.0], [sin(th), cos(th), 0.0], [0.0, 0.0, 1.0]]; };
var rotmat_x = \(th){ [[1.0, 0.0, 0.0], [0.0, cos(th), sin(th)*(-1)], [0.0, sin(th), cos(th)]]; };
var rotmat_y = \(th){ [[cos(th), 0.0, sin(th)], [0.0, 1.0, 0.0], [sin(th)*(-1), 0.0, cos(th)]]; };

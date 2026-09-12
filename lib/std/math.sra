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

// vcross(a, b): 3D の外積 a × b。★2D には無い(外積が擬スカラになるため)。
//   ⚠ 2026-09-05 まで stdlib に **無かった**(vdot/vlen/vnorm はあった)。rotmat_2v の前提。
var vcross = \(a, b){
    [ a[1]*b[2] - a[2]*b[1],
      a[2]*b[0] - a[0]*b[2],
      a[0]*b[1] - a[1]*b[0] ];
};

// ---- 3x3 → transform(m, matrix) が要求する **平坦 12 要素**(行優先 3x4) ----
//   ★ rotmat_* は 3x3 の入れ子を返すが transform は平坦 12/16 を要求するので、この橋が
//     無いと rotmat_* は事実上 **点列専用**(rotate_pts)で、メッシュへ適用する道が無かった。
//   mat34(M)     … 平行移動なし
//   mat34_t(M,t) … 平行移動 t つき (t = [tx,ty,tz])
//   ⚠ 引数の省略は言語が持たないので 2 本に分ける。
var mat34 = \(M){
    [ M[0][0], M[0][1], M[0][2], 0.0,
      M[1][0], M[1][1], M[1][2], 0.0,
      M[2][0], M[2][1], M[2][2], 0.0 ];
};
var mat34_t = \(M, t){
    [ M[0][0], M[0][1], M[0][2], t[0],
      M[1][0], M[1][1], M[1][2], t[1],
      M[2][0], M[2][1], M[2][2], t[2] ];
};

// rotmat_2v(v1, v2): v1 を v2 の向きへ持っていく回転(回転軸 = v1 × v2)。3x3 を返す。
//   a = vnorm(v1) / b = vnorm(v2) / k = a×b / c = a·b とおくと、c != -1 なら三角関数なしに
//     R = I + K + K*K/(1+c)        (K = k の歪対称行列)
//   で書ける。ここは K*K を展開して直接組む。
//
//   退化の扱い:
//     同じ向き (c = 1)  … 単位行列
//     逆向き   (c = -1) … k が 0 になり上式が発散する。a に直交する軸まわりの 180°。
//                         ★ 軸は数学的に一意でないが **実装は決定的に 1 つ選ぶ**
//                         (|a| の成分が最小の座標軸との外積)。実行のたびに違う軸を選ぶと
//                         同じスクリプトが違う結果を出す(cherchi の非決定性 #3438 で踏んだ形)。
//     零ベクトル        … 向きが定義できない。★ **黙って単位行列を返さない**。
//                         ⚠ stdlib からエラーを起こす手段が言語に無いので **NaN 行列**を返す。
//                         使った瞬間に座標が NaN になるので、それらしい形が出てしまうことはない。
var rotmat_2v = \(v1, v2){
    var l1 = vlen(v1);
    var l2 = vlen(v2);
    if ( l1 == 0.0 || l2 == 0.0 ) {
        var q = 0.0 / 0.0;                        // NaN(黙って恒等を返さないため)
        return [[q,q,q],[q,q,q],[q,q,q]];
    }
    var a = v1 * (1.0 / l1);
    var b = v2 * (1.0 / l2);
    var c = vdot(a, b);
    if ( c > 0.999999999999 ) {                   // 同じ向き
        return [[1.0,0.0,0.0],[0.0,1.0,0.0],[0.0,0.0,1.0]];
    }
    if ( c < 0.0 - 0.999999999999 ) {             // 逆向き: a に直交する軸で 180°
        // ★ 決定的に選ぶ: |a| の成分が最小の座標軸 e を取り、u = normalize(a × e)。
        //   最小成分の軸は a と最も「離れている」ので a × e が 0 にならない。
        var ax = abs(a[0]); var ay = abs(a[1]); var az = abs(a[2]);
        var e = [1.0, 0.0, 0.0];
        if ( ay < ax ) {
            if ( az < ay ) { e = [0.0, 0.0, 1.0]; } else { e = [0.0, 1.0, 0.0]; }
        } else {
            if ( az < ax ) { e = [0.0, 0.0, 1.0]; }
        }
        var u = vnorm(vcross(a, e));
        // 180° 回転 = 2 u uᵀ − I
        return [[2.0*u[0]*u[0] - 1.0, 2.0*u[0]*u[1],       2.0*u[0]*u[2]      ],
                [2.0*u[1]*u[0],       2.0*u[1]*u[1] - 1.0, 2.0*u[1]*u[2]      ],
                [2.0*u[2]*u[0],       2.0*u[2]*u[1],       2.0*u[2]*u[2] - 1.0]];
    }
    var k = vcross(a, b);
    var s = 1.0 / (1.0 + c);
    [[ 1.0 - s*(k[1]*k[1] + k[2]*k[2]),  s*k[0]*k[1] - k[2],               s*k[0]*k[2] + k[1]              ],
     [ s*k[0]*k[1] + k[2],               1.0 - s*(k[0]*k[0] + k[2]*k[2]),  s*k[1]*k[2] - k[0]              ],
     [ s*k[0]*k[2] - k[1],               s*k[1]*k[2] + k[0],               1.0 - s*(k[0]*k[0] + k[1]*k[1]) ]];
};

// rotate_v(m, v1, v2): mesh m を「v1 の向きが v2 の向きになる」ように原点まわりで回す。
//   ★ math.sra で唯一 **mesh op に触れる**関数(transform)。純粋な値計算を集めた場所なので
//     例外だが、rotmat_2v をメッシュへ当てる道をここに置かないと、利用者が毎回
//     transform(m, mat34(rotmat_2v(...))) と書くことになるため置く(#3488)。
//     ⚠ 呼ばなければ幾何カーネルは要らない(lambda は定義時に評価されない)。
var rotate_v = \(m, v1, v2){ transform(m, mat34(rotmat_2v(v1, v2))); };

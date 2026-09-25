// std/curve.sra — 曲線生成(点配列を返す)。include "std/curve.sra"; で取り込む。
//   返り値は **点列** [[x,y],…](2D) / [[x,y,z],…](3D) → polygon / line / tube / extrude / revolve に流せる。
//   実装方針: 媒介変数を linspace で作り、各座標を **vectorized**(cos(t)*r 等)で列計算 → transpose で点列化。
//   数値積分は cumsum。すべて「配列を左に」書く(scalar op array は未対応のため)。

//
// ★ 幾何 op を使う関数は、先頭で **自分の候補列を宣言する**:
//     var sup = [<この関数が対応するカーネル>];   use mod_only(sup);
//   意味は 3 つ — 呼び手が選んでいればそれを尊重 / 何も言っていなければ載っているものから /
//   交差しなければその場でエラー (黙って別のカーネルで解かない)。
//   ⇒ 詳細は docs 言語リファレンス §ライブラリ関数の宣言 (#lib-use-decl)。
//   ★ sup は which(op) から **機械的に**起こしてある (2026-09-24)。各関数のコメントはその根拠。

include "std/math.sra";

// arc(cx, cy, r, a0, a1, segs): 中心(cx,cy)・半径 r・角 a0→a1(ラジアン)の円弧。segs+1 点(2D)。
var arc = \(cx, cy, r, a0, a1, segs) {
    var t = linspace(a0, a1, segs + 1);
    transpose([ cos(t)*r + cx, sin(t)*r + cy ]);
};

// ---- 点列アフィン変換(map+ラムダ。点=数値配列・2D/3D 共通) ----
// translate_pts(pts, v): 各点に v を加える(平行移動)。例: translate_pts(ps, [10,0])。
var translate_pts = \(pts, v){ map(pts, \(p){ p + v; }); };
// scale_pts(pts, s): s がスカラ=一様拡大、s が配列=軸別拡大(要素ごと)。例: scale_pts(ps, 2.0) / scale_pts(ps,[2,1])。
var scale_pts = \(pts, s){ map(pts, \(p){ p * s; }); };
// rotate_pts(pts, M): 回転行列 M(math の rotmat2/rotmat_x/y/z 等)を各点へ適用。点列専用(メッシュ rotate とは別)。
//   例(2D): rotate_pts(ps, rotmat2(rad(30)))   例(3D): rotate_pts(ps, rotmat_z(rad(30)))。
var rotate_pts = \(pts, M){ map(pts, \(p){ matvec(M, p); }); };

// lerp_pts(p0, p1, segs): p0→p1 を結ぶ直線上の segs+1 点(2D/3D)。arc_tan の直線フォールバックにも使う。
var lerp_pts = \(p0, p1, segs){
    var dim = length(p0); var cols = []; var d;
    for ( d = 0 ; d < dim ; d = d + 1 ) { cols[d] = linspace(p0[d], p1[d], segs + 1); }
    transpose(cols);
};

// arc_tan(p0, p1, t0, segs): 始点 p0・終点 p1 を通り、p0 で接ベクトル t0 に接する円弧。
//   t0 の向きに p1 まで掃く。**2D/3D 共通**(t0 と弦が張る平面内の唯一の円弧 → 3D は両端＋接線で平面が決まる)。
//   t0 は正規化不要・向きのみ。segs+1 点。t0 が弦とほぼ平行(直線)なら lerp_pts にフォールバック。
//   = arc_start_tan(始点接ベクトル版)。
var arc_tan = \(p0, p1, t0, segs){
    var T    = vnorm(t0);
    var D    = vsub(p1, p0);
    var perp = vsub(D, vscale(T, vdot(D, T)));   // 弦 D の T 直交成分(中心へ向かう向き)
    var h    = vlen(perp);
    if ( h < 0.0000001 * vlen(D) ) { return lerp_pts(p0, p1, segs); }   // ほぼ直線
    var N    = vscale(perp, 1.0 / h);            // 単位法線(弦側)
    var r    = vdot(D, D) / (2.0 * vdot(N, D));   // 半径(= 中心までの距離)
    var C    = vadd(p0, vscale(N, r));           // 中心
    var v1   = vsub(p1, C);
    var ang  = atan2(vdot(v1, T), vdot(v1, N)*(-1));   // 掃き角(t0 向き)
    if ( ang < 0.0 ) { ang = ang + TAU; }
    var a    = linspace(0.0, ang, segs + 1);
    var ca   = cos(a); var sa = sin(a);
    // P(a) = C + r·cos(a)·(−N) + r·sin(a)·T  を各座標(列)で vectorized 評価。
    var dim  = length(p0); var cols = []; var d;
    for ( d = 0 ; d < dim ; d = d + 1 ) {
        cols[d] = ca * (r * N[d] * (-1)) + sa * (r * T[d]) + C[d];
    }
    transpose(cols);
};
// arc_start_tan(p0, p1, t0, segs): 始点接ベクトル版(arc_tan の別名・呼び分け用)。
var arc_start_tan = \(p0, p1, t0, segs){ arc_tan(p0, p1, t0, segs); };
// arc_end_tan(p0, p1, t1, segs): **終点 p1** で接ベクトル t1 に接する版(p1→p0 を −t1 で掃いて反転)。2D/3D 共通。
var arc_end_tan = \(p0, p1, t1, segs){ reverse(arc_tan(p1, p0, vscale(t1, -1), segs)); };

// 二項係数 C(n,k)(整数・逐次積で厳密)。
var _binom = \(n, k) {
    var c = 1;
    var i;
    for ( i = 0 ; i < k ; i = i + 1 ) { c = c * (n - i) / (i + 1); }
    c;
};

// bezier(ctrl, segs): 制御点列 ctrl(各 [x,y] or [x,y,z])の Bezier 曲線。次数 = length(ctrl)-1。
//   segs+1 点。1D/2D/3D 対応(点の次元をそのまま保つ)。Bernstein 基底を vectorized で評価。
var bezier = \(ctrl, segs) {
    var n   = length(ctrl) - 1;          // 次数
    var dim = length(ctrl[0]);
    var t   = linspace(0.0, 1.0, segs + 1);
    var omt = t*(-1) + 1;                // 1 - t(配列を左に)
    var cols = [];
    var d;
    for ( d = 0 ; d < dim ; d = d + 1 ) {
        var acc = t * 0;                 // ゼロ配列(長さ segs+1)
        var i;
        for ( i = 0 ; i <= n ; i = i + 1 ) {
            var w = pow(t, i) * _binom(n, i) * pow(omt, n - i);   // 基底(配列)
            acc = acc + w * ctrl[i][d];
        }
        cols[d] = acc;
    }
    transpose(cols);
};

// spline(ctrl, segs): Catmull-Rom スプライン(制御点を必ず通る)。各区間 segs+1 点を連結。
//   端は最近傍を複製(clamp)。1D/2D/3D 対応。区間境界の点は重複する(line/tube は許容)。
var spline = \(ctrl, segs) {
    var n   = length(ctrl);
    var dim = length(ctrl[0]);
    var u   = linspace(0.0, 1.0, segs + 1);
    var u2  = u * u;
    var u3  = u2 * u;
    var out = [];
    var i;
    for ( i = 0 ; i < n - 1 ; i = i + 1 ) {
        var i0 = i - 1; if ( i0 < 0 )     { i0 = 0; }
        var i3 = i + 2; if ( i3 > n - 1 ) { i3 = n - 1; }
        var cols = [];
        var d;
        for ( d = 0 ; d < dim ; d = d + 1 ) {
            var P0 = ctrl[i0][d];
            var P1 = ctrl[i][d];
            var P2 = ctrl[i + 1][d];
            var P3 = ctrl[i3][d];
            var a0 = 2 * P1;
            var a1 = P2 - P0;
            var a2 = 2 * P0 - 5 * P1 + 4 * P2 - P3;
            var a3 = 3 * P1 - 3 * P2 + P3 - P0;
            cols[d] = (u*a1 + u2*a2 + u3*a3 + a0) * 0.5;   // 配列を左に
        }
        out = concat(out, transpose(cols));
    }
    out;
};

// clothoid(k0, rate, L, segs): オイラー螺旋。曲率 κ(s)=k0+rate·s を弧長 L まで。原点付近始まり・初期方位 0。
//   segs+1 点(2D)。前進オイラー積分(cumsum)。緩和曲線(道路/レール)用。
var clothoid = \(k0, rate, L, segs) {
    var ds    = 1.0 * L / segs;
    var s     = linspace(0.0, L, segs + 1);
    var kappa = s * rate + k0;                 // 曲率(配列)
    var theta = cumsum(kappa * ds);            // ∫κ ds = 方位角
    transpose([ cumsum(cos(theta)*ds), cumsum(sin(theta)*ds) ]);
};

// tube_fw_ruled(pts, w): 折れ線 pts を **一定幅 w** (fw = fixed width) で太らせる。
//   `tube_ruled` に半幅 w/2 を付けて渡すだけの薄いラッパ(tube_ruled は頂点ごとに半径を取る)。
//   幅を頂点ごとに変えたいときは tube_ruled を直接: tube_ruled([[pos, r], …])(r が可変半幅)。
//   ★ **次元は pts の位置の次元に従う** — 2D 点列 [[x,y],…] なら帯、3D 点列 [[x,y,z],…] なら管。
//     関数名は 2D を含まない(旧名 ribbon2d は 2D 専用に見えるが、実装は最初から次元非依存)。
//   ⚠ **w は「幅」であって半径ではない**。半径は w/2。
//   ⚠ w は **実数で渡すこと**。整数どうしの割り算は切り捨てなので、w=1 だと半径が 0 になる。
//     (この関数自体は w * 0.5 で計算するので w=1 でも 0.5 になる。呼び出し側で w/2 を
//      自前計算するときの注意として残す)
//   ★ 対になる名前は tube_fw (すぐ下)。
var tube_fw_ruled = \(pts, w) {
    // 使う op = tube_ruled のみ。sup = その **提供者ぜんぶ** を priority 順に並べたもの
    // (which("tube_ruled") から機械的に起こした・2026-09-24)。必須は無い。
    var sup = ["cgal", "manifold", "geogram", "nef_hybrid", "cherchi", "occt", "openvdb"];
    use mod_only(sup);
    tube_ruled(map(pts, \(p){ [p, w * 0.5]; }));
};

// tube_fw(pts, w): 折れ線 pts を **一定幅 w** で太らせる、tube_fw_ruled の **なめらか版**。
//   `tube` に半幅 w/2 を付けて渡すだけの薄いラッパ。op の対 (tube / tube_ruled) を
//   そのまま stdlib へ写した名前で、分かれ目は **背骨** — tube は点を **通る** C2 の
//   B-spline、tube_ruled は点を直線で結ぶ折れ線。
//   ⚠ **`tube` は occt だけが持つ op**。⇒ この関数は列の末尾で occt を **必須** として宣言して
//     あるので、呼んだ時点で occt が**自動でロードされる**。入っていないビルドでは
//     「no module named 'occt'」で落ちる (黙って折れ線版に化けることは無い)。
//   ⚠⚠ **次元の扱いが tube_fw_ruled と違う**。tube は occt 単独の op で突き合わせる相手が
//     居ないため、2D の点列 [[x,y],…] を **z=0 の 3D として**受ける ⇒ 返るのは
//     *帯ではなく平たい立体*。帯が欲しいなら tube_fw_ruled を使うこと
//     (そちらは 2D 点列で 2D の領域を返す。なお occt の tube_ruled は 3D 専用で、
//      2D 点列は明示エラー)。
//   ⚠ w は「幅」であって半径ではない (半径は w/2)。実数で渡すこと。
var tube_fw = \(pts, w) {
    // 使う op = tube のみ。**occt しか提供しない** ⇒ 列は 1 本に絞り、必須として末尾に置く
    // (which("tube") = occt だけ・2026-09-24)。呼んだ時点でロードされる。
    use [ module(["occt"], {}) ];
    tube(map(pts, \(p){ [p, w * 0.5]; }));
};

// arclen(pts): 点列 pts=[[x,y(,z)],…] の **頭からの累積弧長** を返す。
//   返り = [0, |p1-p0|, |p1-p0|+|p2-p1|, …, 全長 L]（length は pts と同じ・先頭 0・末尾 L）。
//   2D/3D 共通（vlen が次元非依存）。全長だけなら arclen(pts)[length(pts)-1]。
//   例: arclen([[0,0],[3,0],[3,4]]) → [0, 3, 7]。隣接差分→cumsum で積分。
var arclen = \(pts){
    var n = length(pts);
    if ( n < 2 ) { return [0.0]; }
    concat([0.0], cumsum(map(range2(1, n), \(i){ vlen(pts[i] - pts[i - 1]); })));
};

// tube_wall_var(path, ds): tube 用の [v,r] 列 path を、各節で **垂直距離 ds[i]** だけ外側へ
//   オフセットした新しい [v,r] 列を返す（肉厚を節ごとに変えられる可変版）。
//   元のパイプと引き算したとき、面に垂直な壁厚が ds[i] になるようテーパ(半径変化)を補正する:
//   接線 t̂・弧長微分 r'=dr/ds に対し、[v,r] → [v - ds·r'/√(1+r'²)·t̂,  r + ds/√(1+r'²)]。
//     r'=0(円筒)なら半径に +ds するだけ。r'≠0(円錐/ラッパ)では中心線を接線方向へ引いて壁を垂直化。
//   ds を負にすると内側へオフセット(内壁)。path は 2 点以上・節は相異なること。
//   使い方: difference(tube_ruled(tube_wall_var(path, ds)), tube_ruled(path)) で肉厚可変シェル。
var tube_wall_var = \(path, ds){
    var n = length(path);
    if ( n < 2 ) { return path; }
    var V = map(path, \(p){ p[0]; });    // 中心線点
    var R = map(path, \(p){ p[1]; });    // 半径
    var S = arclen(V);                   // 弧長(r' の分母)
    var out = []; var i;
    for ( i = 0 ; i < n ; i = i + 1 ) {
        var ip = i; if ( i > 0 )     { ip = i - 1; }   // 後退側の基準(端点は片側差分)
        var iq = i; if ( i < n - 1 ) { iq = i + 1; }   // 前進側の基準
        var tv = V[iq] - V[ip];
        var tu = tv * (1.0 / vlen(tv));                // 単位接ベクトル t̂
        var rp = (R[iq] - R[ip]) / (S[iq] - S[ip]);    // r' = dr/ds
        var k  = sqrt(1.0 + rp * rp);
        var d  = ds[i];
        out[i] = [ V[i] - tu * (d * rp / k),  R[i] + d / k ];
    }
    out;
};

// tube_wall(path, d): 一定肉厚 d 版（tube_wall_var に一様 d を渡すだけ）。
//   difference(tube_ruled(tube_wall(path, d)), tube_ruled(path)) で肉厚 d 一定のパイプ壁が得られる。
var tube_wall = \(path, d){ tube_wall_var(path, map(path, \(p){ d; })); };

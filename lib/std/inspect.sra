// std/inspect.sra — モデル検査・解析結果の可視化ヘルパ。
//   include "std/inspect.sra"; で取り込む。thin_spots 等の解析 op を球マーカ等に変換する。
//
// ★必須モジュール: cgal
//   **thin_spots を持つのは cgal だけ**。combine も cgal / manifold の 2 本しか持たない。
//   ⇒ このライブラリは「カーネル非依存」ではない (旧コメントの誤り。2026-09-23 訂正)。
//   下で宣言するので利用側が書く必要はない。既にロード済みでも安全で、
//   **利用側が先に付けた priority 指定を壊さない**(素の module() は priority を上書きしない)。
module("cgal.so", {});

// thin_markers(solid, t_min, r): 肉厚が t_min 未満の「割れそうな箇所」に半径 r の球を立てた mesh を返す。
//   builtin thin_spots(solid, t_min)=[[x,y,z,thk],..] の各危険点に球を置いて combine(+++) する。
//   ★ 可視化用なので union(corefinement)ではなく combine(束ねるだけ)を使う(球が多いと union は激重)。
//   危険箇所が無ければ空({})。元モデルと +++ で重ねて viewer で見る:
//     var m = ...; m +++ thin_markers(m, 1.0, 0.5);
//   注: マーカ数はスポット数 N に比例し、N が数千になると(=モデルが一様に薄い)生成自体が重い。
//       その場合は t_min を下げる / thin_markers_band で帯域を絞る / cone を狭めて偽陽性を減らす。
var thin_markers = \(solid, t_min, r) {
    // 使う op = thin_spots → icosphere → transform(>>>) → combine。
    // ★ thin_spots は **cgal しか提供しない** ⇒ 必須。さらに後続が値を受け渡すので
    //   列を広げるとカーネルが混ざる ⇒ **1 本に絞る** (which・2026-09-24)。
    use [ module(["cgal"], {}) ];
    var sp = thin_spots(solid, t_min);
    combine(map(sp, \(p, i) { icosphere(r, 1) >>> [p[0], p[1], p[2]]; }));
};

// thin_spots_band(solid, t_lo, t_hi): 厚みが t_lo 以上 t_hi 未満の危険点だけ返す。
//   thin_spots は 0〜t_hi を全部拾うため、ブール演算の許容差(例: margin_t=0.01)由来の
//   **印刷できない極薄スリバー(< t_lo)**まで混ざる。t_lo に「印刷可能な下限」を入れて
//   ノイズを落とし、本当に問題になる薄肉(t_lo〜t_hi)だけを見るのに使う。
//   例: thin_spots_band(m, 0.5, 1.5) → 0.5〜1.5mm の薄肉のみ(0.04mm スリバーは無視)。
var thin_spots_band = \(solid, t_lo, t_hi) {
    // 使う op = thin_spots のみ (**cgal 専用**) ⇒ 必須として列の末尾に置く。
    use [ module(["cgal"], {}) ];
    var all = thin_spots(solid, t_hi);
    var out = []; var k = 0; var i;
    for ( i = 0 ; i < length(all) ; i = i + 1 ) {
        if ( all[i][3] >= t_lo ) { out[k] = all[i]; k = k + 1; }
    }
    out;
};

// thin_markers_band(solid, t_lo, t_hi, r): thin_spots_band の各点に半径 r の球を立てた mesh。
//   スリバーノイズを除いた薄肉だけを可視化する版。combine(+++) で束ねる。元モデルと +++ で重ねて確認。
var thin_markers_band = \(solid, t_lo, t_hi, r) {
    // thin_markers と同じ理由で cgal 1 本 (thin_spots_band も中で自分の列を宣言している)。
    use [ module(["cgal"], {}) ];
    var sp = thin_spots_band(solid, t_lo, t_hi);
    combine(map(sp, \(p, i) { icosphere(r, 1) >>> [p[0], p[1], p[2]]; }));
};

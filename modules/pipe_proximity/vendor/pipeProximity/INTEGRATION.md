# INTEGRATION — cgal-processor へのプラグイン組み込み

pipe_proximity を `cgal-processor` にプラグインとして組み込むための手引き。
**ライブラリ本体は CGAL に非依存**で、API 境界には CGAL 型が一切出てこない。
したがって統合は「**薄いアダプタ（CGAL ↔ pipe の型変換）＋ host のプラグイン登録部**」を
host 側で書くだけで済む。境界が小さいのが利点。

---

## 1. ビルドへの取り込み

CMake なら sub-project として：

```cmake
add_subdirectory(third_party/pipe_proximity)   # pipeprox ターゲットと include/ を提供
target_link_libraries(your_plugin PRIVATE pipeprox)
```

`pipeprox` はスタティックライブラリ。外部依存なし（C++17）。host が CGAL/GPL でも
pipe_proximity は MIT のまま（互換）。まず `ctest --test-dir build` と `./build/demo` が
host 環境で通ることを確認するのが最短の疎通確認。

---

## 2. 境界を越える型（これだけ）

| pipe 型 | 役割 | 備考 |
|---|---|---|
| `pipe::Vec3{double x,y,z}` | 3D 点・ベクトル | CGAL `Point_3` から変換 |
| `pipe::ChainDesign{S,E,C}` | パイプ中心線の設計（最適化変数） | 両端通過点＋off-curve 制御点列 |
| `pipe::RadiusFn` | `function<optional<double>(double s)>` | 弧長→半径。`nullopt`=管端 |
| `pipe::Scene{vector<Body>}` | 複数本（固定＋可動） | `Body{design,chain,radius,movable}` |
| `pipe::Contact` | 1接近箇所の結果 | gap/接触点/法線/弧長/半径 |
| `pipe::Params` / `CtrlParams` | 検出 / 調整の設定 | |
| `pipe::CtrlResult` | 調整の結果 | design/contacts/feasibility |

**host 側に返すのは基本 `std::vector<Contact>`**（接触点 `pA,pB`・隙間 `gap`・法線 `normal`・
位置 `bodyA/segA/sA` …）。これを host の可視化／編集表現に変換する。

---

## 3. アダプタ雛形（CGAL ↔ pipe）

```cpp
#include "pipe/scene.hpp"
#include "pipe/controller.hpp"
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
using K = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = K::Point_3;

// --- 点変換 ---
static inline pipe::Vec3 toVec(const Point_3& p){
    return { CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()) };
}
static inline Point_3 toCGAL(const pipe::Vec3& v){ return Point_3(v.x, v.y, v.z); }

// --- ポリライン(off-curve 制御点列) -> ChainDesign ---
//   中点方式: 両端 S,E は通過点、中間は制御点（曲線は通らない）。
//   host が「通過点列」を持つ場合は別途 補間（INTEGRATION 末尾の注記参照）。
pipe::ChainDesign toDesign(const std::vector<Point_3>& ctrl /* S, C0..Cm-1, E */){
    pipe::ChainDesign d;
    d.S = toVec(ctrl.front());
    d.E = toVec(ctrl.back());
    for(size_t i=1; i+1<ctrl.size(); ++i) d.C.push_back(toVec(ctrl[i]));
    return d;
}

// --- 半径フィールド -> RadiusFn（弧長 s, 範囲外は nullopt = 管端）---
//   例: 始点半径 r0、係数 m の指数。host の半径テーブルなら補間して返す。
pipe::RadiusFn makeRadius(double r0, double m, double sMax){
    return [=](double s) -> std::optional<double>{
        if(s < 0 || s > sMax) return std::nullopt;
        return r0 * std::exp(m * s);
    };
}
```

---

## 4. 呼び出し（検出・調整）

```cpp
// シーン構築: 固定ライン複数 + 可動ライン
pipe::Scene sc;
for(const auto& poly : fixedPipes){
    pipe::Body b; b.movable=false; b.design=toDesign(poly.ctrl);
    b.radius=makeRadius(poly.r0, poly.m, poly.sMax); sc.bodies.push_back(b);
}
pipe::Body mv; mv.movable=true; mv.design=toDesign(movingPipe.ctrl);
mv.radius=makeRadius(movingPipe.r0, movingPipe.m, movingPipe.sMax);
int movableIdx = (int)sc.bodies.size(); sc.bodies.push_back(mv);
sc.rebuildAll();

// (a) 干渉検出だけ
pipe::Params det; det.reportGap = clearanceMargin;       // これ以下を報告
auto contacts = pipe::findSceneProximities(sc, det);

// (b) クリアランスを保って平衡へ調整
pipe::CtrlParams cp;
cp.dMin = clearanceMargin;     // 一様クリアランス（gap>=dMin）
cp.wLen = 0.5; cp.wBend = 0.2; // テンション/曲げ
cp.fZ = -gravity;              // 重力（任意）
cp.alOuter = 6;               // 拡張ラグランジュ（クリアランス厳密化, 1=純ペナルティ）
cp.ccd = true;                // すり抜け防止（トポロジー保存）。既定で保守的前進
cp.fixedDOF = { /* 固定したい設計点 index: 0=S,1..m=C,m+1=E */ };
pipe::CtrlResult res = pipe::adjustScene(sc, movableIdx, cp);

// res.design を host のパイプ表現に書き戻す
auto newCtrl = std::vector<Point_3>{ toCGAL(res.design.S) };
for(const auto& c : res.design.C) newCtrl.push_back(toCGAL(c));
newCtrl.push_back(toCGAL(res.design.E));
// res.contacts / res.constraintsFeasible / res.maxClearViolation も参照
```

---

## 5. 結果（`Contact`）の読み方

```cpp
for(const pipe::Contact& c : contacts){
    c.gap;            // 表面間の隙間（負方向の貫通は CCD/penetration 判定で別途）
    c.pA, c.pB;       // 2本の表面上の最近接点（toCGAL で host へ）
    c.normal;         // (pA-pB)/gap 接触法線（押し離す向き）
    c.bodyA, c.bodyB; // どの Body 間か（同値なら自己接近）
    c.segA, c.segB;   // セグメント番号 / c.tA,c.tB ローカル param
    c.sA, c.sB;       // 始点からの弧長（位置）/ c.rA,c.rB その位置の半径
}
```

---

## 6. プラグイン登録部（host 側で書く）

cgal-processor のプラグイン規約（登録マクロ／期待するインターフェース／I-O のデータ型）に
合わせて、上記アダプタを呼ぶ薄い登録部を書く。pipe 側は**ステートレスな関数群**なので、

- 各呼び出しは自己完結（グローバル状態なし）。複数パイプ／複数フレームを順に処理可能。
- スレッドから呼ぶ場合、別々の `Scene`/`ChainDesign` を渡す限り互いに独立（共有可変状態なし）。

> cgal-processor のプラグイン API ドキュメントを共有してもらえれば、この章の登録部の雛形まで
> 具体化できる（現状は host 非依存の境界までを提示）。

---

## 7. つまずきやすい点（チェックリスト）

- **単位**：`dMin` / `reportGap` / 半径 / 座標は同一単位系で揃える。
- **`reportGap ≥ dMin`**：調整器は内部で自動的に引き上げるが、検出だけ使うときは明示的に。
- **除外帯 `kExclude·r`**：自己接近で隣接の自明な重なりを除外する帯（既定 k=3）。
  緩い曲率で隣接が誤検出されるなら k を上げる。
- **`κ·r < 1` 前提**：太すぎる急曲げ（局所自己交差）は対象外。host 側で弾くか細める。
- **半径ラムダの定義域 = 管端**：`nullopt` を返す `s` から先はパイプが存在しない扱い。
- **中点方式と「通過点」**：制御点（off-curve）は曲線が通らない。host が「必ず通る点列」を
  持つ場合は、(b) 通過点補間で制御点を逆算するか、(b') 内部通過点ピン（`Pin`）で寄せる。
- **フリー端のチヂミ**：テンション下で自由端は短くなる方へ後退する。意図しないなら端を固定するか、
  「欲しい終端の先に延長セグメントを足してその先端を固定」する運用で制御する。
- **CCD のコスト**：`ccd=true` は反復ごとに追加検出が走る。トポロジー保存が要る場面でのみ on。

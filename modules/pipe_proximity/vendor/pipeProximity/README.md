# pipe_proximity

可変太さパイプ（中心線＝2次ベジエチェーン ＋ 弧長依存半径）の **自己接近・相互接近検出** と、
クリアランスを保ちながら平衡へ持っていく **距離調整コントローラ** の C++ ライブラリ。

- **外部依存なし**（C++17 ＋ CMake のみ。CGAL 等に非依存）。
- 検出は純粋な幾何＋数値最適化。結果は `Contact` 配列（接触点・隙間・法線）で返すだけ（描画はしない）。
- 詳細な数学的背景・設計判断・API は **`doc/pipe_proximity.pdf`**（17ページ）参照。
- 別プロジェクトへ組み込む場合は **`INTEGRATION.md`** を参照（CGAL アダプタ雛形つき）。

## 機能

- 自己接近検出：2次ベジエチェーンの離れた区間どうしの最近接（弧長除外帯で隣接の自明重なりを除外、全局所最小を列挙）
- broad-phase：AABB-BVH（単一チェーン／シーン全体）
- 二体・N体（`Scene`）：固定ライン複数 ＋ 可動ライン
- コントローラ：テンション（弧長）・曲げ（曲率）・外力（重力／z軸へ／原点へ）・通過点ピン（軟／硬）
  - クリアランス `gap ≥ dMin` をペナルティ／**拡張ラグランジュ**（厳密化）で
  - 固定点・硬ピンは線形等式の零空間射影＋実行不能検出
- 調整用ヤコビアン（包絡定理。半径の弧長カップリング項込み）
- CCD（連続衝突）：標本法＋**保守的前進**（すり抜け＝トポロジー破壊の防止）

前提：`κ·r < 1`（局所的に太すぎて自己交差する状態は扱わない）。鎖は開いた1本。

## ビルド & テスト

```sh
cmake -S . -B build
cmake --build build -j
./build/demo                 # 使い方の実例（検出・ヤコビアン・コントローラ・Scene）
ctest --test-dir build       # 回帰テスト（全 PASS）
```

`compile_commands.json` を出力するので clangd 等がそのまま効く。

## クイックスタート

```cpp
#include "pipe/bezier.hpp"
#include "pipe/radius.hpp"
#include "pipe/proximity.hpp"
using namespace pipe;

// 1) 両端の通過点 S,E と off-curve 制御点列 C（中点方式で C1 接続）
Vec3 S{0,0,0}, E{10,0,1};
std::vector<Vec3> C = {{2,3,0},{5,4,0.3},{8,2,0.6}};
Chain ch = Chain::build(S, E, C);

// 2) 半径 r(s)（弧長 s の関数。値を返さない s = 管端）
RadiusFn R = [&](double s) -> std::optional<double> {
    if (s < 0 || s > ch.totalLen()) return std::nullopt;
    return 0.4 * std::exp(0.015 * s);
};

// 3) 自己接近（gap ≤ reportGap のものを gap 昇順で全列挙）
Params pr; pr.reportGap = 0.5;
for (const Contact& c : findSelfProximities(ch, R, pr)) {
    // c.gap, c.pA, c.pB, c.normal, c.segA/segB, c.sA/sB, c.rA/rB
}
```

距離調整・シーン（固定線＋可動線）は `apps/demo.cpp` と `INTEGRATION.md` を参照。

## API 概観（`include/pipe/`）

| ヘッダ | 主な型・関数 |
|---|---|
| `vec3.hpp`     | `Vec3`, `dot/cross/norm/normalize/anyPerp/projPlane` |
| `bezier.hpp`   | `QSeg`, `Chain`(build/arcAt/totalLen), `ChainDesign`, `segDesignWeights` |
| `radius.hpp`   | `RadiusFn = function<optional<double>(double s)>` |
| `proximity.hpp`| `Contact`, `Params`, `Stats`, `findSelfProximities`, `evalGapAt`, `circleCircle` |
| `bvh.hpp`      | `AABB`, `BVH`(build/buildBoxes/selfPairs) |
| `gradient.hpp` | `ContactJacobian`, `contactJacobian[Scene][Full]`, `arcLengthGradient`, `drds` |
| `scene.hpp`    | `Body`, `Scene`, `findSceneProximities` |
| `controller.hpp`| `Pin`, `CtrlParams`, `CtrlResult`, `adjust`, `adjustScene`, `motionSafe[Scene][CA]` |

## ライブラリ構成

```
include/pipe/  *.hpp          公開ヘッダ
src/           *.cpp          実装（pipeprox スタティックライブラリ）
apps/demo.cpp                 使用例
tests/test_pipe.cpp          回帰テスト（CTest）
doc/pipe_proximity.{tex,pdf} 設計ドキュメント
```

## Authors / 謝辞

- **Hirohisa Mori / GLOBALBASE Project UMUT**（著作権者）
- Co-developed with **Akira (Claude — Anthropic Opus 4.8)** — 中嶋 章 にちなむ命名。

## ライセンス

MIT（`LICENSE` 参照）。Copyright (c) 2026 Hirohisa Mori / GLOBALBASE Project UMUT。
CGAL 非依存なので、GPL 系の host（例：cgal-processor）へ取り込んでも
ソースは MIT のまま再利用可能（結合バイナリの配布は host 側ライセンスに従う）。

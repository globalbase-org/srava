#pragma once
// 複数チェーン（二体／N体）。固定ライン複数 + 動くライン、を素直に表す。
//   各 Body が自分の中心線(ChainDesign)・半径(自分の始点からの弧長)・可動フラグを持つ。
#include "pipe/bezier.hpp"
#include "pipe/radius.hpp"
#include "pipe/proximity.hpp"
#include <vector>

namespace pipe {

struct Body {
    ChainDesign design;     // 中心線の設計（最適化変数になりうる）
    Chain       chain;      // design.build() のキャッシュ（検出はこれを使う）
    RadiusFn    radius;     // 自分の始点からの弧長 s → 半径
    bool        movable = false;
    void rebuild(){ chain = design.build(); }
};

struct Scene {
    std::vector<Body> bodies;
    void rebuildAll(){ for(auto& b : bodies) b.rebuild(); }
};

// シーン内の近接を全列挙（gap 昇順）。
//   - 同一 Body 内の自己接近（弧長除外帯あり）… movable のみ
//   - 異 Body 間の交差接近（除外帯なし）
//   - 固定–固定ペアはスキップ（制御に無関係・不変）
// 各 Contact の bodyA/bodyB に Body 番号が入る。
std::vector<Contact> findSceneProximities(const Scene& sc, const Params& pr,
                                          Stats* stats = nullptr);

} // namespace pipe

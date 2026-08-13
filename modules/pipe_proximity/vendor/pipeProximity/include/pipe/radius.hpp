#pragma once
// 半径プロファイル: 始点からの弧長 s → 半径。
// 値を返さなければ（nullopt）「そこにパイプは無い」= 管端（キャップを domain で表現）。
#include <functional>
#include <optional>

namespace pipe {

using RadiusFn = std::function<std::optional<double>(double s)>;

} // namespace pipe

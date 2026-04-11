#pragma once

#include <array>
#include <string>

namespace raceman {

inline constexpr int kPhysicsLayerCount = 8;

using PhysicsLayerNames = std::array<std::string, kPhysicsLayerCount>;
using PhysicsLayerCollisionMatrix = std::array<std::array<bool, kPhysicsLayerCount>, kPhysicsLayerCount>;

} // namespace raceman

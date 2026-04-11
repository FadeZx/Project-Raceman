#pragma once

#include <cstdint>

namespace raceman {

enum class MeshColliderBuildQuality : std::uint8_t {
    BuildSpeed,
    Balanced,
    BuildQuality
};

} // namespace raceman

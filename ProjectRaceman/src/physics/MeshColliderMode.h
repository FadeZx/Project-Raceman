#pragma once

#include <cstdint>

namespace raceman {

enum class MeshColliderMode : std::uint8_t {
    TriangleMesh,
    ConvexHull
};

} // namespace raceman

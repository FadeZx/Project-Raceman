#pragma once

#include <glm/glm.hpp>

namespace raceman {

struct MeshResource {
    unsigned int vao{0};
    unsigned int vbo{0};
    unsigned int ebo{0};
    unsigned int indexCount{0};
    glm::mat4 transform{1.0f};
};

MeshResource CreateUnitCubeMesh(float scale = 1.0f);

} // namespace raceman

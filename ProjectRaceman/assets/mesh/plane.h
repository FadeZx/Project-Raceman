#pragma once

#include <vector>
#include <glm/glm.hpp>

// Include the project's Vertex definition
// Adjust the relative path if your include setup differs
#include "../../src/rendering/mesh.h"

namespace raceman {
namespace assets {
namespace mesh {
namespace plane {

// Unit XZ plane at y = 0, centered at origin, size 1x1
// Vertex layout matches src/rendering/mesh.h::Vertex
inline std::vector<Vertex> getVertices() {
    std::vector<Vertex> v(4);

    // Common attributes
    const glm::vec3 n(0.0f, 1.0f, 0.0f);   // normal up
    const glm::vec3 t(1.0f, 0.0f, 0.0f);   // tangent +X
    const glm::vec3 b(0.0f, 0.0f, 1.0f);   // bitangent +Z

    // Initialize bone IDs/weights to zero
    auto initSkin = [](Vertex& vx) {
        vx.m_BoneIDs[0] = vx.m_BoneIDs[1] = vx.m_BoneIDs[2] = vx.m_BoneIDs[3] = 0;
        vx.m_Weights[0] = vx.m_Weights[1] = vx.m_Weights[2] = vx.m_Weights[3] = 0.0f;
    };

    // Positions (x, y, z) and UVs
    // 0: (-0.5, 0, -0.5) UV (0,0)
    v[0].Position = glm::vec3(-0.5f, 0.0f, -0.5f);
    v[0].Normal   = n;
    v[0].TexCoords= glm::vec2(0.0f, 0.0f);
    v[0].Tangent  = t;
    v[0].Bitangent= b;
    initSkin(v[0]);

    // 1: ( 0.5, 0, -0.5) UV (1,0)
    v[1].Position = glm::vec3( 0.5f, 0.0f, -0.5f);
    v[1].Normal   = n;
    v[1].TexCoords= glm::vec2(1.0f, 0.0f);
    v[1].Tangent  = t;
    v[1].Bitangent= b;
    initSkin(v[1]);

    // 2: ( 0.5, 0,  0.5) UV (1,1)
    v[2].Position = glm::vec3( 0.5f, 0.0f,  0.5f);
    v[2].Normal   = n;
    v[2].TexCoords= glm::vec2(1.0f, 1.0f);
    v[2].Tangent  = t;
    v[2].Bitangent= b;
    initSkin(v[2]);

    // 3: (-0.5, 0,  0.5) UV (0,1)
    v[3].Position = glm::vec3(-0.5f, 0.0f,  0.5f);
    v[3].Normal   = n;
    v[3].TexCoords= glm::vec2(0.0f, 1.0f);
    v[3].Tangent  = t;
    v[3].Bitangent= b;
    initSkin(v[3]);

    return v;
}

inline std::vector<unsigned int> getIndices() {
    // Two triangles: (0,1,2) and (2,3,0)
    return { 0, 1, 2, 2, 3, 0 };
}

} // namespace plane
} // namespace mesh
} // namespace assets
} // namespace raceman
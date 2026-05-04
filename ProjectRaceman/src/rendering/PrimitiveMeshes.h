#pragma once

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace raceman {

class PrimitiveMesh {
public:
    struct Vertex {
        float x, y, z;
        float nx, ny, nz;
        float u, v;
    };

    PrimitiveMesh() = default;
    PrimitiveMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
        create(vertices, indices);
    }

    ~PrimitiveMesh() { destroy(); }

    PrimitiveMesh(const PrimitiveMesh&) = delete;
    PrimitiveMesh& operator=(const PrimitiveMesh&) = delete;

    PrimitiveMesh(PrimitiveMesh&& other) noexcept {
        moveFrom(other);
    }

    PrimitiveMesh& operator=(PrimitiveMesh&& other) noexcept {
        if (this != &other) {
            destroy();
            moveFrom(other);
        }
        return *this;
    }

    void reset(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
        destroy();
        create(vertices, indices);
    }

    unsigned int vao() const { return vao_; }
    unsigned int indexCount() const { return indexCount_; }

private:
    void create(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
        if (vertices.empty() || indices.empty()) {
            return;
        }

        indexCount_ = static_cast<unsigned int>(indices.size());

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(6 * sizeof(float)));

        glBindVertexArray(0);
    }

    void destroy() {
        if (ebo_ != 0) {
            glDeleteBuffers(1, &ebo_);
            ebo_ = 0;
        }
        if (vbo_ != 0) {
            glDeleteBuffers(1, &vbo_);
            vbo_ = 0;
        }
        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
            vao_ = 0;
        }
        indexCount_ = 0;
    }

    void moveFrom(PrimitiveMesh& other) noexcept {
        vao_ = other.vao_;
        vbo_ = other.vbo_;
        ebo_ = other.ebo_;
        indexCount_ = other.indexCount_;
        other.vao_ = 0;
        other.vbo_ = 0;
        other.ebo_ = 0;
        other.indexCount_ = 0;
    }

    unsigned int vao_{0};
    unsigned int vbo_{0};
    unsigned int ebo_{0};
    unsigned int indexCount_{0};
};

inline PrimitiveMesh CreatePlanePrimitiveMesh() {
    using V = PrimitiveMesh::Vertex;
    const std::vector<V> vertices = {
        {-0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        { 0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        { 0.5f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-0.5f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
    };
    const std::vector<unsigned int> indices = {0, 2, 1, 2, 0, 3, 0, 1, 2, 2, 3, 0};
    return PrimitiveMesh(vertices, indices);
}

inline PrimitiveMesh CreateCubePrimitiveMesh() {
    using V = PrimitiveMesh::Vertex;
    const std::vector<V> vertices = {
        {-0.5f,-0.5f, 0.5f, 0,0,1, 0,0}, { 0.5f,-0.5f, 0.5f, 0,0,1, 1,0}, { 0.5f, 0.5f, 0.5f, 0,0,1, 1,1}, {-0.5f, 0.5f, 0.5f, 0,0,1, 0,1},
        { 0.5f,-0.5f,-0.5f, 0,0,-1, 0,0}, {-0.5f,-0.5f,-0.5f, 0,0,-1, 1,0}, {-0.5f, 0.5f,-0.5f, 0,0,-1, 1,1}, { 0.5f, 0.5f,-0.5f, 0,0,-1, 0,1},
        {-0.5f,-0.5f,-0.5f,-1,0,0, 0,0}, {-0.5f,-0.5f, 0.5f,-1,0,0, 1,0}, {-0.5f, 0.5f, 0.5f,-1,0,0, 1,1}, {-0.5f, 0.5f,-0.5f,-1,0,0, 0,1},
        { 0.5f,-0.5f, 0.5f, 1,0,0, 0,0}, { 0.5f,-0.5f,-0.5f, 1,0,0, 1,0}, { 0.5f, 0.5f,-0.5f, 1,0,0, 1,1}, { 0.5f, 0.5f, 0.5f, 1,0,0, 0,1},
        {-0.5f, 0.5f, 0.5f, 0,1,0, 0,0}, { 0.5f, 0.5f, 0.5f, 0,1,0, 1,0}, { 0.5f, 0.5f,-0.5f, 0,1,0, 1,1}, {-0.5f, 0.5f,-0.5f, 0,1,0, 0,1},
        {-0.5f,-0.5f,-0.5f, 0,-1,0, 0,0}, { 0.5f,-0.5f,-0.5f, 0,-1,0, 1,0}, { 0.5f,-0.5f, 0.5f, 0,-1,0, 1,1}, {-0.5f,-0.5f, 0.5f, 0,-1,0, 0,1},
    };
    const std::vector<unsigned int> indices = {
        0,1,2, 2,3,0, 4,5,6, 6,7,4, 8,9,10, 10,11,8,
        12,13,14, 14,15,12, 16,17,18, 18,19,16, 20,21,22, 22,23,20
    };
    return PrimitiveMesh(vertices, indices);
}

inline PrimitiveMesh CreateSpherePrimitiveMesh(int slices = 24, int stacks = 16) {
    using V = PrimitiveMesh::Vertex;
    slices = (std::max)(slices, 3);
    stacks = (std::max)(stacks, 2);

    std::vector<V> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(static_cast<std::size_t>((stacks + 1) * (slices + 1)));

    constexpr float pi = 3.14159265358979323846f;
    for (int stack = 0; stack <= stacks; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(stacks);
        const float phi = v * pi;
        const float y = std::cos(phi) * 0.5f;
        const float ringRadius = std::sin(phi) * 0.5f;
        for (int slice = 0; slice <= slices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * (2.0f * pi);
            const float x = std::cos(theta) * ringRadius;
            const float z = std::sin(theta) * ringRadius;
            const float nx = ringRadius > 0.0f ? x / 0.5f : 0.0f;
            const float ny = y / 0.5f;
            const float nz = ringRadius > 0.0f ? z / 0.5f : 0.0f;
            vertices.push_back({x, y, z, nx, ny, nz, u, 1.0f - v});
        }
    }

    for (int stack = 0; stack < stacks; ++stack) {
        for (int slice = 0; slice < slices; ++slice) {
            const unsigned int first = static_cast<unsigned int>(stack * (slices + 1) + slice);
            const unsigned int second = first + static_cast<unsigned int>(slices + 1);
            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(first + 1);
            indices.push_back(second);
            indices.push_back(second + 1);
        }
    }

    return PrimitiveMesh(vertices, indices);
}

inline PrimitiveMesh CreateCylinderPrimitiveMesh(int slices = 24) {
    using V = PrimitiveMesh::Vertex;
    slices = (std::max)(slices, 3);
    std::vector<V> vertices;
    std::vector<unsigned int> indices;
    constexpr float pi = 3.14159265358979323846f;

    for (int i = 0; i <= slices; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(slices);
        const float angle = u * (2.0f * pi);
        const float x = std::cos(angle) * 0.5f;
        const float z = std::sin(angle) * 0.5f;
        vertices.push_back({x, -0.5f, z, x / 0.5f, 0.0f, z / 0.5f, u, 0.0f});
        vertices.push_back({x,  0.5f, z, x / 0.5f, 0.0f, z / 0.5f, u, 1.0f});
    }

    for (int i = 0; i < slices; ++i) {
        const unsigned int base = static_cast<unsigned int>(i * 2);
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 3);
    }

    const unsigned int topCenter = static_cast<unsigned int>(vertices.size());
    vertices.push_back({0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f});
    const unsigned int bottomCenter = static_cast<unsigned int>(vertices.size());
    vertices.push_back({0.0f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f});

    for (int i = 0; i <= slices; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(slices);
        const float angle = u * (2.0f * pi);
        const float x = std::cos(angle) * 0.5f;
        const float z = std::sin(angle) * 0.5f;
        vertices.push_back({x, 0.5f, z, 0.0f, 1.0f, 0.0f, x + 0.5f, z + 0.5f});
        vertices.push_back({x, -0.5f, z, 0.0f, -1.0f, 0.0f, x + 0.5f, z + 0.5f});
    }

    const unsigned int topStart = bottomCenter + 1;
    for (int i = 0; i < slices; ++i) {
        indices.push_back(topCenter);
        indices.push_back(topStart + static_cast<unsigned int>(i));
        indices.push_back(topStart + static_cast<unsigned int>(i + 1));
    }

    const unsigned int bottomStart = topStart + static_cast<unsigned int>(slices + 1);
    for (int i = 0; i < slices; ++i) {
        indices.push_back(bottomCenter);
        indices.push_back(bottomStart + static_cast<unsigned int>(i + 1));
        indices.push_back(bottomStart + static_cast<unsigned int>(i));
    }

    return PrimitiveMesh(vertices, indices);
}

inline PrimitiveMesh CreateCapsulePrimitiveMesh(int slices = 24, int hemisphereStacks = 8) {
    using V = PrimitiveMesh::Vertex;
    slices = (std::max)(slices, 3);
    hemisphereStacks = (std::max)(hemisphereStacks, 2);

    std::vector<V> vertices;
    std::vector<unsigned int> indices;
    constexpr float pi = 3.14159265358979323846f;
    const float radius = 0.25f;
    const float halfCylinderHeight = 0.25f;

    auto addRing = [&](float centerY, float phi, float v) {
        const float ringRadius = std::sin(phi) * radius;
        const float normalY = std::cos(phi);
        const float y = centerY + normalY * radius;
        for (int i = 0; i <= slices; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(slices);
            const float angle = u * (2.0f * pi);
            const float normalX = std::cos(angle) * std::sin(phi);
            const float normalZ = std::sin(angle) * std::sin(phi);
            vertices.push_back({
                std::cos(angle) * ringRadius,
                y,
                std::sin(angle) * ringRadius,
                normalX,
                normalY,
                normalZ,
                u,
                v
            });
        }
    };

    for (int stack = 0; stack <= hemisphereStacks; ++stack) {
        const float t = static_cast<float>(stack) / static_cast<float>(hemisphereStacks);
        addRing(halfCylinderHeight, t * (pi * 0.5f), t * 0.5f);
    }
    for (int stack = 0; stack <= hemisphereStacks; ++stack) {
        const float t = static_cast<float>(stack) / static_cast<float>(hemisphereStacks);
        addRing(-halfCylinderHeight, (pi * 0.5f) + t * (pi * 0.5f), 0.5f + t * 0.5f);
    }

    const int rings = (hemisphereStacks + 1) * 2;
    for (int ring = 0; ring < rings - 1; ++ring) {
        for (int slice = 0; slice < slices; ++slice) {
            const unsigned int first = static_cast<unsigned int>(ring * (slices + 1) + slice);
            const unsigned int second = first + static_cast<unsigned int>(slices + 1);
            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(first + 1);
            indices.push_back(second);
            indices.push_back(second + 1);
        }
    }

    return PrimitiveMesh(vertices, indices);
}

inline PrimitiveMesh CreateConePrimitiveMesh(int slices = 24) {
    using V = PrimitiveMesh::Vertex;
    slices = (std::max)(slices, 3);
    std::vector<V> vertices;
    std::vector<unsigned int> indices;
    constexpr float pi = 3.14159265358979323846f;

    const float radius = 0.5f;
    const float halfHeight = 0.5f;
    for (int i = 0; i <= slices; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(slices);
        const float angle = u * (2.0f * pi);
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;
        const float nx = x;
        const float nz = z;
        const float ny = radius;
        const float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
        vertices.push_back({x, -halfHeight, z, nx * invLen, ny * invLen, nz * invLen, u, 0.0f});
        vertices.push_back({0.0f, halfHeight, 0.0f, nx * invLen, ny * invLen, nz * invLen, u, 1.0f});
    }

    for (int i = 0; i < slices; ++i) {
        const unsigned int base = static_cast<unsigned int>(i * 2);
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }

    const unsigned int centerIndex = static_cast<unsigned int>(vertices.size());
    vertices.push_back({0.0f, -halfHeight, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f});
    const unsigned int ringStart = static_cast<unsigned int>(vertices.size());
    for (int i = 0; i <= slices; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(slices);
        const float angle = u * (2.0f * pi);
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;
        vertices.push_back({x, -halfHeight, z, 0.0f, -1.0f, 0.0f, x + 0.5f, z + 0.5f});
    }
    for (int i = 0; i < slices; ++i) {
        indices.push_back(centerIndex);
        indices.push_back(ringStart + static_cast<unsigned int>(i + 1));
        indices.push_back(ringStart + static_cast<unsigned int>(i));
    }

    return PrimitiveMesh(vertices, indices);
}

} // namespace raceman

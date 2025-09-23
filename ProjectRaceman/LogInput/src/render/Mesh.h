#pragma once
// Mesh.h – simple OpenGL mesh (VAO/VBO/EBO) + tiny .obj loader (triangles only)

#include "../core/GlmCompat.h"
#include <vector>
#include <string>
#include <cstdint>
#include <glm/glm.hpp>
#include <glad/glad.h>

namespace Render {

    struct Vertex {
        glm::vec3 pos{ 0 };
        glm::vec3 nrm{ 0,1,0 };
        glm::vec2 uv{ 0 };
    };

    class Mesh {
    public:
        Mesh() = default;
        ~Mesh() { destroy(); }

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;

        Mesh(Mesh&& rhs) noexcept { moveFrom(std::move(rhs)); }
        Mesh& operator=(Mesh&& rhs) noexcept {
            if (this != &rhs) { destroy(); moveFrom(std::move(rhs)); }
            return *this;
        }

        // Build from CPU-side arrays and upload to GPU
        bool build(const std::vector<Vertex>& vertices,
            const std::vector<uint32_t>& indices);

        // Convenience: load very basic .obj (v/vt/vn/f with triangles) into this mesh
        // Returns false on failure; 'errOut' holds a message.
        bool loadOBJ(const std::string& path, std::string* errOut = nullptr);

        void draw() const;      // glDrawElements
        void destroy();

        inline bool valid() const { return vao_ != 0; }
        inline size_t indexCount() const { return indexCount_; }

    private:
        void moveFrom(Mesh&& rhs) noexcept {
            vao_ = rhs.vao_; vbo_ = rhs.vbo_; ebo_ = rhs.ebo_;
            indexCount_ = rhs.indexCount_;
            rhs.vao_ = rhs.vbo_ = rhs.ebo_ = 0; rhs.indexCount_ = 0;
        }

        GLuint vao_ = 0;
        GLuint vbo_ = 0;
        GLuint ebo_ = 0;
        size_t indexCount_ = 0;
    };

} // namespace Render

// Mesh.h (additions)
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <glad/glad.h>
#include <glm/glm.hpp>


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

        bool build(const std::vector<Vertex>& vertices,
            const std::vector<uint32_t>& indices);

        // Loads OBJ and (optionally) its MTL; will set a diffuse texture if map_Kd found
        bool loadOBJ(const std::string& path, std::string* errOut = nullptr);

        void draw() const;
        void destroy();

        bool valid() const { return vao_ != 0 && indexCount_ > 0; }

        // Texture accessors (0 if none)
        GLuint texture() const { return tex_; }
        const std::string& texturePath() const { return texPath_; }
        Mesh(Mesh&& rhs) noexcept { moveFrom(std::move(rhs)); }
        Mesh& operator=(Mesh&& rhs) noexcept {
            if (this != &rhs) { destroy(); moveFrom(std::move(rhs)); }
            return *this;
        }

    private:
        void moveFrom(Mesh&& r) noexcept {
            vao_ = r.vao_;   r.vao_ = 0;
            vbo_ = r.vbo_;   r.vbo_ = 0;
            ebo_ = r.ebo_;   r.ebo_ = 0;
            indexCount_ = r.indexCount_; r.indexCount_ = 0;
            tex_ = r.tex_;   r.tex_ = 0;
            texPath_ = std::move(r.texPath_);
        }

        // your fields:
        GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
        size_t indexCount_ = 0;
        GLuint tex_ = 0;
        std::string texPath_;
    };

} // namespace Render

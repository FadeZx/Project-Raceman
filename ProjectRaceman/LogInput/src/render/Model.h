#pragma once
#include "Mesh.h"
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace Render {

    class Model {
    public:
        // Load a single-mesh OBJ
        bool loadOBJ(const std::string& path, std::string* errOut = nullptr) {
            Mesh m;
            if (!m.loadOBJ(path, errOut)) return false;
            meshes_.clear();
            meshes_.push_back(std::move(m));
            return true;
        }

        GLuint texture() const { return meshes_.empty() ? 0u : meshes_[0].texture(); }
        const std::string& texturePath() const {
            static std::string empty;
            return meshes_.empty() ? empty : meshes_[0].texturePath();
        }

        // Draw all sub-meshes
        void draw() const {
            for (const auto& m : meshes_) m.draw();
        }

        bool valid() const { return !meshes_.empty() && meshes_[0].valid(); }

        // --- NEW: optional per-model local transform (defaults to identity) ---
        void setLocal(const glm::mat4& m) { local_ = m; }
        const glm::mat4& local() const { return local_; }

    private:
        std::vector<Mesh> meshes_;
        glm::mat4 local_{ 1.0f };  // identity by default
    };

} // namespace Render

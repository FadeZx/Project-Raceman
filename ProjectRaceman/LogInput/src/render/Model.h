#pragma once
#include "Mesh.h"
#include <vector>
#include <string>

namespace Render {

    // Very small wrapper if you want multiple sub-meshes per model.
    // For now, single-mesh convenience still works.
    class Model {
    public:
        bool loadOBJ(const std::string& path, std::string* errOut = nullptr) {
            Mesh m;
            if (!m.loadOBJ(path, errOut)) return false;
            meshes_.clear();
            meshes_.push_back(std::move(m));
            return true;
        }

        void draw() const {
            for (auto& m : meshes_) m.draw();
        }

        inline bool valid() const { return !meshes_.empty() && meshes_[0].valid(); }

    private:
        std::vector<Mesh> meshes_;

    };

} // namespace Render

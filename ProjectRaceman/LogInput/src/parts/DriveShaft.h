#pragma once
#include "../core/GlmCompat.h"
#include <glm/glm.hpp>
#include "../render/Model.h"

namespace Vehicle {

    struct DriveShaft {
        Render::Model model;
        glm::vec3 localA{ 0,0,0 }; // gearbox/output side in vehicle space
        glm::vec3 localB{ 0,0,0 }; // hub side in vehicle space

        // For now purely visual: we scale/rotate a cylinder from A→B
        void setModel(Render::Model&& m) { model = std::move(m); }
        void draw(const glm::mat4& vehicleXform, GLuint shader, const glm::vec3& color = { 0.7f,0.7f,0.7f }) const;
    };

} // namespace Vehicle

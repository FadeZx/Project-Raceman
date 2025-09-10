#pragma once
#include <glm/glm.hpp>
#include "../render/Model.h"

namespace Vehicle {

    struct WheelParams {
        glm::vec3 localOffset = { 0.0f, 0.0f, 0.0f }; // relative to vehicle origin
        float radius = 0.35f;
        bool  steerable = true; // front wheels true
        bool  driven = true; // for visuals we spin if driven
    };

    struct Wheel {
        WheelParams params;
        Render::Model model;
        glm::mat4 modelFix = glm::mat4(1.0f);
        // dynamic state (visual)
        float steerYaw = 0.0f; // radians, around +Y
        float spinAngle = 0.0f; // radians, around +X

        void setModel(Render::Model&& m) { model = std::move(m); }
        void setModelFix(const glm::mat4& m) { modelFix = m; }
        void update(float steerInput, float wheelOmega, float dt);
        void draw(const glm::mat4& vehicleXform, const glm::mat4& vp, GLuint shader,
            const glm::vec3& color = { 0.9f,0.9f,0.9f }) const;
    };

} // namespace Vehicle

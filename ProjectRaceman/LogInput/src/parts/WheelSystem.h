#pragma once
#include "../core/GlmCompat.h"
#include <glm/glm.hpp>
#include <glad/glad.h>
#include <string>
#include "../render/Model.h"

namespace Vehicle {

    // ---- a single visual wheel
    struct WheelVis {
        float steerDeg = 0.0f;      // yaw about Y
        float spinRad = 0.0f;      // rotation about X
        glm::vec3 localPos{ 0.0f };   // position relative to chassis
        const Render::Model* model = nullptr;
    };

    struct FrontAxle {
        WheelVis left, right;

        // update visual state (no physics yet)
        void update(float steerInput, float omegaL, float omegaR, float dt);

        // draw both wheels (expects shader bound; sets uModel)
        void draw(const glm::mat4& vehicleXform, GLuint program) const;
    };

    class WheelSystem {
    public:
        // loads geometry (OBJ). shaft optional for later
        bool loadModels(const std::string& wheelObjPath,
            const std::string& shaftObjPath = {});

        // set the front axle geometry with halfTrack (m), hub Y and Z offsets (m)
        void setFrontGeometry(float halfTrack, float hubY, float hubZ);

        // update visuals; carSpeed used for spin; wheelRadius controls spin speed
        void update(float steerInput, float carSpeed_mps, float wheelRadius_m, float dt);

        // render all wheels
        void draw(const glm::mat4& vehicleXform, GLuint program) const;

        // (optional) convenience accessors if you want to tweak local positions directly
        WheelVis& leftFront() { return front_.left; }
        WheelVis& rightFront() { return front_.right; }

    private:
        Render::Model wheelModel_;
        Render::Model shaftModel_; // reserved for later
        FrontAxle front_;
    };

} // namespace Vehicle

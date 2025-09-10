#pragma once
#include "Wheel.h"
#include "DriveShaft.h"

namespace Vehicle {

    struct AxleParams {
        float halfTrack = 0.75f;   // half the lateral distance (m)
        float hubY = -0.20f;  // vertical hub offset in vehicle frame
        float hubZ = 1.10f;  // longitudinal position (front axle +Z)
    };

    struct Axle {
        AxleParams params;
        Wheel left, right;
        DriveShaft leftShaft, rightShaft;

        // feeds both wheels with steering input and wheel speed (rad/s) (visual)
        void update(float steerInput, float wheelOmegaL, float wheelOmegaR, float dt);

        // vehicleXform = model matrix of the chassis; shader = your basic shader
        void draw(const glm::mat4& vehicleXform, GLuint shader);
    };

} // namespace Vehicle

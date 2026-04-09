#include "CameraOrbit.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace raceman::scripts {

void CameraOrbit::OnStart(raceman::ObjectScriptContext& context) {
    context.Log("CameraOrbit started");
    // Initialize angles from current rotation if available
    const glm::vec3 rot = context.GetRotationEuler();
    yawDegrees_ = rot.y;
    pitchDegrees_ = rot.x;
    anglesInitialized_ = true;
}

void CameraOrbit::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    if (!context.HasCamera()) {
        if (!warnedMissingCamera_) {
            context.Warning("CameraOrbit requires a Camera component on this object.");
            warnedMissingCamera_ = true;
        }
        return;
    }

    auto camera = context.Camera();

    // FOV adjustments
    float fov = camera.GetFieldOfView();
    if (context.IsKeyDown(GLFW_KEY_Q)) {
        fov -= fovChangeSpeed_ * deltaTime;
    }
    if (context.IsKeyDown(GLFW_KEY_E)) {
        fov += fovChangeSpeed_ * deltaTime;
    }
    fov = glm::clamp(fov, 25.0f, 110.0f);
    camera.SetFieldOfView(fov);

    // Orbit controls
    float yawDelta = 0.0f;
    float pitchDelta = 0.0f;
    if (context.IsKeyDown(GLFW_KEY_A)) yawDelta -= orbitSpeedDeg_ * deltaTime;
    if (context.IsKeyDown(GLFW_KEY_D)) yawDelta += orbitSpeedDeg_ * deltaTime;
    if (context.IsKeyDown(GLFW_KEY_W)) pitchDelta += pitchSpeedDeg_ * deltaTime;
    if (context.IsKeyDown(GLFW_KEY_S)) pitchDelta -= pitchSpeedDeg_ * deltaTime;

    yawDegrees_ += yawDelta;
    pitchDegrees_ = glm::clamp(pitchDegrees_ + pitchDelta, -80.0f, 80.0f);

    // Simple radius adjust
    if (context.IsKeyDown(GLFW_KEY_Z)) radius_ = glm::max(1.5f, radius_ - 2.0f * deltaTime);
    if (context.IsKeyDown(GLFW_KEY_X)) radius_ = glm::min(30.0f, radius_ + 2.0f * deltaTime);

    const glm::vec3 target{0.0f, 1.0f, 0.0f};
    const float yawRad = glm::radians(yawDegrees_);
    const float pitchRad = glm::radians(pitchDegrees_);

    const float cosPitch = cosf(pitchRad);
    const glm::vec3 offset{
        radius_ * cosPitch * sinf(yawRad),
        radius_ * sinf(pitchRad),
        radius_ * cosPitch * cosf(yawRad)};

    context.SetPosition(target + offset);
    context.SetRotationEuler({pitchDegrees_, yawDegrees_, 0.0f});
}

} // namespace raceman::scripts

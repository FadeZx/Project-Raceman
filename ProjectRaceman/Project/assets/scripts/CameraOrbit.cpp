#include "CameraOrbit.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace raceman::scripts {

namespace {

glm::vec3 ComputeLookRotationEuler(const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 forward = glm::normalize(to - from);
    const float yaw = glm::degrees(std::atan2(forward.x, forward.z));
    const float pitch = glm::degrees(std::asin(glm::clamp(-forward.y, -1.0f, 1.0f)));
    return {pitch, yaw, 0.0f};
}

} // namespace

void CameraOrbit::OnStart(raceman::ObjectScriptContext& context) {
    context.Log("CameraOrbit started. RMB drag or controller right stick orbits around the target position.");
}

void CameraOrbit::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    if (!context.HasCamera()) {
        if (!warnedMissingCamera_) {
            context.Warning("CameraOrbit requires a Camera component on this object.");
            warnedMissingCamera_ = true;
        }
        return;
    }

    const glm::vec3 targetPosition = context.GetVec3Field("targetPosition", glm::vec3(0.0f, 1.0f, 0.0f));
    const float minPitch = context.GetFloatField("minPitch", -80.0f);
    const float maxPitch = context.GetFloatField("maxPitch", 80.0f);
    const bool invertY = context.GetBoolField("invertY", false);
    const bool orbitOnRightMouse = context.GetBoolField("orbitOnRightMouse", true);

    float radius = glm::max(0.5f, context.GetFloatField("radius", 6.0f));

    if (!initializedOrbitState_) {
        const glm::vec3 currentPosition = context.GetPosition();
        glm::vec3 offset = currentPosition - targetPosition;
        if (glm::length(offset) <= 0.001f) {
            offset = {0.0f, 2.0f, radius};
        }
        radius = glm::length(offset);
        const float planarLength = glm::max(0.0001f, std::sqrt(offset.x * offset.x + offset.z * offset.z));
        yawDegrees_ = glm::degrees(std::atan2(offset.x, offset.z));
        pitchDegrees_ = glm::degrees(std::atan2(offset.y, planarLength));
        initializedOrbitState_ = true;
    }

    float yawDelta = 0.0f;
    float pitchDelta = 0.0f;

    if (!orbitOnRightMouse || context.IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT)) {
        const glm::vec2 mouseDelta = context.GetMouseDelta();
        const float mouseSensitivity = context.GetFloatField("mouseSensitivity", 0.18f);
        yawDelta += mouseDelta.x * mouseSensitivity;
        pitchDelta -= mouseDelta.y * mouseSensitivity;
    }

    const float stickSensitivity = context.GetFloatField("stickSensitivity", 160.0f);
    yawDelta += context.GetAxis("lookX") * stickSensitivity * deltaTime;
    pitchDelta += context.GetAxis("lookY") * stickSensitivity * deltaTime;

    const float pitchDirection = invertY ? -1.0f : 1.0f;
    yawDegrees_ += yawDelta;
    pitchDegrees_ = glm::clamp(pitchDegrees_ + pitchDelta * pitchDirection, minPitch, maxPitch);

    if (context.IsMouseButtonDown(GLFW_MOUSE_BUTTON_MIDDLE)) {
        const float zoomSpeed = context.GetFloatField("zoomSpeed", 5.0f);
        radius = glm::max(0.5f, radius - context.GetMouseDelta().y * zoomSpeed * 0.02f);
    }

    context.SetFloatField("radius", radius);

    const float yawRad = glm::radians(yawDegrees_);
    const float pitchRad = glm::radians(pitchDegrees_);
    const float cosPitch = std::cos(pitchRad);
    const glm::vec3 offset{
        radius * cosPitch * std::sin(yawRad),
        radius * std::sin(pitchRad),
        radius * cosPitch * std::cos(yawRad)
    };

    const glm::vec3 cameraPosition = targetPosition + offset;
    context.SetPosition(cameraPosition);
    context.SetRotationEuler(ComputeLookRotationEuler(cameraPosition, targetPosition));
}

} // namespace raceman::scripts

#include "CameraOrbitGamepad.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace raceman::scripts {

namespace {

constexpr float kStickDeadzone = 0.15f;

void ComputeOrbitAngles(const glm::vec3& offset, float& yawDegrees, float& pitchDegrees, float& radius) {
    radius = glm::max(glm::length(offset), 0.001f);
    const float planarLength = glm::max(0.0001f, std::sqrt(offset.x * offset.x + offset.z * offset.z));
    yawDegrees = glm::degrees(std::atan2(offset.x, offset.z));
    pitchDegrees = glm::degrees(std::atan2(offset.y, planarLength));
}

} // namespace

void CameraOrbitGamepad::OnStart(raceman::ObjectScriptContext& context) {
    const glm::vec3 orbitCenter = context.GetVec3Field("targetPosition", glm::vec3(0.0f));
    startPosition_ = context.GetPosition();
    startOffset_ = startPosition_ - orbitCenter;
    if (glm::length(startOffset_) <= 0.001f) {
        startOffset_ = glm::vec3(0.0f, 2.0f, 6.0f);
        startPosition_ = orbitCenter + startOffset_;
        context.SetPosition(startPosition_);
    }
    ComputeOrbitAngles(startOffset_, yawDegrees_, pitchDegrees_, radius_);
    initialized_ = true;
    context.Log("CameraOrbitGamepad started. Use the right stick (or hold LMB and drag) to orbit.");
}

void CameraOrbitGamepad::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    if (!context.HasCamera()) {
        if (!warnedMissingCamera_) {
            context.Warning("CameraOrbitGamepad requires a Camera component on this object.");
            warnedMissingCamera_ = true;
        }
        return;
    }

    const glm::vec3 orbitCenter = context.GetVec3Field("targetPosition", glm::vec3(0.0f));
    const float minPitch = context.GetFloatField("minPitch", -20.0f);
    const float maxPitch = context.GetFloatField("maxPitch", 75.0f);
    const bool invertY = context.GetBoolField("invertY", false);
    const bool allowMouse = context.GetBoolField("allowMouse", true);

    if (!initialized_) {
        startPosition_ = context.GetPosition();
        startOffset_ = startPosition_ - orbitCenter;
        ComputeOrbitAngles(startOffset_, yawDegrees_, pitchDegrees_, radius_);
        initialized_ = true;
    }

    bool orbited = false;

    // Right stick: "lookX"/"lookY" are already bound to GLFW_GAMEPAD_AXIS_RIGHT_X/Y
    // on the default character input profile, deadzoned and Y-inverted upstream.
    const float stickX = context.GetAxis("lookX");
    const float stickY = context.GetAxis("lookY");
    if (std::abs(stickX) > kStickDeadzone || std::abs(stickY) > kStickDeadzone) {
        const float stickYawSpeed = context.GetFloatField("stickYawSpeed", 140.0f);
        const float stickPitchSpeed = context.GetFloatField("stickPitchSpeed", 110.0f);
        const float pitchDirection = invertY ? -1.0f : 1.0f;

        yawDegrees_ += stickX * stickYawSpeed * deltaTime;
        pitchDegrees_ = glm::clamp(
            pitchDegrees_ + stickY * stickPitchSpeed * pitchDirection * deltaTime,
            minPitch,
            maxPitch);
        orbited = true;
    }

    if (!orbited && allowMouse && context.IsMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT)) {
        const glm::vec2 mouseDelta = context.GetMouseDelta();
        const float mouseSensitivity = context.GetFloatField("mouseSensitivity", 0.18f);
        yawDegrees_ += mouseDelta.x * mouseSensitivity;
        const float pitchDirection = invertY ? 1.0f : -1.0f;
        pitchDegrees_ = glm::clamp(
            pitchDegrees_ + mouseDelta.y * mouseSensitivity * pitchDirection,
            minPitch,
            maxPitch);
        orbited = true;
    }

    if (!orbited) {
        return;
    }

    const float yawRad = glm::radians(yawDegrees_);
    const float pitchRad = glm::radians(pitchDegrees_);
    const float cosPitch = std::cos(pitchRad);
    const glm::vec3 orbitOffset{
        radius_ * cosPitch * std::sin(yawRad),
        radius_ * std::sin(pitchRad),
        radius_ * cosPitch * std::cos(yawRad)
    };
    context.SetPosition(orbitCenter + orbitOffset);
}

} // namespace raceman::scripts

#include "CameraOrbit.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace raceman::scripts {

namespace {

void ComputeOrbitAngles(const glm::vec3& offset, float& yawDegrees, float& pitchDegrees, float& radius) {
    radius = glm::max(glm::length(offset), 0.001f);
    const float planarLength = glm::max(0.0001f, std::sqrt(offset.x * offset.x + offset.z * offset.z));
    yawDegrees = glm::degrees(std::atan2(offset.x, offset.z));
    pitchDegrees = glm::degrees(std::atan2(offset.y, planarLength));
}

} // namespace

void CameraOrbit::OnStart(raceman::ObjectScriptContext& context) {
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
    context.Log("CameraOrbit started. Hold LMB and drag to orbit; release to return.");
}

void CameraOrbit::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    if (!context.HasCamera()) {
        if (!warnedMissingCamera_) {
            context.Warning("CameraOrbit requires a Camera component on this object.");
            warnedMissingCamera_ = true;
        }
        return;
    }

    const glm::vec3 orbitCenter = context.GetVec3Field("targetPosition", glm::vec3(0.0f));
    const float minPitch = context.GetFloatField("minPitch", -20.0f);
    const float maxPitch = context.GetFloatField("maxPitch", 75.0f);
    const bool invertY = context.GetBoolField("invertY", false);
    const bool returnOnRelease = context.GetBoolField("returnOnRelease", true);

    if (!initialized_) {
        startPosition_ = context.GetPosition();
        startOffset_ = startPosition_ - orbitCenter;
        ComputeOrbitAngles(startOffset_, yawDegrees_, pitchDegrees_, radius_);
        initialized_ = true;
    }

    if (context.IsMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT)) {
        const glm::vec2 mouseDelta = context.GetMouseDelta();
        const float mouseSensitivity = context.GetFloatField("mouseSensitivity", 0.18f);
        yawDegrees_ += mouseDelta.x * mouseSensitivity;
        const float pitchDirection = invertY ? 1.0f : -1.0f;
        pitchDegrees_ = glm::clamp(
            pitchDegrees_ + mouseDelta.y * mouseSensitivity * pitchDirection,
            minPitch,
            maxPitch);

        const float yawRad = glm::radians(yawDegrees_);
        const float pitchRad = glm::radians(pitchDegrees_);
        const float cosPitch = std::cos(pitchRad);
        const glm::vec3 orbitOffset{
            radius_ * cosPitch * std::sin(yawRad),
            radius_ * std::sin(pitchRad),
            radius_ * cosPitch * std::cos(yawRad)
        };
        context.SetPosition(orbitCenter + orbitOffset);
        return;
    }

    if (!returnOnRelease) {
        return;
    }

    const glm::vec3 returnPosition = orbitCenter + startOffset_;
    const float returnSpeed = glm::max(0.0f, context.GetFloatField("returnSpeed", 5.0f));
    const float returnT = 1.0f - std::exp(-returnSpeed * glm::max(deltaTime, 0.0f));
    glm::vec3 cameraPosition = glm::mix(context.GetPosition(), returnPosition, returnT);
    const glm::vec3 returnError = cameraPosition - returnPosition;
    if (glm::dot(returnError, returnError) < 0.000001f) {
        cameraPosition = returnPosition;
    }
    context.SetPosition(cameraPosition);

    // Keep the orbit angles synchronized with the returning camera so a new
    // drag continues smoothly instead of snapping to the previous drag pose.
    ComputeOrbitAngles(cameraPosition - orbitCenter, yawDegrees_, pitchDegrees_, radius_);
}

} // namespace raceman::scripts

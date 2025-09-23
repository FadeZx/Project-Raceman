#include "OrbitCam.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace Render;

void OrbitCamera::setPerspective(float fovy_deg, float aspect, float znear, float zfar) {
    proj_ = glm::perspective(glm::radians(fovy_deg), aspect, znear, zfar);
}

void OrbitCamera::setLimits(float minDist, float maxDist, float minPitchDeg, float maxPitchDeg) {
    minDist_ = std::max(0.01f, minDist);
    maxDist_ = std::max(minDist_, maxDist);
    minPitch_ = glm::radians(minPitchDeg);
    maxPitch_ = glm::radians(maxPitchDeg);
    dist_ = glm::clamp(dist_, minDist_, maxDist_);
    pitch_ = clampPitch(pitch_);
}

glm::vec3 OrbitCamera::eye() const {
    // spherical to Cartesian
    const float cp = std::cos(pitch_), sp = std::sin(pitch_);
    const float cy = std::cos(yaw_), sy = std::sin(yaw_);
    glm::vec3 dir{ cy * cp, sp, sy * cp }; // forward vector
    return target_ + (-dir) * dist_;
}

void OrbitCamera::updateView() {
    view_ = glm::lookAt(eye(), target_, glm::vec3(0, 1, 0));
}

void OrbitCamera::orbit(float dx, float dy, float sensitivity) {
    yaw_ += glm::radians(dx * sensitivity);
    pitch_ = clampPitch(pitch_ + glm::radians(dy * sensitivity));
    updateView();
}

void OrbitCamera::pan(float dx, float dy, float sensitivity) {
    float panScale = dist_ * sensitivity;

    // build basis
    const float cp = std::cos(pitch_), sp = std::sin(pitch_);
    const float cy = std::cos(yaw_), sy = std::sin(yaw_);
    glm::vec3 forward{ -cy * cp, -sp, -sy * cp };
    glm::vec3 right = glm::normalize(glm::cross(forward, { 0,1,0 }));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    target_ += (-dx * panScale) * right + (dy * panScale) * up;
    updateView();
}

void OrbitCamera::dolly(float scrollY, float sensitivity) {
    float factor = std::pow(0.9f, scrollY * sensitivity);
    dist_ = glm::clamp(dist_ * factor, minDist_, maxDist_);
    updateView();
}

float OrbitCamera::clampPitch(float p) const {
    return std::clamp(p, minPitch_, maxPitch_);
}

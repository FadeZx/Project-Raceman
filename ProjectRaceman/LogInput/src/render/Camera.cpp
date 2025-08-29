#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace Render;

void Camera::setPerspective(float fovy_deg, float aspect, float znear, float zfar) {
    m_proj = glm::perspective(glm::radians(fovy_deg), aspect, znear, zfar);
}

void Camera::setView(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up) {
    m_view = glm::lookAt(eye, target, up);
}

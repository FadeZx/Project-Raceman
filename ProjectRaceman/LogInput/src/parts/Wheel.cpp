#include "Wheel.h"
#include "../core/GlmCompat.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>

namespace Vehicle {

    void Wheel::update(float steerInput, float wheelOmega, float dt) {
        if (params.steerable) {
            // map input [-1..1] to max ±30° for now
            const float maxSteer = glm::radians(30.0f);
            steerYaw = maxSteer * glm::clamp(steerInput, -1.0f, 1.0f);
        }
        // spin around X by omega
        spinAngle += wheelOmega * dt; // rad/s * s = rad
    }

    void Wheel::draw(const glm::mat4& vehicleXform, const glm::mat4& vp, GLuint shader, const glm::vec3& color) const {
        // local wheel transform: translate -> steer yaw (Y) -> spin around X -> (model.local)
        glm::mat4 M = vehicleXform;
        M = glm::translate(M, params.localOffset);
        M = glm::rotate(M, steerYaw, { 0,1,0 });
        M = glm::rotate(M, spinAngle, { 1,0,0 });
        M = M * modelFix;

        // shader expects uModel, uViewProj, uColor (your basic.vs/.fs already have uModel/uProj/uView)
        glUseProgram(shader);
        // If your shader has separate uView & uProj, set them outside once per frame (as you do).
        GLint locM = glGetUniformLocation(shader, "uModel");
        GLint locC = glGetUniformLocation(shader, "uColor");
        if (locM >= 0) glUniformMatrix4fv(locM, 1, GL_FALSE, &M[0][0]);
        if (locC >= 0) glUniform3fv(locC, 1, &color[0]);

        model.draw();
    }

} // namespace Vehicle

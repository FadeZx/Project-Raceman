#include "WheelSystem.h"
#include "../core/GlmCompat.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdio>

#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif

namespace {

    // tiny uniform helper
    inline void SetMat4(GLuint prog, const char* name, const glm::mat4& M) {
        GLint loc = glGetUniformLocation(prog, name);
        if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, &M[0][0]);
    }

} // anon

namespace Vehicle {

    // -------- FrontAxle --------
    void FrontAxle::update(float steerInput, float omegaL, float omegaR, float dt) {
        const float maxSteer = 28.0f;
        left.steerDeg = maxSteer * steerInput;
        right.steerDeg = maxSteer * steerInput;

        left.spinRad += omegaL * dt;
        right.spinRad += omegaR * dt;
    }

    void FrontAxle::draw(const glm::mat4& vehicleXform, GLuint program) const {
        auto drawOne = [&](const WheelVis& w, bool mirrorX) {
            if (!w.model || !w.model->valid()) return;

            glm::mat4 M = vehicleXform;
            M = glm::translate(M, w.localPos);
            M = glm::rotate(M, glm::radians(w.steerDeg), glm::vec3(0, 1, 0));
            // spin around local X (typical wheel)
            M = glm::rotate(M, w.spinRad, glm::vec3(1, 0, 0));

            // if your right wheel model needs mirroring (only if the OBJ is asymmetrical)
            if (mirrorX) M = glm::scale(M, glm::vec3(-1.f, 1.f, 1.f));

            SetMat4(program, "uModel", M);
            w.model->draw();
            };

        drawOne(left, false);
        drawOne(right, true);
    }

    // -------- WheelSystem --------
    bool WheelSystem::loadModels(const std::string& wheelObjPath,
        const std::string& shaftObjPath)
    {
        std::string err;
        if (!wheelModel_.loadOBJ(wheelObjPath, &err)) {
            std::fprintf(stderr, "Wheel OBJ load failed: %s\n", err.c_str());
            return false;
        }
        if (!shaftObjPath.empty()) {
            err.clear();
            if (!shaftModel_.loadOBJ(shaftObjPath, &err)) {
                std::fprintf(stderr, "Shaft OBJ load failed (ignored): %s\n", err.c_str());
            }
        }
        front_.left.model = &wheelModel_;
        front_.right.model = &wheelModel_;
        return true;
    }

    void WheelSystem::setFrontGeometry(float halfTrack, float hubY, float hubZ) {
        front_.left.localPos = glm::vec3(+halfTrack, hubY, hubZ);
        front_.right.localPos = glm::vec3(-halfTrack, hubY, hubZ);
    }

    void WheelSystem::update(float steerInput, float carSpeed_mps, float wheelRadius_m, float dt) {
        const float omega = (wheelRadius_m > 1e-4f) ? (carSpeed_mps / wheelRadius_m) : 0.0f;
        front_.update(steerInput, omega, omega, dt);
    }

    void WheelSystem::draw(const glm::mat4& vehicleXform, GLuint program) const {
        front_.draw(vehicleXform, program);
    }

} // namespace Vehicle

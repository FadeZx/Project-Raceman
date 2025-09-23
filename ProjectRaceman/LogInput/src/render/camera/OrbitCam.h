#pragma once
#include "../../core/GlmCompat.h"
#include <glm/glm.hpp>
#include <algorithm>

namespace Render {

    class OrbitCamera {
    public:
        // setup
        void setPerspective(float fovy_deg, float aspect, float znear, float zfar);
        void setTarget(const glm::vec3& t) { target_ = t; }
        void setDistance(float d) { dist_ = glm::clamp(d, minDist_, maxDist_); }
        void setYawPitch(float yaw, float pitch) { yaw_ = yaw; pitch_ = clampPitch(pitch); }
        void setLimits(float minDist, float maxDist, float minPitchDeg = -89.0f, float maxPitchDeg = 89.0f);

        // matrices
        const glm::mat4& proj() const { return proj_; }
        const glm::mat4& view() const { return view_; }
        glm::vec3 eye() const;

        // interactions
        void orbit(float dx, float dy, float sensitivity = 0.3f);
        void pan(float dx, float dy, float sensitivity = 0.002f);
        void dolly(float scrollY, float sensitivity = 1.0f);

        void updateView();

        // ---- getters (for debug/UI) ----
        const glm::vec3& target() const { return target_; }
        float distance() const { return dist_; }
        float yaw() const { return yaw_; }
        float pitch() const { return pitch_; }

    private:
        float clampPitch(float p) const;

        // state
        glm::vec3 target_{ 0.0f };
        float dist_ = 8.0f;
        float yaw_ = glm::radians(35.0f);
        float pitch_ = glm::radians(-20.0f);

        float minDist_ = 1.0f, maxDist_ = 50.0f;
        float minPitch_ = glm::radians(-89.0f), maxPitch_ = glm::radians(89.0f);

        glm::mat4 proj_{ 1.0f };
        glm::mat4 view_{ 1.0f };
    };

} 

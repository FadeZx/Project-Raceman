#pragma once
#include "../core/GlmCompat.h"
#include <glm/glm.hpp>
#include "Camera.h"

namespace Render {

    struct ChaseCamParams {
        bool  enabled = true;      // toggle on/off
        float distance = 6.5f;      // boom length (m)
        float height = 1.2f;      // camera height above car (m)
        float pitch_deg = 12.0f;     // tilt down towards car (deg)
        float lookahead = 2.0f;      // look ahead along car direction (m)
        float lateral_off = 0.0f;      // side offset (m) (e.g., for TV-style)
        float base_fov_deg = 62.0f;     // standing FOV
        float fov_speed_gain = 0.04f;     // extra FOV per m/s
        float min_fov_deg = 50.0f;
        float max_fov_deg = 85.0f;

        // Follow smoothing (critically damped spring)
        float pos_stiffness = 10.0f;     // ω (higher = snappier)
        float pos_damping = 1.0f;      // ζ (1 = critical)
        float yaw_lead_deg = 6.0f;      // yaw lead in direction of motion (deg)
        float bank_scale = 0.015f;    // roll from lateral accel/vel
        float bank_max_deg = 6.0f;      // clamp
    };

    class ChaseCamera {
    public:
        void reset(const glm::vec3& car_pos, const glm::vec3& car_fwd);
        // Call every frame
        void update(const glm::vec3& car_pos,
            const glm::vec3& car_fwd,
            const glm::vec3& car_vel,    // world m/s
            float dt,
            float aspect,
            Camera& cam,                  // will set view/proj here
            const ChaseCamParams& C);

        // For debug/telemetry if needed
        glm::vec3 eye()   const { return m_eye; }
        glm::vec3 target()const { return m_tgt; }
        float     fovDeg()const { return m_curr_fov_deg; }

    private:
        // internal state
        glm::vec3 m_eye{ 0.0f };      // smoothed camera position
        glm::vec3 m_eyeVel{ 0.0f };   // smoothing velocity
        glm::vec3 m_tgt{ 0.0f };      // current target point
        glm::vec3 m_prevVel{ 0.0f };  // for banking
        float     m_curr_fov_deg = 60.0f;
    };

} // namespace Render

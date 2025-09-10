#include "ChaseCam.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

using namespace Render;

static inline glm::vec3 safeNorm(const glm::vec3& v, const glm::vec3& fallback = { 0,0,-1 }) {
    float m2 = glm::dot(v, v);
    return (m2 > 1e-6f) ? (v * glm::inversesqrt(m2)) : fallback;
}

// Critically-damped spring (Unity-like SmoothDamp) for vec3
static glm::vec3 smoothDampVec3(glm::vec3 current, glm::vec3 target,
    glm::vec3& currentVelocity,
    float smoothTime, float dt, float maxSpeed = FLT_MAX)
{
    // smoothTime ~ 1/ω (so for ω=10, smoothTime=0.1)
    smoothTime = std::max(1e-4f, smoothTime);
    float omega = 2.0f / smoothTime;

    float x = omega * dt;
    float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

    glm::vec3 change = current - target;
    float maxChange = maxSpeed * smoothTime;
    float changeLen = glm::length(change);
    if (changeLen > maxChange) change *= (maxChange / (changeLen + 1e-6f));

    glm::vec3 temp = (currentVelocity + omega * change) * dt;
    glm::vec3 next = target + (change + temp) * exp;
    currentVelocity = (currentVelocity - omega * temp) * exp;
    return next;
}

// Convert stiffness (ω) & damping ratio (ζ) to smoothTime for SmoothDamp
static inline float stiffnessToSmoothTime(float stiffness_w, float damping_z) {
    // Critical damping ζ=1 maps roughly to smoothTime ≈ 1/ω
    // For ζ != 1, we still return ~1/ω; you can refine if needed.
    stiffness_w = std::max(1e-3f, stiffness_w);
    (void)damping_z; // kept if you want a richer mapping
    return 1.0f / stiffness_w;
}

void ChaseCamera::reset(const glm::vec3& car_pos, const glm::vec3& car_fwd) {
    glm::vec3 up{ 0,1,0 };
    glm::vec3 f = safeNorm(car_fwd, { 0,0,-1 });
    glm::vec3 r = safeNorm(glm::cross(f, up), { 1,0,0 });
    glm::vec3 u = glm::cross(r, f);

    m_tgt = car_pos + u * 1.2f;
    m_eye = m_tgt - f * 6.5f + u * 0.5f;
    m_eyeVel = glm::vec3(0.0f);
    m_prevVel = glm::vec3(0.0f);
    m_curr_fov_deg = 60.0f;
}

void ChaseCamera::update(const glm::vec3& car_pos,
    const glm::vec3& car_fwd,
    const glm::vec3& car_vel,
    float dt,
    float aspect,
    Camera& cam,
    const ChaseCamParams& C)
{
    // Keep a safe up basis
    const glm::vec3 worldUp{ 0,1,0 };
    const glm::vec3 fwd = safeNorm(car_fwd, { 0,0,-1 });
    const glm::vec3 right = safeNorm(glm::cross(fwd, worldUp), { 1,0,0 });
    const glm::vec3 up = glm::cross(right, fwd);

    const float speed = glm::length(car_vel);

    // Lead/aim direction: yaw lead in direction of velocity (helps anticipation)
    glm::vec3 dirForYaw = fwd;
    if (speed > 0.5f) {
        glm::vec3 vdir = safeNorm(car_vel);
        // small slerp towards velocity direction
        float yawLeadT = std::min(1.0f, C.yaw_lead_deg / 45.0f); // normalize ~deg range
       // dirForYaw = glm::normalize(glm::lerp(fwd, vdir, yawLeadT));
dirForYaw = glm::normalize(glm::mix(fwd, vdir, yawLeadT));


    }

    // Apply pitch (tilt down towards car)
    float pitchRad = glm::radians(std::clamp(C.pitch_deg, -30.0f, 45.0f));
    // rotate dirForYaw around 'right' by -pitch (look downward)
    glm::vec3 aimDir = glm::normalize(
        dirForYaw * std::cos(pitchRad) - up * std::sin(pitchRad)
    );

    // Target look point: ahead of car, a bit up
    glm::vec3 tgt = car_pos + up * C.height + fwd * C.lookahead + right * C.lateral_off;

    // Desired eye: behind target along aimDir by distance
    glm::vec3 desiredEye = tgt - aimDir * C.distance;

    // Subtle banking by lateral motion/accel (cheap & stable)
    float lateral = glm::dot(car_vel, right); // m/s sideways
    float bankDeg = std::clamp(lateral * C.bank_scale * 57.29578f, -C.bank_max_deg, C.bank_max_deg);
    float bankRad = glm::radians(bankDeg);

    // Smooth camera position (critically damped)
    const float smoothTime = stiffnessToSmoothTime(C.pos_stiffness, C.pos_damping);
    m_eye = smoothDampVec3(m_eye, desiredEye, m_eyeVel, smoothTime, dt);

    // Smooth FOV based on speed
    float desiredFov = std::clamp(C.base_fov_deg + C.fov_speed_gain * speed,
        C.min_fov_deg, C.max_fov_deg);
    m_curr_fov_deg = glm::mix(m_curr_fov_deg, desiredFov, std::clamp(dt * 5.0f, 0.0f, 1.0f));

    // Build a banked up vector (rotate worldUp around forward facing direction from eye->tgt)
    glm::vec3 viewDir = safeNorm(tgt - m_eye, { 0,0,-1 });
    // rotate 'worldUp' around 'viewDir' by bankRad
    glm::mat4 rollM = glm::rotate(glm::mat4(1.0f), bankRad, viewDir);
    glm::vec3 camUp = glm::normalize(glm::vec3(rollM * glm::vec4(worldUp, 0)));

    // Output
    m_tgt = tgt;
    cam.setPerspective(m_curr_fov_deg, aspect, 0.05f, 2000.0f);
    cam.setView(m_eye, tgt, camUp);

    m_prevVel = car_vel;
}

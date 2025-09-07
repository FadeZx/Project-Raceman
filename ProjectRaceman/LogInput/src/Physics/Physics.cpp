#include "Physics.h"
#include "Powertrain.h"
#include <algorithm>
#include <cmath>

namespace Physics {

    Params P;
    VehicleState gCar;

    // Helpers (define ONCE)
    static inline float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
    static inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
    static inline float circle_allow(float muFz, float Fy) {
        float s = muFz * muFz - Fy * Fy;
        return (s > 0.0f) ? std::sqrt(s) : 0.0f;
    }

    void Init() {
        P = Params{};
        gCar = VehicleState{};
        gCar.pos.y = 0.5f;
    }

    void Update(double dt, const Controls& c) {
        const float m = P.m, Iz = P.Iz, lf = P.lf, lr = P.lr, Cf = P.Cf, Cr = P.Cr;
        const float mu = P.mu, g = P.g;

        // Speeds
        float speed = std::sqrt(gCar.vx * gCar.vx + gCar.vy * gCar.vy);
        const float v_eps = 0.10f;
        const float vx_guard = (std::fabs(gCar.vx) < v_eps) ? (gCar.vx >= 0.f ? v_eps : -v_eps) : gCar.vx;

        // Static axle loads
        const float L = lf + lr;
        const float Fzf_static = m * g * lr / L;
        const float Fzr_static = m * g * lf / L;

        // Controls (clamped)  ✅ define BEFORE using them
        const float throttle = clamp01(c.throttle);
        const float brake = clamp01(c.brake);

        // Steering limit vs speed
        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        float t = clampf(speed / 30.0f, 0.0f, 1.0f);
        float delta_max = lerp(0.6f, 0.2f, t);
        const float delta = clampf(c.steer, -1.f, 1.f) * delta_max;

        // Slip angles
        float alpha_f = std::atan2(gCar.vy + lf * gCar.r, vx_guard) - delta;
        float alpha_r = std::atan2(gCar.vy - lr * gCar.r, vx_guard);

        // Near-standstill policy
        const bool near_stopped = speed < 0.30f;
        if (near_stopped) {
            gCar.vy *= 0.2f;
            gCar.r *= 0.2f;
            if (throttle > 0.05f) { alpha_f = 0.0f; alpha_r = 0.0f; }
        }

        // Lateral forces (first pass, static loads)
        float Fyf = clampf(-Cf * alpha_f, -mu * Fzf_static, mu * Fzf_static);
        float Fyr = clampf(-Cr * alpha_r, -mu * Fzr_static, mu * Fzr_static);

        // ✅ Forward speed only for powertrain (steering shouldn't change RPM)
        float fwd_speed = std::max(0.0f, gCar.vx);
        float Fx_drive = Powertrain::ComputeWheelForce(fwd_speed, throttle, (float)dt);

        // Braking & net longitudinal request
        float Fx_brake = brake * P.Fb_max;
        float Fx_cmd = Fx_drive - Fx_brake;


        // Rolling resistance opposes motion
        float frr_sign = (gCar.vx >= 0.f ? 1.f : -1.f);
        float Frr = P.Crr * m * g * std::tanh(std::fabs(gCar.vx) / 0.5f) * frr_sign;
        Fx_cmd -= Frr;

        // Friction circle allowance (first pass, static loads)
        float Fx_allow = circle_allow(mu * Fzf_static, Fyf) + circle_allow(mu * Fzr_static, Fyr);
        if (near_stopped && throttle > 0.05f) Fx_allow = mu * (Fzf_static + Fzr_static);
        float Fx = clampf(Fx_cmd, -Fx_allow, Fx_allow);

        // Aero drag
        float Fdrag = 0.5f * P.rho * P.CdA * vx_guard * std::fabs(vx_guard);

        // --- Dynamic load transfer (recompute with ax0) ---
        float ax0 = (Fx - Fdrag) / m + gCar.vy * gCar.r;

        float h = 0.45f;  // CG height
        float dF = (m * h / L) * ax0; // +front when braking (ax0<0)

        float Fzf_dyn = Fzf_static + dF;
        float Fzr_dyn = Fzr_static - dF;

        // Clamp to non-negative normal loads (numerical safety)
        Fzf_dyn = std::max(0.0f, Fzf_dyn);
        Fzr_dyn = std::max(0.0f, Fzr_dyn);

        // Recompute lateral with DYNAMIC loads
        Fyf = clampf(-Cf * alpha_f, -mu * Fzf_dyn, mu * Fzf_dyn);
        Fyr = clampf(-Cr * alpha_r, -mu * Fzr_dyn, mu * Fzr_dyn);

        // Recompute Fx allowance with DYNAMIC loads
        Fx_allow = circle_allow(mu * Fzf_dyn, Fyf) + circle_allow(mu * Fzr_dyn, Fyr);
        if (near_stopped && throttle > 0.05f) Fx_allow = mu * (Fzf_dyn + Fzr_dyn);
        Fx = clampf(Fx_cmd, -Fx_allow, Fx_allow);

        // Final accelerations (no lateral->forward boost term)
        float ax = (Fx - Fdrag) / m + gCar.vy * gCar.r;
        float ay = (Fyr + Fyf * std::cos(delta)) / m - gCar.vx * gCar.r;
        float rdot = (lf * Fyf * std::cos(delta) - lr * Fyr) / Iz;

        // Integrate
        gCar.vx += ax * (float)dt;
        gCar.vy += ay * (float)dt;
        gCar.r += rdot * (float)dt;
        gCar.yaw += gCar.r * (float)dt;

        // Snap tiny residuals when not pressing throttle
        if (std::fabs(gCar.vx) < 0.02f && throttle < 0.01f) gCar.vx = 0.0f;
        if (std::fabs(gCar.vy) < 0.02f && throttle < 0.01f) gCar.vy = 0.0f;
        if (std::fabs(gCar.r) < 0.02f && throttle < 0.01f) gCar.r = 0.0f;

        // World kinematics
        float cy = std::cos(gCar.yaw), sy = std::sin(gCar.yaw);
        gCar.pos.x += (gCar.vx * cy - gCar.vy * sy) * (float)dt;
        gCar.pos.z += (gCar.vx * sy + gCar.vy * cy) * (float)dt;
    }

} // namespace Physics

#include "Physics.h"
#include "../input/Input.h"
#include <algorithm>
#include <cmath>

namespace Physics {

    // DEFINITIONS (these allocate storage)
    Params P;
    VehicleState gCar;

    static inline float clamp(float v, float lo, float hi) {
        return std::max(lo, std::min(hi, v));
    }

    void Init() {
        P = Params{};
        gCar = VehicleState{};
        gCar.pos.y = 0.5f;
    }

    void Update(double dt, const Controls& c) {
        const float m = P.m, Iz = P.Iz, lf = P.lf, lr = P.lr, Cf = P.Cf, Cr = P.Cr;
        const float mu = P.mu, g = P.g;

        // --- speed bookkeeping ---
        const float speed = std::sqrt(gCar.vx * gCar.vx + gCar.vy * gCar.vy);
        const float v_eps = 0.10f;                       // smaller than before
        const float vx = (std::fabs(gCar.vx) < v_eps)    // guard denominators in atan2
            ? (gCar.vx >= 0.f ? v_eps : -v_eps)
            : gCar.vx;

        // Static axle loads (keep it simple for now)
        const float Fzf = m * g * lr / (lf + lr);
        const float Fzr = m * g * lf / (lf + lr);

        // Controls
        const float throttle = std::clamp(c.throttle, 0.f, 1.f);
        const float brake = std::clamp(c.brake, 0.f, 1.f);
        const float delta = std::clamp(c.steer, -1.f, 1.f) * 0.6f; // ~±34°

        // Slip angles
        float alpha_f = std::atan2(gCar.vy + lf * gCar.r, vx) - delta;
        float alpha_r = std::atan2(gCar.vy - lr * gCar.r, vx);

        // --- Near-standstill policy ---
        // If we're basically stopped, zero lateral slips so friction is available for launch,
        // and kill tiny residual vy/r from the previous turn/brake.
        const bool near_stopped = speed < 0.30f;
        if (near_stopped) {
            gCar.vy *= 0.2f;      // damp residual lateral
            gCar.r *= 0.2f;      // damp yaw
            if (throttle > 0.05f) {
                alpha_f = 0.0f;
                alpha_r = 0.0f;
            }
        }

        // Lateral forces (linear then clamp to μFz)
        auto clampf = [](float v, float a, float b) { return std::max(a, std::min(b, v)); };
        float Fyf = clampf(-Cf * alpha_f, -mu * Fzf, mu * Fzf);
        float Fyr = clampf(-Cr * alpha_r, -mu * Fzr, mu * Fzr);

        // Longitudinal command
        float Fx_cmd = throttle * P.Fx_max - brake * P.Fb_max;

        // Friction circle per axle
        auto circle = [](float muFz, float Fy) {
            float s = muFz * muFz - Fy * Fy;
            return s > 0.f ? std::sqrt(s) : 0.f;
            };
        float Fx_allow = circle(mu * Fzf, Fyf) + circle(mu * Fzr, Fyr);

        // When near-stopped and trying to launch, be generous with Fx allowance
        if (near_stopped && throttle > 0.05f) {
            Fx_allow = mu * (Fzf + Fzr);
        }

        float Fx = clampf(Fx_cmd, -Fx_allow, Fx_allow);

        // Aero drag (tiny near standstill)
        float Fdrag = 0.5f * P.rho * P.CdA * vx * std::fabs(vx);

        // Equations of motion (body frame)
        float ax = (Fx - Fdrag + Fyf * std::sin(delta)) / m + gCar.vy * gCar.r;
        float ay = (Fyr + Fyf * std::cos(delta)) / m - gCar.vx * gCar.r;
        float rdot = (lf * Fyf * std::cos(delta) - lr * Fyr) / Iz;

        // Integrate (semi-implicit)
        gCar.vx += ax * (float)dt;
        gCar.vy += ay * (float)dt;
        gCar.r += rdot * (float)dt;
        gCar.yaw += gCar.r * (float)dt;

        // Prevent tiny negative crawl from numerical drift after full brake:
        if (std::fabs(gCar.vx) < 0.02f && throttle < 0.01f) gCar.vx = 0.0f;
        if (std::fabs(gCar.vy) < 0.02f && throttle < 0.01f) gCar.vy = 0.0f;
        if (std::fabs(gCar.r) < 0.02f && throttle < 0.01f) gCar.r = 0.0f;

        // World kinematics
        float cy = std::cos(gCar.yaw), sy = std::sin(gCar.yaw);
        gCar.pos.x += (gCar.vx * cy - gCar.vy * sy) * (float)dt;
        gCar.pos.z += (gCar.vx * sy + gCar.vy * cy) * (float)dt;
    }


} // namespace Physics

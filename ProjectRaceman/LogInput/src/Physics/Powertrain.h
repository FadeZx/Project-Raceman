// Powertrain.h
#pragma once
#include <vector>

namespace Powertrain {
    struct Params {
        std::vector<float> gears = { 3.40f, 2.10f, 1.45f, 1.00f, 0.82f };
        float reverse_ratio = 3.30f;
        float final_drive = 4.10f;
        float wheel_radius = 0.33f;
        float driveline_eff = 0.93f;

        float idle_rpm = 950.f;
        float stall_rpm = 700.f;
        float redline_rpm = 7000.f;

        float engine_inertia = 0.10f;  // was 0.25 — smaller = faster rev up
        float engine_drag_nm = 3.0f;   // was 6.0  — smaller = less engine braking; raise to 5–7 for quicker rev down
        float idle_gain = 0.55f;  // was 0.25 — stronger anti-stall/launch help

        float clutch_lock_v = 3.0f;

        // Soft limiter
        float limiter_rpm = 7000.f;  // where power should taper to ~0
        float limiter_soft_width = 200.f;   // how early to start tapering (e.g. 6700→7000)

        // Shift cut (torque dip) for smoother gear changes
        float shift_rel_sec = 0.18f;
        float shift_cut_sec = 0.08f;  // 100 ms
        float shift_cut_min_scale = 0.60f;  // keep a little torque instead of 0
        float shift_reapply_ms = 90.f;   // optional extra smoothing when torque comes back

        // Engine brake map (drag grows with RPM)
        float engine_drag_rpm_knee = 3000.f; // below this, use base drag
        float engine_drag_nm_hi = 14.0f;  // drag near redline (higher than base)
    };

    struct State {
        int   gear = 0;         // -1=R, 0=N, 1..N forward
        float rpm = 0.0f;
        bool  auto_shift = true;
        float clutch_alpha = 0.0f;
        float shift_rel_t = 0.0f;

        // Already in your newer builds, but keep if missing:
        float since_left_neutral_s = 0.0f;

        // NEW: shift cut timer and last gear we were in
        float shift_cut_t = 0.0f;  // seconds remaining in torque cut
        int   last_gear = 0;     // to detect gear changes
    };


    extern Params P;
    extern State  S;

    void Init();
    void GearUp();
    void GearDown();
    void SetNeutral();  // NEW
    void SetReverse();  // NEW

    float TorqueAtRPM(float rpm);
    float ComputeWheelForce(float speed_mps, float throttle, float dt);
}

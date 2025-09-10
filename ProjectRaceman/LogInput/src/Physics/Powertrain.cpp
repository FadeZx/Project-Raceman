#include "Powertrain.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Powertrain {
    Params P;
    State  S;

    static inline float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
    void Init() {
        P = Params{};
        S = State{};
        S.gear = 0;                        // start in Neutral
        S.rpm = std::max(1200.0f, P.idle_rpm);
        S.auto_shift = true;
    }
    static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
    static inline float saturate(float x) { return std::max(0.0f, std::min(1.0f, x)); }
    static inline float smoothstep01(float x) {
        x = saturate(x);
        return x * x * (3.0f - 2.0f * x);
    }

    // Start the shift cut — do NOT use dt here
    static inline void StartShiftCut() {
        S.shift_cut_t = P.shift_cut_sec;
        S.shift_rel_t = P.shift_rel_sec;      // <— start slip window too
    }


    // Optional: one place to compute RPM-dependent engine drag
    static inline float EngineDragNm(float rpm) {
        const float span = std::max(1.0f, P.redline_rpm - P.idle_rpm);
        const float drag_s = (rpm - P.idle_rpm) / span;        // 0..1-ish
        const float base = P.engine_drag_nm * saturate(drag_s);

        // extra drag gain above knee
        const float hi_span = std::max(1.0f, P.redline_rpm - P.engine_drag_rpm_knee);
        const float over = std::max(0.0f, rpm - P.engine_drag_rpm_knee);
        const float over01 = std::min(1.0f, over / hi_span);
        const float blended = lerp(base, P.engine_drag_nm_hi, over01);

        return blended;
    }


    static inline void MarkLeftNeutralIfNeeded(int old_gear, int new_gear) {
        if (old_gear == 0 && new_gear != 0) S.since_left_neutral_s = 0.0f;
    }

    void GearUp() { int old = S.gear; if (S.gear < 0) S.gear = 0; else if (S.gear < (int)P.gears.size()) ++S.gear; if (S.gear != old) StartShiftCut(); }
    void GearDown() { int old = S.gear; if (S.gear > 1) --S.gear; else if (S.gear == 1) S.gear = 0; else if (S.gear == 0) S.gear = -1; if (S.gear != old) StartShiftCut(); }
    void SetNeutral() { int old = S.gear; S.gear = 0;  if (S.gear != old) StartShiftCut(); }
    void SetReverse() { int old = S.gear; S.gear = -1; if (S.gear != old) StartShiftCut(); }


    float TorqueAtRPM(float rpm) {
        // clamp in-band
        float r = std::clamp(rpm, P.stall_rpm, P.redline_rpm);

        // Base bell curve peaking ~4500
        float x = (r - 4500.0f) / 2000.0f;
        float bell = std::exp(-x * x);

        // Low-rpm boost so it doesn’t die near idle
        // ramps from ~60% at idle to 100% by ~2500 rpm
        float low = std::clamp((r - P.idle_rpm) / 1500.0f, 0.0f, 1.0f);
        float low_boost = 0.60f + 0.40f * low;

        // peak torque
        float peak = 280.0f; // Nm (example)

        // soften near redline
        float soft = 1.0f - std::pow(std::max(0.0f, (r - 6000.0f) / (P.redline_rpm - 6000.0f)), 1.5f);

        float base = peak * bell * low_boost * soft;

        // Soft limiter: fades torque from (limiter_rpm - width) to limiter_rpm
        float start = P.limiter_rpm - P.limiter_soft_width;
        float t = (rpm - start) / std::max(1.0f, P.limiter_soft_width);
        float limiter = 1.0f - saturate(t);           // 1 → 0
        limiter = std::max(0.0f, limiter);

        return base * limiter;
    }


    float ComputeWheelForce(float speed_mps, float throttle, float dt)
    {

        throttle = clamp01(throttle);

        // timers (use dt here — cast to float to avoid float/double mismatch)
        S.since_left_neutral_s += (float)dt;
        S.shift_cut_t = std::max(0.0f, S.shift_cut_t - (float)dt);
        S.shift_rel_t = std::max(0.0f, S.shift_rel_t - (float)dt);


        // Shift-cut scale (1 at end of cut, P.shift_cut_min_scale at start)
        float cut_phase = (P.shift_cut_sec > 1e-4f)
            ? (1.0f - S.shift_cut_t / P.shift_cut_sec)
            : 1.0f;
        float cut_curve = 1.0f - smoothstep01(1.0f - cut_phase);
        float shift_cut_scale = lerp(P.shift_cut_min_scale, 1.0f, cut_curve);

        // Neutral: free-rev, no wheel torque
        if (S.gear == 0) {
            const float idle_def = std::max(0.0f, P.idle_rpm - S.rpm);
            const float Te_idle = P.idle_gain * idle_def;
            const float Te_drag = EngineDragNm(S.rpm);

            const float Te_net = TorqueAtRPM(S.rpm) * throttle + Te_idle - Te_drag;
            const float alpha_e = Te_net / std::max(0.05f, P.engine_inertia);
            S.rpm += (alpha_e * (float)dt) * 60.0f / (2.0f * (float)M_PI);
            S.rpm = std::clamp(S.rpm, P.stall_rpm, P.redline_rpm);
            S.clutch_alpha = 0.0f;
            return 0.0f;
        }

        // Wheel kinematics / gearing
        const bool  reverse = (S.gear < 0);
        const float ratio = reverse ? P.reverse_ratio : P.gears[S.gear - 1];
        const float gtot = ratio * P.final_drive;

        const float wheel_omega = (P.wheel_radius > 0.0f) ? (speed_mps / P.wheel_radius) : 0.0f;
        const float locked_rpm = wheel_omega * gtot * 60.0f / (2.0f * (float)M_PI);

        // Engine torque terms
        const float Te_base = TorqueAtRPM(S.rpm) * throttle;
        const float idle_def = std::max(0.0f, P.idle_rpm - S.rpm);
        const float Te_idle = P.idle_gain * idle_def;
        const float Te_drag = EngineDragNm(S.rpm);
        const float Te_net = Te_base + Te_idle - Te_drag;

        // Free-rev integration
        const float alpha_e = Te_net / std::max(0.05f, P.engine_inertia);
        const float free_rpm = S.rpm + (alpha_e * (float)dt) * 60.0f / (2.0f * (float)M_PI);

        // Idle help at very low rpm
        const float idle_help = 25.0f; // was 10.0f

        float low_rpm_boost = std::clamp((P.idle_rpm * 1.3f - S.rpm) / (P.idle_rpm * 0.3f), 0.0f, 1.0f);

        float Te_now = TorqueAtRPM(S.rpm) * throttle + Te_idle - Te_drag + idle_help * low_rpm_boost;
        Te_now = std::max(0.0f, Te_now);

        // Clutch model
        float clutch_frac = 0.0f;
        const float engage = std::clamp(S.since_left_neutral_s / 0.15f, 0.0f, 1.0f);

        auto predictNextLockedRpm = [&](int nextGear)->float {
            const bool rev = (nextGear < 0);
            const float ratio_n = rev ? P.reverse_ratio : P.gears[nextGear - 1];
            const float gtot_n = ratio_n * P.final_drive;
            return wheel_omega * gtot_n * 60.0f / (2.0f * (float)M_PI);
            };

        // Throttle-aware minimum acceptable RPM after the shift (avoid lugging)
        auto minPostShiftRpm = [&](float th)->float {
            // cruise (~2200) … hard push (~4000)
            return 2200.0f + 1800.0f * th;
            };

        auto slipPhase = [&]() {
            if (P.shift_rel_sec <= 1e-4f) return 0.0f;
            float t = 1.0f - (S.shift_rel_t / P.shift_rel_sec);
            return 1.0f - smoothstep01(t); // starts ~1, eases to 0
            };

        // --- LAUNCH REGION ---
       // --- LAUNCH REGION ---
        if (speed_mps < 1.0f) {
            const float target_locked = locked_rpm;

            const float rpm_excess = std::clamp((S.rpm - P.idle_rpm) / (P.idle_rpm * 0.5f), 0.0f, 1.0f);

            // Deadband for "foot off the gas"
            const bool throttle_dead = (throttle < 0.02f);

            // (A) TORQUE PATH: near-zero baseline, scale with throttle (no surprise creep)
            float clutch_torque_frac;
            if (throttle_dead) {
                // choose 0.00f for *no* creep, or small (e.g. 0.05f) if you want mild AT-like creep
                clutch_torque_frac = 0.00f;
            }
            else {
                clutch_torque_frac = std::clamp(0.10f + 0.70f * throttle + 0.30f * rpm_excess, 0.0f, 0.95f);
            }

            // Launch assist floor only when actually pressing the pedal
            if (!throttle_dead && S.gear == 1 && throttle > 0.30f) {
                clutch_torque_frac = std::max(clutch_torque_frac, 0.45f);
            }

            // (B) RPM PATH: allow a little flare only when you’re on throttle
            const float launch_headroom = throttle_dead ? 0.0f : (600.0f * throttle);
            const float target_locked_plus = target_locked + launch_headroom;

            float rpm_diff = std::max(0.0f, S.rpm - target_locked_plus);
            float rpm_blend_scale = 1.0f - std::clamp(rpm_diff / 3000.0f, 0.0f, 0.8f);

            float clutch_rpm_frac = (clutch_torque_frac * 0.6f) * rpm_blend_scale;
            const float engage = std::clamp(S.since_left_neutral_s / 0.15f, 0.0f, 1.0f);
            clutch_rpm_frac *= engage;
            clutch_rpm_frac = std::max(clutch_rpm_frac, 0.04f * engage);

            // Update engine rpm
            S.rpm = (1.0f - clutch_rpm_frac) * free_rpm + clutch_rpm_frac * target_locked_plus;
            S.rpm = std::max(S.rpm, P.idle_rpm - 100.0f);

            // Export clutch for UI/logic
            clutch_frac = clutch_torque_frac;
        }

        else {
            float alpha = 0.35f
                + 0.55f * std::clamp((speed_mps - 1.0f) / std::max(0.001f, P.clutch_lock_v - 1.0f), 0.0f, 1.0f)
                + 0.10f * throttle;
            alpha = std::clamp(alpha, 0.0f, 1.0f);

            alpha *= engage;
            clutch_frac = alpha;
            S.clutch_alpha = clutch_frac;
            const float target_locked = std::max(locked_rpm, P.idle_rpm);
            S.rpm = (1.0f - alpha) * free_rpm + alpha * target_locked;
            S.rpm = std::clamp(S.rpm, P.stall_rpm, P.redline_rpm);
        }


        // --- track time in current gear ---
        static int   prev_gear = -999;
        static float time_in_gear_s = 0.0f;
        if (S.gear != prev_gear) { prev_gear = S.gear; time_in_gear_s = 0.0f; }
        else { time_in_gear_s += (float)dt; }

        // These are the ONLY latch vars
        static int  last_gear = -1;
        static bool above_up = false;
        static bool below_dn = false;

        // reset latches when gear changes or right after a shift
        if (S.gear != last_gear || time_in_gear_s < 0.02f) {
            above_up = false;
            below_dn = false;
            last_gear = (int)S.gear;
        }

        // --- Auto-shift (forward only) ---
        if (S.auto_shift && S.gear > 0 && (int)S.gear < (int)P.gears.size()) {
            const float engine_rpm = S.rpm;

            // a touch earlier in lower gears, later in higher gears
            float up_hi, up_lo;
            if (S.gear <= 2) {             // 1st–2nd: eager
                up_hi = P.limiter_rpm - 200.0f;
                up_lo = P.limiter_rpm - 500.0f;
            }
            else if (S.gear == 3) {      // 3rd: moderate
                up_hi = P.limiter_rpm - 300.0f;
                up_lo = P.limiter_rpm - 600.0f;
            }
            else {                       // 4th+: be conservative
                up_hi = P.limiter_rpm - 450.0f;
                up_lo = P.limiter_rpm - 700.0f;
            }
            const float dn_lo = 1900.0f, dn_hi = 2200.0f;

            // (NEW) must spend a minimum time in gear to avoid chatter
            const bool min_time_ok = (time_in_gear_s > 0.18f);

            // (TWEAK) a slightly looser upshift clutch gate in low gears
            const float clutch_gate_up = (S.gear <= 2) ? 0.50f : 0.60f;

            // require some forward motion and an engaged clutch (but a bit looser for early gears)
            if (speed_mps > 3.0f && clutch_frac > clutch_gate_up && min_time_ok) {

                // *** USE THE OUTER LATCHES — do NOT redeclare here ***
                if (engine_rpm > up_hi) above_up = true;
                if (engine_rpm < up_lo) above_up = false;
                if (engine_rpm < dn_lo) below_dn = true;
                if (engine_rpm > dn_hi) below_dn = false;

                if (above_up) {
                    const int next = S.gear + 1;
                    const float r_next = predictNextLockedRpm(next);
                    const float r_min = minPostShiftRpm(throttle);

                    // Optional: also require a small minimum speed for 4→5
                    const bool speed_ok_45 = (S.gear == 4) ? (speed_mps > 22.0f) /* ~79 km/h */ : true;

                    if (r_next >= r_min && speed_ok_45) {
                        ++S.gear;
                        StartShiftCut();
                        above_up = false;
                        time_in_gear_s = 0.0f;
                        last_gear = (int)S.gear;
                    }

                    else if (below_dn && S.gear > 1) {
                        --S.gear;                      // shift down
                        StartShiftCut();
                        below_dn = false;
                        time_in_gear_s = 0.0f;
                        last_gear = (int)S.gear;
                    }
                }
            }

            // --- failsafe: near limiter -> force upshift (not blocked by speed/clutch gates)
            const bool near_limiter = (engine_rpm > (P.limiter_rpm - 50.0f));
            if (near_limiter && time_in_gear_s > 0.30f && S.gear < (int)P.gears.size()) {
                ++S.gear;
                if (S.gear > (int)P.gears.size()) S.gear = (int)P.gears.size();
                StartShiftCut();
                time_in_gear_s = 0.0f;
                above_up = below_dn = false;  // reset latches after shift
                last_gear = (int)S.gear;
            }
        }

    }
} // namespace Powertrain

#pragma once

namespace raceman::physics
{

// -------------------------------------------------------------------------
// Discrete drivetrain events.
//
// The vehicle sim runs at a fixed 60 Hz and can take up to four steps per
// rendered frame, while audio ticks once. Polling gear changes from the audio
// tick therefore collapses a double-shift into one event and misses it
// entirely if it reverses. Events are queued here instead and drained later.
// -------------------------------------------------------------------------
enum class EngineShiftKind
{
    Up,
    Down,
    ToNeutral,
    Launch,     // 0 -> 1, happens on every standing start
    ToReverse,
};

struct EngineShiftEvent
{
    EngineShiftKind kind{EngineShiftKind::Up};
    int   fromGear{0};
    int   toGear{1};
    float rpmBefore{0.0f};
};

// -------------------------------------------------------------------------
// Tuning. Defaults are sane for a road car; idle/redline come from the
// vehicle's arcadeHandling config so they can never disagree with the sim.
// -------------------------------------------------------------------------
struct VehicleEngineTuning
{
    float idleRpm{900.0f};
    float redlineRpm{6000.0f};

    // Rotational inertia from EngineConfig. Higher means the engine is slower
    // to pick up and slower to drop when the clutch is out.
    float inertia{0.25f};

    // How tightly RPM follows the gearing when the clutch is locked. Small but
    // non-zero: drivetrain compliance and tyre slip smooth the real thing.
    float lockedTimeConstant{0.05f};

    // Rev limiter: cut fuel, let it fall this far, then light it again.
    float limiterCutSeconds{0.06f};
    float limiterReleaseRpm{220.0f};

    // Ignition cut on an upshift. Deliberately NOT the gearbox's shift time:
    // the clutch is out for the whole mechanical shift, but fuel is only
    // interrupted for a fraction of it. Tying the two together silences the
    // engine for a quarter second on every gear change.
    float shiftCutSeconds{0.07f};

    // Forced induction. The sim has no boost model, so it is integrated here.
    bool  turboEnabled{false};
    bool  supercharger{false};      // rigid RPM lock, no spool lag
    float turboSpoolSeconds{0.55f};
    float turboBleedSeconds{0.30f};
    float turboMinRpmFraction{0.25f};
};

struct VehicleEngineInput
{
    float targetRpmFromGearing{900.0f}; // what GearSpeedToRpm() produced
    float throttle{0.0f};
    float brake{0.0f};
    int   gear{0};                      // -1 reverse, 0 neutral, 1..N
    bool  shifting{false};              // autoShiftCooldown > 0
    float speed{0.0f};
    float previousSpeed{0.0f};
    float commandedAcceleration{18.0f}; // arcadeHandling.acceleration
    float wheelspin{0.0f};              // 0..1, traction control cut
    float deltaTime{1.0f / 60.0f};
};

// -------------------------------------------------------------------------
// Engine state.
//
// This exists because the live RPM signal is purely kinematic — it is
// speed / gearTopSpeed with no smoothing at all, so the engine cannot rev in
// neutral, cannot flare on a downshift, and carries no notion of load. None of
// that matters for handling, but all of it is audible.
// -------------------------------------------------------------------------
struct VehicleEngineState
{
    static constexpr int kMaxPendingShiftEvents = 8;

    float rpm{900.0f};
    float targetRpm{900.0f};
    float load{0.0f};           // 0 = overrun/coasting, 1 = working hard
    float throttle{0.0f};
    float boost{0.0f};          // 0..1
    int   gear{0};
    bool  shiftCut{false};      // ignition interrupted for a gear change
    bool  limiterCut{false};    // ignition interrupted by the rev limiter
    bool  clutchEngaged{true};
    bool  initialized{false};

    float limiterTimer{0.0f};
    float shiftCutTimer{0.0f};

    EngineShiftEvent shiftEvents[kMaxPendingShiftEvents]{};
    int shiftEventCount{0};

    void Update(const VehicleEngineTuning& tuning, const VehicleEngineInput& input);

    void PushShiftEvent(const EngineShiftEvent& event);
    void ClearShiftEvents() { shiftEventCount = 0; }

    // Normalised position in the rev range, 0 at idle and 1 at redline.
    float RpmFraction(const VehicleEngineTuning& tuning) const;
};

} // namespace raceman::physics

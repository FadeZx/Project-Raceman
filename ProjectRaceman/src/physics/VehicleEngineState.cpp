#include "VehicleEngineState.h"

#include <algorithm>
#include <cmath>

namespace raceman::physics
{

namespace
{

float Clamp01(float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

// Frame-rate independent approach rate for a first-order lag. The existing
// audio code uses a raw `rate * dt` lerp, which overshoots badly once the frame
// time passes 1/rate; this cannot.
float ApproachAlpha(float timeConstant, float deltaTime)
{
    if (timeConstant <= 1.0e-4f)
    {
        return 1.0f;
    }
    return 1.0f - std::exp(-deltaTime / timeConstant);
}

EngineShiftKind ClassifyShift(int fromGear, int toGear)
{
    if (toGear == 0)
    {
        return EngineShiftKind::ToNeutral;
    }
    if (toGear < 0)
    {
        return EngineShiftKind::ToReverse;
    }
    if (fromGear <= 0)
    {
        // Every standing start passes through here. The old polling code
        // explicitly skipped it, so launches were silent.
        return EngineShiftKind::Launch;
    }
    return (toGear > fromGear) ? EngineShiftKind::Up : EngineShiftKind::Down;
}

} // namespace

float VehicleEngineState::RpmFraction(const VehicleEngineTuning& tuning) const
{
    const float span = (std::max)(1.0f, tuning.redlineRpm - tuning.idleRpm);
    return Clamp01((rpm - tuning.idleRpm) / span);
}

void VehicleEngineState::PushShiftEvent(const EngineShiftEvent& event)
{
    if (shiftEventCount >= kMaxPendingShiftEvents)
    {
        return; // queue full; dropping is better than shifting the ring under a reader
    }
    shiftEvents[shiftEventCount++] = event;
}

void VehicleEngineState::Update(const VehicleEngineTuning& tuning, const VehicleEngineInput& input)
{
    const float deltaTime = (std::max)(1.0e-5f, input.deltaTime);
    const float idle = (std::max)(0.0f, tuning.idleRpm);
    const float redline = (std::max)(idle + 1.0f, tuning.redlineRpm);
    const float span = redline - idle;

    if (!initialized)
    {
        rpm = (std::clamp)(input.targetRpmFromGearing, idle, redline);
        targetRpm = rpm;
        gear = input.gear;
        initialized = true;
    }

    // --- gear change detection -------------------------------------------
    if (input.gear != gear)
    {
        EngineShiftEvent event;
        event.kind = ClassifyShift(gear, input.gear);
        event.fromGear = gear;
        event.toGear = input.gear;
        event.rpmBefore = rpm;
        PushShiftEvent(event);
        gear = input.gear;
    }

    throttle = Clamp01(input.throttle);

    // --- clutch ------------------------------------------------------------
    // In gear and settled, the engine is mechanically tied to the wheels. In
    // neutral or mid-shift it is free to follow the throttle on its own inertia,
    // which is what produces a downshift flare and lets it rev at a standstill.
    const bool inGear = input.gear != 0;
    shiftCut = input.shifting && inGear;
    clutchEngaged = inGear && !input.shifting;

    // --- target ------------------------------------------------------------
    if (clutchEngaged)
    {
        targetRpm = (std::clamp)(input.targetRpmFromGearing, idle, redline);
    }
    else
    {
        // Free revving: throttle position maps straight onto the rev range.
        targetRpm = idle + throttle * span;
    }

    // --- rev limiter -------------------------------------------------------
    // A hard clamp just pins RPM flat and silent at redline. Real limiters cut
    // fuel, let the engine fall, then relight — that bounce is the sound.
    if (limiterCut)
    {
        // Fuel is off, so pull the target down and let inertia carry RPM away
        // from the limit. Release is purely on the timer: RPM approaches the
        // release threshold asymptotically and would never compare <= to it,
        // which would latch the cut on forever and pin the engine flat.
        targetRpm = (std::min)(targetRpm, redline - tuning.limiterReleaseRpm);
        limiterTimer -= deltaTime;
        if (limiterTimer <= 0.0f)
        {
            limiterCut = false;
        }
    }
    else if (rpm >= redline - 1.0f)
    {
        // Trigger just below redline for the same asymptotic reason.
        limiterCut = true;
        limiterTimer = (std::max)(0.0f, tuning.limiterCutSeconds);
    }

    // --- inertia -----------------------------------------------------------
    float timeConstant;
    if (clutchEngaged)
    {
        timeConstant = (std::max)(0.005f, tuning.lockedTimeConstant);
    }
    else
    {
        // Picking up against compression is slower than falling on a closed
        // throttle, and both scale with the authored flywheel inertia.
        const float inertiaScale = (std::max)(0.02f, tuning.inertia);
        timeConstant = (targetRpm > rpm) ? (0.55f * inertiaScale + 0.06f)
                                         : (0.90f * inertiaScale + 0.10f);
    }
    rpm += (targetRpm - rpm) * ApproachAlpha(timeConstant, deltaTime);
    rpm = (std::clamp)(rpm, idle, redline);

    // --- load --------------------------------------------------------------
    // Engine load is torque demand, which is essentially throttle position.
    // At wide-open throttle in top gear the engine is at FULL load even though
    // the car has almost stopped accelerating, because drag is absorbing the
    // power. Deriving load from measured acceleration gets this backwards and
    // makes the engine fade out in high gears exactly when it should sound
    // hardest worked, so acceleration is only a small flavour term here.
    const float measuredAcceleration = (input.speed - input.previousSpeed) / deltaTime;
    const float commanded = (std::max)(0.1f, input.commandedAcceleration);
    const float accelerationRatio = Clamp01(measuredAcceleration / commanded);

    float loadTarget = throttle * (0.88f + 0.12f * accelerationRatio);
    // Wheelspin genuinely unloads the engine: it flares and thins out.
    loadTarget *= 1.0f - 0.6f * Clamp01(input.wheelspin);
    if (shiftCut || limiterCut)
    {
        loadTarget = 0.0f;
    }
    load += (Clamp01(loadTarget) - load) * ApproachAlpha(0.08f, deltaTime);
    load = Clamp01(load);

    // --- forced induction --------------------------------------------------
    if (tuning.turboEnabled)
    {
        const float minFraction = (std::clamp)(tuning.turboMinRpmFraction, 0.0f, 0.95f);
        const float rpmFraction = Clamp01((rpm - idle) / span);
        const float capable = Clamp01((rpmFraction - minFraction) / (std::max)(0.05f, 1.0f - minFraction));
        const float boostTarget = Clamp01(load * capable);

        if (tuning.supercharger)
        {
            // Belt driven, so it tracks RPM with no lag at all.
            boost = boostTarget;
        }
        else
        {
            const float boostTau = (boostTarget > boost)
                ? (std::max)(0.01f, tuning.turboSpoolSeconds)
                : (std::max)(0.01f, tuning.turboBleedSeconds);
            boost += (boostTarget - boost) * ApproachAlpha(boostTau, deltaTime);
        }
        boost = Clamp01(boost);
    }
    else
    {
        boost = 0.0f;
    }
}

} // namespace raceman::physics

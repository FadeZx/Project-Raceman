#include "SceneEditorVehicleTelemetry.h"

#include "SceneEditorInternal.h"
#include "../input/InputManager.h"

#include <algorithm>
#include <cmath>

namespace raceman {

namespace {

// Relative texture amplitude and coarseness of each surface. Asphalt is a
// faint high frequency hum, dirt is loud and coarse, kerbs are handled
// separately as impacts. Values are project-editable (Project Settings ->
// Physics -> Track Surface Layers) rather than fixed here.
struct SurfaceTexture {
    float amplitude;
    float frequencyScale;
};

SurfaceTexture TextureForSurface(TrackSurfaceType surface, const TrackSurfaceSettings& surfaceSettings) {
    const ColliderSurfaceConfig& config = scene_editor_internal::GetTrackSurfaceSettings(surfaceSettings, surface);
    return {config.ffbRoadAmplitude, (std::max)(0.01f, config.ffbRoadFrequencyScale)};
}

float MoveTowards(float current, float target, float maxDelta) {
    if (current < target) {
        return (std::min)(current + maxDelta, target);
    }
    return (std::max)(current - maxDelta, target);
}

// Simplified magic-formula lateral force: rises to a peak at the slip angle
// limit, then falls away as the tyre starts to slide.
float NormalizedLateralForce(float slipAngleDegrees, float peakSlipDegrees) {
    const float peak = (std::max)(1.0f, peakSlipDegrees);
    const float normalized = slipAngleDegrees / peak;
    const float shaped = std::sin(1.55f * std::atan(1.9f * normalized));
    return (std::clamp)(shaped / 0.985f, -1.0f, 1.0f);
}

// Pneumatic trail collapses as the tyre approaches its limit. This is what
// makes the wheel go light just before the front end washes out, and it is the
// single most important cue a driving wheel can give.
float PneumaticTrail(float slipAngleDegrees, float peakSlipDegrees) {
    const float peak = (std::max)(1.0f, peakSlipDegrees);
    const float ratio = (std::clamp)(std::fabs(slipAngleDegrees) / peak, 0.0f, 2.0f);
    return (std::clamp)(1.0f - ratio * 0.85f, -0.12f, 1.0f);
}

} // namespace

void ClearVehicleForceFeedback(InputManager* inputManager) {
    if (inputManager == nullptr) {
        return;
    }
    inputManager->SetWheelForceFeedbackState(WheelForceFeedbackState{});
    inputManager->SetWheelForceFeedbackActive(false);
}

WheelForceFeedbackState BuildWheelForceFeedbackSample(const ArcadeVehicleTelemetry& telemetry,
                                                      const raceman::physics::VehicleConfig& config,
                                                      const TrackSurfaceSettings& surfaceSettings,
                                                      WheelForceFeedbackRuntimeState& runtimeState,
                                                      float deltaTime) {
    WheelForceFeedbackState sample;
    if (config.wheels.empty() || deltaTime <= 0.0f) {
        return sample;
    }

    const float speedAbs = std::fabs(telemetry.longitudinalSpeed);
    const float maxSpeed = (std::max)(1.0f, telemetry.maxForwardSpeed);
    // Below walking pace the tyres cannot generate a meaningful aligning
    // torque, so the forces fade out instead of jittering around zero.
    const float speedFactor = (std::clamp)(speedAbs / 8.0f, 0.0f, 1.0f);
    const float speedRatio = (std::clamp)(speedAbs / maxSpeed, 0.0f, 1.0f);
    const float peakSlip = (std::max)(2.0f, config.tireGrip.slipAngleLimit);

    // --- Front axle state -------------------------------------------------
    float frontNormalForce = 0.0f;
    float frontSlipAngle = 0.0f;
    float frontTraction = 0.0f;
    float steeredWheelCount = 0.0f;
    bool anyFrontGrounded = false;
    float textureAmplitude = 0.0f;
    float textureFrequencyScale = 1.0f;
    float lockupAmount = 0.0f;
    float spinAmount = 0.0f;
    float suspensionTravelDelta = 0.0f;
    float kerbImpulse = 0.0f;

    for (std::size_t i = 0; i < telemetry.wheels.size() && i < config.wheels.size(); ++i) {
        const ArcadeVehicleWheelTelemetry& wheel = telemetry.wheels[i];
        const bool isFront = wheel.steered || config.wheels[i].mountPosition.y >= 0.0f ||
                             config.wheels[i].maxSteerAngle > 0.01f;
        if (isFront) {
            frontNormalForce += wheel.normalForce;
            frontSlipAngle += wheel.slipAngle;
            frontTraction += wheel.tractionScale;
            steeredWheelCount += 1.0f;
            anyFrontGrounded = anyFrontGrounded || wheel.grounded;

            const SurfaceTexture texture = TextureForSurface(wheel.surfaceType, surfaceSettings);
            if (wheel.grounded) {
                textureAmplitude = (std::max)(textureAmplitude, texture.amplitude);
                textureFrequencyScale = (std::min)(textureFrequencyScale, texture.frequencyScale);
            }
            if (wheel.surfaceType == TrackSurfaceType::Curb && wheel.grounded) {
                kerbImpulse = (std::max)(kerbImpulse, 1.0f);
            }

            // The tyre model already resolved this per wheel, so take its answer
            // rather than inferring one from rolling speed. Slip ratio is signed:
            // negative is a wheel being braked below rolling speed, positive is
            // one being driven above it, and the rack feels both.
            if (wheel.grounded) {
                lockupAmount = (std::max)(lockupAmount, (std::clamp)(-wheel.slipRatio, 0.0f, 1.0f));
                spinAmount = (std::max)(spinAmount, (std::clamp)(wheel.slipRatio, 0.0f, 1.0f));
            }
            suspensionTravelDelta = (std::max)(suspensionTravelDelta, std::fabs(wheel.suspensionTravel));
        }
    }

    if (steeredWheelCount <= 0.0f) {
        // No wheel identified as steered; fall back to whole-car telemetry.
        frontSlipAngle = telemetry.slipAngle;
        frontTraction = telemetry.tractionScale;
        steeredWheelCount = 1.0f;
        frontNormalForce = config.chassis.mass * 9.81f * 0.5f;
        anyFrontGrounded = true;
    } else {
        frontSlipAngle /= steeredWheelCount;
        frontTraction /= steeredWheelCount;
    }

    if (!runtimeState.initialized) {
        runtimeState.initialized = true;
        runtimeState.previousSuspensionTravel = suspensionTravelDelta;
        runtimeState.previousVerticalVelocity = telemetry.verticalVelocity;
    }

    // Static axle load is the reference the measured load is compared against,
    // so the wheel gets heavier under braking and lighter over crests.
    const float staticFrontLoad = (std::max)(1.0f, config.chassis.mass * 9.81f * 0.5f);
    const float loadRatio = (std::clamp)(frontNormalForce / staticFrontLoad, 0.0f, 2.0f);

    // --- Self aligning torque --------------------------------------------
    // Each wheel now reports a real signed slip angle, so this only falls back
    // to steering input when the front axle is airborne and there is no slip
    // angle to read at all.
    const float slipSign = std::fabs(frontSlipAngle) > 0.01f
        ? (frontSlipAngle < 0.0f ? -1.0f : 1.0f)
        : (telemetry.steering < 0.0f ? -1.0f : 1.0f);
    const float slipMagnitude = (std::max)(std::fabs(frontSlipAngle),
                                           std::fabs(telemetry.steering) * peakSlip * 0.55f * speedFactor);
    const float lateralForce = NormalizedLateralForce(slipMagnitude, peakSlip) * slipSign;
    const float trail = PneumaticTrail(slipMagnitude, peakSlip);

    float selfAligningTorque = -lateralForce * trail * loadRatio * speedFactor;
    selfAligningTorque *= (std::clamp)(frontTraction, 0.0f, 1.5f);
    selfAligningTorque *= (std::clamp)(telemetry.surfaceGrip, 0.2f, 1.5f);
    if (!anyFrontGrounded) {
        selfAligningTorque = 0.0f;
    }

    // Lateral load transfer through a corner adds weight to the rack on top of
    // the aligning torque itself.
    const float lateralAcceleration = telemetry.yawRate * telemetry.longitudinalSpeed;
    const float lateralLoadFeel = (std::clamp)(lateralAcceleration / 15.0f, -1.0f, 1.0f);
    selfAligningTorque += lateralLoadFeel * 0.12f * speedFactor;

    const float torqueFilter = (std::clamp)(deltaTime * 30.0f, 0.0f, 1.0f);
    runtimeState.smoothedSelfAligningTorque +=
        (selfAligningTorque - runtimeState.smoothedSelfAligningTorque) * torqueFilter;
    sample.steeringTorque = (std::clamp)(runtimeState.smoothedSelfAligningTorque, -1.0f, 1.0f);

    // --- Impacts: kerbs, landings and collisions --------------------------
    const float verticalJerk = std::fabs(telemetry.verticalVelocity - runtimeState.previousVerticalVelocity);
    runtimeState.previousVerticalVelocity = telemetry.verticalVelocity;
    const float suspensionJerk = std::fabs(suspensionTravelDelta - runtimeState.previousSuspensionTravel) / deltaTime;
    runtimeState.previousSuspensionTravel = suspensionTravelDelta;

    const float impactStrength = (std::clamp)(verticalJerk / 6.0f, 0.0f, 1.0f) +
                                 (std::clamp)(suspensionJerk / 8.0f, 0.0f, 1.0f);
    if (impactStrength > runtimeState.impact) {
        runtimeState.impact = (std::clamp)(impactStrength, 0.0f, 1.0f);
        // Kick the wheel away from the side that was loaded, alternating with
        // the steering so kerb strikes do not always pull the same way.
        runtimeState.impactDirection = telemetry.steering >= 0.0f ? -1.0f : 1.0f;
    }
    runtimeState.impact = MoveTowards(runtimeState.impact, 0.0f, deltaTime * 6.0f);
    sample.impact = runtimeState.impact;
    sample.impactDirection = runtimeState.impactDirection;

    // Kerb rattle decays over a fraction of a second rather than vanishing the
    // instant the tyre leaves the rumble strip.
    runtimeState.kerbEnergy = (std::max)(runtimeState.kerbEnergy, kerbImpulse);
    runtimeState.kerbEnergy = MoveTowards(runtimeState.kerbEnergy, 0.0f, deltaTime * 3.0f);

    // --- Road surface and tyre texture ------------------------------------
    const float wheelRadius = (std::max)(0.15f, config.wheels.front().radius);
    // Tread frequency follows how fast the contact patch moves, so texture
    // rises in pitch with speed like real road noise.
    const float treadFrequency = (speedAbs / (6.2831853f * wheelRadius)) * 8.0f * textureFrequencyScale;

    const float roadAmplitude = textureAmplitude * speedRatio * 0.65f;
    const float kerbAmplitude = runtimeState.kerbEnergy * (0.35f + speedRatio * 0.45f);
    const float slideAmount = (std::clamp)(
        (std::max)(telemetry.frontSlip, telemetry.tireScrub) * (1.0f - (std::clamp)(frontTraction, 0.0f, 1.0f)),
        0.0f, 1.0f);
    const float slideAmplitude = slideAmount * 0.5f * speedFactor;
    const float lockupAmplitude = lockupAmount * 0.55f;
    const float spinAmplitude = spinAmount * 0.3f;

    sample.roadAmplitude = (std::clamp)(roadAmplitude, 0.0f, 1.0f);
    sample.kerbAmplitude = (std::clamp)(kerbAmplitude, 0.0f, 1.0f);
    sample.slipAmplitude = (std::clamp)(slideAmplitude, 0.0f, 1.0f);
    sample.lockupAmplitude = (std::clamp)(lockupAmplitude + spinAmplitude, 0.0f, 1.0f);
    sample.vibrationAmplitude = (std::clamp)(
        roadAmplitude + kerbAmplitude + slideAmplitude + lockupAmplitude + spinAmplitude, 0.0f, 1.0f);

    float vibrationFrequency = (std::clamp)(treadFrequency, 12.0f, 90.0f);
    if (runtimeState.kerbEnergy > 0.15f) {
        vibrationFrequency = (std::clamp)(14.0f + speedRatio * 22.0f, 12.0f, 45.0f);
    } else if (lockupAmount > 0.25f) {
        // Locked tyres judder at a low, unmistakable frequency.
        vibrationFrequency = 18.0f;
    } else if (slideAmount > 0.3f) {
        vibrationFrequency = (std::clamp)(24.0f + slideAmount * 20.0f, 20.0f, 55.0f);
    }
    sample.vibrationFrequencyHz = vibrationFrequency;

    // --- Engine and drivetrain rumble -------------------------------------
    const float redline = (std::max)(1.0f, telemetry.redlineRPM);
    const float rpmRatio = (std::clamp)(telemetry.engineRPM / redline, 0.0f, 1.2f);
    sample.rumbleAmplitude = (std::clamp)(0.12f + rpmRatio * 0.35f + telemetry.throttle * 0.15f, 0.0f, 1.0f) *
                             (rpmRatio > 0.02f ? 1.0f : 0.0f);
    // Two firing events per revolution keeps the rumble in a range wheels can
    // actually reproduce.
    sample.rumbleFrequencyHz = (std::clamp)(telemetry.engineRPM / 60.0f * 2.0f, 8.0f, 120.0f);
    sample.engineRPM = telemetry.engineRPM;
    sample.redlineRPM = redline;

    // --- Conditions -------------------------------------------------------
    // Damping is heaviest at a standstill (where it stops the wheel oscillating
    // on its own forces) and eases off as aerodynamic and tyre forces take over.
    sample.damper = (std::clamp)(0.35f * (1.0f - speedFactor) + 0.08f + slideAmount * 0.1f, 0.0f, 1.0f);
    // Scrub friction rises with the load pressing the tyre into the road.
    sample.friction = (std::clamp)(0.05f + loadRatio * 0.12f + telemetry.handbrake * 0.1f, 0.0f, 1.0f);
    // Centring covers the cases the tyre model cannot: stopped, or airborne.
    const float airborne = anyFrontGrounded ? 0.0f : 1.0f;
    sample.centeringSpring = (std::clamp)((1.0f - speedFactor) * 0.8f + airborne * 0.6f, 0.0f, 1.0f);

    return sample;
}

} // namespace raceman

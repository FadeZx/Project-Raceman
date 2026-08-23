#include "SceneEditorVehicleDynamics.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/quaternion.hpp>

namespace raceman {
namespace {

float MoveTowards(float current, float target, float maxDelta) {
    if (current < target) {
        return (std::min)(current + maxDelta, target);
    }
    return (std::max)(current - maxDelta, target);
}

float SmoothingAlpha(float smoothing, float deltaTime) {
    if (smoothing <= 0.0f) {
        return 1.0f;
    }
    return (std::clamp)(1.0f - std::exp(-smoothing * deltaTime), 0.0f, 1.0f);
}

float DifferentialLockForThrottle(const raceman::physics::DifferentialConfig& differential, bool throttleApplied) {
    switch (differential.type) {
    case raceman::physics::DifferentialConfig::Type::Open:
        return 0.0f;
    case raceman::physics::DifferentialConfig::Type::Locked:
        return 1.0f;
    case raceman::physics::DifferentialConfig::Type::LimitedSlip:
    default:
        return (std::clamp)(
            (std::max)(differential.lockStrength, throttleApplied ? differential.powerLock : differential.coastLock),
            0.0f,
            1.0f);
    }
}

} // namespace

// Tractive effort the engine is actually making, normalised so a mid-gear
// pull at the torque peak comes out at 1.0 and matches the flat arcade rate.
// Short gears therefore pull harder than the flat rate and top gear pulls
// less, which is what makes a gear choice worth anything to the driver.
//
// The gear and RPM are last step values: UpdateArcadeAutomaticGear runs after
// the dynamics. At 60 Hz that is a step of lag on a signal that moves in
// tenths of a second.
float EngineDriveTorqueScale(const raceman::physics::VehicleConfig& config,
                             int gear,
                             float rpm,
                             float absSpeed,
                             float maxForwardSpeed,
                             float shiftCooldown) {
    const std::vector<float>& gearRatios = config.transmission.gearRatios;
    if (gearRatios.empty() || config.engine.torqueCurve.empty()) {
        return 1.0f;
    }

    float peakTorque = 0.0f;
    for (const raceman::physics::TorquePoint& point : config.engine.torqueCurve) {
        peakTorque = (std::max)(peakTorque, point.torque);
    }
    if (peakTorque <= 0.0f) {
        return 1.0f;
    }

    const int gearCount = static_cast<int>(gearRatios.size());
    const float finalDrive = std::fabs(config.transmission.finalDriveRatio);
    const int referenceGear = (std::clamp)((gearCount + 1) / 2, 1, gearCount);
    const float referenceEffort = std::fabs(gearRatios[referenceGear - 1]) * finalDrive * peakTorque;
    if (referenceEffort <= 0.0001f) {
        return 1.0f;
    }

    // Neutral and reverse have no meaningful band to sit in; leave them flat.
    if (gear <= 0) {
        return 1.0f;
    }
    const int clampedGear = (std::clamp)(gear, 1, gearCount);
    const float effort = std::fabs(gearRatios[clampedGear - 1]) * finalDrive *
        raceman::physics::sampleTorqueCurve(config.engine, rpm);

    float scale = effort / referenceEffort;

    // Speed past what the gear can turn is RPM past the redline. Drive dies
    // across a narrow band above it, so an unshifted gear is a wall the car
    // runs into rather than a ceiling it drifts through.
    const float topRatio = (std::max)(0.01f, std::fabs(gearRatios.back()));
    const float gearRatio = (std::max)(0.01f, std::fabs(gearRatios[clampedGear - 1]));
    const float gearTopSpeed = (std::max)(1.0f, maxForwardSpeed * topRatio / gearRatio);
    if (absSpeed > gearTopSpeed) {
        scale *= (std::clamp)(1.0f - (absSpeed / gearTopSpeed - 1.0f) / 0.06f, 0.0f, 1.0f);
    }

    if (shiftCooldown > 0.0f) {
        // Clutch is out. No drive reaches the road during the shift.
        scale *= 0.12f;
    }
    return (std::clamp)(scale, 0.0f, 2.5f);
}

namespace {

// A closed throttle is a brake, and how much of one depends entirely on what
// the engine is being turned by. Low gear at high RPM slows the car hard, top
// gear barely at all, and neutral not at all: the gearbox is disconnected and
// only the bearings are left. This is what makes a downshift into a corner
// worth taking.
float EngineBrakeDeceleration(const raceman::physics::VehicleConfig& config,
                              int gear,
                              float rpm,
                              const raceman::physics::VehicleArcadeHandlingConfig& handling) {
    const float coast = (std::max)(0.0f, handling.coastDeceleration);
    const std::vector<float>& gearRatios = config.transmission.gearRatios;
    if (gear == 0 || gearRatios.empty()) {
        return coast * 0.15f;
    }

    const int gearCount = static_cast<int>(gearRatios.size());
    const int clampedGear = (std::clamp)(std::abs(gear), 1, gearCount);
    const int referenceGear = (std::clamp)((gearCount + 1) / 2, 1, gearCount);
    const float ratio = (std::max)(0.01f, std::fabs(gearRatios[clampedGear - 1]));
    const float reference = (std::max)(0.01f, std::fabs(gearRatios[referenceGear - 1]));

    const float idleRpm = (std::max)(0.0f, config.engine.idleRPM);
    const float redlineRpm = (std::max)(idleRpm + 1.0f, config.engine.redlineRPM);
    const float rpmFraction = (std::clamp)((rpm - idleRpm) / (redlineRpm - idleRpm), 0.0f, 1.0f);

    return coast * (ratio / reference) * (0.35f + 0.85f * rpmFraction);
}

// Drag rises with the square of speed, so top speed stops being a number the
// car is clamped to and becomes one it runs out of breath at. The coefficient
// is derived rather than authored: at maxForwardSpeed in top gear the engine
// is making exactly enough to hold that speed, which is what a top speed is.
float AeroDragCoefficient(const raceman::physics::VehicleConfig& config,
                          const raceman::physics::VehicleArcadeHandlingConfig& handling) {
    const float maxSpeed = (std::max)(1.0f, handling.maxForwardSpeed);
    const int gearCount = (std::max)(1, static_cast<int>(config.transmission.gearRatios.size()));
    const float redlineRpm = (std::max)(config.engine.idleRPM + 1.0f, config.engine.redlineRPM);
    const float topGearThrust = EngineDriveTorqueScale(config, gearCount, redlineRpm, maxSpeed, maxSpeed, 0.0f);
    // Drive reaching the road is already scaled by longitudinal grip, so the
    // drag it has to balance must be too, or the car tops out short of the
    // speed it is authored to reach.
    const float gripScale = config.tireGrip.enabled ? (std::max)(0.05f, config.tireGrip.longitudinalGrip) : 1.0f;
    return (std::max)(0.0f, handling.acceleration) * topGearThrust * gripScale / (maxSpeed * maxSpeed);
}

// Where the axles sit relative to the centre of mass, and how far the front
// wheels can be turned. The yaw model needs real distances: a force at the
// front axle and the same force at the rear rotate the car opposite ways, and
// which one wins is a matter of centimetres.
struct VehicleAxleGeometry {
    float frontDistance{1.4f};
    float rearDistance{1.4f};
    float maxSteerAngle{0.5f};
};

VehicleAxleGeometry AxleGeometry(const raceman::physics::VehicleConfig& config) {
    VehicleAxleGeometry geometry;
    float frontSum = 0.0f;
    float rearSum = 0.0f;
    int frontCount = 0;
    int rearCount = 0;
    float steerMax = 0.0f;
    for (const raceman::physics::WheelConfig& wheel : config.wheels) {
        if (wheel.mountPosition.y >= 0.0f) {
            frontSum += wheel.mountPosition.y;
            ++frontCount;
            steerMax = (std::max)(steerMax, std::fabs(wheel.maxSteerAngle));
        } else {
            rearSum += -wheel.mountPosition.y;
            ++rearCount;
        }
    }
    if (frontCount > 0) {
        geometry.frontDistance = (std::max)(0.2f, frontSum / static_cast<float>(frontCount));
    }
    if (rearCount > 0) {
        geometry.rearDistance = (std::max)(0.2f, rearSum / static_cast<float>(rearCount));
    }
    if (steerMax > 0.01f) {
        geometry.maxSteerAngle = steerMax;
    }
    return geometry;
}

void ApplyArcadeDrivetrain(float& speed,
                           const ArcadeVehicleInput& input,
                           const VehicleControlAmounts& controls,
                           bool routeInput,
                           bool manualGearbox,
                           int gear,
                           float driveGripScale,
                           float driveTorqueScale,
                           float coastDeceleration,
                           float dragCoefficient,
                           float contactRollingDrag,
                           const raceman::physics::VehicleArcadeHandlingConfig& handling,
                           float deltaTime) {
    constexpr float kDriveIntentInputThreshold = 0.20f;
    const bool wantsForward = routeInput && controls.throttle > kDriveIntentInputThreshold;
    const bool wantsReverseOrBrake = routeInput && controls.brake > kDriveIntentInputThreshold;
    const float brakeDeceleration = (std::max)(0.0f, handling.brakeDeceleration);
    if (manualGearbox) {
        // With a gear lever the brake is only ever a brake, and which way the
        // car pulls is the driver's choice rather than a guess made from the
        // sign of the speed. Neutral drives nothing at all.
        if (wantsReverseOrBrake) {
            speed = MoveTowards(speed, 0.0f, brakeDeceleration * driveGripScale * controls.brake * deltaTime);
        } else if (wantsForward && gear > 0) {
            speed += controls.throttle * (std::max)(0.0f, handling.acceleration) * driveGripScale * driveTorqueScale * deltaTime;
        } else if (wantsForward && gear < 0) {
            speed -= controls.throttle * (std::max)(0.0f, handling.reverseAcceleration) * driveGripScale * driveTorqueScale * deltaTime;
        } else {
            speed = MoveTowards(speed, 0.0f, (std::max)(0.0f, coastDeceleration) * deltaTime);
        }
    } else if (wantsForward && wantsReverseOrBrake) {
        speed = MoveTowards(speed, 0.0f, brakeDeceleration * driveGripScale * controls.brake * deltaTime);
    } else if (wantsForward) {
        if (speed < -0.1f) {
            speed = MoveTowards(speed, 0.0f, brakeDeceleration * driveGripScale * controls.throttle * deltaTime);
        } else {
            speed += controls.throttle * (std::max)(0.0f, handling.acceleration) * driveGripScale * driveTorqueScale * deltaTime;
        }
    } else if (wantsReverseOrBrake) {
        if (speed > 0.75f) {
            speed = MoveTowards(speed, 0.0f, brakeDeceleration * driveGripScale * controls.brake * deltaTime);
        } else {
            speed -= controls.brake * (std::max)(0.0f, handling.reverseAcceleration) * driveGripScale * driveTorqueScale * deltaTime;
        }
    } else {
        speed = MoveTowards(speed, 0.0f, (std::max)(0.0f, coastDeceleration) * deltaTime);
    }

    if (input.handbrake > 0.0f) {
        speed = MoveTowards(speed, 0.0f, (std::max)(0.0f, handling.handbrakeDeceleration) * driveGripScale * input.handbrake * deltaTime);
    }
    if (dragCoefficient > 0.0f) {
        speed = MoveTowards(speed, 0.0f, dragCoefficient * speed * speed * deltaTime);
    }
    speed = MoveTowards(speed, 0.0f, contactRollingDrag * deltaTime);
    speed = (std::clamp)(speed, -(std::max)(0.0f, handling.maxReverseSpeed), (std::max)(1.0f, handling.maxForwardSpeed));
}

} // namespace

VehicleDriveRatios ComputeVehicleDriveRatios(const RuntimeVehicleInstance& runtimeVehicle, float rawThrottleAmount) {
    VehicleDriveRatios ratios{};
    int rearDriven = 0;
    int rearWheels = 0;
    int drivenWheels = 0;
    for (const raceman::physics::WheelConfig& wheel : runtimeVehicle.config.wheels) {
        drivenWheels += wheel.driven ? 1 : 0;
        if (wheel.mountPosition.y < 0.0f) {
            ++rearWheels;
            rearDriven += wheel.driven ? 1 : 0;
        }
    }
    ratios.rearDrivenRatio = rearWheels > 0 ? static_cast<float>(rearDriven) / static_cast<float>(rearWheels) : 1.0f;
    ratios.drivenRatio = runtimeVehicle.config.wheels.empty()
        ? 1.0f
        : static_cast<float>(drivenWheels) / static_cast<float>(runtimeVehicle.config.wheels.size());
    ratios.differentialLock = DifferentialLockForThrottle(runtimeVehicle.config.differential, rawThrottleAmount > 0.05f);
    return ratios;
}

VehicleControlAmounts ApplyVehicleDriverAids(RuntimeVehicleInstance& runtimeVehicle,
                                             float rawThrottleAmount,
                                             float rawBrakeAmount,
                                             float absSpeedBeforeDrive,
                                             const VehicleDriveRatios& driveRatios,
                                             float deltaTime) {
    const raceman::physics::VehicleBrakeAssistConfig& brakes = runtimeVehicle.config.brakes;
    const raceman::physics::VehicleTractionControlConfig& tractionControl = runtimeVehicle.config.tractionControl;
    const float tcSlip = (std::max)(0.0f, runtimeVehicle.arcadeRearSlip - (std::max)(0.01f, tractionControl.slipLimit));
    const float tcTargetCut = tractionControl.enabled && rawThrottleAmount > 0.05f && absSpeedBeforeDrive > 1.0f && driveRatios.drivenRatio > 0.0f
        ? (std::clamp)(tcSlip * (std::max)(0.0f, tractionControl.cutStrength) * driveRatios.drivenRatio * (1.0f + driveRatios.differentialLock * 0.35f), 0.0f, 1.0f)
        : 0.0f;
    const float tcRate = tcTargetCut > runtimeVehicle.arcadeTractionControlCut
        ? (std::max)(1.0f, tractionControl.cutStrength * 10.0f)
        : (std::max)(0.1f, tractionControl.recoveryRate);
    runtimeVehicle.arcadeTractionControlCut += (tcTargetCut - runtimeVehicle.arcadeTractionControlCut) * SmoothingAlpha(tcRate, deltaTime);
    runtimeVehicle.arcadeTractionControlCut = (std::clamp)(runtimeVehicle.arcadeTractionControlCut, 0.0f, 1.0f);

    const float minThrottleScale = (std::clamp)(tractionControl.minThrottleScale, 0.0f, 1.0f);
    VehicleControlAmounts amounts{};
    amounts.throttle = rawThrottleAmount * (1.0f - runtimeVehicle.arcadeTractionControlCut * (1.0f - minThrottleScale));
    amounts.brake = rawBrakeAmount * (std::max)(0.0f, brakes.maxBrakeForce);

    const float absSlipEstimate = (std::max)(
        runtimeVehicle.arcadeFrontSlip + std::fabs(runtimeVehicle.arcadeVelocitySlipAngle) / 65.0f,
        runtimeVehicle.arcadeRearSlip * 0.65f);
    const float absTargetScale = brakes.absEnabled && rawBrakeAmount > 0.05f && absSpeedBeforeDrive > 3.0f
        ? (1.0f - (std::clamp)((absSlipEstimate - (std::max)(0.01f, brakes.absSlipLimit)) * (std::max)(0.0f, brakes.absReleaseRate), 0.0f, 0.75f))
        : 1.0f;
    const float absRate = absTargetScale < runtimeVehicle.arcadeAbsBrakeScale
        ? (std::max)(0.1f, brakes.absReleaseRate)
        : (std::max)(0.1f, brakes.absRecoverRate);
    runtimeVehicle.arcadeAbsBrakeScale += (absTargetScale - runtimeVehicle.arcadeAbsBrakeScale) * SmoothingAlpha(absRate, deltaTime);
    runtimeVehicle.arcadeAbsBrakeScale = (std::clamp)(runtimeVehicle.arcadeAbsBrakeScale, 0.25f, 1.0f);
    amounts.brake *= runtimeVehicle.arcadeAbsBrakeScale;
    return amounts;
}

void ApplyArcadeVehicleDynamics(RuntimeVehicleInstance& runtimeVehicle,
                                const ArcadeVehicleInput& input,
                                const VehicleControlAmounts& controls,
                                const VehicleSurfaceSample& surfaceSample,
                                const VehicleDriveRatios& driveRatios,
                                bool routeInput,
                                float previousSpeed,
                                float previousThrottleInput,
                                float deltaTime) {
    float& speed = runtimeVehicle.arcadeSpeed;
    float& lateralSpeed = runtimeVehicle.arcadeLateralSpeed;
    const raceman::physics::VehicleArcadeHandlingConfig& arcadeHandling = runtimeVehicle.config.arcadeHandling;
    const raceman::physics::VehicleTireGripConfig& tireGrip = runtimeVehicle.config.tireGrip;
    const raceman::physics::VehicleTireDynamicsConfig& tireDynamics = runtimeVehicle.config.tireDynamics;
    const raceman::physics::VehicleLoadTransferConfig& loadTransfer = runtimeVehicle.config.loadTransfer;
    const raceman::physics::VehicleYawDynamicsConfig& yawDynamics = runtimeVehicle.config.yawDynamics;
    const raceman::physics::VehicleBrakeAssistConfig& brakes = runtimeVehicle.config.brakes;
    const float maxForwardSpeed = (std::max)(1.0f, arcadeHandling.maxForwardSpeed);
    const float fallbackSteerDegreesPerSecond = (std::max)(0.0f, arcadeHandling.fallbackSteerDegreesPerSecond);
    const float lowSpeedSteerSpeed = (std::max)(0.01f, arcadeHandling.lowSpeedSteerSpeed);
    const float lowSpeedSteerFloorConfig = (std::clamp)(arcadeHandling.lowSpeedSteerFloor, 0.0f, 1.0f);
    const float lowSpeedSteerInputBoost = (std::clamp)(arcadeHandling.lowSpeedSteerInputBoost, 0.0f, 1.0f);
    const float highSpeedSteerCut = (std::clamp)(arcadeHandling.highSpeedSteerCut, 0.0f, 0.95f);
    const float absSpeedBeforeDrive = std::fabs(previousSpeed);
    const float speedFactorBeforeDrive = (std::clamp)(absSpeedBeforeDrive / maxForwardSpeed, 0.0f, 1.0f);
    const float throttleAmount = controls.throttle;
    const float brakeAmount = controls.brake;
    const float rearDrivenRatio = driveRatios.rearDrivenRatio;
    const float diffLock = driveRatios.differentialLock;
    const float contactGripMultiplier = surfaceSample.gripMultiplier;
    const float contactRollingDrag = surfaceSample.rollingDrag;
    const float contactWheelGripFactor = surfaceSample.wheelGripFactor;

    const float handbrakeGripScale = 1.0f - input.handbrake * (1.0f - (std::clamp)(tireGrip.handbrakeGripScale, 0.0f, 1.0f));
    const float tireAeroBoost = speedFactorBeforeDrive * speedFactorBeforeDrive * (std::max)(0.0f, tireGrip.downforceGripScale);
    const float loadAeroBoost = loadTransfer.enabled
        ? (std::min)((std::max)(0.0f, loadTransfer.maxAeroGripBoost),
                     speedFactorBeforeDrive * speedFactorBeforeDrive * (std::max)(0.0f, loadTransfer.aeroDownforce))
        : 0.0f;
    runtimeVehicle.arcadeAeroGripBoost = loadAeroBoost;
    const float downforceGripScale = 1.0f + tireAeroBoost + loadAeroBoost;
    const float effectiveSurfaceGrip = tireGrip.enabled ? (std::max)(0.05f, contactGripMultiplier) : 1.0f;
    const float effectiveWheelGrip = tireGrip.enabled ? (std::clamp)(contactWheelGripFactor, 0.2f, 4.0f) : 1.0f;
    const float effectiveLateralGrip =
        (std::max)(0.0f, tireGrip.lateralGrip) * effectiveSurfaceGrip * effectiveWheelGrip * downforceGripScale * handbrakeGripScale;
    const glm::quat bodyYawRotationBefore = glm::angleAxis(glm::radians(runtimeVehicle.arcadeChassisWorld.rotationEuler.y), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 bodyForwardBefore = bodyYawRotationBefore * glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 bodyRightBefore = bodyYawRotationBefore * glm::vec3(1.0f, 0.0f, 0.0f);
    runtimeVehicle.arcadePlanarVelocity.y = 0.0f;
    if (glm::length(runtimeVehicle.arcadePlanarVelocity) < 0.05f && absSpeedBeforeDrive > 0.05f) {
        runtimeVehicle.arcadePlanarVelocity = bodyForwardBefore * speed;
    }
    float forwardVelocity = glm::dot(runtimeVehicle.arcadePlanarVelocity, bodyForwardBefore);
    float sideVelocity = glm::dot(runtimeVehicle.arcadePlanarVelocity, bodyRightBefore);
    lateralSpeed = sideVelocity;
    const float slipAngle = glm::degrees(std::atan2(sideVelocity, (std::max)(0.5f, std::fabs(forwardVelocity))));
    const float slipLimit = (std::max)(0.1f, tireGrip.slipAngleLimit);
    const float slipOverLimit = tireGrip.enabled
        ? (std::max)(0.0f, (std::fabs(slipAngle) - slipLimit) / slipLimit)
        : 0.0f;
    const float cornerDemand = tireGrip.enabled
        ? std::fabs(input.steering) * speedFactorBeforeDrive * speedFactorBeforeDrive
        : 0.0f;
    const float availableCornerGrip = tireGrip.enabled
        ? (std::max)(0.05f, effectiveLateralGrip / 5.0f)
        : 1.0f;
    const float cornerOverLimit = tireGrip.enabled
        ? (std::max)(0.0f, (cornerDemand - availableCornerGrip) / availableCornerGrip)
        : 0.0f;
    float tractionScale = 1.0f - (std::max)(slipOverLimit, cornerOverLimit) * (std::clamp)(tireGrip.slideGripLoss, 0.0f, 1.0f);
    tractionScale = (std::clamp)(tractionScale, (std::clamp)(tireGrip.minTractionScale, 0.0f, 1.0f), 1.0f);
    const float persistentSlipScale = 1.0f - (std::max)(runtimeVehicle.arcadeFrontSlip, runtimeVehicle.arcadeRearSlip) *
        (std::clamp)(tireGrip.slideGripLoss, 0.0f, 1.0f);
    tractionScale = (std::clamp)((std::min)(tractionScale, persistentSlipScale), (std::clamp)(tireGrip.minTractionScale, 0.0f, 1.0f), 1.0f);
    const float driveGripScale = tireGrip.enabled
        ? effectiveSurfaceGrip * (std::max)(0.0f, tireGrip.longitudinalGrip) * tractionScale
        : contactGripMultiplier;
    // Braking and the handbrake stay on grip alone; only the engine side is
    // gated by what the drivetrain can deliver in the gear it is in.
    const float driveTorqueScale = arcadeHandling.engineDrivenAcceleration
        ? EngineDriveTorqueScale(runtimeVehicle.config,
                                 runtimeVehicle.arcadeGear,
                                 runtimeVehicle.arcadeEngineRPM,
                                 absSpeedBeforeDrive,
                                 maxForwardSpeed,
                                 runtimeVehicle.autoShiftCooldown)
        : 1.0f;
    const bool manualGearbox =
        runtimeVehicle.config.transmission.mode == raceman::physics::TransmissionConfig::Mode::Manual;

    // ---------------------------------------------------------------------
    // Friction circle. A tyre has one budget, and stopping, driving and
    // turning all draw on it. What the driver spends on the brake pedal is
    // not there to corner with, which is the whole reason a corner has to be
    // arrived at slowed down instead of braked through.
    //
    // Utilisation is measured against what the tyre has at this moment, so
    // downforce genuinely buys braking: the same pedal that locks the fronts
    // at 30 m/s is comfortably inside the tyre at 55 m/s.
    // ---------------------------------------------------------------------
    const float brakeFrontShare = 0.5f + (std::clamp)(brakes.frontBias, 0.0f, 1.0f) * 0.5f;
    const float brakeRearShare = 1.5f - brakeFrontShare;
    float frontCircleScale = 1.0f;
    float rearCircleScale = 1.0f;
    float brakeLock = 0.0f;
    if (tireGrip.combinedSlip && tireGrip.enabled) {
        constexpr float kBrakeCircleGain = 1.15f;
        constexpr float kDriveCircleGain = 0.70f;
        const float longitudinalCapacity = (std::max)(0.05f, effectiveSurfaceGrip * downforceGripScale);
        const float frontBrakeUse = controls.brake * brakeFrontShare * kBrakeCircleGain / longitudinalCapacity;
        const float rearBrakeUse = controls.brake * brakeRearShare * kBrakeCircleGain / longitudinalCapacity;
        const float rearDriveUse =
            controls.throttle * driveTorqueScale * rearDrivenRatio * kDriveCircleGain / longitudinalCapacity;

        const float frontUse = (std::clamp)(frontBrakeUse, 0.0f, 1.0f);
        const float rearUse = (std::clamp)((std::max)(rearBrakeUse, rearDriveUse), 0.0f, 1.0f);
        frontCircleScale = std::sqrt((std::max)(0.0f, 1.0f - frontUse * frontUse));
        rearCircleScale = std::sqrt((std::max)(0.0f, 1.0f - rearUse * rearUse));

        // Asked for more than the tyre has and it stops rotating. A locked
        // wheel steers nothing and stops worse than one held at the limit,
        // so the pedal has to be released to get the car back.
        brakeLock = (std::clamp)((frontBrakeUse - 0.98f) / 0.10f, 0.0f, 1.0f);
    }

    // Engine braking and drag only exist for a car whose longitudinal axis is
    // modelled; everything else keeps the flat coast it was authored with.
    const float coastDeceleration = arcadeHandling.engineDrivenAcceleration
        ? EngineBrakeDeceleration(runtimeVehicle.config,
                                  runtimeVehicle.arcadeGear,
                                  runtimeVehicle.arcadeEngineRPM,
                                  arcadeHandling)
        : (std::max)(0.0f, arcadeHandling.coastDeceleration);
    const float dragCoefficient = arcadeHandling.engineDrivenAcceleration
        ? AeroDragCoefficient(runtimeVehicle.config, arcadeHandling)
        : 0.0f;

    // A locked tyre slides, and a sliding tyre stops the car less well than
    // one held just short of it. Only braking can reach a lock, so folding
    // this into the shared grip scale costs the throttle nothing.
    const float lockedGripScale = 1.0f - brakeLock * 0.25f;

    ApplyArcadeDrivetrain(speed,
                          input,
                          controls,
                          routeInput,
                          manualGearbox,
                          runtimeVehicle.arcadeGear,
                          driveGripScale * lockedGripScale,
                          driveTorqueScale,
                          coastDeceleration,
                          dragCoefficient,
                          contactRollingDrag,
                          arcadeHandling,
                          deltaTime);
    const float speedDelta = speed - previousSpeed;
    const float longitudinalLoad = loadTransfer.enabled
        ? (std::clamp)(speedDelta / ((std::max)(0.0001f, deltaTime) * 20.0f), -1.0f, 1.0f)
        : 0.0f;
    runtimeVehicle.arcadeLongitudinalLoad = longitudinalLoad;

    const float absSpeed = std::fabs(speed);
    const float steerAbs = std::fabs(input.steering);
    const float driveIntentForSteer = (std::max)(throttleAmount, brakeAmount);
    const float lowSpeedSteerFloor = steerAbs > 0.001f && (absSpeed > 0.10f || driveIntentForSteer > 0.05f)
        ? (lowSpeedSteerFloorConfig + (std::clamp)(driveIntentForSteer, 0.0f, 1.0f) * lowSpeedSteerInputBoost)
        : 0.0f;
    const float speedForSteer = (std::max)((std::clamp)(absSpeed / lowSpeedSteerSpeed, 0.0f, 1.0f), lowSpeedSteerFloor);
    const float highSpeedSteerScale = 1.0f - (std::clamp)(absSpeed / maxForwardSpeed, 0.0f, highSpeedSteerCut);
    // Locked fronts point wherever the car is already going.
    const float gripSteerScale =
        (std::clamp)(effectiveSurfaceGrip * tractionScale, 0.15f, 1.5f) * (1.0f - brakeLock * 0.85f);
    const float throttleLift = (std::max)(0.0f, previousThrottleInput - throttleAmount);
    const float rearDriveFactor = rearDrivenRatio;
    const float speedCornerDemand = steerAbs * speedFactorBeforeDrive * speedFactorBeforeDrive;
    const float overSpeedDemand = steerAbs *
        (std::clamp)((absSpeed - maxForwardSpeed * 0.55f) / (std::max)(1.0f, maxForwardSpeed * 0.45f), 0.0f, 1.0f);
    const float brakeFrontBias = (std::clamp)(brakes.frontBias, 0.0f, 1.0f);
    const float brakeRearBias = 1.0f - brakeFrontBias;
    const float brakeTurnDemand = brakeAmount * steerAbs * speedFactorBeforeDrive;
    const float frontBrakeDemand = brakeTurnDemand * (0.65f + brakeFrontBias * 0.70f);
    const float rearBrakeDemand = brakeTurnDemand * (0.45f + brakeRearBias * 1.10f);
    const float liftOffDemand = throttleLift * steerAbs * speedFactorBeforeDrive;
    const float throttleTurnDemand = throttleAmount * steerAbs * speedFactorBeforeDrive * rearDriveFactor * (1.0f + diffLock * 0.35f);
    const float handbrakeDemand = input.handbrake * (0.35f + 0.65f * speedFactorBeforeDrive);
    const float frontGripCapacity =
        (std::max)(0.05f, availableCornerGrip * (std::max)(0.05f, tireDynamics.frontGripBias) * frontCircleScale);
    float rearGripCapacity =
        (std::max)(0.05f, availableCornerGrip * (std::max)(0.05f, tireDynamics.rearGripBias) * rearCircleScale);
    const float rearGripLoss =
        liftOffDemand * (std::max)(0.0f, tireDynamics.liftOffRearGripLoss) +
        rearBrakeDemand * (std::max)(0.0f, tireDynamics.brakeRearGripLoss) +
        throttleTurnDemand * (std::max)(0.0f, tireDynamics.throttleRearGripLoss) * (1.0f + diffLock * 0.40f) +
        handbrakeDemand * (std::max)(0.0f, tireDynamics.handbrakeRearGripLoss) +
        overSpeedDemand * (std::max)(0.0f, tireDynamics.overSpeedGripLoss);
    rearGripCapacity *= (std::clamp)(1.0f - rearGripLoss, 0.08f, 1.0f);
    const float frontDemand = speedCornerDemand + overSpeedDemand * 0.45f + frontBrakeDemand * 0.30f;
    const float rearDemand = speedCornerDemand + overSpeedDemand * 0.65f + liftOffDemand + rearBrakeDemand + throttleTurnDemand + handbrakeDemand;
    const float targetFrontSlip = tireGrip.enabled
        ? (std::clamp)((frontDemand - frontGripCapacity) / (std::max)(0.05f, frontGripCapacity), 0.0f, 2.0f)
        : 0.0f;
    const float targetRearSlip = tireGrip.enabled
        ? (std::clamp)((rearDemand - rearGripCapacity) / (std::max)(0.05f, rearGripCapacity), 0.0f, 2.0f)
        : 0.0f;
    const float frontSlipRate = targetFrontSlip > runtimeVehicle.arcadeFrontSlip
        ? (std::max)(0.0f, tireDynamics.lateralRelaxationRate)
        : (std::max)(0.0f, tireDynamics.gripRecoveryRate);
    const float rearSlipRate = targetRearSlip > runtimeVehicle.arcadeRearSlip
        ? (std::max)(0.0f, tireDynamics.lateralRelaxationRate)
        : (std::max)(0.0f, tireDynamics.gripRecoveryRate);
    runtimeVehicle.arcadeFrontSlip += (targetFrontSlip - runtimeVehicle.arcadeFrontSlip) * SmoothingAlpha(frontSlipRate, deltaTime);
    runtimeVehicle.arcadeRearSlip += (targetRearSlip - runtimeVehicle.arcadeRearSlip) * SmoothingAlpha(rearSlipRate, deltaTime);
    runtimeVehicle.arcadeFrontSlip = (std::clamp)(runtimeVehicle.arcadeFrontSlip, 0.0f, 2.0f);
    runtimeVehicle.arcadeRearSlip = (std::clamp)(runtimeVehicle.arcadeRearSlip, 0.0f, 2.0f);
    runtimeVehicle.arcadeGripBalance = runtimeVehicle.arcadeRearSlip - runtimeVehicle.arcadeFrontSlip;

    const float slideSign = std::fabs(input.steering) > 0.001f
        ? input.steering
        : (sideVelocity > 0.0f ? 1.0f : (sideVelocity < 0.0f ? -1.0f : 0.0f));
    const float sideSlipDemand = (std::clamp)(runtimeVehicle.arcadeRearSlip * 0.70f + runtimeVehicle.arcadeFrontSlip * 0.25f, 0.0f, 1.0f);
    const float maxSideSlipSpeed = (std::max)(absSpeed, glm::length(runtimeVehicle.arcadePlanarVelocity)) *
        (std::clamp)(tireDynamics.maxSideSlipSpeedScale, 0.0f, 1.0f);
    const float baseVelocityAlignmentRate = (std::max)(0.0f, tireDynamics.velocityAlignmentRate);
    const float gripControlResponse = (1.0f - sideSlipDemand) *
        (1.0f + tractionScale * 1.75f + throttleAmount * 0.85f + brakeAmount * 2.25f);
    const float velocityAlignRate = baseVelocityAlignmentRate * (1.0f + (std::max)(0.0f, gripControlResponse));
    const float velocityAlignAlpha = SmoothingAlpha(velocityAlignRate, deltaTime);
    forwardVelocity += (speed - forwardVelocity) * velocityAlignAlpha;
    sideVelocity = glm::dot(runtimeVehicle.arcadePlanarVelocity, bodyRightBefore);
    const float sideSlipAcceleration = slideSign *
        sideSlipDemand *
        (std::max)(0.0f, tireDynamics.lateralRelaxationRate) *
        (std::max)(1.0f, absSpeed * 0.65f);
    sideVelocity += sideSlipAcceleration * deltaTime;
    const float scrubStrength = (std::max)(0.0f, tireDynamics.tireScrub) *
        (0.45f + (std::clamp)(sideSlipDemand, 0.0f, 1.0f) * 0.75f) *
        (std::max)(0.25f, effectiveSurfaceGrip);
    runtimeVehicle.arcadeTireScrub = scrubStrength;
    const float slideFriction = (std::max)(0.0f, tireDynamics.slideFriction) *
        (0.40f + (std::clamp)(sideSlipDemand, 0.0f, 1.0f) * 0.85f);
    const float sideVelocityRatio = (std::clamp)(std::fabs(sideVelocity) / (std::max)(1.0f, std::fabs(forwardVelocity)), 0.0f, 1.5f);
    const float slipEnergyDemand = (std::clamp)(
        (std::max)(sideSlipDemand, (std::max)(runtimeVehicle.arcadeFrontSlip, runtimeVehicle.arcadeRearSlip) * 0.55f) +
         sideVelocityRatio * 0.50f,
        0.0f,
        1.5f);
    const float lateralScrubDecel =
        (scrubStrength + slideFriction) *
        (std::max)(1.0f, absSpeed * 0.14f) *
        (1.0f + slipEnergyDemand * 0.35f);
    sideVelocity = MoveTowards(sideVelocity, 0.0f, lateralScrubDecel * deltaTime);
    sideVelocity = (std::clamp)(sideVelocity, -maxSideSlipSpeed, maxSideSlipSpeed);
    if (slipEnergyDemand > 0.001f) {
        const float planarSpeed = glm::length(runtimeVehicle.arcadePlanarVelocity);
        const float tireHeatDrag =
            (slideFriction * 0.55f + scrubStrength * 0.28f) *
            slipEnergyDemand *
            (std::max)(1.0f, planarSpeed * 0.10f) *
            deltaTime;
        forwardVelocity = MoveTowards(forwardVelocity, 0.0f, tireHeatDrag * 0.85f);
        speed = MoveTowards(speed, 0.0f, tireHeatDrag * 0.75f);
    }
    runtimeVehicle.arcadePlanarVelocity = bodyForwardBefore * forwardVelocity + bodyRightBefore * sideVelocity;
    if (std::fabs(speed) < 0.05f && glm::length(runtimeVehicle.arcadePlanarVelocity) < 0.05f) {
        runtimeVehicle.arcadePlanarVelocity = glm::vec3(0.0f);
        sideVelocity = 0.0f;
    }
    lateralSpeed = sideVelocity;
    runtimeVehicle.arcadeSideSlipVelocity = lateralSpeed;

    runtimeVehicle.arcadeVelocitySlipAngle = glm::degrees(std::atan2(sideVelocity, (std::max)(0.5f, std::fabs(forwardVelocity))));
    runtimeVehicle.arcadeSlipAngle = runtimeVehicle.arcadeVelocitySlipAngle;
    const float persistentSlipLoss = (std::max)(runtimeVehicle.arcadeFrontSlip, runtimeVehicle.arcadeRearSlip) *
        (std::clamp)(tireGrip.slideGripLoss, 0.0f, 1.0f);
    tractionScale = (std::clamp)((std::min)(tractionScale, 1.0f - persistentSlipLoss),
                                 (std::clamp)(tireGrip.minTractionScale, 0.0f, 1.0f),
                                 1.0f);
    runtimeVehicle.arcadeTractionScale = tractionScale;
    runtimeVehicle.arcadeSurfaceGrip = effectiveSurfaceGrip;
    const float directionSign = speed < -0.1f ? -1.0f : 1.0f;
    if (yawDynamics.enabled) {
        const float minYawSpeed = (std::max)(0.0f, yawDynamics.minSpeed);
        const float yawSpeedGate = minYawSpeed > 0.0f
            ? (std::clamp)((absSpeed - minYawSpeed) / (std::max)(1.0f, minYawSpeed), 0.0f, 1.0f)
            : 1.0f;
        const float maxYawRate = (std::max)(1.0f, yawDynamics.maxYawRate);
        const float slipAbs = std::fabs(runtimeVehicle.arcadeSlipAngle);
        const float spinSlipAngle = (std::max)(slipLimit + 0.1f, yawDynamics.spinSlipAngle);
        const float rearOverFrontSlip = (std::max)(0.0f, runtimeVehicle.arcadeRearSlip - runtimeVehicle.arcadeFrontSlip * 0.45f);
        const float understeerAmount = yawSpeedGate * (std::clamp)(runtimeVehicle.arcadeFrontSlip, 0.0f, 1.0f);
        const float oversteerAmount = yawSpeedGate * (std::clamp)(rearOverFrontSlip, 0.0f, 1.0f);
        const bool counterSteering = runtimeVehicle.arcadeVelocitySlipAngle * input.steering < -0.01f;
        const float spinTarget = yawSpeedGate * (std::max)(
            (std::clamp)((slipAbs - spinSlipAngle) / (std::max)(1.0f, spinSlipAngle), 0.0f, 1.0f),
            (std::clamp)((runtimeVehicle.arcadeRearSlip - 0.85f) / 0.75f, 0.0f, 1.0f));
        const float spinRate = spinTarget > runtimeVehicle.arcadeSpinAmount
            ? (std::max)(1.0f, yawDynamics.spinYawBoost * 4.0f)
            : (std::max)(0.1f, yawDynamics.spinRecovery);
        const float spinAlpha = SmoothingAlpha(spinRate, deltaTime);
        runtimeVehicle.arcadeSpinAmount += (spinTarget - runtimeVehicle.arcadeSpinAmount) * spinAlpha;
        runtimeVehicle.arcadeOversteerAmount = oversteerAmount;

        // -----------------------------------------------------------------
        // Where rotation comes from.
        //
        // The kinematic path below turns steering input straight into yaw
        // torque. It is responsive and completely unphysical: the car rotates
        // because it was asked to, so a slide can be steered out of with any
        // amount of lock in the right direction and never with too much.
        //
        // The physical path builds the same torque out of the two axles. Each
        // makes a lateral force set by its own slip angle - the angle between
        // where it points and where it is actually travelling - and that force
        // at its distance from the centre of mass is a yaw moment. Nothing
        // about a slide is scripted after that:
        //
        //   - the rear steps out when its force saturates, which the friction
        //     circle above already decides from throttle, brake and load;
        //   - countersteering works because winding lock off cuts the front
        //     slip angle and eventually reverses the front force;
        //   - too little lock will not arrest the rotation, and too much
        //     reverses the force hard enough to throw the car the other way.
        // -----------------------------------------------------------------
        float steeringTorque = 0.0f;
        if (yawDynamics.physicalYaw) {
            const VehicleAxleGeometry axles = AxleGeometry(runtimeVehicle.config);
            const float wheelbase = (std::max)(0.5f, axles.frontDistance + axles.rearDistance);
            const float yawRateRad = glm::radians(runtimeVehicle.arcadeYawRate);

            // Below this the slip-angle arithmetic divides by nearly nothing and
            // the model stops meaning anything; the kinematic term takes over.
            const float referenceSpeed = (std::max)(2.0f, std::fabs(forwardVelocity));
            // Angles are measured the way the chassis basis already is: positive
            // is to the right, which is the direction a positive yaw rate turns
            // and the direction sideVelocity is read in. Positive steering input
            // turns left in this codebase, so the wheel angle flips sign here.
            const float steerAngle = -input.steering * axles.maxSteerAngle * highSpeedSteerScale;

            // What each axle is actually doing, including what the car's own
            // rotation adds at that end of it: ahead of the centre of mass the
            // yaw term adds to the sideways velocity, behind it subtracts.
            const float frontTravelAngle =
                std::atan2(sideVelocity + yawRateRad * axles.frontDistance, referenceSpeed);
            const float rearTravelAngle =
                std::atan2(sideVelocity - yawRateRad * axles.rearDistance, referenceSpeed);
            const float frontSlipAngle = steerAngle - frontTravelAngle;
            const float rearSlipAngle = -rearTravelAngle;

            // Force rises with slip angle up to the peak and then stops: past
            // it the tyre is sliding, and sliding harder buys nothing. The
            // ceiling is the axle capacity the friction circle already worked
            // out, so weight transfer and throttle reach the yaw model here.
            const float peakSlipAngle = (std::max)(glm::radians(2.0f), glm::radians(slipLimit));
            const float frontForce =
                (std::clamp)(frontSlipAngle / peakSlipAngle, -1.0f, 1.0f) * frontGripCapacity;
            const float rearForce =
                (std::clamp)(rearSlipAngle / peakSlipAngle, -1.0f, 1.0f) * rearGripCapacity;

            // A rightward force at the front turns the car right; the same force
            // at the rear turns it left. Positive moment is therefore already a
            // positive yaw rate and needs no sign flip.
            const float yawMoment = (frontForce * axles.frontDistance - rearForce * axles.rearDistance) / wheelbase;
            // No gripSteerScale here: frontGripCapacity/rearGripCapacity already
            // carry every grip loss this car has (friction circle brake/drive
            // use, throttle/lift-off/handbrake weight transfer). gripSteerScale
            // is the *same* persistent-slip signal applied a second time on top
            // of forces that already collapsed from it - and since yawDragTorque
            // below is not grip-scaled at all, that double dip crushes the tyre
            // torque faster than drag, so a slide snapped straight on its own
            // instead of needing to be caught. The rotation this car gets is
            // exactly what the two axle forces earn, once.
            steeringTorque = yawMoment *
                (std::max)(0.0f, yawDynamics.steeringYawResponse) *
                directionSign;
        } else {
            steeringTorque = -input.steering *
                (std::max)(0.0f, yawDynamics.steeringYawResponse) *
                speedForSteer *
                highSpeedSteerScale *
                gripSteerScale *
                (1.0f - understeerAmount * 0.75f) *
                directionSign;
        }
        const float slipSign = runtimeVehicle.arcadeVelocitySlipAngle > 0.0f ? 1.0f : (runtimeVehicle.arcadeVelocitySlipAngle < 0.0f ? -1.0f : 0.0f);
        const float rearSlipBoost = 1.0f +
            input.handbrake * (std::max)(0.0f, yawDynamics.handbrakeRearSlipBoost) +
            throttleAmount * (1.0f - tractionScale) * (std::max)(0.0f, yawDynamics.throttleRearSlipBoost);
        const float spinYawScale = 1.0f + runtimeVehicle.arcadeSpinAmount * (std::max)(0.0f, yawDynamics.spinYawBoost);
        const float yawSlipSign = std::fabs(input.steering) > 0.001f ? input.steering : slipSign;
        const float slipYawResponseScale = (std::max)(0.0f, yawDynamics.slipYawResponse) / 45.0f;
        const float rearSlipYawResponse = (std::max)(0.0f, tireDynamics.yawFromRearSlip) * slipYawResponseScale;
        const float frontSlipYawResponse = (std::max)(0.0f, tireDynamics.yawFromFrontSlip) * slipYawResponseScale;
        const float slipYawTorque = -yawSlipSign *
            rearSlipYawResponse *
            oversteerAmount *
            rearSlipBoost *
            spinYawScale *
            (std::max)(0.0f, tireDynamics.rearSlipYawTorque);
        const float frontPushTorque = input.steering *
            frontSlipYawResponse *
            understeerAmount *
            0.35f;
        const float sideSlipYawTorque = -yawSlipSign *
            rearSlipYawResponse *
            oversteerAmount *
            (std::clamp)(std::fabs(lateralSpeed) / (std::max)(1.0f, absSpeed), 0.0f, 1.0f) *
            (std::max)(0.0f, yawDynamics.sideSlipToYaw);
        const float brakeInstabilityTorque = -yawSlipSign *
            brakeTurnDemand *
            (std::max)(0.0f, tireDynamics.brakeYawInstability) *
            rearSlipYawResponse;
        const float loadMemoryTorque = -yawSlipSign *
            (std::max)(0.0f, tireDynamics.loadMemory) *
            (std::max)(0.0f, -runtimeVehicle.arcadeLongitudinalLoad) *
            steerAbs *
            speedFactorBeforeDrive *
            rearSlipYawResponse;
        const float counterSteerTorque = counterSteering
            ? -runtimeVehicle.arcadeYawRate * (std::max)(0.0f, tireDynamics.counterSteerTorque)
            : 0.0f;
        const float yawDampingScale = (std::max)(0.0f, yawDynamics.yawDamping) / 5.5f;
        const float yawDragTorque = -runtimeVehicle.arcadeYawRate *
            ((std::max)(0.0f, tireDynamics.yawDrag) * yawDampingScale +
             understeerAmount * (std::max)(0.0f, tireDynamics.frontSlipYawDamping));
        // With the tyres deciding, every scripted rotation term is already
        // accounted for by an axle force and adding them would be counting the
        // same physics twice. The countersteer assist in particular has to go:
        // catching the car is the whole skill being modelled, and a torque that
        // does it for free is the reason it felt like nothing.
        const float yawTorque = yawDynamics.physicalYaw
            ? steeringTorque + yawDragTorque
            : steeringTorque + slipYawTorque + sideSlipYawTorque + frontPushTorque +
                  brakeInstabilityTorque + loadMemoryTorque + counterSteerTorque + yawDragTorque;
        runtimeVehicle.arcadeYawTorque = yawTorque;
        const float yawAcceleration = yawTorque / (std::max)(0.1f, tireDynamics.yawInertiaScale);
        runtimeVehicle.arcadeYawRate += yawAcceleration * deltaTime;

        // Parking speeds. The tyres are rolling rather than slipping down here,
        // so yaw is pure geometry - the classic v*tan(steer)/wheelbase - and the
        // slip-angle model has nothing to work with. Blend to it as speed falls
        // so the car still turns into a pit box.
        if (yawDynamics.physicalYaw) {
            const VehicleAxleGeometry axles = AxleGeometry(runtimeVehicle.config);
            const float wheelbase = (std::max)(0.5f, axles.frontDistance + axles.rearDistance);
            const float blendSpeed = (std::max)(1.0f, minYawSpeed * 1.5f);
            const float kinematicBlend = 1.0f - (std::clamp)(absSpeed / blendSpeed, 0.0f, 1.0f);
            if (kinematicBlend > 0.0f) {
                const float steerAngle = input.steering * axles.maxSteerAngle;
                const float kinematicYawRate =
                    -glm::degrees(forwardVelocity * std::tan(steerAngle) / wheelbase) * directionSign;
                runtimeVehicle.arcadeYawRate +=
                    (kinematicYawRate - runtimeVehicle.arcadeYawRate) * kinematicBlend * (std::clamp)(deltaTime * 8.0f, 0.0f, 1.0f);
            }
        }

        runtimeVehicle.arcadeYawRate = (std::clamp)(runtimeVehicle.arcadeYawRate, -maxYawRate, maxYawRate);
        runtimeVehicle.arcadeChassisWorld.rotationEuler.y += runtimeVehicle.arcadeYawRate * deltaTime;

        if (oversteerAmount > 0.0f && std::fabs(lateralSpeed) > 0.001f) {
            const float sideSlipBleed = (std::clamp)((std::max)(0.0f, yawDynamics.sideSlipToYaw) * oversteerAmount * deltaTime * 2.0f, 0.0f, 0.35f);
            lateralSpeed *= 1.0f - sideSlipBleed;
            runtimeVehicle.arcadePlanarVelocity = bodyForwardBefore * forwardVelocity + bodyRightBefore * lateralSpeed;
        }
    } else {
        runtimeVehicle.arcadeChassisWorld.rotationEuler.y -=
            input.steering * fallbackSteerDegreesPerSecond * speedForSteer * highSpeedSteerScale * gripSteerScale * directionSign * deltaTime;
        // A chassis impact injects spin even when the profile's yaw dynamics are off,
        // so let it play out and decay rather than hard-zeroing it here.
        if (std::fabs(runtimeVehicle.arcadeYawRate) > 0.01f) {
            runtimeVehicle.arcadeChassisWorld.rotationEuler.y += runtimeVehicle.arcadeYawRate * deltaTime;
            runtimeVehicle.arcadeYawRate *= (std::clamp)(1.0f - deltaTime * 3.0f, 0.0f, 1.0f);
        } else {
            runtimeVehicle.arcadeYawRate = 0.0f;
        }
        runtimeVehicle.arcadeYawTorque = 0.0f;
        runtimeVehicle.arcadeOversteerAmount = 0.0f;
        runtimeVehicle.arcadeSpinAmount = 0.0f;
    }
}

} // namespace raceman

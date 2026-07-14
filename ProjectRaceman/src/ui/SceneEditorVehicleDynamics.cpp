#include "SceneEditorVehicleDynamics.h"

#include <algorithm>
#include <cmath>

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

void ApplyArcadeDrivetrain(float& speed,
                           const ArcadeVehicleInput& input,
                           const VehicleControlAmounts& controls,
                           bool routeInput,
                           float driveGripScale,
                           float contactRollingDrag,
                           const raceman::physics::VehicleArcadeHandlingConfig& handling,
                           float deltaTime) {
    constexpr float kDriveIntentInputThreshold = 0.20f;
    const bool wantsForward = routeInput && controls.throttle > kDriveIntentInputThreshold;
    const bool wantsReverseOrBrake = routeInput && controls.brake > kDriveIntentInputThreshold;
    const float brakeDeceleration = (std::max)(0.0f, handling.brakeDeceleration);
    if (wantsForward && wantsReverseOrBrake) {
        speed = MoveTowards(speed, 0.0f, brakeDeceleration * driveGripScale * controls.brake * deltaTime);
    } else if (wantsForward) {
        if (speed < -0.1f) {
            speed = MoveTowards(speed, 0.0f, brakeDeceleration * driveGripScale * controls.throttle * deltaTime);
        } else {
            speed += controls.throttle * (std::max)(0.0f, handling.acceleration) * driveGripScale * deltaTime;
        }
    } else if (wantsReverseOrBrake) {
        if (speed > 0.75f) {
            speed = MoveTowards(speed, 0.0f, brakeDeceleration * driveGripScale * controls.brake * deltaTime);
        } else {
            speed -= controls.brake * (std::max)(0.0f, handling.reverseAcceleration) * driveGripScale * deltaTime;
        }
    } else {
        speed = MoveTowards(speed, 0.0f, (std::max)(0.0f, handling.coastDeceleration) * deltaTime);
    }

    if (input.handbrake > 0.0f) {
        speed = MoveTowards(speed, 0.0f, (std::max)(0.0f, handling.handbrakeDeceleration) * driveGripScale * input.handbrake * deltaTime);
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
    ApplyArcadeDrivetrain(speed, input, controls, routeInput, driveGripScale, contactRollingDrag, arcadeHandling, deltaTime);
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
    const float gripSteerScale = (std::clamp)(effectiveSurfaceGrip * tractionScale, 0.15f, 1.5f);
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
    const float frontGripCapacity = (std::max)(0.05f, availableCornerGrip * (std::max)(0.05f, tireDynamics.frontGripBias));
    float rearGripCapacity = (std::max)(0.05f, availableCornerGrip * (std::max)(0.05f, tireDynamics.rearGripBias));
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

        const float steeringTorque = -input.steering *
            (std::max)(0.0f, yawDynamics.steeringYawResponse) *
            speedForSteer *
            highSpeedSteerScale *
            gripSteerScale *
            (1.0f - understeerAmount * 0.75f) *
            directionSign;
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
        const float yawTorque = steeringTorque + slipYawTorque + sideSlipYawTorque + frontPushTorque +
            brakeInstabilityTorque + loadMemoryTorque + counterSteerTorque + yawDragTorque;
        runtimeVehicle.arcadeYawTorque = yawTorque;
        const float yawAcceleration = yawTorque / (std::max)(0.1f, tireDynamics.yawInertiaScale);
        runtimeVehicle.arcadeYawRate += yawAcceleration * deltaTime;
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
        runtimeVehicle.arcadeYawRate = 0.0f;
        runtimeVehicle.arcadeYawTorque = 0.0f;
        runtimeVehicle.arcadeOversteerAmount = 0.0f;
        runtimeVehicle.arcadeSpinAmount = 0.0f;
    }
}

} // namespace raceman

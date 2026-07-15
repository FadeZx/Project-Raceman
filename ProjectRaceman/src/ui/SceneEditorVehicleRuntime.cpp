#include "SceneEditorInternal.h"
#include "SceneEditorVehicleDynamics.h"
#include "SceneEditorVehicleGrounding.h"
#include "SceneEditorVehicleInput.h"
#include "SceneEditorVehicleTelemetry.h"
#include "SceneEditorVehicleVisuals.h"
#include "../input/InputManager.h"
#include "../physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace raceman {
using namespace scene_editor_internal;

namespace {

constexpr float kRuntimeFixedStep = 1.0f / 60.0f;

std::unordered_set<std::string> BuildVehicleRaycastIgnoreSet(const SceneObject& vehicleObject,
                                                             const std::string& chassisBodyObjectId) {
    std::unordered_set<std::string> ignored;
    ignored.insert(vehicleObject.id);
    if (!chassisBodyObjectId.empty()) {
        ignored.insert(chassisBodyObjectId);
    }
    for (const std::string& chassisObjectId : vehicleObject.vehicle.chassisObjectIds) {
        if (!chassisObjectId.empty()) {
            ignored.insert(chassisObjectId);
        }
    }
    for (const VehicleWheelBinding& binding : vehicleObject.vehicle.wheelBindings) {
        if (!binding.objectId.empty()) {
            ignored.insert(binding.objectId);
        }
    }
    return ignored;
}

VehicleSurfaceSample SampleVehicleSurface(const RuntimeVehicleInstance& runtimeVehicle,
                                          const ColliderSurfaceConfig& defaultSurface) {
    float contactGripMultiplier = 0.0f;
    float contactRollingDrag = 0.0f;
    float contactWheelGripFactor = 0.0f;
    int surfaceContactCount = 0;
    for (std::size_t contactIndex = 0; contactIndex < runtimeVehicle.arcadeWheelContacts.size(); ++contactIndex) {
        const RuntimeVehicleInstance::WheelContact& contact = runtimeVehicle.arcadeWheelContacts[contactIndex];
        if (!contact.grounded) {
            continue;
        }
        contactGripMultiplier += contact.surfaceGripMultiplier;
        contactRollingDrag += contact.surfaceRollingDrag;
        if (contactIndex < runtimeVehicle.config.wheels.size()) {
            const raceman::physics::ResolvedWheelTireConfig tire =
                raceman::physics::resolveWheelTire(runtimeVehicle.config, runtimeVehicle.config.wheels[contactIndex]);
            contactWheelGripFactor += (std::max)(0.1f, tire.gripFactor);
        } else {
            contactWheelGripFactor += 1.0f;
        }
        ++surfaceContactCount;
    }

    VehicleSurfaceSample sample{};
    if (surfaceContactCount > 0) {
        const float count = static_cast<float>(surfaceContactCount);
        sample.gripMultiplier = (std::max)(0.0f, contactGripMultiplier / count);
        sample.rollingDrag = (std::max)(0.0f, contactRollingDrag / count);
        sample.wheelGripFactor = (std::max)(0.1f, contactWheelGripFactor / count);
    } else {
        sample.gripMultiplier = (std::max)(0.0f, defaultSurface.gripMultiplier);
        sample.rollingDrag = (std::max)(0.0f, defaultSurface.rollingDrag);
        sample.wheelGripFactor = 1.0f;
    }
    return sample;
}

float GearRatioAbs(const raceman::physics::VehicleConfig& config, int gear) {
    if (config.transmission.gearRatios.empty()) {
        return 1.0f;
    }
    const int index = (std::clamp)(gear - 1, 0, static_cast<int>(config.transmission.gearRatios.size()) - 1);
    return (std::max)(0.01f, std::fabs(config.transmission.gearRatios[static_cast<std::size_t>(index)]));
}

float TopGearRatioAbs(const raceman::physics::VehicleConfig& config) {
    if (config.transmission.gearRatios.empty()) {
        return 1.0f;
    }
    return (std::max)(0.01f, std::fabs(config.transmission.gearRatios.back()));
}

float GearTopSpeed(const raceman::physics::VehicleConfig& config, int gear, float maxForwardSpeed) {
    return (std::max)(1.0f, maxForwardSpeed * TopGearRatioAbs(config) / GearRatioAbs(config, gear));
}

float GearSpeedToRpm(const raceman::physics::VehicleConfig& config,
                     float absSpeed,
                     int gear,
                     float idleRPM,
                     float redlineRPM,
                     float maxForwardSpeed) {
    if (gear <= 0) {
        return idleRPM;
    }
    const float gearTopSpeed = GearTopSpeed(config, gear, maxForwardSpeed);
    const float gearFraction = (std::clamp)(absSpeed / gearTopSpeed, 0.0f, 1.15f);
    return (std::clamp)(idleRPM + gearFraction * (redlineRPM - idleRPM), idleRPM, redlineRPM);
}

float GearShiftCooldown(const raceman::physics::VehicleConfig& config, int fromGear, int gearCount) {
    const float baseShiftTime = (std::max)(0.02f, config.transmission.shiftTime);
    const float gearT = gearCount > 1
        ? (std::clamp)(static_cast<float>((std::max)(1, fromGear) - 1) / static_cast<float>(gearCount - 1), 0.0f, 1.0f)
        : 0.0f;
    return baseShiftTime * (0.55f + 0.45f * gearT);
}

void UpdateArcadeAutomaticGear(RuntimeVehicleInstance& runtimeVehicle,
                               float absSpeed,
                               float signedSpeed,
                               float throttleAmount,
                               float brakeAmount,
                               float idleRPM,
                               float redlineRPM,
                               float maxForwardSpeed,
                               float deltaTime) {
    const int gearCount = (std::max)(1, static_cast<int>(runtimeVehicle.config.transmission.gearRatios.size()));
    runtimeVehicle.autoShiftCooldown = (std::max)(0.0f, runtimeVehicle.autoShiftCooldown - deltaTime);

    if (signedSpeed < -0.5f) {
        runtimeVehicle.arcadeGear = -1;
        runtimeVehicle.arcadeEngineRPM = idleRPM;
        return;
    }

    if (absSpeed < 0.2f && throttleAmount < 0.05f && brakeAmount < 0.05f) {
        runtimeVehicle.arcadeGear = 0;
        runtimeVehicle.arcadeEngineRPM = idleRPM;
        return;
    }

    if (runtimeVehicle.arcadeGear <= 0) {
        runtimeVehicle.arcadeGear = 1;
    }

    runtimeVehicle.arcadeGear = (std::clamp)(runtimeVehicle.arcadeGear, 1, gearCount);
    const int currentGear = runtimeVehicle.arcadeGear;
    const float rpm = GearSpeedToRpm(runtimeVehicle.config, absSpeed, currentGear, idleRPM, redlineRPM, maxForwardSpeed);
    const float gearT = gearCount > 1
        ? static_cast<float>(currentGear - 1) / static_cast<float>(gearCount - 1)
        : 0.0f;
    const float upshiftRpm = idleRPM + (redlineRPM - idleRPM) * (0.68f + 0.16f * gearT);
    const float downshiftRpm = idleRPM + (redlineRPM - idleRPM) * (0.34f + 0.07f * gearT);

    if (runtimeVehicle.autoShiftCooldown <= 0.0f) {
        if (currentGear < gearCount && throttleAmount > 0.03f && rpm >= upshiftRpm) {
            runtimeVehicle.arcadeGear = currentGear + 1;
            runtimeVehicle.autoShiftCooldown = GearShiftCooldown(runtimeVehicle.config, currentGear, gearCount);
        } else if (currentGear > 1 && rpm <= downshiftRpm && throttleAmount < 0.85f) {
            runtimeVehicle.arcadeGear = currentGear - 1;
            runtimeVehicle.autoShiftCooldown = GearShiftCooldown(runtimeVehicle.config, currentGear - 1, gearCount) * 0.85f;
        }
    }

    const float targetRpm = GearSpeedToRpm(runtimeVehicle.config, absSpeed, runtimeVehicle.arcadeGear, idleRPM, redlineRPM, maxForwardSpeed);
    const float shiftBlend = runtimeVehicle.autoShiftCooldown > 0.0f ? 0.45f : 1.0f;
    runtimeVehicle.arcadeEngineRPM = (std::clamp)(
        runtimeVehicle.arcadeEngineRPM + (targetRpm - runtimeVehicle.arcadeEngineRPM) * shiftBlend,
        idleRPM,
        redlineRPM);
}

} // namespace

void SceneEditor::UpdateVehiclePhysics(float deltaTime) {
    if (!scriptsRunning_ || scriptsPaused_ || deltaTime <= 0.0f) {
        ClearVehicleForceFeedback(inputManager_);
        return;
    }

    if (runtimeVehicles_.empty()) {
        ClearVehicleForceFeedback(inputManager_);
        return;
    }

    const bool wheelFfbAllowed = inputManager_ != nullptr && ShouldRouteInputToGame();
    if (inputManager_ != nullptr) {
        inputManager_->SetWheelForceFeedbackActive(wheelFfbAllowed);
    }

    float strongestTorque = 0.0f;
    float strongestDamper = 0.0f;
    float strongestVibration = 0.0f;

    for (RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles_) {
        if (runtimeVehicle.objectIndex < 0 || runtimeVehicle.objectIndex >= static_cast<int>(objects_.size())) {
            continue;
        }
        if (!IsObjectEffectivelyEnabled(runtimeVehicle.objectIndex)) {
            continue;
        }

        const SceneObject& vehicleObject = objects_[runtimeVehicle.objectIndex];
        const std::string profileId = vehicleObject.vehicle.inputProfileId.empty()
            ? std::string("default_vehicle")
            : vehicleObject.vehicle.inputProfileId;
        const bool routeInput = ShouldRouteInputToGame() && inputManager_ != nullptr;
        ConsumePendingVehicleGearActions(runtimeVehicle);
        ArcadeVehicleInput baseInput = SampleArcadeVehicleInput(runtimeVehicle, vehicleObject, inputManager_, profileId, routeInput, deltaTime);

        const float rawThrottleAmount = baseInput.throttle;
        const float rawBrakeAmount = baseInput.brake;
        const VehicleSurfaceSample surfaceSample =
            SampleVehicleSurface(runtimeVehicle, GetProjectTrackSurfaceSettings(TrackSurfaceType::Asphalt));

        ArcadeVehicleTelemetry telemetry;
        if (!runtimeVehicle.arcadeInitialized) {
            runtimeVehicle.arcadeChassisWorld = TransformFromMatrix(GetObjectWorldMatrix(runtimeVehicle.objectIndex));
            runtimeVehicle.arcadePreviousChassisWorld = runtimeVehicle.arcadeChassisWorld;
            runtimeVehicle.arcadeInitialized = true;
        }
        runtimeVehicle.arcadePreviousChassisWorld = runtimeVehicle.arcadeChassisWorld;
        runtimeVehicle.arcadePreviousWheelSpin = runtimeVehicle.arcadeWheelSpin;

        float& speed = runtimeVehicle.arcadeSpeed;
        float& lateralSpeed = runtimeVehicle.arcadeLateralSpeed;
        const raceman::physics::VehicleArcadeHandlingConfig& arcadeHandling = runtimeVehicle.config.arcadeHandling;
        const float maxForwardSpeed = (std::max)(1.0f, arcadeHandling.maxForwardSpeed);
        const float idleRPM = (std::max)(0.0f, arcadeHandling.idleRPM);
        const float redlineRPM = (std::max)(idleRPM + 1.0f, arcadeHandling.redlineRPM);
        const float previousSpeed = speed;
        const float previousThrottleInput = runtimeVehicle.arcadeThrottle;
        const float absSpeedBeforeDrive = std::fabs(speed);
        const VehicleDriveRatios driveRatios = ComputeVehicleDriveRatios(runtimeVehicle, rawThrottleAmount);
        runtimeVehicle.arcadeDifferentialLock = driveRatios.differentialLock;
        const VehicleControlAmounts controls =
            ApplyVehicleDriverAids(runtimeVehicle, rawThrottleAmount, rawBrakeAmount, absSpeedBeforeDrive, driveRatios, deltaTime);
        const float throttleAmount = controls.throttle;
        const float brakeAmount = controls.brake;
        ApplyArcadeVehicleDynamics(runtimeVehicle, baseInput, controls, surfaceSample, driveRatios, routeInput, previousSpeed, previousThrottleInput, deltaTime);

        const float speedFactorBeforeDrive = (std::clamp)(std::fabs(previousSpeed) / maxForwardSpeed, 0.0f, 1.0f);
        const ColliderSurfaceConfig& defaultSurface = GetProjectTrackSurfaceSettings(TrackSurfaceType::Asphalt);
        const std::unordered_set<std::string> ignoredVehicleObjectIds =
            BuildVehicleRaycastIgnoreSet(vehicleObject, runtimeVehicle.chassisBodyObjectId);
        ApplyArcadeVehicleGrounding(
            runtimeVehicle,
            physicsWorld_.get(),
            ignoredVehicleObjectIds,
            defaultSurface,
            [this, &defaultSurface](const std::string& objectId) -> ColliderSurfaceConfig {
                const int objectIndex = FindObjectIndexById(objectId);
                if (objectIndex >= 0 && objectIndex < static_cast<int>(objects_.size())) {
                    return GetProjectTrackSurfaceSettings(objects_[objectIndex].colliderSurface.type);
                }
                return defaultSurface;
            },
            baseInput.steering,
            speedFactorBeforeDrive,
            deltaTime);
        lateralSpeed = runtimeVehicle.arcadeLateralSpeed;
        const float finalAbsSpeed = std::fabs(speed);
        runtimeVehicle.arcadeWheelSpin += (speed / 0.3f) * deltaTime;
        runtimeVehicle.arcadePreviousThrottle = previousThrottleInput;
        runtimeVehicle.arcadeRawThrottle = rawThrottleAmount;
        runtimeVehicle.arcadeRawBrake = rawBrakeAmount;
        runtimeVehicle.arcadeThrottle = throttleAmount;
        runtimeVehicle.arcadeBrake = brakeAmount;
        runtimeVehicle.arcadeSteering = baseInput.steering;
        runtimeVehicle.arcadeHandbrake = baseInput.handbrake;
        UpdateArcadeAutomaticGear(
            runtimeVehicle,
            finalAbsSpeed,
            speed,
            throttleAmount,
            brakeAmount,
            idleRPM,
            redlineRPM,
            maxForwardSpeed,
            deltaTime);

        if (physicsWorld_ != nullptr && !runtimeVehicle.chassisBodyObjectId.empty() && physicsWorld_->HasBody(runtimeVehicle.chassisBodyObjectId)) {
            physicsWorld_->MoveBodyKinematic(
                runtimeVehicle.chassisBodyObjectId,
                runtimeVehicle.arcadeChassisWorld.position,
                runtimeVehicle.arcadeChassisWorld.rotationEuler,
                deltaTime);
        }

        telemetry.steering = runtimeVehicle.arcadeSteering;
        telemetry.longitudinalSpeed = speed;
        telemetry.lateralSpeed = lateralSpeed;
        telemetry.slipAngle = runtimeVehicle.arcadeSlipAngle;
        telemetry.tractionScale = runtimeVehicle.arcadeTractionScale;
        telemetry.wheels.resize((std::max<std::size_t>)(4, runtimeVehicle.config.wheels.size()));
        for (std::size_t wheelIndex = 0; wheelIndex < telemetry.wheels.size(); ++wheelIndex) {
            ArcadeVehicleWheelTelemetry& wheelState = telemetry.wheels[wheelIndex];
            wheelState.normalForce = wheelIndex < runtimeVehicle.arcadeWheelContacts.size()
                ? runtimeVehicle.arcadeWheelContacts[wheelIndex].normalForce
                : 0.0f;
        }

        const WheelForceFeedbackSample ffbSample = BuildWheelForceFeedbackSample(telemetry, runtimeVehicle.config);
        if (std::fabs(ffbSample.torque) >= std::fabs(strongestTorque)) {
            strongestTorque = ffbSample.torque;
            strongestDamper = ffbSample.damper;
            strongestVibration = ffbSample.vibration;
        }
    }

    if (inputManager_ != nullptr && wheelFfbAllowed) {
        inputManager_->SetWheelForceFeedbackState(strongestTorque, strongestDamper, strongestVibration);
    } else if (inputManager_ != nullptr) {
        inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
    }
}

void SceneEditor::UpdateVehicles(float deltaTime) {
    if (!scriptsRunning_ || scriptsPaused_ || deltaTime <= 0.0f) {
        return;
    }

    if (runtimeVehicles_.empty()) {
        return;
    }

    const float renderAlpha = (std::clamp)(runtimeSimulationAccumulator_ / kRuntimeFixedStep, 0.0f, 1.0f);
    UpdateVehicleVisuals(
        objects_,
        runtimeVehicles_,
        [this](const std::string& id) { return FindObjectIndexById(id); },
        [this](int index) { return GetObjectWorldMatrix(index); },
        [this](int index) { return IsObjectEffectivelyEnabled(index); },
        renderAlpha);
}


} // namespace raceman

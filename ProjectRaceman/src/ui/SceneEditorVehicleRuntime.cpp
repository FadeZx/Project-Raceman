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
            contactWheelGripFactor += (std::max)(0.1f, runtimeVehicle.config.wheels[contactIndex].gripFactor);
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
        runtimeVehicle.arcadeEngineRPM = (std::clamp)(idleRPM + (finalAbsSpeed / maxForwardSpeed) * (redlineRPM - idleRPM) * (0.45f + throttleAmount * 0.55f),
                                                      idleRPM,
                                                      redlineRPM);
        runtimeVehicle.arcadePreviousThrottle = previousThrottleInput;
        runtimeVehicle.arcadeRawThrottle = rawThrottleAmount;
        runtimeVehicle.arcadeRawBrake = rawBrakeAmount;
        runtimeVehicle.arcadeThrottle = throttleAmount;
        runtimeVehicle.arcadeBrake = brakeAmount;
        runtimeVehicle.arcadeSteering = baseInput.steering;
        runtimeVehicle.arcadeHandbrake = baseInput.handbrake;
        if (speed < -0.5f) {
            runtimeVehicle.arcadeGear = -1;
        } else if (finalAbsSpeed < 0.2f) {
            runtimeVehicle.arcadeGear = 0;
        } else {
            const int gearCount = (std::max)(1, static_cast<int>(runtimeVehicle.config.transmission.gearRatios.size()));
            const float normalizedSpeed = (std::clamp)(finalAbsSpeed / maxForwardSpeed, 0.0f, 0.999f);
            runtimeVehicle.arcadeGear = (std::clamp)(1 + static_cast<int>(normalizedSpeed * static_cast<float>(gearCount)), 1, gearCount);
        }

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

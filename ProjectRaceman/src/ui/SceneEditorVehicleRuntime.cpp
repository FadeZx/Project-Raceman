#include "SceneEditorInternal.h"
#include "SceneEditorVehicleDynamics.h"
#include "SceneEditorVehicleGrounding.h"
#include "SceneEditorVehicleInput.h"
#include "SceneEditorVehicleTelemetry.h"
#include "SceneEditorVehicleVisuals.h"
#include "SceneEditorVehicleWheelSlip.h"
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

// Sequential manual box. There is no clutch in this model, so it makes two
// concessions and no more: first engages itself from a standstill, and a
// downshift the engine could not survive is refused rather than granted.
void UpdateManualGear(RuntimeVehicleInstance& runtimeVehicle,
                      const VehicleGearActions& actions,
                      float absSpeed,
                      float throttleAmount,
                      float idleRPM,
                      float redlineRPM,
                      float maxForwardSpeed,
                      float deltaTime) {
    const int gearCount = (std::max)(1, static_cast<int>(runtimeVehicle.config.transmission.gearRatios.size()));
    runtimeVehicle.autoShiftCooldown = (std::max)(0.0f, runtimeVehicle.autoShiftCooldown - deltaTime);

    int gear = (std::clamp)(runtimeVehicle.arcadeGear, -1, gearCount);
    const bool stationary = absSpeed < 1.5f;
    const bool canShift = runtimeVehicle.autoShiftCooldown <= 0.0f;

    auto engage = [&](int target) {
        if (target == gear) {
            return;
        }
        runtimeVehicle.autoShiftCooldown = GearShiftCooldown(runtimeVehicle.config, (std::max)(1, gear), gearCount);
        gear = target;
    };

    if (actions.neutral) {
        engage(0);
    } else if (actions.reverse && stationary) {
        engage(-1);
    } else if (canShift && actions.shiftUp) {
        if (gear < 0) {
            engage(0);
        } else if (gear < gearCount) {
            engage(gear + 1);
        }
    } else if (canShift && actions.shiftDown) {
        if (gear > 1) {
            // Speed past the lower gear top speed is speed past its redline.
            // A real sequential will not let you buy a rebuild with a paddle.
            const float lowerGearTopSpeed = GearTopSpeed(runtimeVehicle.config, gear - 1, maxForwardSpeed);
            if (absSpeed <= lowerGearTopSpeed * 1.02f) {
                engage(gear - 1);
            }
        } else if (gear == 1 && stationary) {
            engage(0);
        } else if (gear == 0 && stationary) {
            engage(-1);
        }
    }

    // No clutch to slip, so pulling away is the one thing the box still does
    // on its own. Everything above this speed is the driver's problem.
    if (gear == 0 && throttleAmount > 0.05f && absSpeed < 0.5f) {
        gear = 1;
    }

    runtimeVehicle.arcadeGear = (std::clamp)(gear, -1, gearCount);

    float targetRpm = idleRPM;
    if (runtimeVehicle.arcadeGear == 0) {
        // Nothing is connected to the wheels, so the engine just answers the
        // pedal. Blipping in neutral is audible instead of silent.
        targetRpm = idleRPM + (std::clamp)(throttleAmount, 0.0f, 1.0f) * (redlineRPM - idleRPM) * 0.85f;
    } else {
        targetRpm = GearSpeedToRpm(
            runtimeVehicle.config,
            absSpeed,
            runtimeVehicle.arcadeGear < 0 ? 1 : runtimeVehicle.arcadeGear,
            idleRPM,
            redlineRPM,
            maxForwardSpeed);
    }

    const float shiftBlend = runtimeVehicle.autoShiftCooldown > 0.0f ? 0.45f : 1.0f;
    runtimeVehicle.arcadeEngineRPM = (std::clamp)(
        runtimeVehicle.arcadeEngineRPM + (targetRpm - runtimeVehicle.arcadeEngineRPM) * shiftBlend,
        idleRPM,
        redlineRPM);
}


// Feeds the inertial engine model from the arcade sim. Runs inside the fixed
// step so gear changes are caught even when several steps land in one frame.
void UpdateVehicleEngineState(RuntimeVehicleInstance& runtimeVehicle,
                              float absSpeed,
                              float previousSpeed,
                              float deltaTime) {
    const raceman::physics::VehicleArcadeHandlingConfig& handling = runtimeVehicle.config.arcadeHandling;

    raceman::physics::VehicleEngineTuning tuning;
    // Idle and redline come from arcadeHandling because that is what the sim
    // actually uses; engine.idleRPM/redlineRPM are authored but dead.
    tuning.idleRpm    = (std::max)(0.0f, runtimeVehicle.config.engine.idleRPM);
    tuning.redlineRpm = (std::max)(tuning.idleRpm + 1.0f, runtimeVehicle.config.engine.redlineRPM);
    tuning.inertia    = (std::max)(0.02f, runtimeVehicle.config.engine.inertia);

    raceman::physics::VehicleEngineInput input;
    input.targetRpmFromGearing  = runtimeVehicle.arcadeEngineRPM;
    input.throttle              = runtimeVehicle.arcadeThrottle;
    input.brake                 = runtimeVehicle.arcadeBrake;
    input.gear                  = runtimeVehicle.arcadeGear;
    input.shifting              = runtimeVehicle.autoShiftCooldown > 0.0f;
    input.speed                 = absSpeed;
    input.previousSpeed         = std::fabs(previousSpeed);
    input.commandedAcceleration = handling.acceleration;
    input.wheelspin             = runtimeVehicle.arcadeTractionControlCut;
    input.deltaTime             = deltaTime;

    runtimeVehicle.engineState.Update(tuning, input);
}

// Dry tarmac makes white rubber smoke. Loose surfaces throw dust instead, which
// is browner, thicker and hangs differently - but it comes off the same slip,
// so it is the same system with a different tint.
float SmokeGainForSurface(TrackSurfaceType surface) {
    switch (surface) {
        case TrackSurfaceType::Dirt:  return 1.00f;   // dust, and plenty of it
        case TrackSurfaceType::Grass: return 0.70f;
        case TrackSurfaceType::Curb:  return 0.75f;
        case TrackSurfaceType::Wall:  return 0.0f;
        default:                      return 1.0f;
    }
}

glm::vec3 SmokeTintForSurface(TrackSurfaceType surface) {
    switch (surface) {
        case TrackSurfaceType::Dirt:  return glm::vec3(1.05f, 0.88f, 0.66f);
        case TrackSurfaceType::Grass: return glm::vec3(0.88f, 0.95f, 0.78f);
        default:                      return glm::vec3(1.0f);
    }
}

// How each surface responds to a sliding tyre. Tarmac takes rubber and goes
// black; loose surfaces throw pale dust and keep almost none of it; a wall is
// not something you leave a skid mark on at all.
SkidMarkWheelState MakeSkidMarkWheelState(const RuntimeVehicleWheelContact& contact) {
    SkidMarkWheelState wheel;
    wheel.slipAmount = contact.slipAmount;
    wheel.slipRatio = contact.slipRatio;
    wheel.lateralSlipAngle = contact.lateralSlipAngle;

    switch (contact.surfaceType) {
        case TrackSurfaceType::Dirt:
            wheel.rubberGain = 0.55f;
            wheel.dustColor = glm::vec3(0.66f, 0.55f, 0.40f);
            wheel.dustiness = 0.85f;
            break;
        case TrackSurfaceType::Grass:
            // Torn grass shows for a moment and is gone. Barely a mark.
            wheel.rubberGain = 0.30f;
            wheel.dustColor = glm::vec3(0.52f, 0.58f, 0.38f);
            wheel.dustiness = 0.90f;
            break;
        case TrackSurfaceType::Curb:
            // Painted concrete takes rubber, just less of it than tarmac.
            wheel.rubberGain = 0.70f;
            wheel.dustColor = glm::vec3(0.55f, 0.52f, 0.50f);
            wheel.dustiness = 0.25f;
            break;
        case TrackSurfaceType::Wall:
            wheel.rubberGain = 0.0f;
            break;
        case TrackSurfaceType::Asphalt:
        case TrackSurfaceType::Custom:
        default:
            break;
    }
    return wheel;
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

    const bool routeInputForFfb = inputManager_ != nullptr && ShouldRouteInputToGame();
    if (routeInputForFfb) {
        // Hand FFB back and forth between the wheel and keyboard/gamepad based
        // on whichever one the player is actually touching; hold the last
        // state while nothing is moving so a wheel resting at centre does not
        // read as "not in use".
        for (const RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles_) {
            if (runtimeVehicle.objectIndex < 0 || runtimeVehicle.objectIndex >= static_cast<int>(objects_.size())) {
                continue;
            }
            const SceneObject& vehicleObject = objects_[runtimeVehicle.objectIndex];
            const std::string profileId = vehicleObject.vehicle.inputProfileId.empty()
                ? std::string("default_vehicle")
                : vehicleObject.vehicle.inputProfileId;
            if (inputManager_->IsNonWheelDeviceDrivingProfile(profileId,
                    vehicleObject.vehicle.preferredInputDevice, vehicleObject.vehicle.preferredInputDeviceId)) {
                wheelIsPreferredInputSource_ = false;
            } else if (inputManager_->IsWheelDeviceDrivingProfile(profileId,
                    vehicleObject.vehicle.preferredInputDevice, vehicleObject.vehicle.preferredInputDeviceId)) {
                wheelIsPreferredInputSource_ = true;
            }
        }
    }
    const bool wheelFfbAllowed = routeInputForFfb && wheelIsPreferredInputSource_;
    if (inputManager_ != nullptr) {
        inputManager_->SetWheelForceFeedbackActive(wheelFfbAllowed);
    }

    WheelForceFeedbackState strongestSample{};

    // Ages existing marks once per frame. Emission below is distance driven, so
    // this is the only place time enters the system.
    const SkidMarkSettings skidSettings = SkidMarkSettingsFromProfile(graphicsProfile_);
    skidMarks_.BeginFrame(deltaTime);
    // Smoke ages, drifts and expands here for the same reason, and before any
    // emission below so a puff born this step is not also aged by it.
    const TyreSmokeSettings smokeSettings = TyreSmokeSettingsFromProfile(graphicsProfile_);
    tyreSmoke_.BeginFrame(deltaTime, smokeSettings);

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
        const VehicleGearActions gearActions = ConsumePendingVehicleGearActions(runtimeVehicle);
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
        const float idleRPM = (std::max)(0.0f, runtimeVehicle.config.engine.idleRPM);
        const float redlineRPM = (std::max)(idleRPM + 1.0f, runtimeVehicle.config.engine.redlineRPM);
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
        // Grounding has resolved load, normal and surface for every wheel, so
        // each tyre can now be asked what it is individually doing. Everything
        // cosmetic downstream reads the result rather than chassis averages.
        UpdateArcadeWheelSlip(runtimeVehicle, baseInput, controls, driveRatios, deltaTime);
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
        if (runtimeVehicle.config.transmission.mode == raceman::physics::TransmissionConfig::Mode::Manual) {
            UpdateManualGear(
                runtimeVehicle,
                gearActions,
                finalAbsSpeed,
                throttleAmount,
                idleRPM,
                redlineRPM,
                maxForwardSpeed,
                deltaTime);
        } else {
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
        }

        UpdateVehicleEngineState(runtimeVehicle, finalAbsSpeed, previousSpeed, deltaTime);

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
        telemetry.yawRate = runtimeVehicle.arcadeYawRate;
        telemetry.surfaceGrip = runtimeVehicle.arcadeSurfaceGrip;
        telemetry.frontSlip = runtimeVehicle.arcadeFrontSlip;
        telemetry.rearSlip = runtimeVehicle.arcadeRearSlip;
        telemetry.tireScrub = runtimeVehicle.arcadeTireScrub;
        telemetry.throttle = runtimeVehicle.arcadeThrottle;
        telemetry.brake = runtimeVehicle.arcadeBrake;
        telemetry.handbrake = runtimeVehicle.arcadeHandbrake;
        telemetry.engineRPM = runtimeVehicle.arcadeEngineRPM;
        telemetry.redlineRPM = redlineRPM;
        telemetry.verticalVelocity = runtimeVehicle.arcadeVerticalVelocity;
        telemetry.maxForwardSpeed = maxForwardSpeed;
        telemetry.wheels.resize((std::max<std::size_t>)(4, runtimeVehicle.config.wheels.size()));
        for (std::size_t wheelIndex = 0; wheelIndex < telemetry.wheels.size(); ++wheelIndex) {
            ArcadeVehicleWheelTelemetry& wheelState = telemetry.wheels[wheelIndex];
            if (wheelIndex < runtimeVehicle.arcadeWheelContacts.size()) {
                const RuntimeVehicleWheelContact& contact = runtimeVehicle.arcadeWheelContacts[wheelIndex];
                // Lay rubber from the one quantity that physically deposits it:
                // how fast this tyre's contact patch is sliding over the road,
                // normalised. It is already zero while the tyre grips, so a
                // fast straight line marks nothing and a locked wheel at walking
                // pace marks faintly - both of which a threshold on slip angle
                // got backwards.
                const SkidMarkWheelState markState = MakeSkidMarkWheelState(contact);
                skidMarks_.TrackWheel(runtimeVehicle.objectId, static_cast<int>(wheelIndex),
                                      contact.grounded, contact.contactPosition, contact.normal,
                                      markState, skidSettings);
                // Smoke off the same slip, thrown along the direction the tyre
                // is actually sliding rather than dropped where the car is.
                tyreSmoke_.EmitFromWheel(runtimeVehicle.objectId, static_cast<int>(wheelIndex),
                                         contact.grounded, contact.contactPosition, contact.normal,
                                         runtimeVehicle.arcadePlanarVelocity,
                                         contact.slipAmount,
                                         SmokeGainForSurface(contact.surfaceType),
                                         SmokeTintForSurface(contact.surfaceType),
                                         deltaTime, smokeSettings);
                wheelState.normalForce = contact.normalForce;
                wheelState.slipAngle = contact.slipAngle;
                wheelState.tractionScale = contact.tractionScale;
                wheelState.suspensionTravel = contact.suspensionTravel;
                wheelState.angularVelocity = contact.angularVelocity;
                wheelState.slipRatio = contact.slipRatio;
                wheelState.slipVelocity = contact.slipVelocity;
                wheelState.slipAmount = contact.slipAmount;
                wheelState.gripUtilisation = contact.gripUtilisation;
                wheelState.locked = contact.locked;
                wheelState.spinning = contact.spinning;
                wheelState.grounded = contact.grounded;
                wheelState.surfaceType = contact.surfaceType;
            }
            wheelState.steered = wheelIndex < runtimeVehicle.config.wheels.size() &&
                                 runtimeVehicle.config.wheels[wheelIndex].maxSteerAngle > 0.01f;
        }

        const WheelForceFeedbackState ffbSample = BuildWheelForceFeedbackSample(
            telemetry, runtimeVehicle.config, trackSurfaceSettings_, runtimeVehicle.forceFeedbackState, deltaTime);
        // The wheel can only render one car, so the vehicle generating the most
        // steering load wins - in practice the one the player is driving.
        if (std::fabs(ffbSample.steeringTorque) >= std::fabs(strongestSample.steeringTorque)) {
            strongestSample = ffbSample;
        }
    }

    if (inputManager_ != nullptr && wheelFfbAllowed) {
        inputManager_->SetWheelForceFeedbackState(strongestSample);
    } else if (inputManager_ != nullptr) {
        inputManager_->SetWheelForceFeedbackState(WheelForceFeedbackState{});
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

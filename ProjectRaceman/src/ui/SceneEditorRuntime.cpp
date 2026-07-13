#include "SceneEditorInternal.h"
#include "../audio/AudioManager.h"
#include "../audio/VehicleSoundProfile.h"
#include "../input/InputManager.h"
#include "../physics/PhysicsWorld.h"
#include "../scripting/ScriptRegistry.h"

#include <irrKlang/irrKlang.h>
#include <imgui/imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <unordered_set>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

constexpr float kRuntimeFixedStep = 1.0f / 60.0f;

bool IsEnvironmentFlagEnabled(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    const bool enabled = _dupenv_s(&value, &length, name) == 0 && value != nullptr && std::string(value) == "1";
    free(value);
    return enabled;
#else
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
#endif
}

raceman::physics::VehicleConfig BuildDefaultJoltVehicleConfig() {
    raceman::physics::VehicleConfig config;
    config.name = "Default Jolt Vehicle";
    config.chassis.mass = 1200.0f;
    config.chassis.centerOfMassOffset = {0.0f, 0.0f, -0.25f};
    config.engine.idleRPM = 900.0f;
    config.engine.redlineRPM = 6000.0f;
    config.engine.torqueCurve = {{900.0f, 260.0f}, {3500.0f, 420.0f}, {6000.0f, 360.0f}};
    config.transmission.mode = raceman::physics::TransmissionConfig::Mode::Automatic;
    config.transmission.gearRatios = {2.8f, 1.9f, 1.35f, 1.0f, 0.8f};
    config.transmission.finalDriveRatio = 3.42f;
    config.transmission.reverseRatio = -2.9f;
    config.transmission.shiftTime = 0.25f;
    config.frontSuspension.restLength = 0.35f;
    config.rearSuspension.restLength = 0.35f;

    auto makeWheel = [](std::string name, float x, float y, bool front, bool driven) {
        raceman::physics::WheelConfig wheel;
        wheel.name = std::move(name);
        wheel.mountPosition = {x, y, 0.0f};
        wheel.radius = 0.35f;
        wheel.width = 0.22f;
        wheel.inertia = 1.0f;
        wheel.maxSteerAngle = front ? 0.55f : 0.0f;
        wheel.maxBrakingTorque = front ? 3200.0f : 2800.0f;
        wheel.driven = driven;
        wheel.hasBrake = true;
        return wheel;
    };

    config.wheels = {
        makeWheel("Front Left", -0.82f, 1.25f, true, false),
        makeWheel("Front Right", 0.82f, 1.25f, true, false),
        makeWheel("Rear Left", -0.82f, -1.25f, false, true),
        makeWheel("Rear Right", 0.82f, -1.25f, false, true),
    };
    return config;
}

void EnsureDrivableVehicleConfig(raceman::physics::VehicleConfig& config) {
    if (!config.wheels.empty()) {
        return;
    }
    raceman::physics::VehicleConfig fallback = BuildDefaultJoltVehicleConfig();
    fallback.name = config.name.empty() ? fallback.name : config.name;
    fallback.chassis = config.chassis;
    fallback.engine = config.engine;
    fallback.transmission = config.transmission;
    fallback.frontSuspension = config.frontSuspension;
    fallback.rearSuspension = config.rearSuspension;
    fallback.groundContact = config.groundContact;
    fallback.tireGrip = config.tireGrip;
    config = std::move(fallback);
}

PhysicsColliderDesc BuildDefaultVehicleChassisCollider(const raceman::physics::VehicleConfig& config) {
    (void)config;

    PhysicsColliderDesc collider;
    collider.type = PhysicsColliderType::Box;
    collider.center = glm::vec3(0.0f, -0.2f, 0.0f);
    collider.size = glm::vec3(1.8f, 0.4f, 4.0f);
    return collider;
}

struct WheelForceFeedbackSample {
    float torque{0.0f};
    float damper{0.0f};
    float vibration{0.0f};
};

struct ArcadeVehicleInput {
    float throttle{0.0f};
    float brake{0.0f};
    float steering{0.0f};
    float handbrake{0.0f};
};

struct ArcadeVehicleWheelTelemetry {
    float normalForce{0.0f};
};

struct ArcadeVehicleTelemetry {
    float longitudinalSpeed{0.0f};
    float lateralSpeed{0.0f};
    float slipAngle{0.0f};
    float tractionScale{1.0f};
    float steering{0.0f};
    std::vector<ArcadeVehicleWheelTelemetry> wheels;
};

WheelForceFeedbackSample BuildWheelForceFeedbackSample(const ArcadeVehicleTelemetry& telemetry,
                                                       const raceman::physics::VehicleConfig& config) {
    WheelForceFeedbackSample sample;
    if (config.wheels.empty()) {
        return sample;
    }

    float frontNormalForce = 0.0f;
    int frontWheelCount = 0;
    for (std::size_t i = 0; i < telemetry.wheels.size() && i < config.wheels.size(); ++i) {
        if (config.wheels[i].mountPosition.y >= 0.0f) {
            frontNormalForce += telemetry.wheels[i].normalForce;
            ++frontWheelCount;
        }
    }

    const float averageFrontNormal = frontWheelCount > 0 ? (frontNormalForce / static_cast<float>(frontWheelCount)) : 0.0f;
    const float speedAbs = std::fabs(telemetry.longitudinalSpeed);
    const float speedFactor = (std::clamp)(speedAbs / 25.0f, 0.0f, 1.0f);
    const float loadFactor = (std::clamp)(averageFrontNormal / 4000.0f, 0.0f, 1.0f);
    const float slipFactor = (std::clamp)(std::fabs(telemetry.slipAngle) / 25.0f, 0.0f, 1.0f);

    // Keep the first-pass model intentionally simple: center the wheel against the current steer input,
    // then add a small speed-based damper. This avoids the previous odd slip-pull behavior.
    sample.torque = (std::clamp)(-telemetry.steering * (0.08f + speedFactor * 0.42f + loadFactor * 0.28f) * telemetry.tractionScale, -1.0f, 1.0f);
    sample.damper = (std::clamp)(0.03f + speedFactor * 0.08f + slipFactor * 0.08f, 0.0f, 1.0f);
    sample.vibration = (std::clamp)(slipFactor * (1.0f - telemetry.tractionScale) * 0.35f, 0.0f, 1.0f);
    return sample;
}

float MoveTowards(float current, float target, float maxDelta) {
    if (current < target) {
        return (std::min)(current + maxDelta, target);
    }
    return (std::max)(current - maxDelta, target);
}

float ShortestAngleDeltaDegrees(float from, float to) {
    float delta = std::fmod(to - from, 360.0f);
    if (delta > 180.0f) {
        delta -= 360.0f;
    } else if (delta < -180.0f) {
        delta += 360.0f;
    }
    return delta;
}

glm::vec3 InterpolateEulerDegrees(const glm::vec3& from, const glm::vec3& to, float alpha) {
    return {
        from.x + ShortestAngleDeltaDegrees(from.x, to.x) * alpha,
        from.y + ShortestAngleDeltaDegrees(from.y, to.y) * alpha,
        from.z + ShortestAngleDeltaDegrees(from.z, to.z) * alpha,
    };
}

Transform InterpolateTransform(const Transform& from, const Transform& to, float alpha) {
    Transform result;
    result.position = glm::mix(from.position, to.position, alpha);
    result.rotationEuler = InterpolateEulerDegrees(from.rotationEuler, to.rotationEuler, alpha);
    result.scale = glm::mix(from.scale, to.scale, alpha);
    return result;
}

float SmoothingAlpha(float smoothing, float deltaTime) {
    if (smoothing <= 0.0f) {
        return 1.0f;
    }
    return (std::clamp)(1.0f - std::exp(-smoothing * deltaTime), 0.0f, 1.0f);
}

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

float NormalizeKeyboardSensitivity(float value) {
    if (value > 1.0f) {
        return (std::clamp)(value / 30.0f, 0.0f, 1.0f);
    }
    return (std::clamp)(value, 0.0f, 1.0f);
}

float KeyboardSteeringSensitivityToRate(float value) {
    return 1.0f + NormalizeKeyboardSensitivity(value) * 11.0f;
}

float KeyboardThrottleSensitivityToRate(float value) {
    return 1.0f + NormalizeKeyboardSensitivity(value) * 9.0f;
}

float KeyboardBrakeSensitivityToRate(float value) {
    return 1.0f + NormalizeKeyboardSensitivity(value) * 9.0f;
}

float ResolveKeyboardAxis(const InputManager& inputManager, const InputProfile& profile, std::string_view action) {
    float bestMagnitude = 0.0f;
    float resolved = 0.0f;
    for (const InputBinding& binding : profile.bindings) {
        if (binding.action != action || binding.deviceType != InputDeviceType::Keyboard) {
            continue;
        }

        float value = 0.0f;
        if (binding.source == InputBindingSource::Key) {
            value = binding.key >= 0 && inputManager.IsKeyDown(binding.key) ? 1.0f : 0.0f;
        } else if (binding.source == InputBindingSource::KeyPair) {
            const float negative = binding.negativeKey >= 0 && inputManager.IsKeyDown(binding.negativeKey) ? -1.0f : 0.0f;
            const float positive = binding.positiveKey >= 0 && inputManager.IsKeyDown(binding.positiveKey) ? 1.0f : 0.0f;
            value = negative + positive;
        }

        if (binding.invert) {
            value = -value;
        }

        const float magnitude = std::fabs(value);
        if (magnitude > bestMagnitude) {
            bestMagnitude = magnitude;
            resolved = value;
        }
    }
    return (std::clamp)(resolved, -1.0f, 1.0f);
}

} // namespace

void SceneEditor::SetProjectRoot(std::string path) {
#if defined(_WIN32)
    _putenv_s("RACEMAN_PROJECT_ROOT", path.c_str());
#else
    setenv("RACEMAN_PROJECT_ROOT", path.c_str(), 1);
#endif
    if (!scriptsRunning_) {
        LoadProject();
    }
}

std::string SceneEditor::GetProjectRoot() const {
    return FindProjectRoot().string();
}

const PhysicsBuildProgress* SceneEditor::GetPhysicsBuildProgress() const {
    if (playModeLoad_.progress && !playModeLoad_.progress->isDone.load()) {
        return playModeLoad_.progress.get();
    }
    return nullptr;
}

void SceneEditor::StartRuntime() {
    if (scriptsRunning_ || playModeLoad_.phase != PlayModeLoadState::Phase::Idle) {
        return;
    }

    activeViewport_ = SceneEditorActiveViewport::Game;
    scriptsPaused_ = false;

    std::string scriptLoadError;
    if (!LoadScriptAssembly(&scriptLoadError) && console_ != nullptr) {
        console_->AddWarning(scriptLoadError.empty()
            ? "Script DLL was not loaded; continuing without scripts."
            : scriptLoadError);
    }

    playModeScriptAssemblyReady_ = true;
    SetScriptsRunning(true);
}

void SceneEditor::UpdateRuntime(float deltaTime) {
    TickPlayModeLoading();

    UpdateRuntimeSystems(deltaTime);
}

void SceneEditor::UpdateRuntimeSystems(float deltaTime) {
    if (!scriptsRunning_ || deltaTime <= 0.0f) {
        runtimeSimulationAccumulator_ = 0.0f;
        return;
    }

    UpdateScripts(deltaTime);

    constexpr float kMaxAccumulatedFrameTime = 0.10f;
    constexpr int kMaxFixedStepsPerFrame = 4;

    if (scriptsPaused_) {
        runtimeSimulationAccumulator_ = 0.0f;
        for (RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles_) {
            runtimeVehicle.pendingShiftUp = false;
            runtimeVehicle.pendingShiftDown = false;
            runtimeVehicle.pendingNeutral = false;
            runtimeVehicle.pendingReverse = false;
        }
    } else {
        const bool routeInput = ShouldRouteInputToGame() && inputManager_ != nullptr;
        for (RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles_) {
            if (!routeInput ||
                runtimeVehicle.objectIndex < 0 ||
                runtimeVehicle.objectIndex >= static_cast<int>(objects_.size())) {
                runtimeVehicle.pendingShiftUp = false;
                runtimeVehicle.pendingShiftDown = false;
                runtimeVehicle.pendingNeutral = false;
                runtimeVehicle.pendingReverse = false;
                continue;
            }

            const SceneObject& vehicleObject = objects_[runtimeVehicle.objectIndex];
            const std::string profileId = vehicleObject.vehicle.inputProfileId.empty()
                ? std::string("default_vehicle")
                : vehicleObject.vehicle.inputProfileId;
            runtimeVehicle.pendingShiftUp = runtimeVehicle.pendingShiftUp ||
                inputManager_->WasActionPressedForProfile(profileId, "shiftUp",
                    vehicleObject.vehicle.preferredInputDevice,
                    vehicleObject.vehicle.preferredInputDeviceId);
            runtimeVehicle.pendingShiftDown = runtimeVehicle.pendingShiftDown ||
                inputManager_->WasActionPressedForProfile(profileId, "shiftDown",
                    vehicleObject.vehicle.preferredInputDevice,
                    vehicleObject.vehicle.preferredInputDeviceId);
            runtimeVehicle.pendingNeutral = runtimeVehicle.pendingNeutral ||
                inputManager_->WasActionPressedForProfile(profileId, "neutral",
                    vehicleObject.vehicle.preferredInputDevice,
                    vehicleObject.vehicle.preferredInputDeviceId);
            runtimeVehicle.pendingReverse = runtimeVehicle.pendingReverse ||
                inputManager_->WasActionPressedForProfile(profileId, "reverse",
                    vehicleObject.vehicle.preferredInputDevice,
                    vehicleObject.vehicle.preferredInputDeviceId);
        }

        runtimeSimulationAccumulator_ += (std::min)(deltaTime, kMaxAccumulatedFrameTime);

        int fixedSteps = 0;
        while (runtimeSimulationAccumulator_ >= kRuntimeFixedStep && fixedSteps < kMaxFixedStepsPerFrame) {
            UpdateVehiclePhysics(kRuntimeFixedStep);
            UpdatePhysics(kRuntimeFixedStep);
            runtimeSimulationAccumulator_ -= kRuntimeFixedStep;
            ++fixedSteps;
        }

        if (fixedSteps >= kMaxFixedStepsPerFrame && runtimeSimulationAccumulator_ >= kRuntimeFixedStep) {
            runtimeSimulationAccumulator_ = 0.0f;
        }
    }

    UpdateVehicles(deltaTime);
    UpdateCinemachine(deltaTime);
    UpdateAudio(deltaTime);
}

void SceneEditor::StopRuntime() {
    if (scriptsRunning_) {
        SetScriptsRunning(false);
    }
}

void SceneEditor::UpdateScripts(float deltaTime) {
    if (!scriptsRunning_ || scriptsPaused_ || deltaTime <= 0.0f) {
        return;
    }

    for (RuntimeScriptInstance& runtimeScript : runtimeScripts_) {
        auto objectIt = std::find_if(objects_.begin(), objects_.end(), [&](const SceneObject& object) {
            return object.id == runtimeScript.objectId;
        });
        if (objectIt == objects_.end()) {
            continue;
        }
        const int objectIndex = static_cast<int>(std::distance(objects_.begin(), objectIt));
        if (!IsObjectEffectivelyEnabled(objectIndex) || !objectIt->hasScriptComponent || !objectIt->scriptComponent.enabled) {
            continue;
        }
        if (runtimeScript.attachmentIndex >= objectIt->scriptComponent.attachments.size()) {
            continue;
        }

        ObjectScriptAttachment& attachment = objectIt->scriptComponent.attachments[runtimeScript.attachmentIndex];
        if (!attachment.enabled || !runtimeScript.instance) {
            continue;
        }

        InputManager* scriptInput = ShouldRouteInputToGame() ? inputManager_ : nullptr;
        ObjectScriptContext context(*objectIt, &attachment, console_, scriptInput, physicsWorld_.get(), &objects_);
        if (!runtimeScript.started) {
            runtimeScript.instance->OnStart(context);
            runtimeScript.started = true;
        }
        runtimeScript.instance->OnUpdate(context, deltaTime);
    }
}

void SceneEditor::UpdatePhysics(float deltaTime) {
    if (!scriptsRunning_ || scriptsPaused_ || deltaTime <= 0.0f) {
        return;
    }

    if (!physicsWorld_) {
        return;
    }

    for (const SceneObject& object : objects_) {
        if (object.hasRigidbody &&
            object.rigidbody.enabled &&
            !(object.hasVehicle && object.vehicle.enabled) &&
            object.rigidbody.bodyType != RigidbodyBodyType::Static) {
            physicsWorld_->SetBodyVelocity(object.id, object.rigidbody.velocity);
            physicsWorld_->SetBodyAngularVelocity(object.id, object.rigidbody.angularVelocity);
        }
    }

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        const SceneObject& object = objects_[objectIndex];
        if (!object.hasCharacterController || !object.characterController.enabled || !physicsWorld_->HasCharacter(object.id)) {
            continue;
        }

        const Transform worldTransform = TransformFromMatrix(GetObjectWorldMatrix(objectIndex));
        physicsWorld_->SetCharacterTransform(object.id, worldTransform.position, worldTransform.rotationEuler);
        physicsWorld_->SetCharacterDesiredVelocity(object.id, object.characterController.moveInput);
        if (object.characterController.pendingJumpImpulse > 0.0f) {
            physicsWorld_->AddCharacterJumpImpulse(object.id, object.characterController.pendingJumpImpulse);
        }
    }

    // Feed activator positions to the spatial culling system.
    // Vehicles and characters are the "hot" objects that keep nearby dynamic props awake.
    {
        std::vector<glm::vec3> activatorPositions;
        for (const RuntimeVehicleInstance& rv : runtimeVehicles_) {
            if (!rv.chassisBodyObjectId.empty()) {
                PhysicsBodyState s;
                if (physicsWorld_->GetBodyState(rv.chassisBodyObjectId, s)) {
                    activatorPositions.push_back(s.position);
                }
            }
        }
        for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
            const SceneObject& object = objects_[objectIndex];
            if (object.hasCharacterController && object.characterController.enabled && physicsWorld_->HasCharacter(object.id)) {
                activatorPositions.push_back(TransformFromMatrix(GetObjectWorldMatrix(objectIndex)).position);
            }
        }
        if (!enablePhysicsCulling_) {
            activatorPositions.clear();
        }
        physicsWorld_->SetActivatorPositions(activatorPositions, 80.0f, 120.0f);
    }

    physicsWorld_->Step(deltaTime);

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        SceneObject& object = objects_[objectIndex];
        if (!object.hasRigidbody || (object.hasVehicle && object.vehicle.enabled) || object.rigidbody.bodyType == RigidbodyBodyType::Static) {
            continue;
        }

        PhysicsBodyState state;
        if (!physicsWorld_->GetBodyState(object.id, state)) {
            continue;
        }

        object.rigidbody.velocity = state.velocity;
        object.rigidbody.angularVelocity = state.angularVelocity;
        const Transform previousLocal = object.transform;
        Transform worldTransform;
        worldTransform.position = state.position;
        worldTransform.rotationEuler = state.rotationEuler;
        worldTransform.scale = glm::vec3(1.0f);

        glm::mat4 worldMatrix = BuildTransformMatrix(worldTransform);
        worldMatrix = glm::scale(worldMatrix, previousLocal.scale);
        const int parentIndex = FindObjectIndexById(object.parentId);
        if (parentIndex >= 0 && parentIndex != objectIndex) {
            object.transform = TransformFromMatrix(glm::inverse(GetObjectWorldMatrix(parentIndex)) * worldMatrix);
        } else {
            object.transform = TransformFromMatrix(worldMatrix);
        }
        object.transform.scale = previousLocal.scale;
    }

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        SceneObject& object = objects_[objectIndex];
        if (!object.hasCharacterController || !physicsWorld_->HasCharacter(object.id)) {
            continue;
        }

        PhysicsCharacterState state;
        if (!physicsWorld_->GetCharacterState(object.id, state)) {
            continue;
        }

        object.characterController.velocity = state.velocity;
        object.characterController.groundVelocity = state.groundVelocity;
        object.characterController.grounded = state.grounded;
        object.characterController.pendingJumpImpulse = 0.0f;

        const Transform previousLocal = object.transform;
        Transform worldTransform;
        worldTransform.position = state.position;
        worldTransform.rotationEuler = state.rotationEuler;
        worldTransform.scale = glm::vec3(1.0f);

        glm::mat4 worldMatrix = BuildTransformMatrix(worldTransform);
        worldMatrix = glm::scale(worldMatrix, previousLocal.scale);
        const int parentIndex = FindObjectIndexById(object.parentId);
        if (parentIndex >= 0 && parentIndex != objectIndex) {
            object.transform = TransformFromMatrix(glm::inverse(GetObjectWorldMatrix(parentIndex)) * worldMatrix);
        } else {
            object.transform = TransformFromMatrix(worldMatrix);
        }
        object.transform.scale = previousLocal.scale;
    }
}

void SceneEditor::UpdateVehiclePhysics(float deltaTime) {
    if (!scriptsRunning_ || scriptsPaused_ || deltaTime <= 0.0f) {
        if (inputManager_ != nullptr) {
            inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
            inputManager_->SetWheelForceFeedbackActive(false);
        }
        return;
    }

    if (runtimeVehicles_.empty()) {
        if (inputManager_ != nullptr) {
            inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
            inputManager_->SetWheelForceFeedbackActive(false);
        }
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
        constexpr float kDriveIntentInputThreshold = 0.20f;
        const bool manualShiftUpPressed = routeInput && runtimeVehicle.pendingShiftUp;
        const bool manualShiftDownPressed = routeInput && runtimeVehicle.pendingShiftDown;
        const bool manualNeutralPressed = routeInput && runtimeVehicle.pendingNeutral;
        const bool manualReversePressed = routeInput && runtimeVehicle.pendingReverse;
        runtimeVehicle.pendingShiftUp = false;
        runtimeVehicle.pendingShiftDown = false;
        runtimeVehicle.pendingNeutral = false;
        runtimeVehicle.pendingReverse = false;
        ArcadeVehicleInput baseInput{};
        if (routeInput) {
            baseInput.steering = inputManager_->GetAxisForProfile(profileId, "steer",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
            baseInput.handbrake = inputManager_->GetAxisForProfile(profileId, "handbrake",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
            baseInput.throttle = inputManager_->GetAxisForProfile(profileId, "throttle",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
            baseInput.brake = inputManager_->GetAxisForProfile(profileId, "brake",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);

            const InputProfile* activeProfile = inputManager_->FindProfile(profileId);
            if (activeProfile == nullptr) {
                activeProfile = inputManager_->FindProfile("default_vehicle");
            }
            if (activeProfile != nullptr &&
                (vehicleObject.vehicle.preferredInputDevice == InputDevicePreference::Any ||
                 vehicleObject.vehicle.preferredInputDevice == InputDevicePreference::Keyboard)) {
                const float keyboardSteer = ResolveKeyboardAxis(*inputManager_, *activeProfile, "steer");
                const float keyboardThrottle = (std::max)(0.0f, ResolveKeyboardAxis(*inputManager_, *activeProfile, "throttle"));
                const float keyboardBrake = (std::max)(0.0f, ResolveKeyboardAxis(*inputManager_, *activeProfile, "brake"));
                const float steeringRate = KeyboardSteeringSensitivityToRate(activeProfile->keyboardSteeringSensitivity);
                const float throttleRate = KeyboardThrottleSensitivityToRate(activeProfile->keyboardThrottleSensitivity);
                const float brakeRate = KeyboardBrakeSensitivityToRate(activeProfile->keyboardBrakeSensitivity);
                runtimeVehicle.smoothedKeyboardSteering = MoveTowards(
                    runtimeVehicle.smoothedKeyboardSteering,
                    keyboardSteer,
                    deltaTime * (keyboardSteer == 0.0f ? steeringRate * 1.5f : steeringRate));
                runtimeVehicle.smoothedKeyboardThrottle = MoveTowards(
                    runtimeVehicle.smoothedKeyboardThrottle,
                    keyboardThrottle,
                    deltaTime * (keyboardThrottle == 0.0f ? throttleRate * 1.35f : throttleRate));
                runtimeVehicle.smoothedKeyboardBrake = MoveTowards(
                    runtimeVehicle.smoothedKeyboardBrake,
                    keyboardBrake,
                    deltaTime * (keyboardBrake == 0.0f ? brakeRate * 1.35f : brakeRate));

                if (std::fabs(keyboardSteer) > 0.0f || std::fabs(runtimeVehicle.smoothedKeyboardSteering) > 0.0001f) {
                    baseInput.steering = runtimeVehicle.smoothedKeyboardSteering;
                }
                if (keyboardThrottle > 0.0f || runtimeVehicle.smoothedKeyboardThrottle > 0.0001f) {
                    baseInput.throttle = runtimeVehicle.smoothedKeyboardThrottle;
                }
                if (keyboardBrake > 0.0f || runtimeVehicle.smoothedKeyboardBrake > 0.0001f) {
                    baseInput.brake = runtimeVehicle.smoothedKeyboardBrake;
                }
            } else {
                runtimeVehicle.smoothedKeyboardSteering = 0.0f;
                runtimeVehicle.smoothedKeyboardThrottle = 0.0f;
                runtimeVehicle.smoothedKeyboardBrake = 0.0f;
            }
        } else {
            runtimeVehicle.smoothedKeyboardSteering = 0.0f;
            runtimeVehicle.smoothedKeyboardThrottle = 0.0f;
            runtimeVehicle.smoothedKeyboardBrake = 0.0f;
        }

        baseInput.steering = std::clamp(baseInput.steering, -1.0f, 1.0f);
        baseInput.throttle = std::clamp(baseInput.throttle, 0.0f, 1.0f);
        baseInput.brake = std::clamp(baseInput.brake, 0.0f, 1.0f);
        baseInput.handbrake = std::clamp(baseInput.handbrake, 0.0f, 1.0f);

        const float throttleAmount = baseInput.throttle;
        const float brakeAmount = baseInput.brake;
        const bool wantsForward = routeInput && throttleAmount > kDriveIntentInputThreshold;
        const bool wantsReverseOrBrake = routeInput && brakeAmount > kDriveIntentInputThreshold;
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
        if (surfaceContactCount > 0) {
            const float count = static_cast<float>(surfaceContactCount);
            contactGripMultiplier = (std::max)(0.0f, contactGripMultiplier / count);
            contactRollingDrag = (std::max)(0.0f, contactRollingDrag / count);
            contactWheelGripFactor = (std::max)(0.1f, contactWheelGripFactor / count);
        } else {
            const ColliderSurfaceConfig& defaultSurface = GetProjectTrackSurfaceSettings(TrackSurfaceType::Asphalt);
            contactGripMultiplier = (std::max)(0.0f, defaultSurface.gripMultiplier);
            contactRollingDrag = (std::max)(0.0f, defaultSurface.rollingDrag);
            contactWheelGripFactor = 1.0f;
        }

        ArcadeVehicleTelemetry telemetry;
        if (!runtimeVehicle.arcadeInitialized) {
            runtimeVehicle.arcadeChassisWorld = TransformFromMatrix(GetObjectWorldMatrix(runtimeVehicle.objectIndex));
            runtimeVehicle.arcadePreviousChassisWorld = runtimeVehicle.arcadeChassisWorld;
            runtimeVehicle.arcadeInitialized = true;
        }
        runtimeVehicle.arcadePreviousChassisWorld = runtimeVehicle.arcadeChassisWorld;
        runtimeVehicle.arcadePreviousWheelSpin = runtimeVehicle.arcadeWheelSpin;

        constexpr float kMaxForwardSpeed = 55.0f;
        constexpr float kMaxReverseSpeed = 12.0f;
        constexpr float kAcceleration = 18.0f;
        constexpr float kReverseAcceleration = 10.0f;
        constexpr float kBrakeDeceleration = 36.0f;
        constexpr float kCoastDeceleration = 2.5f;
        constexpr float kHandbrakeDeceleration = 48.0f;
        constexpr float kSteerDegreesPerSecond = 85.0f;
        constexpr float kIdleRPM = 900.0f;
        constexpr float kRedlineRPM = 6000.0f;

        float& speed = runtimeVehicle.arcadeSpeed;
        float& lateralSpeed = runtimeVehicle.arcadeLateralSpeed;
        const raceman::physics::VehicleTireGripConfig& tireGrip = runtimeVehicle.config.tireGrip;
        const float absSpeedBeforeDrive = std::fabs(speed);
        const float speedFactorBeforeDrive = (std::clamp)(absSpeedBeforeDrive / kMaxForwardSpeed, 0.0f, 1.0f);
        const float handbrakeGripScale = 1.0f - baseInput.handbrake * (1.0f - (std::clamp)(tireGrip.handbrakeGripScale, 0.0f, 1.0f));
        const float downforceGripScale = 1.0f + speedFactorBeforeDrive * speedFactorBeforeDrive * (std::max)(0.0f, tireGrip.downforceGripScale);
        const float effectiveSurfaceGrip = tireGrip.enabled ? (std::max)(0.05f, contactGripMultiplier) : 1.0f;
        const float effectiveWheelGrip = tireGrip.enabled ? (std::clamp)(contactWheelGripFactor, 0.2f, 4.0f) : 1.0f;
        const float effectiveLateralGrip =
            (std::max)(0.0f, tireGrip.lateralGrip) * effectiveSurfaceGrip * effectiveWheelGrip * downforceGripScale * handbrakeGripScale;
        const float slipAngle = glm::degrees(std::atan2(lateralSpeed, (std::max)(0.5f, absSpeedBeforeDrive)));
        const float slipLimit = (std::max)(0.1f, tireGrip.slipAngleLimit);
        const float slipOverLimit = tireGrip.enabled
            ? (std::max)(0.0f, (std::fabs(slipAngle) - slipLimit) / slipLimit)
            : 0.0f;
        const float cornerDemand = tireGrip.enabled
            ? std::fabs(baseInput.steering) * speedFactorBeforeDrive * speedFactorBeforeDrive
            : 0.0f;
        const float availableCornerGrip = tireGrip.enabled
            ? (std::max)(0.05f, effectiveLateralGrip / 5.0f)
            : 1.0f;
        const float cornerOverLimit = tireGrip.enabled
            ? (std::max)(0.0f, (cornerDemand - availableCornerGrip) / availableCornerGrip)
            : 0.0f;
        float tractionScale = 1.0f - (std::max)(slipOverLimit, cornerOverLimit) * (std::clamp)(tireGrip.slideGripLoss, 0.0f, 1.0f);
        tractionScale = (std::clamp)(tractionScale, (std::clamp)(tireGrip.minTractionScale, 0.0f, 1.0f), 1.0f);
        const float driveGripScale = tireGrip.enabled
            ? effectiveSurfaceGrip * (std::max)(0.0f, tireGrip.longitudinalGrip) * tractionScale
            : contactGripMultiplier;
        if (wantsForward && wantsReverseOrBrake) {
                speed = MoveTowards(speed, 0.0f, kBrakeDeceleration * driveGripScale * brakeAmount * deltaTime);
        } else if (wantsForward) {
            if (speed < -0.1f) {
                speed = MoveTowards(speed, 0.0f, kBrakeDeceleration * driveGripScale * throttleAmount * deltaTime);
            } else {
                speed += throttleAmount * kAcceleration * driveGripScale * deltaTime;
            }
        } else if (wantsReverseOrBrake) {
            if (speed > 0.75f) {
                speed = MoveTowards(speed, 0.0f, kBrakeDeceleration * driveGripScale * brakeAmount * deltaTime);
            } else {
                speed -= brakeAmount * kReverseAcceleration * driveGripScale * deltaTime;
            }
        } else {
            speed = MoveTowards(speed, 0.0f, kCoastDeceleration * deltaTime);
        }

        if (baseInput.handbrake > 0.0f) {
            speed = MoveTowards(speed, 0.0f, kHandbrakeDeceleration * driveGripScale * baseInput.handbrake * deltaTime);
        }
        speed = MoveTowards(speed, 0.0f, contactRollingDrag * deltaTime);
        speed = (std::clamp)(speed, -kMaxReverseSpeed, kMaxForwardSpeed);

        const float absSpeed = std::fabs(speed);
        const float speedForSteer = (std::clamp)(absSpeed / 3.0f, 0.0f, 1.0f);
        const float highSpeedSteerScale = 1.0f - (std::clamp)(absSpeed / kMaxForwardSpeed, 0.0f, 0.75f);
        const float gripSteerScale = (std::clamp)(effectiveSurfaceGrip * tractionScale, 0.15f, 1.5f);
        const float directionSign = speed < -0.1f ? -1.0f : 1.0f;
        runtimeVehicle.arcadeChassisWorld.rotationEuler.y -=
            baseInput.steering * kSteerDegreesPerSecond * speedForSteer * highSpeedSteerScale * gripSteerScale * directionSign * deltaTime;

        const raceman::physics::VehicleGroundContactConfig& groundContact = runtimeVehicle.config.groundContact;
        const glm::quat yawRotation = glm::angleAxis(glm::radians(runtimeVehicle.arcadeChassisWorld.rotationEuler.y), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 forward = yawRotation * glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec3 right = yawRotation * glm::vec3(1.0f, 0.0f, 0.0f);
        const float handbrakeSlipTarget = baseInput.steering * speed * baseInput.handbrake * 0.55f;
        const float surfaceSlipBoost = tireGrip.enabled ? (std::clamp)(1.2f - effectiveSurfaceGrip, 0.0f, 1.0f) : 0.0f;
        const float cornerSlipTarget = baseInput.steering * speed * cornerOverLimit * 0.18f;
        const float targetLateralSpeed = tireGrip.enabled
            ? (cornerSlipTarget + handbrakeSlipTarget) * (1.0f + surfaceSlipBoost)
            : 0.0f;
        const float lateralApproach = (std::max)(0.0f, tireGrip.recoveryRate) * (std::max)(0.25f, effectiveLateralGrip) * deltaTime;
        lateralSpeed = MoveTowards(lateralSpeed, targetLateralSpeed, lateralApproach);
        if (std::fabs(speed) < 0.1f && std::fabs(targetLateralSpeed) < 0.1f) {
            lateralSpeed = MoveTowards(lateralSpeed, 0.0f, effectiveLateralGrip * deltaTime);
        }

        runtimeVehicle.arcadeSlipAngle = glm::degrees(std::atan2(lateralSpeed, (std::max)(0.5f, std::fabs(speed))));
        runtimeVehicle.arcadeTractionScale = tractionScale;
        runtimeVehicle.arcadeSurfaceGrip = effectiveSurfaceGrip;

        glm::vec3 moveDelta = (forward * speed + right * lateralSpeed) * deltaTime;
        if (physicsWorld_ != nullptr && glm::length(moveDelta) > 0.0001f) {
            const std::unordered_set<std::string> ignoredVehicleObjectIds =
                BuildVehicleRaycastIgnoreSet(vehicleObject, runtimeVehicle.chassisBodyObjectId);
            const glm::vec3 moveDir = glm::normalize(moveDelta);
            const float obstacleSkin = (std::max)(0.0f, groundContact.obstacleSkin);
            PhysicsRaycastHit obstacleHit;
            if (physicsWorld_->RaycastIgnoring(
                    runtimeVehicle.arcadeChassisWorld.position + glm::vec3(0.0f, groundContact.obstacleProbeHeight, 0.0f),
                    moveDir,
                    glm::length(moveDelta) + obstacleSkin,
                    obstacleHit,
                    ignoredVehicleObjectIds) &&
                obstacleHit.hit &&
                obstacleHit.normal.y < groundContact.wallNormalYMax) {
                moveDelta = moveDir * (std::max)(0.0f, obstacleHit.distance - obstacleSkin);
                speed = 0.0f;
                lateralSpeed = 0.0f;
            }
        }

        runtimeVehicle.arcadeChassisWorld.position += moveDelta;
        runtimeVehicle.arcadeWheelContacts.resize(runtimeVehicle.config.wheels.size());
        if (groundContact.enabled && physicsWorld_ != nullptr && !runtimeVehicle.config.wheels.empty()) {
            const std::unordered_set<std::string> ignoredVehicleObjectIds =
                BuildVehicleRaycastIgnoreSet(vehicleObject, runtimeVehicle.chassisBodyObjectId);
            struct WheelHitSample {
                bool hit{false};
                glm::vec3 contactPosition{0.0f};
                glm::vec3 normal{0.0f, 1.0f, 0.0f};
                ColliderSurfaceConfig surface{MakeDefaultTrackSurfaceConfig(TrackSurfaceType::Asphalt)};
                float restLength{0.35f};
                float radius{0.35f};
                glm::vec3 localScene{0.0f};
                float targetMountY{0.0f};
            };

            std::vector<WheelHitSample> wheelHits(runtimeVehicle.config.wheels.size());
            float targetChassisYSum = 0.0f;
            int groundedWheelCount = 0;
            float frontHeightSum = 0.0f;
            float rearHeightSum = 0.0f;
            float leftHeightSum = 0.0f;
            float rightHeightSum = 0.0f;
            float frontLongitudinalSum = 0.0f;
            float rearLongitudinalSum = 0.0f;
            float leftLateralSum = 0.0f;
            float rightLateralSum = 0.0f;
            int frontCount = 0;
            int rearCount = 0;
            int leftCount = 0;
            int rightCount = 0;

            for (std::size_t wheelIndex = 0; wheelIndex < runtimeVehicle.config.wheels.size(); ++wheelIndex) {
                const raceman::physics::WheelConfig& wheel = runtimeVehicle.config.wheels[wheelIndex];
                WheelHitSample& sample = wheelHits[wheelIndex];
                sample.restLength = wheel.mountPosition.y >= 0.0f
                    ? runtimeVehicle.config.frontSuspension.restLength
                    : runtimeVehicle.config.rearSuspension.restLength;
                sample.restLength = (std::max)(0.01f, sample.restLength);
                sample.radius = (std::max)(0.05f, wheel.radius);
                sample.localScene = VehicleVectorToScene(wheel.mountPosition);

                const glm::vec3 wheelLocalWorld = yawRotation * sample.localScene;
                const glm::vec3 mountWorld = runtimeVehicle.arcadeChassisWorld.position + wheelLocalWorld;
                const float probeUp = (std::max)(0.0f, groundContact.probeUp);
                const float probeDistance = (std::max)(0.1f, probeUp + sample.restLength + sample.radius + (std::max)(0.0f, groundContact.extraProbeLength));

                PhysicsRaycastHit hit;
                if (!physicsWorld_->RaycastIgnoring(
                        mountWorld + glm::vec3(0.0f, probeUp, 0.0f),
                        glm::vec3(0.0f, -1.0f, 0.0f),
                        probeDistance,
                        hit,
                        ignoredVehicleObjectIds) ||
                    !hit.hit ||
                    hit.normal.y < groundContact.minGroundNormalY) {
                    continue;
                }

                sample.hit = true;
                sample.contactPosition = hit.position;
                sample.normal = glm::normalize(hit.normal);
                const int hitObjectIndex = FindObjectIndexById(hit.objectId);
                if (hitObjectIndex >= 0 && hitObjectIndex < static_cast<int>(objects_.size())) {
                    sample.surface = GetProjectTrackSurfaceSettings(objects_[hitObjectIndex].colliderSurface.type);
                }
                sample.targetMountY = hit.position.y + sample.radius + sample.restLength + groundContact.rideHeightOffset;
                targetChassisYSum += sample.targetMountY - wheelLocalWorld.y;
                ++groundedWheelCount;

                if (wheel.mountPosition.y >= 0.0f) {
                    frontHeightSum += sample.targetMountY;
                    frontLongitudinalSum += sample.localScene.z;
                    ++frontCount;
                } else {
                    rearHeightSum += sample.targetMountY;
                    rearLongitudinalSum += sample.localScene.z;
                    ++rearCount;
                }

                if (wheel.mountPosition.x >= 0.0f) {
                    rightHeightSum += sample.targetMountY;
                    rightLateralSum += sample.localScene.x;
                    ++rightCount;
                } else {
                    leftHeightSum += sample.targetMountY;
                    leftLateralSum += sample.localScene.x;
                    ++leftCount;
                }
            }

            if (groundedWheelCount > 0) {
                const float heightAlpha = SmoothingAlpha(groundContact.heightSmoothing, deltaTime);
                const float targetChassisY = targetChassisYSum / static_cast<float>(groundedWheelCount);
                runtimeVehicle.arcadeChassisWorld.position.y =
                    runtimeVehicle.arcadeChassisWorld.position.y + (targetChassisY - runtimeVehicle.arcadeChassisWorld.position.y) * heightAlpha;
                runtimeVehicle.arcadeVerticalVelocity = 0.0f;

                float targetPitch = 0.0f;
                float targetRoll = 0.0f;
                if (frontCount > 0 && rearCount > 0) {
                    const float frontHeight = frontHeightSum / static_cast<float>(frontCount);
                    const float rearHeight = rearHeightSum / static_cast<float>(rearCount);
                    const float frontZ = frontLongitudinalSum / static_cast<float>(frontCount);
                    const float rearZ = rearLongitudinalSum / static_cast<float>(rearCount);
                    targetPitch = -glm::degrees(std::atan2(frontHeight - rearHeight, (std::max)(0.01f, std::fabs(frontZ - rearZ))));
                }
                if (leftCount > 0 && rightCount > 0) {
                    const float leftHeight = leftHeightSum / static_cast<float>(leftCount);
                    const float rightHeight = rightHeightSum / static_cast<float>(rightCount);
                    const float leftX = leftLateralSum / static_cast<float>(leftCount);
                    const float rightX = rightLateralSum / static_cast<float>(rightCount);
                    targetRoll = glm::degrees(std::atan2(rightHeight - leftHeight, (std::max)(0.01f, std::fabs(rightX - leftX))));
                }

                const float tiltAlpha = SmoothingAlpha(groundContact.tiltSmoothing, deltaTime);
                runtimeVehicle.arcadeChassisWorld.rotationEuler.x += ShortestAngleDeltaDegrees(runtimeVehicle.arcadeChassisWorld.rotationEuler.x, targetPitch) * tiltAlpha;
                runtimeVehicle.arcadeChassisWorld.rotationEuler.z += ShortestAngleDeltaDegrees(runtimeVehicle.arcadeChassisWorld.rotationEuler.z, targetRoll) * tiltAlpha;
            } else {
                runtimeVehicle.arcadeVerticalVelocity -= (std::max)(0.0f, groundContact.airborneGravity) * deltaTime;
                runtimeVehicle.arcadeChassisWorld.position.y += runtimeVehicle.arcadeVerticalVelocity * deltaTime;
                const float tiltAlpha = SmoothingAlpha(groundContact.tiltSmoothing, deltaTime);
                runtimeVehicle.arcadeChassisWorld.rotationEuler.x += ShortestAngleDeltaDegrees(runtimeVehicle.arcadeChassisWorld.rotationEuler.x, 0.0f) * tiltAlpha;
                runtimeVehicle.arcadeChassisWorld.rotationEuler.z += ShortestAngleDeltaDegrees(runtimeVehicle.arcadeChassisWorld.rotationEuler.z, 0.0f) * tiltAlpha;
            }

            const glm::quat finalYawRotation = glm::angleAxis(glm::radians(runtimeVehicle.arcadeChassisWorld.rotationEuler.y), glm::vec3(0.0f, 1.0f, 0.0f));
            for (std::size_t wheelIndex = 0; wheelIndex < runtimeVehicle.config.wheels.size(); ++wheelIndex) {
                const WheelHitSample& sample = wheelHits[wheelIndex];
                RuntimeVehicleInstance::WheelContact& contact = runtimeVehicle.arcadeWheelContacts[wheelIndex];
                const glm::vec3 wheelLocalWorld = finalYawRotation * sample.localScene;
                const glm::vec3 mountWorld = runtimeVehicle.arcadeChassisWorld.position + wheelLocalWorld;
                contact.grounded = sample.hit;
                contact.normal = sample.hit ? sample.normal : glm::vec3(0.0f, 1.0f, 0.0f);
                contact.surfaceType = sample.surface.type;
                const ColliderSurfaceConfig& defaultSurface = GetProjectTrackSurfaceSettings(TrackSurfaceType::Asphalt);
                contact.surfaceGripMultiplier = sample.hit ? (std::max)(0.0f, sample.surface.gripMultiplier) : (std::max)(0.0f, defaultSurface.gripMultiplier);
                contact.surfaceRollingDrag = sample.hit ? (std::max)(0.0f, sample.surface.rollingDrag) : (std::max)(0.0f, defaultSurface.rollingDrag);
                contact.slipAngle = runtimeVehicle.arcadeSlipAngle;
                contact.tractionScale = runtimeVehicle.arcadeTractionScale;
                if (sample.hit) {
                    const float suspensionLength = (std::max)(0.0f, mountWorld.y - sample.contactPosition.y - sample.radius);
                    contact.suspensionTravel = (std::clamp)(sample.restLength - suspensionLength, 0.0f, sample.restLength);
                    contact.contactPosition = sample.contactPosition;
                    contact.wheelCenterPosition = sample.contactPosition + contact.normal * sample.radius;
                    const float compression = sample.restLength > 0.0f ? contact.suspensionTravel / sample.restLength : 0.0f;
                    contact.normalForce = 2500.0f + compression * 2500.0f;
                } else {
                    contact.suspensionTravel = 0.0f;
                    contact.normalForce = 0.0f;
                    contact.wheelCenterPosition = mountWorld + glm::vec3(0.0f, -(sample.restLength + sample.radius), 0.0f);
                    contact.contactPosition = contact.wheelCenterPosition - glm::vec3(0.0f, sample.radius, 0.0f);
                }
                contact.angularVelocity = speed / (std::max)(0.05f, sample.radius);
            }
        } else {
            runtimeVehicle.arcadeVerticalVelocity = 0.0f;
            for (std::size_t wheelIndex = 0; wheelIndex < runtimeVehicle.config.wheels.size(); ++wheelIndex) {
                const raceman::physics::WheelConfig& wheel = runtimeVehicle.config.wheels[wheelIndex];
                RuntimeVehicleInstance::WheelContact& contact = runtimeVehicle.arcadeWheelContacts[wheelIndex];
                const float restLength = wheel.mountPosition.y >= 0.0f
                    ? runtimeVehicle.config.frontSuspension.restLength
                    : runtimeVehicle.config.rearSuspension.restLength;
                const float radius = (std::max)(0.05f, wheel.radius);
                const glm::vec3 wheelLocalScene = VehicleVectorToScene(wheel.mountPosition);
                const glm::vec3 mountWorld = runtimeVehicle.arcadeChassisWorld.position + yawRotation * wheelLocalScene;
                contact.grounded = false;
                contact.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                contact.surfaceType = TrackSurfaceType::Asphalt;
                const ColliderSurfaceConfig& defaultSurface = GetProjectTrackSurfaceSettings(TrackSurfaceType::Asphalt);
                contact.surfaceGripMultiplier = (std::max)(0.0f, defaultSurface.gripMultiplier);
                contact.surfaceRollingDrag = (std::max)(0.0f, defaultSurface.rollingDrag);
                contact.slipAngle = runtimeVehicle.arcadeSlipAngle;
                contact.tractionScale = runtimeVehicle.arcadeTractionScale;
                contact.suspensionTravel = 0.0f;
                contact.normalForce = 0.0f;
                contact.wheelCenterPosition = mountWorld + glm::vec3(0.0f, -(restLength + radius), 0.0f);
                contact.contactPosition = contact.wheelCenterPosition - glm::vec3(0.0f, radius, 0.0f);
                contact.angularVelocity = speed / radius;
            }
        }
        const float finalAbsSpeed = std::fabs(speed);
        runtimeVehicle.arcadeWheelSpin += (speed / 0.3f) * deltaTime;
        runtimeVehicle.arcadeEngineRPM = (std::clamp)(kIdleRPM + (finalAbsSpeed / kMaxForwardSpeed) * (kRedlineRPM - kIdleRPM) * (0.45f + throttleAmount * 0.55f),
                                                      kIdleRPM,
                                                      kRedlineRPM);
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
            const float normalizedSpeed = (std::clamp)(finalAbsSpeed / kMaxForwardSpeed, 0.0f, 0.999f);
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

    for (RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles_) {
        if (runtimeVehicle.objectIndex < 0 || runtimeVehicle.objectIndex >= static_cast<int>(objects_.size())) {
            continue;
        }
        if (!IsObjectEffectivelyEnabled(runtimeVehicle.objectIndex)) {
            continue;
        }
        if (physicsWorld_ == nullptr) {
            continue;
        }
        (void)physicsWorld_;

        Transform runtimeChassisWorldTransform = InterpolateTransform(
            runtimeVehicle.arcadePreviousChassisWorld,
            runtimeVehicle.arcadeChassisWorld,
            renderAlpha);
        runtimeChassisWorldTransform.scale = glm::vec3(1.0f);
        const float renderWheelSpin = runtimeVehicle.arcadePreviousWheelSpin +
            (runtimeVehicle.arcadeWheelSpin - runtimeVehicle.arcadePreviousWheelSpin) * renderAlpha;

        ApplyWorldTransformToSceneObject(
            objects_,
            [this](const std::string& id) { return FindObjectIndexById(id); },
            [this](int index) { return GetObjectWorldMatrix(index); },
            runtimeVehicle.objectIndex,
            runtimeChassisWorldTransform,
            true);

        const glm::mat4 authoredVehicleWorldMatrix = GetObjectWorldMatrix(runtimeVehicle.objectIndex);
        Transform currentArcadeChassisWorld = runtimeVehicle.arcadeChassisWorld;
        currentArcadeChassisWorld.scale = glm::vec3(1.0f);
        const glm::mat4 currentArcadeChassisWorldMatrix = BuildTransformMatrix(currentArcadeChassisWorld);
        const glm::mat4 runtimeChassisWorldMatrix = BuildTransformMatrix(runtimeChassisWorldTransform);

        const std::size_t wheelCount = (std::min)(runtimeVehicle.config.wheels.size(), runtimeVehicle.wheelObjectIndices.size());
        for (std::size_t wheelIndex = 0; wheelIndex < wheelCount; ++wheelIndex) {
            const int objectIndex = runtimeVehicle.wheelObjectIndices[wheelIndex];
            if (objectIndex < 0 || objectIndex >= static_cast<int>(objects_.size())) {
                continue;
            }

            Transform wheelWorldTransform;
            const raceman::physics::WheelConfig& wheelConfig = runtimeVehicle.config.wheels[wheelIndex];
            const float suspensionRestLength = wheelConfig.mountPosition.y >= 0.0f
                ? runtimeVehicle.config.frontSuspension.restLength
                : runtimeVehicle.config.rearSuspension.restLength;
            const raceman::physics::Vector3 wheelCenterVehicle =
                wheelConfig.mountPosition + raceman::physics::Vector3{0.0f, 0.0f, -suspensionRestLength};
            const glm::vec3 wheelLocalScene = VehicleVectorToScene(wheelCenterVehicle);
            wheelWorldTransform.position = glm::vec3(authoredVehicleWorldMatrix * glm::vec4(wheelLocalScene, 1.0f));
            if (wheelIndex < runtimeVehicle.arcadeWheelContacts.size()) {
                const glm::vec3 contactRelative =
                    glm::vec3(glm::inverse(currentArcadeChassisWorldMatrix) * glm::vec4(runtimeVehicle.arcadeWheelContacts[wheelIndex].wheelCenterPosition, 1.0f));
                wheelWorldTransform.position = glm::vec3(runtimeChassisWorldMatrix * glm::vec4(contactRelative, 1.0f));
            }
            wheelWorldTransform.rotationEuler = runtimeChassisWorldTransform.rotationEuler;
            wheelWorldTransform.rotationEuler.x += glm::degrees(renderWheelSpin);
            wheelWorldTransform.scale = glm::vec3(1.0f);
            if (wheelIndex < runtimeVehicle.wheelBindings.size() && wheelIndex < runtimeVehicle.wheelAuthoredLocalTransforms.size()) {
                wheelWorldTransform = BuildWheelWorldTransformFromAuthoredLocal(
                    authoredVehicleWorldMatrix,
                    runtimeVehicle.wheelAuthoredLocalTransforms[wheelIndex],
                    runtimeChassisWorldTransform,
                    wheelWorldTransform,
                    runtimeVehicle.wheelBindings[wheelIndex]);
            }

            ApplyWorldTransformToSceneObject(
                objects_,
                [this](const std::string& id) { return FindObjectIndexById(id); },
                [this](int index) { return GetObjectWorldMatrix(index); },
                objectIndex,
                wheelWorldTransform,
                false);
        }
    }
}

void SceneEditor::ResetPhysicsVelocities() {
    for (SceneObject& object : objects_) {
        if (object.hasRigidbody) {
            object.rigidbody.velocity = {0.0f, 0.0f, 0.0f};
            object.rigidbody.angularVelocity = {0.0f, 0.0f, 0.0f};
        }
        if (object.hasCharacterController) {
            object.characterController.velocity = {0.0f, 0.0f, 0.0f};
            object.characterController.groundVelocity = {0.0f, 0.0f, 0.0f};
            object.characterController.moveInput = {0.0f, 0.0f, 0.0f};
            object.characterController.pendingJumpImpulse = 0.0f;
            object.characterController.grounded = false;
        }
    }
}

void SceneEditor::SetScriptsRunning(bool running) {
    if (scriptsRunning_ == running) {
        return;
    }
    runtimeSimulationAccumulator_ = 0.0f;
    // Don't start a new build while one is already in progress.
    if (running && playModeLoad_.phase != PlayModeLoadState::Phase::Idle) {
        return;
    }

    if (running) {
        if (!playModeScriptAssemblyReady_) {
            if (inputManager_ != nullptr) {
                inputManager_->SetWheelForceFeedbackActive(false);
                inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
            }
            ClearScriptRuntime();
            UnloadScriptAssembly();
            SaveCurrentScene();
            profilerStats_ = CollectProfilerStats();
            playModeSnapshot_ = {objects_, selectedIndex_, selectedIndices_};
            hasPlayModeSnapshot_ = true;
            activeViewport_ = SceneEditorActiveViewport::Game;
            scriptsPaused_ = false;
            SyncScriptProjectFiles(false);

            playModeLoad_ = {};
            playModeLoad_.scriptBuild = std::make_shared<PlayModeLoadState::ScriptBuildStatus>();
            playModeLoad_.buildStart = std::chrono::high_resolution_clock::now();
            auto status = playModeLoad_.scriptBuild;
            playModeLoad_.scriptBuildThread = std::make_unique<std::thread>([status]() {
                std::string error;
                const bool ok = BuildScriptAssembly(&error);
                {
                    std::lock_guard<std::mutex> lock(status->mutex);
                    status->error = std::move(error);
                }
                status->success.store(ok);
                status->isDone.store(true);
            });
            playModeLoad_.phase = PlayModeLoadState::Phase::BuildingScripts;
            return;
        }

        playModeScriptAssemblyReady_ = false;
        std::fprintf(stdout, "[Play] Building scene...\n");
        std::fflush(stdout);

        // scriptsRunning_ is set to true by TickPlayModeLoading once the background build completes.
        RebuildVehicleRuntime();
        std::vector<PhysicsBodyDesc> physicsBodies;
        std::vector<PhysicsCharacterDesc> physicsCharacters;
        std::unordered_map<std::string, PhysicsBodyDesc> vehicleChassisBodies;
        std::unordered_set<std::string> consumedVehiclePhysicsObjects;

        for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
            const SceneObject& object = objects_[objectIndex];
            if (!IsObjectEffectivelyEnabled(objectIndex) || !object.hasVehicle || !object.vehicle.enabled) {
                continue;
            }

            const Transform worldTransform = TransformFromMatrix(GetObjectWorldMatrix(objectIndex));
            raceman::physics::VehicleConfig chassisConfig = BuildDefaultJoltVehicleConfig();
            if (!object.vehicle.configPath.empty()) {
                try {
                    chassisConfig = raceman::physics::VehicleConfigLoader::loadFromFile(ProjectAssetPathToAbsolute(object.vehicle.configPath).string());
                } catch (...) {
                }
            }
            EnsureDrivableVehicleConfig(chassisConfig);
            chassisConfig.transmission.mode = raceman::physics::TransmissionConfig::Mode::Automatic;
            PhysicsBodyDesc body;
            body.objectId = MakeVehicleChassisBodyObjectId(object.id);
            body.collisionLayer = ClampPhysicsLayerIndex(object.physicsLayer);
            body.position = worldTransform.position;
            body.rotationEuler = worldTransform.rotationEuler;
            body.scale = worldTransform.scale;
            body.bodyType = PhysicsBodyType::Kinematic;
            body.mass = 1500.0f;
            body.useGravity = false;
            body.friction = 0.8f;
            body.restitution = 0.0f;
            body.linearDamping = 0.0f;
            // Allow roll/pitch tilt momentum — use moderate damping to prevent wild oscillation
            body.angularDamping = 0.05f;
            body.motionQuality = PhysicsMotionQuality::Continuous;  // prevent tunneling at high speed
            body.overrideCenterOfMass = false;
            body.overrideMassProperties = false;

            const glm::mat4 vehicleWorldMatrix = GetObjectWorldMatrix(objectIndex);
            if (AppendSupportedVehicleChassisColliders(object, glm::mat4(1.0f), body.colliders)) {
                consumedVehiclePhysicsObjects.insert(object.id);
            }

            for (const std::string& chassisObjectId : object.vehicle.chassisObjectIds) {
                const int candidateIndex = FindObjectIndexById(chassisObjectId);
                if (candidateIndex < 0 || candidateIndex == objectIndex) {
                    continue;
                }
                const SceneObject& candidate = objects_[candidateIndex];
                if (!IsObjectEffectivelyEnabled(candidateIndex) || !IsDescendantOf(candidate.id, object.id)) {
                    continue;
                }
                if (IsVehicleWheelHelperObject(object.vehicle, candidate.id)) {
                    consumedVehiclePhysicsObjects.insert(candidate.id);
                    continue;
                }

                const glm::mat4 relativeMatrix = glm::inverse(vehicleWorldMatrix) * GetObjectWorldMatrix(candidateIndex);
                if (AppendSupportedVehicleChassisColliders(candidate, relativeMatrix, body.colliders)) {
                    consumedVehiclePhysicsObjects.insert(candidate.id);
                }
            }

            if (body.colliders.empty()) {
                body.colliders.push_back(BuildDefaultVehicleChassisCollider(chassisConfig));
                std::fprintf(stdout,
                             "[VehicleDebug] Vehicle '%s' has no chassis collider; using generated default box chassis.\n",
                             object.id.c_str());
                std::fflush(stdout);
            }
            vehicleChassisBodies[object.id] = std::move(body);
        }

        for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
            const SceneObject& object = objects_[objectIndex];
            if (!IsObjectEffectivelyEnabled(objectIndex)) {
                continue;
            }

            if (consumedVehiclePhysicsObjects.find(object.id) != consumedVehiclePhysicsObjects.end() &&
                !(object.hasVehicle && object.vehicle.enabled)) {
                continue;
            }

            const Transform worldTransform = TransformFromMatrix(GetObjectWorldMatrix(objectIndex));
            if (object.hasCharacterController && object.characterController.enabled) {
                PhysicsCharacterDesc character;
                character.objectId = object.id;
                character.position = worldTransform.position;
                character.rotationEuler = worldTransform.rotationEuler;
                character.height = object.characterController.height;
                character.radius = object.characterController.radius;
                character.center = object.characterController.center;
                character.stepHeight = object.characterController.stepHeight;
                character.slopeLimitDegrees = object.characterController.slopeLimitDegrees;
                character.maxStrength = object.characterController.maxStrength;
                character.mass = object.characterController.mass;
                physicsCharacters.push_back(std::move(character));
                continue;
            }

            if (object.hasVehicle && object.vehicle.enabled) {
                auto chassisIt = vehicleChassisBodies.find(object.id);
                if (chassisIt != vehicleChassisBodies.end()) {
                    physicsBodies.push_back(chassisIt->second);
                }
                continue;
            }

            PhysicsBodyDesc body;
            body.objectId = object.id;
            body.collisionLayer = ClampPhysicsLayerIndex(object.physicsLayer);
            body.position = worldTransform.position;
            body.rotationEuler = worldTransform.rotationEuler;
            body.scale = worldTransform.scale;
            body.bodyType = PhysicsBodyType::Static;
            if (object.hasRigidbody && object.rigidbody.enabled && !(object.hasVehicle && object.vehicle.enabled)) {
                body.bodyType = object.rigidbody.bodyType == RigidbodyBodyType::Dynamic
                    ? PhysicsBodyType::Dynamic
                    : (object.rigidbody.bodyType == RigidbodyBodyType::Kinematic ? PhysicsBodyType::Kinematic : PhysicsBodyType::Static);
            }
            body.mass = object.hasRigidbody ? object.rigidbody.mass : 1.0f;
            body.useGravity = object.hasRigidbody ? object.rigidbody.useGravity : false;
            body.linearDamping = object.hasRigidbody ? object.rigidbody.linearDamping : 0.05f;
            body.angularDamping = object.hasRigidbody ? object.rigidbody.angularDamping : 0.05f;
            body.friction = object.hasRigidbody ? object.rigidbody.friction : 0.2f;
            body.restitution = object.hasRigidbody ? object.rigidbody.restitution : 0.0f;
            body.velocity = object.hasRigidbody ? object.rigidbody.velocity : glm::vec3{0.0f};
            body.angularVelocity = object.hasRigidbody ? object.rigidbody.angularVelocity : glm::vec3{0.0f};
            body.freezePositionX = object.hasRigidbody ? object.rigidbody.freezePositionX : false;
            body.freezePositionY = object.hasRigidbody ? object.rigidbody.freezePositionY : false;
            body.freezePositionZ = object.hasRigidbody ? object.rigidbody.freezePositionZ : false;
            body.freezeRotationX = object.hasRigidbody ? object.rigidbody.freezeRotationX : false;
            body.freezeRotationY = object.hasRigidbody ? object.rigidbody.freezeRotationY : false;
            body.freezeRotationZ = object.hasRigidbody ? object.rigidbody.freezeRotationZ : false;
            // Enable CCD for dynamic bodies so fast-moving objects don't tunnel through colliders
            if (body.bodyType == PhysicsBodyType::Dynamic) {
                body.motionQuality = PhysicsMotionQuality::Continuous;
            }

            const SceneColliderType colliderType = GetEnabledColliderType(object);
            if (colliderType == SceneColliderType::Box && object.boxCollider.enabled) {
                PhysicsColliderDesc collider;
                collider.type = PhysicsColliderType::Box;
                collider.isTrigger = object.boxCollider.isTrigger;
                collider.center = object.boxCollider.center;
                collider.size = object.boxCollider.size;
                body.colliders.push_back(collider);
            }
            if (colliderType == SceneColliderType::Sphere && object.sphereCollider.enabled) {
                PhysicsColliderDesc collider;
                collider.type = PhysicsColliderType::Sphere;
                collider.isTrigger = object.sphereCollider.isTrigger;
                collider.center = object.sphereCollider.center;
                collider.radius = object.sphereCollider.radius;
                body.colliders.push_back(collider);
            }
            if (colliderType == SceneColliderType::Capsule && object.capsuleCollider.enabled) {
                PhysicsColliderDesc collider;
                collider.type = PhysicsColliderType::Capsule;
                collider.isTrigger = object.capsuleCollider.isTrigger;
                collider.center = object.capsuleCollider.center;
                collider.radius = object.capsuleCollider.radius;
                collider.height = object.capsuleCollider.height;
                body.colliders.push_back(collider);
            }
            if (colliderType == SceneColliderType::Plane && object.planeCollider.enabled) {
                PhysicsColliderDesc collider;
                collider.type = PhysicsColliderType::Plane;
                collider.isTrigger = object.planeCollider.isTrigger;
                collider.normal = object.planeCollider.normal;
                collider.offset = object.planeCollider.offset;
                collider.infinite = object.planeCollider.infinite;
                collider.halfExtent = object.planeCollider.halfExtent;
                body.colliders.push_back(collider);
            }
            if (colliderType == SceneColliderType::Mesh && object.meshCollider.enabled && object.hasMeshFilter && !object.meshFilter.sourcePath.empty()) {
                PhysicsColliderDesc collider;
                collider.type = PhysicsColliderType::Mesh;
                collider.isTrigger = object.meshCollider.isTrigger;
                collider.meshAssetPath = object.meshFilter.sourcePath;
                collider.meshIndex = object.meshFilter.meshIndex;
                collider.meshName = object.meshFilter.meshName;
                collider.meshPivotOffset = object.meshFilter.pivotOffset;
                collider.meshBuildQuality = object.meshCollider.buildQuality;
                collider.meshMode = object.meshCollider.mode;
                body.colliders.push_back(collider);
            }
            if (!body.colliders.empty()) {
                physicsBodies.push_back(std::move(body));
            }
        }
        // Launch async build on background thread.
        std::fprintf(stdout, "[Play] Runtime physics descriptors: %zu bodies, %zu characters, 0 Jolt vehicles (arcade vehicle runtime).\n",
                     physicsBodies.size(),
                     physicsCharacters.size());
        std::fflush(stdout);
        if (IsEnvironmentFlagEnabled("RACEMAN_DISABLE_PLAYER_PHYSICS")) {
            std::fprintf(stdout, "[Play] Player physics disabled by RACEMAN_DISABLE_PLAYER_PHYSICS=1.\n");
            std::fflush(stdout);
            physicsWorld_.reset();
            playModeLoad_ = {};
            RebuildVehicleRuntime();
            RebuildAudioRuntime();
            RebuildScriptRuntime();
            scriptsRunning_ = true;
            if (inputManager_ != nullptr) {
                inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
                inputManager_->SetWheelForceFeedbackActive(false);
            }
            return;
        }
        std::fprintf(stdout, "[Play] Creating physics world...\n");
        std::fflush(stdout);
        playModeLoad_.pendingWorld = std::make_unique<PhysicsWorld>(physicsLayerCollisionMatrix_);
        playModeLoad_.progress = std::make_shared<PhysicsBuildProgress>();
        playModeLoad_.progress->stepsTotal.store(static_cast<int>(physicsBodies.size()));
        playModeLoad_.buildStart = std::chrono::high_resolution_clock::now();

        PhysicsWorld* worldPtr = playModeLoad_.pendingWorld.get();
        PhysicsBuildProgress* progressPtr = playModeLoad_.progress.get();
        std::fprintf(stdout, "[Play] Starting physics build thread...\n");
        std::fflush(stdout);
        playModeLoad_.buildThread = std::make_unique<std::thread>(
            [worldPtr, progressPtr,
             bodies  = std::move(physicsBodies),
             chars   = std::move(physicsCharacters)]() mutable {
                std::fprintf(stdout, "[Play] Physics build thread started.\n");
                std::fflush(stdout);
                worldPtr->Build(bodies, chars, progressPtr);
            });
        playModeLoad_.phase = PlayModeLoadState::Phase::BuildingPhysics;
        // TickPlayModeLoading() will finalize once the thread completes.
    } else {
        if (inputManager_ != nullptr) {
            inputManager_->SetWheelForceFeedbackActive(false);
            inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
        }
        scriptsRunning_ = false;
        scriptsPaused_ = false;
        playModeScriptAssemblyReady_ = false;
        // Play EngineStop triggers before clearing audio runtime.
        if (audioManager_ && audioManager_->IsInitialized()) {
            for (auto& inst : runtimeVehicleSounds_) {
                for (const auto& trig : inst.profile.triggerSounds) {
                    if (trig.trigger == VehicleSoundTrigger::EngineStop && !trig.clipPath.empty()) {
                        const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                        irrklang::ISound* s = audioManager_->Play2D(p, false, false);
                        if (s) { s->setVolume(trig.volume); s->drop(); }
                        break;
                    }
                }
            }
        }
        ClearAudioRuntime();
        ClearScriptRuntime();
        UnloadScriptAssembly();
        runtimeVehicles_.clear();
        runtimeCinemachineStates_.clear();
        if (physicsWorld_) {
            physicsWorld_->Clear();
            physicsWorld_.reset();
        }
        if (hasPlayModeSnapshot_) {
            objects_ = playModeSnapshot_.objects;
            selectedIndex_ = playModeSnapshot_.selectedIndex;
            selectedIndices_ = playModeSnapshot_.selectedIndices;
            NormalizeSelection();
            playModeSnapshot_ = {};
            hasPlayModeSnapshot_ = false;
        } else {
            ResetPhysicsVelocities();
        }
        activeViewport_ = SceneEditorActiveViewport::Scene;
        activeGizmoAxis_ = -1;
        hoveredGizmoAxis_ = -1;
        if (console_) {
            console_->AddLog("Play mode stopped.");
        }
        profilerStats_ = CollectProfilerStats();
    }
}

void SceneEditor::SetScriptsPaused(bool paused) {
    if (!scriptsRunning_ || scriptsPaused_ == paused) {
        return;
    }

    scriptsPaused_ = paused;
    runtimeSimulationAccumulator_ = 0.0f;
    if (!scriptsPaused_) {
        activeViewport_ = SceneEditorActiveViewport::Game;
    }
    if (console_) {
        console_->AddLog(scriptsPaused_ ? "Play mode paused." : "Play mode resumed.");
    }
}

void SceneEditor::ClearScriptRuntime() {
    runtimeScripts_.clear();
}

void SceneEditor::RestoreFromPlayModeSnapshot() {
    if (hasPlayModeSnapshot_) {
        objects_ = playModeSnapshot_.objects;
        selectedIndex_ = playModeSnapshot_.selectedIndex;
        selectedIndices_ = playModeSnapshot_.selectedIndices;
        NormalizeSelection();
        playModeSnapshot_ = {};
        hasPlayModeSnapshot_ = false;
    }
    activeViewport_ = SceneEditorActiveViewport::Scene;
    playModeScriptAssemblyReady_ = false;
    if (inputManager_ != nullptr) {
        inputManager_->SetWheelForceFeedbackActive(false);
        inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
    }
}

void SceneEditor::TickPlayModeLoading() {
    if (playModeLoad_.phase == PlayModeLoadState::Phase::Idle) {
        return;
    }

    if (playModeLoad_.phase == PlayModeLoadState::Phase::BuildingScripts) {
        auto status = playModeLoad_.scriptBuild;
        if (!status || !status->isDone.load()) {
            return;
        }

        if (playModeLoad_.scriptBuildThread && playModeLoad_.scriptBuildThread->joinable()) {
            playModeLoad_.scriptBuildThread->join();
        }

        std::string error;
        {
            std::lock_guard<std::mutex> lock(status->mutex);
            error = status->error;
        }

        if (!status->success.load()) {
            playModeLoad_ = {};
            RestoreFromPlayModeSnapshot();
            const std::string message = error.empty() ? "Script DLL build failed. Check the build output for compiler errors." : error;
            if (console_) {
                console_->AddError(message);
            }
            std::fprintf(stdout, "[Play] Script build failed: %s\n", message.c_str());
            std::fflush(stdout);
            return;
        }

        std::string loadError;
        if (!LoadScriptAssembly(&loadError)) {
            playModeLoad_ = {};
            RestoreFromPlayModeSnapshot();
            const std::string message = loadError.empty() ? "Script DLL load failed." : loadError;
            if (console_) {
                console_->AddError(message);
            }
            std::fprintf(stdout, "[Play] Script load failed: %s\n", message.c_str());
            std::fflush(stdout);
            return;
        }

        if (console_) {
            console_->AddLog("Loaded script DLL with " + std::to_string(GetRegisteredScripts().size()) + " script(s).");
        }

        playModeLoad_ = {};
        playModeScriptAssemblyReady_ = true;
        SetScriptsRunning(true);
        return;
    }

    auto* prog = playModeLoad_.progress.get();
    if (!prog || !prog->isDone.load()) {
        return; // still building
    }

    // Build thread has signalled completion — join it.
    if (playModeLoad_.buildThread && playModeLoad_.buildThread->joinable()) {
        playModeLoad_.buildThread->join();
    }

    const bool cancelled = prog->wasCancelled.load();

    if (cancelled) {
        // Discard the partially-built world and restore editor state.
        playModeLoad_ = {};
        RestoreFromPlayModeSnapshot();
        std::fprintf(stdout, "[Play] Build cancelled.\n");
        std::fflush(stdout);
        return;
    }

    // Build succeeded — transfer ownership to the main physics world.
    physicsWorld_ = std::move(playModeLoad_.pendingWorld);

    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - playModeLoad_.buildStart).count();
    std::fprintf(stdout, "[Play] Build complete in %.1f ms\n", ms);
    std::fflush(stdout);

    playModeLoad_ = {}; // reset (clears phase to Idle, closes popup next frame)

    RebuildVehicleRuntime();
    RebuildAudioRuntime();
    RebuildScriptRuntime();
    scriptsRunning_ = true;
    if (inputManager_ != nullptr) {
        inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
        inputManager_->SetWheelForceFeedbackActive(true);
    }

    if (console_) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Physics ready (%.1f s). Running.", ms / 1000.0);
        console_->AddLog(buf);
    }
}

void SceneEditor::RenderPlayModeLoadingPopup() {
    if (playModeLoad_.phase == PlayModeLoadState::Phase::Idle) {
        return;
    }

    // Keep the modal open as long as building is in progress.
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f, 124.0f), ImGuiCond_Always);
    ImGui::OpenPopup("###PlayModeLoading");

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.105f, 0.118f, 0.138f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
    if (ImGui::BeginPopupModal("Building Scene...###PlayModeLoading", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {

        if (playModeLoad_.phase == PlayModeLoadState::Phase::BuildingScripts) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::high_resolution_clock::now() - playModeLoad_.buildStart).count();
            const float pulse = static_cast<float>(std::fmod(elapsed * 0.45, 1.0));

            ImGui::TextUnformatted("Building script DLL...");
            ImGui::ProgressBar(pulse, ImVec2(-1.0f, 0.0f), "ProjectScripts.dll");
            ImGui::TextDisabled("Compiling C++ scripts with MSBuild.");
            ImGui::BeginDisabled();
            ImGui::Button("Cancel", ImVec2(100.0f, 0.0f));
            ImGui::EndDisabled();
        } else {
            auto* prog = playModeLoad_.progress.get();
            if (prog) {
                const int done  = prog->stepsDone.load();
                const int total = prog->stepsTotal.load();
                const float fraction = (total > 0) ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;

                ImGui::TextUnformatted("Preparing or baking collision geometry...");

                char label[32];
                std::snprintf(label, sizeof(label), "%d / %d", done, (std::max)(total, 1));
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label);

                const std::string task = prog->GetTask();
                if (!task.empty()) {
                    ImGui::TextDisabled("%s", task.c_str());
                }

                if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
                    prog->cancelRequested.store(true);
                }
            }
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

void SceneEditor::StartCollisionBake(std::vector<std::pair<PhysicsColliderDesc, std::string>> jobs, std::string title) {
    if (jobs.empty()) {
        return;
    }
    if (collisionBake_.active) {
        if (console_) {
            console_->AddWarning("Collision bake is already running.");
        }
        return;
    }

    if (collisionBake_.thread && collisionBake_.thread->joinable()) {
        collisionBake_.thread->join();
    }

    collisionBake_.active = true;
    collisionBake_.title = title.empty() ? "Baking Collision" : std::move(title);
    collisionBake_.progress = std::make_shared<PhysicsBuildProgress>();
    collisionBake_.progress->stepsDone.store(0);
    collisionBake_.progress->stepsTotal.store(static_cast<int>(jobs.size()));
    collisionBake_.progress->SetTask("Preparing...");
    collisionBake_.start = std::chrono::high_resolution_clock::now();
    collisionBake_.bakedCount.store(0);
    collisionBake_.failedCount.store(0);
    {
        std::lock_guard<std::mutex> lock(collisionBake_.mutex);
        collisionBake_.lastError.clear();
    }

    auto progress = collisionBake_.progress;
    auto* bakedCount = &collisionBake_.bakedCount;
    auto* failedCount = &collisionBake_.failedCount;
    auto* errorMutex = &collisionBake_.mutex;
    auto* lastError = &collisionBake_.lastError;
    collisionBake_.thread = std::make_unique<std::thread>(
        [jobs = std::move(jobs), progress, bakedCount, failedCount, errorMutex, lastError]() {
            for (std::size_t i = 0; i < jobs.size(); ++i) {
                if (progress->cancelRequested.load()) {
                    progress->wasCancelled.store(true);
                    break;
                }

                const std::string label = jobs[i].second.empty()
                    ? ("Mesh " + std::to_string(i + 1))
                    : jobs[i].second;
                progress->stepsDone.store(static_cast<int>(i));
                progress->SetTask("Baking: " + label);

                CollisionShapeCacheInfo info;
                const bool baked = PhysicsWorld::BakeCollisionShape(jobs[i].first, &info);
                if (baked) {
                    bakedCount->fetch_add(1);
                } else {
                    failedCount->fetch_add(1);
                    std::lock_guard<std::mutex> lock(*errorMutex);
                    *lastError = label + (info.message.empty() ? "" : (": " + info.message));
                }
            }

            progress->stepsDone.store(static_cast<int>(jobs.size()));
            progress->SetTask(progress->wasCancelled.load() ? "Cancelled." : "Done.");
            progress->isDone.store(true);
        });
}

void SceneEditor::TickCollisionBake() {
    if (!collisionBake_.active || !collisionBake_.progress || !collisionBake_.progress->isDone.load()) {
        return;
    }

    if (collisionBake_.thread && collisionBake_.thread->joinable()) {
        collisionBake_.thread->join();
    }

    const bool cancelled = collisionBake_.progress->wasCancelled.load();
    const int bakedCount = collisionBake_.bakedCount.load();
    const int failedCount = collisionBake_.failedCount.load();
    std::string lastError;
    {
        std::lock_guard<std::mutex> lock(collisionBake_.mutex);
        lastError = collisionBake_.lastError;
    }

    if (console_) {
        if (cancelled) {
            console_->AddWarning("Collision bake cancelled.");
        } else if (failedCount > 0) {
            console_->AddWarning("Collision bake finished: " + std::to_string(bakedCount) + " baked, " +
                                 std::to_string(failedCount) + " failed." +
                                 (lastError.empty() ? "" : (" Last error: " + lastError)));
        } else {
            console_->AddLog("Collision bake complete: " + std::to_string(bakedCount) + " mesh" +
                             (bakedCount == 1 ? "" : "es") + ".");
        }
    }

    collisionBake_.active = false;
    collisionBake_.title.clear();
    collisionBake_.progress.reset();
    collisionBake_.thread.reset();
}

void SceneEditor::RenderCollisionBakeInlineStatus() {
    if (!collisionBake_.active || !collisionBake_.progress) {
        return;
    }

    PhysicsBuildProgress* progress = collisionBake_.progress.get();
    const int done = progress->stepsDone.load();
    const int total = (std::max)(progress->stepsTotal.load(), 1);
    const float fraction = static_cast<float>(done) / static_cast<float>(total);

    ImGui::PushID("CollisionBakeInlineStatus");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted(collisionBake_.title.empty() ? "Baking collision cache" : collisionBake_.title.c_str());
    char label[32];
    std::snprintf(label, sizeof(label), "%d / %d", done, total);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label);

    const std::string task = progress->GetTask();
    if (!task.empty()) {
        ImGui::TextDisabled("%s", task.c_str());
    }

    if (ImGui::SmallButton("Cancel")) {
        progress->cancelRequested.store(true);
    }
    ImGui::PopID();
}

void SceneEditor::RebuildVehicleRuntime() {
    runtimeVehicles_.clear();
    int candidateCount = 0;

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        const SceneObject& object = objects_[objectIndex];
        if (!object.hasVehicle || !object.vehicle.enabled || !IsObjectEffectivelyEnabled(objectIndex)) {
            continue;
        }
        ++candidateCount;

        try {
            raceman::physics::VehicleConfig config = BuildDefaultJoltVehicleConfig();
            if (!object.vehicle.configPath.empty()) {
                try {
                    const fs::path configPath = ProjectAssetPathToAbsolute(object.vehicle.configPath);
                    config = raceman::physics::VehicleConfigLoader::loadFromFile(configPath.string());
                } catch (const std::exception& ex) {
                    if (console_) {
                        console_->AddWarning("Vehicle config load failed for '" + object.name + "', using default Jolt vehicle: " + ex.what());
                    }
                    std::fprintf(stdout,
                                 "[VehicleDebug] Config load failed for '%s' path='%s'; using default Jolt vehicle: %s\n",
                                 object.name.c_str(),
                                 object.vehicle.configPath.c_str(),
                                 ex.what());
                    std::fflush(stdout);
                }
            }
            EnsureDrivableVehicleConfig(config);
            config.transmission.mode = raceman::physics::TransmissionConfig::Mode::Automatic;
            RuntimeVehicleInstance runtimeVehicle;
            runtimeVehicle.objectId = object.id;
            runtimeVehicle.objectIndex = objectIndex;
            runtimeVehicle.chassisBodyObjectId = MakeVehicleChassisBodyObjectId(object.id);
            runtimeVehicle.manualGear = 0;
            runtimeVehicle.arcadeChassisWorld = TransformFromMatrix(GetObjectWorldMatrix(objectIndex));
            runtimeVehicle.arcadePreviousChassisWorld = runtimeVehicle.arcadeChassisWorld;
            runtimeVehicle.arcadePreviousWheelSpin = runtimeVehicle.arcadeWheelSpin;
            runtimeVehicle.arcadeInitialized = true;
            runtimeVehicle.arcadeWheelContacts.resize(config.wheels.size());

            runtimeVehicle.wheelObjectIndices.reserve(config.wheels.size());
            runtimeVehicle.wheelBindings.reserve(config.wheels.size());
            runtimeVehicle.wheelAuthoredLocalTransforms.reserve(config.wheels.size());
            runtimeVehicle.wheelAuthoredRotationEuler.reserve(config.wheels.size());
            const glm::mat4 vehicleWorldMatrix = GetObjectWorldMatrix(objectIndex);
            for (std::size_t wheelConfigIndex = 0; wheelConfigIndex < config.wheels.size(); ++wheelConfigIndex) {
                raceman::physics::WheelConfig& wheelConfig = config.wheels[wheelConfigIndex];
                int wheelObjectIndex = -1;
                VehicleWheelBinding runtimeBinding;
                runtimeBinding.wheelName = wheelConfig.name;
                Transform authoredLocalTransform;
                glm::vec3 authoredRotationEuler{0.0f};
                const auto bindingIt = std::find_if(object.vehicle.wheelBindings.begin(), object.vehicle.wheelBindings.end(),
                    [&](const VehicleWheelBinding& binding) {
                        return binding.wheelName == wheelConfig.name;
                    });
                if (bindingIt != object.vehicle.wheelBindings.end()) {
                    wheelObjectIndex = FindObjectIndexById(bindingIt->objectId);
                    runtimeBinding = *bindingIt;
                }
                if (wheelObjectIndex >= 0 && wheelObjectIndex < static_cast<int>(objects_.size())) {
                    const glm::mat4 wheelRelativeMatrix = glm::inverse(vehicleWorldMatrix) * GetObjectWorldMatrix(wheelObjectIndex);
                    const Transform wheelRelativeTransform = TransformFromMatrix(wheelRelativeMatrix);
                    authoredLocalTransform = wheelRelativeTransform;
                    authoredRotationEuler = wheelRelativeTransform.rotationEuler;

                    const raceman::physics::Vector3 wheelCenterVehicle = SceneVectorToVehicle(wheelRelativeTransform.position);
                    const float suspensionRestLength = wheelCenterVehicle.y >= 0.0f
                        ? config.frontSuspension.restLength
                        : config.rearSuspension.restLength;
                    wheelConfig.mountPosition = wheelCenterVehicle + raceman::physics::Vector3{0.0f, 0.0f, suspensionRestLength};
                }
                runtimeVehicle.wheelObjectIndices.push_back(wheelObjectIndex);
                runtimeVehicle.wheelBindings.push_back(std::move(runtimeBinding));
                runtimeVehicle.wheelAuthoredLocalTransforms.push_back(authoredLocalTransform);
                runtimeVehicle.wheelAuthoredRotationEuler.push_back(authoredRotationEuler);
            }
            runtimeVehicle.config = config;

            runtimeVehicles_.push_back(std::move(runtimeVehicle));
            std::fprintf(stdout,
                         "[Vehicle] Loaded '%s' config='%s' with %zu wheels, chassisBody=%s\n",
                         object.name.c_str(),
                         object.vehicle.configPath.empty() ? "<default>" : object.vehicle.configPath.c_str(),
                         config.wheels.size(),
                         runtimeVehicles_.back().chassisBodyObjectId.empty() ? "<none>" : runtimeVehicles_.back().chassisBodyObjectId.c_str());
            std::fflush(stdout);
        } catch (const std::exception& ex) {
            if (console_) {
                console_->AddWarning("Vehicle runtime load failed for '" + object.name + "': " + ex.what());
            }
            std::fprintf(stdout, "[Vehicle] Runtime load failed for '%s': %s\n", object.name.c_str(), ex.what());
            std::fflush(stdout);
        }
    }

    std::fprintf(stdout, "[Vehicle] Runtime vehicles: %zu/%d\n", runtimeVehicles_.size(), candidateCount);
    std::fflush(stdout);
}

int SceneEditor::HotReloadRuntimeVehiclesForConfig(const std::string& configPath, const physics::VehicleConfig& config) {
    if (!scriptsRunning_ || runtimeVehicles_.empty()) {
        return 0;
    }

    const std::string normalizedConfigPath = NormalizeSlashes(configPath);
    int reloadedCount = 0;
    for (RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles_) {
        if (runtimeVehicle.objectIndex < 0 || runtimeVehicle.objectIndex >= static_cast<int>(objects_.size())) {
            continue;
        }

        const SceneObject& object = objects_[runtimeVehicle.objectIndex];
        if (!object.hasVehicle || NormalizeSlashes(object.vehicle.configPath) != normalizedConfigPath) {
            continue;
        }

        physics::VehicleConfig runtimeConfig = config;
        EnsureDrivableVehicleConfig(runtimeConfig);
        runtimeConfig.transmission.mode = physics::TransmissionConfig::Mode::Automatic;

        std::vector<int> wheelObjectIndices;
        std::vector<VehicleWheelBinding> wheelBindings;
        std::vector<Transform> wheelAuthoredLocalTransforms;
        std::vector<glm::vec3> wheelAuthoredRotationEuler;
        wheelObjectIndices.reserve(runtimeConfig.wheels.size());
        wheelBindings.reserve(runtimeConfig.wheels.size());
        wheelAuthoredLocalTransforms.reserve(runtimeConfig.wheels.size());
        wheelAuthoredRotationEuler.reserve(runtimeConfig.wheels.size());

        const glm::mat4 vehicleWorldMatrix = GetObjectWorldMatrix(runtimeVehicle.objectIndex);
        for (std::size_t wheelConfigIndex = 0; wheelConfigIndex < runtimeConfig.wheels.size(); ++wheelConfigIndex) {
            physics::WheelConfig& wheelConfig = runtimeConfig.wheels[wheelConfigIndex];
            int wheelObjectIndex = -1;
            VehicleWheelBinding runtimeBinding;
            runtimeBinding.wheelName = wheelConfig.name;
            Transform authoredLocalTransform;
            glm::vec3 authoredRotationEuler{0.0f};

            const auto bindingIt = std::find_if(object.vehicle.wheelBindings.begin(), object.vehicle.wheelBindings.end(),
                [&](const VehicleWheelBinding& binding) {
                    return binding.wheelName == wheelConfig.name;
                });
            if (bindingIt != object.vehicle.wheelBindings.end()) {
                wheelObjectIndex = FindObjectIndexById(bindingIt->objectId);
                runtimeBinding = *bindingIt;
            }

            if (wheelObjectIndex >= 0 && wheelObjectIndex < static_cast<int>(objects_.size())) {
                const glm::mat4 wheelRelativeMatrix = glm::inverse(vehicleWorldMatrix) * GetObjectWorldMatrix(wheelObjectIndex);
                const Transform wheelRelativeTransform = TransformFromMatrix(wheelRelativeMatrix);
                authoredLocalTransform = wheelRelativeTransform;
                authoredRotationEuler = wheelRelativeTransform.rotationEuler;

                const physics::Vector3 wheelCenterVehicle = SceneVectorToVehicle(wheelRelativeTransform.position);
                const float suspensionRestLength = wheelCenterVehicle.y >= 0.0f
                    ? runtimeConfig.frontSuspension.restLength
                    : runtimeConfig.rearSuspension.restLength;
                wheelConfig.mountPosition = wheelCenterVehicle + physics::Vector3{0.0f, 0.0f, suspensionRestLength};
            }

            wheelObjectIndices.push_back(wheelObjectIndex);
            wheelBindings.push_back(std::move(runtimeBinding));
            wheelAuthoredLocalTransforms.push_back(authoredLocalTransform);
            wheelAuthoredRotationEuler.push_back(authoredRotationEuler);
        }

        runtimeVehicle.config = std::move(runtimeConfig);
        runtimeVehicle.wheelObjectIndices = std::move(wheelObjectIndices);
        runtimeVehicle.wheelBindings = std::move(wheelBindings);
        runtimeVehicle.wheelAuthoredLocalTransforms = std::move(wheelAuthoredLocalTransforms);
        runtimeVehicle.wheelAuthoredRotationEuler = std::move(wheelAuthoredRotationEuler);
        runtimeVehicle.arcadeWheelContacts.resize(runtimeVehicle.config.wheels.size());
        runtimeVehicle.arcadeGear = (std::clamp)(
            runtimeVehicle.arcadeGear,
            -1,
            (std::max)(1, static_cast<int>(runtimeVehicle.config.transmission.gearRatios.size())));
        ++reloadedCount;
    }

    if (reloadedCount > 0) {
        std::fprintf(stdout,
                     "[Vehicle] Hot-reloaded config '%s' for %d runtime vehicle(s).\n",
                     normalizedConfigPath.c_str(),
                     reloadedCount);
        std::fflush(stdout);
    }
    return reloadedCount;
}

bool SceneEditor::SyncAttachmentScriptFields(ObjectScriptAttachment& attachment) {
    if (attachment.scriptName.empty()) {
        return false;
    }
    if (FindRegisteredScript(attachment.scriptName) == nullptr) {
        return false;
    }
    const std::vector<ScriptFieldDefinition> definitions = GetRegisteredScriptFieldDefinitions(attachment.scriptName);
    if (definitions.empty()) {
        const bool changed = !attachment.fields.empty();
        attachment.fields.clear();
        return changed;
    }
    return SyncScriptAttachmentFields(attachment, definitions);
}

void SceneEditor::RebuildScriptRuntime() {
    ClearScriptRuntime();

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        SceneObject& object = objects_[objectIndex];
        if (!IsObjectEffectivelyEnabled(objectIndex) || !object.hasScriptComponent || !object.scriptComponent.enabled) {
            continue;
        }
        for (std::size_t i = 0; i < object.scriptComponent.attachments.size(); ++i) {
            const ObjectScriptAttachment& attachment = object.scriptComponent.attachments[i];
            if (!attachment.enabled || attachment.scriptName.empty()) {
                continue;
            }

            std::unique_ptr<IObjectScript> instance = CreateRegisteredScript(attachment.scriptName);
            if (!instance) {
                if (console_) {
                    console_->AddWarning("Script not registered, rebuild may be required: " + attachment.scriptName);
                }
                continue;
            }
            SyncAttachmentScriptFields(object.scriptComponent.attachments[i]);

            RuntimeScriptInstance runtimeScript;
            runtimeScript.objectId = object.id;
            runtimeScript.attachmentIndex = i;
            runtimeScript.instance = std::move(instance);
            runtimeScripts_.push_back(std::move(runtimeScript));
        }
    }
}

// ---------------------------------------------------------------------------
// Audio runtime
// ---------------------------------------------------------------------------

static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void SceneEditor::ClearAudioRuntime() {
    // Stop and drop all audio source sounds.
    for (auto& inst : runtimeAudioSources_) {
        if (inst.sound) {
            inst.sound->stop();
            inst.sound->drop();
            inst.sound = nullptr;
        }
    }
    runtimeAudioSources_.clear();

    // Stop and drop all vehicle sound layers.
    for (auto& inst : runtimeVehicleSounds_) {
        for (auto& layer : inst.layers) {
            if (layer.sound) {
                layer.sound->stop();
                layer.sound->drop();
                layer.sound = nullptr;
            }
        }
    }
    runtimeVehicleSounds_.clear();
}

void SceneEditor::RebuildAudioRuntime() {
    ClearAudioRuntime();
    if (!audioManager_ || !audioManager_->IsInitialized()) return;

    // -- Audio sources with PlayOnAwake --
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const SceneObject& obj = objects_[i];
        if (!IsObjectEffectivelyEnabled(i) || !obj.hasAudioSource || !obj.audioSource.enabled) continue;
        if (obj.audioSource.clipPath.empty() || !obj.audioSource.playOnAwake) continue;

        const std::string absPath = ProjectAssetPathToAbsolute(obj.audioSource.clipPath).string();
        const glm::vec3 pos = GetObjectWorldPosition(i);
        const bool is3D = obj.audioSource.spatialBlend > 0.5f;
        irrklang::ISound* snd = is3D
            ? audioManager_->Play3D(absPath, pos, obj.audioSource.loop, /*paused=*/false)
            : audioManager_->Play2D(absPath, obj.audioSource.loop, /*paused=*/false);
        if (snd) {
            snd->setVolume(obj.audioSource.volume);
            snd->setPlaybackSpeed(obj.audioSource.pitch);
            if (is3D) {
                snd->setMinDistance(obj.audioSource.minDistance);
            }
            RuntimeAudioSourceInstance inst;
            inst.objectId = obj.id;
            inst.sound    = snd;
            runtimeAudioSources_.push_back(std::move(inst));
        }
    }

    // -- Vehicle sound profiles --
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const SceneObject& obj = objects_[i];
        if (!IsObjectEffectivelyEnabled(i) || !obj.hasVehicleSound || !obj.vehicleSound.enabled) continue;
        if (obj.vehicleSound.profilePath.empty()) continue;
        // Must also have a Vehicle component to get telemetry.
        if (!obj.hasVehicle || !obj.vehicle.enabled) continue;

        const std::string absPath = ProjectAssetPathToAbsolute(obj.vehicleSound.profilePath).string();
        VehicleSoundProfile profile = VehicleSoundProfileLoader::loadFromFile(absPath);

        RuntimeVehicleSoundInstance inst;
        inst.objectId        = obj.id;
        inst.vehicleObjectId = obj.id;
        inst.profile         = profile;
        inst.lastGear        = 0;
        inst.lastThrottleHigh = false;
        inst.lastLateralSpeed = 0.0f;

        // Start all engine layers looping but paused/silent.
        const glm::vec3 pos = GetObjectWorldPosition(i);
        for (const auto& layer : profile.engineLayers) {
            RuntimeVehicleSoundLayerState ls;
            ls.smoothVolume = 0.0f;
            ls.smoothPitch  = layer.pitchAtRpmMin;
            if (!layer.clipPath.empty()) {
                const std::string lPath = ProjectAssetPathToAbsolute(layer.clipPath).string();
                ls.sound = (profile.spatialBlend > 0.5f)
                    ? audioManager_->Play3D(lPath, pos, /*loop=*/true, /*paused=*/false)
                    : audioManager_->Play2D(lPath, /*loop=*/true, /*paused=*/false);
                if (ls.sound) {
                    ls.sound->setVolume(0.0f);
                    ls.sound->setPlaybackSpeed(ls.smoothPitch);
                    if (profile.spatialBlend > 0.5f) {
                        ls.sound->setMinDistance(profile.minDistance);
                    }
                }
            }
            inst.layers.push_back(std::move(ls));
        }
        runtimeVehicleSounds_.push_back(std::move(inst));
    }

    // Play EngineStart triggers
    for (auto& inst : runtimeVehicleSounds_) {
        for (const auto& trig : inst.profile.triggerSounds) {
            if (trig.trigger == VehicleSoundTrigger::EngineStart && !trig.clipPath.empty()) {
                const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                irrklang::ISound* s = audioManager_->Play2D(p, false, false);
                if (s) { s->setVolume(trig.volume); s->drop(); }
            }
        }
    }
}

void SceneEditor::UpdateAudio(float deltaTime) {
    if (!audioManager_ || !audioManager_->IsInitialized()) return;
    if (!scriptsRunning_ || scriptsPaused_) return;

    // -- Update AudioListener position (first enabled one wins) --
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const SceneObject& obj = objects_[i];
        if (!IsObjectEffectivelyEnabled(i) || !obj.hasAudioListener || !obj.audioListener.enabled) continue;
        const glm::mat4 worldMat = GetObjectWorldMatrix(i);
        const glm::vec3 pos     = glm::vec3(worldMat[3]);
        const glm::vec3 forward = glm::normalize(glm::vec3(-worldMat[2]));
        const glm::vec3 up      = glm::normalize(glm::vec3(worldMat[1]));
        audioManager_->SetListenerTransform(pos, forward, up);
        break;
    }

    // -- Update 3D audio source positions --
    for (auto& inst : runtimeAudioSources_) {
        if (!inst.sound || inst.sound->isFinished()) continue;
        const int idx = FindObjectIndexById(inst.objectId);
        if (idx < 0) continue;
        const AudioSourceComponent& source = objects_[idx].audioSource;
        inst.sound->setVolume(source.volume);
        inst.sound->setPlaybackSpeed(source.pitch);
        if (source.spatialBlend > 0.5f) {
            const glm::vec3 pos = GetObjectWorldPosition(idx);
            inst.sound->setPosition(irrklang::vec3df(pos.x, pos.y, pos.z));
            inst.sound->setMinDistance(source.minDistance);
        }
    }

    // -- Update vehicle sound layers --
    const float smoothRate = 8.0f * deltaTime; // slew rate

    for (auto& inst : runtimeVehicleSounds_) {
        // Find the RuntimeVehicleInstance for this vehicle.
        const RuntimeVehicleInstance* rv = nullptr;
        for (const auto& v : runtimeVehicles_) {
            if (v.objectId == inst.vehicleObjectId) { rv = &v; break; }
        }
        if (!rv) continue;

        const float rpm      = rv->arcadeEngineRPM;
        const float throttle = rv->arcadeThrottle;
        const float latSpd   = std::abs(rv->arcadeLateralSpeed);

        // 3D position — follow the vehicle
        const int vIdx = FindObjectIndexById(inst.vehicleObjectId);
        if (vIdx >= 0 && inst.profile.spatialBlend > 0.5f) {
            const glm::vec3 pos = GetObjectWorldPosition(vIdx);
            const irrklang::vec3df ipos(pos.x, pos.y, pos.z);
            for (auto& ls : inst.layers) {
                if (ls.sound) ls.sound->setPosition(ipos);
            }
        }

        // Update each engine layer
        for (std::size_t li = 0; li < inst.layers.size() && li < inst.profile.engineLayers.size(); ++li) {
            const VehicleSoundEngineLayer& def = inst.profile.engineLayers[li];
            RuntimeVehicleSoundLayerState& ls  = inst.layers[li];
            if (!ls.sound) continue;

            const float range = def.rpmMax - def.rpmMin;
            const float t     = (range > 0.0f) ? clamp01((rpm - def.rpmMin) / range) : 0.0f;

            const float targetPitch  = lerp(def.pitchAtRpmMin, def.pitchAtRpmMax, t);
            float       targetVolume = lerp(def.volumeAtRpmMin, def.volumeAtRpmMax, t);
            targetVolume += throttle * def.volumeThrottleScale;
            targetVolume  = clamp01(targetVolume) * inst.profile.masterVolume;

            ls.smoothPitch  = lerp(ls.smoothPitch,  targetPitch,  smoothRate);
            ls.smoothVolume = lerp(ls.smoothVolume, targetVolume, smoothRate);

            ls.sound->setPlaybackSpeed(ls.smoothPitch);
            ls.sound->setVolume(ls.smoothVolume);
        }

        // Trigger detection
        const int curGear = rv->arcadeGear;
        if (curGear != inst.lastGear && inst.lastGear != 0) {
            const bool up = curGear > inst.lastGear;
            const VehicleSoundTrigger want = up ? VehicleSoundTrigger::GearUp : VehicleSoundTrigger::GearDown;
            for (const auto& trig : inst.profile.triggerSounds) {
                if (trig.trigger == want && !trig.clipPath.empty()) {
                    const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                    irrklang::ISound* s = audioManager_->Play2D(p, false, false);
                    if (s) { s->setVolume(trig.volume); s->drop(); }
                    break;
                }
            }
        }

        const bool throttleHigh = throttle > 0.7f;
        if (inst.lastThrottleHigh && !throttleHigh && rpm > 0.0f) {
            for (const auto& trig : inst.profile.triggerSounds) {
                if (trig.trigger == VehicleSoundTrigger::Backfire &&
                    rpm >= trig.minRpmForBackfire && !trig.clipPath.empty()) {
                    const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                    irrklang::ISound* s = audioManager_->Play2D(p, false, false);
                    if (s) { s->setVolume(trig.volume); s->drop(); }
                    break;
                }
            }
        }

        if (latSpd > 0.0f) {
            const bool squealNow  = latSpd > 2.0f;
            const bool squealPrev = inst.lastLateralSpeed > 2.0f;
            if (squealNow && !squealPrev) {
                for (const auto& trig : inst.profile.triggerSounds) {
                    if (trig.trigger == VehicleSoundTrigger::TireSqueal &&
                        latSpd >= trig.minLateralSpeedForSqueal && !trig.clipPath.empty()) {
                        const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                        irrklang::ISound* s = audioManager_->Play2D(p, false, false);
                        if (s) { s->setVolume(trig.volume); s->drop(); }
                        break;
                    }
                }
            }
        }

        inst.lastGear         = curGear;
        inst.lastThrottleHigh = throttleHigh;
        inst.lastLateralSpeed = latSpd;
    }
}

void SceneEditor::HandleConsoleCommand(const std::string& command) {
    const std::string trimmed = TrimCopyLocal(command);
    if (trimmed.empty()) {
        return;
    }

    if (trimmed == "help" || trimmed == "script.help") {
        if (console_) {
            console_->AddLog("Commands: script.help, script.list, script.run, script.pause, script.stop");
            console_->AddLog("Script callbacks: void OnStart(ObjectScriptContext& context), void OnUpdate(ObjectScriptContext& context, float deltaTime)");
            console_->AddLog("Object: context.GetObjectName(), context.GetTag(), context.SetTag(\"Player\"), context.CompareTag(\"Enemy\")");
            console_->AddLog("Find: auto enemy = context.FindObjectWithTag(\"Enemy\"); auto camera = context.FindObjectByName(\"Main Camera\");");
            console_->AddLog("ObjectHandle: IsValid(), GetObjectName(), GetTag(), GetPosition(), SetPosition(vec3), SetEnabled(bool)");
            console_->AddLog("Transform: context.GetPosition(), context.SetPosition({0, 2, 0}), context.GetForwardVector()");
            console_->AddLog("Input: context.GetAxis(\"moveX\"), context.GetAxis(\"moveY\"), context.IsActionDown(\"jump\")");
            console_->AddLog("Rigidbody: context.IsRigidbodyDynamic(), context.SetRigidbodyVelocity({0, 0, -5}), context.AddRigidbodyImpulse({0, 4, 0})");
            console_->AddLog("Collider: context.HasCollider(), context.SetColliderEnabled(false), context.SetColliderTrigger(true), context.SetBoxColliderSize({1, 2, 1})");
            console_->AddLog("Camera: context.HasCamera(), context.Camera().SetFieldOfView(60.0f)");
            console_->AddLog("Light: context.HasLight(), context.SetLightColor({1, 0.8f, 0.4f}), context.SetLightIntensity(3.0f)");
            console_->AddLog("AudioSource: context.HasAudioSource(), context.SetAudioVolume(0.5f), context.SetAudioPitch(1.2f)");
            console_->AddLog("Fields: RACEMAN_SCRIPT_FIELD_FLOAT(\"moveSpeed\", \"Move Speed\", 8.0f), then context.GetFloatField(\"moveSpeed\", 8.0f)");
        }
        return;
    }
    if (trimmed == "script.run") {
        if (scriptsRunning_) {
            SetScriptsPaused(false);
        } else {
            SetScriptsRunning(true);
        }
        return;
    }
    if (trimmed == "script.pause") {
        SetScriptsPaused(true);
        return;
    }
    if (trimmed == "script.stop") {
        SetScriptsRunning(false);
        return;
    }
    if (trimmed == "script.list") {
        if (!console_) {
            return;
        }
        const auto& scripts = GetRegisteredScripts();
        if (scripts.empty()) {
            console_->AddLog("No registered scripts. Press Play to build/load scripts, or create a script first.");
            return;
        }
        for (const ScriptDescriptor& script : scripts) {
            console_->AddLog(script.name + " (" + script.path + ")");
        }
        return;
    }

    if (console_) {
        console_->AddWarning("Unknown command: " + trimmed);
    }
}

void SceneEditor::UpdateCinemachine(float deltaTime) {
    if (!scriptsRunning_ || scriptsPaused_ || deltaTime <= 0.0f) {
        return;
    }

    auto findById  = [this](const std::string& id) { return FindObjectIndexById(id); };
    auto getMatrix = [this](int idx) { return GetObjectWorldMatrix(idx); };

    for (int camIdx = 0; camIdx < static_cast<int>(objects_.size()); ++camIdx) {
        SceneObject& camObj = objects_[camIdx];
        if (!IsObjectEffectivelyEnabled(camIdx)) {
            continue;
        }
        if (!camObj.hasCamera || !camObj.camera.enabled) {
            continue;
        }
        if (!camObj.hasCinemachine || !camObj.cinemachine.enabled) {
            continue;
        }

        const CinemachineCameraComponent& cine = camObj.cinemachine;

        glm::mat4 desiredWorld(1.0f);
        if (!ComputeCinemachineDesiredWorldMatrix(cine, camIdx, objects_, findById, getMatrix, desiredWorld)) {
            continue;
        }

        // Seed smoothing state on first touch
        RuntimeCinemachineState& state = runtimeCinemachineStates_[camObj.id];
        if (!state.initialized) {
            const glm::mat4 camWorldMatrix = getMatrix(camIdx);
            state.smoothedPosition = glm::vec3(camWorldMatrix[3]);
            state.smoothedRotation = glm::normalize(glm::quat_cast(camWorldMatrix));
            state.initialized = true;
        }

        const float posT = 1.0f - std::exp(-cine.positionDamping * deltaTime);
        const float rotT = 1.0f - std::exp(-cine.rotationDamping * deltaTime);

        const glm::vec3 desiredPos = glm::vec3(desiredWorld[3]);
        const glm::quat desiredRot = glm::normalize(glm::quat_cast(desiredWorld));

        state.smoothedPosition = glm::mix(state.smoothedPosition, desiredPos, posT);
        state.smoothedRotation = glm::normalize(glm::slerp(state.smoothedRotation, desiredRot, rotT));
    }

    // Clear states for cameras that no longer exist
    for (auto it = runtimeCinemachineStates_.begin(); it != runtimeCinemachineStates_.end(); ) {
        const int idx = FindObjectIndexById(it->first);
        if (idx < 0) {
            it = runtimeCinemachineStates_.erase(it);
        } else {
            ++it;
        }
    }
}

void SceneEditor::PreviewCinemachineInEditor() {
}

} // namespace raceman

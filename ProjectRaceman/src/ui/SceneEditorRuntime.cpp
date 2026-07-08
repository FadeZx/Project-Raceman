#include "SceneEditorInternal.h"
#include "../audio/AudioManager.h"
#include "../audio/VehicleSoundProfile.h"
#include "../input/InputManager.h"
#include "../physics/PhysicsWorld.h"
#include "../physics/VehiclePhysics.h"
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

struct WheelForceFeedbackSample {
    float torque{0.0f};
    float damper{0.0f};
    float vibration{0.0f};
};

WheelForceFeedbackSample BuildWheelForceFeedbackSample(const raceman::physics::VehiclePhysics& vehicle) {
    WheelForceFeedbackSample sample;
    const raceman::physics::VehicleTelemetry& telemetry = vehicle.getTelemetry();
    const raceman::physics::VehicleConfig& config = vehicle.getConfig();
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

    // Keep the first-pass model intentionally simple: center the wheel against the current steer input,
    // then add a small speed-based damper. This avoids the previous odd slip-pull behavior.
    sample.torque = (std::clamp)(-telemetry.steering * (0.08f + speedFactor * 0.42f + loadFactor * 0.28f), -1.0f, 1.0f);
    sample.damper = (std::clamp)(0.03f + speedFactor * 0.08f, 0.0f, 1.0f);
    sample.vibration = 0.0f;
    return sample;
}

float MoveTowards(float current, float target, float maxDelta) {
    if (current < target) {
        return (std::min)(current + maxDelta, target);
    }
    return (std::max)(current - maxDelta, target);
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

    UpdateScripts(deltaTime);
    UpdateVehiclePhysics(deltaTime);
    UpdatePhysics(deltaTime);
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

    bool anyEnabledCollider = false;
    bool anyEnabledPlaneCollider = false;
    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        if (!IsObjectEffectivelyEnabled(objectIndex)) {
            continue;
        }
        if (HasEnabledColliderComponent(objects_[objectIndex])) {
            anyEnabledCollider = true;
            if (GetEnabledColliderType(objects_[objectIndex]) == SceneColliderType::Plane) {
                anyEnabledPlaneCollider = true;
            }
            break;
        }
    }

    float strongestTorque = 0.0f;
    float strongestDamper = 0.0f;
    float strongestVibration = 0.0f;

    for (RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles_) {
        if (!runtimeVehicle.instance || runtimeVehicle.objectIndex < 0 || runtimeVehicle.objectIndex >= static_cast<int>(objects_.size())) {
            continue;
        }
        if (!IsObjectEffectivelyEnabled(runtimeVehicle.objectIndex)) {
            continue;
        }

        const bool hasPhysicsChassis = physicsWorld_ != nullptr &&
            !runtimeVehicle.chassisBodyObjectId.empty() &&
            physicsWorld_->HasBody(runtimeVehicle.chassisBodyObjectId);
        runtimeVehicle.instance->setExternalBodySimulation(hasPhysicsChassis);

        const bool canRaycast = physicsWorld_ != nullptr;
        if (canRaycast) {
            runtimeVehicle.instance->setGroundRaycastCallback([this, &runtimeVehicle](const raceman::physics::Vector3 &origin,
                                                                                      const raceman::physics::Vector3 &direction,
                                                                                      float maxDistance,
                                                                                      raceman::physics::VehicleRaycastHit &outHit) {
                if (!physicsWorld_) {
                    return false;
                }
                PhysicsRaycastHit sceneHit;
                const glm::vec3 sceneOrigin = VehicleVectorToScene(origin);
                const glm::vec3 sceneDirection = VehicleVectorToScene(direction);
                std::unordered_set<std::string> ignoredVehicleObjectIds;
                if (!runtimeVehicle.chassisBodyObjectId.empty()) {
                    ignoredVehicleObjectIds.insert(runtimeVehicle.chassisBodyObjectId);
                }
                const SceneObject& vehicleObject = objects_[runtimeVehicle.objectIndex];
                ignoredVehicleObjectIds.insert(vehicleObject.id);
                for (const std::string& chassisObjectId : vehicleObject.vehicle.chassisObjectIds) {
                    if (!chassisObjectId.empty()) {
                        ignoredVehicleObjectIds.insert(chassisObjectId);
                    }
                }
                for (const VehicleWheelBinding& binding : vehicleObject.vehicle.wheelBindings) {
                    if (!binding.objectId.empty()) {
                        ignoredVehicleObjectIds.insert(binding.objectId);
                    }
                }
                if (!physicsWorld_->RaycastIgnoring(sceneOrigin, sceneDirection, maxDistance, sceneHit, ignoredVehicleObjectIds)) {
                    return false;
                }
                if (!sceneHit.hit) {
                    return false;
                }
                outHit.position = SceneVectorToVehicle(sceneHit.position);
                outHit.normal = SceneVectorToVehicle(sceneHit.normal);
                outHit.distance = sceneHit.distance;
                return true;
            });
        } else {
            runtimeVehicle.instance->setGroundRaycastCallback({});
        }

        const bool allowGroundPlane = !canRaycast && anyEnabledPlaneCollider && anyEnabledCollider;
        runtimeVehicle.instance->setUseGroundPlane(allowGroundPlane);

        raceman::physics::VehicleRigidBodyState currentRigidBodyState = runtimeVehicle.instance->getRigidBodyState();

        if (hasPhysicsChassis) {
            PhysicsBodyState chassisState;
            if (physicsWorld_->GetBodyState(runtimeVehicle.chassisBodyObjectId, chassisState)) {
                currentRigidBodyState.transform.position = SceneVectorToVehicle(chassisState.position);
                currentRigidBodyState.transform.rotation = SceneQuatToVehicle(glm::quat(glm::radians(chassisState.rotationEuler)));
                currentRigidBodyState.linearVelocity = SceneVectorToVehicle(chassisState.velocity);
                currentRigidBodyState.angularVelocity = ScenePseudoVectorToVehicle(chassisState.angularVelocity);
                runtimeVehicle.instance->setRigidBodyState(currentRigidBodyState);
            }
        }

        const SceneObject& vehicleObject = objects_[runtimeVehicle.objectIndex];
        const std::string profileId = vehicleObject.vehicle.inputProfileId.empty()
            ? std::string("default_vehicle")
            : vehicleObject.vehicle.inputProfileId;
        const bool routeInput = ShouldRouteInputToGame() && inputManager_ != nullptr;
        const bool wantsForward = routeInput &&
            inputManager_->GetAxisForProfile(profileId, "throttle",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId) > 0.01f;
        const bool wantsReverseOrBrake = routeInput &&
            inputManager_->GetAxisForProfile(profileId, "brake",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId) > 0.01f;
        const bool manualShiftUpPressed = routeInput &&
            inputManager_->WasActionPressedForProfile(profileId, "shiftUp",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
        const bool manualShiftDownPressed = routeInput &&
            inputManager_->WasActionPressedForProfile(profileId, "shiftDown",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
        const bool manualNeutralPressed = routeInput &&
            inputManager_->WasActionPressedForProfile(profileId, "neutral",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
        const bool manualReversePressed = routeInput &&
            inputManager_->WasActionPressedForProfile(profileId, "reverse",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
        raceman::physics::VehicleControlInput baseInput{};
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

        const raceman::physics::VehicleTelemetry& telemetry = runtimeVehicle.instance->getTelemetry();
        const float longitudinalSpeed = VehicleLongitudinalSpeed(currentRigidBodyState);
        constexpr float kReverseEngageSpeed = 0.75f;
        constexpr float kReverseSwitchSpeed = 0.5f;
        const bool manualTransmission = runtimeVehicle.instance->getConfig().transmission.mode ==
            raceman::physics::TransmissionConfig::Mode::Manual;

        raceman::physics::VehicleControlInput input = baseInput;
        bool reverseActive = telemetry.isReverse;

        if (manualTransmission) {
            if (manualShiftUpPressed) {
                if (telemetry.isReverse) {
                    runtimeVehicle.instance->setReverse(false);
                }
                runtimeVehicle.instance->setNeutral(false);
                runtimeVehicle.instance->shiftUp();
            }
            if (manualShiftDownPressed) {
                if (telemetry.isReverse) {
                    runtimeVehicle.instance->setReverse(false);
                }
                runtimeVehicle.instance->shiftDown();
            }
            if (manualNeutralPressed) {
                runtimeVehicle.instance->setNeutral(!telemetry.isNeutral);
            }
            if (manualReversePressed && std::fabs(longitudinalSpeed) <= kReverseEngageSpeed) {
                reverseActive = !telemetry.isReverse;
                runtimeVehicle.instance->setNeutral(false);
            }
            runtimeVehicle.instance->setReverse(reverseActive);

            if (wantsForward && !wantsReverseOrBrake) {
                input.throttle = (std::max)(input.throttle, 1.0f);
            } else if (wantsReverseOrBrake && !wantsForward) {
                input.brake = (std::max)(input.brake, 1.0f);
            } else if (wantsForward && wantsReverseOrBrake) {
                input.brake = (std::max)(input.brake, 1.0f);
            }
        } else {
            if (wantsForward && !wantsReverseOrBrake && reverseActive && std::fabs(longitudinalSpeed) <= kReverseSwitchSpeed) {
                reverseActive = false;
            } else if (wantsReverseOrBrake && !wantsForward) {
                if (reverseActive || longitudinalSpeed <= kReverseEngageSpeed) {
                    reverseActive = true;
                }
            } else if (wantsForward && !wantsReverseOrBrake) {
                reverseActive = false;
            }

            runtimeVehicle.instance->setReverse(reverseActive);

            if (wantsForward && !wantsReverseOrBrake) {
                input.throttle = (std::max)(input.throttle, 1.0f);
            } else if (wantsReverseOrBrake && !wantsForward) {
                if (reverseActive) {
                    input.throttle = (std::max)(input.throttle, 1.0f);
                } else {
                    input.brake = (std::max)(input.brake, 1.0f);
                }
            } else if (wantsForward && wantsReverseOrBrake) {
                input.brake = (std::max)(input.brake, 1.0f);
            }
        }

        runtimeVehicle.instance->setInput(input);

        runtimeVehicle.instance->update(deltaTime);

        const WheelForceFeedbackSample ffbSample = BuildWheelForceFeedbackSample(*runtimeVehicle.instance);
        if (std::fabs(ffbSample.torque) >= std::fabs(strongestTorque)) {
            strongestTorque = ffbSample.torque;
            strongestDamper = ffbSample.damper;
            strongestVibration = ffbSample.vibration;
        }

        if (hasPhysicsChassis) {
            physicsWorld_->AddBodyForce(
                runtimeVehicle.chassisBodyObjectId,
                VehicleVectorToScene(runtimeVehicle.instance->getPendingChassisForce()));
            physicsWorld_->AddBodyTorque(
                runtimeVehicle.chassisBodyObjectId,
                VehiclePseudoVectorToScene(runtimeVehicle.instance->getPendingChassisTorque()));
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

    for (RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles_) {
        if (!runtimeVehicle.instance) {
            continue;
        }
        if (runtimeVehicle.objectIndex < 0 || runtimeVehicle.objectIndex >= static_cast<int>(objects_.size())) {
            continue;
        }
        if (!IsObjectEffectivelyEnabled(runtimeVehicle.objectIndex)) {
            continue;
        }

        const bool hasPhysicsChassis = physicsWorld_ != nullptr &&
            !runtimeVehicle.chassisBodyObjectId.empty() &&
            physicsWorld_->HasBody(runtimeVehicle.chassisBodyObjectId);

        if (hasPhysicsChassis) {
            PhysicsBodyState chassisState;
            if (physicsWorld_->GetBodyState(runtimeVehicle.chassisBodyObjectId, chassisState)) {
                raceman::physics::VehicleRigidBodyState rigidBodyState;
                rigidBodyState.transform.position = SceneVectorToVehicle(chassisState.position);
                rigidBodyState.transform.rotation = SceneQuatToVehicle(glm::quat(glm::radians(chassisState.rotationEuler)));
                rigidBodyState.linearVelocity = SceneVectorToVehicle(chassisState.velocity);
                rigidBodyState.angularVelocity = ScenePseudoVectorToVehicle(chassisState.angularVelocity);
                runtimeVehicle.instance->setRigidBodyState(rigidBodyState);
            }
        }

        const Transform runtimeChassisWorldTransform = TransformFromVehicleTransform(runtimeVehicle.instance->getChassisTransform());

        ApplyWorldTransformToSceneObject(
            objects_,
            [this](const std::string& id) { return FindObjectIndexById(id); },
            [this](int index) { return GetObjectWorldMatrix(index); },
            runtimeVehicle.objectIndex,
            runtimeChassisWorldTransform,
            true);

        const glm::mat4 authoredVehicleWorldMatrix = GetObjectWorldMatrix(runtimeVehicle.objectIndex);

        const std::vector<raceman::physics::Transform>& wheelTransforms = runtimeVehicle.instance->getWheelTransforms();
        const std::size_t wheelCount = (std::min)(wheelTransforms.size(), runtimeVehicle.wheelObjectIndices.size());
        for (std::size_t wheelIndex = 0; wheelIndex < wheelCount; ++wheelIndex) {
            const int objectIndex = runtimeVehicle.wheelObjectIndices[wheelIndex];
            if (objectIndex < 0 || objectIndex >= static_cast<int>(objects_.size())) {
                continue;
            }

            Transform wheelWorldTransform = TransformFromVehicleTransform(wheelTransforms[wheelIndex]);
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
        std::vector<PhysicsBodyDesc> physicsBodies;
        std::vector<PhysicsCharacterDesc> physicsCharacters;
        std::unordered_map<std::string, PhysicsBodyDesc> vehicleChassisBodies;
        std::unordered_set<std::string> consumedVehiclePhysicsObjects;

        for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
            const SceneObject& object = objects_[objectIndex];
            if (!IsObjectEffectivelyEnabled(objectIndex) || !object.hasVehicle || !object.vehicle.enabled || !HasVehicleChassisBindings(object)) {
                continue;
            }

            const Transform worldTransform = TransformFromMatrix(GetObjectWorldMatrix(objectIndex));
            raceman::physics::VehicleConfig chassisConfig{};
            if (!object.vehicle.configPath.empty()) {
                try {
                    chassisConfig = raceman::physics::VehicleConfigLoader::loadFromFile(ProjectAssetPathToAbsolute(object.vehicle.configPath).string());
                } catch (...) {
                }
            }
            PhysicsBodyDesc body;
            body.objectId = MakeVehicleChassisBodyObjectId(object.id);
            body.collisionLayer = ClampPhysicsLayerIndex(object.physicsLayer);
            body.position = worldTransform.position;
            body.rotationEuler = worldTransform.rotationEuler;
            body.scale = worldTransform.scale;
            body.bodyType = PhysicsBodyType::Dynamic;
            body.mass = object.hasRigidbody ? object.rigidbody.mass : (std::max)(1.0f, chassisConfig.chassis.mass);
            body.useGravity = true;
            body.friction = object.hasRigidbody ? object.rigidbody.friction : 0.8f;
            body.restitution = object.hasRigidbody ? object.rigidbody.restitution : 0.0f;
            body.linearDamping = object.hasRigidbody ? object.rigidbody.linearDamping : 0.0f;
            // Allow roll/pitch tilt momentum — use moderate damping to prevent wild oscillation
            body.angularDamping = object.hasRigidbody ? object.rigidbody.angularDamping : 0.4f;
            body.motionQuality = PhysicsMotionQuality::Continuous;  // prevent tunneling at high speed
            body.overrideCenterOfMass = true;
            body.centerOfMassOffset = VehicleVectorToScene(chassisConfig.chassis.centerOfMassOffset);
            body.overrideMassProperties = true;
            body.inertiaDiagonal = glm::vec3(
                (std::max)(0.001f, chassisConfig.chassis.rollInertia),
                (std::max)(0.001f, chassisConfig.chassis.yawInertia),
                (std::max)(0.001f, chassisConfig.chassis.pitchInertia));

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

            if (!body.colliders.empty()) {
                vehicleChassisBodies[object.id] = std::move(body);
            }
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
        std::fprintf(stdout, "[Play] Runtime physics descriptors: %zu bodies, %zu characters.\n",
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

                ImGui::TextUnformatted("Baking collision geometry...");

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
        if (!object.hasVehicle || !object.vehicle.enabled || object.vehicle.configPath.empty() || !IsObjectEffectivelyEnabled(objectIndex)) {
            continue;
        }
        ++candidateCount;

        try {
            const fs::path configPath = ProjectAssetPathToAbsolute(object.vehicle.configPath);
            raceman::physics::VehicleConfig config = raceman::physics::VehicleConfigLoader::loadFromFile(configPath.string());
            RuntimeVehicleInstance runtimeVehicle;
            runtimeVehicle.objectId = object.id;
            runtimeVehicle.objectIndex = objectIndex;
            runtimeVehicle.chassisBodyObjectId = HasVehicleChassisBindings(object) ? MakeVehicleChassisBodyObjectId(object.id) : std::string{};
            runtimeVehicle.instance = std::make_unique<raceman::physics::VehiclePhysics>(config);

            raceman::physics::Transform chassisTransform;
            const Transform sceneWorldTransform = TransformFromMatrix(GetObjectWorldMatrix(objectIndex));
            chassisTransform.position = SceneVectorToVehicle(sceneWorldTransform.position);
            const glm::quat rotation = glm::quat(glm::radians(sceneWorldTransform.rotationEuler));
            chassisTransform.rotation = SceneQuatToVehicle(rotation);
            runtimeVehicle.instance->setChassisTransform(chassisTransform);

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

            runtimeVehicles_.push_back(std::move(runtimeVehicle));
            std::fprintf(stdout,
                         "[Vehicle] Loaded '%s' with %zu wheels, chassisBody=%s\n",
                         object.name.c_str(),
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
        if (!rv || !rv->instance) continue;

        const raceman::physics::VehicleTelemetry& tel = rv->instance->getTelemetry();
        const float rpm      = tel.engineRPM;
        const float throttle = tel.throttle;
        const float latSpd   = std::abs(tel.lateralSpeed);

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
        const int curGear = tel.currentGear;
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

#include "SceneEditorInternal.h"
#include "../input/InputManager.h"
#include "../physics/PhysicsWorld.h"
#include "../physics/VehiclePhysics.h"
#include "../scripting/ScriptRegistry.h"

#include <GLFW/glfw3.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unordered_set>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

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
        ObjectScriptContext context(*objectIt, &attachment, console_, scriptInput, physicsWorld_.get());
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
        return;
    }

    if (runtimeVehicles_.empty()) {
        return;
    }

    const bool wantsForward = ShouldRouteInputToGame() && inputManager_ != nullptr &&
        (inputManager_->IsKeyDown(GLFW_KEY_W) || inputManager_->IsKeyDown(GLFW_KEY_UP));
    const bool wantsReverseOrBrake = ShouldRouteInputToGame() && inputManager_ != nullptr &&
        (inputManager_->IsKeyDown(GLFW_KEY_S) || inputManager_->IsKeyDown(GLFW_KEY_DOWN));
    const bool manualShiftUpPressed = ShouldRouteInputToGame() && inputManager_ != nullptr &&
        (inputManager_->WasKeyPressed(GLFW_KEY_E) || inputManager_->WasKeyPressed(GLFW_KEY_PAGE_UP));
    const bool manualShiftDownPressed = ShouldRouteInputToGame() && inputManager_ != nullptr &&
        (inputManager_->WasKeyPressed(GLFW_KEY_Q) || inputManager_->WasKeyPressed(GLFW_KEY_PAGE_DOWN));
    const bool manualNeutralPressed = ShouldRouteInputToGame() && inputManager_ != nullptr &&
        inputManager_->WasKeyPressed(GLFW_KEY_N);
    const bool manualReversePressed = ShouldRouteInputToGame() && inputManager_ != nullptr &&
        inputManager_->WasKeyPressed(GLFW_KEY_R);
    raceman::physics::VehicleControlInput baseInput{};
    if (ShouldRouteInputToGame() && inputManager_ != nullptr) {
        if (inputManager_->IsKeyDown(GLFW_KEY_A) || inputManager_->IsKeyDown(GLFW_KEY_LEFT)) {
            baseInput.steering -= 1.0f;
        }
        if (inputManager_->IsKeyDown(GLFW_KEY_D) || inputManager_->IsKeyDown(GLFW_KEY_RIGHT)) {
            baseInput.steering += 1.0f;
        }
        if (inputManager_->IsKeyDown(GLFW_KEY_SPACE)) {
            baseInput.handbrake = 1.0f;
        }
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
                const std::string *ignoreId = runtimeVehicle.chassisBodyObjectId.empty() ? nullptr : &runtimeVehicle.chassisBodyObjectId;
                if (!physicsWorld_->Raycast(sceneOrigin, sceneDirection, maxDistance, sceneHit, ignoreId)) {
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
                input.throttle = 1.0f;
            } else if (wantsReverseOrBrake && !wantsForward) {
                input.brake = 1.0f;
            } else if (wantsForward && wantsReverseOrBrake) {
                input.brake = 1.0f;
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
                input.throttle = 1.0f;
            } else if (wantsReverseOrBrake && !wantsForward) {
                if (reverseActive) {
                    input.throttle = 1.0f;
                } else {
                    input.brake = 1.0f;
                }
            } else if (wantsForward && wantsReverseOrBrake) {
                input.brake = 1.0f;
            }
        }

        runtimeVehicle.instance->setInput(input);

        runtimeVehicle.instance->update(deltaTime);

        if (hasPhysicsChassis) {
            physicsWorld_->AddBodyForce(
                runtimeVehicle.chassisBodyObjectId,
                VehicleVectorToScene(runtimeVehicle.instance->getPendingChassisForce()));
            physicsWorld_->AddBodyTorque(
                runtimeVehicle.chassisBodyObjectId,
                VehiclePseudoVectorToScene(runtimeVehicle.instance->getPendingChassisTorque()));
        }
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

    if (running) {
        std::fprintf(stdout, "[Play] Building scene...\n");
        std::fflush(stdout);
        const auto buildStart = std::chrono::high_resolution_clock::now();

        SaveCurrentScene();
        profilerStats_ = CollectProfilerStats();
        playModeSnapshot_ = {objects_, selectedIndex_, selectedIndices_};
        hasPlayModeSnapshot_ = true;
        activeViewport_ = SceneEditorActiveViewport::Game;
        scriptsRunning_ = true;
        scriptsPaused_ = false;
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
            body.mass = object.hasRigidbody ? object.rigidbody.mass : 1200.0f;
            body.useGravity = false;
            body.friction = object.hasRigidbody ? object.rigidbody.friction : 0.8f;
            body.restitution = object.hasRigidbody ? object.rigidbody.restitution : 0.0f;
            body.linearDamping = object.hasRigidbody ? object.rigidbody.linearDamping : 0.0f;
            // Allow roll/pitch tilt momentum — use moderate damping to prevent wild oscillation
            body.angularDamping = object.hasRigidbody ? object.rigidbody.angularDamping : 0.4f;
            if (!object.vehicle.canTilt) {
                // Lock pitch and roll, keep only yaw free (Y-up world: X=pitch, Z=roll in Jolt coords)
                body.freezeRotationX = true;
                body.freezeRotationZ = true;
            }
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
        physicsWorld_ = std::make_unique<PhysicsWorld>(physicsLayerCollisionMatrix_);
        physicsWorld_->Build(physicsBodies, physicsCharacters);
        RebuildVehicleRuntime();
        RebuildScriptRuntime();
        {
            const auto buildEnd = std::chrono::high_resolution_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(buildEnd - buildStart).count();
            std::fprintf(stdout, "[Play] Build complete in %.1f ms\n", ms);
            std::fflush(stdout);
        }
        if (console_) {
            console_->AddLog("Play mode started.");
        }
    } else {
        scriptsRunning_ = false;
        scriptsPaused_ = false;
        ClearScriptRuntime();
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

void SceneEditor::RebuildVehicleRuntime() {
    runtimeVehicles_.clear();

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        const SceneObject& object = objects_[objectIndex];
        if (!object.hasVehicle || !object.vehicle.enabled || object.vehicle.configPath.empty() || !IsObjectEffectivelyEnabled(objectIndex)) {
            continue;
        }

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
        } catch (const std::exception& ex) {
            if (console_) {
                console_->AddWarning("Vehicle runtime load failed for '" + object.name + "': " + ex.what());
            }
        }
    }
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

void SceneEditor::HandleConsoleCommand(const std::string& command) {
    const std::string trimmed = TrimCopyLocal(command);
    if (trimmed.empty()) {
        return;
    }

    if (trimmed == "help" || trimmed == "script.help") {
        if (console_) {
            console_->AddLog("Commands: script.help, script.list, script.run, script.pause, script.stop");
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
            console_->AddLog("No registered scripts. Create a script, rebuild, then attach it.");
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

// Shared helper: compute the instantaneous (no-damping) desired world transform for
// a camera driven by a CinemachineCameraComponent. Returns false if the component
// has no valid follow/look-at target, meaning the camera should not be moved.
static bool ComputeCinemachineDesiredTransform(
    const CinemachineCameraComponent& cine,
    int camIdx,
    const std::vector<SceneObject>& objects,
    const std::function<int(const std::string&)>& findById,
    const std::function<glm::mat4(int)>& getWorldMatrix,
    Transform& outTransform)
{
    const int followIdx = cine.followTargetId.empty() ? -1 : findById(cine.followTargetId);
    const std::string& lookAtId = cine.lookAtTargetId.empty() ? cine.followTargetId : cine.lookAtTargetId;
    const int lookAtIdx = lookAtId.empty() ? -1 : findById(lookAtId);

    const bool hasFollow  = followIdx >= 0 && followIdx != camIdx;
    const bool hasLookAt  = lookAtIdx >= 0 && lookAtIdx != camIdx;

    if (!hasFollow && !hasLookAt) {
        return false;
    }

    // --- position ---
    glm::vec3 desiredPos = glm::vec3(getWorldMatrix(camIdx)[3]);  // default: stay put

    if (hasFollow && cine.type != CinemachineCameraType::LookAt) {
        const glm::mat4 targetWorld = getWorldMatrix(followIdx);
        desiredPos = glm::vec3(targetWorld * glm::vec4(cine.followOffset, 1.0f));
    }

    // --- rotation ---
    glm::quat desiredRot{1.0f, 0.0f, 0.0f, 0.0f};

    if (cine.type == CinemachineCameraType::LookAt || cine.type == CinemachineCameraType::FollowAndLookAt) {
        const int resolvedLookIdx = hasLookAt ? lookAtIdx : followIdx;
        if (resolvedLookIdx >= 0 && resolvedLookIdx != camIdx) {
            const glm::vec3 lookAtPos = glm::vec3(getWorldMatrix(resolvedLookIdx)[3]);
            const glm::vec3 dir = lookAtPos - desiredPos;
            if (glm::length(dir) > 0.001f) {
                const glm::vec3 fwd = glm::normalize(dir);
                const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
                glm::vec3 right = glm::cross(fwd, worldUp);
                right = (glm::length(right) > 0.001f) ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 up = glm::normalize(glm::cross(right, fwd));
                desiredRot = glm::quat_cast(glm::mat3(right, up, -fwd));
            }
        }
    } else if (cine.type == CinemachineCameraType::Follow && hasFollow) {
        desiredRot = glm::quat_cast(glm::mat3(getWorldMatrix(followIdx)));
    }

    // --- pitch / yaw offsets ---
    if (cine.pitchOffset != 0.0f || cine.yawOffset != 0.0f) {
        const glm::quat pitchRot = glm::angleAxis(glm::radians(cine.pitchOffset), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::quat yawRot   = glm::angleAxis(glm::radians(cine.yawOffset),   glm::vec3(0.0f, 1.0f, 0.0f));
        desiredRot = glm::normalize(desiredRot * yawRot * pitchRot);
    }

    const glm::mat4 newWorld = glm::translate(glm::mat4(1.0f), desiredPos) * glm::toMat4(desiredRot);
    outTransform = TransformFromMatrix(newWorld);
    return true;
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

        // Compute the instantaneous desired transform
        Transform desired;
        if (!ComputeCinemachineDesiredTransform(cine, camIdx, objects_, findById, getMatrix, desired)) {
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

        // desired transform holds the fully-computed target (pos + rot + pitch/yaw)
        const glm::mat4 desiredWorld = BuildTransformMatrix(desired);
        const glm::vec3 desiredPos = glm::vec3(desiredWorld[3]);
        const glm::quat desiredRot = glm::normalize(glm::quat_cast(desiredWorld));

        state.smoothedPosition = glm::mix(state.smoothedPosition, desiredPos, posT);
        state.smoothedRotation = glm::normalize(glm::slerp(state.smoothedRotation, desiredRot, rotT));

        const glm::mat4 smoothWorld = glm::translate(glm::mat4(1.0f), state.smoothedPosition)
                                    * glm::toMat4(state.smoothedRotation);
        const Transform smoothTransform = TransformFromMatrix(smoothWorld);
        ApplyWorldTransformToSceneObject(objects_, findById, getMatrix, camIdx, smoothTransform, false);
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
    if (scriptsRunning_) {
        return;  // runtime UpdateCinemachine handles play mode
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

        Transform desired;
        if (!ComputeCinemachineDesiredTransform(camObj.cinemachine, camIdx, objects_, findById, getMatrix, desired)) {
            continue;
        }

        ApplyWorldTransformToSceneObject(objects_, findById, getMatrix, camIdx, desired, false);
        // Intentionally no onDirty_ — this is a visual preview only
    }
}

} // namespace raceman

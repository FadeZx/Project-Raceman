#include "SceneEditorInternal.h"

#include "../physics/PhysicsWorld.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace raceman {
using namespace scene_editor_internal;

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

void SceneEditor::BuildRuntimePhysicsDescriptors(std::vector<PhysicsBodyDesc>& outPhysicsBodies,
                                                  std::vector<PhysicsCharacterDesc>& outPhysicsCharacters) {
    outPhysicsBodies.clear();
    outPhysicsCharacters.clear();
    std::unordered_map<std::string, PhysicsBodyDesc> vehicleChassisBodies;
    std::unordered_set<std::string> consumedVehiclePhysicsObjects;

    BuildVehiclePhysicsBodyDescriptors(vehicleChassisBodies, consumedVehiclePhysicsObjects);

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
            outPhysicsCharacters.push_back(std::move(character));
            continue;
        }

        if (object.hasVehicle && object.vehicle.enabled) {
            auto chassisIt = vehicleChassisBodies.find(object.id);
            if (chassisIt != vehicleChassisBodies.end()) {
                outPhysicsBodies.push_back(chassisIt->second);
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
        // Enable CCD for dynamic bodies so fast-moving objects don't tunnel through colliders.
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
            outPhysicsBodies.push_back(std::move(body));
        }
    }
}

} // namespace raceman

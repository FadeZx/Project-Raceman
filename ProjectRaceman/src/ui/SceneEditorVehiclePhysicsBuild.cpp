#include "SceneEditorInternal.h"
#include "SceneEditorVehicleBuilder.h"

#include "../physics/PhysicsWorld.h"

#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

namespace raceman {
using namespace scene_editor_internal;

namespace {

PhysicsColliderDesc BuildDefaultVehicleChassisCollider(const raceman::physics::VehicleConfig&) {
    PhysicsColliderDesc collider;
    collider.type = PhysicsColliderType::Box;
    collider.center = glm::vec3(0.0f, -0.2f, 0.0f);
    collider.size = glm::vec3(1.8f, 0.4f, 4.0f);
    return collider;
}

} // namespace

void SceneEditor::BuildVehiclePhysicsBodyDescriptors(
    std::unordered_map<std::string, PhysicsBodyDesc>& outVehicleChassisBodies,
    std::unordered_set<std::string>& outConsumedVehiclePhysicsObjects) {
    outVehicleChassisBodies.clear();
    outConsumedVehiclePhysicsObjects.clear();

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        const SceneObject& object = objects_[objectIndex];
        if (!IsObjectEffectivelyEnabled(objectIndex) || !object.hasVehicle || !object.vehicle.enabled) {
            continue;
        }

        const Transform worldTransform = TransformFromMatrix(GetObjectWorldMatrix(objectIndex));
        raceman::physics::VehicleConfig chassisConfig = BuildDefaultJoltVehicleConfig();
        if (!object.vehicle.configPath.empty()) {
            try {
                chassisConfig = raceman::physics::VehicleConfigLoader::loadFromFile(
                    ProjectAssetPathToAbsolute(object.vehicle.configPath).string());
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
        body.angularDamping = 0.05f;
        body.motionQuality = PhysicsMotionQuality::Continuous;
        body.overrideCenterOfMass = false;
        body.overrideMassProperties = false;

        const glm::mat4 vehicleWorldMatrix = GetObjectWorldMatrix(objectIndex);
        if (AppendSupportedVehicleChassisColliders(object, glm::mat4(1.0f), body.colliders)) {
            outConsumedVehiclePhysicsObjects.insert(object.id);
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
                outConsumedVehiclePhysicsObjects.insert(candidate.id);
                continue;
            }

            const glm::mat4 relativeMatrix = glm::inverse(vehicleWorldMatrix) * GetObjectWorldMatrix(candidateIndex);
            if (AppendSupportedVehicleChassisColliders(candidate, relativeMatrix, body.colliders)) {
                outConsumedVehiclePhysicsObjects.insert(candidate.id);
            }
        }

        if (body.colliders.empty()) {
            body.colliders.push_back(BuildDefaultVehicleChassisCollider(chassisConfig));
            std::fprintf(stdout,
                         "[VehicleDebug] Vehicle '%s' has no chassis collider; using generated default box chassis.\n",
                         object.id.c_str());
            std::fflush(stdout);
        }

        outVehicleChassisBodies[object.id] = std::move(body);
    }
}

} // namespace raceman

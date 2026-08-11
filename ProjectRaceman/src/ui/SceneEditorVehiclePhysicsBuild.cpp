#include "SceneEditorInternal.h"
#include "SceneEditorVehicleBuilder.h"

#include "../physics/PhysicsWorld.h"

#include <algorithm>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

namespace raceman {
using namespace scene_editor_internal;

namespace {

const glm::vec3 kFallbackChassisCenter{0.0f, -0.2f, 0.0f};
const glm::vec3 kFallbackChassisSize{1.8f, 0.6f, 4.0f};

PhysicsColliderDesc MakeBoxColliderDesc(const glm::vec3& center, const glm::vec3& size) {
    PhysicsColliderDesc collider;
    collider.type = PhysicsColliderType::Box;
    collider.center = center;
    collider.size = (glm::max)(glm::abs(size), glm::vec3(0.01f));
    return collider;
}

// Fits a box to the vehicle's own mesh bounds, falling back to a sensible car-sized
// box when the object has no mesh to measure.
bool TryBuildAutoBoxFromMeshBounds(const SceneObject& object, glm::vec3& outCenter, glm::vec3& outSize) {
    if (!object.hasMeshFilter) {
        return false;
    }
    const glm::vec3 boundsMin = object.meshFilter.localBoundsMin;
    const glm::vec3 boundsMax = object.meshFilter.localBoundsMax;
    const glm::vec3 size = boundsMax - boundsMin;
    if (size.x <= 0.001f || size.y <= 0.001f || size.z <= 0.001f) {
        return false;
    }
    outCenter = (boundsMin + boundsMax) * 0.5f;
    outSize = size;
    return true;
}

PhysicsColliderDesc MakeConvexMeshColliderDesc(const std::string& meshAssetPath,
                                               const std::string& meshName,
                                               int meshIndex,
                                               const glm::vec3& pivotOffset,
                                               const glm::vec3& center,
                                               const glm::vec3& rotationEuler) {
    PhysicsColliderDesc collider;
    collider.type = PhysicsColliderType::Mesh;
    collider.center = center;
    collider.rotationEuler = rotationEuler;
    collider.meshAssetPath = meshAssetPath;
    collider.meshName = meshName;
    collider.meshIndex = meshIndex;
    collider.meshPivotOffset = pivotOffset;
    // The chassis is a moving body, so triangle meshes are never valid here.
    collider.meshMode = MeshColliderMode::ConvexHull;
    collider.meshBuildQuality = MeshColliderBuildQuality::BuildQuality;
    return collider;
}

void AppendAuthoredChassisShape(const VehicleChassisShape& shape, std::vector<PhysicsColliderDesc>& outColliders) {
    if (!shape.enabled) {
        return;
    }

    switch (shape.type) {
    case VehicleChassisShapeType::Box: {
        PhysicsColliderDesc collider = MakeBoxColliderDesc(shape.center, shape.size);
        collider.rotationEuler = shape.rotationEuler;
        outColliders.push_back(std::move(collider));
        break;
    }
    case VehicleChassisShapeType::Sphere: {
        PhysicsColliderDesc collider;
        collider.type = PhysicsColliderType::Sphere;
        collider.center = shape.center;
        collider.radius = (std::max)(0.01f, shape.radius);
        outColliders.push_back(std::move(collider));
        break;
    }
    case VehicleChassisShapeType::Capsule: {
        PhysicsColliderDesc collider;
        collider.type = PhysicsColliderType::Capsule;
        collider.center = shape.center;
        collider.rotationEuler = shape.rotationEuler;
        collider.radius = (std::max)(0.01f, shape.radius);
        collider.height = (std::max)(collider.radius * 2.0f, shape.height);
        outColliders.push_back(std::move(collider));
        break;
    }
    case VehicleChassisShapeType::ConvexMesh: {
        if (shape.meshAssetPath.empty()) {
            break;
        }
        outColliders.push_back(MakeConvexMeshColliderDesc(
            shape.meshAssetPath, shape.meshName, shape.meshIndex, glm::vec3(0.0f), shape.center, shape.rotationEuler));
        break;
    }
    }
}

} // namespace

std::vector<PhysicsColliderDesc> SceneEditor::BuildVehicleChassisColliderDescs(
    int vehicleObjectIndex,
    std::unordered_set<std::string>* outConsumedObjectIds) const {
    std::vector<PhysicsColliderDesc> colliders;
    if (vehicleObjectIndex < 0 || vehicleObjectIndex >= static_cast<int>(objects_.size())) {
        return colliders;
    }

    const SceneObject& object = objects_[vehicleObjectIndex];
    const VehicleChassisCollisionConfig& config = object.vehicle.chassisCollision;
    if (!config.enabled || config.mode == VehicleChassisCollisionMode::None) {
        return colliders;
    }

    switch (config.mode) {
    case VehicleChassisCollisionMode::AutoBox: {
        glm::vec3 center = kFallbackChassisCenter;
        glm::vec3 size = kFallbackChassisSize;
        TryBuildAutoBoxFromMeshBounds(object, center, size);
        colliders.push_back(MakeBoxColliderDesc(center, size));
        break;
    }
    case VehicleChassisCollisionMode::Box: {
        colliders.push_back(MakeBoxColliderDesc(config.boxCenter, config.boxSize));
        break;
    }
    case VehicleChassisCollisionMode::Shapes: {
        for (const VehicleChassisShape& shape : config.shapes) {
            AppendAuthoredChassisShape(shape, colliders);
        }
        break;
    }
    case VehicleChassisCollisionMode::ConvexMesh: {
        std::string meshAssetPath = config.meshAssetPath;
        std::string meshName = config.meshName;
        int meshIndex = config.meshIndex;
        glm::vec3 pivotOffset{0.0f};
        if (meshAssetPath.empty() && object.hasMeshFilter && !object.meshFilter.sourcePath.empty()) {
            meshAssetPath = object.meshFilter.sourcePath;
            meshName = object.meshFilter.meshName;
            meshIndex = object.meshFilter.meshIndex;
            pivotOffset = object.meshFilter.pivotOffset;
        }
        if (!meshAssetPath.empty()) {
            colliders.push_back(MakeConvexMeshColliderDesc(
                meshAssetPath, meshName, meshIndex, pivotOffset, glm::vec3(0.0f), glm::vec3(0.0f)));
        }
        break;
    }
    case VehicleChassisCollisionMode::ChildColliders: {
        if (AppendSupportedVehicleChassisColliders(object, glm::mat4(1.0f), colliders) && outConsumedObjectIds != nullptr) {
            outConsumedObjectIds->insert(object.id);
        }

        const glm::mat4 vehicleWorldMatrix = GetObjectWorldMatrix(vehicleObjectIndex);
        for (const std::string& chassisObjectId : object.vehicle.chassisObjectIds) {
            const int candidateIndex = FindObjectIndexById(chassisObjectId);
            if (candidateIndex < 0 || candidateIndex == vehicleObjectIndex) {
                continue;
            }
            const SceneObject& candidate = objects_[candidateIndex];
            if (!IsObjectEffectivelyEnabled(candidateIndex) || !IsDescendantOf(candidate.id, object.id)) {
                continue;
            }
            if (IsVehicleWheelHelperObject(object.vehicle, candidate.id)) {
                if (outConsumedObjectIds != nullptr) {
                    outConsumedObjectIds->insert(candidate.id);
                }
                continue;
            }

            const glm::mat4 relativeMatrix = glm::inverse(vehicleWorldMatrix) * GetObjectWorldMatrix(candidateIndex);
            if (AppendSupportedVehicleChassisColliders(candidate, relativeMatrix, colliders) && outConsumedObjectIds != nullptr) {
                outConsumedObjectIds->insert(candidate.id);
            }
        }
        break;
    }
    case VehicleChassisCollisionMode::None:
        break;
    }

    return colliders;
}

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
        body.mass = (std::max)(1.0f, chassisConfig.chassis.mass);
        body.useGravity = false;
        body.friction = 0.8f;
        body.restitution = 0.0f;
        body.linearDamping = 0.0f;
        body.angularDamping = 0.05f;
        body.motionQuality = PhysicsMotionQuality::Continuous;
        body.overrideCenterOfMass = false;
        body.overrideMassProperties = false;

        body.colliders = BuildVehicleChassisColliderDescs(objectIndex, &outConsumedVehiclePhysicsObjects);

        const VehicleChassisCollisionConfig& chassisCollision = object.vehicle.chassisCollision;
        const bool collisionRequested =
            chassisCollision.enabled && chassisCollision.mode != VehicleChassisCollisionMode::None;
        if (body.colliders.empty() && collisionRequested) {
            body.colliders.push_back(MakeBoxColliderDesc(kFallbackChassisCenter, kFallbackChassisSize));
            std::fprintf(stdout,
                         "[VehicleDebug] Vehicle '%s' chassis collision resolved to no shapes; using fallback box.\n",
                         object.id.c_str());
            std::fflush(stdout);
        }

        outVehicleChassisBodies[object.id] = std::move(body);
    }
}

} // namespace raceman

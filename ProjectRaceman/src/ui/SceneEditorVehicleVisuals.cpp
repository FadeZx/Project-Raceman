#include "SceneEditorVehicleVisuals.h"

#include "SceneEditorInternal.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace raceman {
namespace {

using namespace scene_editor_internal;

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

} // namespace

void UpdateVehicleVisuals(std::vector<SceneObject>& objects,
                          std::vector<RuntimeVehicleInstance>& runtimeVehicles,
                          const VehicleFindObjectIndex& findObjectIndexById,
                          const VehicleGetObjectWorldMatrix& getObjectWorldMatrix,
                          const VehicleObjectEnabledQuery& isObjectEnabled,
                          float renderAlpha) {
    for (RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles) {
        if (runtimeVehicle.objectIndex < 0 || runtimeVehicle.objectIndex >= static_cast<int>(objects.size())) {
            continue;
        }
        if (!isObjectEnabled(runtimeVehicle.objectIndex)) {
            continue;
        }

        Transform runtimeChassisWorldTransform = InterpolateTransform(
            runtimeVehicle.arcadePreviousChassisWorld,
            runtimeVehicle.arcadeChassisWorld,
            renderAlpha);
        runtimeChassisWorldTransform.scale = glm::vec3(1.0f);
        const float renderWheelSpin = runtimeVehicle.arcadePreviousWheelSpin +
            (runtimeVehicle.arcadeWheelSpin - runtimeVehicle.arcadePreviousWheelSpin) * renderAlpha;

        ApplyWorldTransformToSceneObject(
            objects,
            findObjectIndexById,
            getObjectWorldMatrix,
            runtimeVehicle.objectIndex,
            runtimeChassisWorldTransform,
            true);

        const glm::mat4 authoredVehicleWorldMatrix = getObjectWorldMatrix(runtimeVehicle.objectIndex);
        Transform currentArcadeChassisWorld = runtimeVehicle.arcadeChassisWorld;
        currentArcadeChassisWorld.scale = glm::vec3(1.0f);
        const glm::mat4 currentArcadeChassisWorldMatrix = BuildTransformMatrix(currentArcadeChassisWorld);
        const glm::mat4 runtimeChassisWorldMatrix = BuildTransformMatrix(runtimeChassisWorldTransform);

        const std::size_t wheelCount = (std::min)(runtimeVehicle.config.wheels.size(), runtimeVehicle.wheelObjectIndices.size());
        for (std::size_t wheelIndex = 0; wheelIndex < wheelCount; ++wheelIndex) {
            const int objectIndex = runtimeVehicle.wheelObjectIndices[wheelIndex];
            if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size())) {
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
                objects,
                findObjectIndexById,
                getObjectWorldMatrix,
                objectIndex,
                wheelWorldTransform,
                false);
        }
    }
}

} // namespace raceman

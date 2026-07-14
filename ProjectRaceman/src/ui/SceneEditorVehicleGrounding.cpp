#include "SceneEditorVehicleGrounding.h"

#include "SceneEditorInternal.h"
#include "../physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/quaternion.hpp>

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

float SmoothingAlpha(float smoothing, float deltaTime) {
    if (smoothing <= 0.0f) {
        return 1.0f;
    }
    return (std::clamp)(1.0f - std::exp(-smoothing * deltaTime), 0.0f, 1.0f);
}

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

} // namespace

void ApplyArcadeVehicleGrounding(RuntimeVehicleInstance& runtimeVehicle,
                                 PhysicsWorld* physicsWorld,
                                 const std::unordered_set<std::string>& ignoredObjectIds,
                                 const ColliderSurfaceConfig& defaultSurface,
                                 const VehicleSurfaceLookup& surfaceLookup,
                                 float steeringInput,
                                 float speedFactorBeforeDrive,
                                 float deltaTime) {
    float& speed = runtimeVehicle.arcadeSpeed;
    float& lateralSpeed = runtimeVehicle.arcadeLateralSpeed;
    const raceman::physics::VehicleLoadTransferConfig& loadTransfer = runtimeVehicle.config.loadTransfer;
    const raceman::physics::VehicleGroundContactConfig& groundContact = runtimeVehicle.config.groundContact;
    const glm::quat yawRotation = glm::angleAxis(glm::radians(runtimeVehicle.arcadeChassisWorld.rotationEuler.y), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 right = yawRotation * glm::vec3(1.0f, 0.0f, 0.0f);
    lateralSpeed = glm::dot(runtimeVehicle.arcadePlanarVelocity, right);
    runtimeVehicle.arcadeSideSlipVelocity = lateralSpeed;

    const float longitudinalLoad = runtimeVehicle.arcadeLongitudinalLoad;
    const float lateralLoad = loadTransfer.enabled
        ? (std::clamp)(steeringInput * speedFactorBeforeDrive * speedFactorBeforeDrive, -1.0f, 1.0f)
        : 0.0f;
    runtimeVehicle.arcadeLateralLoad = lateralLoad;
    const float targetLoadPitch = loadTransfer.enabled
        ? (longitudinalLoad >= 0.0f
            ? (std::max)(0.0f, loadTransfer.throttleSquatAmount) * longitudinalLoad
            : -(std::max)(0.0f, loadTransfer.brakePitchAmount) * -longitudinalLoad)
        : 0.0f;
    const float targetLoadRoll = loadTransfer.enabled
        ? -(std::max)(0.0f, loadTransfer.lateralRollAmount) * lateralLoad
        : 0.0f;
    const float loadVisualAlpha = SmoothingAlpha(loadTransfer.enabled ? loadTransfer.visualSmoothing : 12.0f, deltaTime);
    runtimeVehicle.arcadeLoadPitchOffset += (targetLoadPitch - runtimeVehicle.arcadeLoadPitchOffset) * loadVisualAlpha;
    runtimeVehicle.arcadeLoadRollOffset += (targetLoadRoll - runtimeVehicle.arcadeLoadRollOffset) * loadVisualAlpha;

    glm::vec3 moveDelta = runtimeVehicle.arcadePlanarVelocity * deltaTime;
    if (physicsWorld != nullptr && glm::length(moveDelta) > 0.0001f) {
        const glm::vec3 moveDir = glm::normalize(moveDelta);
        const float obstacleSkin = (std::max)(0.0f, groundContact.obstacleSkin);
        PhysicsRaycastHit obstacleHit;
        if (physicsWorld->RaycastIgnoring(
                runtimeVehicle.arcadeChassisWorld.position + glm::vec3(0.0f, groundContact.obstacleProbeHeight, 0.0f),
                moveDir,
                glm::length(moveDelta) + obstacleSkin,
                obstacleHit,
                ignoredObjectIds) &&
            obstacleHit.hit &&
            obstacleHit.normal.y < groundContact.wallNormalYMax) {
            moveDelta = moveDir * (std::max)(0.0f, obstacleHit.distance - obstacleSkin);
            speed = 0.0f;
            lateralSpeed = 0.0f;
            runtimeVehicle.arcadePlanarVelocity = glm::vec3(0.0f);
            runtimeVehicle.arcadeSideSlipVelocity = 0.0f;
        }
    }

    runtimeVehicle.arcadeChassisWorld.position += moveDelta;
    runtimeVehicle.arcadeWheelContacts.resize(runtimeVehicle.config.wheels.size());
    if (groundContact.enabled && physicsWorld != nullptr && !runtimeVehicle.config.wheels.empty()) {
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
            if (!physicsWorld->RaycastIgnoring(
                    mountWorld + glm::vec3(0.0f, probeUp, 0.0f),
                    glm::vec3(0.0f, -1.0f, 0.0f),
                    probeDistance,
                    hit,
                    ignoredObjectIds) ||
                !hit.hit ||
                hit.normal.y < groundContact.minGroundNormalY) {
                continue;
            }

            sample.hit = true;
            sample.contactPosition = hit.position;
            sample.normal = glm::normalize(hit.normal);
            sample.surface = surfaceLookup ? surfaceLookup(hit.objectId) : defaultSurface;
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
            targetPitch += runtimeVehicle.arcadeLoadPitchOffset;
            targetRoll += runtimeVehicle.arcadeLoadRollOffset;

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
            const raceman::physics::WheelConfig& wheel = runtimeVehicle.config.wheels[wheelIndex];
            const WheelHitSample& sample = wheelHits[wheelIndex];
            RuntimeVehicleInstance::WheelContact& contact = runtimeVehicle.arcadeWheelContacts[wheelIndex];
            const glm::vec3 wheelLocalWorld = finalYawRotation * sample.localScene;
            const glm::vec3 mountWorld = runtimeVehicle.arcadeChassisWorld.position + wheelLocalWorld;
            contact.grounded = sample.hit;
            contact.normal = sample.hit ? sample.normal : glm::vec3(0.0f, 1.0f, 0.0f);
            contact.surfaceType = sample.surface.type;
            contact.surfaceGripMultiplier = sample.hit ? (std::max)(0.0f, sample.surface.gripMultiplier) : (std::max)(0.0f, defaultSurface.gripMultiplier);
            contact.surfaceRollingDrag = sample.hit ? (std::max)(0.0f, sample.surface.rollingDrag) : (std::max)(0.0f, defaultSurface.rollingDrag);
            contact.slipAngle = runtimeVehicle.arcadeSlipAngle;
            contact.tractionScale = runtimeVehicle.arcadeTractionScale;
            const float loadEffect = runtimeVehicle.config.loadTransfer.enabled
                ? (std::max)(0.0f, runtimeVehicle.config.loadTransfer.loadGripEffect)
                : 0.0f;
            const float frontRearLoad = wheel.mountPosition.y >= 0.0f
                ? -runtimeVehicle.arcadeLongitudinalLoad
                : runtimeVehicle.arcadeLongitudinalLoad;
            const float sideLoad = wheel.mountPosition.x >= 0.0f
                ? runtimeVehicle.arcadeLateralLoad
                : -runtimeVehicle.arcadeLateralLoad;
            const float aeroBalance = (std::clamp)(runtimeVehicle.config.loadTransfer.aeroBalance, 0.0f, 1.0f);
            const float aeroAxleWeight = wheel.mountPosition.y >= 0.0f ? aeroBalance : (1.0f - aeroBalance);
            contact.loadMultiplier = runtimeVehicle.config.loadTransfer.enabled
                ? (std::clamp)(1.0f + (frontRearLoad + sideLoad) * loadEffect + runtimeVehicle.arcadeAeroGripBoost * aeroAxleWeight,
                               0.35f,
                               2.50f)
                : 1.0f;
            if (sample.hit) {
                const float suspensionLength = (std::max)(0.0f, mountWorld.y - sample.contactPosition.y - sample.radius);
                contact.suspensionTravel = (std::clamp)(sample.restLength - suspensionLength, 0.0f, sample.restLength);
                contact.contactPosition = sample.contactPosition;
                contact.wheelCenterPosition = sample.contactPosition + contact.normal * sample.radius;
                const float compression = sample.restLength > 0.0f ? contact.suspensionTravel / sample.restLength : 0.0f;
                contact.normalForce = (2500.0f + compression * 2500.0f) * contact.loadMultiplier;
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
            contact.surfaceType = defaultSurface.type;
            contact.surfaceGripMultiplier = (std::max)(0.0f, defaultSurface.gripMultiplier);
            contact.surfaceRollingDrag = (std::max)(0.0f, defaultSurface.rollingDrag);
            contact.slipAngle = runtimeVehicle.arcadeSlipAngle;
            contact.tractionScale = runtimeVehicle.arcadeTractionScale;
            contact.loadMultiplier = 1.0f;
            contact.suspensionTravel = 0.0f;
            contact.normalForce = 0.0f;
            contact.wheelCenterPosition = mountWorld + glm::vec3(0.0f, -(restLength + radius), 0.0f);
            contact.contactPosition = contact.wheelCenterPosition - glm::vec3(0.0f, radius, 0.0f);
            contact.angularVelocity = speed / radius;
        }
    }
}

} // namespace raceman

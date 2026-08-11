#pragma once

#include "SceneEditorTypes.h"
#include "../physics/PhysicsWorld.h"
#include "../physics/VehicleConfig.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_set>

namespace raceman {

class PhysicsWorld;

// Rigid-body properties the impact solver needs. Everything here comes from the
// vehicle profile so two cars with different mass and inertia react differently
// to the same wall.
struct VehicleChassisImpactBody {
    float mass{1200.0f};
    float yawInertia{2200.0f};
    // Centre of mass in world space. The lever arm from here to the contact point
    // is what turns a corner impact into a spin.
    glm::vec3 centerOfMassWorld{0.0f};
    physics::VehicleCollisionConfig collision{};
};

struct VehicleChassisCollisionResult {
    bool collided{false};
    glm::vec3 impactNormal{0.0f};
    glm::vec3 contactPosition{0.0f};
    // Closing speed along the contact normal at the moment of impact, in m/s.
    float impactSpeed{0.0f};
    // Magnitude of the normal impulse applied, in newton-seconds.
    float normalImpulse{0.0f};
    // Spin the impulse added, in degrees per second.
    float yawImpulseDegrees{0.0f};
    int slideIterations{0};
    int depenetrationContacts{0};
    std::string hitObjectId;
};

// Sweeps the chassis shape along moveDelta, stops it at the first blocking
// surface, then slides the rest of the motion along that surface.
//
// At each contact it solves a proper collision impulse:
//
//   j = -(1 + e) * (v_contact . n) / (1/m + (r x n)_y^2 / Iyaw)
//
// where r is the contact offset from the centre of mass. The impulse is applied
// to both the linear velocity (j*n/m) and the yaw rate (j*(r x n)_y / Iyaw), then
// a Coulomb-clamped tangential impulse handles scrubbing along the wall. That is
// why a square-on hit only slows the car while a corner hit also spins it.
//
// planarVelocity is m/s in world space; yawRateDegrees is degrees/s to match the
// arcade solver's convention. Both are updated in place.
//
// Ground is excluded via wallNormalYMax - the wheel raycasts own vertical
// support, so only near-vertical surfaces are treated as blockers.
VehicleChassisCollisionResult ResolveVehicleChassisCollision(const PhysicsQueryShapeHandle& chassisShape,
                                                             PhysicsWorld* physicsWorld,
                                                             const VehicleChassisCollisionConfig& config,
                                                             const VehicleChassisImpactBody& body,
                                                             const std::unordered_set<std::string>& ignoredObjectIds,
                                                             const glm::vec3& chassisPosition,
                                                             const glm::vec3& chassisRotationEuler,
                                                             float wallNormalYMax,
                                                             glm::vec3& moveDelta,
                                                             glm::vec3& planarVelocity,
                                                             float& yawRateDegrees);

} // namespace raceman

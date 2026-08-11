#include "SceneEditorVehicleChassisCollision.h"

#include "../physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace raceman {
namespace {

constexpr float kMinMoveLength = 0.000001f;
constexpr float kDegreesPerRadian = 57.2957795131f;
constexpr float kRadiansPerDegree = 0.01745329251f;

glm::vec3 ProjectOntoPlane(const glm::vec3& v, const glm::vec3& normal) {
    return v - normal * glm::dot(v, normal);
}

glm::vec3 FlattenToPlanar(const glm::vec3& v) {
    return glm::vec3(v.x, 0.0f, v.z);
}

// Angular velocity about +Y crossed with a lever arm: (0, w, 0) x (rx, ry, rz).
glm::vec3 YawCrossLever(float yawRateRadians, const glm::vec3& lever) {
    return glm::vec3(yawRateRadians * lever.z, 0.0f, -yawRateRadians * lever.x);
}

// Y component of (lever x axis) - the only part that drives yaw.
float YawLeverTerm(const glm::vec3& lever, const glm::vec3& axis) {
    return lever.z * axis.x - lever.x * axis.z;
}

struct ImpactImpulse {
    float normalImpulse{0.0f};
    float yawDeltaDegrees{0.0f};
    float closingSpeed{0.0f};
};

// Solves and applies one collision impulse at a contact. Linear and angular
// response both fall out of the same scalar j, so the split between "slowed down"
// and "spun around" is decided by the lever arm rather than by a tuning knob.
ImpactImpulse ApplyContactImpulse(const VehicleChassisImpactBody& body,
                                  const glm::vec3& contactPosition,
                                  const glm::vec3& normal,
                                  glm::vec3& planarVelocity,
                                  float& yawRateDegrees) {
    ImpactImpulse result;

    const physics::VehicleCollisionConfig& collision = body.collision;
    const float mass = (std::max)(1.0f, body.mass);
    const float yawInertia = (std::max)(1.0f, body.yawInertia);
    const float invMass = 1.0f / mass;
    const float invYawInertia = 1.0f / yawInertia;
    // A zero obstacle mass means immovable world geometry, so it contributes no
    // compliance to the effective mass.
    const float invObstacleMass = collision.obstacleMass > 0.0f ? 1.0f / collision.obstacleMass : 0.0f;

    const glm::vec3 lever = FlattenToPlanar(contactPosition - body.centerOfMassWorld);
    const glm::vec3 planarNormal = FlattenToPlanar(normal);
    const float planarNormalLength = glm::length(planarNormal);
    if (planarNormalLength <= kMinMoveLength) {
        return result;
    }
    const glm::vec3 n = planarNormal / planarNormalLength;

    float yawRateRadians = yawRateDegrees * kRadiansPerDegree;
    const glm::vec3 contactVelocity = planarVelocity + YawCrossLever(yawRateRadians, lever);
    const float closingSpeed = glm::dot(contactVelocity, n);
    result.closingSpeed = -closingSpeed;

    // Separating or barely touching: leave it to depenetration.
    if (closingSpeed >= -(std::max)(0.0f, collision.minImpactSpeed)) {
        return result;
    }

    const float restitution = (std::clamp)(collision.restitution, 0.0f, 1.0f);
    const float normalLever = YawLeverTerm(lever, n);
    const float invEffectiveMass = invMass + invObstacleMass + normalLever * normalLever * invYawInertia;
    if (invEffectiveMass <= kMinMoveLength) {
        return result;
    }

    float normalImpulse = -(1.0f + restitution) * closingSpeed / invEffectiveMass;
    normalImpulse *= (std::clamp)(collision.impulseRetention, 0.0f, 2.0f);
    if (normalImpulse <= 0.0f) {
        return result;
    }

    planarVelocity += n * (normalImpulse * invMass);
    const float yawResponse = (std::max)(0.0f, collision.yawResponse);
    yawRateRadians += normalImpulse * normalLever * invYawInertia * yawResponse;

    // Coulomb friction along the contact tangent, using the velocity left after
    // the normal impulse so the two do not fight each other.
    const glm::vec3 postNormalContactVelocity = planarVelocity + YawCrossLever(yawRateRadians, lever);
    const glm::vec3 tangentVelocity = ProjectOntoPlane(postNormalContactVelocity, n);
    const float tangentSpeed = glm::length(tangentVelocity);
    if (tangentSpeed > kMinMoveLength) {
        const glm::vec3 t = tangentVelocity / tangentSpeed;
        const float tangentLever = YawLeverTerm(lever, t);
        const float invTangentMass = invMass + invObstacleMass + tangentLever * tangentLever * invYawInertia;
        if (invTangentMass > kMinMoveLength) {
            const float frictionLimit = (std::max)(0.0f, collision.friction) * normalImpulse;
            const float tangentImpulse =
                (std::clamp)(-tangentSpeed / invTangentMass, -frictionLimit, frictionLimit);
            planarVelocity += t * (tangentImpulse * invMass);
            yawRateRadians += tangentImpulse * tangentLever * invYawInertia * yawResponse;
        }
    }

    // Crumple: bleed extra kinetic energy in proportion to how hard the hit was,
    // normalised against the momentum the car would carry at 10 m/s.
    const float energyLoss = (std::clamp)(collision.energyLoss, 0.0f, 1.0f);
    if (energyLoss > 0.0f) {
        const float severity = (std::clamp)(normalImpulse / (mass * 10.0f), 0.0f, 1.0f);
        const float retain = 1.0f - energyLoss * severity;
        planarVelocity *= retain;
        yawRateRadians *= retain;
    }

    const float newYawRateDegrees = yawRateRadians * kDegreesPerRadian;
    const float maxImpactYawRate = (std::max)(0.0f, collision.maxImpactYawRate);
    const float yawDelta =
        (std::clamp)(newYawRateDegrees - yawRateDegrees, -maxImpactYawRate, maxImpactYawRate);
    yawRateDegrees += yawDelta;

    result.normalImpulse = normalImpulse;
    result.yawDeltaDegrees = yawDelta;
    return result;
}

} // namespace

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
                                                             float& yawRateDegrees) {
    VehicleChassisCollisionResult result;
    if (physicsWorld == nullptr || !chassisShape || !config.enabled ||
        config.mode == VehicleChassisCollisionMode::None) {
        return result;
    }

    const bool impulseEnabled = body.collision.enabled;
    const float skinWidth = (std::max)(0.0f, config.skinWidth);
    const int maxIterations = (std::clamp)(config.maxSlideIterations, 1, 8);
    // The same surface friction that scrubs the impulse also scrubs the leftover
    // motion, so a grippy wall drags the car down and a smooth one lets it run.
    const float slideScrub = (std::clamp)(body.collision.friction, 0.0f, 1.0f);

    glm::vec3 resolvedDelta{0.0f};
    glm::vec3 remaining = moveDelta;

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        const float remainingLength = glm::length(remaining);
        if (remainingLength <= kMinMoveLength) {
            break;
        }

        PhysicsShapeCastHit hit;
        if (!physicsWorld->ShapeCast(chassisShape,
                                     chassisPosition + resolvedDelta,
                                     chassisRotationEuler,
                                     remaining,
                                     hit,
                                     ignoredObjectIds,
                                     wallNormalYMax) ||
            !hit.hit) {
            resolvedDelta += remaining;
            remaining = glm::vec3(0.0f);
            break;
        }

        result.collided = true;
        result.impactNormal = hit.normal;
        result.contactPosition = hit.position;
        result.hitObjectId = hit.objectId;
        result.slideIterations = iteration + 1;

        // Advance up to the contact, leaving a skin so the next sweep does not
        // start already touching the surface.
        const glm::vec3 moveDir = remaining / remainingLength;
        const float safeDistance = (std::max)(0.0f, hit.distance - skinWidth);
        const glm::vec3 step = moveDir * safeDistance;
        resolvedDelta += step;
        remaining -= step;

        if (impulseEnabled) {
            const ImpactImpulse impulse =
                ApplyContactImpulse(body, hit.position, hit.normal, planarVelocity, yawRateDegrees);
            result.impactSpeed = (std::max)(result.impactSpeed, impulse.closingSpeed);
            result.normalImpulse = (std::max)(result.normalImpulse, impulse.normalImpulse);
            result.yawImpulseDegrees += impulse.yawDeltaDegrees;
        } else {
            // Impulses disabled: fall back to cancelling the inbound velocity so the
            // car still cannot drive through the wall.
            const float approachSpeed = glm::dot(planarVelocity, hit.normal);
            if (approachSpeed < 0.0f) {
                result.impactSpeed = (std::max)(result.impactSpeed, -approachSpeed);
                planarVelocity = ProjectOntoPlane(planarVelocity, hit.normal);
            }
        }

        // Slide the leftover motion along the wall and scrub it.
        remaining = ProjectOntoPlane(remaining, hit.normal) * (1.0f - slideScrub);
    }

    moveDelta = resolvedDelta;

    if (config.enableDepenetration) {
        std::vector<PhysicsShapeOverlap> overlaps;
        if (physicsWorld->CollideShape(chassisShape,
                                       chassisPosition + moveDelta,
                                       chassisRotationEuler,
                                       overlaps,
                                       ignoredObjectIds,
                                       wallNormalYMax)) {
            const float maxPush = (std::max)(0.0f, config.maxDepenetrationPerStep);
            glm::vec3 push{0.0f};
            for (const PhysicsShapeOverlap& overlap : overlaps) {
                // Only push along axes we are not already escaping, so opposing
                // contacts do not cancel into a jitter loop.
                const float alreadyPushed = glm::dot(push, overlap.normal);
                const float needed = overlap.penetrationDepth + skinWidth - alreadyPushed;
                if (needed <= 0.0f) {
                    continue;
                }
                push += overlap.normal * needed;
                ++result.depenetrationContacts;
            }

            const float pushLength = glm::length(push);
            if (pushLength > kMinMoveLength) {
                if (pushLength > maxPush) {
                    push *= maxPush / pushLength;
                }
                moveDelta += push;
                result.collided = true;
            }
        }
    }

    return result;
}

} // namespace raceman

#include "SceneEditorVehicleWheelSlip.h"

#include "SceneEditorInternal.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

namespace raceman {
namespace {

using namespace scene_editor_internal;

// Slip ratio a tyre carries at peak longitudinal grip. Below this the tread is
// deforming, not sliding, so it marks nothing however fast the car is going.
constexpr float kPeakSlipRatio = 0.12f;
// Same idea laterally: a tyre needs a few degrees of slip angle to make any
// side force at all, and only what is past the peak is actually scrub.
constexpr float kPeakSlipAngleDegrees = 8.0f;
// Contact patch sliding speed that counts as "as loud and as dark as it gets".
constexpr float kFullSlideSpeed = 6.0f;
// Below this the slip ratio denominator would blow up and a crawling wheel
// would report huge slip from a millimetre of movement.
constexpr float kMinReferenceSpeed = 1.5f;
// A spinning wheel is limited by the drivetrain, not by how fast the car is
// going: a standing burnout has the tread doing 20 m/s while the car does
// nothing. Scaling spin off road speed alone would make exactly that case, the
// most obvious rubber-laying event there is, the quietest one.
constexpr float kSpinSurfaceSpeed = 6.0f;
// Tyre relaxation length. Slip does not appear the instant load does; the
// carcass has to wind up first, and this is what stops effects strobing on a
// kerb or on a single-frame input spike.
constexpr float kRelaxationLength = 0.45f;

float SlipSmoothingAlpha(float speed, float deltaTime) {
    const float rate = (std::max)(2.5f, std::fabs(speed) / kRelaxationLength);
    return (std::clamp)(1.0f - std::exp(-rate * deltaTime), 0.0f, 1.0f);
}

bool IsFrontWheel(const raceman::physics::WheelConfig& wheel) {
    return wheel.mountPosition.y >= 0.0f;
}

} // namespace

void UpdateArcadeWheelSlip(RuntimeVehicleInstance& runtimeVehicle,
                           const ArcadeVehicleInput& input,
                           const VehicleControlAmounts& controls,
                           const VehicleDriveRatios& driveRatios,
                           float deltaTime) {
    (void)controls;
    const raceman::physics::VehicleConfig& config = runtimeVehicle.config;
    const std::size_t wheelCount =
        (std::min)(config.wheels.size(), runtimeVehicle.arcadeWheelContacts.size());
    if (wheelCount == 0 || deltaTime <= 0.0f) {
        return;
    }

    const glm::quat yawRotation =
        glm::angleAxis(glm::radians(runtimeVehicle.arcadeChassisWorld.rotationEuler.y),
                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 sceneUp(0.0f, 1.0f, 0.0f);
    // arcadeYawRate is degrees per second: it is integrated straight into
    // rotationEuler.y. The cross product below needs radians.
    const glm::vec3 yawVector = sceneUp * glm::radians(runtimeVehicle.arcadeYawRate);
    const glm::vec3 chassisForward = yawRotation * glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 chassisRight = yawRotation * glm::vec3(1.0f, 0.0f, 0.0f);

    // Load share decides how much of the drive and brake effort each tyre is
    // being asked to carry. An unloaded inside rear spins on a fraction of the
    // torque that the planted outside rear shrugs off.
    float drivenLoad = 0.0f;
    int drivenCount = 0;
    for (std::size_t wheelIndex = 0; wheelIndex < wheelCount; ++wheelIndex) {
        if (config.wheels[wheelIndex].driven) {
            drivenLoad += (std::max)(0.0f, runtimeVehicle.arcadeWheelContacts[wheelIndex].normalForce);
            ++drivenCount;
        }
    }

    // What the driver is asking the tyres for, in newtons, from the same
    // authored numbers the handling model drives the car with.
    const raceman::physics::VehicleArcadeHandlingConfig& handling = config.arcadeHandling;
    const float mass = (std::max)(1.0f, config.chassis.mass);
    // Same tractive effort curve the handling model pulls the car with, so a
    // wheel cannot spin up on drive the engine is not making in this gear at
    // this speed. Without it, top gear at 60 m/s reads as a burnout.
    const float driveTorqueScale = EngineDriveTorqueScale(
        config,
        runtimeVehicle.arcadeGear,
        runtimeVehicle.arcadeEngineRPM,
        std::fabs(runtimeVehicle.arcadeSpeed),
        (std::max)(1.0f, handling.maxForwardSpeed),
        runtimeVehicle.autoShiftCooldown);
    const float driveForceTotal =
        (std::clamp)(runtimeVehicle.arcadeThrottle, 0.0f, 1.0f) *
        (std::max)(0.0f, handling.acceleration) * mass * driveTorqueScale;
    const float brakePedal = (std::clamp)(runtimeVehicle.arcadeRawBrake, 0.0f, 1.0f) *
                             (std::clamp)(runtimeVehicle.arcadeAbsBrakeScale, 0.0f, 1.0f);
    const float handbrakePedal = (std::clamp)(input.handbrake, 0.0f, 1.0f);
    const float brakeFrontBias = (std::clamp)(config.brakes.frontBias, 0.0f, 1.0f);
    const float steeringInput = (std::clamp)(runtimeVehicle.arcadeSteering, -1.0f, 1.0f);

    for (std::size_t wheelIndex = 0; wheelIndex < wheelCount; ++wheelIndex) {
        const raceman::physics::WheelConfig& wheel = config.wheels[wheelIndex];
        RuntimeVehicleWheelContact& contact = runtimeVehicle.arcadeWheelContacts[wheelIndex];
        contact.previousRotationAngle = contact.rotationAngle;

        const float radius = (std::max)(0.05f, wheel.radius);

        // --- 1. what this corner of the car is doing through the ground -----
        // Chassis velocity plus the yaw term, so the outside wheels in a corner
        // genuinely travel faster than the inside ones, and an oversteering
        // rear sees a slip angle the front does not.
        const glm::vec3 mountOffset = yawRotation * VehicleVectorToScene(wheel.mountPosition);
        const glm::vec3 contactVelocity =
            runtimeVehicle.arcadePlanarVelocity + glm::cross(yawVector, mountOffset);

        const float steerAngle = wheel.maxSteerAngle * steeringInput;
        contact.steerAngle = steerAngle;
        const glm::vec3 wheelForward =
            chassisForward * std::cos(steerAngle) + chassisRight * std::sin(steerAngle);
        const glm::vec3 wheelRight = glm::cross(sceneUp, wheelForward);

        const float longitudinalSpeed = glm::dot(contactVelocity, wheelForward);
        const float lateralSpeed = glm::dot(contactVelocity, wheelRight);
        const float referenceSpeed = (std::max)(std::fabs(longitudinalSpeed), kMinReferenceSpeed);
        const float travelSign = longitudinalSpeed >= 0.0f ? 1.0f : -1.0f;

        // --- 2. how much grip this tyre has right now -----------------------
        const raceman::physics::ResolvedWheelTireConfig tire =
            raceman::physics::resolveWheelTire(config, wheel);
        const float friction = (std::max)(0.05f, tire.gripFactor) *
                               (std::max)(0.05f, contact.surfaceGripMultiplier);
        const float normalForce = (std::max)(0.0f, contact.normalForce);
        const float gripForce = (std::max)(1.0f, friction * normalForce);

        // --- 3. how much it is being asked for ------------------------------
        float driveShare = 0.0f;
        if (wheel.driven) {
            driveShare = drivenLoad > 1.0f
                ? normalForce / drivenLoad
                : 1.0f / static_cast<float>((std::max)(1, drivenCount));
        }
        const float driveForce = driveForceTotal * driveShare;

        const float axleBrakeBias = IsFrontWheel(wheel) ? brakeFrontBias : (1.0f - brakeFrontBias);
        // Authored maxBrakingTorque is what the caliper can make; the bias is
        // the share of it this axle sees. Sized so full pedal on dry tarmac is
        // past the grip limit but part pedal is not - a car you can brake hard
        // in without locking, which is the whole point of threshold braking.
        float brakeTorque = wheel.hasBrake
            ? brakePedal * (std::max)(0.0f, wheel.maxBrakingTorque) * axleBrakeBias
            : 0.0f;
        // The handbrake is a rear-only cable that does not care what the ABS
        // thinks. That is exactly why it locks the rears and pitches the car.
        if (!IsFrontWheel(wheel)) {
            brakeTorque += handbrakePedal * (std::max)(0.0f, wheel.maxBrakingTorque) * 1.10f;
        }
        const float brakeForce = brakeTorque / radius;

        // --- 4. the friction circle -----------------------------------------
        const float longitudinalDemand = (driveForce - brakeForce) / gripForce;
        const float lateralSlipAngle = glm::degrees(
            std::atan2(lateralSpeed, (std::max)(kMinReferenceSpeed, std::fabs(longitudinalSpeed))));
        const float lateralDemand = std::fabs(lateralSlipAngle) / kPeakSlipAngleDegrees;
        const float utilisation =
            std::sqrt(longitudinalDemand * longitudinalDemand + lateralDemand * lateralDemand);
        // Lateral demand eats what is left for braking and driving. That is the
        // whole point of a friction circle: you cannot brake at the limit and
        // turn at the limit at the same time.
        const float clampedLateral = (std::min)(lateralDemand, 1.0f);
        const float longitudinalCapacity =
            std::sqrt((std::max)(0.0f, 1.0f - clampedLateral * clampedLateral));

        // --- 5. resolve the wheel's rotation ---------------------------------
        // The braking side blends rolling speed down to a dead stop, the
        // driving side pushes it above rolling. Both are exact at the ends, so
        // a locked wheel really is stopped rather than nearly stopped.
        float targetSlipRatio = 0.0f;
        float lockFraction = 0.0f;
        if (longitudinalDemand < 0.0f) {
            const float over = -longitudinalDemand - longitudinalCapacity;
            targetSlipRatio = over <= 0.0f
                ? longitudinalDemand * kPeakSlipRatio
                : -(kPeakSlipRatio + over * 0.85f);
            targetSlipRatio = (std::max)(targetSlipRatio, -1.0f);
            lockFraction = (std::clamp)(-targetSlipRatio, 0.0f, 1.0f);
        } else {
            const float over = longitudinalDemand - longitudinalCapacity;
            targetSlipRatio = over <= 0.0f
                ? longitudinalDemand * kPeakSlipRatio
                : kPeakSlipRatio + over * 0.85f;
            // A spinning wheel tops out: past this the surface speed is silly
            // and the visual rotation reads as a glitch rather than a burnout.
            targetSlipRatio = (std::min)(targetSlipRatio, 1.5f);
        }
        if (!contact.grounded) {
            // Nothing to slip against. A wheel in the air just keeps turning at
            // whatever the drivetrain and the brakes leave it doing.
            targetSlipRatio = (wheel.driven && driveForceTotal > 0.0f) ? 0.6f : 0.0f;
            lockFraction = brakePedal > 0.5f ? 1.0f : 0.0f;
            if (lockFraction > 0.0f) {
                targetSlipRatio = -1.0f;
            }
        }

        const float alpha = SlipSmoothingAlpha(longitudinalSpeed, deltaTime);
        contact.slipRatio += (targetSlipRatio - contact.slipRatio) * alpha;
        contact.lateralSlipAngle += (lateralSlipAngle - contact.lateralSlipAngle) * alpha;

        const float rollingAngular = longitudinalSpeed / radius;
        float angularVelocity = rollingAngular;
        if (contact.slipRatio < 0.0f) {
            angularVelocity = rollingAngular * (std::clamp)(1.0f + contact.slipRatio, 0.0f, 1.0f);
        } else {
            const float spinReference = referenceSpeed + kSpinSurfaceSpeed;
            angularVelocity = rollingAngular + contact.slipRatio * spinReference * travelSign / radius;
        }
        contact.angularVelocity = angularVelocity;
        contact.rotationAngle += angularVelocity * deltaTime;

        // --- 6. what actually scrubs -----------------------------------------
        // Only the part past the adhesion peak is rubber leaving the tyre. The
        // deformation below it is a gripping tyre doing its job silently, which
        // is why a fast car in a straight line lays nothing.
        const float longitudinalSlide = (std::max)(
            0.0f,
            std::fabs(angularVelocity * radius - longitudinalSpeed) - kPeakSlipRatio * referenceSpeed);
        const float lateralSlide = (std::max)(
            0.0f,
            std::fabs(lateralSpeed) - std::tan(glm::radians(kPeakSlipAngleDegrees)) * referenceSpeed);
        const float slipVelocity =
            std::sqrt(longitudinalSlide * longitudinalSlide + lateralSlide * lateralSlide);

        contact.slipVelocity = contact.grounded ? slipVelocity : 0.0f;
        contact.lateralSlideMps = contact.grounded ? lateralSlide : 0.0f;
        contact.longitudinalSlideMps = contact.grounded ? longitudinalSlide : 0.0f;
        contact.gripUtilisation = utilisation;
        contact.slipAmount = (std::clamp)(contact.slipVelocity / kFullSlideSpeed, 0.0f, 1.0f);
        contact.locked = contact.grounded && lockFraction > 0.55f && std::fabs(longitudinalSpeed) > 1.0f;
        contact.spinning = contact.grounded && contact.slipRatio > kPeakSlipRatio * 2.0f;

        // Keep the older per-wheel fields meaningful for anything still reading
        // them, but sourced from this wheel rather than from the chassis.
        contact.slipAngle = contact.lateralSlipAngle;
        contact.tractionScale =
            (std::clamp)(1.0f - (std::max)(0.0f, utilisation - 1.0f) * 0.6f, 0.15f, 1.0f);
    }

    (void)driveRatios;
}

} // namespace raceman

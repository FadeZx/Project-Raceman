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
// Tyre surface speed has to genuinely outrun the ground by this much before
// it reads as wheelspin - small numeric noise in the integrated slip should
// not flicker the effect on and off.
constexpr float kWheelspinExcessSpeed = 2.0f;
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
    // The same longitudinal-grip cut ApplyArcadeDrivetrain already applied to
    // the throttle this frame (config.tireGrip.longitudinalGrip, surface grip,
    // and the friction-circle tractionScale). Without it this model is asking
    // for more force than the drivetrain ever actually sent to the road, so it
    // reports wheelspin the car itself isn't having.
    const float driveGripScale = config.tireGrip.enabled
        ? runtimeVehicle.arcadeSurfaceGrip * (std::max)(0.0f, config.tireGrip.longitudinalGrip) *
              runtimeVehicle.arcadeTractionScale
        : 1.0f;
    const float driveForceTotal =
        (std::clamp)(runtimeVehicle.arcadeThrottle, 0.0f, 1.0f) *
        (std::max)(0.0f, handling.acceleration) * mass * driveTorqueScale * driveGripScale;
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

        // --- 5. this wheel's own spin, genuinely simulated -------------------
        // angularVelocity is real per-wheel state carried over from the last
        // tick. Slip is measured from it, a tyre force is computed from that
        // measured slip, and the force is fed back as a torque that changes
        // next tick's angularVelocity - a real feedback loop, not a slip
        // value chosen to match how hard a pedal is pressed.
        const float alpha = SlipSmoothingAlpha(longitudinalSpeed, deltaTime);
        contact.lateralSlipAngle += (lateralSlipAngle - contact.lateralSlipAngle) * alpha;

        const float previousWheelSurfaceSpeed = contact.angularVelocity * radius;
        // referenceSpeed is already floored to kMinReferenceSpeed, so dividing
        // by it is always safe - including standing still, where a burnout
        // still reads as a large ratio without the result being in raw m/s
        // instead of a normalized slip ratio. That unit mismatch used to
        // saturate the tyre force every frame for any wheel with the slightest
        // residual spin at a standstill, kicking it back and forth instead of
        // letting it settle.
        const float unclampedSlipRatio = contact.grounded
            ? (previousWheelSurfaceSpeed - longitudinalSpeed) / referenceSpeed
            : 0.0f;
        const float rawSlipRatio = (std::clamp)(unclampedSlipRatio, -3.0f, 3.0f);
        // Tyre relaxation: the carcass has to wind up before the slip it is
        // carrying catches up with the slip the contact patch is measuring.
        contact.slipRatio += (rawSlipRatio - contact.slipRatio) * alpha;

        // Linear brush-tyre force from that relaxed, measured slip, capped by
        // whatever the friction circle has left after lateral demand takes
        // its share.
        const float maxLongitudinalForce = gripForce * longitudinalCapacity;
        float longitudinalTireForce = contact.grounded
            ? (std::clamp)(contact.slipRatio * tire.longitudinalStiffness,
                           -maxLongitudinalForce, maxLongitudinalForce)
            : 0.0f;
        if (contact.grounded && std::fabs(longitudinalSpeed) > 0.05f) {
            longitudinalTireForce -= travelSign * normalForce * 0.015f;
        }

        // Drive torque spins the wheel up; the tyre force above is the
        // ground pushing back, exactly as much as the tyre can actually
        // find. The caliper is metal on a disc - it does not care about
        // grip - but the wheel it is clamping is still only connected to the
        // road through that same tyre force, so brake torque has to fight
        // the same reactionTorque drive does. A caliper that can out-torque
        // the tyre locks the wheel; one that can't just decelerates it in
        // step with the car, which is what "threshold braking" is.
        const float wheelInertia = (std::max)(0.05f, wheel.inertia);
        const float driveTorque = driveForce * radius;
        const float reactionTorque = longitudinalTireForce * radius;
        const float brakeSpinDirection = contact.angularVelocity != 0.0f
            ? (contact.angularVelocity > 0.0f ? 1.0f : -1.0f)
            : travelSign;

        // The reaction term is the tyre resisting slip - on its own it should
        // only pull the wheel back toward matching ground speed, never past
        // it. At low reference speed the discrete step could otherwise
        // overshoot zero slip and hand the next frame an equally large slip
        // of the opposite sign, ringing for several visible cycles instead of
        // settling - exactly what a wheel with a little leftover road speed
        // after a hard stop was doing, on all four corners regardless of
        // drive or brake. Drive and brake are real, sustained torques and are
        // still allowed to carry the wheel away from ground speed - that is
        // genuine spin or lock - so only the reaction contribution is capped.
        const float groundAngularVelocity = longitudinalSpeed / radius;
        float reactionDelta = (-reactionTorque / wheelInertia) * deltaTime;
        const float afterReaction = contact.angularVelocity + reactionDelta;
        const bool reactionOvershoots =
            (contact.angularVelocity - groundAngularVelocity >= 0.0f && afterReaction - groundAngularVelocity < 0.0f) ||
            (contact.angularVelocity - groundAngularVelocity <= 0.0f && afterReaction - groundAngularVelocity > 0.0f);
        if (reactionOvershoots) {
            reactionDelta = groundAngularVelocity - contact.angularVelocity;
        }

        const float driveBrakeTorque = driveTorque - brakeTorque * brakeSpinDirection;
        float angularVelocity =
            contact.angularVelocity + reactionDelta + (driveBrakeTorque / wheelInertia) * deltaTime;

        if (brakeTorque > 0.0f &&
            ((contact.angularVelocity >= 0.0f && angularVelocity < 0.0f) ||
             (contact.angularVelocity <= 0.0f && angularVelocity > 0.0f))) {
            // A real lock stops the wheel; it does not fling it into reverse
            // spin and buzz back and forth every frame once brake torque has
            // out-fought the tyre.
            angularVelocity = 0.0f;
        }

        // Nothing is actually asking this wheel for anything and the car is
        // parked: snap straight to ground speed instead of waiting out the
        // slip relaxation filter. That filter is deliberately slow (it is
        // what stops a kerb strobing the effect on and off), which otherwise
        // left a wheel that had merely rolled to a stop still reading a
        // stale spin for a second or more after the car was actually still -
        // and the harder it had been spun up before stopping, the longer
        // that tail lasted, since the old version only did this once the
        // wheel's own residual was already small. A free, undriven,
        // unbraked wheel does not keep quietly spinning once the car under
        // it is stopped, however hard it was spinning a moment ago.
        if (contact.grounded && driveTorque == 0.0f && brakeTorque <= 0.0f &&
            std::fabs(longitudinalSpeed) < 0.05f) {
            angularVelocity = 0.0f;
            contact.slipRatio = 0.0f;
        }

        // Safety bound, not a behaviour driver: stops an extended jump with
        // the throttle pinned from spinning the wheel up forever with
        // nothing to react against.
        const float maxAngularSpeed = (referenceSpeed + kSpinSurfaceSpeed * 4.0f) / radius;
        angularVelocity = (std::clamp)(angularVelocity, -maxAngularSpeed, maxAngularSpeed);

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
        // Locked: the wheel's real spin has actually been arrested while the
        // car is still moving over it - not a threshold on brake pedal
        // pressure.
        contact.locked = contact.grounded && std::fabs(angularVelocity * radius) < 1.0f &&
                         std::fabs(longitudinalSpeed) > 1.0f;
        // Spinning: the tyre surface is genuinely outrunning the ground -
        // drive torque beat the grip that was actually there to resist it.
        contact.spinning = contact.grounded &&
                           (angularVelocity * radius - longitudinalSpeed) > kWheelspinExcessSpeed;

        // Keep the older per-wheel fields meaningful for anything still reading
        // them, but sourced from this wheel rather than from the chassis.
        contact.slipAngle = contact.lateralSlipAngle;
        contact.tractionScale =
            (std::clamp)(1.0f - (std::max)(0.0f, utilisation - 1.0f) * 0.6f, 0.15f, 1.0f);
    }

    (void)driveRatios;
}

} // namespace raceman

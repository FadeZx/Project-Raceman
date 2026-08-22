#pragma once

#include "SceneEditorTypes.h"
#include "SceneEditorVehicleTelemetry.h"
#include "../physics/PhysicsWorld.h"
#include "../physics/VehicleConfig.h"
#include "../physics/VehicleEngineState.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace raceman {

struct RuntimeVehicleWheelContact {
    bool grounded{false};
    glm::vec3 contactPosition{0.0f};
    glm::vec3 wheelCenterPosition{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float suspensionTravel{0.0f};
    float normalForce{0.0f};
    float angularVelocity{0.0f};
    TrackSurfaceType surfaceType{TrackSurfaceType::Asphalt};
    float surfaceGripMultiplier{1.0f};
    float surfaceRollingDrag{0.08f};
    float slipAngle{0.0f};
    float tractionScale{1.0f};
    float loadMultiplier{1.0f};

    // --- per-wheel tyre state (SceneEditorVehicleWheelSlip) ---------------
    // Everything below describes THIS tyre, not the car. It is what the sim
    // knows at one instant, and skid marks, tyre sound, force feedback and the
    // wheel's own rotation all read it, so a locked front-left marks, squeals,
    // stops turning and lightens the wheel from the same numbers.
    float steerAngle{0.0f};        // radians, this wheel's actual steer
    // -1 fully locked, 0 rolling, >0 spinning up. Signed on purpose: lock-up
    // and wheelspin are different events and effects want to tell them apart.
    float slipRatio{0.0f};
    float lateralSlipAngle{0.0f};  // degrees, from this wheel's contact velocity
    // Metres per second the contact patch is actually sliding over the ground.
    // The physical source for how hard a tyre marks, smokes and sings.
    float slipVelocity{0.0f};
    // The same slide split into its two axes. They are different events with
    // different sounds: lateral scrub is a tyre singing at the limit,
    // longitudinal slide is a lock-up graunch or a wheelspin chirp. Collapsing
    // them into slipVelocity alone makes all three sound identical.
    float lateralSlideMps{0.0f};
    float longitudinalSlideMps{0.0f};
    // Demand over capacity on the friction circle. 1.0 is exactly the limit,
    // below it the tyre is gripping, above it it is sliding.
    float gripUtilisation{0.0f};
    // slipVelocity mapped to 0..1 for effects that want a normalised amount.
    float slipAmount{0.0f};
    bool locked{false};            // brake torque beat the available grip
    bool spinning{false};          // drive torque beat the available grip
    // Accumulated wheel angle in radians, integrated from angularVelocity, so a
    // locked wheel visibly stops and a spinning one visibly races.
    float rotationAngle{0.0f};
    float previousRotationAngle{0.0f};
};

struct RuntimeVehicleInstance {
    using WheelContact = RuntimeVehicleWheelContact;

    std::string objectId;
    int objectIndex{-1};
    std::string chassisBodyObjectId;
    std::vector<int> wheelObjectIndices;
    std::vector<VehicleWheelBinding> wheelBindings;
    std::vector<Transform> wheelAuthoredLocalTransforms;
    std::vector<glm::vec3> wheelAuthoredRotationEuler;
    float smoothedKeyboardSteering{0.0f};
    float smoothedKeyboardThrottle{0.0f};
    float smoothedKeyboardBrake{0.0f};
    bool pendingShiftUp{false};
    bool pendingShiftDown{false};
    bool pendingNeutral{false};
    bool pendingReverse{false};
    float autoShiftCooldown{0.0f};
    physics::VehicleConfig config;
    int manualGear{0};
    bool arcadeInitialized{false};
    Transform arcadePreviousChassisWorld{};
    Transform arcadeChassisWorld{};
    glm::vec3 arcadePlanarVelocity{0.0f};
    float arcadeSpeed{0.0f};
    float arcadeLateralSpeed{0.0f};
    float arcadeEngineRPM{900.0f};
    float arcadePreviousWheelSpin{0.0f};
    float arcadeWheelSpin{0.0f};
    float arcadeThrottle{0.0f};
    float arcadePreviousThrottle{0.0f};
    float arcadeBrake{0.0f};
    float arcadeRawThrottle{0.0f};
    float arcadeRawBrake{0.0f};
    float arcadeTractionControlCut{0.0f};
    float arcadeAbsBrakeScale{1.0f};
    float arcadeDifferentialLock{0.0f};
    float arcadeSteering{0.0f};
    float arcadeHandbrake{0.0f};
    float arcadeVerticalVelocity{0.0f};
    float arcadeSlipAngle{0.0f};
    float arcadeTractionScale{1.0f};
    float arcadeSurfaceGrip{1.0f};
    float arcadeFrontSlip{0.0f};
    float arcadeRearSlip{0.0f};
    float arcadeSideSlipVelocity{0.0f};
    float arcadeGripBalance{0.0f};
    float arcadeVelocitySlipAngle{0.0f};
    float arcadeYawTorque{0.0f};
    float arcadeTireScrub{0.0f};
    float arcadeAeroGripBoost{0.0f};
    float arcadeLongitudinalLoad{0.0f};
    float arcadeLateralLoad{0.0f};
    float arcadeLoadPitchOffset{0.0f};
    float arcadeLoadRollOffset{0.0f};
    float arcadeYawRate{0.0f};
    float arcadeOversteerAmount{0.0f};
    float arcadeSpinAmount{0.0f};
    int arcadeGear{1};
    // Inertial engine model layered on top of the kinematic arcade RPM. Drives
    // the procedural engine sound; handling does not read it.
    physics::VehicleEngineState engineState{};
    float smoothedWheelSteering{0.0f};
    bool smoothedWheelSteeringInitialized{false};
    WheelForceFeedbackRuntimeState forceFeedbackState{};
    std::vector<WheelContact> arcadeWheelContacts;

    // Chassis collision. The query shape is cooked once when the runtime is built;
    // the config is copied so the fixed step never reads back into the scene graph.
    VehicleChassisCollisionConfig chassisCollision;
    PhysicsQueryShapeHandle chassisQueryShape;
    std::vector<PhysicsColliderDesc> chassisColliderDescs;
    bool chassisCollided{false};
    float chassisImpactSpeed{0.0f};
    float chassisNormalImpulse{0.0f};
    float chassisYawImpulseDegrees{0.0f};
    glm::vec3 chassisImpactNormal{0.0f};
    glm::vec3 chassisContactPosition{0.0f};
};

} // namespace raceman

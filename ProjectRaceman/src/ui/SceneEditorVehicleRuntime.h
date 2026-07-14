#pragma once

#include "SceneEditorTypes.h"
#include "../physics/VehicleConfig.h"

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
    std::vector<WheelContact> arcadeWheelContacts;
};

} // namespace raceman

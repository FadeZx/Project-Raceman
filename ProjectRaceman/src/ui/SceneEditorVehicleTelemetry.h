#pragma once

#include "../physics/VehicleConfig.h"
#include "SceneEditorTypes.h"

#include <vector>

namespace raceman {

class InputManager;
struct WheelForceFeedbackState;

struct ArcadeVehicleWheelTelemetry {
    float normalForce{0.0f};
    float slipAngle{0.0f};
    float tractionScale{1.0f};
    float suspensionTravel{0.0f};
    float angularVelocity{0.0f};
    // Per-wheel tyre state, straight from the contact. Signed slip ratio so a
    // consumer can tell a lock-up from wheelspin without guessing.
    float slipRatio{0.0f};
    float slipVelocity{0.0f};
    float slipAmount{0.0f};
    float gripUtilisation{0.0f};
    bool locked{false};
    bool spinning{false};
    bool grounded{true};
    bool steered{false};
    TrackSurfaceType surfaceType{TrackSurfaceType::Asphalt};
};

struct ArcadeVehicleTelemetry {
    float longitudinalSpeed{0.0f};
    float lateralSpeed{0.0f};
    float slipAngle{0.0f};
    float tractionScale{1.0f};
    float steering{0.0f};
    float yawRate{0.0f};
    float surfaceGrip{1.0f};
    float frontSlip{0.0f};
    float rearSlip{0.0f};
    float tireScrub{0.0f};
    float throttle{0.0f};
    float brake{0.0f};
    float handbrake{0.0f};
    float engineRPM{0.0f};
    float redlineRPM{7000.0f};
    float verticalVelocity{0.0f};
    float maxForwardSpeed{60.0f};
    std::vector<ArcadeVehicleWheelTelemetry> wheels;
};

// Values that have to survive between frames: filters, impact detection and
// the oscillator phase used for surface texture.
struct WheelForceFeedbackRuntimeState {
    bool initialized{false};
    float smoothedSelfAligningTorque{0.0f};
    float previousLateralAcceleration{0.0f};
    float previousVerticalVelocity{0.0f};
    float previousSuspensionTravel{0.0f};
    float impact{0.0f};
    float impactDirection{0.0f};
    float kerbEnergy{0.0f};
};

void ClearVehicleForceFeedback(InputManager* inputManager);

// Turns one simulation step of vehicle telemetry into the force feedback the
// wheel should render this frame.
WheelForceFeedbackState BuildWheelForceFeedbackSample(const ArcadeVehicleTelemetry& telemetry,
                                                      const raceman::physics::VehicleConfig& config,
                                                      const TrackSurfaceSettings& surfaceSettings,
                                                      WheelForceFeedbackRuntimeState& runtimeState,
                                                      float deltaTime);

} // namespace raceman

#pragma once

#include "../physics/VehicleConfig.h"

#include <vector>

namespace raceman {

class InputManager;

struct ArcadeVehicleWheelTelemetry {
    float normalForce{0.0f};
};

struct ArcadeVehicleTelemetry {
    float longitudinalSpeed{0.0f};
    float lateralSpeed{0.0f};
    float slipAngle{0.0f};
    float tractionScale{1.0f};
    float steering{0.0f};
    std::vector<ArcadeVehicleWheelTelemetry> wheels;
};

struct WheelForceFeedbackSample {
    float torque{0.0f};
    float damper{0.0f};
    float vibration{0.0f};
};

void ClearVehicleForceFeedback(InputManager* inputManager);

WheelForceFeedbackSample BuildWheelForceFeedbackSample(const ArcadeVehicleTelemetry& telemetry,
                                                       const raceman::physics::VehicleConfig& config);

} // namespace raceman

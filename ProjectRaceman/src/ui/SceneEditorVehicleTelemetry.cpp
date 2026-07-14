#include "SceneEditorVehicleTelemetry.h"

#include "../input/InputManager.h"

#include <algorithm>
#include <cmath>

namespace raceman {

void ClearVehicleForceFeedback(InputManager* inputManager) {
    if (inputManager == nullptr) {
        return;
    }
    inputManager->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
    inputManager->SetWheelForceFeedbackActive(false);
}

WheelForceFeedbackSample BuildWheelForceFeedbackSample(const ArcadeVehicleTelemetry& telemetry,
                                                       const raceman::physics::VehicleConfig& config) {
    WheelForceFeedbackSample sample;
    if (config.wheels.empty()) {
        return sample;
    }

    float frontNormalForce = 0.0f;
    int frontWheelCount = 0;
    for (std::size_t i = 0; i < telemetry.wheels.size() && i < config.wheels.size(); ++i) {
        if (config.wheels[i].mountPosition.y >= 0.0f) {
            frontNormalForce += telemetry.wheels[i].normalForce;
            ++frontWheelCount;
        }
    }

    const float averageFrontNormal = frontWheelCount > 0 ? (frontNormalForce / static_cast<float>(frontWheelCount)) : 0.0f;
    const float speedAbs = std::fabs(telemetry.longitudinalSpeed);
    const float speedFactor = (std::clamp)(speedAbs / 25.0f, 0.0f, 1.0f);
    const float loadFactor = (std::clamp)(averageFrontNormal / 4000.0f, 0.0f, 1.0f);
    const float slipFactor = (std::clamp)(std::fabs(telemetry.slipAngle) / 25.0f, 0.0f, 1.0f);

    sample.torque = (std::clamp)(-telemetry.steering * (0.08f + speedFactor * 0.42f + loadFactor * 0.28f) * telemetry.tractionScale, -1.0f, 1.0f);
    sample.damper = (std::clamp)(0.03f + speedFactor * 0.08f + slipFactor * 0.08f, 0.0f, 1.0f);
    sample.vibration = (std::clamp)(slipFactor * (1.0f - telemetry.tractionScale) * 0.35f, 0.0f, 1.0f);
    return sample;
}

} // namespace raceman

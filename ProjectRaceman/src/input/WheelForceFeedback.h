#pragma once

#include "InputManager.h"

#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace raceman {

class WheelForceFeedbackController {
public:
    WheelForceFeedbackController();
    ~WheelForceFeedbackController();

    void AttachToWindow(GLFWwindow* window);
    void SetProfiles(const std::vector<WheelSettingsProfile>& profiles);
    void SetEffectState(const WheelForceFeedbackState& state);
    // Calibrated wheel position (-1..1), used for the soft lock end stops.
    void SetSteeringPosition(float position);
    // Fires a short left/right jolt so users can confirm the wheel responds.
    void TriggerTestPulse();
    void SetLogCallback(std::function<void(const std::string&)> callback);
    void SyncDevices(const std::vector<InputDeviceInfo>& devices, bool active);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace raceman

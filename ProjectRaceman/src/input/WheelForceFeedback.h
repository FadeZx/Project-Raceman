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
    void SetEffectState(float steeringTorque, float damper, float vibration);
    void SetLogCallback(std::function<void(const std::string&)> callback);
    void SyncDevices(const std::vector<InputDeviceInfo>& devices, bool active);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace raceman

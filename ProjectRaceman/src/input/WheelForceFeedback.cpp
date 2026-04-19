#include "WheelForceFeedback.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#define DIRECTINPUT_VERSION 0x0800
#define WIN32_LEAN_AND_MEAN
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#include <windows.h>
#include <GLFW/glfw3.h>
#include <dinput.h>
#include <GLFW/glfw3native.h>
#endif

namespace raceman {

namespace {

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ContainsInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return false;
    }
    return ToLowerCopy(std::string(haystack)).find(ToLowerCopy(std::string(needle))) != std::string::npos;
}

std::string HrToString(long hr) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
    return buffer;
}

const WheelSettingsProfile* FindBestWheelProfile(const std::vector<WheelSettingsProfile>& profiles,
                                                 const InputDeviceInfo& device) {
    const WheelSettingsProfile* best = nullptr;
    std::size_t bestMatchLength = 0;
    for (const WheelSettingsProfile& profile : profiles) {
        if (profile.deviceNamePattern.empty()) {
            continue;
        }
        if (ContainsInsensitive(device.displayName, profile.deviceNamePattern) &&
            profile.deviceNamePattern.size() >= bestMatchLength) {
            best = &profile;
            bestMatchLength = profile.deviceNamePattern.size();
        }
    }
    if (best != nullptr) {
        return best;
    }

    auto enabledIt = std::find_if(profiles.begin(), profiles.end(), [](const WheelSettingsProfile& profile) {
        return profile.forceFeedbackEnabled;
    });
    if (enabledIt != profiles.end()) {
        return &(*enabledIt);
    }

    auto it = std::find_if(profiles.begin(), profiles.end(), [](const WheelSettingsProfile& profile) {
        return profile.id == "default_wheel";
    });
    return it != profiles.end() ? &(*it) : nullptr;
}

#if defined(_WIN32)

template <typename T>
void SafeRelease(T*& value) {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

struct DeviceSearchContext {
    std::string deviceNameLower;
    GUID guidInstance{};
    GUID guidProduct{};
    bool found{false};
};

BOOL CALLBACK EnumMatchingWheelDeviceCallback(const DIDEVICEINSTANCE* instance, VOID* context) {
    auto* search = static_cast<DeviceSearchContext*>(context);
    const std::string productName = ToLowerCopy(instance->tszProductName);
    const std::string instanceName = ToLowerCopy(instance->tszInstanceName);
    if (productName.find(search->deviceNameLower) != std::string::npos ||
        instanceName.find(search->deviceNameLower) != std::string::npos ||
        search->deviceNameLower.find(productName) != std::string::npos ||
        search->deviceNameLower.find(instanceName) != std::string::npos) {
        search->guidInstance = instance->guidInstance;
        search->guidProduct = instance->guidProduct;
        search->found = true;
        return DIENUM_STOP;
    }
    return DIENUM_CONTINUE;
}

#endif

} // namespace

struct WheelForceFeedbackController::Impl {
#if defined(_WIN32)
    GLFWwindow* window{nullptr};
    IDirectInput8* directInput{nullptr};
    IDirectInputDevice8* device{nullptr};
    IDirectInputEffect* constantEffect{nullptr};
    std::vector<WheelSettingsProfile> profiles;
    WheelSettingsProfile activeProfile{};
    bool hasActiveProfile{false};
    std::function<void(const std::string&)> logCallback;
    std::string activeRuntimeId;
    std::string activeDeviceName;
    bool active{false};
    float steeringTorque{0.0f};
    float damper{0.0f};
    float vibration{0.0f};

    ~Impl() {
        ReleaseDevice(false);
        SafeRelease(directInput);
    }

    void ReleaseDevice(bool restoreAutoCenter) {
        (void)restoreAutoCenter;
        if (constantEffect != nullptr) {
            constantEffect->Stop();
        }
        SafeRelease(constantEffect);

        if (device != nullptr) {
            device->SendForceFeedbackCommand(DISFFC_STOPALL);
            device->SendForceFeedbackCommand(DISFFC_SETACTUATORSOFF);
            device->SendForceFeedbackCommand(DISFFC_RESET);
            DIPROPDWORD autoCenter{};
            autoCenter.diph.dwSize = sizeof(DIPROPDWORD);
            autoCenter.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            autoCenter.diph.dwObj = 0;
            autoCenter.diph.dwHow = DIPH_DEVICE;
            autoCenter.dwData = DIPROPAUTOCENTER_OFF;
            device->SetProperty(DIPROP_AUTOCENTER, &autoCenter.diph);
            device->Unacquire();
        }

        SafeRelease(device);
        activeRuntimeId.clear();
        activeDeviceName.clear();
    }

    bool EnsureDirectInput() {
        if (directInput != nullptr) {
            return true;
        }

        const HMODULE module = GetModuleHandleA(nullptr);
        return SUCCEEDED(DirectInput8Create(module, DIRECTINPUT_VERSION, IID_IDirectInput8,
                                            reinterpret_cast<void**>(&directInput), nullptr));
    }

    bool AcquireDeviceForWheel(const InputDeviceInfo& wheel) {
        if (window == nullptr || !EnsureDirectInput()) {
            return false;
        }

        DeviceSearchContext search;
        search.deviceNameLower = ToLowerCopy(wheel.displayName);
        if (FAILED(directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumMatchingWheelDeviceCallback, &search, DIEDFL_ATTACHEDONLY)) ||
            !search.found) {
            return false;
        }

        IDirectInputDevice8* nextDevice = nullptr;
        if (FAILED(directInput->CreateDevice(search.guidInstance, &nextDevice, nullptr)) || nextDevice == nullptr) {
            return false;
        }

        const HWND hwnd = glfwGetWin32Window(window);
        if (FAILED(nextDevice->SetDataFormat(&c_dfDIJoystick2)) ||
            FAILED(nextDevice->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND)) ||
            FAILED(nextDevice->Acquire())) {
            if (logCallback) {
                logCallback("Wheel FFB acquire failed while setting format/cooperative level.");
            }
            SafeRelease(nextDevice);
            return false;
        }

        DIDEVCAPS caps{};
        caps.dwSize = sizeof(DIDEVCAPS);
        if (FAILED(nextDevice->GetCapabilities(&caps)) || (caps.dwFlags & DIDC_FORCEFEEDBACK) == 0) {
            if (logCallback) {
                logCallback("Wheel device has no DirectInput force feedback capability.");
            }
            nextDevice->Unacquire();
            SafeRelease(nextDevice);
            return false;
        }

        device = nextDevice;
        activeRuntimeId = wheel.runtimeId;
        activeDeviceName = wheel.displayName;
        device->SendForceFeedbackCommand(DISFFC_RESET);
        device->SendForceFeedbackCommand(DISFFC_STOPALL);
        device->SendForceFeedbackCommand(DISFFC_SETACTUATORSON);
        if (logCallback) {
            logCallback("Wheel FFB acquired: " + activeDeviceName);
        }
        return true;
    }

    void ApplyProfile(const WheelSettingsProfile* profile) {
        if (device == nullptr) {
            return;
        }

        hasActiveProfile = profile != nullptr;
        if (profile != nullptr) {
            activeProfile = *profile;
            if (logCallback) {
                logCallback("Wheel FFB preset: " + activeProfile.displayName);
            }
        } else {
            activeProfile = WheelSettingsProfile{};
        }

        DIPROPDWORD autoCenter{};
        autoCenter.diph.dwSize = sizeof(DIPROPDWORD);
        autoCenter.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        autoCenter.diph.dwObj = 0;
        autoCenter.diph.dwHow = DIPH_DEVICE;
        autoCenter.dwData = DIPROPAUTOCENTER_OFF;
        device->SetProperty(DIPROP_AUTOCENTER, &autoCenter.diph);

        DIPROPDWORD gain{};
        gain.diph.dwSize = sizeof(DIPROPDWORD);
        gain.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        gain.diph.dwObj = 0;
        gain.diph.dwHow = DIPH_DEVICE;
        const float configuredGain = profile != nullptr ? profile->forceFeedbackOverallStrength : 0.5f;
        gain.dwData = static_cast<DWORD>((std::clamp)(configuredGain, 0.0f, 1.0f) * 10000.0f);
        device->SetProperty(DIPROP_FFGAIN, &gain.diph);

        LONG direction = 0;
        DWORD axis = DIJOFS_X;
        DICONSTANTFORCE force{};
        float combinedTorque = 0.0f;
        if (profile != nullptr && profile->forceFeedbackEnabled) {
            const float satScale = (std::max)(0.0f, profile->forceFeedbackSelfAligningTorque);
            const float roadScale = (std::max)(0.0f, profile->forceFeedbackRoadEffects);
            const float slipScale = (std::max)(0.0f, profile->forceFeedbackSlipEffects);
            const float collisionScale = (std::max)(0.0f, profile->forceFeedbackCollisionEffects);
            const float minimumForce = (std::clamp)(profile->forceFeedbackMinimumForce, 0.0f, 1.0f);

            combinedTorque += steeringTorque * satScale;
            combinedTorque += vibration * (roadScale * 0.08f + slipScale * 0.05f + collisionScale * 0.03f);

            if (std::fabs(combinedTorque) > 0.0001f) {
                const float sign = combinedTorque > 0.0f ? 1.0f : -1.0f;
                const float absTorque = (std::clamp)(std::fabs(combinedTorque), 0.0f, 1.0f);
                combinedTorque = sign * (minimumForce + absTorque * (1.0f - minimumForce));
            }
        }
        combinedTorque = (std::clamp)(combinedTorque, -1.0f, 1.0f);
        if (combinedTorque < 0.0f) {
            direction = 18000;
        }
        force.lMagnitude = static_cast<LONG>(std::abs(combinedTorque) * DI_FFNOMINALMAX);

        DIEFFECT effect{};
        effect.dwSize = sizeof(DIEFFECT);
        effect.dwFlags = DIEFF_OBJECTOFFSETS | DIEFF_CARTESIAN;
        effect.dwDuration = INFINITE;
        effect.dwGain = DI_FFNOMINALMAX;
        effect.dwTriggerButton = DIEB_NOTRIGGER;
        effect.cAxes = 1;
        effect.rgdwAxes = &axis;
        effect.rglDirection = &direction;
        effect.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
        effect.lpvTypeSpecificParams = &force;
        effect.dwStartDelay = 0;

        if (constantEffect != nullptr) {
            constantEffect->Stop();
            SafeRelease(constantEffect);
        }

        const HRESULT hr = device->CreateEffect(GUID_ConstantForce, &effect, &constantEffect, nullptr);
        if (SUCCEEDED(hr) && constantEffect != nullptr) {
            if (logCallback) {
                logCallback("Wheel constant-force effect created.");
            }
            const HRESULT startHr = constantEffect->Start(1, DIES_SOLO);
            if (FAILED(startHr) && logCallback) {
                logCallback("Wheel constant-force start failed: " + HrToString(startHr));
            }
        } else if (logCallback) {
            logCallback("Wheel constant-force effect failed: " + HrToString(hr));
        }
    }

    void Sync(const std::vector<InputDeviceInfo>& devicesIn, bool shouldBeActive) {
        active = shouldBeActive;
        if (!active) {
            ReleaseDevice(true);
            return;
        }

        auto wheelIt = std::find_if(devicesIn.begin(), devicesIn.end(), [](const InputDeviceInfo& deviceInfo) {
            return deviceInfo.type == InputDeviceType::Wheel;
        });
        if (wheelIt == devicesIn.end()) {
            ReleaseDevice(true);
            return;
        }

        const WheelSettingsProfile* profile = FindBestWheelProfile(profiles, *wheelIt);
        if (device == nullptr || activeRuntimeId != wheelIt->runtimeId) {
            ReleaseDevice(false);
            if (!AcquireDeviceForWheel(*wheelIt)) {
                ReleaseDevice(false);
                return;
            }
        }

        ApplyProfile(profile);
    }
#else
    GLFWwindow* window{nullptr};
    std::vector<WheelSettingsProfile> profiles;
    float steeringTorque{0.0f};
    float damper{0.0f};
    float vibration{0.0f};
    std::function<void(const std::string&)> logCallback;

    void Sync(const std::vector<InputDeviceInfo>&, bool) {}
#endif
};

WheelForceFeedbackController::WheelForceFeedbackController()
    : impl_(new Impl()) {}

WheelForceFeedbackController::~WheelForceFeedbackController() {
    delete impl_;
    impl_ = nullptr;
}

void WheelForceFeedbackController::AttachToWindow(GLFWwindow* window) {
    impl_->window = window;
}

void WheelForceFeedbackController::SetProfiles(const std::vector<WheelSettingsProfile>& profiles) {
    impl_->profiles = profiles;
}

void WheelForceFeedbackController::SetEffectState(float steeringTorque, float damper, float vibration) {
    impl_->steeringTorque = steeringTorque;
    impl_->damper = damper;
    impl_->vibration = vibration;
}

void WheelForceFeedbackController::SetLogCallback(std::function<void(const std::string&)> callback) {
    impl_->logCallback = std::move(callback);
}

void WheelForceFeedbackController::SyncDevices(const std::vector<InputDeviceInfo>& devices, bool active) {
    impl_->Sync(devices, active);
}

} // namespace raceman

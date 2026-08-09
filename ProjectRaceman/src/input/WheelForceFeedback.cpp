#include "WheelForceFeedback.h"

#include <algorithm>
#include <cctype>
#include <chrono>
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

    auto it = std::find_if(profiles.begin(), profiles.end(), [](const WheelSettingsProfile& profile) {
        return profile.id == "default_wheel";
    });
    if (it != profiles.end()) {
        return &(*it);
    }

    auto enabledIt = std::find_if(profiles.begin(), profiles.end(), [](const WheelSettingsProfile& profile) {
        return profile.forceFeedbackEnabled;
    });
    return enabledIt != profiles.end() ? &(*enabledIt) : nullptr;
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

struct SupportedEffectsContext {
    bool constant{false};
    bool periodicSine{false};
    bool damper{false};
    bool friction{false};
    bool spring{false};
};

BOOL CALLBACK EnumEffectsCallback(const DIEFFECTINFO* info, VOID* context) {
    auto* supported = static_cast<SupportedEffectsContext*>(context);
    if (info->guid == GUID_ConstantForce) {
        supported->constant = true;
    } else if (info->guid == GUID_Sine) {
        supported->periodicSine = true;
    } else if (info->guid == GUID_Damper) {
        supported->damper = true;
    } else if (info->guid == GUID_Friction) {
        supported->friction = true;
    } else if (info->guid == GUID_Spring) {
        supported->spring = true;
    }
    return DIENUM_CONTINUE;
}

LONG ToDirectInputForce(float normalized) {
    const float clamped = (std::clamp)(normalized, -1.0f, 1.0f);
    return static_cast<LONG>(clamped * static_cast<float>(DI_FFNOMINALMAX));
}

DWORD ToDirectInputMagnitude(float normalized) {
    const float clamped = (std::clamp)(normalized, 0.0f, 1.0f);
    return static_cast<DWORD>(clamped * static_cast<float>(DI_FFNOMINALMAX));
}

#endif

} // namespace

struct WheelForceFeedbackController::Impl {
#if defined(_WIN32)
    GLFWwindow* window{nullptr};
    IDirectInput8* directInput{nullptr};
    IDirectInputDevice8* device{nullptr};

    // Effects are created once per device and then retuned in place. Creating
    // and starting them every frame - as this used to - restarts the envelope
    // continuously and leaves the wheel feeling dead or notchy.
    IDirectInputEffect* constantEffect{nullptr};
    IDirectInputEffect* periodicEffect{nullptr};
    IDirectInputEffect* rumbleEffect{nullptr};
    IDirectInputEffect* damperEffect{nullptr};
    IDirectInputEffect* frictionEffect{nullptr};
    IDirectInputEffect* springEffect{nullptr};
    SupportedEffectsContext supported{};

    std::vector<WheelSettingsProfile> profiles;
    WheelSettingsProfile activeProfile{};
    bool hasActiveProfile{false};
    std::string appliedProfileId;
    std::function<void(const std::string&)> logCallback;
    std::string activeRuntimeId;
    std::string activeDeviceName;
    bool active{false};

    WheelForceFeedbackState state{};
    float steeringPosition{0.0f};
    float smoothedTorque{0.0f};
    float testPulseSeconds{0.0f};
    std::chrono::steady_clock::time_point lastUpdate{std::chrono::steady_clock::now()};

    // Last values pushed to the device, so untouched effects are left alone.
    LONG lastConstantForce{0};
    DWORD lastPeriodicMagnitude{0};
    DWORD lastPeriodicPeriod{0};
    DWORD lastRumbleMagnitude{0};
    DWORD lastRumblePeriod{0};
    LONG lastDamperCoefficient{0};
    LONG lastFrictionCoefficient{0};
    LONG lastSpringCoefficient{0};
    LONG lastSpringCenter{0};

    ~Impl() {
        ReleaseDevice();
        SafeRelease(directInput);
    }

    void Log(const std::string& message) {
        if (logCallback) {
            logCallback(message);
        }
    }

    void ReleaseEffects() {
        IDirectInputEffect* effects[] = {
            constantEffect, periodicEffect, rumbleEffect, damperEffect, frictionEffect, springEffect
        };
        for (IDirectInputEffect* effect : effects) {
            if (effect != nullptr) {
                effect->Stop();
            }
        }
        SafeRelease(constantEffect);
        SafeRelease(periodicEffect);
        SafeRelease(rumbleEffect);
        SafeRelease(damperEffect);
        SafeRelease(frictionEffect);
        SafeRelease(springEffect);
        supported = {};
        lastConstantForce = 0;
        lastPeriodicMagnitude = 0;
        lastPeriodicPeriod = 0;
        lastRumbleMagnitude = 0;
        lastRumblePeriod = 0;
        lastDamperCoefficient = 0;
        lastFrictionCoefficient = 0;
        lastSpringCoefficient = 0;
        lastSpringCenter = 0;
        smoothedTorque = 0.0f;
    }

    void ReleaseDevice() {
        ReleaseEffects();

        if (device != nullptr) {
            device->SendForceFeedbackCommand(DISFFC_STOPALL);
            device->SendForceFeedbackCommand(DISFFC_SETACTUATORSOFF);
            device->SendForceFeedbackCommand(DISFFC_RESET);
            // Hand centring back to the driver so the wheel does not go limp
            // once the game releases it.
            DIPROPDWORD autoCenter{};
            autoCenter.diph.dwSize = sizeof(DIPROPDWORD);
            autoCenter.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            autoCenter.diph.dwObj = 0;
            autoCenter.diph.dwHow = DIPH_DEVICE;
            autoCenter.dwData = DIPROPAUTOCENTER_ON;
            device->SetProperty(DIPROP_AUTOCENTER, &autoCenter.diph);
            device->Unacquire();
        }

        SafeRelease(device);
        activeRuntimeId.clear();
        activeDeviceName.clear();
        appliedProfileId.clear();
        hasActiveProfile = false;
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
            Log("Wheel FFB acquire failed while setting format/cooperative level.");
            SafeRelease(nextDevice);
            return false;
        }

        DIDEVCAPS caps{};
        caps.dwSize = sizeof(DIDEVCAPS);
        if (FAILED(nextDevice->GetCapabilities(&caps)) || (caps.dwFlags & DIDC_FORCEFEEDBACK) == 0) {
            Log("Wheel device has no DirectInput force feedback capability.");
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

        supported = {};
        device->EnumEffects(EnumEffectsCallback, &supported, DIEFT_ALL);
        Log("Wheel FFB acquired: " + activeDeviceName);
        return true;
    }

    // Every effect drives the steering axis only, in the same cartesian frame.
    void FillCommonEffect(DIEFFECT& effect, DWORD* axis, LONG* direction) const {
        effect.dwSize = sizeof(DIEFFECT);
        effect.dwFlags = DIEFF_OBJECTOFFSETS | DIEFF_CARTESIAN;
        effect.dwDuration = INFINITE;
        effect.dwSamplePeriod = 0;
        effect.dwGain = DI_FFNOMINALMAX;
        effect.dwTriggerButton = DIEB_NOTRIGGER;
        effect.dwTriggerRepeatInterval = 0;
        effect.cAxes = 1;
        effect.rgdwAxes = axis;
        effect.rglDirection = direction;
        effect.lpEnvelope = nullptr;
        effect.dwStartDelay = 0;
        *axis = DIJOFS_X;
        *direction = 0;
    }

    bool CreateEffects() {
        if (device == nullptr) {
            return false;
        }

        DWORD axis = DIJOFS_X;
        LONG direction = 0;

        if (constantEffect == nullptr) {
            DICONSTANTFORCE constant{};
            DIEFFECT effect{};
            FillCommonEffect(effect, &axis, &direction);
            effect.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
            effect.lpvTypeSpecificParams = &constant;
            const HRESULT hr = device->CreateEffect(GUID_ConstantForce, &effect, &constantEffect, nullptr);
            if (FAILED(hr) || constantEffect == nullptr) {
                Log("Wheel constant-force effect failed: " + HrToString(hr));
                return false;
            }
            constantEffect->Start(1, 0);
        }

        auto createPeriodic = [&](IDirectInputEffect*& target, DWORD periodMicroseconds) {
            if (target != nullptr) {
                return;
            }
            DIPERIODIC periodic{};
            periodic.dwMagnitude = 0;
            periodic.lOffset = 0;
            periodic.dwPhase = 0;
            periodic.dwPeriod = periodMicroseconds;
            DIEFFECT effect{};
            FillCommonEffect(effect, &axis, &direction);
            effect.cbTypeSpecificParams = sizeof(DIPERIODIC);
            effect.lpvTypeSpecificParams = &periodic;
            const HRESULT hr = device->CreateEffect(GUID_Sine, &effect, &target, nullptr);
            if (FAILED(hr) || target == nullptr) {
                Log("Wheel periodic effect unavailable: " + HrToString(hr));
                target = nullptr;
                return;
            }
            target->Start(1, 0);
        };
        // Road/tyre texture and the slower drivetrain rumble run as separate
        // device-side oscillators, so they stay smooth regardless of frame rate.
        createPeriodic(periodicEffect, 20000);
        createPeriodic(rumbleEffect, 50000);

        auto createCondition = [&](IDirectInputEffect*& target, const GUID& guid, const char* label) {
            if (target != nullptr) {
                return;
            }
            DICONDITION condition{};
            condition.lOffset = 0;
            condition.lPositiveCoefficient = 0;
            condition.lNegativeCoefficient = 0;
            condition.dwPositiveSaturation = DI_FFNOMINALMAX;
            condition.dwNegativeSaturation = DI_FFNOMINALMAX;
            condition.lDeadBand = 0;
            DIEFFECT effect{};
            FillCommonEffect(effect, &axis, &direction);
            effect.cbTypeSpecificParams = sizeof(DICONDITION);
            effect.lpvTypeSpecificParams = &condition;
            const HRESULT hr = device->CreateEffect(guid, &effect, &target, nullptr);
            if (FAILED(hr) || target == nullptr) {
                Log(std::string("Wheel ") + label + " effect unavailable: " + HrToString(hr));
                target = nullptr;
                return;
            }
            target->Start(1, 0);
        };
        createCondition(damperEffect, GUID_Damper, "damper");
        createCondition(frictionEffect, GUID_Friction, "friction");
        createCondition(springEffect, GUID_Spring, "spring");
        return true;
    }

    void ApplyProfile(const WheelSettingsProfile* profile) {
        if (device == nullptr) {
            return;
        }

        const std::string profileId = profile != nullptr ? profile->id : std::string();
        const bool profileChanged = !hasActiveProfile || appliedProfileId != profileId ||
            (profile != nullptr && profile->forceFeedbackOverallStrength != activeProfile.forceFeedbackOverallStrength);

        hasActiveProfile = profile != nullptr;
        activeProfile = profile != nullptr ? *profile : WheelSettingsProfile{};

        if (!profileChanged) {
            return;
        }
        appliedProfileId = profileId;

        // Auto-centring is a driver-side spring that fights every effect the
        // game sends, so it stays off while the game owns the wheel.
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

        Log("Wheel FFB preset: " + activeProfile.displayName);
    }

    void UpdateConstantForce(float deltaSeconds) {
        if (constantEffect == nullptr) {
            return;
        }

        const WheelSettingsProfile& p = activeProfile;
        float torque = 0.0f;
        if (hasActiveProfile && p.forceFeedbackEnabled) {
            torque += state.steeringTorque * (std::max)(0.0f, p.forceFeedbackSelfAligningTorque);
            torque += state.impact * state.impactDirection * (std::max)(0.0f, p.forceFeedbackCollisionEffects);

            // Soft lock: past the configured steering range the wheel is pushed
            // back towards the usable arc so the driver feels the end stop.
            const float physicalRange = (std::max)(1.0f, p.steeringPhysicalRangeDegrees);
            const float usedRange = (std::clamp)(p.steeringRangeDegrees, 1.0f, physicalRange);
            const float lockThreshold = usedRange / physicalRange;
            if (std::fabs(steeringPosition) > lockThreshold) {
                const float overshoot = (std::fabs(steeringPosition) - lockThreshold) /
                    (std::max)(0.0001f, 1.0f - lockThreshold);
                const float sign = steeringPosition < 0.0f ? 1.0f : -1.0f;
                torque += sign * (std::clamp)(overshoot, 0.0f, 1.0f) *
                    (std::max)(0.0f, p.forceFeedbackSoftLock);
            }

            if (p.forceFeedbackInvert) {
                torque = -torque;
            }
        }

        if (testPulseSeconds > 0.0f) {
            testPulseSeconds = (std::max)(0.0f, testPulseSeconds - deltaSeconds);
            torque = testPulseSeconds > 0.25f ? 0.6f : -0.6f;
        }

        // A little smoothing removes the step edges introduced by sampling the
        // simulation at frame rate without noticeably delaying the forces.
        const float smoothing = (std::clamp)(hasActiveProfile ? activeProfile.forceFeedbackSmoothing : 0.0f, 0.0f, 0.95f);
        const float blend = smoothing <= 0.0f
            ? 1.0f
            : (std::clamp)(deltaSeconds / (std::max)(0.001f, smoothing * 0.1f), 0.0f, 1.0f);
        smoothedTorque += (torque - smoothedTorque) * blend;

        float output = (std::clamp)(smoothedTorque, -1.0f, 1.0f);
        // Direct drive and belt wheels have a dead zone around zero; the
        // minimum force setting lifts small forces above it.
        const float minimumForce = (std::clamp)(hasActiveProfile ? activeProfile.forceFeedbackMinimumForce : 0.0f, 0.0f, 1.0f);
        if (minimumForce > 0.0f && std::fabs(output) > 0.0005f) {
            const float sign = output < 0.0f ? -1.0f : 1.0f;
            output = sign * (minimumForce + std::fabs(output) * (1.0f - minimumForce));
        }

        const LONG magnitude = ToDirectInputForce(output);
        if (magnitude == lastConstantForce) {
            return;
        }
        lastConstantForce = magnitude;

        DICONSTANTFORCE constant{};
        constant.lMagnitude = magnitude;
        DIEFFECT effect{};
        DWORD axis = DIJOFS_X;
        LONG direction = 0;
        FillCommonEffect(effect, &axis, &direction);
        effect.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
        effect.lpvTypeSpecificParams = &constant;
        constantEffect->SetParameters(&effect, DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_START);
    }

    void UpdatePeriodic(IDirectInputEffect* effect,
                        float amplitude,
                        float frequencyHz,
                        DWORD& lastMagnitude,
                        DWORD& lastPeriod) {
        if (effect == nullptr) {
            return;
        }

        const DWORD magnitude = ToDirectInputMagnitude(amplitude);
        // DirectInput periods are microseconds per cycle.
        const DWORD period = magnitude == 0
            ? lastPeriod
            : static_cast<DWORD>(1000000.0f / (std::clamp)(frequencyHz, 1.0f, 500.0f));
        if (magnitude == lastMagnitude && period == lastPeriod) {
            return;
        }
        lastMagnitude = magnitude;
        lastPeriod = period;

        DIPERIODIC periodic{};
        periodic.dwMagnitude = magnitude;
        periodic.lOffset = 0;
        periodic.dwPhase = 0;
        periodic.dwPeriod = period < 1000 ? 1000 : period;
        DIEFFECT diEffect{};
        DWORD axis = DIJOFS_X;
        LONG direction = 0;
        FillCommonEffect(diEffect, &axis, &direction);
        diEffect.cbTypeSpecificParams = sizeof(DIPERIODIC);
        diEffect.lpvTypeSpecificParams = &periodic;
        effect->SetParameters(&diEffect, DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_START);
    }

    void UpdateCondition(IDirectInputEffect* effect,
                         float strength,
                         float center,
                         LONG& lastCoefficient,
                         LONG* lastCenter) {
        if (effect == nullptr) {
            return;
        }

        const LONG coefficient = ToDirectInputForce((std::clamp)(strength, 0.0f, 1.0f));
        const LONG offset = ToDirectInputForce((std::clamp)(center, -1.0f, 1.0f));
        if (coefficient == lastCoefficient && (lastCenter == nullptr || offset == *lastCenter)) {
            return;
        }
        lastCoefficient = coefficient;
        if (lastCenter != nullptr) {
            *lastCenter = offset;
        }

        DICONDITION condition{};
        condition.lOffset = lastCenter != nullptr ? offset : 0;
        condition.lPositiveCoefficient = coefficient;
        condition.lNegativeCoefficient = coefficient;
        condition.dwPositiveSaturation = DI_FFNOMINALMAX;
        condition.dwNegativeSaturation = DI_FFNOMINALMAX;
        condition.lDeadBand = 0;
        DIEFFECT diEffect{};
        DWORD axis = DIJOFS_X;
        LONG direction = 0;
        FillCommonEffect(diEffect, &axis, &direction);
        diEffect.cbTypeSpecificParams = sizeof(DICONDITION);
        diEffect.lpvTypeSpecificParams = &condition;
        effect->SetParameters(&diEffect, DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_START);
    }

    void UpdateEffects() {
        if (device == nullptr) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = (std::clamp)(
            std::chrono::duration<float>(now - lastUpdate).count(), 0.0f, 0.25f);
        lastUpdate = now;

        // A lost device (alt-tab, sleep) must be re-acquired or every effect
        // update silently fails from then on.
        HRESULT poll = device->Poll();
        if (poll == DIERR_INPUTLOST || poll == DIERR_NOTACQUIRED) {
            if (FAILED(device->Acquire())) {
                return;
            }
            device->SendForceFeedbackCommand(DISFFC_SETACTUATORSON);
        }

        const WheelSettingsProfile& p = activeProfile;
        const bool enabled = hasActiveProfile && p.forceFeedbackEnabled;

        UpdateConstantForce(deltaSeconds);

        // Each texture source carries its own gain so the presets can, for
        // example, keep kerb rattle strong while damping road noise.
        float textureAmplitude = 0.0f;
        if (enabled) {
            textureAmplitude = state.roadAmplitude * (std::max)(0.0f, p.forceFeedbackRoadEffects) +
                               state.slipAmplitude * (std::max)(0.0f, p.forceFeedbackSlipEffects) +
                               state.kerbAmplitude * (std::max)(0.0f, p.forceFeedbackKerbEffects) +
                               state.lockupAmplitude * (std::max)(0.0f, p.forceFeedbackLockupEffects);
            if (textureAmplitude <= 0.0f && state.vibrationAmplitude > 0.0f) {
                // Callers that only fill the combined amplitude still get road
                // texture rather than silence.
                textureAmplitude = state.vibrationAmplitude * (std::max)(0.0f, p.forceFeedbackRoadEffects);
            }
        }
        UpdatePeriodic(periodicEffect,
                       textureAmplitude,
                       state.vibrationFrequencyHz,
                       lastPeriodicMagnitude,
                       lastPeriodicPeriod);
        UpdatePeriodic(rumbleEffect,
                       enabled ? state.rumbleAmplitude * (std::max)(0.0f, p.forceFeedbackEngineEffects) : 0.0f,
                       state.rumbleFrequencyHz,
                       lastRumbleMagnitude,
                       lastRumblePeriod);

        UpdateCondition(damperEffect,
                        enabled ? (p.forceFeedbackDamper * 0.5f + state.damper) : 0.0f,
                        0.0f,
                        lastDamperCoefficient,
                        nullptr);
        UpdateCondition(frictionEffect,
                        enabled ? (p.forceFeedbackFriction * 0.5f + state.friction) : 0.0f,
                        0.0f,
                        lastFrictionCoefficient,
                        nullptr);
        // The spring both provides the static centring/return rate and the
        // configured constant spring weight.
        UpdateCondition(springEffect,
                        enabled ? (p.forceFeedbackSpring + state.centeringSpring * p.forceFeedbackCenteringSpring) : 0.0f,
                        0.0f,
                        lastSpringCoefficient,
                        &lastSpringCenter);
    }

    void Sync(const std::vector<InputDeviceInfo>& devicesIn, bool shouldBeActive) {
        active = shouldBeActive;
        if (!active) {
            if (device != nullptr) {
                ReleaseDevice();
            }
            return;
        }

        auto wheelIt = std::find_if(devicesIn.begin(), devicesIn.end(), [](const InputDeviceInfo& deviceInfo) {
            return deviceInfo.type == InputDeviceType::Wheel;
        });
        if (wheelIt == devicesIn.end()) {
            if (device != nullptr) {
                ReleaseDevice();
            }
            return;
        }

        const WheelSettingsProfile* profile = FindBestWheelProfile(profiles, *wheelIt);
        if (device == nullptr || activeRuntimeId != wheelIt->runtimeId) {
            ReleaseDevice();
            if (!AcquireDeviceForWheel(*wheelIt)) {
                ReleaseDevice();
                return;
            }
            ApplyProfile(profile);
            if (!CreateEffects()) {
                return;
            }
        } else {
            ApplyProfile(profile);
        }

        UpdateEffects();
    }
#else
    GLFWwindow* window{nullptr};
    std::vector<WheelSettingsProfile> profiles;
    WheelForceFeedbackState state{};
    float steeringPosition{0.0f};
    float testPulseSeconds{0.0f};
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

void WheelForceFeedbackController::SetEffectState(const WheelForceFeedbackState& state) {
    impl_->state = state;
}

void WheelForceFeedbackController::SetSteeringPosition(float position) {
    impl_->steeringPosition = (std::clamp)(position, -1.0f, 1.0f);
}

void WheelForceFeedbackController::TriggerTestPulse() {
    impl_->testPulseSeconds = 0.5f;
}

void WheelForceFeedbackController::SetLogCallback(std::function<void(const std::string&)> callback) {
    impl_->logCallback = std::move(callback);
}

void WheelForceFeedbackController::SyncDevices(const std::vector<InputDeviceInfo>& devices, bool active) {
    impl_->Sync(devices, active);
}

} // namespace raceman

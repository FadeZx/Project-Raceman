#include "InputManager.h"
#include "WheelForceFeedback.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cctype>

namespace raceman {

namespace {
constexpr float kDigitalPressThreshold = 0.5f;
constexpr int kFirstJoystick = GLFW_JOYSTICK_1;
constexpr int kLastJoystick = GLFW_JOYSTICK_LAST;

#ifndef GLFW_TRUE
#define GLFW_TRUE 1
#endif

#ifndef GLFW_GAMEPAD_AXIS_LEFT_X
#define GLFW_GAMEPAD_AXIS_LEFT_X 0
#define GLFW_GAMEPAD_AXIS_LEFT_Y 1
#define GLFW_GAMEPAD_AXIS_RIGHT_X 2
#define GLFW_GAMEPAD_AXIS_RIGHT_Y 3
#define GLFW_GAMEPAD_AXIS_LEFT_TRIGGER 4
#define GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER 5
#define GLFW_GAMEPAD_BUTTON_A 0
#define GLFW_GAMEPAD_BUTTON_B 1
#define GLFW_GAMEPAD_BUTTON_X 2
#define GLFW_GAMEPAD_BUTTON_Y 3
#define GLFW_GAMEPAD_BUTTON_LEFT_BUMPER 4
#define GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER 5
#define GLFW_GAMEPAD_BUTTON_BACK 6
#define GLFW_GAMEPAD_BUTTON_START 7
#define GLFW_GAMEPAD_BUTTON_GUIDE 8
#define GLFW_GAMEPAD_BUTTON_LEFT_THUMB 9
#define GLFW_GAMEPAD_BUTTON_RIGHT_THUMB 10
#define GLFW_GAMEPAD_BUTTON_DPAD_UP 11
#define GLFW_GAMEPAD_BUTTON_DPAD_RIGHT 12
#define GLFW_GAMEPAD_BUTTON_DPAD_DOWN 13
#define GLFW_GAMEPAD_BUTTON_DPAD_LEFT 14
#define GLFW_GAMEPAD_BUTTON_LAST GLFW_GAMEPAD_BUTTON_DPAD_LEFT
#define GLFW_GAMEPAD_AXIS_LAST GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER
#endif

#if (GLFW_VERSION_MAJOR > 3) || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3)
#define RACEMAN_GLFW_HAS_GAMEPAD_API 1
#else
#define RACEMAN_GLFW_HAS_GAMEPAD_API 0
#endif

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

const InputProfile* FindProfileById(const std::vector<InputProfile>& profiles, std::string_view profileId) {
    auto it = std::find_if(profiles.begin(), profiles.end(), [&](const InputProfile& profile) {
        return profile.id == profileId;
    });
    return it != profiles.end() ? &(*it) : nullptr;
}

InputProfile* FindProfileById(std::vector<InputProfile>& profiles, std::string_view profileId) {
    auto it = std::find_if(profiles.begin(), profiles.end(), [&](const InputProfile& profile) {
        return profile.id == profileId;
    });
    return it != profiles.end() ? &(*it) : nullptr;
}

bool LooksLikeGamepadName(const char* name) {
    const std::string lower = ToLowerCopy(name != nullptr ? name : "");
    return lower.find("xbox") != std::string::npos ||
        lower.find("playstation") != std::string::npos ||
        lower.find("dualshock") != std::string::npos ||
        lower.find("dualsense") != std::string::npos ||
        lower.find("controller") != std::string::npos ||
        lower.find("gamepad") != std::string::npos ||
        lower.find("nintendo") != std::string::npos;
}
} // namespace

InputManager::InputManager()
    : wheelForceFeedbackController_(std::make_unique<WheelForceFeedbackController>()) {
    if (wheelForceFeedbackController_) {
        wheelForceFeedbackController_->SetLogCallback([](const std::string& message) {
            std::fprintf(stdout, "[WheelFFB] %s\n", message.c_str());
            std::fflush(stdout);
        });
    }
}

InputManager::~InputManager() = default;

void InputManager::AttachToWindow(GLFWwindow* window) {
    window_ = window;
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, &InputManager::KeyCallback);
    glfwSetScrollCallback(window_, &InputManager::ScrollCallback);
    EnsureDefaultProfiles();
    EnsureDefaultWheelSettingsProfiles();
    if (wheelForceFeedbackController_) {
        wheelForceFeedbackController_->AttachToWindow(window_);
        wheelForceFeedbackController_->SetProfiles(wheelSettingsProfiles_);
    }
}

void InputManager::BeginFrame() {
    if (window_ != nullptr) {
        std::vector<int> keysToPoll;
        auto addKeyToPoll = [&](int key) {
            if (key < 0) {
                return;
            }
            if (std::find(keysToPoll.begin(), keysToPoll.end(), key) == keysToPoll.end()) {
                keysToPoll.push_back(key);
            }
        };
        for (const auto& [key, down] : keyState_) {
            (void)down;
            addKeyToPoll(key);
        }
        for (const auto& [key, down] : previousKeyState_) {
            (void)down;
            addKeyToPoll(key);
        }
        for (const InputProfile& profile : inputProfiles_) {
            for (const InputBinding& binding : profile.bindings) {
                if (binding.deviceType != InputDeviceType::Keyboard) {
                    continue;
                }
                addKeyToPoll(binding.key);
                addKeyToPoll(binding.negativeKey);
                addKeyToPoll(binding.positiveKey);
            }
        }

        for (int key : keysToPoll) {
            const int state = glfwGetKey(window_, key);
            const bool isDown = state == GLFW_PRESS || state == GLFW_REPEAT;
            const bool wasDown = previousKeyState_[key];
            keyPressed_[key] = !wasDown && isDown;
            keyState_[key] = isDown;
            previousKeyState_[key] = isDown;
        }
    } else {
        for (auto& [key, pressed] : keyPressed_) {
            (void)key;
            pressed = false;
        }
        previousKeyState_ = keyState_;
    }
    for (auto& [button, pressed] : mouseButtonPressed_) {
        (void)button;
        pressed = false;
    }
    for (auto& [buttonKey, state] : joystickButtons_) {
        (void)buttonKey;
        state.pressed = false;
    }
    if (window_ != nullptr) {
        mouseWheelDelta_ = pendingMouseWheelDelta_;
        pendingMouseWheelDelta_ = 0.0f;

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window_, &mouseX, &mouseY);
        mousePosition_ = {static_cast<float>(mouseX), static_cast<float>(mouseY)};
        if (!mousePositionInitialized_) {
            previousMousePosition_ = mousePosition_;
            mousePositionInitialized_ = true;
        }
        mouseDelta_ = mousePosition_ - previousMousePosition_;
        previousMousePosition_ = mousePosition_;

        for (int button = GLFW_MOUSE_BUTTON_1; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
            const bool isDown = glfwGetMouseButton(window_, button) == GLFW_PRESS;
            const bool wasDown = mouseButtonState_[button];
            mouseButtonPressed_[button] = !wasDown && isDown;
            mouseButtonState_[button] = isDown;
        }
    } else {
        mouseDelta_ = {0.0f, 0.0f};
        mouseWheelDelta_ = 0.0f;
        pendingMouseWheelDelta_ = 0.0f;
    }
    PollDevices();
    const bool wheelWindowFocused = window_ == nullptr || glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    if (wheelForceFeedbackController_) {
        wheelForceFeedbackController_->SetEffectState(
            wheelForceFeedbackTorque_,
            wheelForceFeedbackDamper_,
            wheelForceFeedbackVibration_);
        wheelForceFeedbackController_->SyncDevices(devices_, wheelForceFeedbackActive_ && wheelWindowFocused);
    }
}

void InputManager::EndFrame() {
}

bool InputManager::WasKeyPressed(int key) const {
    auto it = keyPressed_.find(key);
    return it != keyPressed_.end() && it->second;
}

bool InputManager::IsKeyDown(int key) const {
    if (window_ != nullptr) {
        const int state = glfwGetKey(window_, key);
        if (state == GLFW_PRESS || state == GLFW_REPEAT) {
            return true;
        }
    }

    auto it = keyState_.find(key);
    return it != keyState_.end() && it->second;
}

bool InputManager::IsMouseButtonDown(int button) const {
    if (window_ != nullptr) {
        return glfwGetMouseButton(window_, button) == GLFW_PRESS;
    }
    auto it = mouseButtonState_.find(button);
    return it != mouseButtonState_.end() && it->second;
}

bool InputManager::WasMouseButtonPressed(int button) const {
    auto it = mouseButtonPressed_.find(button);
    return it != mouseButtonPressed_.end() && it->second;
}

glm::vec2 InputManager::GetMouseDelta() const {
    return mouseDelta_;
}

float InputManager::GetMouseWheelDelta() const {
    return mouseWheelDelta_;
}

float InputManager::GetAxis(std::string_view action) const {
    return GetAxisForProfile(GetDefaultCharacterProfileId(), action);
}

bool InputManager::IsActionDown(std::string_view action) const {
    return IsActionDownForProfile(GetDefaultCharacterProfileId(), action);
}

bool InputManager::WasActionPressed(std::string_view action) const {
    return WasActionPressedForProfile(GetDefaultCharacterProfileId(), action);
}

float InputManager::GetAxisForProfile(std::string_view profileId,
                                      std::string_view action,
                                      InputDevicePreference preferredDevice,
                                      std::string_view preferredSpecificDeviceId) const {
    const InputProfile* profile = FindProfile(profileId);
    if (profile == nullptr && profileId != GetDefaultVehicleProfileId()) {
        profile = FindProfile(GetDefaultVehicleProfileId());
    }
    if (profile == nullptr && profileId != GetDefaultCharacterProfileId()) {
        profile = FindProfile(GetDefaultCharacterProfileId());
    }
    if (profile == nullptr) {
        return 0.0f;
    }

    float bestMagnitude = 0.0f;
    float resolvedValue = 0.0f;
    for (const InputBinding& binding : profile->bindings) {
        if (binding.action != action) {
            continue;
        }
        const ResolvedDeviceSelection selection = SelectDeviceForBinding(binding, preferredDevice, preferredSpecificDeviceId);
        const float value = ResolveAxisFromBinding(binding, selection.device);
        const float magnitude = std::abs(value);
        if (magnitude > bestMagnitude) {
            bestMagnitude = magnitude;
            resolvedValue = value;
        }
    }
    return resolvedValue;
}

bool InputManager::IsActionDownForProfile(std::string_view profileId,
                                          std::string_view action,
                                          InputDevicePreference preferredDevice,
                                          std::string_view preferredSpecificDeviceId) const {
    const InputProfile* profile = FindProfile(profileId);
    if (profile == nullptr && profileId != GetDefaultVehicleProfileId()) {
        profile = FindProfile(GetDefaultVehicleProfileId());
    }
    if (profile == nullptr && profileId != GetDefaultCharacterProfileId()) {
        profile = FindProfile(GetDefaultCharacterProfileId());
    }
    if (profile == nullptr) {
        return false;
    }

    for (const InputBinding& binding : profile->bindings) {
        if (binding.action != action) {
            continue;
        }
        const ResolvedDeviceSelection selection = SelectDeviceForBinding(binding, preferredDevice, preferredSpecificDeviceId);
        if (ResolveDigitalFromBinding(binding, selection.device)) {
            return true;
        }
    }
    return false;
}

bool InputManager::WasActionPressedForProfile(std::string_view profileId,
                                              std::string_view action,
                                              InputDevicePreference preferredDevice,
                                              std::string_view preferredSpecificDeviceId) const {
    const InputProfile* profile = FindProfile(profileId);
    if (profile == nullptr && profileId != GetDefaultVehicleProfileId()) {
        profile = FindProfile(GetDefaultVehicleProfileId());
    }
    if (profile == nullptr && profileId != GetDefaultCharacterProfileId()) {
        profile = FindProfile(GetDefaultCharacterProfileId());
    }
    if (profile == nullptr) {
        return false;
    }

    for (const InputBinding& binding : profile->bindings) {
        if (binding.action != action) {
            continue;
        }
        const ResolvedDeviceSelection selection = SelectDeviceForBinding(binding, preferredDevice, preferredSpecificDeviceId);
        if (ResolvePressedFromBinding(binding, selection.device)) {
            return true;
        }
    }
    return false;
}

void InputManager::SetInputProfiles(std::vector<InputProfile> profiles) {
    inputProfiles_ = std::move(profiles);
    EnsureDefaultProfiles();
}

void InputManager::SetWheelSettingsProfiles(std::vector<WheelSettingsProfile> profiles) {
    wheelSettingsProfiles_ = std::move(profiles);
    EnsureDefaultWheelSettingsProfiles();
    if (wheelForceFeedbackController_) {
        wheelForceFeedbackController_->SetProfiles(wheelSettingsProfiles_);
    }
}

const InputProfile* InputManager::FindProfile(std::string_view profileId) const {
    return FindProfileById(inputProfiles_, profileId);
}

InputProfile* InputManager::FindProfile(std::string_view profileId) {
    return FindProfileById(inputProfiles_, profileId);
}

void InputManager::EnsureDefaultProfiles() {
    auto addMissingBindings = [](InputProfile& profile, const std::vector<InputBinding>& defaults) {
        for (const InputBinding& defaultBinding : defaults) {
            const auto existing = std::find_if(profile.bindings.begin(), profile.bindings.end(), [&](const InputBinding& binding) {
                return binding.action == defaultBinding.action &&
                       binding.deviceType == defaultBinding.deviceType &&
                       binding.source == defaultBinding.source;
            });
            if (existing == profile.bindings.end()) {
                profile.bindings.push_back(defaultBinding);
            }
        }
    };

    const std::vector<InputBinding> defaultCharacterBindings = {
        {"moveX", InputDeviceType::Keyboard, InputBindingSource::KeyPair, -1, GLFW_KEY_A, GLFW_KEY_D},
        {"moveY", InputDeviceType::Keyboard, InputBindingSource::KeyPair, -1, GLFW_KEY_S, GLFW_KEY_W},
        {"jump", InputDeviceType::Keyboard, InputBindingSource::Key, GLFW_KEY_SPACE},
        {"moveX", InputDeviceType::Gamepad, InputBindingSource::Axis, -1, -1, -1, GLFW_GAMEPAD_AXIS_LEFT_X, -1, false, 0.18f},
        {"moveY", InputDeviceType::Gamepad, InputBindingSource::Axis, -1, -1, -1, GLFW_GAMEPAD_AXIS_LEFT_Y, -1, true, 0.18f},
        {"lookX", InputDeviceType::Gamepad, InputBindingSource::Axis, -1, -1, -1, GLFW_GAMEPAD_AXIS_RIGHT_X, -1, false, 0.18f},
        {"lookY", InputDeviceType::Gamepad, InputBindingSource::Axis, -1, -1, -1, GLFW_GAMEPAD_AXIS_RIGHT_Y, -1, true, 0.18f},
        {"jump", InputDeviceType::Gamepad, InputBindingSource::Button, -1, -1, -1, -1, GLFW_GAMEPAD_BUTTON_A},
        {"moveX", InputDeviceType::Wheel, InputBindingSource::Axis, -1, -1, -1, 0, -1, false, 0.08f},
        {"moveY", InputDeviceType::Wheel, InputBindingSource::Axis, -1, -1, -1, 1, -1, true, 0.05f},
        {"jump", InputDeviceType::Wheel, InputBindingSource::Button, -1, -1, -1, -1, 0}
    };

    const std::vector<InputBinding> defaultVehicleBindings = {
        {"steer", InputDeviceType::Keyboard, InputBindingSource::KeyPair, -1, GLFW_KEY_A, GLFW_KEY_D},
        {"throttle", InputDeviceType::Keyboard, InputBindingSource::KeyPair, -1, -1, GLFW_KEY_W},
        {"brake", InputDeviceType::Keyboard, InputBindingSource::KeyPair, -1, -1, GLFW_KEY_S},
        {"handbrake", InputDeviceType::Keyboard, InputBindingSource::Key, GLFW_KEY_SPACE},
        {"shiftUp", InputDeviceType::Keyboard, InputBindingSource::Key, GLFW_KEY_E},
        {"shiftDown", InputDeviceType::Keyboard, InputBindingSource::Key, GLFW_KEY_Q},
        {"neutral", InputDeviceType::Keyboard, InputBindingSource::Key, GLFW_KEY_N},
        {"reverse", InputDeviceType::Keyboard, InputBindingSource::Key, GLFW_KEY_R},
        {"steer", InputDeviceType::Gamepad, InputBindingSource::Axis, -1, -1, -1, GLFW_GAMEPAD_AXIS_LEFT_X, -1, false, 0.18f},
        {"throttle", InputDeviceType::Gamepad, InputBindingSource::Axis, -1, -1, -1, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, -1, false, 0.05f, -1.0f, -1.0f, 1.0f},
        {"brake", InputDeviceType::Gamepad, InputBindingSource::Axis, -1, -1, -1, GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, -1, false, 0.05f, -1.0f, -1.0f, 1.0f},
        {"handbrake", InputDeviceType::Gamepad, InputBindingSource::Button, -1, -1, -1, -1, GLFW_GAMEPAD_BUTTON_B},
        {"shiftUp", InputDeviceType::Gamepad, InputBindingSource::Button, -1, -1, -1, -1, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER},
        {"shiftDown", InputDeviceType::Gamepad, InputBindingSource::Button, -1, -1, -1, -1, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER},
        {"reverse", InputDeviceType::Gamepad, InputBindingSource::Button, -1, -1, -1, -1, GLFW_GAMEPAD_BUTTON_Y},
        {"steer", InputDeviceType::Wheel, InputBindingSource::Axis, -1, -1, -1, 0, -1, false, 0.05f},
        {"throttle", InputDeviceType::Wheel, InputBindingSource::Axis, -1, -1, -1, 1, -1, true, 0.02f},
        {"brake", InputDeviceType::Wheel, InputBindingSource::Axis, -1, -1, -1, 2, -1, true, 0.02f},
        {"handbrake", InputDeviceType::Wheel, InputBindingSource::Button, -1, -1, -1, -1, 0},
        {"shiftUp", InputDeviceType::Wheel, InputBindingSource::Button, -1, -1, -1, -1, 4},
        {"shiftDown", InputDeviceType::Wheel, InputBindingSource::Button, -1, -1, -1, -1, 5}
    };

    if (FindProfile("default_character") == nullptr) {
        InputProfile profile;
        profile.id = "default_character";
        profile.displayName = "Default Character";
        profile.bindings = defaultCharacterBindings;
        inputProfiles_.push_back(std::move(profile));
    } else if (InputProfile* profile = FindProfile("default_character")) {
        addMissingBindings(*profile, defaultCharacterBindings);
    }

    if (FindProfile("default_vehicle") == nullptr) {
        InputProfile profile;
        profile.id = "default_vehicle";
        profile.displayName = "Default Vehicle";
        profile.bindings = defaultVehicleBindings;
        inputProfiles_.push_back(std::move(profile));
    } else if (InputProfile* profile = FindProfile("default_vehicle")) {
        addMissingBindings(*profile, defaultVehicleBindings);
    }
}

void InputManager::EnsureDefaultWheelSettingsProfiles() {
    if (wheelSettingsProfiles_.empty()) {
        WheelSettingsProfile profile;
        profile.id = "default_wheel";
        profile.displayName = "Default Wheel";
        profile.forceFeedbackEnabled = true;
        profile.forceFeedbackOverallStrength = 1.0f;
        profile.forceFeedbackSelfAligningTorque = 1.25f;
        profile.forceFeedbackDamper = 0.15f;
        profile.forceFeedbackRoadEffects = 0.35f;
        profile.forceFeedbackSlipEffects = 0.2f;
        profile.forceFeedbackCollisionEffects = 0.45f;
        profile.forceFeedbackMinimumForce = 0.08f;
        wheelSettingsProfiles_.push_back(std::move(profile));
    }
}

void InputManager::SetWheelForceFeedbackActive(bool active) {
    wheelForceFeedbackActive_ = active;
    if (wheelForceFeedbackController_) {
        const bool wheelWindowFocused = window_ == nullptr || glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
        wheelForceFeedbackController_->SetEffectState(
            wheelForceFeedbackTorque_,
            wheelForceFeedbackDamper_,
            wheelForceFeedbackVibration_);
        wheelForceFeedbackController_->SyncDevices(devices_, wheelForceFeedbackActive_ && wheelWindowFocused);
    }
}

void InputManager::SetWheelForceFeedbackState(float steeringTorque, float damper, float vibration) {
    wheelForceFeedbackTorque_ = steeringTorque;
    wheelForceFeedbackDamper_ = damper;
    wheelForceFeedbackVibration_ = vibration;
    if (wheelForceFeedbackController_) {
        wheelForceFeedbackController_->SetEffectState(
            wheelForceFeedbackTorque_,
            wheelForceFeedbackDamper_,
            wheelForceFeedbackVibration_);
    }
}

void InputManager::SetLogCallback(std::function<void(const std::string&)> callback) {
    logCallback_ = std::move(callback);
    if (wheelForceFeedbackController_) {
        if (logCallback_) {
            wheelForceFeedbackController_->SetLogCallback([this](const std::string& message) {
                if (logCallback_) {
                    logCallback_("[WheelFFB] " + message);
                }
            });
        } else {
            wheelForceFeedbackController_->SetLogCallback([](const std::string& message) {
                std::fprintf(stdout, "[WheelFFB] %s\n", message.c_str());
                std::fflush(stdout);
            });
        }
    }
}

void InputManager::StartListeningForBinding(InputDeviceType deviceType, InputBindingSource source) {
    listeningForBinding_ = true;
    capturedBindingReady_ = false;
    capturedBinding_ = {};
    listeningDeviceType_ = deviceType;
    listeningSource_ = source;
    listeningAxisBaseline_.clear();
    if (source == InputBindingSource::Axis) {
        for (const InputDeviceInfo& device : devices_) {
            if (device.type != deviceType) {
                continue;
            }
            for (int axisIndex = 0; axisIndex < static_cast<int>(device.axes.size()); ++axisIndex) {
                listeningAxisBaseline_[device.joystickId * 1000 + axisIndex] =
                    device.axes[static_cast<std::size_t>(axisIndex)];
            }
        }
    }
}

void InputManager::CancelListeningForBinding() {
    listeningForBinding_ = false;
    capturedBindingReady_ = false;
    capturedBinding_ = {};
    listeningDeviceType_ = InputDeviceType::Unknown;
    listeningSource_ = InputBindingSource::None;
    listeningAxisBaseline_.clear();
}

bool InputManager::ConsumeCapturedBinding(InputBinding& binding) {
    if (!capturedBindingReady_) {
        return false;
    }
    binding = capturedBinding_;
    capturedBindingReady_ = false;
    listeningForBinding_ = false;
    listeningDeviceType_ = InputDeviceType::Unknown;
    listeningSource_ = InputBindingSource::None;
    listeningAxisBaseline_.clear();
    return true;
}

void InputManager::RegisterKeyCallback(const std::function<void(int)>& callback) {
    keyCallbacks_.push_back(callback);
}

void InputManager::KeyCallback(GLFWwindow* window, int key, int, int action, int) {
    auto* manager = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!manager) {
        return;
    }

    if (action == GLFW_PRESS) {
        manager->keyState_[key] = true;
        manager->keyPressed_[key] = true;
        if (manager->listeningForBinding_ &&
            manager->listeningDeviceType_ == InputDeviceType::Keyboard &&
            (manager->listeningSource_ == InputBindingSource::Key || manager->listeningSource_ == InputBindingSource::KeyPair)) {
            manager->capturedBinding_ = {};
            manager->capturedBinding_.deviceType = InputDeviceType::Keyboard;
            manager->capturedBinding_.source = InputBindingSource::Key;
            manager->capturedBinding_.key = key;
            manager->capturedBindingReady_ = true;
        }
        for (auto& callback : manager->keyCallbacks_) {
            callback(key);
        }
    } else if (action == GLFW_REPEAT) {
        manager->keyState_[key] = true;
    } else if (action == GLFW_RELEASE) {
        manager->keyState_[key] = false;
    }
}

void InputManager::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto* manager = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!manager) {
        return;
    }

    manager->pendingMouseWheelDelta_ += static_cast<float>(yoffset);

    (void)xoffset;
}

void InputManager::PollDevices() {
    devices_.clear();

    InputDeviceInfo keyboard;
    keyboard.runtimeId = "keyboard";
    keyboard.displayName = "Keyboard";
    keyboard.type = InputDeviceType::Keyboard;
    keyboard.connected = true;
    devices_.push_back(std::move(keyboard));

    for (int joystickId = kFirstJoystick; joystickId <= kLastJoystick; ++joystickId) {
        if (!glfwJoystickPresent(joystickId)) {
            continue;
        }

        const char* joystickName = glfwGetJoystickName(joystickId);
#if RACEMAN_GLFW_HAS_GAMEPAD_API
        const bool hasGamepadMapping = glfwJoystickIsGamepad(joystickId) == GLFW_TRUE;
        const bool isGamepad = hasGamepadMapping || LooksLikeGamepadName(joystickName);
#else
        const bool hasGamepadMapping = false;
        const bool isGamepad = LooksLikeGamepadName(joystickName);
#endif
        InputDeviceInfo device;
        device.runtimeId = MakeJoystickRuntimeId(joystickId, joystickName);
        device.displayName = joystickName != nullptr ? joystickName : ("Joystick " + std::to_string(joystickId));
        device.type = InferJoystickType(isGamepad, joystickName);
        device.joystickId = joystickId;
        device.connected = true;
        device.isGamepad = isGamepad;
        if (isGamepad) {
#if RACEMAN_GLFW_HAS_GAMEPAD_API
            GLFWgamepadstate gamepadState{};
            if (hasGamepadMapping && glfwGetGamepadState(joystickId, &gamepadState) == GLFW_TRUE) {
                device.axisCount = GLFW_GAMEPAD_AXIS_LAST + 1;
                device.buttonCount = GLFW_GAMEPAD_BUTTON_LAST + 1;
                device.axes.assign(gamepadState.axes, gamepadState.axes + device.axisCount);
                device.buttons.assign(gamepadState.buttons, gamepadState.buttons + device.buttonCount);
            } else {
                int axisCount = 0;
                int buttonCount = 0;
                const float* axes = glfwGetJoystickAxes(joystickId, &axisCount);
                const unsigned char* buttons = glfwGetJoystickButtons(joystickId, &buttonCount);
                device.axisCount = axisCount;
                device.buttonCount = buttonCount;
                if (axes != nullptr && axisCount > 0) {
                    device.axes.assign(axes, axes + axisCount);
                }
                if (buttons != nullptr && buttonCount > 0) {
                    device.buttons.assign(buttons, buttons + buttonCount);
                }
            }
#else
            int axisCount = 0;
            int buttonCount = 0;
            const float* axes = glfwGetJoystickAxes(joystickId, &axisCount);
            const unsigned char* buttons = glfwGetJoystickButtons(joystickId, &buttonCount);
            device.axisCount = axisCount;
            device.buttonCount = buttonCount;
            if (axes != nullptr && axisCount > 0) {
                device.axes.assign(axes, axes + axisCount);
            }
            if (buttons != nullptr && buttonCount > 0) {
                device.buttons.assign(buttons, buttons + buttonCount);
            }
#endif
        } else {
            int axisCount = 0;
            int buttonCount = 0;
            const float* axes = glfwGetJoystickAxes(joystickId, &axisCount);
            const unsigned char* buttons = glfwGetJoystickButtons(joystickId, &buttonCount);
            device.axisCount = axisCount;
            device.buttonCount = buttonCount;
            if (axes != nullptr && axisCount > 0) {
                device.axes.assign(axes, axes + axisCount);
            }
            if (buttons != nullptr && buttonCount > 0) {
                device.buttons.assign(buttons, buttons + buttonCount);
            }
        }

        if (listeningForBinding_ &&
            !capturedBindingReady_ &&
            listeningDeviceType_ == device.type &&
            listeningSource_ == InputBindingSource::Axis) {
            for (int axisIndex = 0; axisIndex < static_cast<int>(device.axes.size()); ++axisIndex) {
                const int axisKey = joystickId * 1000 + axisIndex;
                const float currentValue = device.axes[static_cast<std::size_t>(axisIndex)];
                auto baselineIt = listeningAxisBaseline_.find(axisKey);
                if (baselineIt == listeningAxisBaseline_.end()) {
                    baselineIt = listeningAxisBaseline_.emplace(axisKey, currentValue).first;
                }
                if (std::abs(currentValue - baselineIt->second) >= 0.5f) {
                    capturedBinding_ = {};
                    capturedBinding_.deviceType = device.type;
                    capturedBinding_.source = InputBindingSource::Axis;
                    capturedBinding_.axis = axisIndex;
                    capturedBindingReady_ = true;
                    break;
                }
            }
        }

        for (int buttonIndex = 0; buttonIndex < static_cast<int>(device.buttons.size()); ++buttonIndex) {
            const int buttonKey = joystickId * 1000 + buttonIndex;
            PolledButtonState& state = joystickButtons_[buttonKey];
            const bool down = device.buttons[static_cast<std::size_t>(buttonIndex)] == GLFW_PRESS;
            state.pressed = !state.down && down;
            state.down = down;
            if (listeningForBinding_ &&
                !capturedBindingReady_ &&
                listeningDeviceType_ == device.type &&
                listeningSource_ == InputBindingSource::Button &&
                state.pressed) {
                capturedBinding_ = {};
                capturedBinding_.deviceType = device.type;
                capturedBinding_.source = InputBindingSource::Button;
                capturedBinding_.button = buttonIndex;
                capturedBindingReady_ = true;
            }
        }

        devices_.push_back(std::move(device));
    }
}

float InputManager::ResolveAxisFromBinding(const InputBinding& binding, const InputDeviceInfo* device) const {
    switch (binding.source) {
    case InputBindingSource::Key:
        return (binding.key >= 0 && IsKeyDown(binding.key)) ? 1.0f : 0.0f;
    case InputBindingSource::KeyPair: {
        const float negative = (binding.negativeKey >= 0 && IsKeyDown(binding.negativeKey)) ? -1.0f : 0.0f;
        const float positive = (binding.positiveKey >= 0 && IsKeyDown(binding.positiveKey)) ? 1.0f : 0.0f;
        return negative + positive;
    }
    case InputBindingSource::Axis:
        if (device == nullptr || binding.axis < 0 || binding.axis >= static_cast<int>(device->axes.size())) {
            return 0.0f;
        }
        return ApplyAxisTuning(device->axes[static_cast<std::size_t>(binding.axis)], binding);
    case InputBindingSource::Button:
        if (device == nullptr || binding.button < 0 || binding.button >= static_cast<int>(device->buttons.size())) {
            return 0.0f;
        }
        return device->buttons[static_cast<std::size_t>(binding.button)] == GLFW_PRESS ? 1.0f : 0.0f;
    case InputBindingSource::None:
    default:
        return 0.0f;
    }
}

bool InputManager::ResolveDigitalFromBinding(const InputBinding& binding, const InputDeviceInfo* device) const {
    if (binding.source == InputBindingSource::Key) {
        return binding.key >= 0 && IsKeyDown(binding.key);
    }
    if (binding.source == InputBindingSource::Button) {
        return device != nullptr &&
               binding.button >= 0 &&
               binding.button < static_cast<int>(device->buttons.size()) &&
               device->buttons[static_cast<std::size_t>(binding.button)] == GLFW_PRESS;
    }
    return std::abs(ResolveAxisFromBinding(binding, device)) >= kDigitalPressThreshold;
}

bool InputManager::ResolvePressedFromBinding(const InputBinding& binding, const InputDeviceInfo* device) const {
    if (binding.source == InputBindingSource::Key) {
        return binding.key >= 0 && WasKeyPressed(binding.key);
    }
    if (binding.source == InputBindingSource::Button && device != nullptr && binding.button >= 0) {
        const int buttonKey = device->joystickId * 1000 + binding.button;
        auto it = joystickButtons_.find(buttonKey);
        return it != joystickButtons_.end() && it->second.pressed;
    }
    return false;
}

InputManager::ResolvedDeviceSelection InputManager::SelectDeviceForBinding(const InputBinding& binding,
                                                                           InputDevicePreference preferredDevice,
                                                                           std::string_view preferredSpecificDeviceId) const {
    if (binding.deviceType == InputDeviceType::Keyboard) {
        if (preferredDevice != InputDevicePreference::Any &&
            preferredDevice != InputDevicePreference::Keyboard) {
            return {};
        }
        return {nullptr, InputDeviceType::Keyboard, true};
    }

    if ((preferredDevice == InputDevicePreference::Keyboard && binding.deviceType != InputDeviceType::Keyboard) ||
        (preferredDevice == InputDevicePreference::Gamepad && binding.deviceType != InputDeviceType::Gamepad) ||
        (preferredDevice == InputDevicePreference::Wheel && binding.deviceType != InputDeviceType::Wheel)) {
        return {};
    }

    auto matchesPreference = [&](const InputDeviceInfo& device) {
        if (device.type != binding.deviceType) {
            return false;
        }
        switch (preferredDevice) {
        case InputDevicePreference::Any:
            return true;
        case InputDevicePreference::Keyboard:
            return binding.deviceType == InputDeviceType::Keyboard;
        case InputDevicePreference::Gamepad:
            return device.type == InputDeviceType::Gamepad;
        case InputDevicePreference::Wheel:
            return device.type == InputDeviceType::Wheel;
        case InputDevicePreference::Specific:
            return !preferredSpecificDeviceId.empty() && device.runtimeId == preferredSpecificDeviceId;
        }
        return true;
    };

    auto it = std::find_if(devices_.begin(), devices_.end(), matchesPreference);
    if (it != devices_.end()) {
        return {&(*it), it->type, false};
    }

    it = std::find_if(devices_.begin(), devices_.end(), [&](const InputDeviceInfo& device) {
        return device.type == binding.deviceType;
    });
    if (it != devices_.end()) {
        return {&(*it), it->type, false};
    }

    return {};
}

std::string InputManager::MakeJoystickRuntimeId(int joystickId, const char* name) {
    return "joy:" + std::to_string(joystickId) + ":" + (name != nullptr ? name : "unknown");
}

InputDeviceType InputManager::InferJoystickType(bool isGamepad, const char* name) {
    if (isGamepad) {
        return InputDeviceType::Gamepad;
    }
    const std::string lowerName = ToLowerCopy(name != nullptr ? name : "");
    if (lowerName.find("wheel") != std::string::npos ||
        lowerName.find("logitech g") != std::string::npos ||
        lowerName.find("thrustmaster") != std::string::npos ||
        lowerName.find("fanatec") != std::string::npos ||
        lowerName.find("pedal") != std::string::npos) {
        return InputDeviceType::Wheel;
    }
    return InputDeviceType::Unknown;
}

float InputManager::ApplyAxisTuning(float rawValue, const InputBinding& binding) {
    float value = rawValue;
    const float minValue = binding.calibrationMin;
    const float centerValue = binding.calibrationCenter;
    const float maxValue = binding.calibrationMax;

    if (centerValue > minValue && maxValue > centerValue) {
        if (value >= centerValue) {
            value = (value - centerValue) / (std::max)(0.0001f, maxValue - centerValue);
        } else {
            value = (value - centerValue) / (std::max)(0.0001f, centerValue - minValue);
        }
    }

    value = (std::max)(-1.0f, (std::min)(1.0f, value));
    if (binding.invert) {
        value = -value;
    }

    if (std::abs(value) < binding.deadzone) {
        return 0.0f;
    }

    const float normalized = (std::abs(value) - binding.deadzone) / (std::max)(0.0001f, 1.0f - binding.deadzone);
    const float curved = std::pow((std::max)(0.0f, normalized), (std::max)(0.01f, binding.responseExponent));
    return value < 0.0f ? -curved : curved;
}

const char* InputManager::GetDefaultCharacterProfileId() {
    return "default_character";
}

const char* InputManager::GetDefaultVehicleProfileId() {
    return "default_vehicle";
}

} // namespace raceman

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

struct GLFWwindow;

typedef void (*GLFWkeyfun)(GLFWwindow*, int, int, int, int);

namespace raceman {

enum class InputDeviceType {
    Keyboard,
    Gamepad,
    Wheel,
    Unknown
};

enum class InputBindingSource {
    None,
    Key,
    KeyPair,
    Axis,
    Button
};

enum class InputDevicePreference {
    Any,
    Keyboard,
    Gamepad,
    Wheel,
    Specific
};

struct InputBinding {
    std::string action;
    InputDeviceType deviceType{InputDeviceType::Keyboard};
    InputBindingSource source{InputBindingSource::None};
    int key{-1};
    int negativeKey{-1};
    int positiveKey{-1};
    int axis{-1};
    int button{-1};
    bool invert{false};
    float deadzone{0.1f};
    float calibrationMin{-1.0f};
    float calibrationCenter{0.0f};
    float calibrationMax{1.0f};
    float responseExponent{1.0f};
};

struct InputProfile {
    std::string id;
    std::string displayName;
    std::vector<InputBinding> bindings;
};

struct InputDeviceInfo {
    std::string runtimeId;
    std::string displayName;
    InputDeviceType type{InputDeviceType::Unknown};
    int joystickId{-1};
    bool connected{false};
    bool isGamepad{false};
    int axisCount{0};
    int buttonCount{0};
    std::vector<float> axes;
    std::vector<unsigned char> buttons;
};

class InputManager {
public:
    InputManager() = default;
    ~InputManager() = default;

    void AttachToWindow(GLFWwindow* window);
    void BeginFrame();
    void EndFrame();

    bool WasKeyPressed(int key) const;
    bool IsKeyDown(int key) const;
    bool IsMouseButtonDown(int button) const;
    bool WasMouseButtonPressed(int button) const;
    glm::vec2 GetMouseDelta() const;
    float GetAxis(std::string_view action) const;
    bool IsActionDown(std::string_view action) const;
    bool WasActionPressed(std::string_view action) const;

    float GetAxisForProfile(std::string_view profileId,
                            std::string_view action,
                            InputDevicePreference preferredDevice = InputDevicePreference::Any,
                            std::string_view preferredSpecificDeviceId = {}) const;
    bool IsActionDownForProfile(std::string_view profileId,
                                std::string_view action,
                                InputDevicePreference preferredDevice = InputDevicePreference::Any,
                                std::string_view preferredSpecificDeviceId = {}) const;
    bool WasActionPressedForProfile(std::string_view profileId,
                                    std::string_view action,
                                    InputDevicePreference preferredDevice = InputDevicePreference::Any,
                                    std::string_view preferredSpecificDeviceId = {}) const;

    void SetInputProfiles(std::vector<InputProfile> profiles);
    std::vector<InputProfile>& GetInputProfiles() { return inputProfiles_; }
    const std::vector<InputProfile>& GetInputProfiles() const { return inputProfiles_; }
    const InputProfile* FindProfile(std::string_view profileId) const;
    InputProfile* FindProfile(std::string_view profileId);
    void EnsureDefaultProfiles();
    const std::vector<InputDeviceInfo>& GetConnectedDevices() const { return devices_; }
    bool IsListeningForBinding() const { return listeningForBinding_; }
    void StartListeningForBinding(InputDeviceType deviceType, InputBindingSource source);
    void CancelListeningForBinding();
    bool ConsumeCapturedBinding(InputBinding& binding);

    void RegisterKeyCallback(const std::function<void(int)>& callback);

    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

private:
    struct PolledButtonState {
        bool down{false};
        bool pressed{false};
    };

    struct ResolvedDeviceSelection {
        const InputDeviceInfo* device{nullptr};
        InputDeviceType deviceType{InputDeviceType::Unknown};
        bool useKeyboard{false};
    };

    void PollDevices();
    float ResolveAxisFromBinding(const InputBinding& binding, const InputDeviceInfo* device) const;
    bool ResolveDigitalFromBinding(const InputBinding& binding, const InputDeviceInfo* device) const;
    bool ResolvePressedFromBinding(const InputBinding& binding, const InputDeviceInfo* device) const;
    ResolvedDeviceSelection SelectDeviceForBinding(const InputBinding& binding,
                                                   InputDevicePreference preferredDevice,
                                                   std::string_view preferredSpecificDeviceId) const;
    static std::string MakeJoystickRuntimeId(int joystickId, const char* name);
    static InputDeviceType InferJoystickType(bool isGamepad, const char* name);
    static float ApplyAxisTuning(float rawValue, const InputBinding& binding);
    static const char* GetDefaultCharacterProfileId();
    static const char* GetDefaultVehicleProfileId();

    GLFWwindow* window_{nullptr};
    std::unordered_map<int, bool> keyState_;
    std::unordered_map<int, bool> keyPressed_;
    std::unordered_map<int, bool> mouseButtonState_;
    std::unordered_map<int, bool> mouseButtonPressed_;
    std::vector<std::function<void(int)>> keyCallbacks_;
    std::unordered_map<int, PolledButtonState> joystickButtons_;
    std::vector<InputDeviceInfo> devices_;
    std::vector<InputProfile> inputProfiles_;
    glm::vec2 mousePosition_{0.0f, 0.0f};
    glm::vec2 previousMousePosition_{0.0f, 0.0f};
    glm::vec2 mouseDelta_{0.0f, 0.0f};
    bool mousePositionInitialized_{false};
    bool listeningForBinding_{false};
    bool capturedBindingReady_{false};
    InputBinding capturedBinding_{};
    InputDeviceType listeningDeviceType_{InputDeviceType::Unknown};
    InputBindingSource listeningSource_{InputBindingSource::None};
};

} // namespace raceman

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

struct GLFWwindow;

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
    float keyboardSteeringSensitivity{0.35f};
    float keyboardThrottleSensitivity{0.45f};
    float keyboardBrakeSensitivity{0.45f};
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

struct WheelSettingsProfile {
    std::string id{"default_wheel"};
    std::string displayName{"Default Wheel"};
    std::string deviceNamePattern;
    // Lock-to-lock rotation the hardware/driver is configured for.
    float steeringPhysicalRangeDegrees{900.0f};
    // Rotation that maps to full in-game lock. Smaller than the physical range
    // means the wheel reaches full lock before its end stops (soft lock).
    float steeringRangeDegrees{900.0f};
    float steeringSensitivity{1.0f};
    float steeringSaturation{1.0f};
    bool steeringInvert{false};
    float steeringDeadzone{0.0f};
    float steeringCalibrationMin{-1.0f};
    float steeringCalibrationCenter{0.0f};
    float steeringCalibrationMax{1.0f};
    float steeringResponseExponent{1.0f};
    // 0 = raw axis, 1 = heavily filtered. Applied per vehicle sample.
    float steeringSmoothing{0.0f};
    // 0 = constant lock, 1 = strongly reduced lock at top speed.
    float steeringSpeedSensitivity{0.0f};
    // Auto-return rate (units/s) used for devices that cannot self-centre.
    float steeringReturnRate{0.0f};
    bool combinedPedals{false};
    bool throttleInvert{true};
    float throttleDeadzone{0.02f};
    float throttleSaturation{1.0f};
    float throttleCalibrationMin{-1.0f};
    float throttleCalibrationCenter{-1.0f};
    float throttleCalibrationMax{1.0f};
    float throttleResponseExponent{1.0f};
    bool brakeInvert{true};
    float brakeDeadzone{0.02f};
    float brakeSaturation{1.0f};
    float brakeCalibrationMin{-1.0f};
    float brakeCalibrationCenter{-1.0f};
    float brakeCalibrationMax{1.0f};
    float brakeResponseExponent{1.0f};
    bool clutchInvert{true};
    float clutchDeadzone{0.02f};
    float clutchSaturation{1.0f};
    float clutchCalibrationMin{-1.0f};
    float clutchCalibrationCenter{-1.0f};
    float clutchCalibrationMax{1.0f};
    float clutchResponseExponent{1.0f};
    bool forceFeedbackEnabled{false};
    bool forceFeedbackInvert{false};
    float forceFeedbackOverallStrength{0.75f};
    float forceFeedbackSelfAligningTorque{1.0f};
    float forceFeedbackRoadEffects{0.2f};
    float forceFeedbackSlipEffects{0.15f};
    float forceFeedbackCollisionEffects{0.35f};
    float forceFeedbackKerbEffects{0.5f};
    float forceFeedbackEngineEffects{0.0f};
    float forceFeedbackLockupEffects{0.4f};
    float forceFeedbackDamper{0.1f};
    float forceFeedbackFriction{0.05f};
    float forceFeedbackSpring{0.0f};
    // Static centring spring applied when the tyres cannot generate torque
    // (standstill, airborne). Doubles as the wheel return rate.
    float forceFeedbackCenteringSpring{0.25f};
    // End-stop spring applied past the configured steering range.
    float forceFeedbackSoftLock{0.6f};
    float forceFeedbackSmoothing{0.15f};
    float forceFeedbackMinimumForce{0.0f};
};

// Per-frame force feedback request produced by the vehicle simulation and
// consumed by the platform force feedback backend.
struct WheelForceFeedbackState {
    // Signed steering torque in [-1, 1]. Positive pulls the wheel clockwise.
    float steeringTorque{0.0f};
    // Dynamic condition strengths in [0, 1].
    float damper{0.0f};
    float friction{0.0f};
    float centeringSpring{0.0f};
    // Texture components in [0, 1]. They are kept apart so each one can be
    // scaled by its own preset gain before being mixed into the device-side
    // periodic effect.
    float roadAmplitude{0.0f};
    float slipAmplitude{0.0f};
    float kerbAmplitude{0.0f};
    float lockupAmplitude{0.0f};
    // Combined amplitude, used by callers that do not separate components.
    float vibrationAmplitude{0.0f};
    float vibrationFrequencyHz{0.0f};
    // Low frequency engine/drivetrain rumble.
    float rumbleAmplitude{0.0f};
    float rumbleFrequencyHz{0.0f};
    // One-shot impact spike (kerbs, collisions) with its direction.
    float impact{0.0f};
    float impactDirection{0.0f};
};

class WheelForceFeedbackController;

class InputManager {
public:
    InputManager();
    ~InputManager();

    void AttachToWindow(GLFWwindow* window);
    void BeginFrame();
    void EndFrame();

    bool WasKeyPressed(int key) const;
    bool IsKeyDown(int key) const;
    bool IsMouseButtonDown(int button) const;
    bool WasMouseButtonPressed(int button) const;
    glm::vec2 GetMouseDelta() const;
    float GetMouseWheelDelta() const;
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
    void SetWheelSettingsProfiles(std::vector<WheelSettingsProfile> profiles);
    std::vector<WheelSettingsProfile>& GetWheelSettingsProfiles() { return wheelSettingsProfiles_; }
    const std::vector<WheelSettingsProfile>& GetWheelSettingsProfiles() const { return wheelSettingsProfiles_; }
    const InputProfile* FindProfile(std::string_view profileId) const;
    InputProfile* FindProfile(std::string_view profileId);
    void EnsureDefaultProfiles();
    void EnsureDefaultWheelSettingsProfiles();
    const std::vector<InputDeviceInfo>& GetConnectedDevices() const { return devices_; }
    // First connected wheel, or nullptr when none is attached.
    const InputDeviceInfo* FindPrimaryWheelDevice() const;
    // Wheel preset that matches a device, falling back to the default preset.
    const WheelSettingsProfile* FindWheelSettingsForDevice(const InputDeviceInfo& device) const;
    // Normalized wheel position (-1..1) after calibration but before range,
    // sensitivity and deadzone shaping. Used for soft lock and calibration UI.
    float GetWheelSteeringPosition() const;
    void SetWheelForceFeedbackActive(bool active);
    bool IsWheelForceFeedbackActive() const { return wheelForceFeedbackActive_; }
    void SetWheelForceFeedbackState(const WheelForceFeedbackState& state);
    void SetWheelForceFeedbackState(float steeringTorque, float damper, float vibration);
    // Plays a short test pulse so users can confirm force feedback is alive.
    void TriggerWheelForceFeedbackTest();
    void SetLogCallback(std::function<void(const std::string&)> callback);
    bool IsListeningForBinding() const { return listeningForBinding_; }
    void StartListeningForBinding(InputDeviceType deviceType, InputBindingSource source);
    void CancelListeningForBinding();
    bool ConsumeCapturedBinding(InputBinding& binding);

    void RegisterKeyCallback(const std::function<void(int)>& callback);

    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

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

    enum class WheelAxisRole {
        Steering,
        Throttle,
        Brake,
        Clutch,
        Generic
    };

    void PollDevices();
    float ResolveAxisFromBinding(const InputBinding& binding,
                                 const InputDeviceInfo* device,
                                 std::string_view action = {}) const;
    float ResolveWheelAxis(float rawValue,
                           const InputBinding& binding,
                           const InputDeviceInfo& device,
                           std::string_view action) const;
    float TuneAxisValue(float rawValue,
                        const InputBinding& binding,
                        const InputDeviceInfo* device,
                        std::string_view action) const;
    static WheelAxisRole ClassifyWheelAxisRole(std::string_view action);
    static float ApplyBipolarCalibration(float rawValue, float minValue, float centerValue, float maxValue);
    static float ApplyUnipolarCalibration(float rawValue, float minValue, float maxValue, bool invert);
    static float ShapeUnipolarAxis(float value, float deadzone, float saturation, float responseExponent);
    bool ResolveDigitalFromBinding(const InputBinding& binding,
                                   const InputDeviceInfo* device,
                                   std::string_view action = {}) const;
    bool ResolvePressedFromBinding(const InputBinding& binding,
                                   const InputDeviceInfo* device,
                                   std::string_view action = {}) const;
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
    std::unordered_map<int, bool> previousKeyState_;
    std::unordered_map<int, bool> keyPressed_;
    std::unordered_map<int, bool> mouseButtonState_;
    std::unordered_map<int, bool> mouseButtonPressed_;
    std::vector<std::function<void(int)>> keyCallbacks_;
    std::unordered_map<int, PolledButtonState> joystickButtons_;
    // Previous frame raw axis samples, keyed the same way as joystickButtons_.
    // Used to detect edges on axis bound as a button (paddles, sequential
    // shifters and hat-driven controls).
    std::unordered_map<int, float> previousAxisValues_;
    std::vector<InputDeviceInfo> devices_;
    std::vector<InputProfile> inputProfiles_;
    std::vector<WheelSettingsProfile> wheelSettingsProfiles_;
    glm::vec2 mousePosition_{0.0f, 0.0f};
    glm::vec2 previousMousePosition_{0.0f, 0.0f};
    glm::vec2 mouseDelta_{0.0f, 0.0f};
    float mouseWheelDelta_{0.0f};
    float pendingMouseWheelDelta_{0.0f};
    bool mousePositionInitialized_{false};
    bool listeningForBinding_{false};
    bool capturedBindingReady_{false};
    InputBinding capturedBinding_{};
    InputDeviceType listeningDeviceType_{InputDeviceType::Unknown};
    InputBindingSource listeningSource_{InputBindingSource::None};
    std::unordered_map<int, float> listeningAxisBaseline_;
    bool wheelForceFeedbackActive_{false};
    double wheelForceFeedbackTestUntil_{0.0};
    WheelForceFeedbackState wheelForceFeedbackState_{};
    std::function<void(const std::string&)> logCallback_;
    std::unique_ptr<WheelForceFeedbackController> wheelForceFeedbackController_;
};

} // namespace raceman

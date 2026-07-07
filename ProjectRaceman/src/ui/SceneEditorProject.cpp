#include "SceneEditorInternal.h"
#include "NativeDialogs.h"
#include "../physics/SimpleJson.h"
#include "../rendering/ShaderRegistry.h"
#include <GLFW/glfw3.h>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

const char* ProjectCreateAssetTypeTitle(ProjectCreateAssetType type) {
    if (type == ProjectCreateAssetType::Folder) return "Create Folder";
    if (type == ProjectCreateAssetType::Scene) return "Create Scene";
    if (type == ProjectCreateAssetType::Material) return "Create Material";
    if (type == ProjectCreateAssetType::VehicleProfile) return "Create Vehicle Profile";
    if (type == ProjectCreateAssetType::VehicleSoundProfile) return "Create Vehicle Sound Profile";
    if (type == ProjectCreateAssetType::Track) return "Create Track";
    if (type == ProjectCreateAssetType::Script) return "Create C++ Script";
    if (type == ProjectCreateAssetType::ShaderGraph) return "Create Shader Graph";
    return "Create Asset";
}

const char* ProjectCreateAssetTypeDefaultName(ProjectCreateAssetType type) {
    if (type == ProjectCreateAssetType::Folder) return "NewFolder";
    if (type == ProjectCreateAssetType::Scene) return "NewScene";
    if (type == ProjectCreateAssetType::Material) return "NewMaterial";
    if (type == ProjectCreateAssetType::VehicleProfile) return "NewVehicleProfile";
    if (type == ProjectCreateAssetType::VehicleSoundProfile) return "NewVehicleSoundProfile";
    if (type == ProjectCreateAssetType::Track) return "NewTrack";
    if (type == ProjectCreateAssetType::Script) return "NewScript";
    if (type == ProjectCreateAssetType::ShaderGraph) return "NewShaderGraph";
    return "NewAsset";
}

const char* InputDeviceTypeLabel(InputDeviceType type) {
    switch (type) {
    case InputDeviceType::Keyboard: return "Keyboard";
    case InputDeviceType::Gamepad: return "Gamepad";
    case InputDeviceType::Wheel: return "Wheel";
    case InputDeviceType::Unknown:
    default: return "Unknown";
    }
}

const char* InputBindingSourceLabel(InputBindingSource source) {
    switch (source) {
    case InputBindingSource::Key: return "Key";
    case InputBindingSource::KeyPair: return "Key Pair";
    case InputBindingSource::Axis: return "Axis";
    case InputBindingSource::Button: return "Button";
    case InputBindingSource::None:
    default: return "None";
    }
}

const char* GamepadButtonName(int button) {
    static const char* names[] = {
        "A / Cross",
        "B / Circle",
        "X / Square",
        "Y / Triangle",
        "Left Bumper",
        "Right Bumper",
        "Back / Share",
        "Start / Options",
        "Guide / PS",
        "Left Stick Click",
        "Right Stick Click",
        "D-Pad Up",
        "D-Pad Right",
        "D-Pad Down",
        "D-Pad Left"
    };
    return button >= 0 && button < static_cast<int>(std::size(names)) ? names[button] : "Unknown Button";
}

const char* GamepadAxisName(int axis) {
    static const char* names[] = {
        "Left Stick X",
        "Left Stick Y",
        "Right Stick X",
        "Right Stick Y",
        "Left Trigger",
        "Right Trigger"
    };
    return axis >= 0 && axis < static_cast<int>(std::size(names)) ? names[axis] : "Unknown Axis";
}

const char* InputDevicePreferenceLabel(InputDevicePreference value) {
    switch (value) {
    case InputDevicePreference::Any: return "Any";
    case InputDevicePreference::Keyboard: return "Keyboard";
    case InputDevicePreference::Gamepad: return "Gamepad";
    case InputDevicePreference::Wheel: return "Wheel";
    case InputDevicePreference::Specific: return "Specific Device";
    }
    return "Any";
}

const InputDeviceInfo* FindFirstDeviceOfType(const InputManager* inputManager, InputDeviceType type) {
    if (inputManager == nullptr) {
        return nullptr;
    }
    const auto& devices = inputManager->GetConnectedDevices();
    auto it = std::find_if(devices.begin(), devices.end(), [&](const InputDeviceInfo& device) {
        return device.type == type;
    });
    return it != devices.end() ? &(*it) : nullptr;
}

void SyncInputProfiles(InputManager* inputManager, std::vector<InputProfile>& profiles) {
    if (inputManager != nullptr) {
        inputManager->SetInputProfiles(profiles);
        profiles = inputManager->GetInputProfiles();
    }
}

void SyncWheelSettingsProfiles(InputManager* inputManager, std::vector<WheelSettingsProfile>& profiles) {
    if (inputManager != nullptr) {
        inputManager->SetWheelSettingsProfiles(profiles);
        profiles = inputManager->GetWheelSettingsProfiles();
    }
}

InputBinding* FindBindingForAction(InputProfile& profile, InputDeviceType deviceType, const std::string& action) {
    auto it = std::find_if(profile.bindings.begin(), profile.bindings.end(), [&](InputBinding& binding) {
        return binding.deviceType == deviceType && binding.action == action;
    });
    return it != profile.bindings.end() ? &(*it) : nullptr;
}

float NormalizeKeyboardSensitivity(float value) {
    if (value > 1.0f) {
        return (std::clamp)(value / 30.0f, 0.0f, 1.0f);
    }
    return (std::clamp)(value, 0.0f, 1.0f);
}

float KeyboardSteeringSensitivityToRate(float value) {
    return 1.0f + NormalizeKeyboardSensitivity(value) * 11.0f;
}

float KeyboardThrottleSensitivityToRate(float value) {
    return 1.0f + NormalizeKeyboardSensitivity(value) * 9.0f;
}

float KeyboardBrakeSensitivityToRate(float value) {
    return 1.0f + NormalizeKeyboardSensitivity(value) * 9.0f;
}

float MoveTowardsPreview(float current, float target, float maxDelta) {
    if (current < target) {
        return (std::min)(current + maxDelta, target);
    }
    return (std::max)(current - maxDelta, target);
}

float ResolveKeyboardAxisPreview(const InputManager* inputManager, const InputProfile& profile, std::string_view action) {
    if (inputManager == nullptr) {
        return 0.0f;
    }

    float bestMagnitude = 0.0f;
    float resolved = 0.0f;
    for (const InputBinding& binding : profile.bindings) {
        if (binding.action != action || binding.deviceType != InputDeviceType::Keyboard) {
            continue;
        }

        float value = 0.0f;
        if (binding.source == InputBindingSource::Key) {
            value = binding.key >= 0 && inputManager->IsKeyDown(binding.key) ? 1.0f : 0.0f;
        } else if (binding.source == InputBindingSource::KeyPair) {
            const float negative = binding.negativeKey >= 0 && inputManager->IsKeyDown(binding.negativeKey) ? -1.0f : 0.0f;
            const float positive = binding.positiveKey >= 0 && inputManager->IsKeyDown(binding.positiveKey) ? 1.0f : 0.0f;
            value = negative + positive;
        }

        if (binding.invert) {
            value = -value;
        }

        const float magnitude = std::fabs(value);
        if (magnitude > bestMagnitude) {
            bestMagnitude = magnitude;
            resolved = value;
        }
    }
    return (std::clamp)(resolved, -1.0f, 1.0f);
}

float ResolveProfileAxisPreview(const InputManager* inputManager,
                                const InputProfile& profile,
                                std::string_view action,
                                InputDevicePreference preference) {
    if (inputManager == nullptr || profile.id.empty()) {
        return 0.0f;
    }
    return (std::clamp)(inputManager->GetAxisForProfile(profile.id, action, preference), -1.0f, 1.0f);
}

struct LiveInputSample {
    float value{0.0f};
    int matchingBindings{0};
    int matchingDevices{0};
};

float ApplyAxisTuningPreview(float rawValue, const InputBinding& binding) {
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

    value = (std::clamp)(value, -1.0f, 1.0f);
    if (binding.invert) {
        value = -value;
    }

    if (std::fabs(value) < binding.deadzone) {
        return 0.0f;
    }

    const float normalized = (std::fabs(value) - binding.deadzone) / (std::max)(0.0001f, 1.0f - binding.deadzone);
    const float curved = std::pow((std::max)(0.0f, normalized), (std::max)(0.01f, binding.responseExponent));
    return value < 0.0f ? -curved : curved;
}

bool InputDeviceMatchesPreference(InputDeviceType bindingType, InputDevicePreference preference) {
    switch (preference) {
    case InputDevicePreference::Any: return true;
    case InputDevicePreference::Keyboard: return bindingType == InputDeviceType::Keyboard;
    case InputDevicePreference::Gamepad: return bindingType == InputDeviceType::Gamepad;
    case InputDevicePreference::Wheel: return bindingType == InputDeviceType::Wheel;
    case InputDevicePreference::Specific: return true;
    }
    return true;
}

float ResolveBindingAxisPreview(const InputManager* inputManager,
                                const InputBinding& binding,
                                const InputDeviceInfo* device) {
    switch (binding.source) {
    case InputBindingSource::Key:
        return inputManager != nullptr && binding.key >= 0 && inputManager->IsKeyDown(binding.key) ? 1.0f : 0.0f;
    case InputBindingSource::KeyPair: {
        const float negative = inputManager != nullptr && binding.negativeKey >= 0 && inputManager->IsKeyDown(binding.negativeKey) ? -1.0f : 0.0f;
        const float positive = inputManager != nullptr && binding.positiveKey >= 0 && inputManager->IsKeyDown(binding.positiveKey) ? 1.0f : 0.0f;
        return (std::clamp)(negative + positive, -1.0f, 1.0f);
    }
    case InputBindingSource::Axis:
        if (device == nullptr || binding.axis < 0 || binding.axis >= static_cast<int>(device->axes.size())) {
            return 0.0f;
        }
        return ApplyAxisTuningPreview(device->axes[static_cast<std::size_t>(binding.axis)], binding);
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

LiveInputSample ResolveLiveInputSample(const InputManager* inputManager,
                                       const InputProfile& profile,
                                       std::string_view action,
                                       InputDevicePreference preference) {
    LiveInputSample sample;
    if (inputManager == nullptr) {
        return sample;
    }

    float bestMagnitude = 0.0f;
    for (const InputBinding& binding : profile.bindings) {
        if (binding.action != action || !InputDeviceMatchesPreference(binding.deviceType, preference)) {
            continue;
        }
        ++sample.matchingBindings;

        if (binding.deviceType == InputDeviceType::Keyboard) {
            ++sample.matchingDevices;
            const float value = ResolveBindingAxisPreview(inputManager, binding, nullptr);
            const float magnitude = std::fabs(value);
            if (magnitude > bestMagnitude) {
                bestMagnitude = magnitude;
                sample.value = value;
            }
            continue;
        }

        for (const InputDeviceInfo& device : inputManager->GetConnectedDevices()) {
            if (device.type != binding.deviceType) {
                continue;
            }
            ++sample.matchingDevices;
            const float value = ResolveBindingAxisPreview(inputManager, binding, &device);
            const float magnitude = std::fabs(value);
            if (magnitude > bestMagnitude) {
                bestMagnitude = magnitude;
                sample.value = value;
            }
        }
    }

    sample.value = (std::clamp)(sample.value, -1.0f, 1.0f);
    return sample;
}

void RenderSteeringInputMeter(const char* id, float rawValue, float outputValue) {
    const float width = (std::max)(ImGui::GetContentRegionAvail().x, 260.0f);
    const ImVec2 size(width, 90.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    const ImU32 panel = IM_COL32(18, 22, 27, 255);
    const ImU32 rail = IM_COL32(52, 59, 68, 255);
    const ImU32 center = IM_COL32(145, 154, 168, 170);
    const ImU32 rawColor = IM_COL32(80, 155, 255, 210);
    const ImU32 outputColor = IM_COL32(255, 202, 80, 245);
    draw->AddRectFilled(p0, p1, panel, 6.0f);
    draw->AddRect(p0, p1, IM_COL32(84, 94, 108, 165), 6.0f);

    const float left = p0.x + 22.0f;
    const float right = p1.x - 22.0f;
    const float centerX = (left + right) * 0.5f;
    const float railY = p0.y + 48.0f;
    draw->AddRectFilled(ImVec2(left, railY - 5.0f), ImVec2(right, railY + 5.0f), rail, 5.0f);
    draw->AddLine(ImVec2(centerX, p0.y + 22.0f), ImVec2(centerX, p1.y - 18.0f), center, 2.0f);
    for (int tick = -2; tick <= 2; ++tick) {
        const float x = centerX + (right - left) * 0.5f * (static_cast<float>(tick) / 2.0f);
        draw->AddLine(ImVec2(x, railY - 13.0f), ImVec2(x, railY + 13.0f), IM_COL32(97, 106, 119, 130), tick == 0 ? 2.0f : 1.0f);
    }

    const float rawX = centerX + (right - left) * 0.5f * (std::clamp)(rawValue, -1.0f, 1.0f);
    const float outputX = centerX + (right - left) * 0.5f * (std::clamp)(outputValue, -1.0f, 1.0f);
    draw->AddCircleFilled(ImVec2(rawX, railY), 7.0f, rawColor, 24);
    draw->AddRectFilled(ImVec2((std::min)(centerX, outputX), railY + 14.0f),
                        ImVec2((std::max)(centerX, outputX), railY + 22.0f),
                        outputColor,
                        4.0f);
    draw->AddTriangleFilled(ImVec2(outputX, railY + 31.0f),
                            ImVec2(outputX - 8.0f, railY + 43.0f),
                            ImVec2(outputX + 8.0f, railY + 43.0f),
                            outputColor);

    char valueText[96]{};
    std::snprintf(valueText, sizeof(valueText), "STEERING  raw %.2f  output %.2f", rawValue, outputValue);
    draw->AddText(ImVec2(p0.x + 14.0f, p0.y + 10.0f), IM_COL32(218, 225, 235, 245), valueText);
    draw->AddText(ImVec2(left, p1.y - 18.0f), IM_COL32(145, 154, 168, 210), "LEFT");
    const char* rightText = "RIGHT";
    draw->AddText(ImVec2(right - ImGui::CalcTextSize(rightText).x, p1.y - 18.0f), IM_COL32(145, 154, 168, 210), rightText);
}

void RenderPedalInputMeter(const char* id, const char* label, float rawValue, float outputValue, ImU32 color) {
    const ImVec2 size(96.0f, 118.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    draw->AddRectFilled(p0, p1, IM_COL32(18, 22, 27, 255), 6.0f);
    draw->AddRect(p0, p1, IM_COL32(84, 94, 108, 155), 6.0f);

    const float barLeft = p0.x + 30.0f;
    const float barRight = p1.x - 30.0f;
    const float barTop = p0.y + 26.0f;
    const float barBottom = p1.y - 30.0f;
    draw->AddRectFilled(ImVec2(barLeft, barTop), ImVec2(barRight, barBottom), IM_COL32(42, 48, 56, 255), 4.0f);
    const float rawY = barBottom - (barBottom - barTop) * (std::clamp)(rawValue, 0.0f, 1.0f);
    const float outputY = barBottom - (barBottom - barTop) * (std::clamp)(outputValue, 0.0f, 1.0f);
    draw->AddRectFilled(ImVec2(barLeft, outputY), ImVec2(barRight, barBottom), color, 4.0f);
    draw->AddLine(ImVec2(barLeft - 8.0f, rawY), ImVec2(barRight + 8.0f, rawY), IM_COL32(80, 155, 255, 235), 2.0f);

    draw->AddText(ImVec2(p0.x + 10.0f, p0.y + 8.0f), IM_COL32(218, 225, 235, 245), label);
    char valueText[32]{};
    std::snprintf(valueText, sizeof(valueText), "%.2f", outputValue);
    const ImVec2 textSize = ImGui::CalcTextSize(valueText);
    draw->AddText(ImVec2(p0.x + (size.x - textSize.x) * 0.5f, p1.y - 21.0f), IM_COL32(218, 225, 235, 245), valueText);
}

const char* InputDevicePageName(InputDeviceType type) {
    switch (type) {
    case InputDeviceType::Keyboard: return "Keyboard";
    case InputDeviceType::Gamepad: return "Gamepad";
    case InputDeviceType::Wheel: return "Wheel";
    case InputDeviceType::Unknown:
    default: return "Unknown";
    }
}

std::vector<std::string> GatherProfileActions(const InputProfile& profile) {
    std::vector<std::string> actions;
    for (const InputBinding& binding : profile.bindings) {
        if (!binding.action.empty() &&
            std::find(actions.begin(), actions.end(), binding.action) == actions.end()) {
            actions.push_back(binding.action);
        }
    }
    return actions;
}

void EnsureCommonActions(std::vector<std::string>& actions) {
    static const char* defaults[] = {
        "moveX", "moveY", "jump",
        "steer", "throttle", "brake", "handbrake",
        "shiftUp", "shiftDown", "neutral", "reverse"
    };
    for (const char* action : defaults) {
        if (std::find(actions.begin(), actions.end(), action) == actions.end()) {
            actions.push_back(action);
        }
    }
}

void EnsureDefaultWheelSettingsProfiles(std::vector<WheelSettingsProfile>& profiles) {
    if (!profiles.empty()) {
        return;
    }

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
    profiles.push_back(std::move(profile));
}

InputBinding* FindOrCreateWheelBinding(InputProfile& profile, const std::string& action, InputBindingSource source) {
    if (InputBinding* existing = FindBindingForAction(profile, InputDeviceType::Wheel, action)) {
        if (existing->source == InputBindingSource::None) {
            existing->source = source;
        }
        return existing;
    }

    profile.bindings.push_back(InputBinding{action, InputDeviceType::Wheel, source});
    return &profile.bindings.back();
}

void ApplyWheelSettingsToBinding(InputBinding& binding,
                                 bool invert,
                                 float deadzone,
                                 float calibrationMin,
                                 float calibrationCenter,
                                 float calibrationMax,
                                 float responseExponent) {
    binding.invert = invert;
    binding.deadzone = deadzone;
    binding.calibrationMin = calibrationMin;
    binding.calibrationCenter = calibrationCenter;
    binding.calibrationMax = calibrationMax;
    binding.responseExponent = responseExponent;
}

void ApplyWheelSettingsProfileToInputProfile(const WheelSettingsProfile& settings, InputProfile& profile) {
    InputBinding* steer = FindOrCreateWheelBinding(profile, "steer", InputBindingSource::Axis);
    steer->axis = steer->axis < 0 ? 0 : steer->axis;
    ApplyWheelSettingsToBinding(*steer,
                                settings.steeringInvert,
                                settings.steeringDeadzone,
                                settings.steeringCalibrationMin,
                                settings.steeringCalibrationCenter,
                                settings.steeringCalibrationMax,
                                settings.steeringResponseExponent);

    InputBinding* throttle = FindOrCreateWheelBinding(profile, "throttle", InputBindingSource::Axis);
    throttle->axis = throttle->axis < 0 ? 1 : throttle->axis;
    ApplyWheelSettingsToBinding(*throttle,
                                settings.throttleInvert,
                                settings.throttleDeadzone,
                                settings.throttleCalibrationMin,
                                settings.throttleCalibrationCenter,
                                settings.throttleCalibrationMax,
                                settings.throttleResponseExponent);

    InputBinding* brake = FindOrCreateWheelBinding(profile, "brake", InputBindingSource::Axis);
    brake->axis = brake->axis < 0 ? 2 : brake->axis;
    ApplyWheelSettingsToBinding(*brake,
                                settings.brakeInvert,
                                settings.brakeDeadzone,
                                settings.brakeCalibrationMin,
                                settings.brakeCalibrationCenter,
                                settings.brakeCalibrationMax,
                                settings.brakeResponseExponent);

    if (InputBinding* clutch = FindBindingForAction(profile, InputDeviceType::Wheel, "clutch")) {
        ApplyWheelSettingsToBinding(*clutch,
                                    settings.clutchInvert,
                                    settings.clutchDeadzone,
                                    settings.clutchCalibrationMin,
                                    settings.clutchCalibrationCenter,
                                    settings.clutchCalibrationMax,
                                    settings.clutchResponseExponent);
    }
}

const WheelSettingsProfile* FindWheelSettingsForDevice(const std::vector<WheelSettingsProfile>& profiles,
                                                       const InputDeviceInfo& device) {
    for (const WheelSettingsProfile& profile : profiles) {
        if (!profile.deviceNamePattern.empty() &&
            ToLowerCopy(device.displayName).find(ToLowerCopy(profile.deviceNamePattern)) != std::string::npos) {
            return &profile;
        }
    }
    return nullptr;
}

std::string SanitizeFolderName(std::string value) {
    value = TrimCopyLocal(std::move(value));
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            out.push_back(ch);
        }
    }
    return TrimCopyLocal(out);
}

std::string SanitizeAssetBaseName(std::string value) {
    value = TrimCopyLocal(std::move(value));
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            out.push_back(ch == ' ' ? '_' : ch);
        }
    }
    return out.empty() ? std::string("Asset") : out;
}

std::string ProjectFolderDisplayName(const std::string& path) {
    if (NormalizeSlashes(path) == "assets") {
        return "Assets";
    }
    std::string name = fs::path(path).filename().string();
    return name.empty() ? path : name;
}

bool IsDirectChildProjectPath(const std::string& path, const std::string& parent) {
    return NormalizeSlashes(ParentProjectDirectory(path)) == NormalizeSlashes(parent);
}

std::string AssetIconForProjectFile(const std::string& path) {
    const std::string lower = ToLowerCopy(NormalizeSlashes(path));
    const std::string extension = ToLowerCopy(fs::path(path).extension().string());
    if (IsSceneAssetPath(path))        return "asset-scene.png";
    if (IsMaterialAssetPath(path))     return "asset-material.png";
    if (IsShaderGraphAssetPath(path))  return "asset-shader-graph.png";
    if (IsVehicleConfigAssetPath(path))return "asset-vehicle.png";
    if (IsVehicleSoundAssetPath(path)) return "asset-vehicle-sound.png";
    if (IsTrackAssetPath(path))        return "asset-track.png";
    if (IsMeshAssetPath(path))         return "asset-mesh.png";
    if (IsPrefabAssetPath(path))       return "asset-prefab.png";
    if (IsAudioAssetPath(path))        return "asset-audio.png";
    if (extension == ".h" || extension == ".cpp") return "asset-script.png";
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".webp" || extension == ".hdr") return "asset-image.png";
    if (lower.find("/skybox/") != std::string::npos) return "asset-skybox.png";
    return "asset-file.png";
}

std::vector<std::string> BreadcrumbParts(const std::string& directory) {
    std::vector<std::string> parts;
    fs::path path(NormalizeSlashes(directory));
    for (const auto& part : path) {
        parts.push_back(part.string());
    }
    if (parts.empty()) {
        parts.push_back("assets");
    }
    return parts;
}

std::string EllipsizeTextToWidth(const std::string& text, float maxWidth) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }

    constexpr const char* kEllipsis = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;
    if (ellipsisWidth >= maxWidth) {
        return kEllipsis;
    }

    std::string clipped = text;
    while (!clipped.empty() && ImGui::CalcTextSize(clipped.c_str()).x + ellipsisWidth > maxWidth) {
        clipped.pop_back();
    }
    return clipped.empty() ? std::string(kEllipsis) : clipped + kEllipsis;
}

int ProjectDirectoryDepth(const std::string& path) {
    int depth = 0;
    fs::path normalized(NormalizeSlashes(path));
    for (const auto& part : normalized) {
        (void)part;
        ++depth;
    }
    return (std::max)(0, depth - 1);
}

std::string SanitizeImportedMaterialId(std::string value) {
    value = TrimCopyLocal(std::move(value));
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            out.push_back(ch == ' ' ? '_' : static_cast<char>(std::tolower(uch)));
        }
    }
    return out.empty() ? std::string("material") : out;
}

std::string ResolveImportedTextureAssetPath(const std::string& meshAssetPath, const std::string& texturePath) {
    if (meshAssetPath.empty() || texturePath.empty()) {
        return {};
    }

    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path meshAbsolutePath = ProjectAssetPathToAbsolute(meshAssetPath);
    fs::path textureAbsolutePath = fs::path(NormalizeSlashes(texturePath));
    if (!textureAbsolutePath.is_absolute()) {
        textureAbsolutePath = (meshAbsolutePath.parent_path() / textureAbsolutePath).lexically_normal();
    }

    if (!fs::exists(textureAbsolutePath) || !IsUnderPath(textureAbsolutePath, assetsRoot)) {
        return {};
    }

    return ToProjectAssetPath(textureAbsolutePath, assetsRoot);
}

std::string ImportedTextureExtension(const std::string& texturePath, const std::string& embeddedExtension, const std::string& fallback) {
    std::string ext;
    try {
        ext = fs::path(texturePath).extension().string();
        if (!ext.empty() && ext.front() == '.') {
            ext.erase(ext.begin());
        }
    } catch (...) {
        ext.clear();
    }
    if (ext.empty()) {
        ext = embeddedExtension.empty() ? fallback : embeddedExtension;
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (ext == "jpeg") {
        ext = "jpg";
    }
    return ext.empty() ? fallback : ext;
}

std::string FindFallbackImportedTextureAssetPath(const std::string& meshAssetPath,
                                                 const ImportedMeshInfo& info,
                                                 const std::string& slotName) {
    static const std::vector<std::string> imageExtensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds"};
    const std::string modelStem = SanitizeImportedMaterialId(fs::path(meshAssetPath).stem().string());
    const std::string materialStem = SanitizeImportedMaterialId(info.materialName);
    const std::string meshIndexText = std::to_string(info.meshIndex);
    const std::string paddedMeshIndexText = info.meshIndex < 1000
        ? (std::string(3 - (std::min)(3, static_cast<int>(meshIndexText.size())), '0') + meshIndexText)
        : meshIndexText;
    std::vector<std::string> slotAliases;
    if (slotName == "albedo") {
        slotAliases = {"albedo", "diffuse", "diff", "basecolor", "base_color", "color"};
    } else if (slotName == "normal") {
        slotAliases = {"normal", "norm", "nrm"};
    } else if (slotName == "metallic") {
        slotAliases = {"metallic", "metal", "metalness"};
    } else if (slotName == "roughness") {
        slotAliases = {"roughness", "rough"};
    } else if (slotName == "ao") {
        slotAliases = {"ao", "occlusion", "ambientocclusion"};
    } else {
        slotAliases = {slotName};
    }

    auto normalizeKey = [](const std::string& value) {
        std::string key = ToLowerCopy(value);
        key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char ch) {
            return ch == '_' || ch == '-' || ch == ' ' || ch == '.';
        }), key.end());
        return key;
    };

    const std::string normalizedMaterial = normalizeKey(materialStem);
    const std::string normalizedModel = normalizeKey(modelStem);
    const std::string normalizedMeshIndex = normalizeKey(meshIndexText);
    const std::string normalizedPaddedMeshIndex = normalizeKey(paddedMeshIndexText);

    std::vector<fs::path> searchRoots;
    try {
        const fs::path meshAbsolutePath = ProjectAssetPathToAbsolute(meshAssetPath);
        const fs::path meshDir = meshAbsolutePath.parent_path();
        searchRoots.push_back(meshDir);
        searchRoots.push_back(meshDir / "textures");
        searchRoots.push_back(meshDir.parent_path() / "textures");
        searchRoots.push_back(meshDir.parent_path());
    } catch (...) {
        return {};
    }

    std::vector<fs::path> bestMatches;
    for (const fs::path& root : searchRoots) {
        if (root.empty() || !fs::exists(root) || !fs::is_directory(root)) {
            continue;
        }
        try {
            for (const auto& entry : fs::recursive_directory_iterator(root)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                const std::string ext = ToLowerCopy(entry.path().extension().string());
                if (std::find(imageExtensions.begin(), imageExtensions.end(), ext) == imageExtensions.end()) {
                    continue;
                }
                const std::string stem = normalizeKey(entry.path().stem().string());
                bool slotMatch = slotName == "albedo";
                for (const std::string& alias : slotAliases) {
                    if (stem.find(normalizeKey(alias)) != std::string::npos) {
                        slotMatch = true;
                        break;
                    }
                }
                if (!slotMatch) {
                    continue;
                }
                const bool meshIndexMatch =
                    (!normalizedModel.empty() && stem.find(normalizedModel + normalizedMeshIndex) != std::string::npos) ||
                    (!normalizedModel.empty() && stem.find(normalizedModel + normalizedPaddedMeshIndex) != std::string::npos) ||
                    stem.find(normalizedMeshIndex + normalizeKey(slotName)) != std::string::npos ||
                    stem.find(normalizedPaddedMeshIndex + normalizeKey(slotName)) != std::string::npos;
                const bool materialMatch = !normalizedMaterial.empty() && stem.find(normalizedMaterial) != std::string::npos;
                if (meshIndexMatch || materialMatch) {
                    bestMatches.push_back(entry.path());
                }
            }
        } catch (...) {
        }
    }

    if (bestMatches.empty()) {
        return {};
    }
    std::sort(bestMatches.begin(), bestMatches.end(), [](const fs::path& a, const fs::path& b) {
        return ToLowerCopy(a.string()) < ToLowerCopy(b.string());
    });
    try {
        return ToProjectAssetPath(bestMatches.front(), FindAssetsRoot());
    } catch (...) {
        return {};
    }
}

std::string ResolveOrFallbackImportedTextureAssetPath(const std::string& meshAssetPath,
                                                      const ImportedMeshInfo& info,
                                                      const std::string& texturePath,
                                                      const std::string& slotName) {
    std::string resolved = ResolveImportedTextureAssetPath(meshAssetPath, texturePath);
    if (!resolved.empty()) {
        return resolved;
    }
    return FindFallbackImportedTextureAssetPath(meshAssetPath, info, slotName);
}

std::string ExtractOrResolveImportedTextureAssetPath(const std::string& meshAssetPath,
                                                     const ImportedMeshInfo& info,
                                                     const std::string& texturePath,
                                                     const std::vector<unsigned char>& embeddedData,
                                                     const std::string& embeddedExtension,
                                                     const fs::path& targetFolder,
                                                     const std::string& materialId,
                                                     const std::string& slotName) {
    if (texturePath.empty() && embeddedData.empty()) {
        return FindFallbackImportedTextureAssetPath(meshAssetPath, info, slotName);
    }

    if (!embeddedData.empty()) {
        if (targetFolder.empty()) {
            return {};
        }
        const std::string extension = ImportedTextureExtension(texturePath, embeddedExtension, "png");
        if (extension == "rgba") {
            return {};
        }
        const fs::path outputPath = (targetFolder / (materialId + "_" + slotName + "." + extension)).lexically_normal();
        try {
            fs::create_directories(outputPath.parent_path());
            std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
            if (!out.good()) {
                return {};
            }
            out.write(reinterpret_cast<const char*>(embeddedData.data()), static_cast<std::streamsize>(embeddedData.size()));
            out.close();
            return ToProjectAssetPath(outputPath, FindAssetsRoot());
        } catch (...) {
            return {};
        }
    }

    std::string resolvedProjectPath = ResolveImportedTextureAssetPath(meshAssetPath, texturePath);
    if (!resolvedProjectPath.empty()) {
        return resolvedProjectPath;
    }
    if (targetFolder.empty()) {
        return {};
    }

    try {
        const fs::path assetsRoot = FindAssetsRoot();
        const fs::path meshAbsolutePath = ProjectAssetPathToAbsolute(meshAssetPath);
        fs::path textureAbsolutePath = fs::path(NormalizeSlashes(texturePath));
        if (!textureAbsolutePath.is_absolute()) {
            textureAbsolutePath = (meshAbsolutePath.parent_path() / textureAbsolutePath).lexically_normal();
        }
        if (!fs::exists(textureAbsolutePath) || !fs::is_regular_file(textureAbsolutePath)) {
            return {};
        }
        const std::string extension = ImportedTextureExtension(textureAbsolutePath.string(), embeddedExtension, "png");
        const fs::path outputPath = (targetFolder / (materialId + "_" + slotName + "." + extension)).lexically_normal();
        fs::create_directories(outputPath.parent_path());
        fs::copy_file(textureAbsolutePath, outputPath, fs::copy_options::overwrite_existing);
        return ToProjectAssetPath(outputPath, assetsRoot);
    } catch (...) {
        return {};
    }
}

struct ModelMaterialMapping {
    int meshIndex{0};
    std::string meshName;
    std::string importedMaterialName;
    std::string materialId;
};

struct ModelCollisionMapping {
    int meshIndex{0};
    std::string meshName;
    int mode{1};
    int buildQuality{2};
    bool autoBake{true};
};

struct ModelImportSettings {
    int version{1};
    int pivotMode{0};
    bool importMaterials{true};
    int materialLocation{0};
    int materialRemapNaming{0};
    int materialRemapSearch{0};
    std::vector<ModelMaterialMapping> materials;
    std::vector<ModelCollisionMapping> collisions;
};

std::string DefaultImportedMaterialId(const std::string& meshAssetPath,
                                      const std::string& packageBaseName,
                                      const ImportedMeshInfo& info) {
    const std::string baseId = SanitizeImportedMaterialId(packageBaseName);
    const std::string materialPart = SanitizeImportedMaterialId(
        info.materialName.empty() ? fs::path(meshAssetPath).stem().string() : info.materialName);
    return baseId + "_" + materialPart;
}

void ConfigureImportedMaterialRenderMode(Material& material, const ImportedMeshInfo& info) {
    const std::string alphaMode = ToLowerCopy(info.materialAlphaMode);
    const bool blend = alphaMode == "blend" || info.materialOpacity < 0.999f;
    const bool mask = alphaMode == "mask";

    material.shader = blend ? "transparent" : "pbr";
    if (blend) {
        material.albedoColor[3] = (std::max)(0.0f, (std::min)(1.0f, info.materialOpacity));
    }

    if (mask) {
        MaterialPropertyValue cutoff;
        cutoff.type = MaterialPropertyType::Float;
        cutoff.values[0] = info.materialAlphaCutoff > 0.0f ? info.materialAlphaCutoff : 0.5f;
        material.properties["alphaCutoff"] = cutoff;
    }
}

std::string CreateOrUpdateImportedMaterialAsset(MaterialManager& materialManager,
                                                const std::string& meshAssetPath,
                                                const std::string& packageBaseName,
                                                const ImportedMeshInfo& info,
                                                bool overwriteExisting) {
    const std::string materialId = DefaultImportedMaterialId(meshAssetPath, packageBaseName, info);
    if (!overwriteExisting && materialManager.Exists(materialId)) {
        return materialId;
    }
    Material material;
    material.name = info.materialName.empty() ? materialId : info.materialName;
    material.shader = "pbr";
    material.texAlbedo = ResolveOrFallbackImportedTextureAssetPath(meshAssetPath, info, info.diffuseTexturePath, "albedo");
    material.texNormal = ResolveOrFallbackImportedTextureAssetPath(meshAssetPath, info, info.normalTexturePath, "normal");
    material.texMetallic = ResolveOrFallbackImportedTextureAssetPath(meshAssetPath, info, info.metallicTexturePath, "metallic");
    material.texRoughness = ResolveOrFallbackImportedTextureAssetPath(meshAssetPath, info, info.roughnessTexturePath, "roughness");
    material.texAo = ResolveOrFallbackImportedTextureAssetPath(meshAssetPath, info, info.aoTexturePath, "ao");
    ConfigureImportedMaterialRenderMode(material, info);
    materialManager.Save(materialId, material);
    return materialId;
}

bool SaveImportedMaterialAssetToFolder(const std::string& materialId,
                                       const Material& material,
                                       const fs::path& folderAbsolutePath,
                                       bool overwriteExisting) {
    if (materialId.empty() || folderAbsolutePath.empty()) {
        return false;
    }

    const fs::path materialPath = (folderAbsolutePath / (materialId + ".mat.json")).lexically_normal();
    if (!overwriteExisting && fs::exists(materialPath)) {
        return true;
    }

    try {
        fs::create_directories(materialPath.parent_path());
    } catch (...) {
        return false;
    }

    std::ofstream out(materialPath, std::ios::trunc);
    if (!out.good()) {
        return false;
    }

    const std::string shaderId = ShaderRegistry::NormalizeShaderId(material.shader.empty() ? std::string("pbr") : material.shader);
    const std::string savedShader = (ShaderRegistry::IsKnownShader(shaderId) || ShaderRegistry::IsGraphShaderId(shaderId)) ? shaderId : std::string("pbr");
    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"name\": \"" << JsonEscape(material.name.empty() ? materialId : material.name) << "\",\n";
    out << "  \"shader\": \"" << JsonEscape(savedShader) << "\",\n";
    out << "  \"albedoColor\": [" << material.albedoColor[0] << ", " << material.albedoColor[1] << ", " << material.albedoColor[2] << ", " << material.albedoColor[3] << "],\n";
    out << "  \"metallic\": " << material.metallic << ",\n";
    out << "  \"roughness\": " << material.roughness << ",\n";
    out << "  \"emissiveColor\": [" << material.emissiveColor[0] << ", " << material.emissiveColor[1] << ", " << material.emissiveColor[2] << "],\n";
    out << "  \"uvTiling\": [" << material.uvTiling[0] << ", " << material.uvTiling[1] << "],\n";
    out << "  \"uvOffset\": [" << material.uvOffset[0] << ", " << material.uvOffset[1] << "],\n";
    out << "  \"textures\": {\n";
    out << "    \"albedo\": \"" << JsonEscape(material.texAlbedo) << "\",\n";
    out << "    \"normal\": \"" << JsonEscape(material.texNormal) << "\",\n";
    out << "    \"metallic\": \"" << JsonEscape(material.texMetallic) << "\",\n";
    out << "    \"roughness\": \"" << JsonEscape(material.texRoughness) << "\",\n";
    out << "    \"ao\": \"" << JsonEscape(material.texAo) << "\"\n";
    out << "  },\n";
    out << "  \"properties\": {\n";
    std::size_t propertyIndex = 0;
    for (const auto& entry : material.properties) {
        const MaterialPropertyValue& property = entry.second;
        out << "    \"" << JsonEscape(entry.first) << "\": { \"type\": \"";
        switch (property.type) {
        case MaterialPropertyType::Float: out << "float"; break;
        case MaterialPropertyType::Vec2: out << "vec2"; break;
        case MaterialPropertyType::Vec3: out << "vec3"; break;
        case MaterialPropertyType::Vec4: out << "vec4"; break;
        case MaterialPropertyType::Bool: out << "bool"; break;
        case MaterialPropertyType::Texture2D: out << "texture2D"; break;
        }
        out << "\", \"value\": ";
        if (property.type == MaterialPropertyType::Bool) {
            out << (property.boolValue ? "true" : "false");
        } else if (property.type == MaterialPropertyType::Texture2D) {
            out << "\"" << JsonEscape(property.texturePath) << "\"";
        } else if (property.type == MaterialPropertyType::Float) {
            out << property.values[0];
        } else {
            const int count = property.type == MaterialPropertyType::Vec2 ? 2 : (property.type == MaterialPropertyType::Vec3 ? 3 : 4);
            out << "[";
            for (int i = 0; i < count; ++i) {
                if (i > 0) out << ", ";
                out << property.values[static_cast<std::size_t>(i)];
            }
            out << "]";
        }
        out << " }";
        if (++propertyIndex < material.properties.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  }\n";
    out << "}\n";
    return true;
}

std::string CreateOrUpdateImportedMaterialAssetAtFolder(const std::string& meshAssetPath,
                                                        const std::string& packageBaseName,
                                                        const ImportedMeshInfo& info,
                                                        const fs::path& folderAbsolutePath,
                                                        bool overwriteExisting) {
    const std::string materialId = DefaultImportedMaterialId(meshAssetPath, packageBaseName, info);
    Material material;
    material.name = info.materialName.empty() ? materialId : info.materialName;
    material.shader = "pbr";
    material.texAlbedo = ExtractOrResolveImportedTextureAssetPath(meshAssetPath, info, info.diffuseTexturePath, info.diffuseTextureEmbeddedData, info.diffuseTextureEmbeddedExtension, folderAbsolutePath, materialId, "albedo");
    material.texNormal = ExtractOrResolveImportedTextureAssetPath(meshAssetPath, info, info.normalTexturePath, info.normalTextureEmbeddedData, info.normalTextureEmbeddedExtension, folderAbsolutePath, materialId, "normal");
    material.texMetallic = ExtractOrResolveImportedTextureAssetPath(meshAssetPath, info, info.metallicTexturePath, info.metallicTextureEmbeddedData, info.metallicTextureEmbeddedExtension, folderAbsolutePath, materialId, "metallic");
    material.texRoughness = ExtractOrResolveImportedTextureAssetPath(meshAssetPath, info, info.roughnessTexturePath, info.roughnessTextureEmbeddedData, info.roughnessTextureEmbeddedExtension, folderAbsolutePath, materialId, "roughness");
    material.texAo = ExtractOrResolveImportedTextureAssetPath(meshAssetPath, info, info.aoTexturePath, info.aoTextureEmbeddedData, info.aoTextureEmbeddedExtension, folderAbsolutePath, materialId, "ao");
    ConfigureImportedMaterialRenderMode(material, info);
    SaveImportedMaterialAssetToFolder(materialId, material, folderAbsolutePath, overwriteExisting);
    return materialId;
}

fs::path ModelImportSettingsAbsolutePath(const std::string& meshAssetPath) {
    return fs::path(ProjectAssetPathToAbsolute(meshAssetPath).string() + ".import.json").lexically_normal();
}

bool IsModelImportSettingsPath(const std::string& path) {
    return EndsWith(ToLowerCopy(NormalizeSlashes(path)), ".import.json");
}

bool LoadModelImportSettings(const std::string& meshAssetPath, ModelImportSettings& settings) {
    settings = ModelImportSettings{};
    const fs::path settingsPath = ModelImportSettingsAbsolutePath(meshAssetPath);
    if (!fs::exists(settingsPath)) {
        return false;
    }

    std::ifstream in(settingsPath);
    if (!in.good()) {
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    try {
        using namespace raceman::physics::json;
        const Value root = parse(buffer.str());
        if (!root.is_object()) {
            return false;
        }
        const auto& object = root.as_object();
        if (auto it = object.find("version"); it != object.end() && it->second.is_number()) {
            settings.version = static_cast<int>(it->second.as_number());
        }
        if (auto it = object.find("pivotMode"); it != object.end() && it->second.is_number()) {
            settings.pivotMode = static_cast<int>(it->second.as_number());
        }
        if (auto it = object.find("importMaterials"); it != object.end() && it->second.is_bool()) {
            settings.importMaterials = it->second.as_bool();
        }
        if (auto it = object.find("materialLocation"); it != object.end() && it->second.is_number()) {
            settings.materialLocation = static_cast<int>(it->second.as_number());
        }
        if (auto it = object.find("materialRemapNaming"); it != object.end() && it->second.is_number()) {
            settings.materialRemapNaming = static_cast<int>(it->second.as_number());
        }
        if (auto it = object.find("materialRemapSearch"); it != object.end() && it->second.is_number()) {
            settings.materialRemapSearch = static_cast<int>(it->second.as_number());
        }
        if (auto it = object.find("materials"); it != object.end() && it->second.is_array()) {
            for (const Value& value : it->second.as_array()) {
                if (!value.is_object()) {
                    continue;
                }
                const auto& materialObject = value.as_object();
                ModelMaterialMapping mapping;
                if (auto mi = materialObject.find("meshIndex"); mi != materialObject.end() && mi->second.is_number()) {
                    mapping.meshIndex = static_cast<int>(mi->second.as_number());
                }
                if (auto name = materialObject.find("meshName"); name != materialObject.end() && name->second.is_string()) {
                    mapping.meshName = name->second.as_string();
                }
                if (auto imported = materialObject.find("importedMaterialName"); imported != materialObject.end() && imported->second.is_string()) {
                    mapping.importedMaterialName = imported->second.as_string();
                }
                if (auto mat = materialObject.find("materialId"); mat != materialObject.end() && mat->second.is_string()) {
                    mapping.materialId = mat->second.as_string();
                }
                settings.materials.push_back(std::move(mapping));
            }
        }
        if (auto it = object.find("collisions"); it != object.end() && it->second.is_array()) {
            for (const Value& value : it->second.as_array()) {
                if (!value.is_object()) {
                    continue;
                }
                const auto& collisionObject = value.as_object();
                ModelCollisionMapping mapping;
                if (auto mi = collisionObject.find("meshIndex"); mi != collisionObject.end() && mi->second.is_number()) {
                    mapping.meshIndex = static_cast<int>(mi->second.as_number());
                }
                if (auto name = collisionObject.find("meshName"); name != collisionObject.end() && name->second.is_string()) {
                    mapping.meshName = name->second.as_string();
                }
                if (auto mode = collisionObject.find("mode"); mode != collisionObject.end() && mode->second.is_number()) {
                    mapping.mode = static_cast<int>(mode->second.as_number());
                }
                if (auto quality = collisionObject.find("buildQuality"); quality != collisionObject.end() && quality->second.is_number()) {
                    mapping.buildQuality = static_cast<int>(quality->second.as_number());
                }
                if (auto autoBake = collisionObject.find("autoBake"); autoBake != collisionObject.end() && autoBake->second.is_bool()) {
                    mapping.autoBake = autoBake->second.as_bool();
                }
                settings.collisions.push_back(std::move(mapping));
            }
        }
        return true;
    } catch (...) {
        settings = ModelImportSettings{};
        return false;
    }
}

bool SaveModelImportSettings(const std::string& meshAssetPath, const ModelImportSettings& settings) {
    const fs::path settingsPath = ModelImportSettingsAbsolutePath(meshAssetPath);
    try {
        fs::create_directories(settingsPath.parent_path());
    } catch (...) {
        return false;
    }

    std::ofstream out(settingsPath, std::ios::trunc);
    if (!out.good()) {
        return false;
    }
    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"pivotMode\": " << settings.pivotMode << ",\n";
    out << "  \"importMaterials\": " << (settings.importMaterials ? "true" : "false") << ",\n";
    out << "  \"materialLocation\": " << settings.materialLocation << ",\n";
    out << "  \"materialRemapNaming\": " << settings.materialRemapNaming << ",\n";
    out << "  \"materialRemapSearch\": " << settings.materialRemapSearch << ",\n";
    out << "  \"materials\": [\n";
    for (std::size_t i = 0; i < settings.materials.size(); ++i) {
        const ModelMaterialMapping& mapping = settings.materials[i];
        out << "    {\n";
        out << "      \"meshIndex\": " << mapping.meshIndex << ",\n";
        out << "      \"meshName\": \"" << JsonEscape(mapping.meshName) << "\",\n";
        out << "      \"importedMaterialName\": \"" << JsonEscape(mapping.importedMaterialName) << "\",\n";
        out << "      \"materialId\": \"" << JsonEscape(mapping.materialId) << "\"\n";
        out << "    }" << (i + 1 < settings.materials.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"collisions\": [\n";
    for (std::size_t i = 0; i < settings.collisions.size(); ++i) {
        const ModelCollisionMapping& mapping = settings.collisions[i];
        out << "    {\n";
        out << "      \"meshIndex\": " << mapping.meshIndex << ",\n";
        out << "      \"meshName\": \"" << JsonEscape(mapping.meshName) << "\",\n";
        out << "      \"mode\": " << mapping.mode << ",\n";
        out << "      \"buildQuality\": " << mapping.buildQuality << ",\n";
        out << "      \"autoBake\": " << (mapping.autoBake ? "true" : "false") << "\n";
        out << "    }" << (i + 1 < settings.collisions.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

void MoveModelImportSettingsFile(const std::string& oldMeshPath, const std::string& newMeshPath) {
    if (!IsMeshAssetPath(oldMeshPath) || !IsMeshAssetPath(newMeshPath)) {
        return;
    }
    const fs::path oldSettings = ModelImportSettingsAbsolutePath(oldMeshPath);
    const fs::path newSettings = ModelImportSettingsAbsolutePath(newMeshPath);
    if (!fs::exists(oldSettings)) {
        return;
    }
    try {
        fs::create_directories(newSettings.parent_path());
        if (fs::exists(newSettings)) {
            fs::remove(newSettings);
        }
        fs::rename(oldSettings, newSettings);
    } catch (...) {
    }
}

void CopyModelImportSettingsFile(const std::string& sourceMeshPath, const std::string& destMeshPath) {
    if (!IsMeshAssetPath(sourceMeshPath) || !IsMeshAssetPath(destMeshPath)) {
        return;
    }
    const fs::path sourceSettings = ModelImportSettingsAbsolutePath(sourceMeshPath);
    const fs::path destSettings = ModelImportSettingsAbsolutePath(destMeshPath);
    if (!fs::exists(sourceSettings)) {
        return;
    }
    try {
        fs::create_directories(destSettings.parent_path());
        fs::copy_file(sourceSettings, destSettings, fs::copy_options::overwrite_existing);
    } catch (...) {
    }
}

void DeleteModelImportSettingsFile(const std::string& meshPath) {
    if (!IsMeshAssetPath(meshPath)) {
        return;
    }
    const fs::path settingsPath = ModelImportSettingsAbsolutePath(meshPath);
    try {
        if (fs::exists(settingsPath)) {
            fs::remove(settingsPath);
        }
    } catch (...) {
    }
}

ModelMaterialMapping* FindModelMaterialMapping(ModelImportSettings& settings, int meshIndex) {
    for (ModelMaterialMapping& mapping : settings.materials) {
        if (mapping.meshIndex == meshIndex) {
            return &mapping;
        }
    }
    return nullptr;
}

const ModelMaterialMapping* FindModelMaterialMapping(const ModelImportSettings& settings, int meshIndex) {
    for (const ModelMaterialMapping& mapping : settings.materials) {
        if (mapping.meshIndex == meshIndex) {
            return &mapping;
        }
    }
    return nullptr;
}

ModelCollisionMapping* FindModelCollisionMapping(ModelImportSettings& settings, int meshIndex) {
    for (ModelCollisionMapping& mapping : settings.collisions) {
        if (mapping.meshIndex == meshIndex) {
            return &mapping;
        }
    }
    return nullptr;
}

const ModelCollisionMapping* FindModelCollisionMapping(const ModelImportSettings& settings, int meshIndex) {
    for (const ModelCollisionMapping& mapping : settings.collisions) {
        if (mapping.meshIndex == meshIndex) {
            return &mapping;
        }
    }
    return nullptr;
}

ModelMaterialMapping& EnsureModelMaterialMapping(ModelImportSettings& settings,
                                                 const std::string& meshAssetPath,
                                                 const std::string& packageBaseName,
                                                 const ImportedMeshInfo& info) {
    const int meshIndex = static_cast<int>(info.meshIndex);
    if (ModelMaterialMapping* existing = FindModelMaterialMapping(settings, meshIndex)) {
        existing->meshName = info.meshName;
        existing->importedMaterialName = info.materialName;
        if (existing->materialId.empty()) {
            existing->materialId = DefaultImportedMaterialId(meshAssetPath, packageBaseName, info);
        }
        return *existing;
    }

    ModelMaterialMapping mapping;
    mapping.meshIndex = meshIndex;
    mapping.meshName = info.meshName;
    mapping.importedMaterialName = info.materialName;
    mapping.materialId = DefaultImportedMaterialId(meshAssetPath, packageBaseName, info);
    settings.materials.push_back(std::move(mapping));
    return settings.materials.back();
}

ModelCollisionMapping& EnsureModelCollisionMapping(ModelImportSettings& settings,
                                                   const ImportedMeshInfo& info) {
    const int meshIndex = static_cast<int>(info.meshIndex);
    if (ModelCollisionMapping* existing = FindModelCollisionMapping(settings, meshIndex)) {
        existing->meshName = info.meshName;
        existing->mode = (std::max)(0, (std::min)(2, existing->mode));
        existing->buildQuality = (std::max)(0, (std::min)(2, existing->buildQuality));
        return *existing;
    }

    ModelCollisionMapping mapping;
    mapping.meshIndex = meshIndex;
    mapping.meshName = info.meshName;
    settings.collisions.push_back(std::move(mapping));
    return settings.collisions.back();
}

void ReconcileModelImportSettings(ModelImportSettings& settings,
                                  const std::string& meshAssetPath,
                                  const std::string& packageBaseName,
                                  const std::vector<ImportedMeshInfo>& infos) {
    std::vector<ModelMaterialMapping> reconciled;
    reconciled.reserve(infos.size());
    for (const ImportedMeshInfo& info : infos) {
        const int meshIndex = static_cast<int>(info.meshIndex);
        ModelMaterialMapping mapping;
        if (const ModelMaterialMapping* existing = FindModelMaterialMapping(settings, meshIndex)) {
            mapping = *existing;
        } else {
            mapping.materialId = DefaultImportedMaterialId(meshAssetPath, packageBaseName, info);
        }
        mapping.meshIndex = meshIndex;
        mapping.meshName = info.meshName;
        mapping.importedMaterialName = info.materialName;
        if (mapping.materialId.empty()) {
            mapping.materialId = DefaultImportedMaterialId(meshAssetPath, packageBaseName, info);
        }
        reconciled.push_back(std::move(mapping));
    }
    settings.materials = std::move(reconciled);

    std::vector<ModelCollisionMapping> collisionSettings;
    collisionSettings.reserve(infos.size());
    for (const ImportedMeshInfo& info : infos) {
        const int meshIndex = static_cast<int>(info.meshIndex);
        ModelCollisionMapping mapping;
        if (const ModelCollisionMapping* existing = FindModelCollisionMapping(settings, meshIndex)) {
            mapping = *existing;
        }
        mapping.meshIndex = meshIndex;
        mapping.meshName = info.meshName;
        mapping.mode = (std::max)(0, (std::min)(2, mapping.mode));
        mapping.buildQuality = (std::max)(0, (std::min)(2, mapping.buildQuality));
        collisionSettings.push_back(std::move(mapping));
    }
    settings.collisions = std::move(collisionSettings);
}

MeshColliderMode CollisionMappingModeToMeshMode(const ModelCollisionMapping& mapping) {
    return mapping.mode == 2 ? MeshColliderMode::ConvexHull : MeshColliderMode::TriangleMesh;
}

MeshColliderBuildQuality CollisionMappingQualityToBuildQuality(const ModelCollisionMapping& mapping) {
    if (mapping.buildQuality == 0) return MeshColliderBuildQuality::BuildSpeed;
    if (mapping.buildQuality == 1) return MeshColliderBuildQuality::Balanced;
    return MeshColliderBuildQuality::BuildQuality;
}

PhysicsColliderDesc BuildModelCollisionDesc(const std::string& importPath,
                                            const ImportedMeshInfo& info,
                                            const ModelCollisionMapping& mapping) {
    PhysicsColliderDesc collider;
    collider.type = PhysicsColliderType::Mesh;
    collider.meshAssetPath = importPath;
    collider.meshName = info.meshName;
    collider.meshIndex = static_cast<int>(info.meshIndex);
    collider.meshPivotOffset = glm::vec3{0.0f};
    collider.meshMode = CollisionMappingModeToMeshMode(mapping);
    collider.meshBuildQuality = CollisionMappingQualityToBuildQuality(mapping);
    return collider;
}

void ApplyModelCollisionMappingToSceneObject(SceneObject& object, const ModelCollisionMapping& mapping) {
    if (mapping.mode == 0) {
        object.hasMeshCollider = false;
        object.meshCollider = MeshColliderComponent{};
        return;
    }
    SetActiveColliderType(object, SceneColliderType::Mesh);
    object.meshCollider.enabled = true;
    object.meshCollider.isTrigger = false;
    object.meshCollider.mode = CollisionMappingModeToMeshMode(mapping);
    object.meshCollider.buildQuality = CollisionMappingQualityToBuildQuality(mapping);
}

const char* CollisionCacheStatusLabel(CollisionShapeCacheStatus status) {
    switch (status) {
    case CollisionShapeCacheStatus::Ready: return "Ready";
    case CollisionShapeCacheStatus::Stale: return "Stale";
    case CollisionShapeCacheStatus::Failed: return "Failed";
    case CollisionShapeCacheStatus::Missing:
    default: return "Missing";
    }
}

std::string NormalizedImportMatchKey(const std::string& value) {
    std::string key = ToLowerCopy(fs::path(NormalizeSlashes(value)).stem().string());
    key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char ch) {
        return ch == '_' || ch == '-' || ch == ' ' || ch == '.';
    }), key.end());
    return key;
}

std::string ImportedSlotMatchKey(const ImportedMeshInfo& info, int namingMode) {
    if (namingMode == 0) {
        std::string key = NormalizedImportMatchKey(info.diffuseTexturePath);
        if (!key.empty()) {
            return key;
        }
    }
    std::string key = NormalizedImportMatchKey(info.materialName);
    if (!key.empty()) {
        return key;
    }
    return NormalizedImportMatchKey(info.meshName);
}

int ImportedMaterialMatchScore(const Material& material,
                               const std::string& materialId,
                               const ImportedMeshInfo& info,
                               int namingMode) {
    const std::string target = ImportedSlotMatchKey(info, namingMode);
    if (target.empty()) {
        return 0;
    }

    std::vector<std::string> candidates;
    candidates.push_back(NormalizedImportMatchKey(materialId));
    candidates.push_back(NormalizedImportMatchKey(material.name));
    candidates.push_back(NormalizedImportMatchKey(material.texAlbedo));
    candidates.push_back(NormalizedImportMatchKey(material.texNormal));

    int bestScore = 0;
    for (const std::string& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        if (candidate == target) {
            bestScore = bestScore > 100 ? bestScore : 100;
        } else if (candidate.find(target) != std::string::npos || target.find(candidate) != std::string::npos) {
            bestScore = bestScore > 70 ? bestScore : 70;
        }
    }
    return bestScore;
}

std::string FindBestExistingMaterialForImportedSlot(MaterialManager& materialManager,
                                                    const ImportedMeshInfo& info,
                                                    int namingMode) {
    std::vector<std::string> materialIds = materialManager.ListMaterialIds();
    std::sort(materialIds.begin(), materialIds.end(), [](const std::string& a, const std::string& b) {
        return ToLowerCopy(a) < ToLowerCopy(b);
    });

    std::string bestId;
    int bestScore = 0;
    for (const std::string& materialId : materialIds) {
        const Material* material = materialManager.Get(materialId);
        if (material == nullptr) {
            continue;
        }
        const int score = ImportedMaterialMatchScore(*material, materialId, info, namingMode);
        if (score > bestScore) {
            bestScore = score;
            bestId = materialId;
        }
    }
    return bestScore >= 70 ? bestId : std::string{};
}

} // namespace

bool SceneEditor::RefreshModelAssetInspectorCache(bool forceReload) {
    const std::string selectedPath = NormalizeSlashes(selectedProjectFile_);
    if (!forceReload && modelAssetInspectorCache_.selectedPath == selectedPath) {
        return modelAssetInspectorCache_.loaded && modelAssetInspectorCache_.error.empty();
    }

    modelAssetInspectorCache_ = ModelAssetInspectorCache{};
    modelAssetInspectorCache_.selectedPath = selectedPath;
    if (!IsMeshAssetPath(selectedPath)) {
        modelAssetInspectorCache_.error = "No model asset selected.";
        return false;
    }

    if (!TryLoadMeshAsset(selectedPath,
                          modelAssetInspectorCache_.importPath,
                          modelAssetInspectorCache_.model,
                          modelAssetInspectorCache_.infos)) {
        modelAssetInspectorCache_.error = "Failed to load model asset.";
        return false;
    }

    modelAssetInspectorCache_.loaded = true;
    return true;
}

void SceneEditor::RenderModelAssetInspector() {
    if (!IsMeshAssetPath(selectedProjectFile_)) {
        ImGui::TextDisabled("No model asset selected.");
        return;
    }

    if (!RefreshModelAssetInspectorCache(false)) {
        ImGui::TextUnformatted("Model");
        ImGui::Separator();
        const std::string& error = modelAssetInspectorCache_.error.empty()
            ? std::string("Failed to load model asset.")
            : modelAssetInspectorCache_.error;
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", error.c_str());
        ImGui::TextWrapped("%s", selectedProjectFile_.c_str());
        if (ImGui::Button("Retry")) {
            RefreshModelAssetInspectorCache(true);
        }
        return;
    }

    const std::string& importPath = modelAssetInspectorCache_.importPath;
    const std::shared_ptr<::Model>& model = modelAssetInspectorCache_.model;
    const std::vector<ImportedMeshInfo>& infos = modelAssetInspectorCache_.infos;

    std::string baseName;
    try {
        baseName = fs::path(importPath).stem().string();
    } catch (...) {
        baseName = "Model";
    }

    ModelImportSettings settings;
    LoadModelImportSettings(importPath, settings);
    ReconcileModelImportSettings(settings, importPath, baseName, infos);

    auto chooseMaterialExtractFolder = [&]() -> fs::path {
        const std::string folder = PickFolderDialog(L"Select Material Extract Folder");
        if (folder.empty()) {
            return {};
        }
        fs::path folderPath = fs::path(NormalizeSlashes(folder)).lexically_normal();
        try {
            const fs::path assetsRoot = FindAssetsRoot();
            if (!IsUnderPath(folderPath, assetsRoot)) {
                if (console_) {
                    console_->AddError("Choose a folder inside the project's assets folder.");
                }
                return {};
            }
        } catch (...) {
            return {};
        }
        return folderPath;
    };

    auto extractMaterials = [&](bool overwriteExisting, const fs::path& targetFolder) {
        bool changed = false;
        for (const ImportedMeshInfo& info : infos) {
            ModelMaterialMapping& mapping = EnsureModelMaterialMapping(settings, importPath, baseName, info);
            const bool missing = mapping.materialId.empty() || !materialManager_.Exists(mapping.materialId);
            if (overwriteExisting || missing) {
                if (!targetFolder.empty()) {
                    mapping.materialId = CreateOrUpdateImportedMaterialAssetAtFolder(importPath, baseName, info, targetFolder, overwriteExisting);
                } else {
                    mapping.materialId = CreateOrUpdateImportedMaterialAsset(materialManager_, importPath, baseName, info, overwriteExisting);
                }
                changed = true;
            }
        }
        if (SaveModelImportSettings(importPath, settings)) {
            changed = true;
        }
        if (changed) {
            materialManager_.LoadAll();
            RefreshProjectFiles();
            if (console_) {
                console_->AddLog(std::string(overwriteExisting ? "Re-extracted" : "Extracted") + " model materials: " + importPath);
            }
        }
    };

    auto extractTextures = [&](const fs::path& targetFolder) {
        if (targetFolder.empty()) {
            return;
        }
        int extractedCount = 0;
        for (const ImportedMeshInfo& info : infos) {
            ModelMaterialMapping& mapping = EnsureModelMaterialMapping(settings, importPath, baseName, info);
            if (mapping.materialId.empty()) {
                mapping.materialId = DefaultImportedMaterialId(importPath, baseName, info);
            }
            auto extractSlot = [&](const std::string& texturePath,
                                   const std::vector<unsigned char>& embeddedData,
                                   const std::string& embeddedExtension,
                                   const std::string& slotName) {
                const std::string projectPath = ExtractOrResolveImportedTextureAssetPath(importPath, info, texturePath, embeddedData, embeddedExtension, targetFolder, mapping.materialId, slotName);
                if (!projectPath.empty()) {
                    ++extractedCount;
                }
            };
            extractSlot(info.diffuseTexturePath, info.diffuseTextureEmbeddedData, info.diffuseTextureEmbeddedExtension, "albedo");
            extractSlot(info.normalTexturePath, info.normalTextureEmbeddedData, info.normalTextureEmbeddedExtension, "normal");
            extractSlot(info.metallicTexturePath, info.metallicTextureEmbeddedData, info.metallicTextureEmbeddedExtension, "metallic");
            extractSlot(info.roughnessTexturePath, info.roughnessTextureEmbeddedData, info.roughnessTextureEmbeddedExtension, "roughness");
            extractSlot(info.aoTexturePath, info.aoTextureEmbeddedData, info.aoTextureEmbeddedExtension, "ao");
        }
        RefreshProjectFiles();
        if (console_) {
            console_->AddLog("Extracted or resolved " + std::to_string(extractedCount) + " imported texture slot" + (extractedCount == 1 ? "" : "s") + ".");
        }
    };

    auto applyMaterialsToScene = [&]() {
        int changedCount = 0;
        bool undoPushed = false;
        const std::string normalizedImportPath = NormalizeSlashes(importPath);
        for (SceneObject& object : objects_) {
            if (!object.hasMeshFilter || !object.hasMeshRenderer) {
                continue;
            }
            if (NormalizeSlashes(object.meshFilter.sourcePath) != normalizedImportPath) {
                continue;
            }
            const ModelMaterialMapping* mapping = FindModelMaterialMapping(settings, object.meshFilter.meshIndex);
            if (mapping == nullptr || mapping->materialId.empty() || object.meshRenderer.materialId == mapping->materialId) {
                continue;
            }
            if (!undoPushed) {
                PushUndoState();
                undoPushed = true;
            }
            object.meshRenderer.materialId = mapping->materialId;
            ++changedCount;
        }
        if (changedCount > 0) {
            if (onDirty_) onDirty_();
        }
        if (console_) {
            console_->AddLog("Applied model material mappings to " + std::to_string(changedCount) + " scene object" + (changedCount == 1 ? "" : "s") + ".");
        }
    };

    auto reimportSceneInstances = [&]() {
        if (!RefreshModelAssetInspectorCache(true)) {
            if (console_) {
                console_->AddError("Failed to reimport model: " + selectedProjectFile_);
            }
            return;
        }
        const std::string refreshedImportPath = modelAssetInspectorCache_.importPath;
        const std::shared_ptr<::Model> refreshedModel = modelAssetInspectorCache_.model;
        const std::vector<ImportedMeshInfo> refreshedInfos = modelAssetInspectorCache_.infos;
        int changedCount = 0;
        bool undoPushed = false;
        const std::string normalizedImportPath = NormalizeSlashes(refreshedImportPath);
        for (SceneObject& object : objects_) {
            if (!object.hasMeshFilter || NormalizeSlashes(object.meshFilter.sourcePath) != normalizedImportPath) {
                continue;
            }
            const int meshIndex = object.meshFilter.meshIndex;
            if (meshIndex < 0 || meshIndex >= static_cast<int>(refreshedInfos.size())) {
                continue;
            }
            if (!undoPushed) {
                PushUndoState();
                undoPushed = true;
            }
            ApplyMeshInfoToSceneObject(object, refreshedInfos[static_cast<std::size_t>(meshIndex)], refreshedModel);
            object.meshFilter.sourcePath = refreshedImportPath;
            if (object.hasMeshRenderer) {
                if (const ModelMaterialMapping* mapping = FindModelMaterialMapping(settings, meshIndex)) {
                    if (!mapping->materialId.empty()) {
                        object.meshRenderer.materialId = mapping->materialId;
                    }
                }
            }
            ++changedCount;
        }
        SaveModelImportSettings(importPath, settings);
        if (changedCount > 0 && onDirty_) {
            onDirty_();
        }
        if (console_) {
            console_->AddLog("Reimported model data for " + std::to_string(changedCount) + " scene object" + (changedCount == 1 ? "" : "s") + ".");
        }
    };

    auto searchAndRemapMaterials = [&]() {
        materialManager_.LoadAll();
        int remappedCount = 0;
        for (const ImportedMeshInfo& info : infos) {
            ModelMaterialMapping& mapping = EnsureModelMaterialMapping(settings, importPath, baseName, info);
            const std::string matchedMaterialId = FindBestExistingMaterialForImportedSlot(materialManager_, info, settings.materialRemapNaming);
            if (!matchedMaterialId.empty() && mapping.materialId != matchedMaterialId) {
                mapping.materialId = matchedMaterialId;
                ++remappedCount;
            }
        }
        SaveModelImportSettings(importPath, settings);
        if (console_) {
            console_->AddLog("Model material remap matched " + std::to_string(remappedCount) + " slot" + (remappedCount == 1 ? "" : "s") + ".");
        }
    };

    std::vector<std::string> materialIds = materialManager_.ListMaterialIds();
    std::sort(materialIds.begin(), materialIds.end(), [](const std::string& a, const std::string& b) {
        return ToLowerCopy(a) < ToLowerCopy(b);
    });

    auto appendTextureSummarySlot = [](std::string& summary, const char* slotLabel, const std::string& path) {
        if (path.empty()) {
            return;
        }
        if (!summary.empty()) {
            summary += " | ";
        }
        summary += slotLabel;
        summary += ": ";
        summary += NormalizeSlashes(path);
    };

    auto appendImportedTextureSummarySlot = [](std::string& summary,
                                               const char* slotLabel,
                                               const std::string& path,
                                               const std::vector<unsigned char>& embeddedData) {
        if (path.empty() && embeddedData.empty()) {
            return;
        }
        if (!summary.empty()) {
            summary += " | ";
        }
        summary += slotLabel;
        summary += ": ";
        summary += path.empty() ? "(embedded)" : NormalizeSlashes(path);
    };

    auto materialTextureSummary = [&](const std::string& materialId) {
        std::string summary;
        if (const Material* material = materialManager_.Get(materialId)) {
            appendTextureSummarySlot(summary, "A", material->texAlbedo);
            appendTextureSummarySlot(summary, "N", material->texNormal);
            appendTextureSummarySlot(summary, "M", material->texMetallic);
            appendTextureSummarySlot(summary, "R", material->texRoughness);
            appendTextureSummarySlot(summary, "AO", material->texAo);
            for (const auto& propertyEntry : material->properties) {
                const MaterialPropertyValue& property = propertyEntry.second;
                if (property.type == MaterialPropertyType::Texture2D && !property.texturePath.empty()) {
                    appendTextureSummarySlot(summary, propertyEntry.first.c_str(), property.texturePath);
                }
            }
        }
        return summary;
    };

    auto importedTextureSummary = [&](const ImportedMeshInfo& info) {
        std::string summary;
        appendImportedTextureSummarySlot(summary, "A", info.diffuseTexturePath, info.diffuseTextureEmbeddedData);
        appendImportedTextureSummarySlot(summary, "N", info.normalTexturePath, info.normalTextureEmbeddedData);
        appendImportedTextureSummarySlot(summary, "M", info.metallicTexturePath, info.metallicTextureEmbeddedData);
        appendImportedTextureSummarySlot(summary, "R", info.roughnessTexturePath, info.roughnessTextureEmbeddedData);
        appendImportedTextureSummarySlot(summary, "AO", info.aoTexturePath, info.aoTextureEmbeddedData);
        return summary;
    };

    auto displayTextureSummary = [&](const ImportedMeshInfo& info, const std::string& materialId) {
        std::string summary = materialTextureSummary(materialId);
        if (summary.empty()) {
            summary = importedTextureSummary(info);
        }
        return summary.empty() ? std::string("(none)") : summary;
    };

    ImGui::TextUnformatted((fs::path(importPath).filename().string() + " Import Settings").c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Open")) {
        OpenProjectAssetInDefaultEditor(importPath);
    }
    ImGui::Separator();

    if (ImGui::BeginTabBar("ModelImportSettingsTabs")) {
        if (ImGui::BeginTabItem("Model")) {
            ImGui::TextDisabled("Source");
            ImGui::TextWrapped("%s", importPath.c_str());
            ImGui::TextDisabled("Format: %s", fs::path(importPath).extension().string().c_str());
            ImGui::TextDisabled("Meshes: %d", static_cast<int>(infos.size()));

            ImGui::Spacing();
            ImGui::TextUnformatted("Pivot");
            const char* pivotModes[] = {"Preserve Source Pivot", "Center Each Mesh"};
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##ModelPivotMode", &settings.pivotMode, pivotModes, IM_ARRAYSIZE(pivotModes))) {
                SaveModelImportSettings(importPath, settings);
            }

            ImGui::Spacing();
            if (ImGui::Button("Reimport Model")) {
                reimportSceneInstances();
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply To Scene")) {
                applyMaterialsToScene();
            }

            ImGui::Spacing();
            if (ImGui::BeginTable("ModelMeshInfoTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 46.0f);
                ImGui::TableSetupColumn("Mesh");
                ImGui::TableSetupColumn("Imported Material");
                ImGui::TableSetupColumn("Textures");
                ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableHeadersRow();
                for (const ImportedMeshInfo& info : infos) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u", info.meshIndex);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(info.meshName.empty() ? "(unnamed)" : info.meshName.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(info.materialName.empty() ? "(none)" : info.materialName.c_str());
                    ImGui::TableSetColumnIndex(3);
                    const ModelMaterialMapping& mapping = EnsureModelMaterialMapping(settings, importPath, baseName, info);
                    const std::string meshTextureSummary = displayTextureSummary(info, mapping.materialId);
                    ImGui::TextUnformatted(meshTextureSummary.c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%u", info.indexCount / 3);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Materials")) {
            bool importMaterials = settings.importMaterials;
            if (ImGui::Checkbox("Import Materials", &importMaterials)) {
                settings.importMaterials = importMaterials;
                SaveModelImportSettings(importPath, settings);
            }

            const char* materialLocations[] = {"Use Embedded Materials", "Use External Materials"};
            ImGui::BeginDisabled(!settings.importMaterials);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("Location", &settings.materialLocation, materialLocations, IM_ARRAYSIZE(materialLocations))) {
                SaveModelImportSettings(importPath, settings);
            }

            if (ImGui::Button("Extract Textures")) {
                const fs::path targetFolder = chooseMaterialExtractFolder();
                if (!targetFolder.empty()) {
                    extractTextures(targetFolder);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Extract Materials")) {
                const fs::path targetFolder = chooseMaterialExtractFolder();
                if (!targetFolder.empty()) {
                    extractMaterials(false, targetFolder);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Re-extract")) {
                const fs::path targetFolder = chooseMaterialExtractFolder();
                if (!targetFolder.empty()) {
                    extractMaterials(true, targetFolder);
                }
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextWrapped("Material assignments can be remapped below. Search and Remap pairs imported slots to existing Raceman materials by texture or material name.");

            if (ImGui::CollapsingHeader("On Demand Remap", ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* namingModes[] = {"By Base Texture Name", "By Material Name"};
                const char* searchModes[] = {"Recursive-Up", "Everywhere in Assets"};
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("Naming", &settings.materialRemapNaming, namingModes, IM_ARRAYSIZE(namingModes))) {
                    SaveModelImportSettings(importPath, settings);
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("Search", &settings.materialRemapSearch, searchModes, IM_ARRAYSIZE(searchModes))) {
                    SaveModelImportSettings(importPath, settings);
                }
                if (ImGui::Button("Search and Remap")) {
                    searchAndRemapMaterials();
                }
            }

            ImGui::Spacing();
            if (ImGui::BeginTable("ModelMaterialMappingsTable", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 44.0f);
                ImGui::TableSetupColumn("Imported");
                ImGui::TableSetupColumn("Texture");
                ImGui::TableSetupColumn("Raceman Material");
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 84.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 118.0f);
                ImGui::TableHeadersRow();
                for (const ImportedMeshInfo& info : infos) {
                    ModelMaterialMapping& mapping = EnsureModelMaterialMapping(settings, importPath, baseName, info);
                    const bool materialExists = !mapping.materialId.empty() && materialManager_.Exists(mapping.materialId);
                    const std::string textureLabel = displayTextureSummary(info, mapping.materialId);
                    ImGui::PushID(static_cast<int>(info.meshIndex));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u", info.meshIndex);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(info.materialName.empty() ? "(none)" : info.materialName.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(textureLabel.c_str());
                    ImGui::TableSetColumnIndex(3);
                    const char* preview = mapping.materialId.empty() ? "None (Material)" : mapping.materialId.c_str();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##ModelMaterialCombo", preview)) {
                        if (ImGui::Selectable("None (Material)", mapping.materialId.empty())) {
                            mapping.materialId.clear();
                            SaveModelImportSettings(importPath, settings);
                        }
                        for (const std::string& materialId : materialIds) {
                            const bool selected = mapping.materialId == materialId;
                            if (ImGui::Selectable(materialId.c_str(), selected)) {
                                mapping.materialId = materialId;
                                SaveModelImportSettings(importPath, settings);
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TableSetColumnIndex(4);
                    if (materialExists) {
                        ImGui::TextColored(ImVec4(0.48f, 0.82f, 0.58f, 1.0f), "Ready");
                    } else if (mapping.materialId.empty()) {
                        ImGui::TextDisabled("None");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.25f, 1.0f), "Missing");
                    }
                    ImGui::TableSetColumnIndex(5);
                    ImGui::BeginDisabled(!materialExists);
                    if (ImGui::SmallButton("Edit")) {
                        OpenMaterialEditor(mapping.materialId);
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Extract")) {
                        const fs::path targetFolder = chooseMaterialExtractFolder();
                        if (!targetFolder.empty()) {
                            mapping.materialId = CreateOrUpdateImportedMaterialAssetAtFolder(importPath, baseName, info, targetFolder, false);
                            SaveModelImportSettings(importPath, settings);
                            materialManager_.LoadAll();
                            RefreshProjectFiles();
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            if (ImGui::Button("Revert")) {
                LoadModelImportSettings(importPath, settings);
                ReconcileModelImportSettings(settings, importPath, baseName, infos);
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply")) {
                SaveModelImportSettings(importPath, settings);
                applyMaterialsToScene();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Collision")) {
            ImGui::TextWrapped("Bake mesh collision data before Play. Play mode still cooks missing or stale shapes, but ready cache entries load faster.");
            ImGui::Spacing();

            auto bakeCollisionForInfo = [&](const ImportedMeshInfo& info, ModelCollisionMapping& mapping) {
                if (mapping.mode == 0) {
                    return false;
                }
                CollisionShapeCacheInfo bakedInfo;
                const bool baked = PhysicsWorld::BakeCollisionShape(BuildModelCollisionDesc(importPath, info, mapping), &bakedInfo);
                if (console_) {
                    const std::string meshLabel = info.meshName.empty() ? ("Mesh " + std::to_string(info.meshIndex)) : info.meshName;
                    if (baked) {
                        console_->AddLog("Baked collision: " + meshLabel + " (" + CollisionCacheStatusLabel(bakedInfo.status) + ")");
                    } else {
                        console_->AddError("Failed to bake collision: " + meshLabel + (bakedInfo.message.empty() ? "" : (" - " + bakedInfo.message)));
                    }
                }
                return baked;
            };

            if (ImGui::Button("Bake All")) {
                int bakedCount = 0;
                for (const ImportedMeshInfo& info : infos) {
                    ModelCollisionMapping& mapping = EnsureModelCollisionMapping(settings, info);
                    if (bakeCollisionForInfo(info, mapping)) {
                        ++bakedCount;
                    }
                }
                SaveModelImportSettings(importPath, settings);
                if (console_) {
                    console_->AddLog("Collision bake complete: " + std::to_string(bakedCount) + " mesh" + (bakedCount == 1 ? "" : "es") + ".");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Collision Cache")) {
                std::string error;
                const int removedCount = PhysicsWorld::ClearCollisionShapeCache(&error);
                lastPhysicsCacheStatus_ = "Ready";
                if (console_) {
                    if (!error.empty()) {
                        console_->AddWarning("Cleared " + std::to_string(removedCount) + " collision cache files, but some files could not be removed: " + error);
                    } else {
                        console_->AddLog("Cleared " + std::to_string(removedCount) + " collision cache files.");
                    }
                }
            }

            ImGui::Spacing();
            if (ImGui::BeginTable("ModelCollisionMappingsTable", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_WidthFixed, 78.0f);
                ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Auto", ImGuiTableColumnFlags_WidthFixed, 52.0f);
                ImGui::TableSetupColumn("Cache", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 96.0f);
                ImGui::TableHeadersRow();
                const char* collisionModes[] = {"None", "Triangle Mesh", "Convex Hull"};
                for (const ImportedMeshInfo& info : infos) {
                    ModelCollisionMapping& mapping = EnsureModelCollisionMapping(settings, info);
                    const std::uint32_t triangleCount = info.indexCount / 3;
                    CollisionShapeCacheInfo cacheInfo;
                    if (mapping.mode == 0) {
                        cacheInfo.status = CollisionShapeCacheStatus::Missing;
                        cacheInfo.message = "Collision disabled.";
                    } else {
                        cacheInfo = PhysicsWorld::GetCollisionShapeCacheInfo(BuildModelCollisionDesc(importPath, info, mapping));
                    }

                    ImGui::PushID(static_cast<int>(info.meshIndex));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u", info.meshIndex);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(info.meshName.empty() ? "(unnamed)" : info.meshName.c_str());
                    if (triangleCount >= 50000 && mapping.mode == 1) {
                        ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.25f, 1.0f), "Heavy triangle mesh");
                    }
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%u", triangleCount);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::Combo("##CollisionMode", &mapping.mode, collisionModes, IM_ARRAYSIZE(collisionModes))) {
                        SaveModelImportSettings(importPath, settings);
                    }
                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::Checkbox("##AutoBake", &mapping.autoBake)) {
                        SaveModelImportSettings(importPath, settings);
                    }
                    ImGui::TableSetColumnIndex(5);
                    const CollisionShapeCacheStatus status = cacheInfo.status;
                    if (status == CollisionShapeCacheStatus::Ready) {
                        ImGui::TextColored(ImVec4(0.48f, 0.82f, 0.58f, 1.0f), "Ready");
                    } else if (status == CollisionShapeCacheStatus::Stale) {
                        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "Stale");
                    } else if (status == CollisionShapeCacheStatus::Failed) {
                        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.35f, 1.0f), "Failed");
                    } else {
                        ImGui::TextDisabled("Missing");
                    }
                    if (ImGui::IsItemHovered() && !cacheInfo.message.empty()) {
                        ImGui::SetTooltip("%s", cacheInfo.message.c_str());
                    }
                    ImGui::TableSetColumnIndex(6);
                    ImGui::BeginDisabled(mapping.mode == 0);
                    if (ImGui::SmallButton("Bake")) {
                        bakeCollisionForInfo(info, mapping);
                        SaveModelImportSettings(importPath, settings);
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::TextDisabled("Triangle Mesh colliders are static-only. Use Convex Hull for dynamic bodies.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void SceneEditor::RenderProjectPanel() {
    if (IsPanelHiddenByFullscreen("Browser")) {
        return;
    }

    ApplyPanelFullscreenWindowSetup("Browser");
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | PanelFullscreenWindowFlags("Browser");
    if (ImGui::Begin("Browser", nullptr, windowFlags)) {
        HandlePanelHeadingDoubleClick("Browser");
        if (ImGui::BeginTabBar("BrowserTabs")) {
            if (ImGui::BeginTabItem("Project")) {
                if (ImGui::Button("Refresh")) {
                    RefreshProjectFiles();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", selectedProjectDirectory_.c_str());
                ImGui::Separator();

                const float directoryWidth = 230.0f;
                ImGui::BeginChild("ProjectDirectories", ImVec2(directoryWidth, 0.0f), true);
                ImGui::TextUnformatted("Directories");
                ImGui::Separator();

                std::unordered_map<std::string, std::vector<std::string>> directoryChildren;
                std::unordered_set<std::string> directorySet;
                directoryChildren.reserve(projectDirectories_.size() + 1);
                directorySet.reserve(projectDirectories_.size() + 1);
                for (const std::string& directory : projectDirectories_) {
                    directorySet.insert(directory);
                }
                for (const std::string& directory : projectDirectories_) {
                    if (directory == "assets") {
                        continue;
                    }
                    directoryChildren[ParentProjectDirectory(directory)].push_back(directory);
                }

                auto renderDirectoryTree = [&](auto&& self, const std::string& directory) -> void {
                    const auto childIt = directoryChildren.find(directory);
                    const bool hasChildFolders = childIt != directoryChildren.end() && !childIt->second.empty();

                    ImGui::PushID(directory.c_str());
                    const bool selected = (directory == selectedProjectDirectory_);
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (selected) {
                        flags |= ImGuiTreeNodeFlags_Selected;
                    }
                    if (directory == "assets") {
                        flags |= ImGuiTreeNodeFlags_DefaultOpen;
                    }
                    if (!hasChildFolders) {
                        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    }
                    const std::string directoryLabel = ProjectFolderDisplayName(directory);
                    const bool open = ImGui::TreeNodeEx(directoryLabel.c_str(), flags);
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
                        selectedProjectDirectory_ = directory;
                        if (ParentProjectDirectory(selectedProjectFile_) != selectedProjectDirectory_) {
                            selectedProjectFile_.clear();
                        }
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                            const char* payloadPath = static_cast<const char*>(payload->Data);
                            if (payloadPath != nullptr) {
                                MoveProjectFile(payloadPath, directory);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", directory.c_str());
                    }
                    if (open && hasChildFolders) {
                        for (const std::string& child : childIt->second) {
                            self(self, child);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                };
                renderDirectoryTree(renderDirectoryTree, "assets");
                for (const std::string& directory : projectDirectories_) {
                    if (directory != "assets" && directorySet.find(ParentProjectDirectory(directory)) == directorySet.end()) {
                        renderDirectoryTree(renderDirectoryTree, directory);
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("ProjectFiles", ImVec2(0.0f, 0.0f), true);
                std::string sceneToOpen;
                ImGui::TextUnformatted("Path:");
                ImGui::SameLine();
                if (selectedProjectDirectory_ != "assets") {
                    if (ImGui::SmallButton("Up")) {
                        selectedProjectDirectory_ = ParentProjectDirectory(selectedProjectDirectory_);
                        selectedProjectFile_.clear();
                    }
                    ImGui::SameLine();
                }
                const std::vector<std::string> breadcrumbs = BreadcrumbParts(selectedProjectDirectory_);
                std::string breadcrumbPath;
                for (std::size_t i = 0; i < breadcrumbs.size(); ++i) {
                    breadcrumbPath = i == 0 ? breadcrumbs[i] : breadcrumbPath + "/" + breadcrumbs[i];
                    if (i > 0) {
                        ImGui::TextDisabled(">");
                        ImGui::SameLine();
                    }
                    const std::string displayLabel = i == 0 ? std::string("Assets") : breadcrumbs[i];
                    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", displayLabel.c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                        selectedProjectDirectory_ = breadcrumbPath;
                        selectedProjectFile_.clear();
                    }
                    ImGui::SameLine();
                }
                const float refreshWidth = ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                const float refreshX = ImGui::GetWindowContentRegionMax().x - refreshWidth;
                if (ImGui::GetCursorPosX() < refreshX) {
                    ImGui::SetCursorPosX(refreshX);
                }
                if (ImGui::SmallButton("Refresh##ProjectFiles")) {
                    RefreshProjectFiles();
                }
                ImGui::Separator();

                const float tileWidth = 96.0f;
                const float tileHeight = 92.0f;
                const float iconSize = 46.0f;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float availableWidth = ImGui::GetContentRegionAvail().x;
                const float visibleTileMinY = ImGui::GetWindowPos().y;
                const float visibleTileMaxY = visibleTileMinY + ImGui::GetWindowHeight();
                int columnIndex = 0;
                bool hasTiles = false;
                bool breakTileLoop = false;

                auto renderTile = [&](const std::string& id,
                                      const std::string& displayName,
                                      const std::string& iconFilename,
                                      bool selected,
                                      bool isFolder) -> bool {
                    ImGui::PushID(id.c_str());
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    const bool tileVisible = pos.y + tileHeight >= visibleTileMinY && pos.y <= visibleTileMaxY;
                    if (!tileVisible && renamingProjectFile_ != id) {
                        ImGui::Dummy(ImVec2(tileWidth, tileHeight));
                        ImGui::PopID();
                        if ((columnIndex + 1) * (tileWidth + spacing) + tileWidth <= availableWidth) {
                            ImGui::SameLine();
                            ++columnIndex;
                        } else {
                            columnIndex = 0;
                        }
                        return false;
                    }
                    ImGui::InvisibleButton("##projectTile", ImVec2(tileWidth, tileHeight));
                    const bool hovered = ImGui::IsItemHovered();
                    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                    const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const ImU32 background = selected
                        ? ImGui::GetColorU32(ImGuiCol_HeaderActive)
                        : hovered ? ImGui::GetColorU32(ImGuiCol_HeaderHovered) : 0;
                    if (background != 0) {
                        drawList->AddRectFilled(pos, ImVec2(pos.x + tileWidth, pos.y + tileHeight), background, 6.0f);
                    }
                    unsigned int icon = GetComponentIconTexture(iconFilename);
                    const bool drawGeneratedShaderGraphIcon = icon == 0 && iconFilename == "asset-shader-graph.png";
                    if (icon == 0 && iconFilename != "asset-file.png" && !drawGeneratedShaderGraphIcon) {
                        icon = GetComponentIconTexture("asset-file.png");
                    }
                    const ImVec2 iconMin(pos.x + (tileWidth - iconSize) * 0.5f, pos.y + 8.0f);
                    const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
                    if (drawGeneratedShaderGraphIcon) {
                        const ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.12f, 0.18f, 0.30f, 1.0f));
                        const ImU32 line = ImGui::ColorConvertFloat4ToU32(ImVec4(0.24f, 0.72f, 0.95f, 1.0f));
                        const ImU32 nodeA = ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.44f, 0.36f, 1.0f));
                        const ImU32 nodeB = ImGui::ColorConvertFloat4ToU32(ImVec4(0.42f, 0.82f, 0.50f, 1.0f));
                        drawList->AddRectFilled(iconMin, iconMax, bg, 7.0f);
                        const ImVec2 a(iconMin.x + 12.0f, iconMin.y + 16.0f);
                        const ImVec2 b(iconMax.x - 12.0f, iconMax.y - 16.0f);
                        drawList->AddLine(a, b, line, 2.5f);
                        drawList->AddCircleFilled(a, 7.0f, nodeA);
                        drawList->AddCircleFilled(b, 7.0f, nodeB);
                        drawList->AddText(ImVec2(iconMin.x + 13.0f, iconMax.y - 18.0f), ImGui::GetColorU32(ImGuiCol_Text), "SG");
                    } else if (icon != 0) {
                        drawList->AddImage(static_cast<ImTextureID>(icon), iconMin, iconMax);
                    } else {
                        drawList->AddRect(iconMin, iconMax, ImGui::GetColorU32(ImGuiCol_TextDisabled), 4.0f);
                        drawList->AddText(ImVec2(iconMin.x + 8.0f, iconMin.y + 14.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), isFolder ? "DIR" : "FILE");
                    }
                    if (renamingProjectFile_ == id) {
                        ImGui::SetCursorScreenPos(ImVec2(pos.x + 4.0f, pos.y + iconSize + 18.0f));
                        ImGui::SetNextItemWidth(tileWidth - 8.0f);
                        if (focusProjectRename_) {
                            ImGui::SetKeyboardFocusHere();
                            focusProjectRename_ = false;
                        }
                        const bool enterPressed = ImGui::InputText("##projectRename", projectRenameBuffer_, sizeof(projectRenameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                        if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
                            CommitProjectFileRename();
                        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                            renamingProjectFile_.clear();
                        }
                    } else {
                        const std::string clippedName = EllipsizeTextToWidth(displayName, tileWidth - 10.0f);
                        const ImVec2 textSize = ImGui::CalcTextSize(clippedName.c_str());
                        drawList->AddText(ImVec2(pos.x + (tileWidth - textSize.x) * 0.5f, pos.y + iconSize + 20.0f), ImGui::GetColorU32(ImGuiCol_Text), clippedName.c_str());
                    }
                    ImGui::PopID();
                    if ((columnIndex + 1) * (tileWidth + spacing) + tileWidth <= availableWidth) {
                        ImGui::SameLine();
                        ++columnIndex;
                    } else {
                        columnIndex = 0;
                    }
                    return clicked || doubleClicked;
                };

                const auto selectedDirectoryChildrenIt = directoryChildren.find(selectedProjectDirectory_);
                const std::vector<std::string> emptyChildFolders;
                const std::vector<std::string>& childFolders = selectedDirectoryChildrenIt != directoryChildren.end()
                    ? selectedDirectoryChildrenIt->second
                    : emptyChildFolders;

                for (const std::string& directory : childFolders) {
                    hasTiles = true;
                    const bool selected = selectedProjectFile_ == directory;
                    const bool activated = renderTile(directory, ProjectFolderDisplayName(directory), "asset-folder.png", selected, true);
                    if (activated) {
                        selectedProjectFile_ = directory;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            selectedProjectDirectory_ = directory;
                            selectedProjectFile_.clear();
                        }
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                            const char* payloadPath = static_cast<const char*>(payload->Data);
                            if (payloadPath != nullptr) {
                                MoveProjectFile(payloadPath, directory);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::BeginPopupContextItem(("FolderContext##" + directory).c_str())) {
                        selectedProjectFile_ = directory;
                        ImGui::TextDisabled("%s", ProjectFolderDisplayName(directory).c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Open")) {
                            selectedProjectDirectory_ = directory;
                            selectedProjectFile_.clear();
                        }
                        if (ImGui::BeginMenu("Vehicle")) {
                            if (ImGui::MenuItem("Add Vehicle Profile")) {
                                selectedProjectDirectory_ = directory;
                                selectedProjectFile_.clear();
                                createProjectAssetType_ = ProjectCreateAssetType::VehicleProfile;
                                std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                                showCreateProjectAssetPopup_ = true;
                            }
                            if (ImGui::MenuItem("Add Vehicle Sound Profile")) {
                                selectedProjectDirectory_ = directory;
                                selectedProjectFile_.clear();
                                createProjectAssetType_ = ProjectCreateAssetType::VehicleSoundProfile;
                                std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                                showCreateProjectAssetPopup_ = true;
                            }
                            ImGui::EndMenu();
                        }
                        if (ImGui::MenuItem("Rename", "F2")) {
                            BeginProjectFileRename(directory);
                        }
                        if (ImGui::MenuItem("Delete Empty Folder", "Del")) {
                            DeleteProjectFolder(directory);
                            breakTileLoop = true;
                        }
                        ImGui::EndPopup();
                    }
                    if (breakTileLoop) {
                        break;
                    }
                }
                if (!breakTileLoop) {
                for (const std::string& file : projectFiles_) {
                    if (ParentProjectDirectory(file) != selectedProjectDirectory_) {
                        continue;
                    }

                    hasTiles = true;
                    const bool isMesh = IsMeshAssetPath(file);
                    const bool isMaterial = IsMaterialAssetPath(file);
                    const bool isScene = IsSceneAssetPath(file);
                    const bool isPrefab = IsPrefabAssetPath(file);
                    const bool isShaderGraph = IsShaderGraphAssetPath(file);
                    const bool isTrack = IsTrackAssetPath(file);
                    const bool isVehicleConfig = IsVehicleConfigAssetPath(file);
                    const bool isVehicleSound = IsVehicleSoundAssetPath(file);
                    std::string filename = ProjectAssetDisplayFilename(file);

                    ImGui::PushID(file.c_str());
                    if (renamingProjectFile_ == file) {
                        renderTile(file, filename, AssetIconForProjectFile(file), selectedProjectFile_ == file, false);
                    } else {
                        bool deletedFromContext = false;
                        const bool selected = (selectedProjectFile_ == file);
                        if (renderTile(file, filename, AssetIconForProjectFile(file), selected, false)) {
                            SelectProjectFile(file);
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                if (isScene) {
                                    sceneToOpen = file;
                                } else if (isMaterial) {
                                    OpenMaterialEditor(MaterialIdFromAssetPath(file));
                                } else if (isShaderGraph) {
                                    OpenShaderGraphEditor(file);
                                } else if (isTrack) {
                                    OpenTrackGenerator(file);
                                } else if (isVehicleConfig) {
                                    OpenVehicleConfigEditor(file);
                                } else if (isVehicleSound) {
                                    OpenVehicleSoundEditor(file);
                                } else if (isPrefab) {
                                    if (InstantiatePrefab(file)) {
                                        if (console_) console_->AddLog("Instantiated prefab: " + file);
                                    } else if (console_) {
                                        console_->AddError("Failed to instantiate prefab: " + file);
                                    }
                                } else if (OpenProjectAssetInDefaultEditor(file)) {
                                    if (console_) {
                                        console_->AddLog("Opened project file: " + file);
                                    }
                                } else if (console_) {
                                    console_->AddError("Failed to open project file: " + file);
                                }
                            }
                        }
                        if (ImGui::BeginPopupContextItem("ProjectFileContext")) {
                            SelectProjectFile(file);
                            ImGui::TextDisabled("%s", ProjectAssetDisplayFilename(file).c_str());
                            ImGui::Separator();
                            if (isScene) {
                                if (ImGui::MenuItem("Open")) {
                                    sceneToOpen = file;
                                }
                            } else if (isPrefab) {
                                if (ImGui::MenuItem("Instantiate")) {
                                    if (InstantiatePrefab(file)) {
                                        if (console_) console_->AddLog("Instantiated prefab: " + file);
                                    } else if (console_) {
                                        console_->AddError("Failed to instantiate prefab: " + file);
                                    }
                                }
                            } else if (isMaterial) {
                                if (ImGui::MenuItem("Edit")) {
                                    OpenMaterialEditor(MaterialIdFromAssetPath(file));
                                }
                                if (ImGui::MenuItem("Open in Default App")) {
                                    if (OpenProjectAssetInDefaultEditor(file)) {
                                        if (console_) {
                                            console_->AddLog("Opened project file: " + file);
                                        }
                                    } else if (console_) {
                                        console_->AddError("Failed to open project file: " + file);
                                    }
                                }
                            } else if (isTrack) {
                                if (ImGui::MenuItem("Edit")) {
                                    OpenTrackGenerator(file);
                                }
                                if (ImGui::MenuItem("Open in Default App")) {
                                    if (OpenProjectAssetInDefaultEditor(file)) {
                                        if (console_) {
                                            console_->AddLog("Opened project file: " + file);
                                        }
                                    } else if (console_) {
                                        console_->AddError("Failed to open project file: " + file);
                                    }
                                }
                            } else if (isShaderGraph) {
                                if (ImGui::MenuItem("Edit")) {
                                    OpenShaderGraphEditor(file);
                                }
                                if (ImGui::MenuItem("Open in Default App")) {
                                    if (OpenProjectAssetInDefaultEditor(file)) {
                                        if (console_) {
                                            console_->AddLog("Opened project file: " + file);
                                        }
                                    } else if (console_) {
                                        console_->AddError("Failed to open project file: " + file);
                                    }
                                }
                            } else if (isVehicleConfig) {
                                if (ImGui::MenuItem("Edit")) {
                                    OpenVehicleConfigEditor(file);
                                }
                                if (ImGui::MenuItem("Open in Default App")) {
                                    if (OpenProjectAssetInDefaultEditor(file)) {
                                        if (console_) {
                                            console_->AddLog("Opened project file: " + file);
                                        }
                                    } else if (console_) {
                                        console_->AddError("Failed to open project file: " + file);
                                    }
                                }
                            } else if (isVehicleSound) {
                                if (ImGui::MenuItem("Edit")) {
                                    OpenVehicleSoundEditor(file);
                                }
                                if (ImGui::MenuItem("Open in Default App")) {
                                    if (OpenProjectAssetInDefaultEditor(file)) {
                                        if (console_) {
                                            console_->AddLog("Opened project file: " + file);
                                        }
                                    } else if (console_) {
                                        console_->AddError("Failed to open project file: " + file);
                                    }
                                }
                            } else {
                                if (ImGui::MenuItem("Open")) {
                                    if (OpenProjectAssetInDefaultEditor(file)) {
                                        if (console_) {
                                            console_->AddLog("Opened project file: " + file);
                                        }
                                    } else if (console_) {
                                        console_->AddError("Failed to open project file: " + file);
                                    }
                                }
                            }
                            ImGui::Separator();
                            if (ImGui::BeginMenu("Move To")) {
                                const std::string currentDirectory = ParentProjectDirectory(file);

                                auto renderMoveTarget = [&](auto&& self, const std::string& directory) -> bool {
                                    std::vector<std::string> children;
                                    for (const std::string& candidate : projectDirectories_) {
                                        if (candidate != directory && IsDirectChildProjectPath(candidate, directory)) {
                                            children.push_back(candidate);
                                        }
                                    }

                                    const bool isCurrentDirectory = NormalizeSlashes(directory) == NormalizeSlashes(currentDirectory);
                                    if (children.empty()) {
                                        if (isCurrentDirectory) {
                                            ImGui::BeginDisabled();
                                            ImGui::MenuItem((ProjectFolderDisplayName(directory) + "##moveCurrent_" + directory).c_str());
                                            ImGui::EndDisabled();
                                            return false;
                                        }
                                        if (ImGui::MenuItem((ProjectFolderDisplayName(directory) + "##moveTarget_" + directory).c_str())) {
                                            MoveProjectFile(file, directory);
                                            return true;
                                        }
                                        return false;
                                    }

                                    if (!ImGui::BeginMenu((ProjectFolderDisplayName(directory) + "##moveMenu_" + directory).c_str())) {
                                        return false;
                                    }

                                    bool moved = false;
                                    if (isCurrentDirectory) {
                                        ImGui::TextDisabled("Current folder");
                                    } else if (ImGui::MenuItem(("Move here##moveHere_" + directory).c_str())) {
                                        MoveProjectFile(file, directory);
                                        moved = true;
                                    }

                                    ImGui::Separator();
                                    for (const std::string& child : children) {
                                        if (self(self, child)) {
                                            moved = true;
                                            break;
                                        }
                                    }

                                    ImGui::EndMenu();
                                    return moved;
                                };

                                bool anyMoveTarget = false;
                                for (const std::string& directory : projectDirectories_) {
                                    if (directory != currentDirectory) {
                                        anyMoveTarget = true;
                                        break;
                                    }
                                }

                                if (!anyMoveTarget) {
                                    ImGui::TextDisabled("No other folders.");
                                } else {
                                    if (renderMoveTarget(renderMoveTarget, "assets")) {
                                        deletedFromContext = true;
                                    }
                                    for (const std::string& directory : projectDirectories_) {
                                        if (directory != "assets" &&
                                            std::find(projectDirectories_.begin(), projectDirectories_.end(), ParentProjectDirectory(directory)) == projectDirectories_.end()) {
                                            if (renderMoveTarget(renderMoveTarget, directory)) {
                                                deletedFromContext = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                ImGui::EndMenu();
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                                fileClipboard_.path = file;
                                fileClipboard_.isCut = false;
                            }
                            if (ImGui::MenuItem("Cut", "Ctrl+X")) {
                                fileClipboard_.path = file;
                                fileClipboard_.isCut = true;
                            }
                            if (ImGui::MenuItem("Rename", "F2")) {
                                BeginProjectFileRename(file);
                            }
                            if (ImGui::MenuItem("Delete", "Del")) {
                                DeleteProjectFile(file);
                                deletedFromContext = true;
                            }
                            ImGui::EndPopup();
                        }
                        if (deletedFromContext) {
                            ImGui::PopID();
                            breakTileLoop = true;
                            break;
                        }
                        const bool fileActive = (selectedProjectFile_ == file) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                        if ((ImGui::IsItemFocused() || fileActive) && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                            BeginProjectFileRename(selectedProjectFile_.empty() ? file : selectedProjectFile_);
                        }
                        if ((ImGui::IsItemFocused() || fileActive) && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
                            DeleteProjectFile(selectedProjectFile_.empty() ? file : selectedProjectFile_);
                            ImGui::PopID();
                            breakTileLoop = true;
                            break;
                        }
                    }
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        ImGui::SetDragDropPayload(kProjectFilePayload, file.c_str(), file.size() + 1);
                        ImGui::TextUnformatted(file.c_str());
                        ImGui::EndDragDropSource();
                    }
                    ImGui::PopID();
                }
                }
                if (!hasTiles) {
                    ImGui::TextDisabled("No assets in this folder.");
                }
                // Keyboard shortcuts for copy/cut/paste (when panel is focused, not typing)
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
                    const bool ctrl = ImGui::GetIO().KeyCtrl;
                    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C) && !selectedProjectFile_.empty() && !fs::is_directory(ProjectAssetPathToAbsolute(selectedProjectFile_))) {
                        fileClipboard_.path = selectedProjectFile_;
                        fileClipboard_.isCut = false;
                    }
                    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X) && !selectedProjectFile_.empty() && !fs::is_directory(ProjectAssetPathToAbsolute(selectedProjectFile_))) {
                        fileClipboard_.path = selectedProjectFile_;
                        fileClipboard_.isCut = true;
                    }
                    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V) && !fileClipboard_.path.empty()) {
                        if (fileClipboard_.isCut) {
                            if (MoveProjectFile(fileClipboard_.path, selectedProjectDirectory_)) {
                                fileClipboard_.path.clear();
                            }
                        } else {
                            CopyProjectFileTo(fileClipboard_.path, selectedProjectDirectory_);
                        }
                    }
                    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Delete) && !selectedProjectFile_.empty()) {
                        const fs::path selectedAbsolutePath = ProjectAssetPathToAbsolute(selectedProjectFile_);
                        if (fs::is_directory(selectedAbsolutePath)) {
                            DeleteProjectFolder(selectedProjectFile_);
                        } else {
                            DeleteProjectFile(selectedProjectFile_);
                        }
                    }
                }
                if (ImGui::BeginPopupContextWindow("ProjectFilesEmptyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                    if (!fileClipboard_.path.empty()) {
                        const std::string pasteLabel = std::string("Paste  ") + ProjectAssetDisplayFilename(fileClipboard_.path) + (fileClipboard_.isCut ? "  (cut)" : "  (copy)");
                        if (ImGui::MenuItem(pasteLabel.c_str(), "Ctrl+V")) {
                            if (fileClipboard_.isCut) {
                                if (MoveProjectFile(fileClipboard_.path, selectedProjectDirectory_)) {
                                    fileClipboard_.path.clear();
                                }
                            } else {
                                CopyProjectFileTo(fileClipboard_.path, selectedProjectDirectory_);
                            }
                        }
                        ImGui::Separator();
                    }
                    if (ImGui::BeginMenu("Create")) {
                        if (ImGui::MenuItem("Folder")) {
                            createProjectAssetType_ = ProjectCreateAssetType::Folder;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        if (ImGui::MenuItem("Scene")) {
                            createProjectAssetType_ = ProjectCreateAssetType::Scene;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        if (ImGui::MenuItem("Material")) {
                            createProjectAssetType_ = ProjectCreateAssetType::Material;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            createProjectMaterialShaderIndex_ = 0;
                            showCreateProjectAssetPopup_ = true;
                        }
                        if (ImGui::MenuItem("Shader Graph")) {
                            createProjectAssetType_ = ProjectCreateAssetType::ShaderGraph;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        if (ImGui::MenuItem("Track")) {
                            createProjectAssetType_ = ProjectCreateAssetType::Track;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        if (ImGui::MenuItem("C++ Script")) {
                            createProjectAssetType_ = ProjectCreateAssetType::Script;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Vehicle")) {
                        if (ImGui::MenuItem("Add Vehicle Profile")) {
                            createProjectAssetType_ = ProjectCreateAssetType::VehicleProfile;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        if (ImGui::MenuItem("Add Vehicle Sound Profile")) {
                            createProjectAssetType_ = ProjectCreateAssetType::VehicleSoundProfile;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Refresh")) {
                        RefreshProjectFiles();
                    }
                    ImGui::EndPopup();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                        const char* payloadPath = static_cast<const char*>(payload->Data);
                        if (payloadPath != nullptr) {
                            MoveProjectFile(payloadPath, selectedProjectDirectory_);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 2.0f;
                const float footerY = ImGui::GetWindowHeight() - footerHeight;
                if (ImGui::GetCursorPosY() < footerY) {
                    ImGui::SetCursorPosY(footerY);
                }
                ImGui::Separator();
                const std::string selectedPath = selectedProjectFile_.empty() ? selectedProjectDirectory_ : selectedProjectFile_;
                ImGui::TextDisabled("Selected: %s", selectedPath.c_str());
                ImGui::EndChild();
                // Accept hierarchy-object drag onto the project files pane to create a prefab.
                // Must be called after EndChild() so the child window is the last item.
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                        if (payload->DataSize == sizeof(int)) {
                            pendingPrefabObjectIndex_ = *static_cast<const int*>(payload->Data);
                            const std::string defaultName =
                                (pendingPrefabObjectIndex_ >= 0 && pendingPrefabObjectIndex_ < (int)objects_.size())
                                ? SanitizeAssetBaseName(objects_[pendingPrefabObjectIndex_].name)
                                : std::string("NewPrefab");
                            std::snprintf(savePrefabNameBuffer_, sizeof(savePrefabNameBuffer_), "%s", defaultName.c_str());
                            showSavePrefabPopup_ = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (showSavePrefabPopup_) {
                    ImGui::OpenPopup("Save Prefab");
                    showSavePrefabPopup_ = false;
                }
                if (ImGui::BeginPopupModal("Save Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Save as Prefab");
                    ImGui::TextDisabled("Directory: %s", selectedProjectDirectory_.c_str());
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::IsWindowAppearing()) {
                        ImGui::SetKeyboardFocusHere();
                    }
                    const bool enterPressed = ImGui::InputText("Name##prefabName", savePrefabNameBuffer_, sizeof(savePrefabNameBuffer_),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    const bool submit = ImGui::Button("Save") || enterPressed;
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        pendingPrefabObjectIndex_ = -1;
                        ImGui::CloseCurrentPopup();
                    }
                    if (submit && pendingPrefabObjectIndex_ >= 0) {
                        const std::string sanitized = SanitizeAssetBaseName(savePrefabNameBuffer_);
                        if (!sanitized.empty()) {
                            const std::string prefabPath = selectedProjectDirectory_ + "/" + sanitized + ".prefab.json";
                            if (SaveObjectAsPrefab(pendingPrefabObjectIndex_, prefabPath)) {
                                RefreshProjectFiles();
                                selectedProjectFile_ = prefabPath;
                                if (console_) console_->AddLog("Saved prefab: " + prefabPath);
                            } else if (console_) {
                                console_->AddError("Failed to save prefab: " + prefabPath);
                            }
                            pendingPrefabObjectIndex_ = -1;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::EndPopup();
                }
                if (showCreateProjectAssetPopup_) {
                    ImGui::OpenPopup("Create Project Asset");
                    showCreateProjectAssetPopup_ = false;
                }
                if (ImGui::BeginPopupModal("Create Project Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted(ProjectCreateAssetTypeTitle(createProjectAssetType_));
                    ImGui::TextDisabled("Directory: %s", selectedProjectDirectory_.c_str());
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("Name", createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_));
                    if (createProjectAssetType_ == ProjectCreateAssetType::Material) {
                        const auto& shaders = ShaderRegistry::BuiltInShaders();
                        createProjectMaterialShaderIndex_ = (std::max)(0, (std::min)(createProjectMaterialShaderIndex_, static_cast<int>(shaders.size()) - 1));
                        const ShaderDefinition& currentShader = shaders[static_cast<std::size_t>(createProjectMaterialShaderIndex_)];
                        ImGui::SetNextItemWidth(260.0f);
                        if (ImGui::BeginCombo("Shader", currentShader.displayName.c_str())) {
                            for (int i = 0; i < static_cast<int>(shaders.size()); ++i) {
                                const ShaderDefinition& shader = shaders[static_cast<std::size_t>(i)];
                                const bool selected = i == createProjectMaterialShaderIndex_;
                                if (ImGui::Selectable(shader.displayName.c_str(), selected)) {
                                    createProjectMaterialShaderIndex_ = i;
                                }
                                if (selected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }
                    const bool submit = ImGui::Button("Create") || ImGui::IsKeyPressed(ImGuiKey_Enter);
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        createProjectAssetType_ = ProjectCreateAssetType::None;
                        ImGui::CloseCurrentPopup();
                    }
                    if (submit) {
                        bool created = false;
                        std::string createdScenePath;
                        std::string createdVehicleConfigPath;
                        if (createProjectAssetType_ == ProjectCreateAssetType::Folder) {
                            created = CreateProjectFolder(createProjectAssetNameBuffer_);
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::Scene) {
                            created = CreateSceneAsset(createProjectAssetNameBuffer_, &createdScenePath);
                            if (created) {
                                selectedProjectFile_ = createdScenePath;
                            }
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::Material) {
                            std::string materialId;
                            const auto& shaders = ShaderRegistry::BuiltInShaders();
                            createProjectMaterialShaderIndex_ = (std::max)(0, (std::min)(createProjectMaterialShaderIndex_, static_cast<int>(shaders.size()) - 1));
                            created = CreateMaterialAsset(createProjectAssetNameBuffer_, &materialId, shaders[static_cast<std::size_t>(createProjectMaterialShaderIndex_)].id);
                            if (created) {
                                selectedProjectFile_ = selectedProjectDirectory_ + "/" + materialId + ".mat.json";
                            }
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::ShaderGraph) {
                            std::string graphPath;
                            created = CreateShaderGraphAsset(createProjectAssetNameBuffer_, &graphPath);
                            if (created) {
                                selectedProjectFile_ = graphPath;
                                OpenShaderGraphEditor(graphPath);
                            }
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::VehicleProfile) {
                            created = CreateVehicleConfigAsset(createProjectAssetNameBuffer_, &createdVehicleConfigPath);
                            if (created) {
                                OpenVehicleConfigEditor(createdVehicleConfigPath);
                            }
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::VehicleSoundProfile) {
                            std::string createdSoundProfilePath;
                            created = CreateVehicleSoundAsset(createProjectAssetNameBuffer_, &createdSoundProfilePath);
                            if (created) {
                                OpenVehicleSoundEditor(createdSoundProfilePath);
                            }
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::Track) {
                            const std::string baseName = SanitizeAssetBaseName(createProjectAssetNameBuffer_);
                            fs::path target = ProjectAssetPathToAbsolute(selectedProjectDirectory_) / (baseName + ".track.json");
                            int suffix = 1;
                            while (fs::exists(target)) {
                                target = ProjectAssetPathToAbsolute(selectedProjectDirectory_) / (baseName + "_" + std::to_string(suffix++) + ".track.json");
                            }
                            TrackSource source;
                            source.name = fs::path(target).stem().stem().string();
                            source.closed = false;
                            source.controlPoints = BuildTrackPresetPoints(source.presetType, 120.0f, 70.0f, 18.0f, 16);
                            std::string error;
                            created = SaveTrackSource(target.string(), source, &error);
                            if (created) {
                                selectedProjectFile_ = ToProjectAssetPath(target, FindAssetsRoot());
                                OpenTrackGenerator(selectedProjectFile_);
                            } else if (console_) {
                                console_->AddError("Failed to create track: " + error);
                            }
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::Script) {
                            created = CreateScriptAsset(createProjectAssetNameBuffer_, false);
                        }
                        if (created) {
                            createProjectAssetType_ = ProjectCreateAssetType::None;
                            createProjectAssetNameBuffer_[0] = '\0';
                            RefreshProjectFiles();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::EndPopup();
                }
                if (!sceneToOpen.empty() && sceneToOpen != savePath_) {
                    if (OpenSceneAsset(sceneToOpen)) {
                        if (console_) {
                            console_->AddLog("Opened scene asset: " + sceneToOpen);
                        }
                    } else if (console_) {
                        console_->AddError("Failed to open scene asset: " + sceneToOpen);
                    }
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Console")) {
                if (console_) {
                    console_->RenderContents();
                } else {
                    ImGui::TextDisabled("Console is not connected.");
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void SceneEditor::RenderProjectPhysicsSettings() {
    bool projectSettingsChanged = false;
    if (ImGui::BeginTable("ProjectPhysicsLayerMatrix", kPhysicsLayerCount + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Layer");
        for (int column = 0; column < kPhysicsLayerCount; ++column) {
            ImGui::TableSetColumnIndex(column + 1);
            ImGui::PushID(("projectPhysicsLayerNameHeader_" + std::to_string(column)).c_str());
            ImGui::SetNextItemWidth(100.0f);
            std::string layerName = physicsLayerNames_[static_cast<std::size_t>(column)];
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "%s", layerName.c_str());
            if (ImGui::InputText("##layerName", buffer, sizeof(buffer))) {
                physicsLayerNames_[static_cast<std::size_t>(column)] = buffer;
                projectSettingsChanged = true;
            }
            ImGui::PopID();
        }

        for (int row = 0; row < kPhysicsLayerCount; ++row) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(GetPhysicsLayerName(row));
            for (int column = 0; column < kPhysicsLayerCount; ++column) {
                ImGui::TableSetColumnIndex(column + 1);
                bool collides = physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
                const std::string checkboxId = "##projectPhysicsLayerCollision_" + std::to_string(row) + "_" + std::to_string(column);
                if (ImGui::Checkbox(checkboxId.c_str(), &collides)) {
                    physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = collides;
                    physicsLayerCollisionMatrix_[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] = collides;
                    projectSettingsChanged = true;
                }
            }
        }

        ImGui::EndTable();
    }

    if (projectSettingsChanged) {
        for (int layerIndex = 0; layerIndex < kPhysicsLayerCount; ++layerIndex) {
            if (physicsLayerNames_[static_cast<std::size_t>(layerIndex)].empty()) {
                physicsLayerNames_[static_cast<std::size_t>(layerIndex)] = layerIndex == 0 ? "Default" : ("Layer" + std::to_string(layerIndex));
            }
        }
        SaveProject();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Assign each object's physics layer in the Inspector. This matrix controls which layers collide.");
    ImGui::Spacing();
    ImGui::SeparatorText("Collision Bake Cache");
    ImGui::TextDisabled("Path: %s", PhysicsWorld::GetCollisionShapeCacheDirectory().c_str());
    if (ImGui::Button("Clear Collision Cache")) {
        std::string error;
        const int removedCount = PhysicsWorld::ClearCollisionShapeCache(&error);
        lastPhysicsCacheStatus_ = "Ready";
        if (console_) {
            if (!error.empty()) {
                console_->AddWarning("Cleared " + std::to_string(removedCount) + " collision cache files, but some files could not be removed: " + error);
            } else {
                console_->AddLog("Cleared " + std::to_string(removedCount) + " collision cache files.");
            }
        }
    }
}

void SceneEditor::RenderProjectTagsAndLayersSettings() {
    EnsureProjectTags();

    if (ImGui::CollapsingHeader("Tags", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("ProjectTagsTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("Tag");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableHeadersRow();

            for (int tagIndex = 0; tagIndex < static_cast<int>(projectTags_.size()); ++tagIndex) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", tagIndex);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(projectTags_[static_cast<std::size_t>(tagIndex)].c_str());

                ImGui::TableSetColumnIndex(2);
                if (tagIndex == 0) {
                    ImGui::TextDisabled("Built-in");
                } else {
                    ImGui::PushID(("removeTag_" + std::to_string(tagIndex)).c_str());
                    if (ImGui::SmallButton("Remove")) {
                        RemoveProjectTag(tagIndex);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("New Tag", createTagNameBuffer_, sizeof(createTagNameBuffer_));
        ImGui::SameLine();
        if (ImGui::Button("Add")) {
            if (AddProjectTag(createTagNameBuffer_)) {
                createTagNameBuffer_[0] = '\0';
            }
        }
        ImGui::TextDisabled("Objects pick tags from the Inspector dropdown.");
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
        RenderProjectPhysicsSettings();
    }
}

void SceneEditor::RenderProjectInputSettings() {
    if (inputManager_ != nullptr && inputProfiles_.empty()) {
        inputManager_->EnsureDefaultProfiles();
        inputProfiles_ = inputManager_->GetInputProfiles();
    }

    bool projectSettingsChanged = false;
    const int previousSelectedInputProfileIndex = selectedInputProfileIndex_;
    const int previousSelectedInputDevicePage = selectedInputDevicePage_;
    const int previousSelectedWheelSettingsProfileIndex = selectedWheelSettingsProfileIndex_;
    const bool previousProjectInputTestActive = projectInputTestActive_;
    const int previousProjectInputTestDeviceIndex = projectInputTestDeviceIndex_;

    if (ImGui::CollapsingHeader("Connected Devices", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (inputManager_ == nullptr) {
            ImGui::TextDisabled("Input manager is not connected.");
        } else {
            const auto& devices = inputManager_->GetConnectedDevices();
            for (const InputDeviceInfo& device : devices) {
                ImGui::PushID(device.runtimeId.c_str());
                ImGui::SeparatorText(device.displayName.c_str());
                ImGui::TextDisabled("Type: %s", InputDeviceTypeLabel(device.type));
                ImGui::TextDisabled("Runtime Id: %s", device.runtimeId.c_str());
                ImGui::TextDisabled("Axes: %d  Buttons: %d", device.axisCount, device.buttonCount);
                if (device.type == InputDeviceType::Wheel) {
                    if (const WheelSettingsProfile* matchedSettings = FindWheelSettingsForDevice(wheelSettingsProfiles_, device)) {
                        ImGui::TextDisabled("Matched Wheel Preset: %s", matchedSettings->displayName.c_str());
                    } else {
                        ImGui::TextDisabled("Matched Wheel Preset: none");
                    }
                }
                if (!device.axes.empty()) {
                    for (int axisIndex = 0; axisIndex < static_cast<int>(device.axes.size()); ++axisIndex) {
                        const float value = device.axes[static_cast<std::size_t>(axisIndex)];
                        ImGui::TextDisabled("Axis %d: %.3f", axisIndex, value);
                    }
                }
                if (!device.buttons.empty()) {
                    for (int buttonIndex = 0; buttonIndex < static_cast<int>(device.buttons.size()); ++buttonIndex) {
                        ImGui::TextDisabled("Button %d: %s", buttonIndex,
                            device.buttons[static_cast<std::size_t>(buttonIndex)] == GLFW_PRESS ? "Pressed" : "Released");
                    }
                }
                ImGui::PopID();
            }
        }
    }

    if (ImGui::CollapsingHeader("Wheel Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        EnsureDefaultWheelSettingsProfiles(wheelSettingsProfiles_);
        selectedWheelSettingsProfileIndex_ = (std::max)(0, (std::min)(selectedWheelSettingsProfileIndex_, static_cast<int>(wheelSettingsProfiles_.size()) - 1));

        const char* wheelPreview = wheelSettingsProfiles_[static_cast<std::size_t>(selectedWheelSettingsProfileIndex_)].displayName.c_str();
        if (ImGui::BeginCombo("Wheel Preset", wheelPreview)) {
            for (int i = 0; i < static_cast<int>(wheelSettingsProfiles_.size()); ++i) {
                const bool isSelected = i == selectedWheelSettingsProfileIndex_;
                if (ImGui::Selectable(wheelSettingsProfiles_[static_cast<std::size_t>(i)].displayName.c_str(), isSelected)) {
                    selectedWheelSettingsProfileIndex_ = i;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Add Wheel Preset")) {
            WheelSettingsProfile profile;
            profile.id = "wheel_preset_" + std::to_string(wheelSettingsProfiles_.size() + 1);
            profile.displayName = "New Wheel Preset";
            wheelSettingsProfiles_.push_back(std::move(profile));
            selectedWheelSettingsProfileIndex_ = static_cast<int>(wheelSettingsProfiles_.size()) - 1;
            projectSettingsChanged = true;
        }
        ImGui::SameLine();
        const bool canDeleteWheelPreset = !wheelSettingsProfiles_.empty() &&
            wheelSettingsProfiles_[static_cast<std::size_t>(selectedWheelSettingsProfileIndex_)].id != "default_wheel";
        if (!canDeleteWheelPreset) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Delete Wheel Preset") && canDeleteWheelPreset) {
            wheelSettingsProfiles_.erase(wheelSettingsProfiles_.begin() + selectedWheelSettingsProfileIndex_);
            selectedWheelSettingsProfileIndex_ = (std::max)(0, selectedWheelSettingsProfileIndex_ - 1);
            projectSettingsChanged = true;
        }
        if (!canDeleteWheelPreset) {
            ImGui::EndDisabled();
        }

        WheelSettingsProfile& wheelSettings = wheelSettingsProfiles_[static_cast<std::size_t>(selectedWheelSettingsProfileIndex_)];
        char wheelIdBuffer[128]{};
        char wheelNameBuffer[128]{};
        char wheelPatternBuffer[128]{};
        std::snprintf(wheelIdBuffer, sizeof(wheelIdBuffer), "%s", wheelSettings.id.c_str());
        std::snprintf(wheelNameBuffer, sizeof(wheelNameBuffer), "%s", wheelSettings.displayName.c_str());
        std::snprintf(wheelPatternBuffer, sizeof(wheelPatternBuffer), "%s", wheelSettings.deviceNamePattern.c_str());
        if (ImGui::InputText("Preset Id", wheelIdBuffer, sizeof(wheelIdBuffer))) {
            wheelSettings.id = SanitizeAssetBaseName(wheelIdBuffer);
            projectSettingsChanged = true;
        }
        if (ImGui::InputText("Preset Name", wheelNameBuffer, sizeof(wheelNameBuffer))) {
            wheelSettings.displayName = TrimCopyLocal(wheelNameBuffer);
            projectSettingsChanged = true;
        }
        if (ImGui::InputText("Device Match", wheelPatternBuffer, sizeof(wheelPatternBuffer))) {
            wheelSettings.deviceNamePattern = TrimCopyLocal(wheelPatternBuffer);
            projectSettingsChanged = true;
        }
        ImGui::TextDisabled("Device Match uses a case-insensitive substring of the detected wheel name.");

        if (!inputProfiles_.empty()) {
            if (ImGui::Button("Apply To Selected Input Profile")) {
                ApplyWheelSettingsProfileToInputProfile(
                    wheelSettings,
                    inputProfiles_[static_cast<std::size_t>(selectedInputProfileIndex_)]);
                projectSettingsChanged = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Target: %s", inputProfiles_[static_cast<std::size_t>(selectedInputProfileIndex_)].displayName.c_str());
        }

        if (ImGui::CollapsingHeader("Steering", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::DragFloat("Range Degrees", &wheelSettings.steeringRangeDegrees, 5.0f, 90.0f, 1440.0f, "%.0f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Sensitivity", &wheelSettings.steeringSensitivity, 0.01f, 0.1f, 4.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Saturation", &wheelSettings.steeringSaturation, 0.01f, 0.1f, 2.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::Checkbox("Invert Steering", &wheelSettings.steeringInvert)) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Steering Deadzone", &wheelSettings.steeringDeadzone, 0.005f, 0.0f, 0.95f, "%.3f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Steering Response", &wheelSettings.steeringResponseExponent, 0.05f, 0.1f, 4.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Steering Min", &wheelSettings.steeringCalibrationMin, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Steering Center", &wheelSettings.steeringCalibrationCenter, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Steering Max", &wheelSettings.steeringCalibrationMax, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
        }

        if (ImGui::CollapsingHeader("Pedals", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Combined Pedals", &wheelSettings.combinedPedals)) {
                projectSettingsChanged = true;
            }

            ImGui::SeparatorText("Throttle");
            if (ImGui::Checkbox("Invert Throttle", &wheelSettings.throttleInvert)) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Throttle Deadzone", &wheelSettings.throttleDeadzone, 0.005f, 0.0f, 0.95f, "%.3f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Throttle Response", &wheelSettings.throttleResponseExponent, 0.05f, 0.1f, 4.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Throttle Min", &wheelSettings.throttleCalibrationMin, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Throttle Center", &wheelSettings.throttleCalibrationCenter, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Throttle Max", &wheelSettings.throttleCalibrationMax, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }

            ImGui::SeparatorText("Brake");
            if (ImGui::Checkbox("Invert Brake", &wheelSettings.brakeInvert)) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Brake Deadzone", &wheelSettings.brakeDeadzone, 0.005f, 0.0f, 0.95f, "%.3f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Brake Response", &wheelSettings.brakeResponseExponent, 0.05f, 0.1f, 4.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Brake Min", &wheelSettings.brakeCalibrationMin, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Brake Center", &wheelSettings.brakeCalibrationCenter, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Brake Max", &wheelSettings.brakeCalibrationMax, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }

            ImGui::SeparatorText("Clutch");
            if (ImGui::Checkbox("Invert Clutch", &wheelSettings.clutchInvert)) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Clutch Deadzone", &wheelSettings.clutchDeadzone, 0.005f, 0.0f, 0.95f, "%.3f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Clutch Response", &wheelSettings.clutchResponseExponent, 0.05f, 0.1f, 4.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Clutch Min", &wheelSettings.clutchCalibrationMin, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Clutch Center", &wheelSettings.clutchCalibrationCenter, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Clutch Max", &wheelSettings.clutchCalibrationMax, 0.01f, -1.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
        }

        if (ImGui::CollapsingHeader("Force Feedback", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Force Feedback", &wheelSettings.forceFeedbackEnabled)) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Overall Strength", &wheelSettings.forceFeedbackOverallStrength, 0.01f, 0.0f, 2.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Self Aligning Torque", &wheelSettings.forceFeedbackSelfAligningTorque, 0.01f, 0.0f, 2.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Road Effects", &wheelSettings.forceFeedbackRoadEffects, 0.01f, 0.0f, 2.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Slip Effects", &wheelSettings.forceFeedbackSlipEffects, 0.01f, 0.0f, 2.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Collision Effects", &wheelSettings.forceFeedbackCollisionEffects, 0.01f, 0.0f, 2.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Damper", &wheelSettings.forceFeedbackDamper, 0.01f, 0.0f, 2.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Friction", &wheelSettings.forceFeedbackFriction, 0.01f, 0.0f, 2.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Spring", &wheelSettings.forceFeedbackSpring, 0.01f, 0.0f, 2.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            if (ImGui::DragFloat("Minimum Force", &wheelSettings.forceFeedbackMinimumForce, 0.01f, 0.0f, 1.0f, "%.2f")) {
                projectSettingsChanged = true;
            }
            ImGui::TextDisabled("Windows play mode now claims wheel FFB ownership and disables driver auto-centering. Vehicle-driven FFB forces are still minimal.");
        }
    }

    if (ImGui::CollapsingHeader("Profiles", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (inputProfiles_.empty()) {
            InputProfile defaultProfile;
            defaultProfile.id = "default_vehicle";
            defaultProfile.displayName = "Default Vehicle";
            inputProfiles_.push_back(std::move(defaultProfile));
            projectSettingsChanged = true;
        }

        selectedInputProfileIndex_ = (std::max)(0, (std::min)(selectedInputProfileIndex_, static_cast<int>(inputProfiles_.size()) - 1));
        const char* preview = inputProfiles_[static_cast<std::size_t>(selectedInputProfileIndex_)].displayName.c_str();
        if (ImGui::BeginCombo("Profile", preview)) {
            for (int i = 0; i < static_cast<int>(inputProfiles_.size()); ++i) {
                const bool isSelected = i == selectedInputProfileIndex_;
                if (ImGui::Selectable(inputProfiles_[static_cast<std::size_t>(i)].displayName.c_str(), isSelected)) {
                    selectedInputProfileIndex_ = i;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Add Profile")) {
            InputProfile profile;
            profile.id = "profile_" + std::to_string(inputProfiles_.size() + 1);
            profile.displayName = "New Profile";
            inputProfiles_.push_back(std::move(profile));
            selectedInputProfileIndex_ = static_cast<int>(inputProfiles_.size()) - 1;
            projectSettingsChanged = true;
        }
        ImGui::SameLine();
        const bool canDeleteProfile = !inputProfiles_.empty() &&
            inputProfiles_[static_cast<std::size_t>(selectedInputProfileIndex_)].id != "default_vehicle" &&
            inputProfiles_[static_cast<std::size_t>(selectedInputProfileIndex_)].id != "default_character";
        if (!canDeleteProfile) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Delete Profile") && canDeleteProfile) {
            inputProfiles_.erase(inputProfiles_.begin() + selectedInputProfileIndex_);
            selectedInputProfileIndex_ = (std::max)(0, selectedInputProfileIndex_ - 1);
            projectSettingsChanged = true;
        }
        if (!canDeleteProfile) {
            ImGui::EndDisabled();
        }

        if (!inputProfiles_.empty()) {
            InputProfile& profile = inputProfiles_[static_cast<std::size_t>(selectedInputProfileIndex_)];
            char idBuffer[128]{};
            char nameBuffer[128]{};
            std::snprintf(idBuffer, sizeof(idBuffer), "%s", profile.id.c_str());
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", profile.displayName.c_str());
            if (ImGui::InputText("Profile Id", idBuffer, sizeof(idBuffer))) {
                profile.id = SanitizeAssetBaseName(idBuffer);
                projectSettingsChanged = true;
            }
            if (ImGui::InputText("Display Name", nameBuffer, sizeof(nameBuffer))) {
                profile.displayName = TrimCopyLocal(nameBuffer);
                projectSettingsChanged = true;
            }
            ImGui::Spacing();
            ImGui::SeparatorText("Keyboard Feel");
            ImGui::TextDisabled("0 is slow/smooth, 1 is instant. The monitor below uses real bound input.");
            float steeringSensitivity = NormalizeKeyboardSensitivity(profile.keyboardSteeringSensitivity);
            float throttleSensitivity = NormalizeKeyboardSensitivity(profile.keyboardThrottleSensitivity);
            float brakeSensitivity = NormalizeKeyboardSensitivity(profile.keyboardBrakeSensitivity);
            if (ImGui::SliderFloat("Steering Sensitivity", &steeringSensitivity, 0.0f, 1.0f, "%.2f")) {
                profile.keyboardSteeringSensitivity = steeringSensitivity;
                projectSettingsChanged = true;
            }
            if (ImGui::SliderFloat("Throttle Sensitivity", &throttleSensitivity, 0.0f, 1.0f, "%.2f")) {
                profile.keyboardThrottleSensitivity = throttleSensitivity;
                projectSettingsChanged = true;
            }
            if (ImGui::SliderFloat("Brake Sensitivity", &brakeSensitivity, 0.0f, 1.0f, "%.2f")) {
                profile.keyboardBrakeSensitivity = brakeSensitivity;
                projectSettingsChanged = true;
            }

            const float steeringRate = KeyboardSteeringSensitivityToRate(profile.keyboardSteeringSensitivity);
            const float throttleRate = KeyboardThrottleSensitivityToRate(profile.keyboardThrottleSensitivity);
            const float brakeRate = KeyboardBrakeSensitivityToRate(profile.keyboardBrakeSensitivity);
            ImGui::TextDisabled("Full response time: steering %.2fs, throttle %.2fs, brake %.2fs",
                1.0f / steeringRate,
                1.0f / throttleRate,
                1.0f / brakeRate);

            static float previewSteering = 0.0f;
            static float previewThrottle = 0.0f;
            static float previewBrake = 0.0f;
            const char* livePreviewDevices[] = {"Keyboard", "Gamepad", "Wheel", "Any"};
            const InputDevicePreference livePreviewPreferences[] = {
                InputDevicePreference::Keyboard,
                InputDevicePreference::Gamepad,
                InputDevicePreference::Wheel,
                InputDevicePreference::Any
            };
            projectInputTestDeviceIndex_ = (std::clamp)(projectInputTestDeviceIndex_, 0, static_cast<int>(std::size(livePreviewDevices)) - 1);
            ImGui::SetNextItemWidth(150.0f);
            ImGui::Combo("Monitor Device", &projectInputTestDeviceIndex_, livePreviewDevices, static_cast<int>(std::size(livePreviewDevices)));
            ImGui::SameLine();
            if (ImGui::Button(projectInputTestActive_ ? "Stop Input Test" : "Start Input Test")) {
                projectInputTestActive_ = !projectInputTestActive_;
                previewSteering = 0.0f;
                previewThrottle = 0.0f;
                previewBrake = 0.0f;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(projectInputTestActive_ ? "Receiving real input" : "Idle");

            const InputDevicePreference previewPreference = livePreviewPreferences[projectInputTestDeviceIndex_];
            const LiveInputSample steeringSample = projectInputTestActive_
                ? ResolveLiveInputSample(inputManager_, profile, "steer", previewPreference)
                : LiveInputSample{};
            const LiveInputSample throttleSample = projectInputTestActive_
                ? ResolveLiveInputSample(inputManager_, profile, "throttle", previewPreference)
                : LiveInputSample{};
            const LiveInputSample brakeSample = projectInputTestActive_
                ? ResolveLiveInputSample(inputManager_, profile, "brake", previewPreference)
                : LiveInputSample{};
            const LiveInputSample keyboardSteeringSample = projectInputTestActive_
                ? ResolveLiveInputSample(inputManager_, profile, "steer", InputDevicePreference::Keyboard)
                : LiveInputSample{};
            const LiveInputSample keyboardThrottleSample = projectInputTestActive_
                ? ResolveLiveInputSample(inputManager_, profile, "throttle", InputDevicePreference::Keyboard)
                : LiveInputSample{};
            const LiveInputSample keyboardBrakeSample = projectInputTestActive_
                ? ResolveLiveInputSample(inputManager_, profile, "brake", InputDevicePreference::Keyboard)
                : LiveInputSample{};
            const float liveSteering = steeringSample.value;
            const float liveThrottle = (std::max)(0.0f, throttleSample.value);
            const float liveBrake = (std::max)(0.0f, brakeSample.value);
            const bool keyboardMonitor =
                previewPreference == InputDevicePreference::Keyboard ||
                (previewPreference == InputDevicePreference::Any &&
                    (std::fabs(keyboardSteeringSample.value) > 0.001f ||
                     std::fabs(keyboardThrottleSample.value) > 0.001f ||
                     std::fabs(keyboardBrakeSample.value) > 0.001f));
            const float targetSteering = liveSteering;
            const float targetThrottle = liveThrottle;
            const float targetBrake = liveBrake;
            const float previewDeltaTime = (std::max)(ImGui::GetIO().DeltaTime, 1.0f / 240.0f);
            if (keyboardMonitor) {
                previewSteering = MoveTowardsPreview(
                    previewSteering,
                    targetSteering,
                    previewDeltaTime * (std::fabs(targetSteering) <= 0.001f ? steeringRate * 1.5f : steeringRate));
                previewThrottle = MoveTowardsPreview(
                    previewThrottle,
                    targetThrottle,
                    previewDeltaTime * (targetThrottle <= 0.001f ? throttleRate * 1.5f : throttleRate));
                previewBrake = MoveTowardsPreview(
                    previewBrake,
                    targetBrake,
                    previewDeltaTime * (targetBrake <= 0.001f ? brakeRate * 1.5f : brakeRate));
            } else {
                previewSteering = targetSteering;
                previewThrottle = targetThrottle;
                previewBrake = targetBrake;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Live Input Monitor");
            ImGui::TextDisabled("Bindings/devices: steer %d/%d, throttle %d/%d, brake %d/%d",
                steeringSample.matchingBindings,
                steeringSample.matchingDevices,
                throttleSample.matchingBindings,
                throttleSample.matchingDevices,
                brakeSample.matchingBindings,
                brakeSample.matchingDevices);
            RenderSteeringInputMeter("##LiveSteeringMeter", liveSteering, previewSteering);
            if (ImGui::BeginTable("LivePedalMeters", 2, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableNextColumn();
                RenderPedalInputMeter("##LiveThrottleMeter", "THR", liveThrottle, previewThrottle, IM_COL32(44, 196, 105, 245));
                ImGui::TableNextColumn();
                RenderPedalInputMeter("##LiveBrakeMeter", "BRK", liveBrake, previewBrake, IM_COL32(232, 73, 73, 245));
                ImGui::EndTable();
            }
            if (!keyboardMonitor) {
                ImGui::TextDisabled("Analog devices show direct output. Keyboard input shows smoothed output.");
            }
            if (!projectInputTestActive_) {
                ImGui::TextDisabled("Press Start Input Test, then use the bound steering/throttle/brake controls.");
            }

            if (profile.keyboardSteeringSensitivity != steeringSensitivity ||
                profile.keyboardThrottleSensitivity != throttleSensitivity ||
                profile.keyboardBrakeSensitivity != brakeSensitivity) {
                if (!projectSettingsChanged) {
                    profile.keyboardSteeringSensitivity = steeringSensitivity;
                    profile.keyboardThrottleSensitivity = throttleSensitivity;
                    profile.keyboardBrakeSensitivity = brakeSensitivity;
                    projectSettingsChanged = true;
                }
            }

            std::vector<std::string> actions = GatherProfileActions(profile);
            EnsureCommonActions(actions);

            if (ImGui::Button("Add Action")) {
                std::string baseAction = "action" + std::to_string(actions.size() + 1);
                std::string uniqueAction = baseAction;
                int suffix = 2;
                while (std::find(actions.begin(), actions.end(), uniqueAction) != actions.end()) {
                    uniqueAction = baseAction + std::to_string(suffix++);
                }
                profile.bindings.push_back(InputBinding{uniqueAction, InputDeviceType::Keyboard, InputBindingSource::Key});
                projectSettingsChanged = true;
            }
            ImGui::Spacing();
            selectedInputDevicePage_ = (std::max)(0, (std::min)(selectedInputDevicePage_, 2));
            if (ImGui::BeginTabBar("InputDevicePages")) {
                const InputDeviceType deviceTypes[] = {
                    InputDeviceType::Keyboard,
                    InputDeviceType::Gamepad,
                    InputDeviceType::Wheel
                };
                for (int devicePage = 0; devicePage < 3; ++devicePage) {
                    if (ImGui::BeginTabItem(InputDevicePageName(deviceTypes[devicePage]))) {
                        selectedInputDevicePage_ = devicePage;
                        const InputDeviceType activeDeviceType = deviceTypes[devicePage];
                        ImGui::SeparatorText(InputDevicePageName(activeDeviceType));

                        for (std::size_t actionIndex = 0; actionIndex < actions.size(); ++actionIndex) {
                            std::string& actionName = actions[actionIndex];
                            InputBinding* binding = FindBindingForAction(profile, activeDeviceType, actionName);
                            if (binding == nullptr) {
                                profile.bindings.push_back(InputBinding{actionName, activeDeviceType, activeDeviceType == InputDeviceType::Keyboard ? InputBindingSource::Key : InputBindingSource::Axis});
                                binding = &profile.bindings.back();
                                projectSettingsChanged = true;
                            }

                            ImGui::PushID((actionName + "_" + std::to_string(devicePage)).c_str());
                            if (ImGui::CollapsingHeader(actionName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                                char actionBuffer[128]{};
                                std::snprintf(actionBuffer, sizeof(actionBuffer), "%s", actionName.c_str());
                                if (ImGui::InputText("Action", actionBuffer, sizeof(actionBuffer))) {
                                    const std::string previousAction = actionName;
                                    actionName = TrimCopyLocal(actionBuffer);
                                    for (InputBinding& candidate : profile.bindings) {
                                        if (candidate.action == previousAction) {
                                            candidate.action = actionName;
                                        }
                                    }
                                    projectSettingsChanged = true;
                                }

                                if (ImGui::CollapsingHeader("Binding", ImGuiTreeNodeFlags_DefaultOpen)) {
                                    int sourceIndex = static_cast<int>(binding->source);
                                    if (activeDeviceType == InputDeviceType::Keyboard) {
                                        const char* sourceLabels[] = {"None", "Key", "Key Pair"};
                                        const int sourceMap[] = {
                                            static_cast<int>(InputBindingSource::None),
                                            static_cast<int>(InputBindingSource::Key),
                                            static_cast<int>(InputBindingSource::KeyPair)
                                        };
                                        int keyboardSourceIndex = 0;
                                        for (int i = 0; i < IM_ARRAYSIZE(sourceMap); ++i) {
                                            if (sourceMap[i] == sourceIndex) {
                                                keyboardSourceIndex = i;
                                                break;
                                            }
                                        }
                                        if (ImGui::Combo("Source", &keyboardSourceIndex, sourceLabels, IM_ARRAYSIZE(sourceLabels))) {
                                            binding->source = static_cast<InputBindingSource>(sourceMap[keyboardSourceIndex]);
                                            projectSettingsChanged = true;
                                        }
                                    } else {
                                        const char* sourceLabels[] = {"None", "Axis", "Button"};
                                        const int sourceMap[] = {
                                            static_cast<int>(InputBindingSource::None),
                                            static_cast<int>(InputBindingSource::Axis),
                                            static_cast<int>(InputBindingSource::Button)
                                        };
                                        int analogSourceIndex = 0;
                                        for (int i = 0; i < IM_ARRAYSIZE(sourceMap); ++i) {
                                            if (sourceMap[i] == sourceIndex) {
                                                analogSourceIndex = i;
                                                break;
                                            }
                                        }
                                        if (ImGui::Combo("Source", &analogSourceIndex, sourceLabels, IM_ARRAYSIZE(sourceLabels))) {
                                            binding->source = static_cast<InputBindingSource>(sourceMap[analogSourceIndex]);
                                            projectSettingsChanged = true;
                                        }
                                    }

                                    if (binding->source == InputBindingSource::Key) {
                                        if (ImGui::InputInt("Key", &binding->key)) {
                                            projectSettingsChanged = true;
                                        }
                                    } else if (binding->source == InputBindingSource::KeyPair) {
                                        if (ImGui::InputInt("Negative Key", &binding->negativeKey)) {
                                            projectSettingsChanged = true;
                                        }
                                        if (ImGui::InputInt("Positive Key", &binding->positiveKey)) {
                                            projectSettingsChanged = true;
                                        }
                                    } else if (binding->source == InputBindingSource::Axis) {
                                        if (activeDeviceType == InputDeviceType::Gamepad) {
                                            int axisIndex = (std::max)(0, (std::min)(binding->axis, 5));
                                            const char* axisNames[] = {
                                                "Left Stick X",
                                                "Left Stick Y",
                                                "Right Stick X",
                                                "Right Stick Y",
                                                "Left Trigger",
                                                "Right Trigger"
                                            };
                                            if (ImGui::Combo("Axis", &axisIndex, axisNames, IM_ARRAYSIZE(axisNames))) {
                                                binding->axis = axisIndex;
                                                projectSettingsChanged = true;
                                            }
                                        } else if (ImGui::InputInt("Axis", &binding->axis)) {
                                            projectSettingsChanged = true;
                                        }
                                    } else if (binding->source == InputBindingSource::Button) {
                                        if (activeDeviceType == InputDeviceType::Gamepad) {
                                            int buttonIndex = (std::max)(0, (std::min)(binding->button, 14));
                                            const char* buttonNames[] = {
                                                "A / Cross",
                                                "B / Circle",
                                                "X / Square",
                                                "Y / Triangle",
                                                "Left Bumper",
                                                "Right Bumper",
                                                "Back / Share",
                                                "Start / Options",
                                                "Guide / PS",
                                                "Left Stick Click",
                                                "Right Stick Click",
                                                "D-Pad Up",
                                                "D-Pad Right",
                                                "D-Pad Down",
                                                "D-Pad Left"
                                            };
                                            if (ImGui::Combo("Button", &buttonIndex, buttonNames, IM_ARRAYSIZE(buttonNames))) {
                                                binding->button = buttonIndex;
                                                projectSettingsChanged = true;
                                            }
                                        } else if (ImGui::InputInt("Button", &binding->button)) {
                                            projectSettingsChanged = true;
                                        }
                                    }

                                    if (inputManager_ != nullptr) {
                                        const bool canListenForBinding =
                                            binding->source == InputBindingSource::Key ||
                                            binding->source == InputBindingSource::Axis ||
                                            binding->source == InputBindingSource::Button;

                                        if (!canListenForBinding) {
                                            ImGui::TextDisabled("Listen is available for single key, axis, and button bindings.");
                                        } else if (!inputManager_->IsListeningForBinding()) {
                                            if (ImGui::Button("Listen For Input")) {
                                                inputManager_->StartListeningForBinding(activeDeviceType, binding->source);
                                            }
                                        } else {
                                            ImGui::TextDisabled("Listening for next input...");
                                            ImGui::SameLine();
                                            if (ImGui::Button("Cancel Listen")) {
                                                inputManager_->CancelListeningForBinding();
                                            }
                                        }

                                        InputBinding capturedBinding;
                                        if (canListenForBinding && inputManager_->ConsumeCapturedBinding(capturedBinding)) {
                                            if (capturedBinding.deviceType == activeDeviceType &&
                                                capturedBinding.source == binding->source) {
                                                capturedBinding.action = binding->action;
                                                capturedBinding.deadzone = binding->deadzone;
                                                capturedBinding.calibrationMin = binding->calibrationMin;
                                                capturedBinding.calibrationCenter = binding->calibrationCenter;
                                                capturedBinding.calibrationMax = binding->calibrationMax;
                                                capturedBinding.responseExponent = binding->responseExponent;
                                                *binding = capturedBinding;
                                                projectSettingsChanged = true;
                                            }
                                        }
                                    }

                                    if (activeDeviceType == InputDeviceType::Gamepad) {
                                        if (binding->source == InputBindingSource::Button && binding->button >= 0) {
                                            ImGui::TextDisabled("Selected: %s", GamepadButtonName(binding->button));
                                        } else if (binding->source == InputBindingSource::Axis && binding->axis >= 0) {
                                            ImGui::TextDisabled("Selected: %s", GamepadAxisName(binding->axis));
                                        }
                                    }
                                }

                                const bool showSettings = binding->source == InputBindingSource::Axis;
                                if (showSettings && ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                                    if (ImGui::Checkbox("Invert", &binding->invert)) {
                                        projectSettingsChanged = true;
                                    }
                                    if (ImGui::DragFloat("Deadzone", &binding->deadzone, 0.005f, 0.0f, 0.95f, "%.3f")) {
                                        projectSettingsChanged = true;
                                    }
                                    if (ImGui::DragFloat("Response", &binding->responseExponent, 0.05f, 0.1f, 4.0f, "%.2f")) {
                                        projectSettingsChanged = true;
                                    }

                                    const InputDeviceInfo* liveDevice = FindFirstDeviceOfType(inputManager_, activeDeviceType);
                                    const bool hasLiveAxis = liveDevice != nullptr &&
                                        binding->axis >= 0 &&
                                        binding->axis < static_cast<int>(liveDevice->axes.size());
                                    if (hasLiveAxis) {
                                        const float rawAxis = liveDevice->axes[static_cast<std::size_t>(binding->axis)];
                                        ImGui::TextDisabled("Raw Axis Value: %.3f", rawAxis);
                                        if (ImGui::Button("Capture Min")) {
                                            binding->calibrationMin = rawAxis;
                                            projectSettingsChanged = true;
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::Button("Capture Center")) {
                                            binding->calibrationCenter = rawAxis;
                                            projectSettingsChanged = true;
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::Button("Capture Max")) {
                                            binding->calibrationMax = rawAxis;
                                            projectSettingsChanged = true;
                                        }
                                    } else {
                                        ImGui::TextDisabled("Connect a matching device to calibrate this axis live.");
                                    }
                                }

                                if (ImGui::Button("Remove Action")) {
                                    const std::string removedAction = binding->action;
                                    profile.bindings.erase(
                                        std::remove_if(profile.bindings.begin(), profile.bindings.end(), [&](const InputBinding& candidate) {
                                            return candidate.action == removedAction;
                                        }),
                                        profile.bindings.end());
                                    projectSettingsChanged = true;
                                    ImGui::PopID();
                                    break;
                                }
                            }
                            ImGui::PopID();
                        }

                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }
    }

    if (projectSettingsChanged) {
        SyncInputProfiles(inputManager_, inputProfiles_);
        SyncWheelSettingsProfiles(inputManager_, wheelSettingsProfiles_);
        SaveProject();
    } else if (selectedInputProfileIndex_ != previousSelectedInputProfileIndex ||
               selectedInputDevicePage_ != previousSelectedInputDevicePage ||
               selectedWheelSettingsProfileIndex_ != previousSelectedWheelSettingsProfileIndex ||
               projectInputTestActive_ != previousProjectInputTestActive ||
               projectInputTestDeviceIndex_ != previousProjectInputTestDeviceIndex) {
        SaveProject();
    }
}


void SceneEditor::ImportObj(const std::string& path) {
    if (path.empty()) {
        return;
    }
    ModelImportSettings importSettings;
    std::string importPath = NormalizeSlashes(path);
    try {
        importPath = ToProjectAssetPath(ProjectAssetPathToAbsolute(path), FindAssetsRoot());
    } catch (...) {
        importPath = NormalizeSlashes(path);
    }
    LoadModelImportSettings(importPath, importSettings);
    ImportObjWithOptions(path, importSettings.pivotMode);
}

void SceneEditor::ImportObjWithOptions(const std::string& path, int pivotMode) {
    namespace fs = std::filesystem;
    if (path.empty()) return;
    try {
        std::string importPath;
        std::shared_ptr<::Model> model;
        std::vector<ImportedMeshInfo> infos;
        if (!TryLoadMeshAsset(path, importPath, model, infos)) {
            return;
        }
        std::string baseName;
        try { baseName = fs::path(importPath).stem().string(); } catch (...) { baseName = "Mesh"; }
        materialManager_.LoadAll();
        ModelImportSettings importSettings;
        LoadModelImportSettings(importPath, importSettings);
        importSettings.pivotMode = pivotMode;
        ReconcileModelImportSettings(importSettings, importPath, baseName, infos);
        PushUndoState();
        const bool centerPivot = pivotMode == 1;
        std::string packageRootId;
        int firstImportedIndex = -1;
        if (infos.size() > 1) {
            SceneObject packageRoot;
            packageRoot.id = MakeId("gameobject");
            packageRoot.name = baseName;
            packageRoot.type = "GameObject";
            packageRoot.transform.position = {0.0f, 0.0f, 0.0f};
            packageRoot.transform.rotationEuler = {0.0f, 0.0f, 0.0f};
            packageRoot.transform.scale = {1.0f, 1.0f, 1.0f};
            packageRoot.hasMeshFilter = false;
            packageRoot.hasMeshRenderer = false;
            packageRoot.hasScriptComponent = false;
            packageRoot.hasRigidbody = false;
            packageRoot.hasVehicle = false;
            packageRoot.hasCharacterController = false;
            packageRoot.hasBoxCollider = false;
            packageRoot.hasSphereCollider = false;
            packageRoot.hasCapsuleCollider = false;
            packageRoot.hasPlaneCollider = false;
            packageRoot.hasMeshCollider = false;
            packageRoot.hasCamera = false;
            packageRoot.hasLight = false;
            packageRootId = packageRoot.id;
            objects_.push_back(std::move(packageRoot));
            firstImportedIndex = static_cast<int>(objects_.size()) - 1;
        }

        for (size_t i = 0; i < infos.size(); ++i) {
            const auto& info = infos[i];
            const std::string meshBaseName = baseName + (infos.size() > 1 ? ("_" + std::to_string(i)) : "");
            const glm::vec3 meshCenter = (info.localBoundsMin + info.localBoundsMax) * 0.5f;

            SceneObject o;
            o.id = MakeId("mesh");
            o.name = meshBaseName;
            o.type = "GameObject";
            o.parentId = packageRootId;
            o.transform.position = centerPivot ? meshCenter : glm::vec3{0.0f, 0.0f, 0.0f};
            o.transform.rotationEuler = {0.0f, 0.0f, 0.0f};
            o.transform.scale = {1.0f, 1.0f, 1.0f};
            o.hasMeshRenderer = true;
            o.meshRenderer.color = {1.0f, 1.0f, 1.0f, 1.0f};
            o.meshFilter.sourcePath = importPath;
            if (centerPivot) {
                o.meshFilter.pivotOffset = meshCenter;
            }
            ApplyMeshInfoToSceneObject(o, info, model);
            ModelMaterialMapping& mapping = EnsureModelMaterialMapping(importSettings, importPath, baseName, info);
            if (!materialManager_.Exists(mapping.materialId)) {
                mapping.materialId = CreateOrUpdateImportedMaterialAsset(materialManager_, importPath, baseName, info, false);
            }
            o.meshRenderer.materialId = mapping.materialId;
            ModelCollisionMapping& collisionMapping = EnsureModelCollisionMapping(importSettings, info);
            ApplyModelCollisionMappingToSceneObject(o, collisionMapping);
            if (collisionMapping.autoBake && collisionMapping.mode != 0) {
                PhysicsWorld::BakeCollisionShape(BuildModelCollisionDesc(importPath, info, collisionMapping));
            }
            objects_.push_back(std::move(o));
            if (firstImportedIndex < 0) {
                firstImportedIndex = static_cast<int>(objects_.size()) - 1;
            }
        }

        if (firstImportedIndex >= 0) {
            SaveModelImportSettings(importPath, importSettings);
            materialManager_.LoadAll();
            Select(firstImportedIndex);
            if (console_) {
                console_->AddLog("Imported mesh: " + importPath + " (" + std::to_string(infos.size()) + " mesh" + (infos.size() != 1 ? "es" : "") + ")");
            }
            RefreshProjectFiles();
            if (onDirty_) onDirty_();
        }
    } catch (const std::exception&) {
        // ignore
    }
}

bool SceneEditor::ReplaceSelectedMeshFromObj(const std::string& path) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size()) || path.empty()) {
        return false;
    }

    try {
        std::string importPath;
        std::shared_ptr<::Model> model;
        std::vector<ImportedMeshInfo> infos;
        if (!TryLoadMeshAsset(path, importPath, model, infos)) {
            return false;
        }

        PushUndoState();
        SceneObject& obj = objects_[selectedIndex_];
        obj.type = "Mesh";
        obj.hasMeshFilter = true;
        obj.hasMeshRenderer = true;
        obj.meshFilter.sourcePath = importPath;
        ApplyMeshInfoToSceneObject(obj, infos.front(), model);
        std::string baseName;
        try { baseName = fs::path(importPath).stem().string(); } catch (...) { baseName = "Mesh"; }
        materialManager_.LoadAll();
        ModelImportSettings importSettings;
        LoadModelImportSettings(importPath, importSettings);
        ReconcileModelImportSettings(importSettings, importPath, baseName, infos);
        ModelMaterialMapping& mapping = EnsureModelMaterialMapping(importSettings, importPath, baseName, infos.front());
        if (!materialManager_.Exists(mapping.materialId)) {
            mapping.materialId = CreateOrUpdateImportedMaterialAsset(materialManager_, importPath, baseName, infos.front(), false);
        }
        obj.meshRenderer.materialId = mapping.materialId;
        ModelCollisionMapping& collisionMapping = EnsureModelCollisionMapping(importSettings, infos.front());
        ApplyModelCollisionMappingToSceneObject(obj, collisionMapping);
        if (collisionMapping.autoBake && collisionMapping.mode != 0) {
            PhysicsWorld::BakeCollisionShape(BuildModelCollisionDesc(importPath, infos.front(), collisionMapping));
        }
        SaveModelImportSettings(importPath, importSettings);
        materialManager_.LoadAll();

        if (console_) {
            console_->AddLog("Replaced Mesh Filter on " + obj.name + " with " + importPath);
        }
        RefreshProjectFiles();
        if (onDirty_) onDirty_();
        return true;
    } catch (...) {
        if (console_) {
            console_->AddLog("Failed to replace Mesh Filter with mesh asset: " + path);
        }
        return false;
    }
}

bool SceneEditor::ReplaceSelectedMeshWithPlane() {
    return ReplaceSelectedMeshWithBuiltIn("Plane");
}

bool SceneEditor::ReplaceSelectedMeshWithBuiltIn(const std::string& meshType) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size()) || meshType.empty()) {
        return false;
    }

    PushUndoState();
    SceneObject& obj = objects_[selectedIndex_];
    obj.type = "GameObject";
    obj.hasMeshFilter = true;
    obj.hasMeshRenderer = true;
    if (!ConfigureBuiltInPrimitive(obj, meshType, builtInPrimitiveMeshes_)) {
        return false;
    }
    if (console_) {
        console_->AddLog("Replaced Mesh Filter on " + obj.name + " with built-in " + meshType);
    }
    if (onDirty_) onDirty_();
    return true;
}

void SceneEditor::BeginProjectFileRename(const std::string& path) {
    if (path.empty()) {
        return;
    }
    SelectProjectFile(path);
    renamingProjectFile_ = path;
    std::string editableName = fs::path(path).filename().string();
    if (IsSceneAssetPath(path)) {
        editableName = ProjectAssetDisplayFilename(path);
        const std::string displaySuffix = ".scene";
        if (EndsWith(ToLowerCopy(editableName), displaySuffix)) {
            editableName.resize(editableName.size() - displaySuffix.size());
        }
    } else if (IsTrackAssetPath(path)) {
        editableName = ProjectAssetDisplayFilename(path);
        const std::string displaySuffix = ".track";
        if (EndsWith(ToLowerCopy(editableName), displaySuffix)) {
            editableName.resize(editableName.size() - displaySuffix.size());
        }
    } else if (IsMaterialAssetPath(path)) {
        editableName = MaterialIdFromAssetPath(path);
    }
    std::snprintf(projectRenameBuffer_, sizeof(projectRenameBuffer_), "%s", editableName.c_str());
    focusProjectRename_ = true;
}

void SceneEditor::CommitProjectFileRename() {
    if (renamingProjectFile_.empty()) {
        return;
    }

    const std::string oldProjectPath = NormalizeSlashes(renamingProjectFile_);
    std::string newFilename = TrimCopyLocal(projectRenameBuffer_);
    if (newFilename.empty()) {
        renamingProjectFile_.clear();
        return;
    }
    if (IsSceneAssetPath(oldProjectPath)) {
        const std::string sceneSuffix = ".scene";
        const std::string storageSuffix = ".scene.json";
        if (EndsWith(ToLowerCopy(newFilename), storageSuffix)) {
            // Keep explicit full names working.
        } else {
            if (EndsWith(ToLowerCopy(newFilename), sceneSuffix)) {
                newFilename.resize(newFilename.size() - sceneSuffix.size());
            }
            newFilename += storageSuffix;
        }
    } else if (IsTrackAssetPath(oldProjectPath)) {
        const std::string displaySuffix = ".track";
        const std::string storageSuffix = ".track.json";
        if (EndsWith(ToLowerCopy(newFilename), storageSuffix)) {
            // Keep explicit full names working.
        } else {
            if (EndsWith(ToLowerCopy(newFilename), displaySuffix)) {
                newFilename.resize(newFilename.size() - displaySuffix.size());
            }
            newFilename += storageSuffix;
        }
    } else if (IsMaterialAssetPath(oldProjectPath)) {
        const std::string displaySuffix = ".mat";
        const std::string storageSuffix = ".mat.json";
        if (EndsWith(ToLowerCopy(newFilename), storageSuffix)) {
            // Keep explicit full names working.
        } else {
            if (EndsWith(ToLowerCopy(newFilename), displaySuffix)) {
                newFilename.resize(newFilename.size() - displaySuffix.size());
            }
            newFilename += storageSuffix;
        }
    }

    const fs::path oldAbsolutePath = ProjectAssetPathToAbsolute(oldProjectPath);
    const bool oldWasDirectory = fs::exists(oldAbsolutePath) && fs::is_directory(oldAbsolutePath);
    const fs::path newAbsolutePath = (oldAbsolutePath.parent_path() / fs::path(newFilename)).lexically_normal();

    const fs::path assetsRoot = FindAssetsRoot();
    if (!IsUnderPath(newAbsolutePath, assetsRoot)) {
        if (console_) {
            console_->AddError("Rename blocked outside assets: " + newFilename);
        }
        renamingProjectFile_.clear();
        return;
    }

    const std::string newProjectPath = ToProjectAssetPath(newAbsolutePath, assetsRoot);
    if (NormalizeSlashes(newProjectPath) == oldProjectPath) {
        renamingProjectFile_.clear();
        return;
    }

    try {
        if (fs::exists(newAbsolutePath)) {
            if (console_) {
                console_->AddError("Project file already exists: " + newProjectPath);
            }
            renamingProjectFile_.clear();
            return;
        }

        fs::rename(oldAbsolutePath, newAbsolutePath);
        MoveModelImportSettingsFile(oldProjectPath, newProjectPath);

        if (oldWasDirectory) {
            const std::string oldPrefix = oldProjectPath + "/";
            const std::string newPrefix = newProjectPath + "/";
            auto rewriteProjectPath = [&](std::string& value) {
                value = NormalizeSlashes(value);
                if (value == oldProjectPath) {
                    value = newProjectPath;
                } else if (value.rfind(oldPrefix, 0) == 0) {
                    value = newPrefix + value.substr(oldPrefix.size());
                }
            };

            rewriteProjectPath(defaultScenePath_);
            rewriteProjectPath(lastScenePath_);
            rewriteProjectPath(savePath_);
            rewriteProjectPath(selectedProjectDirectory_);
            rewriteProjectPath(selectedProjectFile_);
            for (auto& object : objects_) {
                rewriteProjectPath(object.meshFilter.sourcePath);
                rewriteProjectPath(object.meshFilter.diffuseTexturePath);
                rewriteProjectPath(object.vehicle.configPath);
                for (ObjectScriptAttachment& script : object.scriptComponent.attachments) {
                    rewriteProjectPath(script.scriptPath);
                }
            }
            materialManager_.LoadAll();
            SyncScriptProjectFiles();
        } else if (IsSceneAssetPath(oldProjectPath) || IsSceneAssetPath(newProjectPath)) {
            UpdateProjectSceneReference(oldProjectPath, IsSceneAssetPath(newProjectPath) ? newProjectPath : std::string{});
        }

        const bool renamedScript = ToLowerCopy(fs::path(newProjectPath).extension().string()) == ".cpp"
            || ToLowerCopy(fs::path(newProjectPath).extension().string()) == ".h";
        if (renamedScript) {
            SyncScriptProjectFiles();
        }

        const std::string oldMaterialId = MaterialIdFromAssetPath(oldProjectPath);
        const std::string newMaterialId = MaterialIdFromAssetPath(newProjectPath);
        if (IsMaterialAssetPath(oldProjectPath) && IsMaterialAssetPath(newProjectPath)) {
            for (auto& object : objects_) {
                if (object.meshRenderer.materialId == oldMaterialId) {
                    object.meshRenderer.materialId = newMaterialId;
                }
            }
            if (inspectedMaterialId_ == oldMaterialId) {
                inspectedMaterialId_ = newMaterialId;
            }
            materialManager_.LoadAll();
        }

        for (auto& object : objects_) {
            if (!oldWasDirectory && NormalizeSlashes(object.meshFilter.sourcePath) == oldProjectPath) {
                object.meshFilter.sourcePath = newProjectPath;
            }
            if (!oldWasDirectory && NormalizeSlashes(object.meshFilter.diffuseTexturePath) == oldProjectPath) {
                object.meshFilter.diffuseTexturePath = newProjectPath;
            }
            if (!oldWasDirectory && NormalizeSlashes(object.vehicle.configPath) == oldProjectPath) {
                object.vehicle.configPath = newProjectPath;
            }
        }

        if (console_) {
            console_->AddLog("Renamed project file: " + oldProjectPath + " -> " + newProjectPath);
        }
        selectedProjectFile_ = newProjectPath;
        renamingProjectFile_.clear();
        RefreshProjectFiles();
        if (onDirty_) onDirty_();
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to rename project file: " + oldProjectPath);
        }
        renamingProjectFile_.clear();
    }
}

void SceneEditor::DeleteProjectFile(const std::string& path) {
    if (path.empty()) {
        return;
    }

    const std::string projectPath = NormalizeSlashes(path);
    const fs::path absolutePath = ProjectAssetPathToAbsolute(projectPath);
    const std::string materialId = MaterialIdFromAssetPath(projectPath);
    const bool isSceneAsset = IsSceneAssetPath(projectPath);
    const bool isScriptAsset = ParentProjectDirectory(projectPath) == "assets/scripts"
        && (ToLowerCopy(fs::path(projectPath).extension().string()) == ".cpp"
            || ToLowerCopy(fs::path(projectPath).extension().string()) == ".h");

    try {
        if (!fs::exists(absolutePath) || !fs::is_regular_file(absolutePath)) {
            return;
        }

        fs::remove(absolutePath);
        DeleteModelImportSettingsFile(projectPath);

        if (isSceneAsset) {
            UpdateProjectSceneReference(projectPath, "");
        }

        if (isScriptAsset) {
            const std::string scriptName = absolutePath.stem().string();
            for (auto& object : objects_) {
                object.scriptComponent.attachments.erase(
                    std::remove_if(object.scriptComponent.attachments.begin(), object.scriptComponent.attachments.end(), [&](const ObjectScriptAttachment& script) {
                        return script.scriptName == scriptName || NormalizeSlashes(script.scriptPath) == projectPath;
                    }),
                    object.scriptComponent.attachments.end());
            }
            SyncScriptProjectFiles();
            if (scriptsRunning_) {
                RebuildScriptRuntime();
            }
        }

        if (IsMaterialAssetPath(projectPath)) {
            for (auto& object : objects_) {
                if (object.meshRenderer.materialId == materialId) {
                    object.meshRenderer.materialId = "pbr_default";
                }
            }
            if (inspectedMaterialId_ == materialId) {
                inspectMaterial_ = false;
                inspectedMaterialId_.clear();
            }
            materialManager_.LoadAll();
        }

        if (IsShaderGraphAssetPath(projectPath)) {
            const std::string graphShaderId = ShaderRegistry::MakeGraphShaderId(projectPath);
            for (auto& object : objects_) {
                Material* material = materialManager_.Get(object.meshRenderer.materialId);
                if (material != nullptr && material->shader == graphShaderId) {
                    material->shader = "pbr";
                    materialManager_.Save(object.meshRenderer.materialId, *material);
                }
            }
            if (inspectedShaderGraphPath_ == projectPath) {
                showShaderGraphEditor_ = false;
                inspectedShaderGraphPath_.clear();
            }
            materialManager_.LoadAll();
        }

        for (auto& object : objects_) {
            if (NormalizeSlashes(object.meshFilter.sourcePath) == projectPath) {
                object.meshFilter.sourcePath.clear();
                object.meshFilter.vao = 0;
                object.meshFilter.indexCount = 0;
                object.meshFilter.modelRef.reset();
            }
            if (NormalizeSlashes(object.meshFilter.diffuseTexturePath) == projectPath) {
                object.meshFilter.diffuseTexturePath.clear();
                object.meshFilter.diffuseTextureId = 0;
            }
            if (NormalizeSlashes(object.vehicle.configPath) == projectPath) {
                object.vehicle.configPath.clear();
                object.vehicle.wheelBindings.clear();
            }
        }

        if (console_) {
            console_->AddLog("Deleted project file: " + projectPath);
        }
        if (renamingProjectFile_ == projectPath) {
            renamingProjectFile_.clear();
        }
        if (selectedProjectFile_ == projectPath) {
            selectedProjectFile_.clear();
        }
        RefreshProjectFiles();
        if (onDirty_) onDirty_();
        if (isScriptAsset) {
            Save(savePath_);
        }
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to delete project file: " + projectPath);
        }
    }
}

void SceneEditor::DeleteProjectFolder(const std::string& path) {
    const std::string projectPath = NormalizeSlashes(path);
    if (projectPath.empty() || projectPath == "assets") {
        return;
    }

    try {
        const fs::path assetsRoot = FindAssetsRoot();
        const fs::path absolutePath = ProjectAssetPathToAbsolute(projectPath);
        if (!IsUnderPath(absolutePath, assetsRoot) || !fs::exists(absolutePath) || !fs::is_directory(absolutePath)) {
            return;
        }
        if (!fs::is_empty(absolutePath)) {
            if (console_) {
                console_->AddError("Folder is not empty: " + projectPath);
            }
            return;
        }

        fs::remove(absolutePath);
        if (selectedProjectDirectory_ == projectPath) {
            selectedProjectDirectory_ = ParentProjectDirectory(projectPath);
        }
        if (selectedProjectFile_ == projectPath) {
            selectedProjectFile_.clear();
        }
        if (renamingProjectFile_ == projectPath) {
            renamingProjectFile_.clear();
        }
        RefreshProjectFiles();
        SaveProject();
        if (console_) {
            console_->AddLog("Deleted folder: " + projectPath);
        }
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to delete folder: " + projectPath);
        }
    }
}

bool SceneEditor::CreateProjectFolder(const std::string& requestedName) {
    std::string folderName = SanitizeFolderName(requestedName);
    if (folderName.empty()) {
        if (console_) {
            console_->AddError("Folder name cannot be empty.");
        }
        return false;
    }

    try {
        const fs::path assetsRoot = FindAssetsRoot();
        const fs::path baseDirectory = ProjectAssetPathToAbsolute(selectedProjectDirectory_);
        fs::path folderPath = (baseDirectory / fs::path(folderName)).lexically_normal();
        if (!IsUnderPath(folderPath, assetsRoot)) {
            if (console_) {
                console_->AddError("Folder creation blocked outside assets: " + folderName);
            }
            return false;
        }

        fs::path uniqueFolderPath = folderPath;
        for (int i = 1; fs::exists(uniqueFolderPath) && i < 10000; ++i) {
            uniqueFolderPath = (baseDirectory / fs::path(folderName + "_" + std::to_string(i))).lexically_normal();
        }
        if (fs::exists(uniqueFolderPath)) {
            if (console_) {
                console_->AddError("Folder already exists: " + ToProjectAssetPath(uniqueFolderPath, assetsRoot));
            }
            return false;
        }

        fs::create_directories(uniqueFolderPath);
        selectedProjectDirectory_ = ToProjectAssetPath(uniqueFolderPath, assetsRoot);
        RefreshProjectFiles();
        SaveProject();
        if (console_) {
            console_->AddLog("Created folder: " + selectedProjectDirectory_);
        }
        return true;
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to create folder: " + folderName);
        }
        return false;
    }
}

bool SceneEditor::MoveProjectFile(const std::string& path, const std::string& targetDirectory) {
    const std::string oldProjectPath = NormalizeSlashes(path);
    const std::string targetProjectDirectory = NormalizeSlashes(targetDirectory.empty() ? std::string("assets") : targetDirectory);
    if (oldProjectPath.empty() || ParentProjectDirectory(oldProjectPath) == targetProjectDirectory) {
        return false;
    }

    try {
        const fs::path assetsRoot = FindAssetsRoot();
        const fs::path oldAbsolutePath = ProjectAssetPathToAbsolute(oldProjectPath);
        const fs::path targetAbsoluteDirectory = ProjectAssetPathToAbsolute(targetProjectDirectory);
        const fs::path newAbsolutePath = (targetAbsoluteDirectory / oldAbsolutePath.filename()).lexically_normal();
        const std::string oldExtension = ToLowerCopy(oldAbsolutePath.extension().string());
        const bool movedScript = oldExtension == ".cpp" || oldExtension == ".h";
        const fs::path oldScriptSiblingPath = movedScript
            ? (oldAbsolutePath.parent_path() / (oldAbsolutePath.stem().string() + (oldExtension == ".h" ? ".cpp" : ".h"))).lexically_normal()
            : fs::path{};
        const fs::path newScriptSiblingPath = movedScript
            ? (targetAbsoluteDirectory / oldScriptSiblingPath.filename()).lexically_normal()
            : fs::path{};
        if (!IsUnderPath(newAbsolutePath, assetsRoot) || !fs::exists(oldAbsolutePath) || !fs::is_regular_file(oldAbsolutePath)) {
            return false;
        }
        if (fs::exists(newAbsolutePath)) {
            if (console_) {
                console_->AddError("Move blocked because file already exists: " + ToProjectAssetPath(newAbsolutePath, assetsRoot));
            }
            return false;
        }
        if (movedScript && fs::exists(oldScriptSiblingPath) && fs::exists(newScriptSiblingPath)) {
            if (console_) {
                console_->AddError("Move blocked because script pair target already exists: " + ToProjectAssetPath(newScriptSiblingPath, assetsRoot));
            }
            return false;
        }

        fs::create_directories(targetAbsoluteDirectory);
        fs::rename(oldAbsolutePath, newAbsolutePath);
        const std::string newProjectPath = ToProjectAssetPath(newAbsolutePath, assetsRoot);
        MoveModelImportSettingsFile(oldProjectPath, newProjectPath);
        if (movedScript && fs::exists(oldScriptSiblingPath) && IsUnderPath(newScriptSiblingPath, assetsRoot)) {
            fs::rename(oldScriptSiblingPath, newScriptSiblingPath);
        }

        const std::string oldScriptSourceProjectPath = movedScript
            ? ToProjectAssetPath(oldExtension == ".cpp" ? oldAbsolutePath : oldScriptSiblingPath, assetsRoot)
            : std::string{};
        const std::string newScriptSourceProjectPath = movedScript
            ? ToProjectAssetPath(oldExtension == ".cpp" ? newAbsolutePath : newScriptSiblingPath, assetsRoot)
            : std::string{};
        if (IsSceneAssetPath(oldProjectPath) || IsSceneAssetPath(newProjectPath)) {
            UpdateProjectSceneReference(oldProjectPath, IsSceneAssetPath(newProjectPath) ? newProjectPath : std::string{});
        }

        if (movedScript) {
            const fs::path movedHeaderPath = oldExtension == ".h" ? newAbsolutePath : newScriptSiblingPath;
            if (!movedHeaderPath.empty() && fs::exists(movedHeaderPath)) {
                std::string header;
                {
                    std::ifstream in(movedHeaderPath);
                    if (in.good()) {
                        std::stringstream buffer;
                        buffer << in.rdbuf();
                        header = buffer.str();
                    }
                }
                if (!header.empty()) {
                    const std::string objectScriptInclude = NormalizeSlashes(fs::relative(FindEngineRoot() / "src" / "scripting" / "ObjectScript.h", movedHeaderPath.parent_path()).string());
                    const std::string includeLine = "#include \"" + objectScriptInclude + "\"";
                    if (header.find("#include \"") != std::string::npos && header.find("src/scripting/ObjectScript.h") != std::string::npos) {
                        std::stringstream in(header);
                        std::string line;
                        std::string rewritten;
                        while (std::getline(in, line)) {
                            if (line.find("src/scripting/ObjectScript.h") != std::string::npos) {
                                rewritten += includeLine;
                            } else {
                                rewritten += line;
                            }
                            rewritten += "\n";
                        }
                        fs::create_directories(movedHeaderPath.parent_path());
                        std::ofstream out(movedHeaderPath, std::ios::trunc);
                        out << rewritten;
                    }
                }
            }
            SyncScriptProjectFiles();
        }

        if (IsMaterialAssetPath(oldProjectPath) && IsMaterialAssetPath(newProjectPath)) {
            materialManager_.LoadAll();
        }

        for (auto& object : objects_) {
            if (NormalizeSlashes(object.meshFilter.sourcePath) == oldProjectPath) {
                object.meshFilter.sourcePath = newProjectPath;
            }
            if (NormalizeSlashes(object.meshFilter.diffuseTexturePath) == oldProjectPath) {
                object.meshFilter.diffuseTexturePath = newProjectPath;
            }
            if (NormalizeSlashes(object.vehicle.configPath) == oldProjectPath) {
                object.vehicle.configPath = newProjectPath;
            }
            for (ObjectScriptAttachment& script : object.scriptComponent.attachments) {
                const std::string normalizedScriptPath = NormalizeSlashes(script.scriptPath);
                if (normalizedScriptPath == oldProjectPath || (movedScript && normalizedScriptPath == oldScriptSourceProjectPath)) {
                    script.scriptPath = movedScript ? newScriptSourceProjectPath : newProjectPath;
                }
            }
        }

        selectedProjectDirectory_ = targetProjectDirectory;
        selectedProjectFile_ = movedScript && oldExtension == ".h" && fs::exists(newScriptSiblingPath) ? newScriptSourceProjectPath : newProjectPath;
        RefreshProjectFiles();
        SaveProject();
        if (console_) {
            console_->AddLog("Moved project file: " + oldProjectPath + " -> " + newProjectPath);
        }
        if (onDirty_) onDirty_();
        return true;
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to move project file: " + oldProjectPath);
        }
        return false;
    }
}


bool SceneEditor::CopyProjectFileTo(const std::string& sourcePath, const std::string& targetDirectory) {
    const std::string srcProjectPath = NormalizeSlashes(sourcePath);
    const std::string targetProjectDir = NormalizeSlashes(targetDirectory.empty() ? std::string("assets") : targetDirectory);
    if (srcProjectPath.empty()) return false;

    try {
        const fs::path assetsRoot  = FindAssetsRoot();
        const fs::path srcAbsolute = ProjectAssetPathToAbsolute(srcProjectPath);
        const fs::path targetAbsDir = ProjectAssetPathToAbsolute(targetProjectDir);

        if (!IsUnderPath(srcAbsolute, assetsRoot) || !fs::exists(srcAbsolute) || !fs::is_regular_file(srcAbsolute)) {
            return false;
        }

        // Build a unique destination filename: "stem (1).ext", "stem (2).ext", …
        const std::string stem = srcAbsolute.stem().string();
        const std::string ext  = srcAbsolute.extension().string();
        fs::create_directories(targetAbsDir);

        fs::path destAbsolute = targetAbsDir / srcAbsolute.filename();
        if (fs::exists(destAbsolute)) {
            for (int n = 1; ; ++n) {
                destAbsolute = targetAbsDir / (stem + " (" + std::to_string(n) + ")" + ext);
                if (!fs::exists(destAbsolute)) break;
            }
        }

        if (!IsUnderPath(destAbsolute, assetsRoot)) return false;

        fs::copy_file(srcAbsolute, destAbsolute);

        const std::string destProjectPath = ToProjectAssetPath(destAbsolute, assetsRoot);
        CopyModelImportSettingsFile(srcProjectPath, destProjectPath);
        selectedProjectDirectory_ = targetProjectDir;
        selectedProjectFile_ = destProjectPath;
        RefreshProjectFiles();
        if (console_) console_->AddLog("Copied " + srcProjectPath + " -> " + destProjectPath);
        return true;
    } catch (...) {
        if (console_) console_->AddError("Failed to copy project file: " + srcProjectPath);
        return false;
    }
}

void SceneEditor::RefreshProjectFiles() {
    projectDirectories_.clear();
    projectFiles_.clear();
    materialManager_.LoadAll();

    try {
        const fs::path assetsRoot = FindAssetsRoot();
        if (!fs::exists(assetsRoot) || !fs::is_directory(assetsRoot)) {
            return;
        }

        projectDirectories_.push_back("assets");
        for (const auto& entry : fs::recursive_directory_iterator(assetsRoot)) {
            if (entry.is_directory()) {
                projectDirectories_.push_back(ToProjectAssetPath(entry.path(), assetsRoot));
                continue;
            }
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string path = ToProjectAssetPath(entry.path(), assetsRoot);
            if (IsModelImportSettingsPath(path)) {
                continue;
            }
            projectFiles_.push_back(path);
        }
        std::sort(projectDirectories_.begin(), projectDirectories_.end(), [](const std::string& a, const std::string& b) {
            return ToLowerCopy(a) < ToLowerCopy(b);
        });
        projectDirectories_.erase(std::unique(projectDirectories_.begin(), projectDirectories_.end()), projectDirectories_.end());

        std::sort(projectFiles_.begin(), projectFiles_.end(), [](const std::string& a, const std::string& b) {
            const bool aSpecial = IsSceneAssetPath(a) || IsMeshAssetPath(a) || IsMaterialAssetPath(a) || IsShaderGraphAssetPath(a);
            const bool bSpecial = IsSceneAssetPath(b) || IsMeshAssetPath(b) || IsMaterialAssetPath(b) || IsShaderGraphAssetPath(b);
            if (aSpecial != bSpecial) {
                return aSpecial > bSpecial;
            }
            return ToLowerCopy(a) < ToLowerCopy(b);
        });

        if (std::find(projectDirectories_.begin(), projectDirectories_.end(), selectedProjectDirectory_) == projectDirectories_.end()) {
            selectedProjectDirectory_ = "assets";
        }
        if (!selectedProjectFile_.empty() &&
            std::find(projectFiles_.begin(), projectFiles_.end(), selectedProjectFile_) == projectFiles_.end() &&
            std::find(projectDirectories_.begin(), projectDirectories_.end(), selectedProjectFile_) == projectDirectories_.end()) {
            selectedProjectFile_.clear();
        }
    } catch (...) {
        projectDirectories_.clear();
        projectFiles_.clear();
        selectedProjectDirectory_ = "assets";
        selectedProjectFile_.clear();
    }
}

void SceneEditor::ScanObjDir(const std::string& dir) {
    objFiles_.clear();
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (IsSupportedMeshExtension(ext)) {
                std::filesystem::path p = entry.path();
                objFiles_.push_back(p.lexically_relative(dir).string());
            }
        }
        std::sort(objFiles_.begin(), objFiles_.end());
    } catch (...) {
        // ignore
    }
}
} // namespace raceman


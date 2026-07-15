#include "SceneEditorVehicleInput.h"

#include "SceneEditorInternal.h"
#include "../input/InputManager.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace raceman {
using namespace scene_editor_internal;

namespace {

float MoveTowards(float current, float target, float maxDelta) {
    if (current < target) {
        return (std::min)(current + maxDelta, target);
    }
    return (std::max)(current - maxDelta, target);
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

float ResolveKeyboardAxis(const InputManager& inputManager, const InputProfile& profile, std::string_view action) {
    float bestMagnitude = 0.0f;
    float resolved = 0.0f;
    for (const InputBinding& binding : profile.bindings) {
        if (binding.action != action || binding.deviceType != InputDeviceType::Keyboard) {
            continue;
        }

        float value = 0.0f;
        if (binding.source == InputBindingSource::Key) {
            value = binding.key >= 0 && inputManager.IsKeyDown(binding.key) ? 1.0f : 0.0f;
        } else if (binding.source == InputBindingSource::KeyPair) {
            const float negative = binding.negativeKey >= 0 && inputManager.IsKeyDown(binding.negativeKey) ? -1.0f : 0.0f;
            const float positive = binding.positiveKey >= 0 && inputManager.IsKeyDown(binding.positiveKey) ? 1.0f : 0.0f;
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

} // namespace

void SceneEditor::CaptureVehicleRuntimeInputActions(bool routeInput) {
    for (RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles_) {
        if (!routeInput ||
            inputManager_ == nullptr ||
            runtimeVehicle.objectIndex < 0 ||
            runtimeVehicle.objectIndex >= static_cast<int>(objects_.size())) {
            runtimeVehicle.pendingShiftUp = false;
            runtimeVehicle.pendingShiftDown = false;
            runtimeVehicle.pendingNeutral = false;
            runtimeVehicle.pendingReverse = false;
            continue;
        }

        const SceneObject& vehicleObject = objects_[runtimeVehicle.objectIndex];
        const std::string profileId = vehicleObject.vehicle.inputProfileId.empty()
            ? std::string("default_vehicle")
            : vehicleObject.vehicle.inputProfileId;
        runtimeVehicle.pendingShiftUp = runtimeVehicle.pendingShiftUp ||
            inputManager_->WasActionPressedForProfile(profileId, "shiftUp",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
        runtimeVehicle.pendingShiftDown = runtimeVehicle.pendingShiftDown ||
            inputManager_->WasActionPressedForProfile(profileId, "shiftDown",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
        runtimeVehicle.pendingNeutral = runtimeVehicle.pendingNeutral ||
            inputManager_->WasActionPressedForProfile(profileId, "neutral",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
        runtimeVehicle.pendingReverse = runtimeVehicle.pendingReverse ||
            inputManager_->WasActionPressedForProfile(profileId, "reverse",
                vehicleObject.vehicle.preferredInputDevice,
                vehicleObject.vehicle.preferredInputDeviceId);
    }
}

void ConsumePendingVehicleGearActions(RuntimeVehicleInstance& runtimeVehicle) {
    runtimeVehicle.pendingShiftUp = false;
    runtimeVehicle.pendingShiftDown = false;
    runtimeVehicle.pendingNeutral = false;
    runtimeVehicle.pendingReverse = false;
}

ArcadeVehicleInput SampleArcadeVehicleInput(RuntimeVehicleInstance& runtimeVehicle,
                                            const SceneObject& vehicleObject,
                                            InputManager* inputManager,
                                            const std::string& profileId,
                                            bool routeInput,
                                            float deltaTime) {
    ArcadeVehicleInput input{};
    if (routeInput && inputManager != nullptr) {
        input.steering = inputManager->GetAxisForProfile(profileId, "steer",
            vehicleObject.vehicle.preferredInputDevice,
            vehicleObject.vehicle.preferredInputDeviceId);
        input.handbrake = inputManager->GetAxisForProfile(profileId, "handbrake",
            vehicleObject.vehicle.preferredInputDevice,
            vehicleObject.vehicle.preferredInputDeviceId);
        input.throttle = inputManager->GetAxisForProfile(profileId, "throttle",
            vehicleObject.vehicle.preferredInputDevice,
            vehicleObject.vehicle.preferredInputDeviceId);
        input.brake = inputManager->GetAxisForProfile(profileId, "brake",
            vehicleObject.vehicle.preferredInputDevice,
            vehicleObject.vehicle.preferredInputDeviceId);

        const InputProfile* activeProfile = inputManager->FindProfile(profileId);
        if (activeProfile == nullptr) {
            activeProfile = inputManager->FindProfile("default_vehicle");
        }
        if (activeProfile != nullptr &&
            (vehicleObject.vehicle.preferredInputDevice == InputDevicePreference::Any ||
             vehicleObject.vehicle.preferredInputDevice == InputDevicePreference::Keyboard)) {
            const float keyboardSteer = ResolveKeyboardAxis(*inputManager, *activeProfile, "steer");
            const float keyboardThrottle = (std::max)(0.0f, ResolveKeyboardAxis(*inputManager, *activeProfile, "throttle"));
            const float keyboardBrake = (std::max)(0.0f, ResolveKeyboardAxis(*inputManager, *activeProfile, "brake"));
            const float steeringRate = KeyboardSteeringSensitivityToRate(activeProfile->keyboardSteeringSensitivity);
            const float throttleRate = KeyboardThrottleSensitivityToRate(activeProfile->keyboardThrottleSensitivity);
            const float brakeRate = KeyboardBrakeSensitivityToRate(activeProfile->keyboardBrakeSensitivity);
            runtimeVehicle.smoothedKeyboardSteering = MoveTowards(
                runtimeVehicle.smoothedKeyboardSteering,
                keyboardSteer,
                deltaTime * (keyboardSteer == 0.0f ? steeringRate * 1.5f : steeringRate));
            runtimeVehicle.smoothedKeyboardThrottle = MoveTowards(
                runtimeVehicle.smoothedKeyboardThrottle,
                keyboardThrottle,
                deltaTime * (keyboardThrottle == 0.0f ? throttleRate * 1.35f : throttleRate));
            runtimeVehicle.smoothedKeyboardBrake = MoveTowards(
                runtimeVehicle.smoothedKeyboardBrake,
                keyboardBrake,
                deltaTime * (keyboardBrake == 0.0f ? brakeRate * 1.35f : brakeRate));

            if (std::fabs(keyboardSteer) > 0.0f || std::fabs(runtimeVehicle.smoothedKeyboardSteering) > 0.0001f) {
                input.steering = runtimeVehicle.smoothedKeyboardSteering;
            }
            if (keyboardThrottle > 0.0f || runtimeVehicle.smoothedKeyboardThrottle > 0.0001f) {
                input.throttle = runtimeVehicle.smoothedKeyboardThrottle;
            }
            if (keyboardBrake > 0.0f || runtimeVehicle.smoothedKeyboardBrake > 0.0001f) {
                input.brake = runtimeVehicle.smoothedKeyboardBrake;
            }
        } else {
            runtimeVehicle.smoothedKeyboardSteering = 0.0f;
            runtimeVehicle.smoothedKeyboardThrottle = 0.0f;
            runtimeVehicle.smoothedKeyboardBrake = 0.0f;
        }
    } else {
        runtimeVehicle.smoothedKeyboardSteering = 0.0f;
        runtimeVehicle.smoothedKeyboardThrottle = 0.0f;
        runtimeVehicle.smoothedKeyboardBrake = 0.0f;
    }

    input.steering = std::clamp(input.steering, -1.0f, 1.0f);
    input.throttle = std::clamp(input.throttle, 0.0f, 1.0f);
    input.brake = std::clamp(input.brake, 0.0f, 1.0f);
    input.handbrake = std::clamp(input.handbrake, 0.0f, 1.0f);
    return input;
}

} // namespace raceman

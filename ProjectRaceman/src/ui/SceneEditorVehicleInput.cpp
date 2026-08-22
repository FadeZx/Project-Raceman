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

// Wheel presets own the feel of the steering axis itself: filtering, the
// speed-dependent lock reduction and, for devices that cannot centre
// themselves, the return rate.
void ApplyWheelSteeringFeel(RuntimeVehicleInstance& runtimeVehicle,
                            const SceneObject& vehicleObject,
                            InputManager& inputManager,
                            ArcadeVehicleInput& input,
                            float deltaTime) {
    if (vehicleObject.vehicle.preferredInputDevice == InputDevicePreference::Keyboard ||
        vehicleObject.vehicle.preferredInputDevice == InputDevicePreference::Gamepad) {
        runtimeVehicle.smoothedWheelSteeringInitialized = false;
        return;
    }

    const InputDeviceInfo* wheelDevice = inputManager.FindPrimaryWheelDevice();
    if (wheelDevice == nullptr) {
        runtimeVehicle.smoothedWheelSteeringInitialized = false;
        return;
    }
    const WheelSettingsProfile* settings = inputManager.FindWheelSettingsForDevice(*wheelDevice);
    if (settings == nullptr) {
        return;
    }

    if (settings->steeringSpeedSensitivity > 0.0f) {
        const float maxSpeed = (std::max)(1.0f, runtimeVehicle.config.arcadeHandling.maxForwardSpeed);
        const float speedRatio = (std::clamp)(std::fabs(runtimeVehicle.arcadeSpeed) / maxSpeed, 0.0f, 1.0f);
        input.steering *= 1.0f - (std::clamp)(settings->steeringSpeedSensitivity, 0.0f, 0.9f) * speedRatio;
    }

    if (!runtimeVehicle.smoothedWheelSteeringInitialized) {
        runtimeVehicle.smoothedWheelSteering = input.steering;
        runtimeVehicle.smoothedWheelSteeringInitialized = true;
    }

    // A device without force feedback holds whatever position it is left in,
    // so an optional return rate walks the input back to centre for it. Wheels
    // with force feedback centre physically and must never be fought here.
    if (settings->steeringReturnRate > 0.0f && !settings->forceFeedbackEnabled) {
        runtimeVehicle.smoothedWheelSteering = MoveTowards(
            runtimeVehicle.smoothedWheelSteering, input.steering, deltaTime * settings->steeringReturnRate);
        input.steering = runtimeVehicle.smoothedWheelSteering;
        return;
    }

    const float smoothing = (std::clamp)(settings->steeringSmoothing, 0.0f, 0.95f);
    if (smoothing > 0.0f) {
        // Time-constant filter so the feel does not change with frame rate.
        const float blend = (std::clamp)(deltaTime / (std::max)(0.001f, smoothing * 0.12f), 0.0f, 1.0f);
        runtimeVehicle.smoothedWheelSteering += (input.steering - runtimeVehicle.smoothedWheelSteering) * blend;
        input.steering = runtimeVehicle.smoothedWheelSteering;
    } else {
        runtimeVehicle.smoothedWheelSteering = input.steering;
    }
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

VehicleGearActions ConsumePendingVehicleGearActions(RuntimeVehicleInstance& runtimeVehicle) {
    VehicleGearActions actions;
    actions.shiftUp = runtimeVehicle.pendingShiftUp;
    actions.shiftDown = runtimeVehicle.pendingShiftDown;
    actions.neutral = runtimeVehicle.pendingNeutral;
    actions.reverse = runtimeVehicle.pendingReverse;
    runtimeVehicle.pendingShiftUp = false;
    runtimeVehicle.pendingShiftDown = false;
    runtimeVehicle.pendingNeutral = false;
    runtimeVehicle.pendingReverse = false;
    return actions;
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

        bool keyboardSteeringActive = false;
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
                keyboardSteeringActive = true;
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

        // Only shape the axis when it is actually coming from the wheel;
        // keyboard steering already has its own ramp.
        if (!keyboardSteeringActive) {
            ApplyWheelSteeringFeel(runtimeVehicle, vehicleObject, *inputManager, input, deltaTime);
        } else {
            runtimeVehicle.smoothedWheelSteeringInitialized = false;
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

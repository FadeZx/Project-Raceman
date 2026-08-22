#pragma once

#include "SceneEditorVehicleRuntime.h"
#include "SceneEditorTypes.h"

namespace raceman {

struct ArcadeVehicleInput {
    float throttle{0.0f};
    float brake{0.0f};
    float steering{0.0f};
    float handbrake{0.0f};
};

struct VehicleSurfaceSample {
    float gripMultiplier{1.0f};
    float rollingDrag{0.0f};
    float wheelGripFactor{1.0f};
};

struct VehicleDriveRatios {
    float rearDrivenRatio{1.0f};
    float drivenRatio{1.0f};
    float differentialLock{0.0f};
};

struct VehicleControlAmounts {
    float throttle{0.0f};
    float brake{0.0f};
};

// Tractive effort the engine is making right now, normalised so a mid-gear pull
// at the torque peak is 1.0. Shared rather than duplicated: the tyre model has
// to ask the same question the handling model does, or a wheel would spin up
// from drive the car is not actually making in this gear at this speed.
float EngineDriveTorqueScale(const raceman::physics::VehicleConfig& config,
                             int gear,
                             float rpm,
                             float absSpeed,
                             float maxForwardSpeed,
                             float shiftCooldown);

VehicleDriveRatios ComputeVehicleDriveRatios(const RuntimeVehicleInstance& runtimeVehicle, float rawThrottleAmount);

VehicleControlAmounts ApplyVehicleDriverAids(RuntimeVehicleInstance& runtimeVehicle,
                                             float rawThrottleAmount,
                                             float rawBrakeAmount,
                                             float absSpeedBeforeDrive,
                                             const VehicleDriveRatios& driveRatios,
                                             float deltaTime);

void ApplyArcadeVehicleDynamics(RuntimeVehicleInstance& runtimeVehicle,
                                const ArcadeVehicleInput& input,
                                const VehicleControlAmounts& controls,
                                const VehicleSurfaceSample& surfaceSample,
                                const VehicleDriveRatios& driveRatios,
                                bool routeInput,
                                float previousSpeed,
                                float previousThrottleInput,
                                float deltaTime);

} // namespace raceman

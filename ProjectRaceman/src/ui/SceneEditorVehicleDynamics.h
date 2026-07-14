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

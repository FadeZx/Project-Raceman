#pragma once

#include "SceneEditorTypes.h"
#include "SceneEditorVehicleDynamics.h"
#include "SceneEditorVehicleRuntime.h"

#include <string>

namespace raceman {

class InputManager;

// One frame of discrete gearbox requests. Edge triggered: each press is
// reported once and cleared, so a shift can never be applied twice by the
// fixed step running more than once in a rendered frame.
struct VehicleGearActions {
    bool shiftUp{false};
    bool shiftDown{false};
    bool neutral{false};
    bool reverse{false};
};

VehicleGearActions ConsumePendingVehicleGearActions(RuntimeVehicleInstance& runtimeVehicle);

ArcadeVehicleInput SampleArcadeVehicleInput(RuntimeVehicleInstance& runtimeVehicle,
                                            const SceneObject& vehicleObject,
                                            InputManager* inputManager,
                                            const std::string& profileId,
                                            bool routeInput,
                                            float deltaTime);

} // namespace raceman

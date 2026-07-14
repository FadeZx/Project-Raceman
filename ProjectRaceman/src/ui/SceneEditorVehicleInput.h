#pragma once

#include "SceneEditorTypes.h"
#include "SceneEditorVehicleDynamics.h"
#include "SceneEditorVehicleRuntime.h"

#include <string>

namespace raceman {

class InputManager;

void ConsumePendingVehicleGearActions(RuntimeVehicleInstance& runtimeVehicle);

ArcadeVehicleInput SampleArcadeVehicleInput(RuntimeVehicleInstance& runtimeVehicle,
                                            const SceneObject& vehicleObject,
                                            InputManager* inputManager,
                                            const std::string& profileId,
                                            bool routeInput,
                                            float deltaTime);

} // namespace raceman

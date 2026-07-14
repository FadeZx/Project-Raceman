#pragma once

#include "SceneEditorVehicleRuntime.h"
#include "../physics/VehicleConfig.h"

namespace raceman {

void RenderVehicleRuntimeDebugPanel(const raceman::physics::VehicleConfig& loadedConfig,
                                    const RuntimeVehicleInstance& runtimeVehicle);

} // namespace raceman

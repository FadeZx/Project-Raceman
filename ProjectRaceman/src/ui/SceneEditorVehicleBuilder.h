#pragma once

#include "SceneEditorTypes.h"
#include "SceneEditorVehicleRuntime.h"

#include <functional>
#include <string>

#include <glm/glm.hpp>

namespace raceman {

using VehicleBuilderFindObjectIndex = std::function<int(const std::string&)>;
using VehicleBuilderGetObjectWorldMatrix = std::function<glm::mat4(int)>;

raceman::physics::VehicleConfig BuildDefaultJoltVehicleConfig();
void EnsureDrivableVehicleConfig(raceman::physics::VehicleConfig& config);

void RebindRuntimeVehicleWheels(RuntimeVehicleInstance& runtimeVehicle,
                                const SceneObject& vehicleObject,
                                raceman::physics::VehicleConfig& config,
                                const glm::mat4& vehicleWorldMatrix,
                                const VehicleBuilderFindObjectIndex& findObjectIndexById,
                                const VehicleBuilderGetObjectWorldMatrix& getObjectWorldMatrix);

} // namespace raceman

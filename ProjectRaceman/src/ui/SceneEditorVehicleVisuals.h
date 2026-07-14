#pragma once

#include "SceneEditorTypes.h"
#include "SceneEditorVehicleRuntime.h"

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace raceman {

using VehicleFindObjectIndex = std::function<int(const std::string&)>;
using VehicleGetObjectWorldMatrix = std::function<glm::mat4(int)>;
using VehicleObjectEnabledQuery = std::function<bool(int)>;

void UpdateVehicleVisuals(std::vector<SceneObject>& objects,
                          std::vector<RuntimeVehicleInstance>& runtimeVehicles,
                          const VehicleFindObjectIndex& findObjectIndexById,
                          const VehicleGetObjectWorldMatrix& getObjectWorldMatrix,
                          const VehicleObjectEnabledQuery& isObjectEnabled,
                          float renderAlpha);

} // namespace raceman

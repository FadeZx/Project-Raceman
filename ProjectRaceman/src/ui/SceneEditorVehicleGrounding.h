#pragma once

#include "SceneEditorTypes.h"
#include "SceneEditorVehicleRuntime.h"

#include <functional>
#include <string>
#include <unordered_set>

namespace raceman {

class PhysicsWorld;

using VehicleSurfaceLookup = std::function<ColliderSurfaceConfig(const std::string& objectId)>;

void ApplyArcadeVehicleGrounding(RuntimeVehicleInstance& runtimeVehicle,
                                 PhysicsWorld* physicsWorld,
                                 const std::unordered_set<std::string>& ignoredObjectIds,
                                 const ColliderSurfaceConfig& defaultSurface,
                                 const VehicleSurfaceLookup& surfaceLookup,
                                 float steeringInput,
                                 float speedFactorBeforeDrive,
                                 float deltaTime);

} // namespace raceman

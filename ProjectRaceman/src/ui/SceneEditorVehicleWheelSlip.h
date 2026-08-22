#pragma once

#include "SceneEditorVehicleDynamics.h"
#include "SceneEditorVehicleRuntime.h"

namespace raceman {

// Resolves what each tyre is doing this step: its own slip angle, its own slip
// ratio, and how fast its contact patch is actually sliding.
//
// Runs after grounding, because it needs the contact load, normal and surface
// that grounding resolves. It does not feed force back into the chassis - the
// arcade handling model still moves the car - but it is derived from the same
// speed, yaw rate, steering, throttle and brake the handling model used, so it
// never disagrees with what the car is visibly doing. Every cosmetic system
// reads these fields instead of re-deriving slip from chassis averages, which
// is what keeps marks, sound and force feedback on the same instant.
void UpdateArcadeWheelSlip(RuntimeVehicleInstance& runtimeVehicle,
                           const ArcadeVehicleInput& input,
                           const VehicleControlAmounts& controls,
                           const VehicleDriveRatios& driveRatios,
                           float deltaTime);

} // namespace raceman

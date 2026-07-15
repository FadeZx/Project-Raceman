#include "SceneEditorVehicleValidation.h"

#include "Console.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace raceman {

namespace {

void AddIssue(std::vector<VehicleConfigValidationIssue>& issues,
              VehicleConfigValidationSeverity severity,
              std::string message) {
    issues.push_back({severity, std::move(message)});
}

int CountDrivenWheels(const raceman::physics::VehicleConfig& config) {
    int count = 0;
    for (const raceman::physics::WheelConfig& wheel : config.wheels) {
        if (wheel.driven) {
            ++count;
        }
    }
    return count;
}

int CountBrakeWheels(const raceman::physics::VehicleConfig& config) {
    int count = 0;
    for (const raceman::physics::WheelConfig& wheel : config.wheels) {
        if (wheel.hasBrake) {
            ++count;
        }
    }
    return count;
}

float AverageWheelGrip(const raceman::physics::VehicleConfig& config) {
    if (config.wheels.empty()) {
        return 0.0f;
    }
    float total = 0.0f;
    for (const raceman::physics::WheelConfig& wheel : config.wheels) {
        const raceman::physics::ResolvedWheelTireConfig tire = raceman::physics::resolveWheelTire(config, wheel);
        total += (std::max)(0.0f, tire.gripFactor);
    }
    return total / static_cast<float>(config.wheels.size());
}

} // namespace

std::vector<VehicleConfigValidationIssue> ValidateVehicleConfigForTuning(const raceman::physics::VehicleConfig& config) {
    std::vector<VehicleConfigValidationIssue> issues;

    if (config.name.empty()) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Vehicle profile name is empty.");
    }
    if (config.wheels.empty()) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "Vehicle has no wheels.");
    } else if (config.wheels.size() < 4) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Vehicle has fewer than 4 wheels; ground contact and axle slip may be incomplete.");
    }

    const int drivenWheels = CountDrivenWheels(config);
    if (drivenWheels <= 0) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "No driven wheels are configured.");
    }
    if (CountBrakeWheels(config) <= 0) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "No brake wheels are configured.");
    }

    for (const raceman::physics::WheelConfig& wheel : config.wheels) {
        const std::string prefix = wheel.name.empty() ? std::string("Wheel") : ("Wheel '" + wheel.name + "'");
        if (wheel.radius <= 0.05f) {
            AddIssue(issues, VehicleConfigValidationSeverity::Error, prefix + " radius is too small.");
        }
        if (wheel.mass <= 0.0f || wheel.inertia <= 0.0f) {
            AddIssue(issues, VehicleConfigValidationSeverity::Warning, prefix + " mass or inertia is non-positive.");
        }
        const raceman::physics::ResolvedWheelTireConfig tire = raceman::physics::resolveWheelTire(config, wheel);
        if (tire.gripFactor <= 0.1f) {
            AddIssue(issues, VehicleConfigValidationSeverity::Warning, prefix + " grip factor is very low.");
        }
        if (wheel.maxBrakingTorque <= 0.0f && wheel.hasBrake) {
            AddIssue(issues, VehicleConfigValidationSeverity::Warning, prefix + " has brakes enabled but zero brake torque.");
        }
    }

    if (config.chassis.mass < 200.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Chassis mass is very low for a car; acceleration and collision response may feel unstable.");
    }
    if (config.chassis.yawInertia <= 0.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "Chassis yaw inertia must be positive.");
    }

    if (config.engine.idleRPM <= 0.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "Engine idle RPM must be positive.");
    }
    if (config.engine.redlineRPM <= config.engine.idleRPM + 100.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "Engine redline RPM must be comfortably above idle RPM.");
    }
    if (config.arcadeHandling.redlineRPM <= config.arcadeHandling.idleRPM + 100.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "Arcade handling redline RPM must be above idle RPM.");
    }
    if (config.transmission.gearRatios.empty()) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "Transmission has no forward gear ratios.");
    }
    if (std::fabs(config.transmission.finalDriveRatio) <= 0.01f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "Transmission final drive ratio is near zero.");
    }

    if (config.arcadeHandling.maxForwardSpeed <= 1.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "Max forward speed is too low.");
    }
    if (config.arcadeHandling.acceleration <= 0.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "Acceleration must be positive.");
    }
    if (config.arcadeHandling.brakeDeceleration <= 0.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Error, "Brake deceleration must be positive.");
    } else if (config.arcadeHandling.acceleration > config.arcadeHandling.brakeDeceleration * 0.85f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Acceleration is close to brake deceleration; braking may feel weak versus engine power.");
    }
    if (config.arcadeHandling.lowSpeedSteerFloor < 0.25f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Low-speed steer floor is low; slow steering may feel too heavy.");
    }
    if (config.arcadeHandling.highSpeedSteerCut > 0.85f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "High-speed steer cut is high; steering may feel muted at speed.");
    }

    if (config.tireGrip.lateralGrip < 2.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Lateral tire grip is low; vehicle may skate sideways.");
    }
    if (config.tireGrip.minTractionScale < 0.15f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Minimum traction scale is very low; slides may become hard to recover.");
    }
    if (AverageWheelGrip(config) < 0.65f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Average wheel grip is low for asphalt racing.");
    }

    if (config.tireDynamics.slideFriction < 1.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Slide friction is low; slides can feel like they preserve too much energy.");
    }
    if (config.tireDynamics.tireScrub < 1.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Tire scrub is low; sideways slip may feel frictionless.");
    }
    if (config.tireDynamics.velocityAlignmentRate < 0.45f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Velocity alignment is low; steering response can feel delayed and heavy.");
    }
    if (config.tireDynamics.velocityAlignmentRate > 3.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Velocity alignment is high; slide recovery may snap too quickly.");
    }
    if (config.tireDynamics.yawInertiaScale > 2.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Yaw inertia scale is high; counter-steer may feel delayed.");
    }
    if (config.tireDynamics.yawDrag > 5.0f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Yaw drag is high; steering rotation can feel damped.");
    }
    if (config.tireDynamics.rearGripBias < config.tireDynamics.frontGripBias * 0.78f) {
        AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Rear grip bias is much lower than front grip; frequent oversteer is expected.");
    }

    if (config.yawDynamics.enabled) {
        if (config.yawDynamics.maxYawRate < 70.0f) {
            AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Max yaw rate is low; vehicle may refuse to rotate into oversteer.");
        }
        if (config.yawDynamics.spinSlipAngle <= config.tireGrip.slipAngleLimit + 5.0f) {
            AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Spin slip angle is close to tire slip limit; spins may start too early.");
        }
    }

    if (config.groundContact.enabled) {
        if (config.groundContact.probeUp <= 0.0f || config.groundContact.extraProbeLength <= 0.0f) {
            AddIssue(issues, VehicleConfigValidationSeverity::Error, "Ground contact probe lengths must be positive.");
        }
        if (config.groundContact.heightSmoothing <= 0.0f || config.groundContact.tiltSmoothing <= 0.0f) {
            AddIssue(issues, VehicleConfigValidationSeverity::Warning, "Ground contact smoothing should be positive.");
        }
    }

    return issues;
}

void LogVehicleConfigValidationIssues(Console* console,
                                      const std::string& context,
                                      const raceman::physics::VehicleConfig& config,
                                      bool logWhenClean) {
    if (console == nullptr) {
        return;
    }

    const std::vector<VehicleConfigValidationIssue> issues = ValidateVehicleConfigForTuning(config);
    if (issues.empty()) {
        if (logWhenClean) {
            console->AddLog("Vehicle config validation passed: " + context);
        }
        return;
    }

    for (const VehicleConfigValidationIssue& issue : issues) {
        const std::string message = "Vehicle config " + context + ": " + issue.message;
        if (issue.severity == VehicleConfigValidationSeverity::Error) {
            console->AddError(message);
        } else {
            console->AddWarning(message);
        }
    }
}

} // namespace raceman

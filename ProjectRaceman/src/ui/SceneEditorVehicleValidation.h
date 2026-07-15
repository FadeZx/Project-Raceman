#pragma once

#include "../physics/VehicleConfig.h"

#include <string>
#include <vector>

namespace raceman {

class Console;

enum class VehicleConfigValidationSeverity {
    Warning,
    Error
};

struct VehicleConfigValidationIssue {
    VehicleConfigValidationSeverity severity{VehicleConfigValidationSeverity::Warning};
    std::string message;
};

std::vector<VehicleConfigValidationIssue> ValidateVehicleConfigForTuning(const raceman::physics::VehicleConfig& config);
void LogVehicleConfigValidationIssues(Console* console,
                                      const std::string& context,
                                      const raceman::physics::VehicleConfig& config,
                                      bool logWhenClean = false);

} // namespace raceman

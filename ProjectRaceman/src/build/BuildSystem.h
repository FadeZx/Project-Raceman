#pragma once

#include <string>

namespace raceman {

struct BuildResult {
    bool success{false};
    std::string message;
};

BuildResult BuildStandaloneGame(const std::string& outputDirectory, const std::string& projectRoot);

} // namespace raceman

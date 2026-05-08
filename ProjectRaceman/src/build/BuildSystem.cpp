#include "BuildSystem.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace raceman {
namespace {

fs::path FindRepoRoot() {
    fs::path current = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        if (fs::exists(current / "tools" / "build-game.ps1") &&
            fs::exists(current / "ProjectRaceman" / "Project Raceman.sln")) {
            return current;
        }
        if (fs::exists(current / "Project Raceman.sln") &&
            fs::exists(current.parent_path() / "tools" / "build-game.ps1")) {
            return current.parent_path();
        }
        if (!current.has_parent_path() || current.parent_path() == current) {
            break;
        }
        current = current.parent_path();
    }
    return fs::current_path();
}

std::string QuoteCommandPath(const fs::path& path) {
    std::string value = path.string();
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
}

} // namespace

BuildResult BuildStandaloneGame(const std::string& outputDirectory) {
    if (outputDirectory.empty()) {
        return {false, "Build cancelled: no output folder selected."};
    }

    const fs::path repoRoot = FindRepoRoot();
    const fs::path scriptPath = repoRoot / "tools" / "build-game.ps1";
    if (!fs::exists(scriptPath)) {
        return {false, "Build helper not found: " + scriptPath.string()};
    }

    std::ostringstream command;
    command << "powershell -NoProfile -ExecutionPolicy Bypass -File "
            << QuoteCommandPath(scriptPath)
            << " -OutputPath " << QuoteCommandPath(fs::path(outputDirectory))
            << " -Configuration Release"
            << " -Platform x64";

    const int result = std::system(command.str().c_str());
    if (result != 0) {
        return {false, "Standalone build failed. Check the console output for details."};
    }

    return {true, "Standalone build completed: " + fs::path(outputDirectory).string()};
}

} // namespace raceman

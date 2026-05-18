#include "BuildSystem.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace raceman {
namespace {

fs::path FindBuildRootFrom(fs::path current) {
    for (int i = 0; i < 8; ++i) {
        if (fs::exists(current / "tools" / "build-game.ps1")) {
            return current;
        }
        if (fs::exists(current.parent_path() / "tools" / "build-game.ps1")) {
            return current.parent_path();
        }
        if (!current.has_parent_path() || current.parent_path() == current) {
            break;
        }
        current = current.parent_path();
    }
    return fs::current_path();
}

fs::path ExecutableDirectory() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return fs::path(buffer).parent_path();
    }
#endif
    return fs::current_path();
}

fs::path FindBuildRoot() {
    const fs::path fromCurrent = FindBuildRootFrom(fs::current_path());
    if (fs::exists(fromCurrent / "tools" / "build-game.ps1")) {
        return fromCurrent;
    }

    const fs::path fromExe = FindBuildRootFrom(ExecutableDirectory());
    if (fs::exists(fromExe / "tools" / "build-game.ps1")) {
        return fromExe;
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

BuildResult BuildStandaloneGame(const std::string& outputDirectory, const std::string& projectRoot) {
    if (outputDirectory.empty()) {
        return {false, "Build cancelled: no output folder selected."};
    }
    if (projectRoot.empty()) {
        return {false, "Build cancelled: no project is open."};
    }

    const fs::path projectPath = fs::absolute(fs::path(projectRoot)).lexically_normal();
    if (!fs::exists(projectPath / "project.raceman.json")) {
        return {false, "Build cancelled: project.raceman.json not found in " + projectPath.string()};
    }

    const fs::path buildRoot = FindBuildRoot();
    const fs::path scriptPath = buildRoot / "tools" / "build-game.ps1";
    if (!fs::exists(scriptPath)) {
        return {false, "Build helper not found: " + scriptPath.string()};
    }

    std::ostringstream command;
    command << "powershell -NoProfile -ExecutionPolicy Bypass -File "
            << QuoteCommandPath(scriptPath)
            << " -OutputPath " << QuoteCommandPath(fs::path(outputDirectory))
            << " -ProjectPath " << QuoteCommandPath(projectPath)
            << " -Configuration Release"
            << " -Platform x64";

    const int result = std::system(command.str().c_str());
    if (result != 0) {
        return {false, "Standalone build failed. Check the console output for details."};
    }

    return {true, "Standalone build completed: " + fs::path(outputDirectory).string()};
}

} // namespace raceman

#include "Application.h"

#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::filesystem::path g_playerLogPath;

std::filesystem::path ExecutableDirectory() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

void SetProjectRootEnvironment(const std::filesystem::path& projectRoot) {
#if defined(_WIN32)
    _putenv_s("RACEMAN_PROJECT_ROOT", projectRoot.string().c_str());
#else
    setenv("RACEMAN_PROJECT_ROOT", projectRoot.string().c_str(), 1);
#endif
}

void AppendPlayerLog(const std::string& message) {
    if (g_playerLogPath.empty()) {
        return;
    }
    std::ofstream log(g_playerLogPath, std::ios::app);
    if (log) {
        log << message << '\n';
    }
}

void StartPlayerLog(const std::filesystem::path& exeDir, const std::filesystem::path& projectRoot) {
    g_playerLogPath = exeDir / "player.log";
    {
        std::ofstream log(g_playerLogPath, std::ios::trunc);
        if (log) {
            log << "Project Raceman player log\n";
            log << "WorkingDirectory=" << exeDir.string() << '\n';
            log << "ProjectRoot=" << projectRoot.string() << '\n';
        }
    }

#if defined(_WIN32)
    FILE* stdoutFile = nullptr;
    FILE* stderrFile = nullptr;
    freopen_s(&stdoutFile, g_playerLogPath.string().c_str(), "a", stdout);
    freopen_s(&stderrFile, g_playerLogPath.string().c_str(), "a", stderr);
#else
    std::freopen(g_playerLogPath.string().c_str(), "a", stdout);
    std::freopen(g_playerLogPath.string().c_str(), "a", stderr);
#endif
    std::cout << "[Player] Log started." << std::endl;
}

#if defined(_WIN32)
LONG WINAPI PlayerUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    const DWORD code = exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr
        ? exceptionInfo->ExceptionRecord->ExceptionCode
        : 0;
    char buffer[128] = {};
    std::snprintf(buffer, sizeof(buffer), "[Crash] Unhandled Windows exception: 0x%08lX", static_cast<unsigned long>(code));
    AppendPlayerLog(buffer);
    return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI PlayerVectoredExceptionHandler(EXCEPTION_POINTERS* exceptionInfo) {
    if (exceptionInfo != nullptr &&
        exceptionInfo->ExceptionRecord != nullptr &&
        exceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
        AppendPlayerLog("[Player] Ignored debug breakpoint exception in player mode.");
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

} // namespace

int main(int argc, char** argv) {
    using namespace raceman;

    ApplicationConfig config;
    std::filesystem::path explicitProjectRoot;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        if (arg == "--player") {
            config.playerMode = true;
            config.enableImGui = false;
        } else if (arg == "--project" && i + 1 < argc) {
            explicitProjectRoot = argv[++i];
        }
    }

    const std::filesystem::path exeDir = ExecutableDirectory();
    if (std::filesystem::exists(exeDir / "player.raceman")) {
        config.playerMode = true;
        // Keep ImGui enabled when debug.raceman is present so the in-game debug overlay renders.
        config.enableImGui = std::filesystem::exists(exeDir / "debug.raceman");
        // Use the exe filename (without extension) as the display name so the window
        // title matches the project name chosen at build time.
        const std::string exeStem = std::filesystem::path(argv[0] != nullptr ? argv[0] : "").stem().string();
        if (!exeStem.empty()) {
            config.windowTitle = exeStem;
        }
    }

    if (config.playerMode) {
        const std::filesystem::path projectRoot = explicitProjectRoot.empty()
            ? (exeDir / "Project")
            : explicitProjectRoot;
        config.projectRoot = projectRoot.string();
        StartPlayerLog(exeDir, projectRoot);
#if defined(_WIN32)
        SetUnhandledExceptionFilter(PlayerUnhandledExceptionFilter);
        AddVectoredExceptionHandler(1, PlayerVectoredExceptionHandler);
        _putenv_s("RACEMAN_PLAYER_MODE", "1");
#endif
        SetProjectRootEnvironment(projectRoot);
        std::error_code ec;
        std::filesystem::current_path(exeDir, ec);
        if (ec) {
            AppendPlayerLog("[Player] Failed to set working directory: " + ec.message());
        }
    }

    try {
        Application app(config);

        app.Run();
    } catch (const std::exception& e) {
        AppendPlayerLog(std::string("[Crash] Unhandled C++ exception: ") + e.what());
        return 1;
    } catch (...) {
        AppendPlayerLog("[Crash] Unknown unhandled exception.");
        return 1;
    }

    return 0;
}

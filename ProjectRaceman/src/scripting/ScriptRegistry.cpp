#include "ScriptRegistry.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace raceman {
namespace {

using RegisterScriptsFn = void (*)(std::vector<ScriptDescriptor>&);
using GetScriptCountFn = int (*)();
using GetScriptNameFn = const char* (*)(int);
using GetScriptPathFn = const char* (*)(int);
using CreateScriptByNameFn = IObjectScript* (*)(const char*);

std::vector<ScriptDescriptor> g_scripts;

#if defined(_WIN32)
HMODULE g_scriptModule = nullptr;
#endif

std::filesystem::path EngineRootPath() {
    std::filesystem::path current = std::filesystem::current_path();
    for (int i = 0; i < 5; ++i) {
        if (std::filesystem::exists(current / "Project Raceman.vcxproj")) {
            return current;
        }
        if (!current.has_parent_path() || current.parent_path() == current) {
            break;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
}

std::string QuoteCommandPath(const std::filesystem::path& path) {
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

std::string CurrentConfiguration() {
#if defined(_DEBUG)
    return "Debug";
#else
    return "Release";
#endif
}

std::filesystem::path ScriptDllPath() {
    const std::filesystem::path packagedDll = std::filesystem::current_path() / "ProjectScripts.dll";
    if (std::filesystem::exists(packagedDll)) {
        return packagedDll;
    }
    return EngineRootPath() / "bin" / CurrentConfiguration() / "ProjectScripts.dll";
}

bool PlayerLoggingEnabled() {
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    const bool enabled = _dupenv_s(&value, &length, "RACEMAN_PLAYER_MODE") == 0 && value != nullptr;
    free(value);
    return enabled;
#else
    return std::getenv("RACEMAN_PLAYER_MODE") != nullptr;
#endif
}

} // namespace

const std::vector<ScriptDescriptor>& GetRegisteredScripts() {
    return g_scripts;
}

const ScriptDescriptor* FindRegisteredScript(const std::string& name) {
    for (const ScriptDescriptor& script : GetRegisteredScripts()) {
        if (script.name == name) {
            return &script;
        }
    }
    return nullptr;
}

std::unique_ptr<IObjectScript> CreateRegisteredScript(const std::string& name) {
    const ScriptDescriptor* script = FindRegisteredScript(name);
    if (script == nullptr) {
        return {};
    }
    if (script->create != nullptr) {
        return script->create();
    }
    if (script->createByName != nullptr) {
        return std::unique_ptr<IObjectScript>(script->createByName(script->name.c_str()));
    }
    return {};
}

std::vector<ScriptFieldDefinition> GetRegisteredScriptFieldDefinitions(const std::string& name) {
    std::unique_ptr<IObjectScript> script = CreateRegisteredScript(name);
    if (!script) {
        return {};
    }
    return script->GetFieldDefinitions();
}

bool LoadScriptAssembly(std::string* outError) {
#if !defined(_WIN32)
    if (outError) {
        *outError = "Script DLL loading is only implemented on Windows.";
    }
    return false;
#else
    UnloadScriptAssembly();

    const std::filesystem::path dllPath = ScriptDllPath();
    if (PlayerLoggingEnabled()) {
        std::cout << "[Player] Loading script DLL: " << dllPath.string() << std::endl;
    }
    if (!std::filesystem::exists(dllPath)) {
        if (outError) {
            *outError = "Script DLL not found: " + dllPath.string();
        }
        return false;
    }

    g_scriptModule = LoadLibraryA(dllPath.string().c_str());
    if (g_scriptModule == nullptr) {
        if (outError) {
            *outError = "Failed to load script DLL: " + dllPath.string();
        }
        return false;
    }
    if (PlayerLoggingEnabled()) {
        std::cout << "[Player] Script DLL loaded." << std::endl;
    }

    const bool playerMode = PlayerLoggingEnabled();

    auto getScriptCount = reinterpret_cast<GetScriptCountFn>(GetProcAddress(g_scriptModule, "RacemanGetScriptCount"));
    auto getScriptName = reinterpret_cast<GetScriptNameFn>(GetProcAddress(g_scriptModule, "RacemanGetScriptName"));
    auto getScriptPath = reinterpret_cast<GetScriptPathFn>(GetProcAddress(g_scriptModule, "RacemanGetScriptPath"));
    auto createScriptByName = reinterpret_cast<CreateScriptByNameFn>(GetProcAddress(g_scriptModule, "RacemanCreateScriptByName"));
    if (playerMode && getScriptCount != nullptr && getScriptName != nullptr && getScriptPath != nullptr && createScriptByName != nullptr) {
        if (PlayerLoggingEnabled()) {
            std::cout << "[Player] Registering scripts through stable DLL ABI..." << std::endl;
        }
        g_scripts.clear();
        const int count = getScriptCount();
        for (int i = 0; i < count; ++i) {
            const char* name = getScriptName(i);
            const char* path = getScriptPath(i);
            if (name != nullptr && name[0] != '\0') {
                g_scripts.push_back({name, path != nullptr ? path : "", nullptr, createScriptByName});
            }
        }
        if (PlayerLoggingEnabled()) {
            std::cout << "[Player] Registered " << g_scripts.size() << " script(s)." << std::endl;
        }
        return true;
    }

    auto registerScripts = reinterpret_cast<RegisterScriptsFn>(GetProcAddress(g_scriptModule, "RacemanRegisterScripts"));
    if (registerScripts == nullptr) {
        if (outError) {
            *outError = "Script DLL is missing script registration exports.";
        }
        UnloadScriptAssembly();
        return false;
    }

    if (PlayerLoggingEnabled()) {
        std::cout << "[Player] Registering scripts..." << std::endl;
    }
    registerScripts(g_scripts);
    if (PlayerLoggingEnabled()) {
        std::cout << "[Player] Registered " << g_scripts.size() << " script(s)." << std::endl;
    }
    return true;
#endif
}

bool BuildScriptAssembly(std::string* outError) {
#if !defined(_WIN32)
    if (outError) {
        *outError = "Script DLL build is only implemented on Windows.";
    }
    return false;
#else
    const std::filesystem::path engineRoot = EngineRootPath();
    const std::filesystem::path script = engineRoot.parent_path() / "tools" / "build-scripts.ps1";
    if (!std::filesystem::exists(script)) {
        if (outError) {
            *outError = "Script build helper not found: " + script.string();
        }
        return false;
    }

    std::ostringstream command;
    command << "powershell -NoProfile -ExecutionPolicy Bypass -File "
            << QuoteCommandPath(script)
            << " -Configuration " << CurrentConfiguration()
            << " -Platform x64";

    const int result = std::system(command.str().c_str());
    if (result != 0) {
        if (outError) {
            *outError = "Script DLL build failed. Check the build output for compiler errors.";
        }
        return false;
    }

    return true;
#endif
}

bool BuildAndLoadScriptAssembly(std::string* outError) {
    if (!BuildScriptAssembly(outError)) {
        return false;
    }
    return LoadScriptAssembly(outError);
}

void UnloadScriptAssembly() {
    g_scripts.clear();
#if defined(_WIN32)
    if (g_scriptModule != nullptr) {
        FreeLibrary(g_scriptModule);
        g_scriptModule = nullptr;
    }
#endif
}

bool IsScriptAssemblyLoaded() {
#if defined(_WIN32)
    return g_scriptModule != nullptr;
#else
    return false;
#endif
}

} // namespace raceman

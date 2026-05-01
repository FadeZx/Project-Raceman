#include "ScriptRegistry.h"

#include <cstdlib>
#include <filesystem>
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
    return EngineRootPath() / "bin" / CurrentConfiguration() / "ProjectScripts.dll";
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
    if (script == nullptr || script->create == nullptr) {
        return {};
    }
    return script->create();
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

    auto registerScripts = reinterpret_cast<RegisterScriptsFn>(GetProcAddress(g_scriptModule, "RacemanRegisterScripts"));
    if (registerScripts == nullptr) {
        if (outError) {
            *outError = "Script DLL is missing RacemanRegisterScripts export.";
        }
        UnloadScriptAssembly();
        return false;
    }

    registerScripts(g_scripts);
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

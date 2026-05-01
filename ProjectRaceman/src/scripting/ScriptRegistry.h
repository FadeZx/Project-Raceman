#pragma once

#include "ObjectScript.h"

#include <memory>
#include <string>
#include <vector>

namespace raceman {

struct ScriptDescriptor {
    std::string name;
    std::string path;
    std::unique_ptr<IObjectScript> (*create)();
};

const std::vector<ScriptDescriptor>& GetRegisteredScripts();
const ScriptDescriptor* FindRegisteredScript(const std::string& name);
std::unique_ptr<IObjectScript> CreateRegisteredScript(const std::string& name);
std::vector<ScriptFieldDefinition> GetRegisteredScriptFieldDefinitions(const std::string& name);
bool LoadScriptAssembly(std::string* outError = nullptr);
bool BuildScriptAssembly(std::string* outError = nullptr);
bool BuildAndLoadScriptAssembly(std::string* outError = nullptr);
void UnloadScriptAssembly();
bool IsScriptAssemblyLoaded();

} // namespace raceman

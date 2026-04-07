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

} // namespace raceman

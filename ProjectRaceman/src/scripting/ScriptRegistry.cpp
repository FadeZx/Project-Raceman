#include "ScriptRegistry.h"

#include "../../Project/assets/scripts/CharacterControllerTest.h"
#include "../../Project/assets/scripts/Test1.h"

namespace raceman {
namespace {

std::unique_ptr<IObjectScript> CreateCharacterControllerTest() {
    return std::make_unique<scripts::CharacterControllerTest>();
}

std::unique_ptr<IObjectScript> CreateTest1() {
    return std::make_unique<scripts::Test1>();
}

} // namespace

const std::vector<ScriptDescriptor>& GetRegisteredScripts() {
    static const std::vector<ScriptDescriptor> scripts = {
        {"CharacterControllerTest", "assets/scripts/CharacterControllerTest.cpp", &CreateCharacterControllerTest},
        {"Test1", "assets/scripts/Test1.cpp", &CreateTest1},
    };
    return scripts;
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

} // namespace raceman

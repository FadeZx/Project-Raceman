#include "ScriptRegistry.h"

#include <cstring>

#include "../../../Project1/assets/scripts/CameraOrbit.h"
#include "../../../Project1/assets/scripts/CharacterControllerTest.h"
#include "../../../Project1/assets/scripts/TestPlayerMovement.h"
#include "../../../Project1/assets/scripts/VirtualCameraSwitcher.h"

namespace raceman {
namespace {

std::unique_ptr<IObjectScript> CreateCameraOrbit() {
    return std::make_unique<scripts::CameraOrbit>();
}

std::unique_ptr<IObjectScript> CreateCharacterControllerTest() {
    return std::make_unique<scripts::CharacterControllerTest>();
}

std::unique_ptr<IObjectScript> CreateTestPlayerMovement() {
    return std::make_unique<scripts::TestPlayerMovement>();
}

std::unique_ptr<IObjectScript> CreateVirtualCameraSwitcher() {
    return std::make_unique<scripts::VirtualCameraSwitcher>();
}

struct ScriptExportEntry {
    const char* name;
    const char* path;
};

const ScriptExportEntry kScripts[] = {
    {"CameraOrbit", "assets/scripts/CameraOrbit.cpp"},
    {"CharacterControllerTest", "assets/scripts/CharacterControllerTest.cpp"},
    {"TestPlayerMovement", "assets/scripts/TestPlayerMovement.cpp"},
    {"VirtualCameraSwitcher", "assets/scripts/VirtualCameraSwitcher.cpp"},
};

} // namespace

extern "C" __declspec(dllexport) int RacemanGetScriptApiVersion() {
    return kObjectScriptApiVersion;
}

extern "C" __declspec(dllexport) int RacemanGetScriptCount() {
    return static_cast<int>(sizeof(kScripts) / sizeof(kScripts[0]));
}

extern "C" __declspec(dllexport) const char* RacemanGetScriptName(int index) {
    const int count = RacemanGetScriptCount();
    return index >= 0 && index < count ? kScripts[index].name : nullptr;
}

extern "C" __declspec(dllexport) const char* RacemanGetScriptPath(int index) {
    const int count = RacemanGetScriptCount();
    return index >= 0 && index < count ? kScripts[index].path : nullptr;
}

extern "C" __declspec(dllexport) raceman::IObjectScript* RacemanCreateScriptByName(const char* name) {
    if (name == nullptr) return nullptr;
    if (std::strcmp(name, "CameraOrbit") == 0) return new scripts::CameraOrbit();
    if (std::strcmp(name, "CharacterControllerTest") == 0) return new scripts::CharacterControllerTest();
    if (std::strcmp(name, "TestPlayerMovement") == 0) return new scripts::TestPlayerMovement();
    if (std::strcmp(name, "VirtualCameraSwitcher") == 0) return new scripts::VirtualCameraSwitcher();
    return nullptr;
}

extern "C" __declspec(dllexport) void RacemanRegisterScripts(std::vector<raceman::ScriptDescriptor>& scripts) {
    scripts.clear();
    scripts.push_back({"CameraOrbit", "assets/scripts/CameraOrbit.cpp", &CreateCameraOrbit});
    scripts.push_back({"CharacterControllerTest", "assets/scripts/CharacterControllerTest.cpp", &CreateCharacterControllerTest});
    scripts.push_back({"TestPlayerMovement", "assets/scripts/TestPlayerMovement.cpp", &CreateTestPlayerMovement});
    scripts.push_back({"VirtualCameraSwitcher", "assets/scripts/VirtualCameraSwitcher.cpp", &CreateVirtualCameraSwitcher});
}

} // namespace raceman

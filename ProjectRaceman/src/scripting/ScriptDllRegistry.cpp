#include "ScriptRegistry.h"

#include "../../Project/assets/scripts/CameraOrbit.h"
#include "../../Project/assets/scripts/CharacterControllerTest.h"
#include "../../Project/assets/scripts/TestPlayerMovement.h"

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

} // namespace

extern "C" __declspec(dllexport) void RacemanRegisterScripts(std::vector<raceman::ScriptDescriptor>& scripts) {
    scripts.clear();
    scripts.push_back({"CameraOrbit", "assets/scripts/CameraOrbit.cpp", &CreateCameraOrbit});
    scripts.push_back({"CharacterControllerTest", "assets/scripts/CharacterControllerTest.cpp", &CreateCharacterControllerTest});
    scripts.push_back({"TestPlayerMovement", "assets/scripts/TestPlayerMovement.cpp", &CreateTestPlayerMovement});
}

} // namespace raceman

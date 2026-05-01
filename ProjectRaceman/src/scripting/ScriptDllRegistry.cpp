#include "ScriptRegistry.h"

#include "../../Project/assets/scripts/CameraOrbit.h"
#include "../../Project/assets/scripts/CharacterControllerTest.h"
#include "../../Project/assets/scripts/Test1.h"

namespace raceman {
namespace {

std::unique_ptr<IObjectScript> CreateCameraOrbit() {
    return std::make_unique<scripts::CameraOrbit>();
}

std::unique_ptr<IObjectScript> CreateCharacterControllerTest() {
    return std::make_unique<scripts::CharacterControllerTest>();
}

std::unique_ptr<IObjectScript> CreateTest1() {
    return std::make_unique<scripts::Test1>();
}

} // namespace

extern "C" __declspec(dllexport) void RacemanRegisterScripts(std::vector<raceman::ScriptDescriptor>& scripts) {
    scripts.clear();
    scripts.push_back({"CameraOrbit", "assets/scripts/CameraOrbit.cpp", &CreateCameraOrbit});
    scripts.push_back({"CharacterControllerTest", "assets/scripts/CharacterControllerTest.cpp", &CreateCharacterControllerTest});
    scripts.push_back({"Test1", "assets/scripts/Test1.cpp", &CreateTest1});
}

} // namespace raceman

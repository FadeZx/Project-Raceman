#include "VirtualCameraSwitcher.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace raceman::scripts {

namespace {

std::string Trim(std::string value) {
    const auto isNotSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
    return value;
}

} // namespace

std::vector<raceman::ObjectScriptContext::ObjectHandle> VirtualCameraSwitcher::GetUsableCameras(raceman::ObjectScriptContext& context) const {
    std::vector<raceman::ObjectScriptContext::ObjectHandle> cameras;
    const auto configuredCameras = context.GetObjectListField("cameras");
    cameras.reserve(configuredCameras.size());
    for (const auto& camera : configuredCameras) {
        if (camera.IsValid() && camera.HasVirtualCamera()) {
            cameras.push_back(camera);
        }
    }
    if (!configuredCameras.empty()) {
        return cameras;
    }

    // Compatibility fallback for scenes saved before typed object-list fields.
    std::string names = context.GetStringField("cameraNames", "Chase Camera, Cockpit Camera");
    std::replace(names.begin(), names.end(), ';', ',');

    std::stringstream stream(names);
    std::string name;
    while (std::getline(stream, name, ',')) {
        name = Trim(std::move(name));
        const auto camera = context.FindObjectByName(name);
        if (!name.empty() && camera.IsValid() && camera.HasVirtualCamera()) {
            cameras.push_back(camera);
        }
    }
    return cameras;
}

void VirtualCameraSwitcher::ActivateCamera(raceman::ObjectScriptContext& context, int index) {
    const auto cameras = GetUsableCameras(context);
    if (cameras.empty()) {
        if (!warnedNoCameras_) {
            context.Warning("VirtualCameraSwitcher has no usable Virtual Cameras.");
            warnedNoCameras_ = true;
        }
        return;
    }
    warnedNoCameras_ = false;

    const int count = static_cast<int>(cameras.size());
    activeIndex_ = ((index % count) + count) % count;
    const int activePriority = context.GetIntField("activePriority", 100);
    const int inactivePriority = context.GetIntField("inactivePriority", 0);

    for (int i = 0; i < count; ++i) {
        auto camera = cameras[i];
        camera.SetVirtualCameraPriority(i == activeIndex_ ? activePriority : inactivePriority);
    }

    context.Log("Active virtual camera: " + cameras[activeIndex_].GetObjectName());
}

void VirtualCameraSwitcher::OnStart(raceman::ObjectScriptContext& context) {
    ActivateCamera(context, context.GetIntField("startIndex", 0));
}

void VirtualCameraSwitcher::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    const int switchKey = context.GetKeyField("switchKey", GLFW_KEY_L);
    if (context.WasKeyPressed(switchKey)) {
        ActivateCamera(context, activeIndex_ + 1);
    }
    (void)deltaTime;
}

} // namespace raceman::scripts

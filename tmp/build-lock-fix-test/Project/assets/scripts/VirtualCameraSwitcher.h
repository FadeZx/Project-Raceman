#pragma once

#include "../../../src/scripting/ObjectScript.h"

#include <GLFW/glfw3.h>

namespace raceman::scripts {

class VirtualCameraSwitcher : public raceman::IObjectScript {
public:
    RACEMAN_SCRIPT_FIELDS_BEGIN()
        RACEMAN_SCRIPT_FIELD_OBJECT_LIST("cameras", "Virtual Cameras", "VirtualCamera"),
        RACEMAN_SCRIPT_FIELD_KEY("switchKey", "Switch Key", GLFW_KEY_L),
        RACEMAN_SCRIPT_FIELD_INT("startIndex", "Start Index", 0),
        RACEMAN_SCRIPT_FIELD_INT("activePriority", "Active Priority", 100),
        RACEMAN_SCRIPT_FIELD_INT("inactivePriority", "Inactive Priority", 0),
        RACEMAN_SCRIPT_FIELD_HIDDEN_STRING("cameraNames", "Chase Camera, Cockpit Camera")
    RACEMAN_SCRIPT_FIELDS_END();

    void OnStart(raceman::ObjectScriptContext& context) override;
    void OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) override;

private:
    std::vector<raceman::ObjectScriptContext::ObjectHandle> GetUsableCameras(raceman::ObjectScriptContext& context) const;
    void ActivateCamera(raceman::ObjectScriptContext& context, int index);

    int activeIndex_{-1};
    bool warnedNoCameras_{false};
};

} // namespace raceman::scripts

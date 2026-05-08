#pragma once

#include "../../../src/scripting/ObjectScript.h"

namespace raceman::scripts {

class CameraOrbit : public raceman::IObjectScript {
public:
    RACEMAN_SCRIPT_FIELDS_BEGIN()
        RACEMAN_SCRIPT_FIELD_VEC3("targetPosition", "Target Position", glm::vec3(0.0f, 1.0f, 0.0f)),
        RACEMAN_SCRIPT_FIELD_FLOAT("radius", "Radius", 6.0f),
        RACEMAN_SCRIPT_FIELD_FLOAT("mouseSensitivity", "Mouse Sensitivity", 0.18f),
        RACEMAN_SCRIPT_FIELD_FLOAT("stickSensitivity", "Stick Sensitivity", 160.0f),
        RACEMAN_SCRIPT_FIELD_FLOAT("zoomSpeed", "Zoom Speed", 5.0f),
        RACEMAN_SCRIPT_FIELD_FLOAT("minPitch", "Min Pitch", -80.0f),
        RACEMAN_SCRIPT_FIELD_FLOAT("maxPitch", "Max Pitch", 80.0f),
        RACEMAN_SCRIPT_FIELD_BOOL("invertY", "Invert Y", false),
        RACEMAN_SCRIPT_FIELD_BOOL("orbitOnRightMouse", "Orbit On Right Mouse", true)
    RACEMAN_SCRIPT_FIELDS_END();

    void OnStart(raceman::ObjectScriptContext& context) override;
    void OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) override;

private:
    bool warnedMissingCamera_{false};
    float yawDegrees_{0.0f};
    float pitchDegrees_{15.0f};
    bool initializedOrbitState_{false};
};

} // namespace raceman::scripts

#pragma once

#include "../../../src/scripting/ObjectScript.h"

namespace raceman::scripts {

class CameraOrbit : public raceman::IObjectScript {
public:
    RACEMAN_SCRIPT_FIELDS_BEGIN()
        RACEMAN_SCRIPT_FIELD_VEC3("targetPosition", "Orbit Center", glm::vec3(0.0f)),
        RACEMAN_SCRIPT_FIELD_FLOAT("mouseSensitivity", "Mouse Sensitivity", 0.18f),
        RACEMAN_SCRIPT_FIELD_FLOAT("returnSpeed", "Return Speed", 5.0f),
        RACEMAN_SCRIPT_FIELD_FLOAT("minPitch", "Min Pitch", -20.0f),
        RACEMAN_SCRIPT_FIELD_FLOAT("maxPitch", "Max Pitch", 75.0f),
        RACEMAN_SCRIPT_FIELD_BOOL("invertY", "Invert Y", false),
        RACEMAN_SCRIPT_FIELD_BOOL("returnOnRelease", "Return On Release", true)
    RACEMAN_SCRIPT_FIELDS_END();

    void OnStart(raceman::ObjectScriptContext& context) override;
    void OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) override;

private:
    bool warnedMissingCamera_{false};
    glm::vec3 startPosition_{0.0f};
    glm::vec3 startOffset_{0.0f};
    float yawDegrees_{0.0f};
    float pitchDegrees_{15.0f};
    float radius_{6.0f};
    bool initialized_{false};
};

} // namespace raceman::scripts

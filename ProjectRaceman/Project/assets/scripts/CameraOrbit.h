#pragma once

#include "../../../src/scripting/ObjectScript.h"

namespace raceman::scripts {

class CameraOrbit : public raceman::IObjectScript {
public:
    void OnStart(raceman::ObjectScriptContext& context) override;
    void OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) override;

private:
    bool warnedMissingCamera_{false};
    bool anglesInitialized_{false};
    float yawDegrees_{0.0f};
    float pitchDegrees_{15.0f};
    float radius_{6.0f};
    float orbitSpeedDeg_{60.0f};
    float pitchSpeedDeg_{45.0f};
    float fovChangeSpeed_{30.0f};
};

} // namespace raceman::scripts

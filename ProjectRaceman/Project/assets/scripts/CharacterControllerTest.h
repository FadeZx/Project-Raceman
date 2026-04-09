#pragma once

#include "../../../src/scripting/ObjectScript.h"

namespace raceman::scripts {

class CharacterControllerTest : public raceman::IObjectScript {
public:
    RACEMAN_SCRIPT_FIELDS_BEGIN()
        RACEMAN_SCRIPT_FIELD_FLOAT("moveSpeed", "Move Speed", 6.0f),
        RACEMAN_SCRIPT_FIELD_FLOAT("jumpImpulse", "Jump Impulse", 1.5f)
    RACEMAN_SCRIPT_FIELDS_END();

    void OnStart(raceman::ObjectScriptContext& context) override;
    void OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) override;

private:
    bool jumpHeld_{false};
    bool warnedMissingController_{false};
};

} // namespace raceman::scripts

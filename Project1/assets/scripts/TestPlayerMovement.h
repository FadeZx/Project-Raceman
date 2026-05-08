#pragma once

#include "../../../src/scripting/ObjectScript.h"

namespace raceman::scripts {

class TestPlayerMovement : public raceman::IObjectScript {
public:
    RACEMAN_SCRIPT_FIELDS_BEGIN()
        RACEMAN_SCRIPT_FIELD_FLOAT("moveSpeed", "Move Speed", 8.0f)
    RACEMAN_SCRIPT_FIELDS_END();

    void OnStart(raceman::ObjectScriptContext& context) override;
    void OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) override;

private:
    bool warnedMissingRigidbody_{false};
};

} // namespace raceman::scripts

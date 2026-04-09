#pragma once

#include "../../../src/scripting/ObjectScript.h"

namespace raceman::scripts {

class CharacterControllerTest : public raceman::IObjectScript {
public:
    void OnStart(raceman::ObjectScriptContext& context) override;
    void OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) override;

private:
    float moveSpeed_{6.0f};
    float jumpImpulse_{6.5f};
    bool jumpHeld_{false};
    bool warnedMissingController_{false};
};

} // namespace raceman::scripts

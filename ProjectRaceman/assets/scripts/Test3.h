#pragma once

#include "../../src/scripting/ObjectScript.h"

namespace raceman::scripts {

class Test3 : public raceman::IObjectScript {
public:
    void OnStart(raceman::ObjectScriptContext& context) override;
    void OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) override;
};

} // namespace raceman::scripts

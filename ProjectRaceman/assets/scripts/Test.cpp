#include "Test.h"

namespace raceman::scripts {

void Test::OnStart(raceman::ObjectScriptContext& context) {
    context.Log("started");
}

void Test::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    (void)context;
    (void)deltaTime;
}

} // namespace raceman::scripts

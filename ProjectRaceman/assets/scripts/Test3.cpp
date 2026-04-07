#include "Test3.h"

namespace raceman::scripts {

void Test3::OnStart(raceman::ObjectScriptContext& context) {
    context.Log("started");
}

void Test3::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    (void)context;
    (void)deltaTime;
}

} // namespace raceman::scripts

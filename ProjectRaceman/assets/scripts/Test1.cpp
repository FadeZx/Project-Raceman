#include "Test1.h"

namespace raceman::scripts {

void Test1::OnStart(raceman::ObjectScriptContext& context) {
    context.Log("started");
}

void Test1::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    (void)context;
    (void)deltaTime;
}

} // namespace raceman::scripts

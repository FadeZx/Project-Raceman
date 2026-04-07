#include "Test1.h"

#include <GLFW/glfw3.h>

namespace raceman::scripts {

void Test1::OnStart(raceman::ObjectScriptContext& context) {
    context.Log("Test1 Rigidbody WASD movement started");
}

void Test1::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    if (!context.IsRigidbodyDynamic()) {
        if (!warnedMissingDynamicRigidbody_) {
            context.Warning("Test1 requires an enabled Dynamic Rigidbody.");
            warnedMissingDynamicRigidbody_ = true;
        }
        return;
    }

    glm::vec3 moveDirection{0.0f, 0.0f, 0.0f};
    if (context.IsKeyDown(GLFW_KEY_W)) {
        moveDirection.z -= 1.0f;
    }
    if (context.IsKeyDown(GLFW_KEY_S)) {
        moveDirection.z += 1.0f;
    }
    if (context.IsKeyDown(GLFW_KEY_A)) {
        moveDirection.x -= 1.0f;
    }
    if (context.IsKeyDown(GLFW_KEY_D)) {
        moveDirection.x += 1.0f;
    }

    glm::vec3 velocity = context.GetRigidbodyVelocity();
    velocity.x = moveDirection.x * moveSpeed_;
    velocity.z = moveDirection.z * moveSpeed_;
    context.SetRigidbodyVelocity(velocity);
}

} // namespace raceman::scripts

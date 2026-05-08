#include "TestPlayerMovement.h"

#include <glm/glm.hpp>

namespace raceman::scripts {

void TestPlayerMovement::OnStart(raceman::ObjectScriptContext& context) {
    context.Log("TestPlayerMovement started. Use WASD to move the rigidbody.");
}

void TestPlayerMovement::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    if (!context.IsRigidbodyDynamic()) {
        if (!warnedMissingRigidbody_) {
            context.Warning("TestPlayerMovement requires an enabled Dynamic Rigidbody component.");
            warnedMissingRigidbody_ = true;
        }
        return;
    }

    const float moveSpeed = context.GetFloatField("moveSpeed", 8.0f);
    glm::vec3 moveDirection{context.GetAxis("moveX"), 0.0f, -context.GetAxis("moveY")};

    if (glm::dot(moveDirection, moveDirection) > 0.0001f) {
        moveDirection = glm::normalize(moveDirection) * moveSpeed;
    }

    glm::vec3 velocity = context.GetRigidbodyVelocity();
    velocity.x = moveDirection.x;
    velocity.z = moveDirection.z;
    context.SetRigidbodyVelocity(velocity);
    context.WakeRigidbody();

    (void)deltaTime;
}

} // namespace raceman::scripts

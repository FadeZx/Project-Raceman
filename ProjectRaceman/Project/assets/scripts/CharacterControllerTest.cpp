#include "CharacterControllerTest.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace raceman::scripts {

void CharacterControllerTest::OnStart(raceman::ObjectScriptContext& context) {
    context.Log("CharacterControllerTest started. Controls: WASD move, Space jump.");
}

void CharacterControllerTest::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    if (!context.HasCharacterController()) {
        if (!warnedMissingController_) {
            context.Warning("CharacterControllerTest requires an enabled Character Controller component.");
            warnedMissingController_ = true;
        }
        return;
    }

    glm::vec3 moveDirection{0.0f};
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

    if (glm::dot(moveDirection, moveDirection) > 0.0001f) {
        moveDirection = glm::normalize(moveDirection) * moveSpeed_;
    }

    context.SetCharacterMoveInput(moveDirection);

    const bool jumpDown = context.IsKeyDown(GLFW_KEY_SPACE);
    if (jumpDown && !jumpHeld_ && context.IsCharacterGrounded()) {
        context.Jump(jumpImpulse_);
    }
    jumpHeld_ = jumpDown;

    if (glm::dot(moveDirection, moveDirection) > 0.0001f) {
        const float yawDegrees = glm::degrees(atan2f(moveDirection.x, -moveDirection.z));
        glm::vec3 rotation = context.GetRotationEuler();
        rotation.y = yawDegrees;
        context.SetRotationEuler(rotation);
    }

    (void)context.GetCharacterVelocity();
}

} // namespace raceman::scripts

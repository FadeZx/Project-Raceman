#include "CharacterControllerTest.h"

#include <glm/glm.hpp>

namespace raceman::scripts {

void CharacterControllerTest::OnStart(raceman::ObjectScriptContext& context) {
    context.Log("CharacterControllerTest started. Controls use the mapped character input profile.");
}

void CharacterControllerTest::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {
    if (!context.HasCharacterController()) {
        if (!warnedMissingController_) {
            context.Warning("CharacterControllerTest requires an enabled Character Controller component.");
            warnedMissingController_ = true;
        }
        return;
    }

    const float moveSpeed = context.GetFloatField("moveSpeed", 6.0f);
    const float jumpImpulse = context.GetFloatField("jumpImpulse", 1.5f);

    glm::vec3 moveDirection{context.GetAxis("moveX"), 0.0f, -context.GetAxis("moveY")};

    if (glm::dot(moveDirection, moveDirection) > 0.0001f) {
        moveDirection = glm::normalize(moveDirection) * moveSpeed;
    }

    context.SetCharacterMoveInput(moveDirection);

    const bool jumpDown = context.IsActionDown("jump");
    if (jumpDown && !jumpHeld_ && context.IsCharacterGrounded()) {
        context.Jump(jumpImpulse);
    }
    jumpHeld_ = jumpDown;

    (void)context.GetCharacterVelocity();
}

} // namespace raceman::scripts

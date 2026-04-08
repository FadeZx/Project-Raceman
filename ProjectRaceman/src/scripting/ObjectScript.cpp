#include "ObjectScript.h"

#include "../input/InputManager.h"
#include "../physics/PhysicsWorld.h"
#include "../ui/Console.h"
#include "../ui/SceneEditor.h"

namespace raceman {

ObjectScriptContext::ObjectScriptContext(SceneObject& object, Console* console, InputManager* inputManager, PhysicsWorld* physicsWorld)
    : object_(object), console_(console), inputManager_(inputManager), physicsWorld_(physicsWorld) {}

const std::string& ObjectScriptContext::GetObjectId() const {
    return object_.id;
}

const std::string& ObjectScriptContext::GetObjectName() const {
    return object_.name;
}

glm::vec3 ObjectScriptContext::GetPosition() const {
    return object_.transform.position;
}

void ObjectScriptContext::SetPosition(const glm::vec3& value) {
    object_.transform.position = value;
}

glm::vec3 ObjectScriptContext::GetRotationEuler() const {
    return object_.transform.rotationEuler;
}

void ObjectScriptContext::SetRotationEuler(const glm::vec3& value) {
    object_.transform.rotationEuler = value;
}

glm::vec3 ObjectScriptContext::GetScale() const {
    return object_.transform.scale;
}

void ObjectScriptContext::SetScale(const glm::vec3& value) {
    object_.transform.scale = value;
}

std::string ObjectScriptContext::GetMaterialId() const {
    return object_.meshRenderer.materialId;
}

void ObjectScriptContext::SetMaterialId(const std::string& value) {
    object_.meshRenderer.materialId = value;
}

bool ObjectScriptContext::IsEnabled() const {
    return object_.enabled;
}

void ObjectScriptContext::SetEnabled(bool value) {
    object_.enabled = value;
}

bool ObjectScriptContext::HasRigidbody() const {
    return object_.hasRigidbody && object_.rigidbody.enabled;
}

bool ObjectScriptContext::IsRigidbodyDynamic() const {
    return HasRigidbody() && object_.rigidbody.bodyType == RigidbodyBodyType::Dynamic;
}

glm::vec3 ObjectScriptContext::GetRigidbodyVelocity() const {
    if (!HasRigidbody()) {
        return {0.0f, 0.0f, 0.0f};
    }
    if (physicsWorld_ && physicsWorld_->HasBody(object_.id)) {
        return physicsWorld_->GetBodyVelocity(object_.id);
    }
    return object_.rigidbody.velocity;
}

void ObjectScriptContext::SetRigidbodyVelocity(const glm::vec3& value) {
    if (!IsRigidbodyDynamic()) {
        return;
    }
    object_.rigidbody.velocity = value;
    if (physicsWorld_ && physicsWorld_->HasBody(object_.id)) {
        physicsWorld_->SetBodyVelocity(object_.id, value);
    }
}

bool ObjectScriptContext::IsKeyDown(int key) const {
    return inputManager_ != nullptr && inputManager_->IsKeyDown(key);
}

void ObjectScriptContext::Log(const std::string& message) const {
    if (console_) {
        console_->AddLog("[Script:" + object_.name + "] " + message);
    }
}

void ObjectScriptContext::Warning(const std::string& message) const {
    if (console_) {
        console_->AddWarning("[Script:" + object_.name + "] " + message);
    }
}

void ObjectScriptContext::Error(const std::string& message) const {
    if (console_) {
        console_->AddError("[Script:" + object_.name + "] " + message);
    }
}

} // namespace raceman

#include "ObjectScript.h"

#include "../input/InputManager.h"
#include "../physics/PhysicsWorld.h"
#include "../ui/Console.h"
#include "../ui/SceneEditor.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/euler_angles.hpp>

namespace raceman {

ObjectScriptContext::ObjectScriptContext(SceneObject& object, Console* console, InputManager* inputManager, PhysicsWorld* physicsWorld)
    : object_(object), console_(console), inputManager_(inputManager), physicsWorld_(physicsWorld) {}

// CameraHandle ----------------------------------------------------------------

ObjectScriptContext::CameraHandle::CameraHandle(SceneObject* object, Console* console)
    : object_(object), console_(console) {}

bool ObjectScriptContext::CameraHandle::IsValid() const {
    return object_ != nullptr && object_->hasCamera && object_->camera.enabled;
}

float ObjectScriptContext::CameraHandle::GetFieldOfView() const {
    return IsValid() ? object_->camera.fieldOfViewDegrees : 0.0f;
}

void ObjectScriptContext::CameraHandle::SetFieldOfView(float degrees) const {
    if (!IsValid()) {
        WarnInvalid("SetFieldOfView");
        return;
    }
    object_->camera.fieldOfViewDegrees = degrees;
}

float ObjectScriptContext::CameraHandle::GetNearClip() const {
    return IsValid() ? object_->camera.nearClip : 0.0f;
}

void ObjectScriptContext::CameraHandle::SetNearClip(float value) const {
    if (!IsValid()) {
        WarnInvalid("SetNearClip");
        return;
    }
    object_->camera.nearClip = value;
}

float ObjectScriptContext::CameraHandle::GetFarClip() const {
    return IsValid() ? object_->camera.farClip : 0.0f;
}

void ObjectScriptContext::CameraHandle::SetFarClip(float value) const {
    if (!IsValid()) {
        WarnInvalid("SetFarClip");
        return;
    }
    object_->camera.farClip = value;
}

glm::vec4 ObjectScriptContext::CameraHandle::GetClearColor() const {
    return IsValid() ? object_->camera.clearColor : glm::vec4(0.0f);
}

void ObjectScriptContext::CameraHandle::SetClearColor(const glm::vec4& value) const {
    if (!IsValid()) {
        WarnInvalid("SetClearColor");
        return;
    }
    object_->camera.clearColor = value;
}

bool ObjectScriptContext::CameraHandle::IsMain() const {
    return IsValid() ? object_->camera.isMain : false;
}

void ObjectScriptContext::CameraHandle::SetMain(bool value) const {
    if (!IsValid()) {
        WarnInvalid("SetMain");
        return;
    }
    object_->camera.isMain = value;
}

void ObjectScriptContext::CameraHandle::WarnInvalid(const std::string& action) const {
    if (console_) {
        console_->AddWarning("[Script:" + (object_ ? object_->name : std::string("unknown")) + "] Camera unavailable for " + action);
    }
}

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

bool ObjectScriptContext::HasCamera() const {
    return object_.hasCamera && object_.camera.enabled;
}

ObjectScriptContext::CameraHandle ObjectScriptContext::Camera() {
    return CameraHandle(&object_, console_);
}

glm::vec3 ObjectScriptContext::GetForwardVector() const {
    const glm::vec3 r = glm::radians(object_.transform.rotationEuler);
    const glm::mat4 rot = glm::yawPitchRoll(r.y, r.x, r.z);
    return glm::normalize(glm::vec3(rot * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
}

glm::vec3 ObjectScriptContext::GetUpVector() const {
    const glm::vec3 r = glm::radians(object_.transform.rotationEuler);
    const glm::mat4 rot = glm::yawPitchRoll(r.y, r.x, r.z);
    return glm::normalize(glm::vec3(rot * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
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

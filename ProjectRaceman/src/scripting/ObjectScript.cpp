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

namespace {

ScriptFieldEntry* FindAttachmentField(ObjectScriptAttachment* attachment, const std::string& name) {
    if (attachment == nullptr) {
        return nullptr;
    }
    for (ScriptFieldEntry& field : attachment->fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

const ScriptFieldEntry* FindAttachmentField(const ObjectScriptAttachment* attachment, const std::string& name) {
    if (attachment == nullptr) {
        return nullptr;
    }
    for (const ScriptFieldEntry& field : attachment->fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

template <typename T>
T GetTypedFieldValue(const ObjectScriptAttachment* attachment, const std::string& name, const T& fallback) {
    const ScriptFieldEntry* field = FindAttachmentField(attachment, name);
    if (field == nullptr) {
        return fallback;
    }
    if (const T* value = std::get_if<T>(&field->value)) {
        return *value;
    }
    return fallback;
}

template <typename T>
void SetTypedFieldValue(ObjectScriptAttachment* attachment, const std::string& name, ScriptFieldType expectedType, const T& value) {
    ScriptFieldEntry* field = FindAttachmentField(attachment, name);
    if (field == nullptr || field->type != expectedType) {
        return;
    }
    field->value = value;
}

} // namespace

ObjectScriptContext::ObjectScriptContext(SceneObject& object, ObjectScriptAttachment* attachment, Console* console, InputManager* inputManager, PhysicsWorld* physicsWorld)
    : object_(object), attachment_(attachment), console_(console), inputManager_(inputManager), physicsWorld_(physicsWorld) {}

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

glm::vec3 ObjectScriptContext::GetRigidbodyAngularVelocity() const {
    if (!HasRigidbody()) {
        return {0.0f, 0.0f, 0.0f};
    }
    if (physicsWorld_ && physicsWorld_->HasBody(object_.id)) {
        return physicsWorld_->GetBodyAngularVelocity(object_.id);
    }
    return object_.rigidbody.angularVelocity;
}

void ObjectScriptContext::SetRigidbodyAngularVelocity(const glm::vec3& value) {
    if (!IsRigidbodyDynamic()) {
        return;
    }
    object_.rigidbody.angularVelocity = value;
    if (physicsWorld_ && physicsWorld_->HasBody(object_.id)) {
        physicsWorld_->SetBodyAngularVelocity(object_.id, value);
    }
}

void ObjectScriptContext::AddRigidbodyForce(const glm::vec3& force) {
    if (!IsRigidbodyDynamic()) {
        return;
    }
    if (physicsWorld_ && physicsWorld_->HasBody(object_.id)) {
        physicsWorld_->AddBodyForce(object_.id, force);
    }
}

void ObjectScriptContext::AddRigidbodyImpulse(const glm::vec3& impulse) {
    if (!IsRigidbodyDynamic()) {
        return;
    }
    if (physicsWorld_ && physicsWorld_->HasBody(object_.id)) {
        physicsWorld_->AddBodyImpulse(object_.id, impulse);
    }
}

void ObjectScriptContext::AddRigidbodyTorque(const glm::vec3& torque) {
    if (!IsRigidbodyDynamic()) {
        return;
    }
    if (physicsWorld_ && physicsWorld_->HasBody(object_.id)) {
        physicsWorld_->AddBodyTorque(object_.id, torque);
    }
}

void ObjectScriptContext::AddRigidbodyAngularImpulse(const glm::vec3& impulse) {
    if (!IsRigidbodyDynamic()) {
        return;
    }
    if (physicsWorld_ && physicsWorld_->HasBody(object_.id)) {
        physicsWorld_->AddBodyAngularImpulse(object_.id, impulse);
    }
}

void ObjectScriptContext::WakeRigidbody() {
    if (!HasRigidbody()) {
        return;
    }
    if (physicsWorld_ && physicsWorld_->HasBody(object_.id)) {
        physicsWorld_->WakeBody(object_.id);
    }
}

void ObjectScriptContext::SleepRigidbody() {
    if (!HasRigidbody()) {
        return;
    }
    if (physicsWorld_ && physicsWorld_->HasBody(object_.id)) {
        physicsWorld_->SleepBody(object_.id);
    }
}

bool ObjectScriptContext::HasCharacterController() const {
    return object_.hasCharacterController && object_.characterController.enabled;
}

bool ObjectScriptContext::IsCharacterGrounded() const {
    if (!HasCharacterController()) {
        return false;
    }
    if (physicsWorld_ && physicsWorld_->HasCharacter(object_.id)) {
        PhysicsCharacterState state;
        if (physicsWorld_->GetCharacterState(object_.id, state)) {
            return state.grounded;
        }
    }
    return object_.characterController.grounded;
}

glm::vec3 ObjectScriptContext::GetCharacterVelocity() const {
    if (!HasCharacterController()) {
        return {0.0f, 0.0f, 0.0f};
    }
    if (physicsWorld_ && physicsWorld_->HasCharacter(object_.id)) {
        return physicsWorld_->GetCharacterVelocity(object_.id);
    }
    return object_.characterController.velocity;
}

void ObjectScriptContext::SetCharacterMoveInput(const glm::vec3& value) {
    if (!HasCharacterController()) {
        return;
    }
    object_.characterController.moveInput = value;
    if (physicsWorld_ && physicsWorld_->HasCharacter(object_.id)) {
        physicsWorld_->SetCharacterDesiredVelocity(object_.id, value);
    }
}

void ObjectScriptContext::Jump(float impulse) {
    if (!HasCharacterController()) {
        return;
    }
    object_.characterController.pendingJumpImpulse += impulse;
    if (physicsWorld_ && physicsWorld_->HasCharacter(object_.id)) {
        physicsWorld_->AddCharacterJumpImpulse(object_.id, impulse);
    }
}

bool ObjectScriptContext::IsKeyDown(int key) const {
    return inputManager_ != nullptr && inputManager_->IsKeyDown(key);
}

bool ObjectScriptContext::GetBoolField(const std::string& name, bool fallback) const {
    return GetTypedFieldValue<bool>(attachment_, name, fallback);
}

int ObjectScriptContext::GetIntField(const std::string& name, int fallback) const {
    return GetTypedFieldValue<int>(attachment_, name, fallback);
}

float ObjectScriptContext::GetFloatField(const std::string& name, float fallback) const {
    return GetTypedFieldValue<float>(attachment_, name, fallback);
}

std::string ObjectScriptContext::GetStringField(const std::string& name, const std::string& fallback) const {
    return GetTypedFieldValue<std::string>(attachment_, name, fallback);
}

glm::vec2 ObjectScriptContext::GetVec2Field(const std::string& name, const glm::vec2& fallback) const {
    return GetTypedFieldValue<glm::vec2>(attachment_, name, fallback);
}

glm::vec3 ObjectScriptContext::GetVec3Field(const std::string& name, const glm::vec3& fallback) const {
    return GetTypedFieldValue<glm::vec3>(attachment_, name, fallback);
}

glm::vec4 ObjectScriptContext::GetVec4Field(const std::string& name, const glm::vec4& fallback) const {
    return GetTypedFieldValue<glm::vec4>(attachment_, name, fallback);
}

void ObjectScriptContext::SetBoolField(const std::string& name, bool value) {
    SetTypedFieldValue<bool>(attachment_, name, ScriptFieldType::Bool, value);
}

void ObjectScriptContext::SetIntField(const std::string& name, int value) {
    SetTypedFieldValue<int>(attachment_, name, ScriptFieldType::Int, value);
}

void ObjectScriptContext::SetFloatField(const std::string& name, float value) {
    SetTypedFieldValue<float>(attachment_, name, ScriptFieldType::Float, value);
}

void ObjectScriptContext::SetStringField(const std::string& name, const std::string& value) {
    SetTypedFieldValue<std::string>(attachment_, name, ScriptFieldType::String, value);
}

void ObjectScriptContext::SetVec2Field(const std::string& name, const glm::vec2& value) {
    SetTypedFieldValue<glm::vec2>(attachment_, name, ScriptFieldType::Vec2, value);
}

void ObjectScriptContext::SetVec3Field(const std::string& name, const glm::vec3& value) {
    SetTypedFieldValue<glm::vec3>(attachment_, name, ScriptFieldType::Vec3, value);
}

void ObjectScriptContext::SetVec4Field(const std::string& name, const glm::vec4& value) {
    SetTypedFieldValue<glm::vec4>(attachment_, name, ScriptFieldType::Vec4, value);
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

const std::vector<ScriptFieldDefinition>& IObjectScript::GetFieldDefinitions() const {
    static const std::vector<ScriptFieldDefinition> kNoFields;
    return kNoFields;
}

} // namespace raceman

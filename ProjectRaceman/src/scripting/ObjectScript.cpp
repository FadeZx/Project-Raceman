#include "ObjectScript.h"

#include "../input/InputManager.h"
#include "../physics/PhysicsWorld.h"
#include "../ui/Console.h"
#include "../ui/SceneEditor.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>

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

ObjectScriptContext::ObjectScriptContext(SceneObject& object,
                                         ObjectScriptAttachment* attachment,
                                         Console* console,
                                         InputManager* inputManager,
                                         PhysicsWorld* physicsWorld,
                                         std::vector<SceneObject>* sceneObjects)
    : object_(object),
      attachment_(attachment),
      console_(console),
      inputManager_(inputManager),
      physicsWorld_(physicsWorld),
      sceneObjects_(sceneObjects) {}

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

// ObjectHandle ----------------------------------------------------------------

ObjectScriptContext::ObjectHandle::ObjectHandle(SceneObject* object)
    : object_(object) {}

bool ObjectScriptContext::ObjectHandle::IsValid() const {
    return object_ != nullptr;
}

const std::string& ObjectScriptContext::ObjectHandle::GetObjectId() const {
    static const std::string kEmpty;
    return object_ != nullptr ? object_->id : kEmpty;
}

const std::string& ObjectScriptContext::ObjectHandle::GetObjectName() const {
    static const std::string kEmpty;
    return object_ != nullptr ? object_->name : kEmpty;
}

std::string ObjectScriptContext::ObjectHandle::GetTag() const {
    if (object_ == nullptr) {
        return "Untagged";
    }
    return object_->tag.empty() ? "Untagged" : object_->tag;
}

void ObjectScriptContext::ObjectHandle::SetTag(const std::string& value) const {
    if (object_ != nullptr) {
        object_->tag = value.empty() ? "Untagged" : value;
    }
}

bool ObjectScriptContext::ObjectHandle::CompareTag(const std::string& value) const {
    return IsValid() && GetTag() == value;
}

glm::vec3 ObjectScriptContext::ObjectHandle::GetPosition() const {
    return object_ != nullptr ? object_->transform.position : glm::vec3(0.0f);
}

void ObjectScriptContext::ObjectHandle::SetPosition(const glm::vec3& value) const {
    if (object_ != nullptr) {
        object_->transform.position = value;
    }
}

glm::vec3 ObjectScriptContext::ObjectHandle::GetRotationEuler() const {
    return object_ != nullptr ? object_->transform.rotationEuler : glm::vec3(0.0f);
}

void ObjectScriptContext::ObjectHandle::SetRotationEuler(const glm::vec3& value) const {
    if (object_ != nullptr) {
        object_->transform.rotationEuler = value;
    }
}

glm::vec3 ObjectScriptContext::ObjectHandle::GetScale() const {
    return object_ != nullptr ? object_->transform.scale : glm::vec3(1.0f);
}

void ObjectScriptContext::ObjectHandle::SetScale(const glm::vec3& value) const {
    if (object_ != nullptr) {
        object_->transform.scale = value;
    }
}

std::string ObjectScriptContext::ObjectHandle::GetMaterialId() const {
    return object_ != nullptr ? object_->meshRenderer.materialId : std::string();
}

void ObjectScriptContext::ObjectHandle::SetMaterialId(const std::string& value) const {
    if (object_ != nullptr) {
        object_->meshRenderer.materialId = value;
    }
}

bool ObjectScriptContext::ObjectHandle::IsEnabled() const {
    return object_ != nullptr && object_->enabled;
}

void ObjectScriptContext::ObjectHandle::SetEnabled(bool value) const {
    if (object_ != nullptr) {
        object_->enabled = value;
    }
}

bool ObjectScriptContext::ObjectHandle::HasVirtualCamera() const {
    return object_ != nullptr && object_->hasCinemachine;
}

bool ObjectScriptContext::ObjectHandle::IsVirtualCameraEnabled() const {
    return HasVirtualCamera() && object_->cinemachine.enabled;
}

void ObjectScriptContext::ObjectHandle::SetVirtualCameraEnabled(bool value) const {
    if (HasVirtualCamera()) {
        object_->cinemachine.enabled = value;
    }
}

int ObjectScriptContext::ObjectHandle::GetVirtualCameraPriority() const {
    return HasVirtualCamera() ? object_->cinemachine.priority : 0;
}

void ObjectScriptContext::ObjectHandle::SetVirtualCameraPriority(int value) const {
    if (HasVirtualCamera()) {
        object_->cinemachine.priority = value;
    }
}

const std::string& ObjectScriptContext::GetObjectId() const {
    return object_.id;
}

const std::string& ObjectScriptContext::GetObjectName() const {
    return object_.name;
}

std::string ObjectScriptContext::GetTag() const {
    return object_.tag.empty() ? "Untagged" : object_.tag;
}

void ObjectScriptContext::SetTag(const std::string& value) {
    object_.tag = value.empty() ? "Untagged" : value;
}

bool ObjectScriptContext::CompareTag(const std::string& value) const {
    return GetTag() == value;
}

ObjectScriptContext::ObjectHandle ObjectScriptContext::Self() {
    return ObjectHandle(&object_);
}

ObjectScriptContext::ObjectHandle ObjectScriptContext::FindObjectById(const std::string& id) const {
    if (sceneObjects_ == nullptr || id.empty()) {
        return {};
    }
    auto it = std::find_if(sceneObjects_->begin(), sceneObjects_->end(), [&](const SceneObject& object) {
        return object.id == id;
    });
    return it != sceneObjects_->end() ? ObjectHandle(&(*it)) : ObjectHandle{};
}

ObjectScriptContext::ObjectHandle ObjectScriptContext::FindObjectByName(const std::string& name) const {
    if (sceneObjects_ == nullptr || name.empty()) {
        return {};
    }
    auto it = std::find_if(sceneObjects_->begin(), sceneObjects_->end(), [&](const SceneObject& object) {
        return object.name == name;
    });
    return it != sceneObjects_->end() ? ObjectHandle(&(*it)) : ObjectHandle{};
}

ObjectScriptContext::ObjectHandle ObjectScriptContext::FindObjectWithTag(const std::string& tag) const {
    if (sceneObjects_ == nullptr || tag.empty()) {
        return {};
    }
    auto it = std::find_if(sceneObjects_->begin(), sceneObjects_->end(), [&](const SceneObject& object) {
        const std::string objectTag = object.tag.empty() ? "Untagged" : object.tag;
        return objectTag == tag;
    });
    return it != sceneObjects_->end() ? ObjectHandle(&(*it)) : ObjectHandle{};
}

std::vector<ObjectScriptContext::ObjectHandle> ObjectScriptContext::FindObjectsWithTag(const std::string& tag) const {
    std::vector<ObjectHandle> matches;
    if (sceneObjects_ == nullptr || tag.empty()) {
        return matches;
    }
    for (SceneObject& object : *sceneObjects_) {
        const std::string objectTag = object.tag.empty() ? "Untagged" : object.tag;
        if (objectTag == tag) {
            matches.emplace_back(&object);
        }
    }
    return matches;
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

bool ObjectScriptContext::HasCollider() const {
    return (object_.hasBoxCollider && object_.boxCollider.enabled) ||
           (object_.hasSphereCollider && object_.sphereCollider.enabled) ||
           (object_.hasCapsuleCollider && object_.capsuleCollider.enabled) ||
           (object_.hasPlaneCollider && object_.planeCollider.enabled) ||
           (object_.hasMeshCollider && object_.meshCollider.enabled);
}

bool ObjectScriptContext::IsColliderEnabled() const {
    return HasCollider();
}

void ObjectScriptContext::SetColliderEnabled(bool value) {
    if (object_.hasBoxCollider) object_.boxCollider.enabled = value;
    if (object_.hasSphereCollider) object_.sphereCollider.enabled = value;
    if (object_.hasCapsuleCollider) object_.capsuleCollider.enabled = value;
    if (object_.hasPlaneCollider) object_.planeCollider.enabled = value;
    if (object_.hasMeshCollider) object_.meshCollider.enabled = value;
}

bool ObjectScriptContext::IsColliderTrigger() const {
    if (object_.hasBoxCollider) return object_.boxCollider.isTrigger;
    if (object_.hasSphereCollider) return object_.sphereCollider.isTrigger;
    if (object_.hasCapsuleCollider) return object_.capsuleCollider.isTrigger;
    if (object_.hasPlaneCollider) return object_.planeCollider.isTrigger;
    if (object_.hasMeshCollider) return object_.meshCollider.isTrigger;
    return false;
}

void ObjectScriptContext::SetColliderTrigger(bool value) {
    if (object_.hasBoxCollider) object_.boxCollider.isTrigger = value;
    if (object_.hasSphereCollider) object_.sphereCollider.isTrigger = value;
    if (object_.hasCapsuleCollider) object_.capsuleCollider.isTrigger = value;
    if (object_.hasPlaneCollider) object_.planeCollider.isTrigger = value;
    if (object_.hasMeshCollider) object_.meshCollider.isTrigger = value;
}

glm::vec3 ObjectScriptContext::GetBoxColliderSize() const {
    return object_.hasBoxCollider ? object_.boxCollider.size : glm::vec3(0.0f);
}

void ObjectScriptContext::SetBoxColliderSize(const glm::vec3& value) {
    if (object_.hasBoxCollider) {
        object_.boxCollider.size = {
            (std::max)(0.001f, value.x),
            (std::max)(0.001f, value.y),
            (std::max)(0.001f, value.z)
        };
    }
}

float ObjectScriptContext::GetSphereColliderRadius() const {
    return object_.hasSphereCollider ? object_.sphereCollider.radius : 0.0f;
}

void ObjectScriptContext::SetSphereColliderRadius(float value) {
    if (object_.hasSphereCollider) {
        object_.sphereCollider.radius = (std::max)(0.001f, value);
    }
}

float ObjectScriptContext::GetCapsuleColliderRadius() const {
    return object_.hasCapsuleCollider ? object_.capsuleCollider.radius : 0.0f;
}

void ObjectScriptContext::SetCapsuleColliderRadius(float value) {
    if (object_.hasCapsuleCollider) {
        object_.capsuleCollider.radius = (std::max)(0.001f, value);
        object_.capsuleCollider.height = (std::max)(object_.capsuleCollider.height, object_.capsuleCollider.radius * 2.0f);
    }
}

float ObjectScriptContext::GetCapsuleColliderHeight() const {
    return object_.hasCapsuleCollider ? object_.capsuleCollider.height : 0.0f;
}

void ObjectScriptContext::SetCapsuleColliderHeight(float value) {
    if (object_.hasCapsuleCollider) {
        object_.capsuleCollider.height = (std::max)(object_.capsuleCollider.radius * 2.0f, value);
    }
}

bool ObjectScriptContext::HasLight() const {
    return object_.hasLight && object_.light.enabled;
}

bool ObjectScriptContext::IsLightEnabled() const {
    return HasLight();
}

void ObjectScriptContext::SetLightEnabled(bool value) {
    if (object_.hasLight) {
        object_.light.enabled = value;
    }
}

glm::vec3 ObjectScriptContext::GetLightColor() const {
    return object_.hasLight ? object_.light.color : glm::vec3(0.0f);
}

void ObjectScriptContext::SetLightColor(const glm::vec3& value) {
    if (object_.hasLight) {
        object_.light.color = {
            (std::max)(0.0f, value.x),
            (std::max)(0.0f, value.y),
            (std::max)(0.0f, value.z)
        };
    }
}

float ObjectScriptContext::GetLightIntensity() const {
    return object_.hasLight ? object_.light.intensity : 0.0f;
}

void ObjectScriptContext::SetLightIntensity(float value) {
    if (object_.hasLight) {
        object_.light.intensity = (std::max)(0.0f, value);
    }
}

float ObjectScriptContext::GetLightRange() const {
    return object_.hasLight ? object_.light.range : 0.0f;
}

void ObjectScriptContext::SetLightRange(float value) {
    if (object_.hasLight) {
        object_.light.range = (std::max)(0.001f, value);
    }
}

float ObjectScriptContext::GetLightSpotAngle() const {
    return object_.hasLight ? object_.light.spotAngleDegrees : 0.0f;
}

void ObjectScriptContext::SetLightSpotAngle(float degrees) {
    if (object_.hasLight) {
        object_.light.spotAngleDegrees = (std::max)(1.0f, (std::min)(179.0f, degrees));
    }
}

bool ObjectScriptContext::HasAudioSource() const {
    return object_.hasAudioSource && object_.audioSource.enabled;
}

bool ObjectScriptContext::IsAudioSourceEnabled() const {
    return HasAudioSource();
}

void ObjectScriptContext::SetAudioSourceEnabled(bool value) {
    if (object_.hasAudioSource) {
        object_.audioSource.enabled = value;
    }
}

std::string ObjectScriptContext::GetAudioClipPath() const {
    return object_.hasAudioSource ? object_.audioSource.clipPath : std::string();
}

void ObjectScriptContext::SetAudioClipPath(const std::string& value) {
    if (object_.hasAudioSource) {
        object_.audioSource.clipPath = value;
    }
}

float ObjectScriptContext::GetAudioVolume() const {
    return object_.hasAudioSource ? object_.audioSource.volume : 0.0f;
}

void ObjectScriptContext::SetAudioVolume(float value) {
    if (object_.hasAudioSource) {
        object_.audioSource.volume = (std::max)(0.0f, value);
    }
}

float ObjectScriptContext::GetAudioPitch() const {
    return object_.hasAudioSource ? object_.audioSource.pitch : 0.0f;
}

void ObjectScriptContext::SetAudioPitch(float value) {
    if (object_.hasAudioSource) {
        object_.audioSource.pitch = (std::max)(0.01f, value);
    }
}

bool ObjectScriptContext::IsAudioLooping() const {
    return object_.hasAudioSource && object_.audioSource.loop;
}

void ObjectScriptContext::SetAudioLooping(bool value) {
    if (object_.hasAudioSource) {
        object_.audioSource.loop = value;
    }
}

bool ObjectScriptContext::IsAudioPlayOnAwake() const {
    return object_.hasAudioSource && object_.audioSource.playOnAwake;
}

void ObjectScriptContext::SetAudioPlayOnAwake(bool value) {
    if (object_.hasAudioSource) {
        object_.audioSource.playOnAwake = value;
    }
}

float ObjectScriptContext::GetAudioSpatialBlend() const {
    return object_.hasAudioSource ? object_.audioSource.spatialBlend : 0.0f;
}

void ObjectScriptContext::SetAudioSpatialBlend(float value) {
    if (object_.hasAudioSource) {
        object_.audioSource.spatialBlend = (std::max)(0.0f, (std::min)(1.0f, value));
    }
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

bool ObjectScriptContext::IsMouseButtonDown(int button) const {
    return inputManager_ != nullptr && inputManager_->IsMouseButtonDown(button);
}

bool ObjectScriptContext::WasMouseButtonPressed(int button) const {
    return inputManager_ != nullptr && inputManager_->WasMouseButtonPressed(button);
}

glm::vec2 ObjectScriptContext::GetMouseDelta() const {
    return inputManager_ != nullptr ? inputManager_->GetMouseDelta() : glm::vec2(0.0f);
}

bool ObjectScriptContext::WasKeyPressed(int key) const {
    return inputManager_ != nullptr && inputManager_->WasKeyPressed(key);
}

float ObjectScriptContext::GetMouseWheelDelta() const {
    return inputManager_ != nullptr ? inputManager_->GetMouseWheelDelta() : 0.0f;
}

float ObjectScriptContext::GetAxis(const std::string& action) const {
    return inputManager_ != nullptr ? inputManager_->GetAxis(action) : 0.0f;
}

bool ObjectScriptContext::IsActionDown(const std::string& action) const {
    return inputManager_ != nullptr && inputManager_->IsActionDown(action);
}

bool ObjectScriptContext::WasActionPressed(const std::string& action) const {
    return inputManager_ != nullptr && inputManager_->WasActionPressed(action);
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

ObjectScriptContext::ObjectHandle ObjectScriptContext::GetObjectField(const std::string& name) const {
    return FindObjectById(GetTypedFieldValue<std::string>(attachment_, name, {}));
}

std::vector<ObjectScriptContext::ObjectHandle> ObjectScriptContext::GetObjectListField(const std::string& name) const {
    std::vector<ObjectHandle> result;
    const std::vector<std::string> ids = GetTypedFieldValue<std::vector<std::string>>(attachment_, name, {});
    result.reserve(ids.size());
    for (const std::string& id : ids) {
        result.push_back(FindObjectById(id));
    }
    return result;
}

int ObjectScriptContext::GetKeyField(const std::string& name, int fallback) const {
    return GetTypedFieldValue<int>(attachment_, name, fallback);
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

void ObjectScriptContext::SetObjectField(const std::string& name, const ObjectHandle& value) {
    SetTypedFieldValue<std::string>(attachment_, name, ScriptFieldType::ObjectRef, value.IsValid() ? value.GetObjectId() : std::string{});
}

void ObjectScriptContext::SetObjectListField(const std::string& name, const std::vector<ObjectHandle>& values) {
    std::vector<std::string> ids;
    ids.reserve(values.size());
    for (const ObjectHandle& value : values) {
        ids.push_back(value.IsValid() ? value.GetObjectId() : std::string{});
    }
    SetTypedFieldValue<std::vector<std::string>>(attachment_, name, ScriptFieldType::ObjectRefList, ids);
}

void ObjectScriptContext::SetKeyField(const std::string& name, int value) {
    SetTypedFieldValue<int>(attachment_, name, ScriptFieldType::Key, value);
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

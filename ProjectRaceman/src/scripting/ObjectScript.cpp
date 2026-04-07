#include "ObjectScript.h"

#include "../ui/Console.h"
#include "../ui/SceneEditor.h"

namespace raceman {

ObjectScriptContext::ObjectScriptContext(SceneObject& object, Console* console)
    : object_(object), console_(console) {}

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
    return object_.materialId;
}

void ObjectScriptContext::SetMaterialId(const std::string& value) {
    object_.materialId = value;
}

bool ObjectScriptContext::IsEnabled() const {
    return object_.enabled;
}

void ObjectScriptContext::SetEnabled(bool value) {
    object_.enabled = value;
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

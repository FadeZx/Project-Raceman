#pragma once

#include <string>

#include <glm/glm.hpp>

namespace raceman {

class Console;
class InputManager;
struct SceneObject;

class ObjectScriptContext {
public:
    ObjectScriptContext(SceneObject& object, Console* console, InputManager* inputManager = nullptr);

    const std::string& GetObjectId() const;
    const std::string& GetObjectName() const;

    glm::vec3 GetPosition() const;
    void SetPosition(const glm::vec3& value);

    glm::vec3 GetRotationEuler() const;
    void SetRotationEuler(const glm::vec3& value);

    glm::vec3 GetScale() const;
    void SetScale(const glm::vec3& value);

    std::string GetMaterialId() const;
    void SetMaterialId(const std::string& value);

    bool IsEnabled() const;
    void SetEnabled(bool value);

    bool HasRigidbody() const;
    bool IsRigidbodyDynamic() const;
    glm::vec3 GetRigidbodyVelocity() const;
    void SetRigidbodyVelocity(const glm::vec3& value);

    bool IsKeyDown(int key) const;

    void Log(const std::string& message) const;
    void Warning(const std::string& message) const;
    void Error(const std::string& message) const;

private:
    SceneObject& object_;
    Console* console_{nullptr};
    InputManager* inputManager_{nullptr};
};

class IObjectScript {
public:
    virtual ~IObjectScript() = default;
    virtual void OnStart(ObjectScriptContext& context) {}
    virtual void OnUpdate(ObjectScriptContext& context, float deltaTime) {}
};

} // namespace raceman

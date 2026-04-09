#pragma once

#include <string>

#include <glm/glm.hpp>

namespace raceman {

class Console;
class InputManager;
class PhysicsWorld;
struct CameraComponent;
struct SceneObject;

class ObjectScriptContext {
public:
    class CameraHandle {
    public:
        CameraHandle(SceneObject* object, Console* console);

        bool IsValid() const;

        float GetFieldOfView() const;
        void SetFieldOfView(float degrees) const;

        float GetNearClip() const;
        void SetNearClip(float value) const;

        float GetFarClip() const;
        void SetFarClip(float value) const;

        glm::vec4 GetClearColor() const;
        void SetClearColor(const glm::vec4& value) const;

        bool IsMain() const;
        void SetMain(bool value) const;

    private:
        void WarnInvalid(const std::string& action) const;

    private:
        SceneObject* object_{nullptr};
        Console* console_{nullptr};
    };

    ObjectScriptContext(SceneObject& object, Console* console, InputManager* inputManager = nullptr, PhysicsWorld* physicsWorld = nullptr);

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

    bool HasCharacterController() const;
    bool IsCharacterGrounded() const;
    glm::vec3 GetCharacterVelocity() const;
    void SetCharacterMoveInput(const glm::vec3& value);
    void Jump(float impulse);

    bool HasCamera() const;
    CameraHandle Camera();

    glm::vec3 GetForwardVector() const;
    glm::vec3 GetUpVector() const;

    bool IsKeyDown(int key) const;

    void Log(const std::string& message) const;
    void Warning(const std::string& message) const;
    void Error(const std::string& message) const;

private:
    SceneObject& object_;
    Console* console_{nullptr};
    InputManager* inputManager_{nullptr};
    PhysicsWorld* physicsWorld_{nullptr};
};

class IObjectScript {
public:
    virtual ~IObjectScript() = default;
    virtual void OnStart(ObjectScriptContext& context) {}
    virtual void OnUpdate(ObjectScriptContext& context, float deltaTime) {}
};

} // namespace raceman

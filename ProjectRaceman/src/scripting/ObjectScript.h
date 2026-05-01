#pragma once

#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#if defined(_WIN32)
#if defined(RACEMAN_SCRIPT_DLL)
#define RACEMAN_SCRIPT_API __declspec(dllimport)
#else
#define RACEMAN_SCRIPT_API __declspec(dllexport)
#endif
#else
#define RACEMAN_SCRIPT_API
#endif

namespace raceman {

class Console;
class InputManager;
class PhysicsWorld;
struct ObjectScriptAttachment;
struct CameraComponent;
struct SceneObject;

enum class ScriptFieldType {
    Bool,
    Int,
    Float,
    String,
    Vec2,
    Vec3,
    Vec4
};

using ScriptFieldValue = std::variant<bool, int, float, std::string, glm::vec2, glm::vec3, glm::vec4>;

struct ScriptFieldDefinition {
    std::string name;
    std::string label;
    ScriptFieldType type{ScriptFieldType::Float};
    ScriptFieldValue defaultValue{0.0f};
};

struct ScriptFieldEntry {
    std::string name;
    ScriptFieldType type{ScriptFieldType::Float};
    ScriptFieldValue value{0.0f};
};

inline ScriptFieldDefinition MakeBoolScriptField(std::string name, std::string label, bool defaultValue) {
    return {std::move(name), std::move(label), ScriptFieldType::Bool, defaultValue};
}

inline ScriptFieldDefinition MakeIntScriptField(std::string name, std::string label, int defaultValue) {
    return {std::move(name), std::move(label), ScriptFieldType::Int, defaultValue};
}

inline ScriptFieldDefinition MakeFloatScriptField(std::string name, std::string label, float defaultValue) {
    return {std::move(name), std::move(label), ScriptFieldType::Float, defaultValue};
}

inline ScriptFieldDefinition MakeStringScriptField(std::string name, std::string label, std::string defaultValue) {
    return {std::move(name), std::move(label), ScriptFieldType::String, std::move(defaultValue)};
}

inline ScriptFieldDefinition MakeVec2ScriptField(std::string name, std::string label, glm::vec2 defaultValue) {
    return {std::move(name), std::move(label), ScriptFieldType::Vec2, defaultValue};
}

inline ScriptFieldDefinition MakeVec3ScriptField(std::string name, std::string label, glm::vec3 defaultValue) {
    return {std::move(name), std::move(label), ScriptFieldType::Vec3, defaultValue};
}

inline ScriptFieldDefinition MakeVec4ScriptField(std::string name, std::string label, glm::vec4 defaultValue) {
    return {std::move(name), std::move(label), ScriptFieldType::Vec4, defaultValue};
}

#define RACEMAN_SCRIPT_FIELDS_BEGIN() \
    const std::vector<raceman::ScriptFieldDefinition>& GetFieldDefinitions() const override { \
        static const std::vector<raceman::ScriptFieldDefinition> kFields = {

#define RACEMAN_SCRIPT_FIELD_BOOL(name, label, default_value) raceman::MakeBoolScriptField(name, label, default_value)
#define RACEMAN_SCRIPT_FIELD_INT(name, label, default_value) raceman::MakeIntScriptField(name, label, default_value)
#define RACEMAN_SCRIPT_FIELD_FLOAT(name, label, default_value) raceman::MakeFloatScriptField(name, label, default_value)
#define RACEMAN_SCRIPT_FIELD_STRING(name, label, default_value) raceman::MakeStringScriptField(name, label, default_value)
#define RACEMAN_SCRIPT_FIELD_VEC2(name, label, default_value) raceman::MakeVec2ScriptField(name, label, default_value)
#define RACEMAN_SCRIPT_FIELD_VEC3(name, label, default_value) raceman::MakeVec3ScriptField(name, label, default_value)
#define RACEMAN_SCRIPT_FIELD_VEC4(name, label, default_value) raceman::MakeVec4ScriptField(name, label, default_value)

#define RACEMAN_SCRIPT_FIELDS_END() \
        }; \
        return kFields; \
    }

class RACEMAN_SCRIPT_API ObjectScriptContext {
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

    ObjectScriptContext(SceneObject& object, ObjectScriptAttachment* attachment, Console* console, InputManager* inputManager = nullptr, PhysicsWorld* physicsWorld = nullptr);

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
    glm::vec3 GetRigidbodyAngularVelocity() const;
    void SetRigidbodyAngularVelocity(const glm::vec3& value);
    void AddRigidbodyForce(const glm::vec3& force);
    void AddRigidbodyImpulse(const glm::vec3& impulse);
    void AddRigidbodyTorque(const glm::vec3& torque);
    void AddRigidbodyAngularImpulse(const glm::vec3& impulse);
    void WakeRigidbody();
    void SleepRigidbody();

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
    bool IsMouseButtonDown(int button) const;
    bool WasMouseButtonPressed(int button) const;
    glm::vec2 GetMouseDelta() const;
    float GetAxis(const std::string& action) const;
    bool IsActionDown(const std::string& action) const;
    bool WasActionPressed(const std::string& action) const;

    bool GetBoolField(const std::string& name, bool fallback = false) const;
    int GetIntField(const std::string& name, int fallback = 0) const;
    float GetFloatField(const std::string& name, float fallback = 0.0f) const;
    std::string GetStringField(const std::string& name, const std::string& fallback = {}) const;
    glm::vec2 GetVec2Field(const std::string& name, const glm::vec2& fallback = glm::vec2(0.0f)) const;
    glm::vec3 GetVec3Field(const std::string& name, const glm::vec3& fallback = glm::vec3(0.0f)) const;
    glm::vec4 GetVec4Field(const std::string& name, const glm::vec4& fallback = glm::vec4(0.0f)) const;

    void SetBoolField(const std::string& name, bool value);
    void SetIntField(const std::string& name, int value);
    void SetFloatField(const std::string& name, float value);
    void SetStringField(const std::string& name, const std::string& value);
    void SetVec2Field(const std::string& name, const glm::vec2& value);
    void SetVec3Field(const std::string& name, const glm::vec3& value);
    void SetVec4Field(const std::string& name, const glm::vec4& value);

    void Log(const std::string& message) const;
    void Warning(const std::string& message) const;
    void Error(const std::string& message) const;

private:
    SceneObject& object_;
    ObjectScriptAttachment* attachment_{nullptr};
    Console* console_{nullptr};
    InputManager* inputManager_{nullptr};
    PhysicsWorld* physicsWorld_{nullptr};
};

class RACEMAN_SCRIPT_API IObjectScript {
public:
    virtual ~IObjectScript() = default;
    virtual const std::vector<ScriptFieldDefinition>& GetFieldDefinitions() const;
    virtual void OnStart(ObjectScriptContext& context) {}
    virtual void OnUpdate(ObjectScriptContext& context, float deltaTime) {}
};

} // namespace raceman

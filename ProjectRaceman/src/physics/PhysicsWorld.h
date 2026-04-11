#pragma once

#include "MeshColliderBuildQuality.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace raceman {

enum class PhysicsBodyType {
    Static,
    Kinematic,
    Dynamic
};

enum class PhysicsColliderType {
    Box,
    Sphere,
    Capsule,
    Plane,
    Mesh
};

enum class PhysicsMotionQuality : std::uint8_t {
    Discrete,
    Continuous
};

struct PhysicsColliderDesc {
    PhysicsColliderType type{PhysicsColliderType::Box};
    bool isTrigger{false};
    glm::vec3 center{0.0f};
    glm::vec3 size{1.0f};
    float radius{0.5f};
    float height{2.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float offset{0.0f};
    bool infinite{true};
    float halfExtent{1000.0f};
    std::string meshAssetPath;
    int meshIndex{0};
    MeshColliderBuildQuality meshBuildQuality{MeshColliderBuildQuality::BuildSpeed};
};

struct PhysicsBodyDesc {
    std::string objectId;
    PhysicsBodyType bodyType{PhysicsBodyType::Static};
    glm::vec3 position{0.0f};
    glm::vec3 rotationEuler{0.0f};
    glm::vec3 scale{1.0f};
    float mass{1.0f};
    bool useGravity{true};
    float linearDamping{0.05f};
    float angularDamping{0.05f};
    float friction{0.2f};
    float restitution{0.0f};
    PhysicsMotionQuality motionQuality{PhysicsMotionQuality::Discrete};
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    bool freezePositionX{false};
    bool freezePositionY{false};
    bool freezePositionZ{false};
    bool freezeRotationX{false};
    bool freezeRotationY{false};
    bool freezeRotationZ{false};
    std::vector<PhysicsColliderDesc> colliders;
};

struct PhysicsBodyState {
    std::string objectId;
    glm::vec3 position{0.0f};
    glm::vec3 rotationEuler{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
};

struct PhysicsCharacterDesc {
    std::string objectId;
    glm::vec3 position{0.0f};
    glm::vec3 rotationEuler{0.0f};
    float height{1.8f};
    float radius{0.4f};
    glm::vec3 center{0.0f};
    float stepHeight{0.35f};
    float slopeLimitDegrees{50.0f};
    float maxStrength{100.0f};
    float mass{70.0f};
};

struct PhysicsCharacterState {
    std::string objectId;
    glm::vec3 position{0.0f};
    glm::vec3 rotationEuler{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 groundVelocity{0.0f};
    bool grounded{false};
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void Build(const std::vector<PhysicsBodyDesc>& bodies);
    void Build(const std::vector<PhysicsBodyDesc>& bodies, const std::vector<PhysicsCharacterDesc>& characters);
    void Clear();
    void Step(float deltaTime);

    bool HasBody(const std::string& objectId) const;
    bool GetBodyState(const std::string& objectId, PhysicsBodyState& outState) const;
    glm::vec3 GetBodyVelocity(const std::string& objectId) const;
    void SetBodyVelocity(const std::string& objectId, const glm::vec3& velocity);
    glm::vec3 GetBodyAngularVelocity(const std::string& objectId) const;
    void SetBodyAngularVelocity(const std::string& objectId, const glm::vec3& velocity);
    void AddBodyForce(const std::string& objectId, const glm::vec3& force);
    void AddBodyImpulse(const std::string& objectId, const glm::vec3& impulse);
    void AddBodyTorque(const std::string& objectId, const glm::vec3& torque);
    void AddBodyAngularImpulse(const std::string& objectId, const glm::vec3& impulse);
    void WakeBody(const std::string& objectId);
    void SleepBody(const std::string& objectId);

    bool HasCharacter(const std::string& objectId) const;
    bool GetCharacterState(const std::string& objectId, PhysicsCharacterState& outState) const;
    glm::vec3 GetCharacterVelocity(const std::string& objectId) const;
    void SetCharacterTransform(const std::string& objectId, const glm::vec3& position, const glm::vec3& rotationEuler);
    void SetCharacterDesiredVelocity(const std::string& objectId, const glm::vec3& velocity);
    void AddCharacterJumpImpulse(const std::string& objectId, float impulse);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace raceman

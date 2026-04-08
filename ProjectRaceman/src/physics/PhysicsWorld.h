#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace raceman {

enum class PhysicsBodyType {
    Static,
    Dynamic
};

enum class PhysicsColliderType {
    Box,
    Sphere,
    Capsule
};

struct PhysicsColliderDesc {
    PhysicsColliderType type{PhysicsColliderType::Box};
    bool isTrigger{false};
    glm::vec3 center{0.0f};
    glm::vec3 size{1.0f};
    float radius{0.5f};
    float height{2.0f};
};

struct PhysicsBodyDesc {
    std::string objectId;
    PhysicsBodyType bodyType{PhysicsBodyType::Static};
    glm::vec3 position{0.0f};
    glm::vec3 rotationEuler{0.0f};
    glm::vec3 scale{1.0f};
    float mass{1.0f};
    bool useGravity{true};
    glm::vec3 velocity{0.0f};
    std::vector<PhysicsColliderDesc> colliders;
};

struct PhysicsBodyState {
    std::string objectId;
    glm::vec3 position{0.0f};
    glm::vec3 rotationEuler{0.0f};
    glm::vec3 velocity{0.0f};
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void Build(const std::vector<PhysicsBodyDesc>& bodies);
    void Clear();
    void Step(float deltaTime);

    bool HasBody(const std::string& objectId) const;
    bool GetBodyState(const std::string& objectId, PhysicsBodyState& outState) const;
    glm::vec3 GetBodyVelocity(const std::string& objectId) const;
    void SetBodyVelocity(const std::string& objectId, const glm::vec3& velocity);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace raceman

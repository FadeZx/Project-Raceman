#pragma once

#include "PhysicsLayers.h"
#include "MeshColliderBuildQuality.h"
#include "MeshColliderMode.h"

#include <array>
#include <atomic>
#include <glm/glm.hpp>
#include <mutex>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace raceman {

// Shared state between the main thread (UI) and the background physics build thread.
// All fields that cross the thread boundary use atomics or a mutex.
struct PhysicsBuildProgress {
    std::atomic<int>  stepsDone{0};
    std::atomic<int>  stepsTotal{0};
    std::atomic<bool> cancelRequested{false};
    std::atomic<bool> isDone{false};
    std::atomic<bool> wasCancelled{false};

    // Current task description — mutex-protected so it is safe to write from
    // the build thread and read from the main thread each frame.
    mutable std::mutex taskMutex;
    std::string        currentTask;

    void SetTask(std::string task) {
        std::lock_guard<std::mutex> lk(taskMutex);
        currentTask = std::move(task);
    }
    std::string GetTask() const {
        std::lock_guard<std::mutex> lk(taskMutex);
        return currentTask;
    }
};

struct PhysicsMeshContributorStats {
    std::string meshAssetPath;
    std::string meshName;
    int meshIndex{0};
    std::uint32_t usageCount{0};
    std::uint64_t triangleCount{0};
    MeshColliderMode meshMode{MeshColliderMode::TriangleMesh};
};

struct PhysicsWorldStats {
    std::uint32_t bodyCount{0};
    std::uint32_t characterCount{0};
    std::uint32_t boxColliderCount{0};
    std::uint32_t sphereColliderCount{0};
    std::uint32_t capsuleColliderCount{0};
    std::uint32_t planeColliderCount{0};
    std::uint32_t meshColliderCount{0};
    std::uint32_t triangleMeshColliderCount{0};
    std::uint32_t convexHullColliderCount{0};
    std::uint32_t dynamicBodyCount{0};
    std::uint32_t activeDynamicCount{0};
    double lastBuildTimeMs{0.0};
    double lastStepTimeMs{0.0};
    std::vector<PhysicsMeshContributorStats> meshContributors;
};

// Snapshot used for debug visualisation of the spatial activation culling.
struct PhysicsCullingDebugInfo {
    struct BodyDebug {
        glm::vec3 position;
        bool isActive{false};
    };
    bool hasActivators{false};
    float activationRadius{0.0f};
    float deactivationRadius{0.0f};
    std::vector<glm::vec3> activatorPositions;
    std::vector<BodyDebug> dynamicBodies;
};

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

enum class CollisionShapeCacheStatus : std::uint8_t {
    Missing,
    Ready,
    Stale,
    Failed
};

struct CollisionShapeCacheInfo {
    CollisionShapeCacheStatus status{CollisionShapeCacheStatus::Missing};
    std::uint64_t triangleCount{0};
    std::string cachePath;
    std::string message;
};

struct PhysicsColliderDesc {
    PhysicsColliderType type{PhysicsColliderType::Box};
    bool isTrigger{false};
    glm::vec3 center{0.0f};
    glm::vec3 rotationEuler{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 size{1.0f};
    float radius{0.5f};
    float height{2.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float offset{0.0f};
    bool infinite{true};
    float halfExtent{1000.0f};
    std::string meshAssetPath;
    std::string meshName;
    int meshIndex{0};
    glm::vec3 meshPivotOffset{0.0f}; // vertex-space offset applied as translate(-offset) before body placement
    MeshColliderBuildQuality meshBuildQuality{MeshColliderBuildQuality::BuildQuality};
    MeshColliderMode meshMode{MeshColliderMode::TriangleMesh};
};

struct PhysicsBodyDesc {
    std::string objectId;
    int collisionLayer{0};
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
    bool overrideCenterOfMass{false};
    glm::vec3 centerOfMassOffset{0.0f};
    bool overrideMassProperties{false};
    glm::vec3 inertiaDiagonal{0.0f};
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

struct PhysicsRaycastHit {
    bool hit{false};
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance{0.0f};
    std::string objectId;
};

class PhysicsWorld {
public:
    explicit PhysicsWorld(const PhysicsLayerCollisionMatrix& collisionMatrix = {});
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void Build(const std::vector<PhysicsBodyDesc>& bodies);
    void Build(const std::vector<PhysicsBodyDesc>& bodies, const std::vector<PhysicsCharacterDesc>& characters);
    // Async-friendly overload: reports per-body progress and supports cancellation.
    // Pass a non-null PhysicsBuildProgress* — the caller owns its lifetime.
    void Build(const std::vector<PhysicsBodyDesc>& bodies, const std::vector<PhysicsCharacterDesc>& characters,
               PhysicsBuildProgress* progress);
    void Clear();
    void Step(float deltaTime);

    bool HasBody(const std::string& objectId) const;
    bool GetBodyState(const std::string& objectId, PhysicsBodyState& outState) const;
    glm::vec3 GetBodyVelocity(const std::string& objectId) const;
    void SetBodyVelocity(const std::string& objectId, const glm::vec3& velocity);
    glm::vec3 GetBodyAngularVelocity(const std::string& objectId) const;
    void SetBodyAngularVelocity(const std::string& objectId, const glm::vec3& velocity);
    void MoveBodyKinematic(const std::string& objectId, const glm::vec3& position, const glm::vec3& rotationEuler, float deltaTime);
    void AddBodyForce(const std::string& objectId, const glm::vec3& force);
    void AddBodyForceAtPosition(const std::string& objectId, const glm::vec3& force, const glm::vec3& position);
    void AddBodyImpulse(const std::string& objectId, const glm::vec3& impulse);
    void AddBodyTorque(const std::string& objectId, const glm::vec3& torque);
    void AddBodyAngularImpulse(const std::string& objectId, const glm::vec3& impulse);
    void WakeBody(const std::string& objectId);
    void SleepBody(const std::string& objectId);

    // Spatial activation culling. Call each frame with the world positions of active
    // drivers/cameras. Dynamic bodies farther than deactivationRadius from every activator
    // are put to sleep; bodies closer than activationRadius are woken. Hysteresis between
    // the two radii prevents rapid toggling. Pass an empty vector to disable culling.
    void SetActivatorPositions(const std::vector<glm::vec3>& positions,
                               float activationRadius   = 150.0f,
                               float deactivationRadius = 200.0f);

    bool HasCharacter(const std::string& objectId) const;
    bool GetCharacterState(const std::string& objectId, PhysicsCharacterState& outState) const;
    glm::vec3 GetCharacterVelocity(const std::string& objectId) const;
    void SetCharacterTransform(const std::string& objectId, const glm::vec3& position, const glm::vec3& rotationEuler);
    void SetCharacterDesiredVelocity(const std::string& objectId, const glm::vec3& velocity);
    void AddCharacterJumpImpulse(const std::string& objectId, float impulse);

    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, PhysicsRaycastHit& outHit, const std::string* ignoreObjectId = nullptr) const;
    bool RaycastIgnoring(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, PhysicsRaycastHit& outHit, const std::unordered_set<std::string>& ignoreObjectIds) const;
    const PhysicsWorldStats& GetStats() const;
    PhysicsCullingDebugInfo GetCullingDebugInfo() const;
    static std::string GetCollisionShapeCacheDirectory();
    static int ClearCollisionShapeCache(std::string* outError = nullptr);
    static CollisionShapeCacheInfo GetCollisionShapeCacheInfo(const PhysicsColliderDesc& collider);
    static bool BakeCollisionShape(const PhysicsColliderDesc& collider, CollisionShapeCacheInfo* outInfo = nullptr);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace raceman

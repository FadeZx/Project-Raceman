#include "PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace raceman {

namespace {

float MaxAbsComponent(const glm::vec3& value) {
    return (std::max)((std::max)(std::abs(value.x), std::abs(value.y)), std::abs(value.z));
}

namespace Layers {
static constexpr JPH::ObjectLayer NonMoving = 0;
static constexpr JPH::ObjectLayer Moving = 1;
static constexpr JPH::ObjectLayer Sensor = 2;
static constexpr JPH::ObjectLayer NumLayers = 3;
}

namespace BroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer NonMoving(0);
static constexpr JPH::BroadPhaseLayer Moving(1);
static constexpr JPH::BroadPhaseLayer Sensor(2);
static constexpr JPH::uint NumLayers = 3;
}

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerInterfaceImpl() {
        objectToBroadPhase_[Layers::NonMoving] = BroadPhaseLayers::NonMoving;
        objectToBroadPhase_[Layers::Moving] = BroadPhaseLayers::Moving;
        objectToBroadPhase_[Layers::Sensor] = BroadPhaseLayers::Sensor;
    }

    JPH::uint GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NumLayers;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return objectToBroadPhase_[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(layer)) {
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NonMoving): return "NonMoving";
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::Moving): return "Moving";
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::Sensor): return "Sensor";
        default: return "Invalid";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer objectToBroadPhase_[Layers::NumLayers];
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override {
        if (first == Layers::Sensor || second == Layers::Sensor) {
            return first != second;
        }
        if (first == Layers::NonMoving) {
            return second == Layers::Moving;
        }
        return true;
    }
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadphaseLayer) const override {
        if (layer == Layers::Sensor) {
            return broadphaseLayer != BroadPhaseLayers::Sensor;
        }
        if (layer == Layers::NonMoving) {
            return broadphaseLayer == BroadPhaseLayers::Moving;
        }
        return true;
    }
};

void EnsureJoltInitialized() {
    static bool initialized = false;
    if (initialized) {
        return;
    }

    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    initialized = true;
}

JPH::Vec3 ToJoltVec3(const glm::vec3& value) {
    return JPH::Vec3(value.x, value.y, value.z);
}

JPH::RVec3 ToJoltRVec3(const glm::vec3& value) {
    return JPH::RVec3(value.x, value.y, value.z);
}

glm::vec3 FromJoltVec3(JPH::Vec3Arg value) {
    return {value.GetX(), value.GetY(), value.GetZ()};
}

glm::vec3 FromJoltRVec3(JPH::RVec3Arg value) {
    return {
        static_cast<float>(value.GetX()),
        static_cast<float>(value.GetY()),
        static_cast<float>(value.GetZ())
    };
}

JPH::Quat ToJoltQuat(const glm::vec3& rotationEuler) {
    const glm::quat quat = glm::quat(glm::radians(rotationEuler));
    return JPH::Quat(quat.x, quat.y, quat.z, quat.w);
}

glm::vec3 FromJoltQuat(JPH::QuatArg quat) {
    const glm::quat glmQuat(quat.GetW(), quat.GetX(), quat.GetY(), quat.GetZ());
    return glm::degrees(glm::eulerAngles(glmQuat));
}

JPH::ShapeRefC CreateCharacterShape(float height, float radius) {
    const float clampedRadius = (std::max)(0.001f, radius);
    const float clampedHeight = (std::max)(clampedRadius * 2.0f, height);
    const float halfCylinderHeight = (std::max)(0.001f, clampedHeight * 0.5f - clampedRadius);
    return new JPH::CapsuleShape(halfCylinderHeight, clampedRadius);
}

JPH::EAllowedDOFs ToAllowedDOFs(const PhysicsBodyDesc& body) {
    JPH::EAllowedDOFs allowed = JPH::EAllowedDOFs::All;
    if (body.freezePositionX) allowed &= ~JPH::EAllowedDOFs::TranslationX;
    if (body.freezePositionY) allowed &= ~JPH::EAllowedDOFs::TranslationY;
    if (body.freezePositionZ) allowed &= ~JPH::EAllowedDOFs::TranslationZ;
    if (body.freezeRotationX) allowed &= ~JPH::EAllowedDOFs::RotationX;
    if (body.freezeRotationY) allowed &= ~JPH::EAllowedDOFs::RotationY;
    if (body.freezeRotationZ) allowed &= ~JPH::EAllowedDOFs::RotationZ;
    return allowed == JPH::EAllowedDOFs::None ? JPH::EAllowedDOFs::All : allowed;
}

JPH::ShapeRefC CreateBaseShape(const PhysicsColliderDesc& collider, const glm::vec3& scale) {
    if (collider.type == PhysicsColliderType::Box) {
        const glm::vec3 halfExtent = glm::max(glm::abs(collider.size * scale) * 0.5f, glm::vec3(0.001f));
        return new JPH::BoxShape(ToJoltVec3(halfExtent));
    }
    if (collider.type == PhysicsColliderType::Sphere) {
        return new JPH::SphereShape((std::max)(0.001f, collider.radius * MaxAbsComponent(scale)));
    }
    if (collider.type == PhysicsColliderType::Plane) {
        glm::vec3 normal = collider.normal;
        if (glm::length2(normal) <= 0.000001f) {
            normal = {0.0f, 1.0f, 0.0f};
        } else {
            normal = glm::normalize(normal);
        }
        const JPH::Plane plane(ToJoltVec3(normal), -collider.offset);
        const float halfExtent = collider.infinite
            ? JPH::PlaneShapeSettings::cDefaultHalfExtent
            : (std::max)(0.001f, collider.halfExtent);
        return new JPH::PlaneShape(plane, nullptr, halfExtent);
    }

    const float radius = (std::max)(0.001f, collider.radius * (std::max)(std::abs(scale.x), std::abs(scale.z)));
    const float scaledHeight = (std::max)(radius * 2.0f, collider.height * std::abs(scale.y));
    const float halfCylinderHeight = (std::max)(0.001f, (scaledHeight * 0.5f) - radius);
    return new JPH::CapsuleShape(halfCylinderHeight, radius);
}

JPH::ShapeRefC CreateShape(const PhysicsBodyDesc& body, bool sensorOnly) {
    std::vector<JPH::ShapeRefC> shapes;
    std::vector<JPH::Vec3> centers;
    for (const PhysicsColliderDesc& collider : body.colliders) {
        if (sensorOnly != collider.isTrigger) {
            continue;
        }
        shapes.push_back(CreateBaseShape(collider, body.scale));
        centers.push_back(ToJoltVec3(collider.center * body.scale));
    }

    if (shapes.empty()) {
        return {};
    }
    if (shapes.size() == 1) {
        if (centers[0].LengthSq() <= 0.000001f) {
            return shapes[0];
        }
        JPH::RotatedTranslatedShapeSettings offsetSettings(centers[0], JPH::Quat::sIdentity(), shapes[0]);
        JPH::ShapeSettings::ShapeResult result = offsetSettings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC{};
    }

    JPH::StaticCompoundShapeSettings compoundSettings;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        compoundSettings.AddShape(centers[i], JPH::Quat::sIdentity(), shapes[i]);
    }
    JPH::ShapeSettings::ShapeResult result = compoundSettings.Create();
    return result.IsValid() ? result.Get() : JPH::ShapeRefC{};
}

} // namespace

class PhysicsWorld::Impl {
public:
    struct CharacterRecord {
        PhysicsCharacterDesc desc;
        PhysicsCharacterState state;
        JPH::Ref<JPH::CharacterVirtual> character;
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        glm::vec3 desiredVelocity{0.0f};
        float pendingJumpImpulse{0.0f};
    };

    void Build(const std::vector<PhysicsBodyDesc>& inputBodies) {
        Build(inputBodies, {});
    }

    void Build(const std::vector<PhysicsBodyDesc>& inputBodies, const std::vector<PhysicsCharacterDesc>& inputCharacters) {
        Clear();
        EnsureJoltInitialized();

        bodies_ = inputBodies;
        for (const PhysicsBodyDesc& body : bodies_) {
            states_[body.objectId] = {body.objectId, body.position, body.rotationEuler, body.velocity, body.angularVelocity};
        }

        tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
        const int workerCount = (std::max)(1u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u);
        jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerCount);
        physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
        physicsSystem_->Init(65536, 0, 65536, 10240, broadPhaseLayerInterface_, objectVsBroadPhaseLayerFilter_, objectLayerPairFilter_);

        JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
        for (const PhysicsBodyDesc& body : bodies_) {
            const bool hasPlaneCollider = std::any_of(body.colliders.begin(), body.colliders.end(), [](const PhysicsColliderDesc& collider) {
                return collider.type == PhysicsColliderType::Plane;
            });
            const bool hasSolidCollider = std::any_of(body.colliders.begin(), body.colliders.end(), [](const PhysicsColliderDesc& collider) {
                return !collider.isTrigger;
            });
            const bool sensorOnly = !hasSolidCollider;
            JPH::ShapeRefC shape = CreateShape(body, sensorOnly);
            if (!shape) {
                continue;
            }

            const bool movable = body.bodyType != PhysicsBodyType::Static && !hasPlaneCollider;
            const bool dynamic = body.bodyType == PhysicsBodyType::Dynamic && !hasPlaneCollider;
            const JPH::EMotionType motionType = dynamic
                ? JPH::EMotionType::Dynamic
                : (movable ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static);
            const JPH::ObjectLayer layer = sensorOnly ? Layers::Sensor : (movable ? Layers::Moving : Layers::NonMoving);
            JPH::BodyCreationSettings settings(shape, ToJoltRVec3(body.position), ToJoltQuat(body.rotationEuler), motionType, layer);
            settings.mIsSensor = sensorOnly;
            settings.mGravityFactor = body.useGravity ? 1.0f : 0.0f;
            settings.mLinearDamping = (std::max)(0.0f, body.linearDamping);
            settings.mAngularDamping = (std::max)(0.0f, body.angularDamping);
            settings.mAllowedDOFs = ToAllowedDOFs(body);
            settings.mLinearVelocity = ToJoltVec3(body.velocity);
            settings.mAngularVelocity = ToJoltVec3(body.angularVelocity);
            if (movable) {
                settings.mAllowDynamicOrKinematic = true;
            }
            if (dynamic) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = (std::max)(0.001f, body.mass);
            }

            JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, movable ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            bodyIds_[body.objectId] = bodyId;
            if (movable) {
                activeBodies_.insert(body.objectId);
            }
        }

        physicsSystem_->OptimizeBroadPhase();

        const JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(objectVsBroadPhaseLayerFilter_, Layers::Moving);
        const JPH::DefaultObjectLayerFilter objectLayerFilter(objectLayerPairFilter_, Layers::Moving);
        const JPH::BodyFilter bodyFilter;
        const JPH::ShapeFilter shapeFilter;
        for (const PhysicsCharacterDesc& desc : inputCharacters) {
            CharacterRecord record;
            record.desc = desc;
            record.state.objectId = desc.objectId;
            record.state.position = desc.position;
            record.state.rotationEuler = desc.rotationEuler;

            JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
            settings->mMass = (std::max)(0.001f, desc.mass);
            settings->mMaxStrength = (std::max)(0.0f, desc.maxStrength);
            settings->mMaxSlopeAngle = JPH::DegreesToRadians((std::max)(1.0f, (std::min)(89.0f, desc.slopeLimitDegrees)));
            settings->mShape = CreateCharacterShape(desc.height, desc.radius);
            settings->mShapeOffset = ToJoltVec3(desc.center + glm::vec3(0.0f, (std::max)(desc.height, desc.radius * 2.0f) * 0.5f, 0.0f));

            record.updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -(std::max)(0.05f, desc.stepHeight + 0.05f), 0.0f);
            record.updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, (std::max)(0.0f, desc.stepHeight), 0.0f);

            record.character = new JPH::CharacterVirtual(settings.GetPtr(), ToJoltRVec3(desc.position), ToJoltQuat(desc.rotationEuler), physicsSystem_.get());
            record.character->SetCharacterVsCharacterCollision(&characterVsCharacterCollision_);
            characterVsCharacterCollision_.Add(record.character.GetPtr());
            record.character->RefreshContacts(broadPhaseFilter, objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator_);
            characterStates_[desc.objectId] = record.state;
            characters_[desc.objectId] = std::move(record);
        }
    }

    void Clear() {
        characterVsCharacterCollision_.mCharacters.clear();
        characters_.clear();
        characterStates_.clear();
        if (physicsSystem_) {
            JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
            for (auto& [objectId, bodyId] : bodyIds_) {
                if (!bodyId.IsInvalid()) {
                    bodyInterface.RemoveBody(bodyId);
                    bodyInterface.DestroyBody(bodyId);
                }
            }
        }

        bodyIds_.clear();
        activeBodies_.clear();
        physicsSystem_.reset();
        jobSystem_.reset();
        tempAllocator_.reset();
        bodies_.clear();
        states_.clear();
    }

    void Step(float deltaTime) {
        if (deltaTime <= 0.0f || !physicsSystem_) {
            return;
        }

        const float step = (std::min)(deltaTime, 0.05f);
        JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
        for (const std::string& objectId : activeBodies_) {
            auto stateIt = states_.find(objectId);
            auto bodyIt = bodyIds_.find(objectId);
            if (stateIt == states_.end() || bodyIt == bodyIds_.end()) {
                continue;
            }
            bodyInterface.SetLinearVelocity(bodyIt->second, ToJoltVec3(stateIt->second.velocity));
            bodyInterface.SetAngularVelocity(bodyIt->second, ToJoltVec3(stateIt->second.angularVelocity));
        }

        physicsSystem_->Update(step, 1, tempAllocator_.get(), jobSystem_.get());
        for (const std::string& objectId : activeBodies_) {
            auto stateIt = states_.find(objectId);
            auto bodyIt = bodyIds_.find(objectId);
            if (stateIt == states_.end() || bodyIt == bodyIds_.end()) {
                continue;
            }
            stateIt->second.position = FromJoltRVec3(bodyInterface.GetPosition(bodyIt->second));
            stateIt->second.rotationEuler = FromJoltQuat(bodyInterface.GetRotation(bodyIt->second));
            stateIt->second.velocity = FromJoltVec3(bodyInterface.GetLinearVelocity(bodyIt->second));
            stateIt->second.angularVelocity = FromJoltVec3(bodyInterface.GetAngularVelocity(bodyIt->second));
        }

        const JPH::Vec3 gravity = JPH::Vec3(0.0f, -9.81f, 0.0f);
        const JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(objectVsBroadPhaseLayerFilter_, Layers::Moving);
        const JPH::DefaultObjectLayerFilter objectLayerFilter(objectLayerPairFilter_, Layers::Moving);
        const JPH::BodyFilter bodyFilter;
        const JPH::ShapeFilter shapeFilter;
        for (auto& [objectId, record] : characters_) {
            JPH::CharacterVirtual* character = record.character.GetPtr();
            if (character == nullptr) {
                continue;
            }

            const glm::vec3 currentVelocity = FromJoltVec3(character->GetLinearVelocity());
            const glm::vec3 groundVelocity = FromJoltVec3(character->GetGroundVelocity());
            const bool supported = character->IsSupported();
            glm::vec3 nextVelocity = record.desiredVelocity;
            nextVelocity.y = supported ? 0.0f : currentVelocity.y + gravity.GetY() * step;
            if (supported) {
                nextVelocity += groundVelocity;
            }
            if (record.pendingJumpImpulse > 0.0f && supported) {
                nextVelocity.y = record.pendingJumpImpulse;
            }

            character->SetRotation(ToJoltQuat(record.state.rotationEuler));
            character->SetLinearVelocity(ToJoltVec3(nextVelocity));
            character->ExtendedUpdate(step, gravity, record.updateSettings, broadPhaseFilter, objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator_);

            record.pendingJumpImpulse = 0.0f;
            record.state.position = FromJoltRVec3(character->GetPosition());
            record.state.rotationEuler = FromJoltQuat(character->GetRotation());
            record.state.velocity = FromJoltVec3(character->GetLinearVelocity());
            record.state.groundVelocity = FromJoltVec3(character->GetGroundVelocity());
            record.state.grounded = character->IsSupported();
            characterStates_[objectId] = record.state;
        }
    }

    bool HasBody(const std::string& objectId) const {
        return bodyIds_.find(objectId) != bodyIds_.end();
    }

    bool GetBodyState(const std::string& objectId, PhysicsBodyState& outState) const {
        auto it = states_.find(objectId);
        if (it == states_.end()) {
            return false;
        }
        outState = it->second;
        return true;
    }

    glm::vec3 GetBodyVelocity(const std::string& objectId) const {
        auto it = states_.find(objectId);
        return it == states_.end() ? glm::vec3{0.0f} : it->second.velocity;
    }

    void SetBodyVelocity(const std::string& objectId, const glm::vec3& velocity) {
        auto stateIt = states_.find(objectId);
        if (stateIt != states_.end()) {
            stateIt->second.velocity = velocity;
        }
        if (!physicsSystem_ || activeBodies_.find(objectId) == activeBodies_.end()) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().SetLinearVelocity(bodyIt->second, ToJoltVec3(velocity));
        }
    }

    glm::vec3 GetBodyAngularVelocity(const std::string& objectId) const {
        auto it = states_.find(objectId);
        return it == states_.end() ? glm::vec3{0.0f} : it->second.angularVelocity;
    }

    void SetBodyAngularVelocity(const std::string& objectId, const glm::vec3& velocity) {
        auto stateIt = states_.find(objectId);
        if (stateIt != states_.end()) {
            stateIt->second.angularVelocity = velocity;
        }
        if (!physicsSystem_ || activeBodies_.find(objectId) == activeBodies_.end()) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().SetAngularVelocity(bodyIt->second, ToJoltVec3(velocity));
        }
    }

    bool HasCharacter(const std::string& objectId) const {
        return characters_.find(objectId) != characters_.end();
    }

    bool GetCharacterState(const std::string& objectId, PhysicsCharacterState& outState) const {
        auto it = characterStates_.find(objectId);
        if (it == characterStates_.end()) {
            return false;
        }
        outState = it->second;
        return true;
    }

    glm::vec3 GetCharacterVelocity(const std::string& objectId) const {
        auto it = characterStates_.find(objectId);
        return it == characterStates_.end() ? glm::vec3{0.0f} : it->second.velocity;
    }

    void SetCharacterTransform(const std::string& objectId, const glm::vec3& position, const glm::vec3& rotationEuler) {
        auto it = characters_.find(objectId);
        if (it == characters_.end() || it->second.character == nullptr) {
            return;
        }
        it->second.state.position = position;
        it->second.state.rotationEuler = rotationEuler;
        it->second.character->SetPosition(ToJoltRVec3(position));
        it->second.character->SetRotation(ToJoltQuat(rotationEuler));
        characterStates_[objectId] = it->second.state;
    }

    void SetCharacterDesiredVelocity(const std::string& objectId, const glm::vec3& velocity) {
        auto it = characters_.find(objectId);
        if (it == characters_.end()) {
            return;
        }
        it->second.desiredVelocity = velocity;
    }

    void AddCharacterJumpImpulse(const std::string& objectId, float impulse) {
        auto it = characters_.find(objectId);
        if (it == characters_.end()) {
            return;
        }
        it->second.pendingJumpImpulse += impulse;
    }

private:
    std::vector<PhysicsBodyDesc> bodies_;
    std::unordered_map<std::string, PhysicsBodyState> states_;
    std::unordered_map<std::string, JPH::BodyID> bodyIds_;
    std::unordered_set<std::string> activeBodies_;
    std::unordered_map<std::string, CharacterRecord> characters_;
    std::unordered_map<std::string, PhysicsCharacterState> characterStates_;
    JPH::CharacterVsCharacterCollisionSimple characterVsCharacterCollision_;

    BroadPhaseLayerInterfaceImpl broadPhaseLayerInterface_;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter_;
    ObjectLayerPairFilterImpl objectLayerPairFilter_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
};

PhysicsWorld::PhysicsWorld()
    : impl_(std::make_unique<Impl>()) {}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::Build(const std::vector<PhysicsBodyDesc>& bodies) {
    impl_->Build(bodies);
}

void PhysicsWorld::Build(const std::vector<PhysicsBodyDesc>& bodies, const std::vector<PhysicsCharacterDesc>& characters) {
    impl_->Build(bodies, characters);
}

void PhysicsWorld::Clear() {
    impl_->Clear();
}

void PhysicsWorld::Step(float deltaTime) {
    impl_->Step(deltaTime);
}

bool PhysicsWorld::HasBody(const std::string& objectId) const {
    return impl_->HasBody(objectId);
}

bool PhysicsWorld::GetBodyState(const std::string& objectId, PhysicsBodyState& outState) const {
    return impl_->GetBodyState(objectId, outState);
}

glm::vec3 PhysicsWorld::GetBodyVelocity(const std::string& objectId) const {
    return impl_->GetBodyVelocity(objectId);
}

void PhysicsWorld::SetBodyVelocity(const std::string& objectId, const glm::vec3& velocity) {
    impl_->SetBodyVelocity(objectId, velocity);
}

glm::vec3 PhysicsWorld::GetBodyAngularVelocity(const std::string& objectId) const {
    return impl_->GetBodyAngularVelocity(objectId);
}

void PhysicsWorld::SetBodyAngularVelocity(const std::string& objectId, const glm::vec3& velocity) {
    impl_->SetBodyAngularVelocity(objectId, velocity);
}

bool PhysicsWorld::HasCharacter(const std::string& objectId) const {
    return impl_->HasCharacter(objectId);
}

bool PhysicsWorld::GetCharacterState(const std::string& objectId, PhysicsCharacterState& outState) const {
    return impl_->GetCharacterState(objectId, outState);
}

glm::vec3 PhysicsWorld::GetCharacterVelocity(const std::string& objectId) const {
    return impl_->GetCharacterVelocity(objectId);
}

void PhysicsWorld::SetCharacterTransform(const std::string& objectId, const glm::vec3& position, const glm::vec3& rotationEuler) {
    impl_->SetCharacterTransform(objectId, position, rotationEuler);
}

void PhysicsWorld::SetCharacterDesiredVelocity(const std::string& objectId, const glm::vec3& velocity) {
    impl_->SetCharacterDesiredVelocity(objectId, velocity);
}

void PhysicsWorld::AddCharacterJumpImpulse(const std::string& objectId, float impulse) {
    impl_->AddCharacterJumpImpulse(objectId, impulse);
}

} // namespace raceman

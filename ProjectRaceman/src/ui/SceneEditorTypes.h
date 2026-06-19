#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "../input/InputManager.h"
#include "../physics/MeshColliderBuildQuality.h"
#include "../physics/MeshColliderMode.h"
#include "../scripting/ObjectScript.h"

class Model;

namespace raceman {

enum class GizmoMode {
    Move,
    Rotate,
    Scale
};

enum class ProjectAssetPickerMode {
    None,
    ReplaceMesh,
    AssignMaterial,
    AttachScript,
    AssignVehicleConfig
};

enum class ProjectCreateAssetType {
    None,
    Folder,
    Scene,
    Material,
    VehicleProfile,
    VehicleSoundProfile,
    Track,
    Script,
    ShaderGraph
};

enum class SceneComponentType {
    Transform,
    MeshFilter,
    MeshRenderer,
    Script,
    Rigidbody,
    Vehicle,
    CharacterController,
    BoxCollider,
    SphereCollider,
    CapsuleCollider,
    PlaneCollider,
    Camera,
    Light,
    AudioListener,
    AudioSource,
    VehicleSound,
    TrackGenerator
};

enum class SceneInspectorComponentType {
    Transform,
    MeshFilter,
    MeshRenderer,
    Script,
    Rigidbody,
    Vehicle,
    CharacterController,
    Collider,
    Camera,
    Cinemachine,
    Light,
    AudioListener,
    AudioSource,
    VehicleSound,
    TrackGenerator
};

enum class RigidbodyBodyType {
    Static,
    Kinematic,
    Dynamic
};

enum class SceneEditorActiveViewport {
    None,
    Scene,
    Game
};

struct ShaderGraphNodeState {
    int id{0};
    std::string type;
    std::string title;
    std::string noteText;
    std::string textureSlot{"albedo"};
    glm::vec2 position{0.0f, 0.0f};
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float vectorValue[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float floatValue{0.0f};
};

struct ShaderGraphLinkState {
    int id{0};
    int startPin{0};
    int endPin{0};
};

struct ShaderGraphHistoryState {
    std::string name;
    std::vector<ShaderGraphNodeState> nodes;
    std::vector<ShaderGraphLinkState> links;
    int nextNodeId{101};
    int nextLinkId{1};
    int selectedNodeId{0};
};

enum class LightType {
    Directional,
    Point,
    Spot
};

struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEuler{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

struct MeshFilterComponent {
    bool enabled{true};
    std::string meshType;
    std::string sourcePath;
    int meshIndex{0};
    std::string meshName;
    glm::vec3 pivotOffset{0.0f, 0.0f, 0.0f};
    std::string importedMaterialName;
    std::string diffuseTexturePath;
    unsigned int diffuseTextureId{0};
    unsigned int vao{0};
    unsigned int indexCount{0};
    glm::vec3 localBoundsMin{-0.5f, -0.5f, -0.5f};
    glm::vec3 localBoundsMax{0.5f, 0.5f, 0.5f};
    std::vector<glm::vec3> pickVertices;
    std::vector<unsigned int> pickIndices;
    std::shared_ptr<::Model> modelRef;
};

struct MeshRendererComponent {
    bool enabled{true};
    std::string materialId{"pbr_default"};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct ObjectScriptAttachment {
    bool enabled{true};
    std::string scriptName;
    std::string scriptPath;
    std::vector<ScriptFieldEntry> fields;
};

struct ScriptComponent {
    bool enabled{true};
    std::vector<ObjectScriptAttachment> attachments;
};

struct RigidbodyComponent {
    bool enabled{true};
    RigidbodyBodyType bodyType{RigidbodyBodyType::Dynamic};
    float mass{1.0f};
    bool useGravity{true};
    float linearDamping{0.05f};
    float angularDamping{0.05f};
    float friction{0.2f};
    float restitution{0.0f};
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    glm::vec3 angularVelocity{0.0f, 0.0f, 0.0f};
    bool freezePositionX{false};
    bool freezePositionY{false};
    bool freezePositionZ{false};
    bool freezeRotationX{false};
    bool freezeRotationY{false};
    bool freezeRotationZ{false};
};

struct CharacterControllerComponent {
    bool enabled{true};
    float height{1.8f};
    float radius{0.4f};
    glm::vec3 center{0.0f, 0.0f, 0.0f};
    float stepHeight{0.35f};
    float slopeLimitDegrees{50.0f};
    float maxStrength{100.0f};
    float mass{70.0f};
    bool grounded{false};
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    glm::vec3 groundVelocity{0.0f, 0.0f, 0.0f};
    glm::vec3 moveInput{0.0f, 0.0f, 0.0f};
    float pendingJumpImpulse{0.0f};
};

struct VehicleWheelBinding {
    std::string wheelName;
    std::string objectId;
    glm::vec3 visualRotationEuler{0.0f, 0.0f, 0.0f};
};

struct VehicleComponent {
    bool enabled{true};
    bool canTilt{true};
    std::string configPath;
    std::string inputProfileId{"default_vehicle"};
    InputDevicePreference preferredInputDevice{InputDevicePreference::Any};
    std::string preferredInputDeviceId;
    std::vector<std::string> chassisObjectIds;
    std::vector<VehicleWheelBinding> wheelBindings;
};

struct BoxColliderComponent {
    bool enabled{true};
    bool isTrigger{false};
    glm::vec3 center{0.0f, 0.0f, 0.0f};
    glm::vec3 size{1.0f, 1.0f, 1.0f};
};

struct SphereColliderComponent {
    bool enabled{true};
    bool isTrigger{false};
    glm::vec3 center{0.0f, 0.0f, 0.0f};
    float radius{0.5f};
};

struct CapsuleColliderComponent {
    bool enabled{true};
    bool isTrigger{false};
    glm::vec3 center{0.0f, 0.0f, 0.0f};
    float radius{0.5f};
    float height{2.0f};
};

struct PlaneColliderComponent {
    bool enabled{true};
    bool isTrigger{false};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float offset{0.0f};
    bool infinite{true};
    float halfExtent{1000.0f};
};

struct MeshColliderComponent {
    bool enabled{true};
    bool isTrigger{false};
    MeshColliderBuildQuality buildQuality{MeshColliderBuildQuality::BuildQuality};
    MeshColliderMode mode{MeshColliderMode::TriangleMesh};
};

struct CameraComponent {
    bool enabled{true};
    bool isMain{true};
    float fieldOfViewDegrees{60.0f};
    float nearClip{0.1f};
    float farClip{500.0f};
    glm::vec4 clearColor{0.02f, 0.02f, 0.02f, 1.0f};
};

enum class CinemachineCameraType {
    Follow,
    LookAt,
    FollowAndLookAt,
};

struct CinemachineCameraComponent {
    bool enabled{true};
    CinemachineCameraType type{CinemachineCameraType::FollowAndLookAt};
    std::string followTargetId;
    std::string lookAtTargetId;
    glm::vec3 followOffset{0.0f, 2.0f, -5.0f};
    float pitchOffset{0.0f};
    float yawOffset{0.0f};
    float positionDamping{5.0f};
    float rotationDamping{5.0f};
};

struct LightComponent {
    bool enabled{true};
    LightType type{LightType::Point};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    float range{10.0f};
    float spotAngleDegrees{30.0f};
};

struct AudioListenerComponent {
    bool enabled{true};
};

struct AudioSourceComponent {
    bool enabled{true};
    std::string clipPath;
    float volume{1.0f};
    float pitch{1.0f};
    bool loop{false};
    bool playOnAwake{true};
    float spatialBlend{1.0f};
    float minDistance{1.0f};
    float maxDistance{50.0f};
};

struct VehicleSoundComponent {
    bool enabled{true};
    std::string profilePath;
};

struct TrackGeneratorComponent {
    bool enabled{true};
    std::string trackSourcePath;
    std::string roadObjectId;
    std::string shoulderObjectId;
};

struct SceneObject {
    std::string id;
    std::string parentId;
    std::string name;
    std::string tag{"Untagged"};
    std::string type;
    int physicsLayer{0};
    std::vector<SceneInspectorComponentType> inspectorComponentOrder;
    Transform transform;
    bool enabled{true};
    bool hasMeshFilter{true};
    bool hasMeshRenderer{true};
    bool hasScriptComponent{true};
    bool hasRigidbody{false};
    bool hasVehicle{false};
    bool hasCharacterController{false};
    bool hasBoxCollider{false};
    bool hasSphereCollider{false};
    bool hasCapsuleCollider{false};
    bool hasPlaneCollider{false};
    bool hasMeshCollider{false};
    bool hasCamera{false};
    bool hasCinemachine{false};
    bool hasLight{false};
    bool hasAudioListener{false};
    bool hasAudioSource{false};
    bool hasVehicleSound{false};
    bool hasTrackGenerator{false};
    MeshFilterComponent meshFilter;
    MeshRendererComponent meshRenderer;
    ScriptComponent scriptComponent;
    RigidbodyComponent rigidbody;
    VehicleComponent vehicle;
    CharacterControllerComponent characterController;
    BoxColliderComponent boxCollider;
    SphereColliderComponent sphereCollider;
    CapsuleColliderComponent capsuleCollider;
    PlaneColliderComponent planeCollider;
    MeshColliderComponent meshCollider;
    CameraComponent camera;
    CinemachineCameraComponent cinemachine;
    LightComponent light;
    AudioListenerComponent audioListener;
    AudioSourceComponent audioSource;
    VehicleSoundComponent vehicleSound;
    TrackGeneratorComponent trackGenerator;
};

struct SceneMeshContributorStats {
    std::string meshAssetPath;
    std::string meshName;
    int meshIndex{0};
    std::uint32_t objectCount{0};
    std::uint64_t triangleCount{0};
    MeshColliderMode meshMode{MeshColliderMode::TriangleMesh};
};

struct SceneProfilerStats {
    std::uint32_t visibleMeshCount{0};
    std::uint32_t visibleLightCount{0};
    std::uint32_t bodyCount{0};
    std::uint32_t characterCount{0};
    std::uint32_t boxColliderCount{0};
    std::uint32_t sphereColliderCount{0};
    std::uint32_t capsuleColliderCount{0};
    std::uint32_t planeColliderCount{0};
    std::uint32_t meshColliderCount{0};
    std::uint32_t triangleMeshColliderCount{0};
    std::uint32_t convexHullColliderCount{0};
    std::vector<SceneMeshContributorStats> meshContributors;
};

} // namespace raceman

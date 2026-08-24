#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "../input/InputManager.h"
#include "../audio/VehicleSoundProfile.h"
#include "../physics/MeshColliderBuildQuality.h"
#include "../physics/MeshColliderMode.h"
#include "../rendering/Material.h"
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
    AssignLodMesh,
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
    ShaderGraph,
    ShaderCode
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
    MeshCollider,
    Camera,
    Cinemachine,
    Light,
    ReflectionProbe,
    Decal,
    WeatherShelter,
    AudioListener,
    AudioSource,
    VehicleSound,
    AudioReverbZone,
    AudioEnvironment,
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
    ReflectionProbe,
    Decal,
    WeatherShelter,
    AudioListener,
    AudioSource,
    VehicleSound,
    AudioReverbZone,
    AudioEnvironment,
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

enum class TrackSurfaceType {
    Asphalt,
    Dirt,
    Grass,
    Curb,
    Wall,
    Custom
};

inline constexpr int kTrackSurfaceTypeCount = 6;

struct ColliderSurfaceConfig {
    TrackSurfaceType type{TrackSurfaceType::Asphalt};
    float gripMultiplier{1.0f};
    float rollingDrag{0.08f};
    // Force feedback road texture: how strongly this surface buzzes through
    // the wheel, and the relative pitch of that buzz (lower = coarser/rattly).
    float ffbRoadAmplitude{0.22f};
    float ffbRoadFrequencyScale{1.0f};
};

using TrackSurfaceSettings = std::array<ColliderSurfaceConfig, kTrackSurfaceTypeCount>;

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

struct MeshLodLevel {
    // The base MeshFilter is always LOD0. These entries are LOD1 and below.
    std::string sourcePath;
    int meshIndex{0};
    float screenRelativeHeight{0.25f};
    unsigned int vao{0};
    unsigned int indexCount{0};
    glm::vec3 localBoundsMin{-0.5f, -0.5f, -0.5f};
    glm::vec3 localBoundsMax{0.5f, 0.5f, 0.5f};
    std::shared_ptr<::Model> modelRef;
};

struct MeshFilterComponent {
    bool enabled{true};
    std::string meshType;
    std::string sourcePath;
    std::string assetId;
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
    bool lodEnabled{false};
    float lodBias{1.0f};
    float lodHysteresis{0.10f};
    int forcedLod{-1};
    int activeLod{0};
    std::vector<MeshLodLevel> lodLevels;
};

struct MeshRendererComponent {
    bool enabled{true};
    std::string materialId{"pbr_default"};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    // Unity-style per-object material instance: an embedded live-inherit variant
    // of `materialId`, applied only to this object and never written to the
    // project browser. materialOverride.baseMaterialId is kept == materialId;
    // materialOverride.overriddenFieldIds records which fields the instance edits.
    bool hasMaterialOverride{false};
    Material materialOverride;
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

// How the chassis collision volume is authored. The vehicle is kinematic and driven
// by the arcade solver, so every shape here must stay convex - Jolt cannot sweep a
// triangle mesh as the moving shape.
enum class VehicleChassisCollisionMode : std::uint8_t {
    None,           // wheels-only grounding, no chassis response
    AutoBox,        // single box auto-fitted to the vehicle's visual bounds
    Box,            // single explicit box
    Shapes,         // authored list of primitives
    ConvexMesh,     // convex hull baked from a mesh asset
    ChildColliders  // gather collider components from chassisObjectIds
};

enum class VehicleChassisShapeType : std::uint8_t {
    Box,
    Sphere,
    Capsule,
    ConvexMesh
};

struct VehicleChassisShape {
    bool enabled{true};
    std::string name{"Shape"};
    VehicleChassisShapeType type{VehicleChassisShapeType::Box};
    glm::vec3 center{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEuler{0.0f, 0.0f, 0.0f};
    glm::vec3 size{1.8f, 0.6f, 4.0f};
    float radius{0.5f};
    float height{1.0f};
    std::string meshAssetPath;
    std::string meshName;
    int meshIndex{0};
};

struct VehicleChassisCollisionConfig {
    bool enabled{true};
    VehicleChassisCollisionMode mode{VehicleChassisCollisionMode::AutoBox};
    std::vector<VehicleChassisShape> shapes;

    // Single-box authoring (Box / AutoBox modes).
    glm::vec3 boxCenter{0.0f, -0.2f, 0.0f};
    glm::vec3 boxSize{1.8f, 0.6f, 4.0f};

    // Convex-mesh authoring (ConvexMesh mode). Empty path falls back to the
    // vehicle object's own mesh filter.
    std::string meshAssetPath;
    std::string meshName;
    int meshIndex{0};

    // Sweep/geometry settings. How the car *reacts* to a hit - restitution,
    // friction, spin - is physics and lives in the vehicle profile's collision
    // section, computed from chassis mass and yaw inertia.
    float skinWidth{0.03f};
    int maxSlideIterations{3};
    bool enableDepenetration{true};
    float maxDepenetrationPerStep{0.35f};
    bool debugDraw{false};
};

inline const char* VehicleChassisCollisionModeLabel(VehicleChassisCollisionMode mode) {
    switch (mode) {
    case VehicleChassisCollisionMode::None: return "None";
    case VehicleChassisCollisionMode::Box: return "Box";
    case VehicleChassisCollisionMode::Shapes: return "Shapes";
    case VehicleChassisCollisionMode::ConvexMesh: return "ConvexMesh";
    case VehicleChassisCollisionMode::ChildColliders: return "ChildColliders";
    case VehicleChassisCollisionMode::AutoBox:
    default: return "AutoBox";
    }
}

inline VehicleChassisCollisionMode VehicleChassisCollisionModeFromLabel(const std::string& value) {
    if (value == "None") return VehicleChassisCollisionMode::None;
    if (value == "Box") return VehicleChassisCollisionMode::Box;
    if (value == "Shapes") return VehicleChassisCollisionMode::Shapes;
    if (value == "ConvexMesh") return VehicleChassisCollisionMode::ConvexMesh;
    if (value == "ChildColliders") return VehicleChassisCollisionMode::ChildColliders;
    return VehicleChassisCollisionMode::AutoBox;
}

inline const char* VehicleChassisShapeTypeLabel(VehicleChassisShapeType type) {
    switch (type) {
    case VehicleChassisShapeType::Sphere: return "Sphere";
    case VehicleChassisShapeType::Capsule: return "Capsule";
    case VehicleChassisShapeType::ConvexMesh: return "ConvexMesh";
    case VehicleChassisShapeType::Box:
    default: return "Box";
    }
}

inline VehicleChassisShapeType VehicleChassisShapeTypeFromLabel(const std::string& value) {
    if (value == "Sphere") return VehicleChassisShapeType::Sphere;
    if (value == "Capsule") return VehicleChassisShapeType::Capsule;
    if (value == "ConvexMesh") return VehicleChassisShapeType::ConvexMesh;
    return VehicleChassisShapeType::Box;
}

struct VehicleComponent {
    bool enabled{true};
    bool canTilt{true};
    std::string configPath;
    std::string inputProfileId{"default_vehicle"};
    InputDevicePreference preferredInputDevice{InputDevicePreference::Any};
    std::string preferredInputDeviceId;
    std::vector<std::string> chassisObjectIds;
    std::vector<VehicleWheelBinding> wheelBindings;
    VehicleChassisCollisionConfig chassisCollision;
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
    int priority{10};
    float blendDuration{0.6f};
    CinemachineCameraType type{CinemachineCameraType::FollowAndLookAt};
    std::string followTargetId;
    std::string lookAtTargetId;
    glm::vec3 followOffset{0.0f, 2.0f, -5.0f};
    float pitchOffset{0.0f};
    float yawOffset{0.0f};
    bool lockTargetPitchRoll{false};
    float positionDamping{5.0f};
    float rotationDamping{5.0f};
};

struct LightComponent {
    bool enabled{true};
    bool castShadows{true};
    LightType type{LightType::Point};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    float range{10.0f};
    float spotAngleDegrees{30.0f};
};

enum class ReflectionProbeShape {
    Box,
    Sphere
};

enum class ReflectionProbeUpdateMode {
    Baked,
    Realtime
};

enum class DecalBlendModeSetting {
    Multiply,
    AlphaBlend
};

// Projected decal: skid marks, rubber, dirt, oil, painted markings. The object's
// Transform is the projection volume - scale is the box size in world units, and
// projection runs down the volume's local -Y, so the default orientation projects
// onto the ground.
struct DecalComponent {
    bool enabled{true};
    std::string texturePath;
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float opacity{1.0f};
    float angleFadeDegrees{70.0f};
    float uvTiling[2]{1.0f, 1.0f};
    float uvOffset[2]{0.0f, 0.0f};
    DecalBlendModeSetting blendMode{DecalBlendModeSetting::Multiply};
    int sortOrder{0};
};

// A region rain cannot reach. The object's Transform is the volume: scale is the
// box size in world units.
//
// Spatial rather than material on purpose. A garage floor is usually the same
// asphalt material as the track outside it, so keeping it dry is a question of
// where it is, not what it is made of.
struct WeatherShelterComponent {
    bool enabled{true};
    float amount{1.0f};    // 1 = completely dry inside
    float falloff{1.0f};   // metres of gradient inward from the wall, so doorways blend
};

struct ReflectionProbeComponent {
    bool enabled{true};
    ReflectionProbeShape shape{ReflectionProbeShape::Box};
    glm::vec3 boxSize{10.0f};
    float sphereRadius{5.0f};
    float blendDistance{1.0f};
    float intensity{1.0f};
    int resolution{256};
    ReflectionProbeUpdateMode updateMode{ReflectionProbeUpdateMode::Baked};
    // Realtime probes only refresh while the camera is within this distance of
    // the influence volume, keeping garages/tunnels cheap when off-screen.
    float realtimeUpdateDistance{50.0f};
    int realtimeFacesPerFrame{1};
    std::string bakedCubemapPath;
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
    // The engine voice. Accepts either a .enginesound.json (procedural synth)
    // or a legacy .vehiclesound.json (pitched sample layers) - they are two
    // implementations of the same slot, so one field is correct.
    std::string profilePath;

    // How THIS vehicle emits, which is a property of the object rather than of
    // what the engine sounds like. Previously duplicated inside both profile
    // types, which meant the same setting had two homes that could disagree.
    float spatialBlend{1.0f};
    float minDistance{6.0f};
    float maxDistance{140.0f};

    // Tyre audio: rolling, slip squeal and per-surface texture. A separate
    // asset because tyres are not the engine, and a car can change tyres
    // without changing engines.
    std::string tyreProfilePath;

    // Gear-shift and start/stop one-shots. Not engine sound at all, so they
    // belong here rather than inside an engine profile.
    std::vector<VehicleSoundTriggerEntry> triggerSounds;
};

// -------------------------------------------------------------------------
// Audio reverb zone.
//
// This is the standard racing-game approach: acoustics belong to the PLACE,
// not to the car. A scene has a default outdoor character, and volumes placed
// over tunnels, bridges, pit garages and tree-lined sections blend their own
// acoustics in as the listener enters them. One car then sounds correct
// everywhere instead of carrying a private copy of the track's reverb.
// -------------------------------------------------------------------------
struct AudioReverbZoneComponent {
    bool enabled{true};
    // Zone shape is the object's transform: a box of this half-extent, scaled
    // by the object's own scale.
    glm::vec3 halfExtents{10.0f, 5.0f, 10.0f};
    // Distance over which the zone blends in at its boundary, so walking into
    // a tunnel is a transition rather than a switch.
    float blendMetres{6.0f};
    // Higher priority wins where zones overlap (a garage inside a pit lane).
    int priority{0};

    // Acoustics, matching EngineReverbSettings.
    float earlyGain{0.60f};
    float earlySpreadMs{22.0f};
    float tailGain{0.55f};
    float tailDecaySeconds{1.60f};
    float tailDamping{0.25f};
    // Enclosed spaces also darken and hold the sound in.
    float lowPassScale{0.80f};
    float volumeScale{1.15f};
};

// Scene-wide default acoustics, used wherever no zone applies.
struct AudioEnvironmentComponent {
    bool enabled{true};
    float earlyGain{0.46f};
    float earlySpreadMs{48.0f};
    float tailGain{0.16f};
    float tailDecaySeconds{0.85f};
    float tailDamping{0.62f};
    float lowPassScale{1.0f};
    float volumeScale{1.0f};
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
    // Prefab link. Empty sourcePrefabPath means this object is not part of a
    // prefab instance. prefabLocalId is the id this object has *inside* the
    // prefab file, stable across instantiations; live scene ids are remapped.
    std::string sourcePrefabPath;
    std::string prefabInstanceRootId;
    std::string prefabLocalId;
    std::set<SceneComponentType> prefabOverriddenComponents;
    bool prefabStructureOverridden{false};
    std::vector<SceneInspectorComponentType> inspectorComponentOrder;
    Transform transform;
    bool enabled{true};
    bool hasMeshFilter{false};
    bool hasMeshRenderer{false};
    bool hasScriptComponent{false};
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
    bool hasReflectionProbe{false};
    bool hasDecal{false};
    bool hasWeatherShelter{false};
    bool hasAudioListener{false};
    bool hasAudioSource{false};
    bool hasVehicleSound{false};
    bool hasTrackGenerator{false};
    bool hasAudioReverbZone{false};
    bool hasAudioEnvironment{false};
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
    ColliderSurfaceConfig colliderSurface;
    CameraComponent camera;
    CinemachineCameraComponent cinemachine;
    LightComponent light;
    ReflectionProbeComponent reflectionProbe;
    DecalComponent decal;
    WeatherShelterComponent weatherShelter;
    AudioListenerComponent audioListener;
    AudioSourceComponent audioSource;
    VehicleSoundComponent vehicleSound;
    TrackGeneratorComponent trackGenerator;
    AudioReverbZoneComponent audioReverbZone;
    AudioEnvironmentComponent audioEnvironment;
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

struct SceneEditorFrameTimings {
    float shortcutsMs{0.0f};
    float playModePopupMs{0.0f};
    float runtimeUpdatesMs{0.0f};
    float dockspaceMs{0.0f};
    float scenePanelMs{0.0f};
    float inspectorMs{0.0f};
    float browserMs{0.0f};
    float viewportPanelMs{0.0f};
    float auxiliaryWindowsMs{0.0f};
};

} // namespace raceman

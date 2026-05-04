#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../input/InputManager.h"
#include "../physics/MeshColliderBuildQuality.h"
#include "../physics/MeshColliderMode.h"
#include "../physics/PhysicsLayers.h"
#include "../physics/VehicleConfig.h"
#include "../rendering/Renderer.h"
#include "../rendering/Material.h"
#include "../rendering/PrimitiveMeshes.h"
#include "../scripting/ObjectScript.h"
#include "../audio/VehicleSoundProfile.h"

class Model;

namespace irrklang { class ISound; }

namespace raceman {

namespace physics {
class VehiclePhysics;
}

class Renderer;
class Console;
class PhysicsWorld;
class AudioManager;
struct PhysicsBuildProgress;

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
    Script
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
    VehicleSound
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
    VehicleSound
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

enum class LightType {
    Directional,
    Point,
    Spot
};

struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEuler{0.0f, 0.0f, 0.0f}; // degrees
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

struct MeshFilterComponent {
    bool enabled{true};
    std::string meshType; // e.g., "Plane" or "Mesh"
    std::string sourcePath;
    int meshIndex{0};
    std::string meshName; // sub-object name from the asset (e.g. Assimp mName)
    glm::vec3 pivotOffset{0.0f, 0.0f, 0.0f}; // centre-pivot import: vertices are at pivotOffset in world-space; rendering pre-translates by -pivotOffset
    std::string importedMaterialName;
    std::string diffuseTexturePath;
    unsigned int diffuseTextureId{0};
    unsigned int vao{0};
    unsigned int indexCount{0};
    glm::vec3 localBoundsMin{-0.5f, -0.5f, -0.5f};
    glm::vec3 localBoundsMax{0.5f, 0.5f, 0.5f};
    std::vector<glm::vec3>    pickVertices; // CPU positions for picking narrow phase
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
    Follow,           // offset from target, camera looks in its own forward direction
    LookAt,           // camera stays fixed, rotates to look at target
    FollowAndLookAt,  // follows at offset and always looks at target
};

struct CinemachineCameraComponent {
    bool enabled{true};
    CinemachineCameraType type{CinemachineCameraType::FollowAndLookAt};
    std::string followTargetId;
    std::string lookAtTargetId;  // if empty, uses followTargetId
    glm::vec3 followOffset{0.0f, 2.0f, -5.0f};  // offset in target-local space
    float pitchOffset{0.0f};  // degrees — tilts camera up/down after look-at
    float yawOffset{0.0f};    // degrees — rotates camera left/right after look-at
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
    std::string clipPath;       // e.g. "assets/audio/sound.ogg"
    float volume{1.0f};
    float pitch{1.0f};          // playback speed multiplier
    bool loop{false};
    bool playOnAwake{true};
    float spatialBlend{1.0f};   // 0 = 2D, 1 = fully 3D
    float minDistance{1.0f};
    float maxDistance{50.0f};
};

struct VehicleSoundComponent {
    bool enabled{true};
    std::string profilePath;    // e.g. "assets/sounds/f1_engine.vehiclesound.json"
};

struct SceneObject {
    std::string id;    // simple unique id
    std::string parentId;
    std::string name;  // editable name
    std::string tag{"Untagged"};
    std::string type;  // legacy/display object type, e.g., "Plane", "Mesh"
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

class SceneEditor {
public:
    SceneEditor();
    ~SceneEditor();

    // Render both Scene (hierarchy) and Inspector panels; handle shortcuts (Ctrl+S)
    void RenderUI(float deltaTime);

    // Quick action: add a plane via external UI (Menu)
    void AddMeshPlane();
    // Programmatic access (future use)
    const std::vector<SceneObject>& GetObjects() const { return objects_; }

    // Submit renderables for drawing via Renderer (PBR pipeline)
    void SubmitDraws(Renderer& renderer, bool editorInteraction = true);
    void SetConsole(Console* console);
    void SetInputManager(InputManager* inputManager);
    void SetAudioManager(AudioManager* audio) { audioManager_ = audio; }
    bool IsRunMode() const { return scriptsRunning_; }
    bool IsGameViewActive() const { return true; }
    bool TryGetGameCamera(glm::mat4& outView, glm::mat4& outProj, float aspect, glm::vec4* outClearColor = nullptr) const;
    float GetViewportAspect() const;
    RendererViewport GetRenderViewport(int framebufferWidth, int framebufferHeight) const;
    RendererViewport GetSceneRenderViewport(int framebufferWidth, int framebufferHeight) const;
    RendererViewport GetGameRenderViewport(int framebufferWidth, int framebufferHeight) const;
    bool IsViewportHovered() const { return viewportHovered_; }
    bool ContainsViewportPoint(float x, float y) const;
    bool ContainsSceneViewportPoint(float x, float y) const;
    bool ContainsGameViewportPoint(float x, float y) const;
    bool ShouldRouteInputToGame() const { return activeViewport_ == SceneEditorActiveViewport::Game; }
    bool IsSceneViewportActiveForEditorControls() const { return activeViewport_ == SceneEditorActiveViewport::Scene; }
    void SetSceneViewportTexture(unsigned int textureId) { sceneViewportTextureId_ = textureId; }
    void SetGameViewportTexture(unsigned int textureId) { gameViewportTextureId_ = textureId; }
    void SetEditorCameraMatrices(const glm::mat4& view, const glm::mat4& proj) {
        editorCameraView_ = view;
        editorCameraProj_ = proj;
        hasEditorCameraMatrices_ = true;
    }

    // Notify app when editor content changes
    void SetOnDirty(std::function<void()> cb) {
        onDirty_ = [this, inner = std::move(cb)]() {
            sceneDirty_ = true;
            if (inner) inner();
        };
    }
    bool IsSceneDirty() const { return sceneDirty_; }
    void MarkSceneClean() { sceneDirty_ = false; }
    void SetOnFocusObject(std::function<void(const glm::vec3&, float)> cb) { onFocusObject_ = std::move(cb); }
    // Fired when the user clicks an axis on the corner orientation cube; lets the camera owner
    // decompose the new view matrix back into yaw/pitch/position.
    void SetOnEditorCameraViewChanged(std::function<void(const glm::mat4&)> cb) { onEditorCameraViewChanged_ = std::move(cb); }

    // Profiler stats toggle wired from Game View "Stats" button
    void SetProfilerCallbacks(std::function<bool()> getter, std::function<void(bool)> setter) {
        getProfilerVisible_ = std::move(getter);
        setProfilerVisible_ = std::move(setter);
    }

    // Game viewport position/size — used by Application to anchor the profiler overlay
    glm::vec2 GetGameViewportPos()  const { return gameViewportPos_; }
    glm::vec2 GetGameViewportSize() const { return gameViewportSize_; }
    glm::vec2 GetSceneViewportSize() const { return sceneViewportSize_; }

    // Control persistence location and access from Application
    void SetSavePath(const std::string& path);
    void Save(const std::string& path);
    void Load(const std::string& path);
    void NewScene();
    void NewScene(const std::string& sceneName);
    void SaveActiveAsset();
    void SaveCurrentScene();
    void SaveCurrentSceneAs();
    bool OpenSceneAsset(const std::string& path);
    void SaveProject();
    void RenderProjectInputSettings();
    void RenderProjectPhysicsSettings();
    void RenderProjectTagsAndLayersSettings();
    SceneProfilerStats CollectProfilerStats() const;
    std::vector<std::string> GetSceneAssetPaths() const;
    const std::string& GetCurrentScenePath() const { return savePath_; }
    const std::string& GetProjectName() const { return projectName_; }
    const PhysicsWorld* GetPhysicsWorld() const { return physicsWorld_.get(); }
    void SetShowCullingDebug(bool show) { showCullingDebug_ = show; }
    bool IsPhysicsCullingEnabled() const { return enablePhysicsCulling_; }
    void SetPhysicsCullingEnabled(bool v) { enablePhysicsCulling_ = v; }
    bool IsFrustumCullingEnabled() const { return enableFrustumCulling_; }
    void SetFrustumCullingEnabled(bool v) { enableFrustumCulling_ = v; }
    bool ShowFrustumCullDebug() const { return showFrustumCullDebug_; }
    void SetShowFrustumCullDebug(bool v) { showFrustumCullDebug_ = v; }

    void ImportObj(const std::string& path);
    void ImportObjWithOptions(const std::string& path, int pivotMode);
    void ScanObjDir(const std::string& dir);

private:
    // UI panels
    void RenderScenePanel();
    void RenderInspectorPanel();
    void RenderMultiSelectionInspector();
    void RenderProjectPanel();
    void RenderViewportPanel();
    void RenderDockspaceHost();
    void RenderMaterialInspector();
    void RenderVehicleConfigEditorWindow();
    void RenderVehicleSoundEditorWindow();
    void RenderMaterialProperties(const std::string& materialId, bool showBackButton);
    void RenderProjectAssetPickerPopup();
    unsigned int GetComponentIconTexture(const std::string& filename);
    void HandleEditorShortcuts();
    void UpdateScripts(float deltaTime);
    void UpdateVehiclePhysics(float deltaTime);
    void UpdatePhysics(float deltaTime);
    void UpdateVehicles(float deltaTime);
    void UpdateCinemachine(float deltaTime);
    void PreviewCinemachineInEditor();
    void ResetPhysicsVelocities();
    void SetScriptsRunning(bool running);
    void SetScriptsPaused(bool paused);
    void RebuildScriptRuntime();
    void RebuildVehicleRuntime();
    void RebuildAudioRuntime();
    void ClearScriptRuntime();
    void ClearAudioRuntime();
    void UpdateAudio(float deltaTime);
    void RestoreFromPlayModeSnapshot();
    void TickPlayModeLoading();
    void RenderPlayModeLoadingPopup();
    void HandleConsoleCommand(const std::string& command);
    void UpdateGizmo(Renderer& renderer);
    void UpdateImGuizmo();
    void SubmitGizmo(Renderer& renderer);
    void SubmitCullingDebug(Renderer& renderer);
    void TrySelectObjectAtMouse(Renderer& renderer);
    void PushUndoState();
    void Undo();
    void Redo();
    void PushVehicleConfigUndoState();
    void PushVehicleSoundUndoState();
    void UndoVehicleConfig();
    void RedoVehicleConfig();
    void UndoVehicleSound();
    void RedoVehicleSound();
    void RequestFocusSelectedObject();

    // Actions
    void AddPlane();
    void AddBuiltInPrimitiveObject(const std::string& meshType);
    void AddEmptyObject();
    void AddCameraObject();
    void AddLightObject(LightType type);
    void DeleteSelectedObject();
    bool ReplaceSelectedMeshWithPlane();
    bool ReplaceSelectedMeshWithBuiltIn(const std::string& meshType);
    bool ReplaceSelectedMeshFromObj(const std::string& path);
    bool AssignMaterialToSelected(const std::string& materialId);
    bool AssignVehicleConfigToSelected(const std::string& configPath);
    bool AttachScriptToSelected(const std::string& scriptName, const std::string& scriptPath);
    bool CreateScriptAsset(const std::string& requestedName, bool attachToSelected = true);
    // (name, projectSourcePath) pairs for every .h+.cpp script under assets/.
    std::vector<std::pair<std::string, std::string>> ScanProjectScripts() const;
    bool SyncAttachmentScriptFields(ObjectScriptAttachment& attachment);
    bool CreateMaterialAsset(const std::string& requestedName, std::string* outMaterialId = nullptr);
    bool CreateVehicleConfigAsset(const std::string& requestedName, std::string* outConfigPath = nullptr);
    bool CreateVehicleSoundAsset(const std::string& requestedName, std::string* outProfilePath = nullptr);
    bool CreateSceneAsset(const std::string& requestedName, std::string* outScenePath = nullptr);
    bool CreateProjectFolder(const std::string& requestedName);
    bool SaveObjectAsPrefab(int objectIndex, const std::string& path);
    bool InstantiatePrefab(const std::string& path);
    void SyncScriptProjectFiles(bool logResult = true);
    void OpenMaterialEditor(const std::string& materialId);
    void OpenVehicleConfigEditor(const std::string& configPath);
    void OpenVehicleSoundEditor(const std::string& profilePath);
    void BeginObjectRename(int index);
    void BeginProjectFileRename(const std::string& path);
    void CommitProjectFileRename();
    void DeleteProjectFile(const std::string& path);
    void DeleteProjectFolder(const std::string& path);
    bool MoveProjectFile(const std::string& path, const std::string& targetDirectory);
    bool CopyProjectFileTo(const std::string& sourcePath, const std::string& targetDirectory);
    void SelectProjectFile(const std::string& path);
    void RefreshProjectFiles();
    void LoadProject();
    void UpdateProjectSceneReference(const std::string& oldPath, const std::string& newPath);
    std::string MakeUniqueSceneAssetPath(const std::string& baseName) const;
    void CreateDefaultSceneObjects();

    void Select(int index);
    void ToggleSelect(int index);
    bool IsSelected(int index) const;
    void NormalizeSelection();
    void QueueHierarchyRevealForSelection();
    int FindObjectIndexById(const std::string& id) const;
    bool IsObjectEffectivelyEnabled(int index) const;
    bool IsDescendantOf(const std::string& objectId, const std::string& potentialAncestorId) const;
    void SetParent(int childIndex, int parentIndex);
    bool MoveObjectInHierarchy(int childIndex, int newParentIndex, int insertAfterIndex);
    glm::mat4 GetObjectWorldMatrix(int index) const;
    glm::vec3 GetObjectWorldPosition(int index) const;
    int ClampPhysicsLayerIndex(int layer) const;
    const char* GetPhysicsLayerName(int layer) const;
    void ResetPhysicsLayerSettings();
    void EnsureProjectTags();
    bool AddProjectTag(const std::string& tag);
    bool RemoveProjectTag(int index);
    void CopySelectedObjectsToClipboard();
    void PasteObjectsFromClipboard();
    bool CopyInspectorComponentToClipboard(int objectIndex, SceneInspectorComponentType type);
    bool PasteInspectorComponentFromClipboard(const std::vector<int>& targetIndices, SceneInspectorComponentType targetType);

    // Utils
    std::string MakeId(const std::string& base);

private:
    std::vector<SceneObject> objects_;
    int selectedIndex_{-1};
    std::vector<int> selectedIndices_;

    // persistence
    std::string projectPath_{"project.raceman.json"};
    std::string projectName_{"Project Raceman"};
    std::string assetsRootSetting_{"assets"};
    std::string defaultScenePath_{"assets/scenes/EditorScene.scene.json"};
    std::string lastScenePath_{"assets/scenes/EditorScene.scene.json"};
    std::string savePath_{"assets/scenes/EditorScene.scene.json"};
    PhysicsLayerNames physicsLayerNames_{};
    PhysicsLayerCollisionMatrix physicsLayerCollisionMatrix_{};
    std::vector<std::string> projectTags_{"Untagged"};
    std::vector<InputProfile> inputProfiles_{};
    std::vector<WheelSettingsProfile> wheelSettingsProfiles_{};
    int selectedInputProfileIndex_{0};
    int selectedInputDevicePage_{0};
    int selectedWheelSettingsProfileIndex_{0};

    // shared primitives
    std::unordered_map<std::string, PrimitiveMesh> builtInPrimitiveMeshes_;
    Console* console_{nullptr};
    InputManager* inputManager_{nullptr};
    AudioManager* audioManager_{nullptr};

    // Materials
    MaterialManager materialManager_;
    std::unordered_map<std::string, unsigned int> componentIconTextures_;

    // Import dialog state
    bool showImportObjPopup_{false};
    char importPath_[512]{};
    std::string objScanDir_{"assets/mesh"};
    std::vector<std::string> objFiles_;
    int objSelectIndex_{-1};
    bool showImportMeshOptionsPopup_{false};
    int pendingImportMeshPivotMode_{0};
    std::string pendingImportMeshPath_;

    std::vector<std::string> projectDirectories_;
    std::vector<std::string> projectFiles_;
    std::string selectedProjectDirectory_{"assets"};
    std::string selectedProjectFile_;

    bool inspectMaterial_{false};
    std::string inspectedMaterialId_;
    bool showVehicleConfigEditor_{false};
    std::string inspectedVehicleConfigPath_;
    physics::VehicleConfig inspectedVehicleConfig_{};
    bool inspectedVehicleConfigLoaded_{false};
    std::string inspectedVehicleConfigError_;
    bool vehicleConfigEditorHovered_{false};
    bool vehicleConfigEditorFocused_{false};
    bool vehicleConfigEditActive_{false};
    bool showVehicleSoundEditor_{false};
    std::string inspectedVehicleSoundPath_;
    VehicleSoundProfile inspectedVehicleSound_{};
    bool inspectedVehicleSoundLoaded_{false};
    std::string inspectedVehicleSoundError_;
    bool vehicleSoundEditorHovered_{false};
    bool vehicleSoundEditorFocused_{false};
    bool vehicleSoundEditActive_{false};
    ProjectAssetPickerMode assetPickerMode_{ProjectAssetPickerMode::None};
    bool scriptsRunning_{false};
    bool scriptsPaused_{false};
    bool playModeScriptAssemblyReady_{false};
    bool showCreateScriptPopup_{false};
    char createScriptNameBuffer_[128]{};
    char createMaterialNameBuffer_[128]{};
    char createVehicleConfigNameBuffer_[128]{};
    char createTagNameBuffer_[128]{};
    bool showCreateProjectAssetPopup_{false};
    ProjectCreateAssetType createProjectAssetType_{ProjectCreateAssetType::None};
    char createProjectAssetNameBuffer_[128]{};

    bool showSavePrefabPopup_{false};
    int pendingPrefabObjectIndex_{-1};
    char savePrefabNameBuffer_[128]{};

    struct RuntimeScriptInstance {
        std::string objectId;
        std::size_t attachmentIndex{0};
        std::unique_ptr<IObjectScript> instance;
        bool started{false};
    };
    std::vector<RuntimeScriptInstance> runtimeScripts_;

    struct RuntimeVehicleInstance {
        std::string objectId;
        int objectIndex{-1};
        std::string chassisBodyObjectId;
        std::vector<int> wheelObjectIndices;
        std::vector<VehicleWheelBinding> wheelBindings;
        std::vector<Transform> wheelAuthoredLocalTransforms;
        std::vector<glm::vec3> wheelAuthoredRotationEuler;
        std::unique_ptr<physics::VehiclePhysics> instance;
    };
    std::vector<RuntimeVehicleInstance> runtimeVehicles_;

    struct RuntimeCinemachineState {
        glm::vec3 smoothedPosition{0.0f};
        glm::quat smoothedRotation{1.0f, 0.0f, 0.0f, 0.0f};
        bool initialized{false};
    };
    std::unordered_map<std::string, RuntimeCinemachineState> runtimeCinemachineStates_;

    // Audio source runtime (one per AudioSource component)
    struct RuntimeAudioSourceInstance {
        std::string objectId;
        irrklang::ISound* sound{nullptr};
    };
    std::vector<RuntimeAudioSourceInstance> runtimeAudioSources_;

    // Vehicle sound runtime (one per VehicleSound component)
    struct RuntimeVehicleSoundLayerState {
        irrklang::ISound* sound{nullptr};
        float smoothVolume{0.0f};
        float smoothPitch{1.0f};
    };
    struct RuntimeVehicleSoundInstance {
        std::string objectId;           // vehicle object id
        std::string vehicleObjectId;    // same object that has VehicleComponent
        VehicleSoundProfile profile;
        std::vector<RuntimeVehicleSoundLayerState> layers;
        // Trigger detection state
        int  lastGear{0};
        bool lastThrottleHigh{false};   // throttle was >0.8 last frame
        float lastLateralSpeed{0.0f};
    };
    std::vector<RuntimeVehicleSoundInstance> runtimeVehicleSounds_;

    int renamingObjectIndex_{-1};
    bool focusObjectRename_{false};
    char objectRenameBuffer_[128]{};
    int pendingHierarchySelectIndex_{-1};
    bool pendingHierarchySelectToggle_{false};
    bool pendingHierarchySelectRange_{false};
    int pendingHierarchyRangeAnchor_{-1};
    bool pendingHierarchyFocusObject_{false};
    bool pendingHierarchySelectionDragged_{false};
    bool scenePanelHovered_{false};
    bool scenePanelFocused_{false};
    std::string hierarchyKeyboardTargetObjectId_;
    std::string pendingHierarchyToggleObjectId_;
    std::string pendingHierarchyRevealObjectId_;
    std::string lastHierarchyRevealObjectId_;
    std::unordered_map<std::string, bool> hierarchyOpenStates_;

    std::string renamingProjectFile_;
    bool focusProjectRename_{false};
    char projectRenameBuffer_[260]{};

    struct FileClipboardState {
        std::string path;
        bool isCut{false};
    } fileClipboard_;

    int hoveredGizmoAxis_{-1};
    int activeGizmoAxis_{-1};
    GizmoMode gizmoMode_{GizmoMode::Move};
    glm::vec2 gizmoDragStartMouse_{0.0f};
    glm::vec3 gizmoDragStartPosition_{0.0f};
    glm::vec3 gizmoDragStartRotation_{0.0f};
    glm::vec3 gizmoDragStartScale_{1.0f};
    std::vector<int> gizmoDragSelectionIndices_;
    std::vector<Transform> gizmoDragStartLocalTransforms_;
    std::vector<glm::mat4> gizmoDragStartWorldMatrices_;
    bool gizmoDirtyDuringDrag_{false};
    bool showCullingDebug_{false};
    bool enablePhysicsCulling_{true};
    bool enableFrustumCulling_{true};
    bool showFrustumCullDebug_{false};
    bool inspectorEditActive_{false};
    bool linkedScaleValues_{true};
    bool linkedMultiScaleValues_{true};
    bool inspectorPanelHovered_{false};
    bool inspectorPanelFocused_{false};
    std::string inspectorKeyboardTargetComponentKey_;
    std::string inspectorKeyboardTargetObjectId_;
    SceneInspectorComponentType inspectorKeyboardTargetComponentType_{SceneInspectorComponentType::Transform};
    std::string pendingInspectorToggleComponentKey_;
    std::unordered_map<std::string, bool> inspectorComponentOpenStates_;
    struct ComponentClipboardState {
        bool hasValue{false};
        SceneInspectorComponentType type{SceneInspectorComponentType::Transform};
        SceneObject sourceObject{};
    } componentClipboard_;
    struct ObjectClipboardState {
        bool hasValue{false};
        std::vector<SceneObject> objects;
        std::vector<std::string> rootObjectIds;
    } objectClipboard_;
    bool dockLayoutInitialized_{false};
    glm::vec2 viewportPanelPos_{0.0f, 0.0f};
    glm::vec2 viewportPanelSize_{0.0f, 0.0f};
    glm::vec2 sceneViewportPos_{0.0f, 0.0f};
    glm::vec2 sceneViewportSize_{0.0f, 0.0f};
    glm::vec2 gameViewportPos_{0.0f, 0.0f};
    glm::vec2 gameViewportSize_{0.0f, 0.0f};
    unsigned int sceneViewportTextureId_{0};
    unsigned int gameViewportTextureId_{0};
    bool viewportHovered_{false};
    bool viewportFocused_{false};
    bool sceneViewportHovered_{false};
    bool sceneViewportFocused_{false};
    bool gameViewportHovered_{false};
    bool gameViewportFocused_{false};
    glm::mat4 editorCameraView_{1.0f};
    glm::mat4 editorCameraProj_{1.0f};
    bool hasEditorCameraMatrices_{false};
    SceneEditorActiveViewport activeViewport_{SceneEditorActiveViewport::Scene};

    struct HistoryState {
        std::vector<SceneObject> objects;
        int selectedIndex{-1};
        std::vector<int> selectedIndices;
    };
    std::vector<HistoryState> undoStack_;
    std::vector<HistoryState> redoStack_;
    struct VehicleConfigHistoryState {
        physics::VehicleConfig config;
    };
    std::vector<VehicleConfigHistoryState> vehicleConfigUndoStack_;
    std::vector<VehicleConfigHistoryState> vehicleConfigRedoStack_;
    struct VehicleSoundHistoryState {
        VehicleSoundProfile profile;
    };
    std::vector<VehicleSoundHistoryState> vehicleSoundUndoStack_;
    std::vector<VehicleSoundHistoryState> vehicleSoundRedoStack_;
    HistoryState playModeSnapshot_{};
    bool hasPlayModeSnapshot_{false};
    std::unique_ptr<PhysicsWorld> physicsWorld_;

    struct PlayModeLoadState {
        enum class Phase { Idle, BuildingScripts, BuildingPhysics };
        struct ScriptBuildStatus {
            std::atomic<bool> isDone{false};
            std::atomic<bool> success{false};
            std::string error;
            mutable std::mutex mutex;
        };
        Phase phase{Phase::Idle};
        std::shared_ptr<ScriptBuildStatus> scriptBuild;
        std::unique_ptr<std::thread> scriptBuildThread;
        std::shared_ptr<PhysicsBuildProgress> progress;
        std::unique_ptr<std::thread> buildThread;
        std::unique_ptr<PhysicsWorld> pendingWorld;
        std::chrono::time_point<std::chrono::high_resolution_clock> buildStart{};
    };
    PlayModeLoadState playModeLoad_;
    SceneProfilerStats profilerStats_{};

    bool sceneDirty_{false};
    std::function<void()> onDirty_{};
    std::function<void(const glm::vec3&, float)> onFocusObject_{};
    std::function<void(const glm::mat4&)> onEditorCameraViewChanged_{};
    std::function<bool()> getProfilerVisible_{};
    std::function<void(bool)> setProfilerVisible_{};
};

} // namespace raceman

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
#include "SceneEditorTypes.h"
#include "../input/InputManager.h"
#include "../physics/PhysicsLayers.h"
#include "../physics/VehicleConfig.h"
#include "../rendering/Renderer.h"
#include "../rendering/Material.h"
#include "../rendering/PrimitiveMeshes.h"
#include "../scripting/ObjectScript.h"
#include "../audio/VehicleSoundProfile.h"
#include "../rendering/SkyboxController.h"
#include "TrackGenerator.h"

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

class SceneEditor {
public:
    SceneEditor();
    ~SceneEditor();

    // Render both Scene (hierarchy) and Inspector panels; handle shortcuts (Ctrl+S)
    void RenderUI(float deltaTime);
    void StartRuntime();
    void UpdateRuntime(float deltaTime);
    void StopRuntime();
    void SetProjectRoot(std::string path);
    std::string GetProjectRoot() const;

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
    bool IsGameViewActive() const { return activeViewport_ == SceneEditorActiveViewport::Game; }
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
    bool ShouldRenderGameViewportInEditMode() const {
        return gameViewportRenderDirty_ && gameViewportSize_.x > 1.0f && gameViewportSize_.y > 1.0f;
    }
    void MarkGameViewportRendered() {
        gameViewportRenderDirty_ = false;
        lastRenderedGameViewportSize_ = gameViewportSize_;
    }
    void SetEditorCameraNavigating(bool navigating) { editorCameraNavigating_ = navigating; }
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
            gameViewportRenderDirty_ = true;
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
    const SceneEditorFrameTimings& GetFrameTimings() const { return frameTimings_; }
    std::vector<std::string> GetSceneAssetPaths() const;
    const std::string& GetCurrentScenePath() const { return savePath_; }
    const std::string& GetProjectName() const { return projectName_; }
    const SkyboxFaces& GetSkyboxFaces() const { return skyboxFaces_; }
    void SetSkyboxFaces(const SkyboxFaces& faces) { skyboxFaces_ = faces; }
    const PhysicsWorld* GetPhysicsWorld() const { return physicsWorld_.get(); }
    // Returns the active physics build progress if cooking is in progress, nullptr otherwise.
    const PhysicsBuildProgress* GetPhysicsBuildProgress() const;
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
    void SyncScripts() { SyncScriptProjectFiles(false); }

private:
    // UI panels
    void RenderScenePanel();
    void RenderInspectorPanel();
    void RenderMultiSelectionInspector();
    void RenderProjectPanel();
    void RenderViewportPanel();
    void RenderDockspaceHost();
    void RenderMaterialInspector();
    void RenderShaderGraphEditorWindow();
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
    void PushMaterialUndoState(const Material& snapshot);
    void UndoMaterial();
    void RedoMaterial();
    void PushShaderGraphUndoState();
    void UndoShaderGraph();
    void RedoShaderGraph();
    void RequestFocusSelectedObject();

    // Actions
    void AddPlane();
    void AddBuiltInPrimitiveObject(const std::string& meshType);
    void AddEmptyObject();
    void AddTrackGeneratorObject();
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
    bool CreateMaterialAsset(const std::string& requestedName, std::string* outMaterialId = nullptr, const std::string& shaderId = "pbr");
    bool CreateShaderGraphAsset(const std::string& requestedName, std::string* outGraphPath = nullptr);
    bool SaveShaderGraphAsset();
    bool CreateVehicleConfigAsset(const std::string& requestedName, std::string* outConfigPath = nullptr);
    bool CreateVehicleSoundAsset(const std::string& requestedName, std::string* outProfilePath = nullptr);
    bool CreateSceneAsset(const std::string& requestedName, std::string* outScenePath = nullptr);
    bool CreateProjectFolder(const std::string& requestedName);
    bool SaveObjectAsPrefab(int objectIndex, const std::string& path);
    bool InstantiatePrefab(const std::string& path);
    void SyncScriptProjectFiles(bool logResult = true);
    void OpenMaterialEditor(const std::string& materialId);
    void OpenShaderGraphEditor(const std::string& graphPath);
    void OpenVehicleConfigEditor(const std::string& configPath);
    void OpenVehicleSoundEditor(const std::string& profilePath);
    void OpenTrackGenerator(const std::string& trackPath);
    void RenderTrackGeneratorWindow();
    void HandleTrackDrawingInput();
    bool BakeTrackToScene(bool realtime = false);
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
    glm::mat4 GetObjectDisplayWorldMatrix(int index) const;
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
    SkyboxFaces skyboxFaces_{};
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
    std::unordered_map<std::string, unsigned int> materialTextureCache_;

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
    bool showShaderGraphEditor_{false};
    std::string inspectedShaderGraphPath_;
    char shaderGraphNameBuffer_[128]{};
    std::vector<ShaderGraphNodeState> shaderGraphNodes_;
    std::vector<ShaderGraphLinkState> shaderGraphLinks_;
    int shaderGraphNextNodeId_{101};
    int shaderGraphNextLinkId_{1};
    int shaderGraphSelectedNodeId_{0};
    std::string shaderGraphStatus_;
    std::vector<ShaderGraphHistoryState> shaderGraphUndoStack_;
    std::vector<ShaderGraphHistoryState> shaderGraphRedoStack_;
    bool shaderGraphEditorFocused_{false};
    bool shaderGraphEditorHovered_{false};
    bool shaderGraphDragUndoArmed_{false};
    glm::vec2 shaderGraphContextScreenPos_{0.0f, 0.0f};
    glm::vec2 shaderGraphCanvasSize_{720.0f, 420.0f};
    int shaderGraphBaseColorNode_{1};
    int shaderGraphEmissiveNode_{0};
    int shaderGraphMetallicNode_{0};
    int shaderGraphRoughnessNode_{0};
    float shaderGraphBaseColor_[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float shaderGraphEmissive_[3]{0.0f, 0.0f, 0.0f};
    float shaderGraphMetallic_{0.0f};
    float shaderGraphRoughness_{0.5f};
    bool shaderGraphLoaded_{false};
    bool shaderGraphDirty_{false};
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
    int createProjectMaterialShaderIndex_{0};

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
    int gameViewportAspectIndex_{0};
    int gameViewportZoomIndex_{0};
    unsigned int sceneViewportTextureId_{0};
    unsigned int gameViewportTextureId_{0};
    bool viewportHovered_{false};
    bool viewportFocused_{false};
    bool sceneViewportHovered_{false};
    bool sceneViewportFocused_{false};
    bool gameViewportHovered_{false};
    bool gameViewportFocused_{false};
    bool gameViewportRenderDirty_{true};
    glm::vec2 lastRenderedGameViewportSize_{0.0f, 0.0f};
    bool editorCameraNavigating_{false};
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
    bool showTrackGenerator_{false};
    TrackGeneratorMode trackGeneratorMode_{TrackGeneratorMode::Preset};
    TrackSource trackSource_{};
    std::string inspectedTrackPath_;
    std::string trackGeneratorStatus_;
    int selectedTrackPointIndex_{-1};
    bool draggingTrackPoint_{false};
    bool trackDrawAddTool_{false};
    bool trackDrawPreviewValid_{false};
    glm::vec3 trackDrawPreviewPoint_{0.0f};
    bool trackRealtimeBake_{true};
    bool trackBakeDirty_{false};
    double trackBakeDirtyTime_{0.0};
    float trackPresetLength_{120.0f};
    float trackPresetWidth_{70.0f};
    float trackPresetRadius_{18.0f};
    int trackPresetPointCount_{16};
    struct MaterialHistoryState {
        std::string materialId;
        Material material;
    };
    std::vector<MaterialHistoryState> materialUndoStack_;
    std::vector<MaterialHistoryState> materialRedoStack_;
    bool materialEditActive_{false};
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
    SceneEditorFrameTimings frameTimings_{};

    bool sceneDirty_{false};
    std::function<void()> onDirty_{};
    std::function<void(const glm::vec3&, float)> onFocusObject_{};
    std::function<void(const glm::mat4&)> onEditorCameraViewChanged_{};
    std::function<bool()> getProfilerVisible_{};
    std::function<void(bool)> setProfilerVisible_{};
};

} // namespace raceman

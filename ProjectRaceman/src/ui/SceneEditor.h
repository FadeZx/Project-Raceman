#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <utility>
#include <filesystem>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "SceneEditorTypes.h"
#include "EditorProgress.h"
#include "SceneEditorVehicleRuntime.h"
#include "../input/InputManager.h"
#include "../physics/PhysicsLayers.h"
#include "../physics/VehicleConfig.h"
#include "../rendering/Renderer.h"
#include "../rendering/SkidMarks.h"
#include "../rendering/Material.h"
#include "../rendering/PrimitiveMeshes.h"
#include "../scripting/ObjectScript.h"
#include "../audio/VehicleSoundProfile.h"
#include "../audio/AudioManager.h"
#include "../audio/EngineSoundProfile.h"
#include "../audio/EngineSynth.h"
#include "../audio/TyreSoundProfile.h"
#include "../audio/TyreSynth.h"
#include "../rendering/SkyboxController.h"
#include "TrackGenerator.h"
#include "ObjImport.h"

namespace raceman {

class Renderer;
class Console;
class PhysicsWorld;
class AudioManager;
struct PhysicsColliderDesc;
struct PhysicsBodyDesc;
struct PhysicsCharacterDesc;
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
    void SetRenderer(Renderer* renderer) { renderer_ = renderer; }
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
    bool ShouldRouteInputToGame() const { return activeViewport_ == SceneEditorActiveViewport::Game || (scriptsRunning_ && gameViewportHovered_); }
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
    void SetProjectSettingsUndoRedo(bool shortcutTarget,
                                    std::function<void()> undo,
                                    std::function<void()> redo) {
        projectSettingsShortcutTarget_ = shortcutTarget;
        undoProjectSettings_ = std::move(undo);
        redoProjectSettings_ = std::move(redo);
    }

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
    void RequestSaveCurrentScene();
    void RequestSaveProject();
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
    const GraphicsProfile& GetGraphicsProfile() const { return graphicsProfile_; }
    void SetGraphicsProfile(const GraphicsProfile& profile) { graphicsProfile_ = profile; }
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
    bool ImportModelChild(const std::string& path, int meshIndex);
    void ScanObjDir(const std::string& dir);
    void SyncScripts() { SyncScriptProjectFiles(false); }
    std::vector<PhysicsColliderDesc> CollectMeshCollidersNeedingBake() const;

private:
    // UI panels
    void RenderScenePanel();
    void RenderInspectorPanel();
    void RenderMultiSelectionInspector();
    void RenderProjectPanel();
    void RenderViewportPanel();
    void RenderDockspaceHost();
    void RenderStatusBar(float deltaTime);
    bool IsPanelHiddenByFullscreen(const char* windowName) const;
    int PanelFullscreenWindowFlags(const char* windowName) const;
    void RestorePanelDockLayoutIfNeeded();
    void ApplyPanelFullscreenWindowSetup(const char* windowName);
    void HandlePanelHeadingDoubleClick(const char* windowName);
    void RenderModelAssetInspector();
    void RenderModelChildAssetInspector();
    unsigned int GetModelChildThumbnailTexture(const std::string& importPath,
                                               const ImportedMeshInfo& info,
                                               const std::string& materialId,
                                               int width,
                                               int height);
    unsigned int GetModelPackageThumbnailTexture(const std::string& importPath,
                                                 const std::vector<ImportedMeshInfo>& infos,
                                                 const std::vector<std::string>& materialIds,
                                                 int width,
                                                 int height);
    void ClearModelChildThumbnailCache(const std::string& importPath, int meshIndex);
    // Renders (and caches) a lit sphere textured with the given material for use
    // as its browser icon, Unity-style. Returns 0 if the material/renderer is
    // unavailable.
    unsigned int GetMaterialPreviewTexture(const std::string& materialId, int width, int height);
    // As above but for an already-resolved material value (per-object instances),
    // keyed by an explicit cache key.
    unsigned int GetMaterialPreviewTextureForMaterial(const std::string& cacheKey, const Material& resolved, int width, int height);
    // Effective material for an object: its embedded per-object instance override
    // resolved against the shared base, or just the shared material.
    Material ResolveObjectMaterial(const MeshRendererComponent& meshRenderer) const;
    bool RefreshModelAssetInspectorCache(bool forceReload);
    void RenderMaterialInspector();
    void RenderShaderGraphEditorWindow();
    // Inspector shown when a hand-written .vs/.fs asset is selected in the
    // project browser. Editing happens in the user's IDE; this panel surfaces
    // compile status, the GL error log, and a read-only source preview.
    void RenderShaderCodeAssetInspector();
    void RenderVehicleConfigEditorWindow();
    void RenderVehicleSoundEditorWindow();
    void RenderEngineSoundEditorWindow();
    void RenderVehicleComponentInspector(SceneObject& object,
                                         SceneInspectorComponentType& reorderDraggedType,
                                         SceneInspectorComponentType& reorderTargetType);
    // Chassis collision authoring: mode selection, shape list, and impact tuning.
    void RenderVehicleChassisCollisionInspector(SceneObject& object);
    void RenderMaterialProperties(const std::string& materialId, bool showBackButton);
    // Inline editor for an object's embedded per-object material instance
    // (edits meshRenderer.materialOverride, saves to the scene, never to disk).
    void RenderMaterialOverrideEditor(MeshRendererComponent& meshRenderer, const std::string& objectPreviewKey);
    // Shared material-editor body used by both the asset editor and the embedded
    // instance editor. `assetId` is the on-disk id (empty for instances);
    // `lockedBaseId` is the fixed inherited base for instances.
    void RenderMaterialEditor(Material* material, bool isEmbeddedInstance, const std::string& assetId,
                              const std::string& lockedBaseId, const std::string& previewCacheKey, bool showBackButton);
    void RenderProjectAssetPickerPopup();
    unsigned int GetComponentIconTexture(const std::string& filename);
    void HandleEditorShortcuts();
    void UpdateScripts(float deltaTime);
    void UpdateVehiclePhysics(float deltaTime);
    void CaptureVehicleRuntimeInputActions(bool routeInput);
    void BuildVehiclePhysicsBodyDescriptors(std::unordered_map<std::string, PhysicsBodyDesc>& outVehicleChassisBodies,
                                            std::unordered_set<std::string>& outConsumedVehiclePhysicsObjects);
    // Resolves the authored chassis collision config into collider descriptors
    // expressed in the vehicle's local space. Shared by the kinematic chassis body
    // build and the runtime sweep so both always agree on the collision volume.
    std::vector<PhysicsColliderDesc> BuildVehicleChassisColliderDescs(
        int vehicleObjectIndex,
        std::unordered_set<std::string>* outConsumedObjectIds) const;
    void BuildRuntimePhysicsDescriptors(std::vector<PhysicsBodyDesc>& outPhysicsBodies,
                                        std::vector<PhysicsCharacterDesc>& outPhysicsCharacters);
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
    void ClearVehicleSoundRuntime();
    void RebuildVehicleSoundRuntime();
    void UpdateVehicleSoundRuntime(float deltaTime);
    void PlayVehicleSoundStopTriggers();
    void UpdateAudio(float deltaTime);
    // Active AudioListener lookup, shared by the audio update and audio.debug.
    bool HasActiveAudioListener() const;
    glm::vec3 GetActiveAudioListenerPosition() const;
    // Resolves where the player actually hears from. Cinemachine drives the live
    // camera through runtimeCameraBrainState_ and never writes back to the
    // camera object's transform, so reading the transform leaves the listener
    // parked at the authored pose while the car drives away.
    bool GetActiveAudioListenerPose(glm::vec3& outPosition, glm::vec3& outForward,
                                    glm::vec3& outUp) const;

    // Acoustics of wherever the listener currently stands: the scene default,
    // blended with whichever reverb zone it is inside.
    struct ResolvedAudioEnvironment {
        float earlyGain{0.46f};
        float earlySpreadMs{48.0f};
        float tailGain{0.16f};
        float tailDecaySeconds{0.85f};
        float tailDamping{0.62f};
        float lowPassScale{1.0f};
        float volumeScale{1.0f};
    };
    ResolvedAudioEnvironment ResolveListenerEnvironment(const glm::vec3& listenerPosition) const;
    void RestoreFromPlayModeSnapshot();
    void TickPlayModeLoading();
    void RenderPlayModeLoadingPopup();
    void StartPlayModePhysicsBuild();
    void TickPendingSceneSave();
    void TickReflectionProbeBake();
    void TickRealtimeReflectionProbes();
    void RenderSceneSavePopup();
    int HotReloadRuntimeVehiclesForConfig(const std::string& configPath, const physics::VehicleConfig& config);
    void StartCollisionBake(std::vector<std::pair<PhysicsColliderDesc, std::string>> jobs, std::string title);
    bool TryBuildMeshColliderBakeJob(const SceneObject& object, PhysicsColliderDesc& outCollider, std::string& outLabel) const;
    void StartMeshColliderAutoBake(const SceneObject& object, const std::string& title);
    void StartMeshColliderAutoBakeForIndices(const std::vector<int>& objectIndices, const std::string& title);
    void StartSelectedMeshColliderAutoBake(const std::string& title);
    void TickCollisionBake();
    void RenderCollisionBakeInlineStatus();
    void TickMaterialExtract();
    void RenderMaterialExtractInlineStatus();
    void HandleConsoleCommand(const std::string& command);
    void UpdateRuntimeSystems(float deltaTime);
    void UpdateGizmo(Renderer& renderer);
    void UpdateImGuizmo();
    void SubmitGizmo(Renderer& renderer);
    void SubmitColliderWireframe(Renderer& renderer, int objectIndex, const glm::vec4& colorOverride, bool useColorOverride);
    // Wireframe for the vehicle chassis collision volume, drawn only when the
    // component has Debug Draw enabled.
    void SubmitVehicleChassisCollisionWireframe(Renderer& renderer, int objectIndex, const glm::vec4& colorOverride, bool useColorOverride);
    void SubmitAllColliders(Renderer& renderer);
    void SubmitAudioGizmos(Renderer& renderer);
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
    bool AssignSelectedLodMesh(const std::string& path);
    bool AssignMaterialToSelected(const std::string& materialId);
    bool AssignVehicleConfigToSelected(const std::string& configPath);
    bool AttachScriptToSelected(const std::string& scriptName, const std::string& scriptPath);
    bool CreateScriptAsset(const std::string& requestedName, bool attachToSelected = true);
    // (name, projectSourcePath) pairs for every .h+.cpp script under assets/.
    std::vector<std::pair<std::string, std::string>> ScanProjectScripts() const;
    bool SyncAttachmentScriptFields(ObjectScriptAttachment& attachment);
    bool CreateMaterialAsset(const std::string& requestedName, std::string* outMaterialId = nullptr, const std::string& shaderId = "pbr");
    bool CreateMaterialVariant(const std::string& baseMaterialId, const std::string& requestedName, std::string* outMaterialId = nullptr);
    bool CreateShaderGraphAsset(const std::string& requestedName, std::string* outGraphPath = nullptr);
    bool SaveShaderGraphAsset();
    // Stamps out a starter .vs/.fs pair whose interface matches what the
    // renderer binds, so the new shader renders correctly before it is edited.
    bool CreateShaderCodeAsset(const std::string& requestedName, std::string* outFragmentPath = nullptr);
    // Recompiles the shader behind a .vs/.fs asset and records the result.
    // Returns false and logs to the console when compilation fails.
    bool RecompileShaderCodeAsset(const std::string& shaderSourcePath, bool quiet = false);
    // Polls the mtime of shader sources in use and recompiles the ones that
    // changed on disk, so saving in an external IDE updates the viewport live.
    void TickShaderCodeWatcher(float deltaTime);
    bool CreateVehicleConfigAsset(const std::string& requestedName, std::string* outConfigPath = nullptr);
    // directoryOverride places the asset next to a related asset (e.g. the
    // vehicle config) instead of wherever the project browser is pointing.
    bool CreateVehicleSoundAsset(const std::string& requestedName, std::string* outProfilePath = nullptr,
                                 const std::string& directoryOverride = std::string());
    bool CreateSceneAsset(const std::string& requestedName, std::string* outScenePath = nullptr);
    bool CreateProjectFolder(const std::string& requestedName);
    bool SaveObjectAsPrefab(int objectIndex, const std::string& path);
    bool InstantiatePrefab(const std::string& path);

    // Linked prefab instances: override queries and Apply/Revert operations.
    struct PrefabSourceDocument {
        std::string path;
        bool valid{false};
        std::vector<SceneObject> objects;                 // as stored in the file; ids ARE prefab-local ids
        std::unordered_map<std::string, int> byLocalId;   // prefab-local id -> index into objects
    };
    bool ParsePrefabFileObjects(const std::string& path, std::vector<SceneObject>& outObjects, bool resolveMeshes);
    const PrefabSourceDocument* GetOrLoadPrefabSourceDocument(const std::string& sourcePrefabPath);
    void InvalidatePrefabSourceDocument(const std::string& sourcePrefabPath);
    bool IsPrefabInstance(int objectIndex) const;
    bool IsPrefabInstanceRoot(int objectIndex) const;
    bool HasComponentOverride(int objectIndex, SceneComponentType componentType) const;
    void RefreshPrefabOverrideFlags(int objectIndex);
    // Copy of the object with live cross-object id references translated to
    // prefab-local ids so it can be compared with / written into prefab data.
    SceneObject NormalizeInstanceObjectForPrefabComparison(const SceneObject& object) const;
    // Reverse translation: prefab-local id references -> this instance's live ids.
    SceneObject TranslatePrefabObjectToInstance(const SceneObject& source, const std::string& instanceRootId) const;
    void RevertComponentToPrefab(int objectIndex, SceneComponentType componentType);
    void RevertObjectToPrefab(int objectIndex);
    void RevertInstanceToPrefab(int instanceRootIndex);
    void ApplyComponentToPrefab(int objectIndex, SceneComponentType componentType);
    void ApplyObjectToPrefab(int objectIndex);
    void ApplyInstanceToPrefab(int instanceRootIndex);
    void UnpackPrefabInstance(int objectIndex);
    void PropagateApplyToSiblingInstances(const std::string& sourcePrefabPath,
                                          const std::string& appliedPrefabLocalId,
                                          SceneComponentType componentType,
                                          int excludeObjectIndex);
    void SyncScriptProjectFiles(bool logResult = true);
    void OpenMaterialEditor(const std::string& materialId);
    void OpenShaderGraphEditor(const std::string& graphPath);
    void OpenVehicleConfigEditor(const std::string& configPath);
    void OpenVehicleSoundEditor(const std::string& profilePath);
    void OpenEngineSoundEditor(const std::string& profilePath);
    // Cross-session panel state for the two audio editors: which profile each
    // had open, the tab it was left on, and the audition rig's settings.
    void LoadAudioEditorPanelState();
    void SaveAudioEditorPanelState();
    void TickAudioEditorPanelStatePersistence();
    std::string SerializeAudioEditorPanelState() const;
    void PushEngineSoundUndoState();
    void UndoEngineSound();
    void RedoEngineSound();
    bool CreateEngineSoundAsset(const std::string& requestedName, std::string* outProfilePath = nullptr,
                                const std::string& directoryOverride = std::string());
    void StopEngineSoundAudition();
    // Re-bakes the edited profile into any vehicle already playing it, so tuning
    // is heard on the car in play mode rather than only in the audition voice.
    void ApplyEngineSoundEditsToRuntime();
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
    void ResetTrackSurfaceSettings();
    const ColliderSurfaceConfig& GetProjectTrackSurfaceSettings(TrackSurfaceType type) const;
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
    GraphicsProfile graphicsProfile_{};
    SkidMarkSystem skidMarks_;
    void UpdateWeather(float deltaTime);
    float runtimeWetness_{0.0f};
    bool weatherWetnessInitialized_{false};
    // Built from the live graphics profile each frame so the editor's sliders
    // take effect without restarting the run. Not const: resolving the decal
    // prefab warms the prefab source cache.
    SkidMarkSettings SkidMarkSettingsFromProfile(const GraphicsProfile& profile);
    // Reads the Decal component out of the configured skid-mark prefab. The
    // prefab is a look template, not something instantiated: nothing is added to
    // the scene, so runtime marks stay out of the hierarchy, undo and saves.
    SkidMarkDecalTemplate ResolveSkidMarkDecalTemplate(const std::string& prefabPath);
    std::string projectName_{"Project Raceman"};
    std::string assetsRootSetting_{"assets"};
    std::string defaultScenePath_{"assets/scenes/EditorScene.scene.json"};
    std::string lastScenePath_{"assets/scenes/EditorScene.scene.json"};
    std::string savePath_{"assets/scenes/EditorScene.scene.json"};
    PhysicsLayerNames physicsLayerNames_{};
    PhysicsLayerCollisionMatrix physicsLayerCollisionMatrix_{};
    TrackSurfaceSettings trackSurfaceSettings_{};
    std::vector<std::string> projectTags_{"Untagged"};
    std::vector<InputProfile> inputProfiles_{};
    std::vector<WheelSettingsProfile> wheelSettingsProfiles_{};
    int selectedInputProfileIndex_{0};
    int selectedInputDevicePage_{0};
    int selectedWheelSettingsProfileIndex_{0};
    bool projectInputTestActive_{false};
    int projectInputTestDeviceIndex_{0};

    // shared primitives
    std::unordered_map<std::string, PrimitiveMesh> builtInPrimitiveMeshes_;
    Console* console_{nullptr};
    InputManager* inputManager_{nullptr};
    AudioManager* audioManager_{nullptr};
    Renderer* renderer_{nullptr};

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

    std::vector<std::string> projectDirectories_;
    std::vector<std::string> projectFiles_;
    std::unordered_map<std::string, std::vector<std::string>> projectDirectoryChildren_;
    std::unordered_map<std::string, std::vector<std::string>> projectFilesByDirectory_;
    std::vector<std::string> projectRootDirectories_;
    std::string selectedProjectDirectory_{"assets"};
    std::string selectedProjectFile_;
    int selectedModelChildMeshIndex_{-1};

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

    // Hand-written code shaders. Compile results are keyed by the shader's
    // project asset path ("shaders/water.fs") so the inspector can report on a
    // file whether or not any material currently references it.
    struct ShaderCodeStatus {
        bool compiled{true};
        std::string log;
    };
    std::unordered_map<std::string, ShaderCodeStatus> shaderCodeStatus_;
    // mtime of each watched source, plus the time the change was first seen.
    // Recompiles wait out a short debounce because IDEs write files in several
    // steps and reading mid-write yields spurious syntax errors.
    struct ShaderCodeWatchEntry {
        std::filesystem::file_time_type modifiedTime{};
        double pendingSince{0.0};
        bool pending{false};
        bool seen{false};
    };
    std::unordered_map<std::string, ShaderCodeWatchEntry> shaderCodeWatch_;
    float shaderCodeWatchAccumulator_{0.0f};
    // Read-only source preview for the inspector, refreshed only on mtime change.
    std::string shaderCodePreviewPath_;
    std::string shaderCodePreviewText_;
    std::filesystem::file_time_type shaderCodePreviewModifiedTime_{};

    bool showVehicleConfigEditor_{false};
    std::string inspectedVehicleConfigPath_;
    physics::VehicleConfig inspectedVehicleConfig_{};
    bool inspectedVehicleConfigLoaded_{false};
    std::string inspectedVehicleConfigError_;
    bool vehicleConfigEditorHovered_{false};
    bool vehicleConfigEditorFocused_{false};
    bool vehicleConfigEditActive_{false};
    bool vehicleConfigEditorFocusRequested_{false};
    double vehicleConfigEditorHighlightUntil_{0.0};
    bool showVehicleSoundEditor_{false};
    std::string inspectedVehicleSoundPath_;
    VehicleSoundProfile inspectedVehicleSound_{};
    bool inspectedVehicleSoundLoaded_{false};
    std::string inspectedVehicleSoundError_;
    bool vehicleSoundEditorHovered_{false};
    bool vehicleSoundEditorFocused_{false};
    bool vehicleSoundEditActive_{false};
    bool vehicleSoundEditorFocusRequested_{false};
    double vehicleSoundEditorHighlightUntil_{0.0};

    // --- Engine Sound (procedural synth) editor ---
    bool showEngineSoundEditor_{false};
    std::string inspectedEngineSoundPath_;
    EngineSoundProfile inspectedEngineSound_{};
    bool inspectedEngineSoundLoaded_{false};
    std::string inspectedEngineSoundError_;
    bool engineSoundEditorHovered_{false};
    bool engineSoundEditorFocused_{false};
    bool engineSoundEditActive_{false};
    bool engineSoundEditorFocusRequested_{false};
    double engineSoundEditorHighlightUntil_{0.0};
    bool engineSoundProfileDirty_{true};   // profile changed, needs re-bake
    // Live audition. Engine sound is tuned by ear, so the panel drives its own
    // 2D synth voice instead of requiring a trip through play mode.
    std::shared_ptr<EngineSynth> engineSoundAuditionSynth_;
    AudioVoice* engineSoundAuditionVoice_{nullptr};
    float engineSoundAuditionRpm_{900.0f};
    float engineSoundAuditionThrottle_{0.0f};
    float engineSoundAuditionRedline_{7000.0f};
    float engineSoundAuditionIdle_{900.0f};
    bool engineSoundAuditionSweep_{false};
    float engineSoundAuditionSweepT_{0.0f};
    int engineSoundSelectedOrder_{0};
    std::string engineSoundEditorActiveTab_;   // tab currently shown, remembered across sessions
    std::string engineSoundEditorPendingTab_;  // tab to re-select on the first frame after a restore
    std::string audioEditorPanelStateLastWritten_;
    double audioEditorPanelStateNextWriteTime_{0.0};
    ProjectAssetPickerMode assetPickerMode_{ProjectAssetPickerMode::None};
    int pendingLodLevelIndex_{-1};
    bool scriptsRunning_{false};
    bool scriptsPaused_{false};
    float runtimeSimulationAccumulator_{0.0f};
    bool playModeScriptAssemblyReady_{false};
    bool showCreateScriptPopup_{false};
    char createScriptNameBuffer_[128]{};
    char createMaterialNameBuffer_[128]{};
    char createVehicleConfigNameBuffer_[128]{};
    char assetPickerSearchBuffer_[128]{};
    bool showAssetPickerCreatePopup_{false};
    char createTagNameBuffer_[128]{};
    bool showCreateProjectAssetPopup_{false};
    ProjectCreateAssetType createProjectAssetType_{ProjectCreateAssetType::None};
    char createProjectAssetNameBuffer_[128]{};
    int createProjectMaterialShaderIndex_{0};

    bool showSavePrefabPopup_{false};
    int pendingPrefabObjectIndex_{-1};
    char savePrefabNameBuffer_[128]{};
    bool showCreateMaterialVariantPopup_{false};
    std::string pendingMaterialVariantBaseId_;
    char createMaterialVariantNameBuffer_[128]{};
    // When set, the variant produced by the Create Material Variant popup is
    // assigned to the current selection (used by the object Material slot's
    // "Create Variant" action, mirroring Unity's create-and-assign flow).
    bool pendingMaterialVariantAssignToSelection_{false};

    struct RuntimeScriptInstance {
        std::string objectId;
        std::size_t attachmentIndex{0};
        std::unique_ptr<IObjectScript> instance;
        bool started{false};
    };
    std::vector<RuntimeScriptInstance> runtimeScripts_;

    std::vector<RuntimeVehicleInstance> runtimeVehicles_;

    struct RuntimeCinemachineState {
        glm::vec3 smoothedPosition{0.0f};
        glm::quat smoothedRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 previousFollowTargetPosition{0.0f};
        std::string followTargetId;
        bool followTargetInitialized{false};
        bool initialized{false};
    };
    std::unordered_map<std::string, RuntimeCinemachineState> runtimeCinemachineStates_;
    struct RuntimeCameraBrainState {
        std::string activeVirtualCameraId;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 blendStartPosition{0.0f};
        glm::quat blendStartRotation{1.0f, 0.0f, 0.0f, 0.0f};
        float fieldOfView{60.0f};
        float blendStartFieldOfView{60.0f};
        float nearClip{0.1f};
        float farClip{500.0f};
        glm::vec4 clearColor{0.02f, 0.02f, 0.02f, 1.0f};
        float blendElapsed{0.0f};
        float blendDuration{0.0f};
        bool initialized{false};
    };
    RuntimeCameraBrainState runtimeCameraBrainState_{};

    // Audio source runtime (one per AudioSource component)
    struct RuntimeAudioSourceInstance {
        std::string objectId;
        AudioVoice* voice{nullptr};
    };
    std::vector<RuntimeAudioSourceInstance> runtimeAudioSources_;

    // Vehicle sound runtime (one per VehicleSound component)
    struct RuntimeVehicleSoundLayerState {
        AudioVoice* voice{nullptr};
        float smoothVolume{0.0f};
        float smoothPitch{1.0f};
    };
    struct RuntimeVehicleSoundInstance {
        std::string objectId;           // vehicle object id
        std::string vehicleObjectId;    // same object that has VehicleComponent
        VehicleSoundProfile profile;
        std::vector<RuntimeVehicleSoundLayerState> layers;

        // Procedural path. When the assigned asset is a .enginesound.json the
        // pitched sample layers above are unused and a single synth voice
        // replaces them entirely.
        bool usesSynth{false};
        EngineSoundProfile engineProfile;
        std::shared_ptr<EngineSynth> synth;
        AudioVoice* synthVoice{nullptr};
        bool lastLimiterCut{false};
        bool lastShiftCut{false};

        // Tyres. Continuous rolling and slip squeal, one voice per vehicle.
        std::shared_ptr<TyreSoundProfile> tyreProfile;
        std::shared_ptr<TyreSynth> tyreSynth;
        AudioVoice* tyreVoice{nullptr};
        float lastSuspensionTravel[4]{};
        bool  lastSuspensionValid{false};
        float overrunPopCooldown{0.0f};
        // Trigger detection state
        int  lastGear{0};
        bool lastThrottleHigh{false};   // throttle was >0.8 last frame
        float lastLateralSpeed{0.0f};
    };
    std::vector<RuntimeVehicleSoundInstance> runtimeVehicleSounds_;

    // Declared here rather than with the other audio methods: they take
    // RuntimeVehicleSoundInstance by reference, which is defined just above.
    void CreateVehicleTyreVoice(RuntimeVehicleSoundInstance& inst, const SceneObject& obj,
                                const glm::vec3& position);
    void UpdateVehicleTyreSound(RuntimeVehicleSoundInstance& inst, RuntimeVehicleInstance& vehicle,
                                const glm::vec3& vehiclePos, float listenerDistance, float deltaTime);


    // Phase 0 spike state for the "audio.synthtest" console command.
    std::shared_ptr<EngineSynthGenerator> synthTestGenerator_;
    AudioVoice* synthTestVoice_{nullptr};
    glm::vec3 previousListenerPosition_{0.0f};
    bool listenerVelocityValid_{false};

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
    bool showAllColliders_{false};
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
    struct FullscreenPanelState {
        bool active{false};
        std::string windowName;
        unsigned int originalDockId{0};
        std::string restoreWindowName;
        unsigned int restoreDockId{0};
        std::string dockSettingsSnapshot;
    };
    FullscreenPanelState fullscreenPanel_;
    glm::vec2 viewportPanelPos_{0.0f, 0.0f};
    glm::vec2 viewportPanelSize_{0.0f, 0.0f};
    glm::vec2 sceneViewportPos_{0.0f, 0.0f};
    glm::vec2 sceneViewportSize_{0.0f, 0.0f};
    glm::vec2 gameViewportPos_{0.0f, 0.0f};
    glm::vec2 gameViewportSize_{0.0f, 0.0f};
    int gameViewportAspectIndex_{0};
    float gameViewportZoomScale_{1.0f};
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
    bool colliderEditMode_{false};
    bool colliderGizmoActive_{false};
    bool colliderGizmoDirtyDuringDrag_{false};
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
    struct EngineSoundHistoryState {
        EngineSoundProfile profile;
    };
    std::vector<EngineSoundHistoryState> engineSoundUndoStack_;
    std::vector<EngineSoundHistoryState> engineSoundRedoStack_;
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
    struct ModelAssetInspectorCache {
        std::string selectedPath;
        std::string importPath;
        std::string error;
        std::shared_ptr<::Model> model;
        std::vector<ImportedMeshInfo> infos;
        std::vector<std::string> materialIds;
        bool loaded{false};
    };
    ModelAssetInspectorCache modelAssetInspectorCache_;
    std::unordered_map<std::string, ModelAssetInspectorCache> browserModelPackageCaches_;
    std::unordered_set<std::string> expandedModelPackages_;
    struct ModelThumbnailCacheEntry {
        unsigned int framebuffer{0};
        unsigned int texture{0};
        unsigned int depthRenderbuffer{0};
        int width{0};
        int height{0};
    };
    std::unordered_map<std::string, ModelThumbnailCacheEntry> modelThumbnailCache_;
    // Unity-style material thumbnails: a lit sphere rendered with the material.
    // Keyed by material id; each entry re-renders in place when the material's
    // resolved content hash changes, so editing a material refreshes its icon.
    struct MaterialPreviewCacheEntry {
        unsigned int framebuffer{0};
        unsigned int texture{0};
        unsigned int depthRenderbuffer{0};
        int width{0};
        int height{0};
        std::size_t contentHash{0};
        bool rendered{false};
    };
    std::unordered_map<std::string, MaterialPreviewCacheEntry> materialPreviewCache_;
    // Material keys already reported by the preview texture diagnostic, so it
    // logs once per material rather than on every re-render.
    std::unordered_set<std::string> materialPreviewDiagnosticsLogged_;
    unsigned int materialPreviewSphereVao_{0};
    unsigned int materialPreviewSphereVbo_{0};
    unsigned int materialPreviewSphereEbo_{0};
    unsigned int materialPreviewSphereIndexCount_{0};
    // HDR intermediate + ACES/sRGB resolve so previews get the same tone mapping
    // as the viewport (raw HDR would clip emissive/bright materials to white).
    unsigned int materialPreviewHdrFbo_{0};
    unsigned int materialPreviewHdrColor_{0};
    unsigned int materialPreviewHdrDepth_{0};
    int materialPreviewHdrWidth_{0};
    int materialPreviewHdrHeight_{0};
    unsigned int materialPreviewTonemapProgram_{0};
    unsigned int materialPreviewFullscreenVao_{0};
    std::vector<MaterialHistoryState> materialUndoStack_;
    std::vector<MaterialHistoryState> materialRedoStack_;
    bool materialEditActive_{false};
    HistoryState playModeSnapshot_{};
    bool hasPlayModeSnapshot_{false};
    std::unique_ptr<PhysicsWorld> physicsWorld_;

    struct PlayModeLoadState {
        // Play-mode entry runs as one staged pipeline so every step is visible
        // in the loading popup, not just the physics/collision-cache step.
        enum class Phase { Idle, Preparing, BuildingScripts, LoadingScripts, PreparingPhysics, BuildingPhysics, Finalizing };
        struct ScriptBuildStatus {
            std::atomic<bool> isDone{false};
            std::atomic<bool> success{false};
            std::string error;
            mutable std::mutex mutex;
        };
        Phase phase{Phase::Idle};
        // Set once the current phase's label has been on screen for a frame;
        // main-thread stages only run after that, so each one is actually seen.
        bool stageAnnounced{false};
        std::shared_ptr<ScriptBuildStatus> scriptBuild;
        std::unique_ptr<std::thread> scriptBuildThread;
        std::shared_ptr<PhysicsBuildProgress> progress;
        std::unique_ptr<std::thread> buildThread;
        std::unique_ptr<PhysicsWorld> pendingWorld;
        std::chrono::time_point<std::chrono::high_resolution_clock> buildStart{};
        EditorProgressTask window;
    };
    PlayModeLoadState playModeLoad_;
    struct CollisionBakeState {
        bool active{false};
        std::string title;
        std::shared_ptr<PhysicsBuildProgress> progress;
        std::unique_ptr<std::thread> thread;
        std::chrono::time_point<std::chrono::high_resolution_clock> start{};
        std::atomic<int> bakedCount{0};
        std::atomic<int> failedCount{0};
        std::string lastError;
        mutable std::mutex mutex;
        EditorProgressTask window;
    };
    CollisionBakeState collisionBake_;
    struct MaterialExtractState {
        bool active{false};
        bool reloadMaterials{false};
        bool refreshProjectFiles{false};
        std::string title;
        std::shared_ptr<PhysicsBuildProgress> progress;
        std::unique_ptr<std::thread> thread;
        std::atomic<int> itemCount{0};
        std::atomic<int> errorCount{0};
        std::string summary;
        mutable std::mutex mutex;
        EditorProgressTask window;
    };
    MaterialExtractState materialExtract_;
    SceneProfilerStats profilerStats_{};
    SceneEditorFrameTimings frameTimings_{};
    std::string lastPhysicsCacheStatus_{"Ready"};

    bool sceneDirty_{false};
    bool sceneSaveRequested_{false};
    bool projectSaveRequested_{false};
    bool saveWaitingForPopup_{false};
    EditorProgressTask saveProgress_{};
    struct ReflectionProbeBakeState {
        std::string objectId;
        EditorProgressTask window;
    } reflectionProbeBake_;
    // Rotates realtime updates across every in-range probe so one probe per
    // frame gets fresh faces without starving the others.
    std::size_t realtimeProbeRoundRobin_{0};
    std::unordered_map<std::string, PrefabSourceDocument> prefabSourceCache_;
    std::function<void()> onDirty_{};
    std::function<void(const glm::vec3&, float)> onFocusObject_{};
    std::function<void(const glm::mat4&)> onEditorCameraViewChanged_{};
    bool projectSettingsShortcutTarget_{false};
    std::function<void()> undoProjectSettings_{};
    std::function<void()> redoProjectSettings_{};
    std::function<bool()> getProfilerVisible_{};
    std::function<void(bool)> setProfilerVisible_{};
};

} // namespace raceman

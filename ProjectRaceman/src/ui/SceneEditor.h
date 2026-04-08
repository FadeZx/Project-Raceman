#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

#include <glm/glm.hpp>
#include "../rendering/Material.h"
#include "../scripting/ObjectScript.h"

class Model;

namespace raceman {

class Renderer;
class PrimitivePlane;
class Console;
class InputManager;
class PhysicsWorld;

enum class GizmoMode {
    Move,
    Rotate,
    Scale
};

enum class ProjectAssetPickerMode {
    None,
    ReplaceMesh,
    AssignMaterial,
    AttachScript
};

enum class ProjectCreateAssetType {
    None,
    Folder,
    Scene,
    Material,
    Script
};

enum class SceneComponentType {
    Transform,
    MeshFilter,
    MeshRenderer,
    Script,
    Rigidbody,
    BoxCollider,
    SphereCollider,
    CapsuleCollider,
    Camera,
    Light
};

enum class RigidbodyBodyType {
    Static,
    Dynamic
};

enum class SceneEditorViewportMode {
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
    std::string importedMaterialName;
    std::string diffuseTexturePath;
    unsigned int diffuseTextureId{0};
    unsigned int vao{0};
    unsigned int indexCount{0};
    glm::vec3 localBoundsMin{-0.5f, -0.5f, -0.5f};
    glm::vec3 localBoundsMax{0.5f, 0.5f, 0.5f};
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
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
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

struct CameraComponent {
    bool enabled{true};
    bool isMain{true};
    float fieldOfViewDegrees{60.0f};
    float nearClip{0.1f};
    float farClip{500.0f};
    glm::vec4 clearColor{0.02f, 0.02f, 0.02f, 1.0f};
};

struct LightComponent {
    bool enabled{true};
    LightType type{LightType::Point};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    float range{10.0f};
    float spotAngleDegrees{30.0f};
};

struct SceneObject {
    std::string id;    // simple unique id
    std::string parentId;
    std::string name;  // editable name
    std::string type;  // legacy/display object type, e.g., "Plane", "Mesh"
    Transform transform;
    bool enabled{true};
    bool hasMeshFilter{true};
    bool hasMeshRenderer{true};
    bool hasScriptComponent{true};
    bool hasRigidbody{false};
    bool hasBoxCollider{false};
    bool hasSphereCollider{false};
    bool hasCapsuleCollider{false};
    bool hasCamera{false};
    bool hasLight{false};
    MeshFilterComponent meshFilter;
    MeshRendererComponent meshRenderer;
    ScriptComponent scriptComponent;
    RigidbodyComponent rigidbody;
    BoxColliderComponent boxCollider;
    SphereColliderComponent sphereCollider;
    CapsuleColliderComponent capsuleCollider;
    CameraComponent camera;
    LightComponent light;
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
    void SetInputManager(InputManager* inputManager) { inputManager_ = inputManager; }
    bool IsRunMode() const { return scriptsRunning_; }
    bool IsGameViewActive() const { return viewportMode_ == SceneEditorViewportMode::Game; }
    bool TryGetGameCamera(glm::mat4& outView, glm::mat4& outProj, float aspect, glm::vec4* outClearColor = nullptr) const;

    // Notify app when editor content changes
    void SetOnDirty(std::function<void()> cb) { onDirty_ = std::move(cb); }
    void SetOnFocusObject(std::function<void(const glm::vec3&, float)> cb) { onFocusObject_ = std::move(cb); }

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
    std::vector<std::string> GetSceneAssetPaths() const;
    const std::string& GetCurrentScenePath() const { return savePath_; }
    const std::string& GetProjectName() const { return projectName_; }

    void ImportObj(const std::string& path);
    void ScanObjDir(const std::string& dir);

private:
    // UI panels
    void RenderScenePanel();
    void RenderInspectorPanel();
    void RenderMultiSelectionInspector();
    void RenderProjectPanel();
    void RenderMaterialInspector();
    void RenderMaterialProperties(const std::string& materialId, bool showBackButton);
    void RenderProjectAssetPickerPopup();
    unsigned int GetComponentIconTexture(const std::string& filename);
    void HandleEditorShortcuts();
    void UpdateScripts(float deltaTime);
    void UpdatePhysics(float deltaTime);
    void ResetPhysicsVelocities();
    void SetScriptsRunning(bool running);
    void SetScriptsPaused(bool paused);
    void RebuildScriptRuntime();
    void ClearScriptRuntime();
    void HandleConsoleCommand(const std::string& command);
    void UpdateGizmo(Renderer& renderer);
    void SubmitGizmo(Renderer& renderer);
    void TrySelectObjectAtMouse(Renderer& renderer);
    void PushUndoState();
    void Undo();
    void Redo();
    void RequestFocusSelectedObject();

    // Actions
    void AddPlane();
    void AddEmptyObject();
    void AddCameraObject();
    void AddLightObject(LightType type);
    void DeleteSelectedObject();
    bool ReplaceSelectedMeshWithPlane();
    bool ReplaceSelectedMeshFromObj(const std::string& path);
    bool AssignMaterialToSelected(const std::string& materialId);
    bool AttachScriptToSelected(const std::string& scriptName, const std::string& scriptPath);
    bool CreateScriptAsset(const std::string& requestedName, bool attachToSelected = true);
    bool CreateMaterialAsset(const std::string& requestedName, std::string* outMaterialId = nullptr);
    bool CreateSceneAsset(const std::string& requestedName, std::string* outScenePath = nullptr);
    bool CreateProjectFolder(const std::string& requestedName);
    void SyncScriptProjectFiles();
    void OpenMaterialEditor(const std::string& materialId);
    void BeginObjectRename(int index);
    void BeginProjectFileRename(const std::string& path);
    void CommitProjectFileRename();
    void DeleteProjectFile(const std::string& path);
    void DeleteProjectFolder(const std::string& path);
    bool MoveProjectFile(const std::string& path, const std::string& targetDirectory);
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
    int FindObjectIndexById(const std::string& id) const;
    bool IsObjectEffectivelyEnabled(int index) const;
    bool IsDescendantOf(const std::string& objectId, const std::string& potentialAncestorId) const;
    void SetParent(int childIndex, int parentIndex);
    glm::mat4 GetObjectWorldMatrix(int index) const;
    glm::vec3 GetObjectWorldPosition(int index) const;

    // Utils
    static std::string MakeId(const std::string& base);

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

    // shared primitives
    std::unique_ptr<PrimitivePlane> planePrim_;
    Console* console_{nullptr};
    InputManager* inputManager_{nullptr};

    // Materials
    MaterialManager materialManager_;
    std::unordered_map<std::string, unsigned int> componentIconTextures_;

    // Import dialog state
    bool showImportObjPopup_{false};
    char importPath_[512]{};
    std::string objScanDir_{"assets/mesh"};
    std::vector<std::string> objFiles_;
    int objSelectIndex_{-1};

    std::vector<std::string> projectDirectories_;
    std::vector<std::string> projectFiles_;
    std::string selectedProjectDirectory_{"assets"};
    std::string selectedProjectFile_;

    bool inspectMaterial_{false};
    std::string inspectedMaterialId_;
    ProjectAssetPickerMode assetPickerMode_{ProjectAssetPickerMode::None};
    bool scriptsRunning_{false};
    bool scriptsPaused_{false};
    bool showCreateScriptPopup_{false};
    char createScriptNameBuffer_[128]{};
    char createMaterialNameBuffer_[128]{};
    bool showCreateProjectAssetPopup_{false};
    ProjectCreateAssetType createProjectAssetType_{ProjectCreateAssetType::None};
    char createProjectAssetNameBuffer_[128]{};

    struct RuntimeScriptInstance {
        std::string objectId;
        std::size_t attachmentIndex{0};
        std::unique_ptr<IObjectScript> instance;
        bool started{false};
    };
    std::vector<RuntimeScriptInstance> runtimeScripts_;

    int renamingObjectIndex_{-1};
    bool focusObjectRename_{false};
    char objectRenameBuffer_[128]{};

    std::string renamingProjectFile_;
    bool focusProjectRename_{false};
    char projectRenameBuffer_[260]{};

    int hoveredGizmoAxis_{-1};
    int activeGizmoAxis_{-1};
    GizmoMode gizmoMode_{GizmoMode::Move};
    glm::vec2 gizmoDragStartMouse_{0.0f};
    glm::vec3 gizmoDragStartPosition_{0.0f};
    glm::vec3 gizmoDragStartRotation_{0.0f};
    glm::vec3 gizmoDragStartScale_{1.0f};
    bool gizmoDirtyDuringDrag_{false};
    bool inspectorEditActive_{false};

    struct HistoryState {
        std::vector<SceneObject> objects;
        int selectedIndex{-1};
        std::vector<int> selectedIndices;
    };
    std::vector<HistoryState> undoStack_;
    std::vector<HistoryState> redoStack_;
    HistoryState playModeSnapshot_{};
    bool hasPlayModeSnapshot_{false};
    SceneEditorViewportMode viewportMode_{SceneEditorViewportMode::Scene};
    std::unique_ptr<PhysicsWorld> physicsWorld_;

    std::function<void()> onDirty_{};
    std::function<void(const glm::vec3&, float)> onFocusObject_{};
};

} // namespace raceman

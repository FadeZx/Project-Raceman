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

enum class SceneComponentType {
    Transform,
    MeshFilter,
    MeshRenderer,
    Script,
    Rigidbody,
    BoxCollider,
    SphereCollider,
    CapsuleCollider
};

enum class RigidbodyBodyType {
    Static,
    Dynamic
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

struct SceneObject {
    std::string id;    // simple unique id
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
    MeshFilterComponent meshFilter;
    MeshRendererComponent meshRenderer;
    ScriptComponent scriptComponent;
    RigidbodyComponent rigidbody;
    BoxColliderComponent boxCollider;
    SphereColliderComponent sphereCollider;
    CapsuleColliderComponent capsuleCollider;
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
    void SubmitDraws(Renderer& renderer);
    void SetConsole(Console* console);
    void SetInputManager(InputManager* inputManager) { inputManager_ = inputManager; }
    bool IsRunMode() const { return scriptsRunning_; }

    // Notify app when editor content changes
    void SetOnDirty(std::function<void()> cb) { onDirty_ = std::move(cb); }

    // Control persistence location and access from Application
    void SetSavePath(const std::string& path);
    void Save(const std::string& path);
    void Load(const std::string& path);

    void ImportObj(const std::string& path);
    void ScanObjDir(const std::string& dir);

private:
    // UI panels
    void RenderScenePanel();
    void RenderInspectorPanel();
    void RenderProjectPanel();
    void RenderMaterialInspector();
    void RenderProjectAssetPickerPopup();
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

    // Actions
    void AddPlane();
    void DeleteSelectedObject();
    bool ReplaceSelectedMeshWithPlane();
    bool ReplaceSelectedMeshFromObj(const std::string& path);
    bool AssignMaterialToSelected(const std::string& materialId);
    bool AttachScriptToSelected(const std::string& scriptName, const std::string& scriptPath);
    bool CreateScriptAsset(const std::string& requestedName);
    void SyncScriptProjectFiles();
    void OpenMaterialEditor(const std::string& materialId);
    void BeginObjectRename(int index);
    void BeginProjectFileRename(const std::string& path);
    void CommitProjectFileRename();
    void DeleteProjectFile(const std::string& path);
    void SelectProjectFile(const std::string& path);
    void RefreshProjectFiles();

    void Select(int index);

    // Utils
    static std::string MakeId(const std::string& base);

private:
    std::vector<SceneObject> objects_;
    int selectedIndex_{-1};

    // persistence
    std::string savePath_{"config/scenes/EditorScene.json"};

    // shared primitives
    std::unique_ptr<PrimitivePlane> planePrim_;
    Console* console_{nullptr};
    InputManager* inputManager_{nullptr};

    // Materials
    MaterialManager materialManager_;

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
    };
    std::vector<HistoryState> undoStack_;
    std::vector<HistoryState> redoStack_;
    HistoryState playModeSnapshot_{};
    bool hasPlayModeSnapshot_{false};

    std::function<void()> onDirty_{};
};

} // namespace raceman

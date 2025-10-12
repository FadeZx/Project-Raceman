#pragma once

#include <string>
#include <vector>
#include <memory>

#include <glm/glm.hpp>
#include "../rendering/Material.h"

class Model;

namespace raceman {

class Renderer;
class PrimitivePlane;
class Console;

struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEuler{0.0f, 0.0f, 0.0f}; // degrees
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

struct SceneObject {
    std::string id;    // simple unique id
    std::string name;  // editable name
    std::string type;  // e.g., "Plane", "Mesh"
    Transform transform;

    // Visuals
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

    // Render data (if renderable)
    unsigned int vao{0};
    unsigned int indexCount{0};
    std::string materialId; // e.g., "pbr_default"
    std::shared_ptr<::Model> modelRef; // keep model alive for VAO/VBO lifetime (global Model)
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
    void SetConsole(Console* console) { console_ = console; }
    void ImportObj(const std::string& path);
    void ScanObjDir(const std::string& dir);

private:
    // UI panels
    void RenderScenePanel();
    void RenderInspectorPanel();

    // Actions
    void AddPlane();

    void Select(int index);
    void Save(const std::string& path);
    void Load(const std::string& path);

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

    // Materials
    MaterialManager materialManager_;

    // Import dialog state
    bool showImportObjPopup_{false};
    char importPath_[512]{};
    std::string objScanDir_{"assets/mesh"};
    std::vector<std::string> objFiles_;
    int objSelectIndex_{-1};
};

} // namespace raceman
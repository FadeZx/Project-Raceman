#pragma once

#include <string>
#include <vector>
#include <memory>

#include <glm/glm.hpp>

namespace raceman {

class Renderer;
class PrimitivePlane;

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

    // Render data (if renderable)
    unsigned int vao{0};
    unsigned int indexCount{0};
    std::string materialId; // e.g., "pbr_default"
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
};

} // namespace raceman
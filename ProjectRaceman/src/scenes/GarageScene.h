#pragma once

#include "Scene.h"
#include "MeshResource.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

class Skybox;
class Shader;

struct aiScene;
struct aiMesh;
struct aiNode;

namespace raceman {

class GarageScene : public Scene {
public:
    explicit GarageScene(std::shared_ptr<Renderer> renderer);

    void OnSceneActivated() override;
    void Update(float deltaTime) override;
    void Render(Renderer& renderer) override;
    void RenderDebugUi(DebugUI& ui) override;

private:
    void LoadAssets();
    bool LoadModelFromFile(const std::string& path, const glm::mat4& rootTransform);
    void ProcessAssimpNode(const aiScene* scene, const aiNode* node, const glm::mat4& parentTransform);
    MeshResource UploadMesh(const aiMesh* mesh, const glm::mat4& transform);
    void CreateFallbackMesh();

    std::vector<MeshResource> meshes_;
    std::vector<std::size_t> displayMeshIndices_;
    std::vector<glm::mat4> baseDisplayTransforms_;
    glm::vec3 ambientLight_{0.1f};
    glm::vec3 directionalLightDir_{-0.2f, -1.0f, -0.3f};
    glm::vec3 directionalLightColor_{1.0f};
    bool rotateDisplayVehicle_{false};
    float rotationSpeed_{15.0f};
    float accumulatedRotation_{0.0f};
    glm::mat4 baseDisplayTransform_{1.0f};
    bool baseTransformCaptured_{false};
    glm::mat4 projectionMatrix_{1.0f};
    glm::vec3 cameraPosition_{0.0f, 2.5f, 6.0f};
    glm::vec3 cameraTarget_{0.0f, 0.75f, 0.0f};
    glm::vec3 cameraUp_{0.0f, 1.0f, 0.0f};
    std::unique_ptr<Shader> skyboxShader_;
    std::unique_ptr<Skybox> skybox_;
};

} // namespace raceman

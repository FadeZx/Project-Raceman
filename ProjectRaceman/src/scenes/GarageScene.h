#pragma once

#include "Scene.h"
#include "MeshResource.h"

#include <memory>
#include <vector>

struct aiScene;
struct aiMesh;

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
    void ProcessAssimpNode(const aiScene* scene, const struct aiNode* node, const glm::mat4& parentTransform);
    MeshResource UploadMesh(const aiMesh* mesh, const glm::mat4& transform);

    std::vector<MeshResource> meshes_;
    glm::vec3 ambientLight_{0.1f};
    glm::vec3 directionalLightDir_{-0.2f, -1.0f, -0.3f};
    glm::vec3 directionalLightColor_{1.0f};
};

} // namespace raceman

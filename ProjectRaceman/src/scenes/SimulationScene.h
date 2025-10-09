#pragma once

#include "MeshResource.h"
#include "Scene.h"

#include "../physics/PhysicsLayer.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Skybox;
class Shader;

namespace raceman {

class SimulationScene : public Scene {
public:
    explicit SimulationScene(std::shared_ptr<Renderer> renderer);

    void SetPhysicsLayer(std::shared_ptr<PhysicsLayer> physics);

    void OnSceneActivated() override;
    void Update(float deltaTime) override;
    void Render(Renderer& renderer) override;
    void RenderDebugUi(DebugUI& ui) override;

private:
    std::shared_ptr<PhysicsLayer> physics_;
    std::unordered_map<std::string, MeshResource> cachedMeshes_;
    std::vector<RigidBodyState> visibleBodies_;
    bool drawWireframe_{false};
    float debugVehicleScale_{1.0f};
    glm::mat4 projectionMatrix_{1.0f};
    glm::vec3 cameraPosition_{0.0f, 6.0f, 14.0f};
    glm::vec3 cameraTarget_{0.0f, 0.5f, 0.0f};
    glm::vec3 cameraUp_{0.0f, 1.0f, 0.0f};
    std::unique_ptr<Shader> skyboxShader_;
    std::unique_ptr<Skybox> skybox_;

    MeshResource CreatePlaceholderVehicleMesh(const std::string& meshId);
};

} // namespace raceman

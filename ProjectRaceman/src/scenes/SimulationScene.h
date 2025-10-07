#pragma once

#include "MeshResource.h"
#include "Scene.h"

#include "physics/PhysicsLayer.h"

#include <unordered_map>
#include <vector>

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

    MeshResource CreatePlaceholderVehicleMesh(const std::string& meshId);
};

} // namespace raceman

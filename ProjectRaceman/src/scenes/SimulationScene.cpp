#include "SimulationScene.h"

#include "rendering/Renderer.h"

#include <glad/glad.h>

namespace raceman {

SimulationScene::SimulationScene(std::shared_ptr<Renderer> renderer)
    : Scene("Simulation", std::move(renderer)) {}

void SimulationScene::SetPhysicsLayer(std::shared_ptr<PhysicsLayer> physics) {
    physics_ = std::move(physics);
}

void SimulationScene::OnSceneActivated() {
    renderer_->SetupEnvironment("assets/environments/track_day.hdr");
    renderer_->CreateShadowMaps(4096);
}

void SimulationScene::Update(float) {
    if (!physics_) {
        return;
    }

    physics_->ForEachBody([this](const RigidBodyState& state) {
        if (cachedMeshes_.find(state.meshId) == cachedMeshes_.end()) {
            // Mesh streaming/loading would happen here. For now, assume the mesh exists and is uploaded elsewhere.
            cachedMeshes_[state.meshId] = MeshResource{};
        }
    });
}

void SimulationScene::Render(Renderer& renderer) {
    if (!physics_) {
        return;
    }

    physics_->ForEachBody([&](const RigidBodyState& state) {
        auto it = cachedMeshes_.find(state.meshId);
        if (it == cachedMeshes_.end()) {
            return;
        }

        MeshDrawCommand cmd{};
        cmd.vao = it->second.vao;
        cmd.indexCount = it->second.indexCount;
        cmd.modelMatrix = state.transform;
        cmd.materialId = state.meshId;
        renderer.SubmitMesh(cmd);
    });

    renderer.Flush();
}

void SimulationScene::RenderDebugUi(DebugUI& ui) {
    Scene::RenderDebugUi(ui);
}

} // namespace raceman

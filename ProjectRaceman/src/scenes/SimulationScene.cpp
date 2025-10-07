#include "SimulationScene.h"

#include "rendering/Renderer.h"
#include "ui/DebugUI.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <cmath>

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
    visibleBodies_.clear();

    if (!physics_) {
        return;
    }

    physics_->ForEachBody([this](const RigidBodyState& state) {
        visibleBodies_.push_back(state);
        if (cachedMeshes_.find(state.meshId) == cachedMeshes_.end()) {
            cachedMeshes_.emplace(state.meshId, CreatePlaceholderVehicleMesh(state.meshId));
        }
    });
}

void SimulationScene::Render(Renderer& renderer) {
    if (!physics_) {
        return;
    }

    if (drawWireframe_) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    for (const auto& state : visibleBodies_) {
        auto it = cachedMeshes_.find(state.meshId);
        if (it == cachedMeshes_.end()) {
            continue;
        }

        MeshDrawCommand cmd{};
        cmd.vao = it->second.vao;
        cmd.indexCount = it->second.indexCount;
        cmd.modelMatrix = state.transform * glm::scale(glm::mat4(1.0f), glm::vec3(debugVehicleScale_));
        cmd.materialId = state.meshId;
        renderer.SubmitMesh(cmd);
    }

    renderer.Flush();

    if (drawWireframe_) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

void SimulationScene::RenderDebugUi(DebugUI&) {
    if (ImGui::Begin("Simulation")) {
        ImGui::Checkbox("Wireframe", &drawWireframe_);
        ImGui::SliderFloat("Vehicle Scale", &debugVehicleScale_, 0.25f, 3.0f, "%.2fx");

        ImGui::Separator();
        ImGui::Text("Active Bodies: %zu", visibleBodies_.size());

        if (ImGui::BeginTable("Bodies", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Mesh");
            ImGui::TableSetupColumn("Position");
            ImGui::TableSetupColumn("Rotation Y (deg)");
            ImGui::TableSetupColumn("Scale");
            ImGui::TableHeadersRow();

            for (const auto& body : visibleBodies_) {
                const glm::vec3 position = glm::vec3(body.transform[3]);
                const float rotationY = glm::degrees(std::atan2(body.transform[0][2], body.transform[0][0]));

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(body.meshId.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%.2f, %.2f, %.2f", position.x, position.y, position.z);

                ImGui::TableNextColumn();
                ImGui::Text("%.1f", rotationY);

                ImGui::TableNextColumn();
                ImGui::Text("%.2f", debugVehicleScale_);
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

MeshResource SimulationScene::CreatePlaceholderVehicleMesh(const std::string& meshId) {
    (void)meshId;
    return CreateUnitCubeMesh();
}

} // namespace raceman

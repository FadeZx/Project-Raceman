#include "SimulationScene.h"

#include "../rendering/Renderer.h"
#include "../rendering/Skybox.h"
#include "../rendering/shader.h"
#include "../ui/DebugUI.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

std::vector<std::string> BuildRacetrackSkyboxFaces()
{
    const std::string basePath = "assets/skybox/racetrack/";
    return {basePath + "px.jpg", basePath + "nx.jpg", basePath + "py.jpg", basePath + "ny.jpg", basePath + "pz.jpg",
            basePath + "nz.jpg"};
}

} // namespace

namespace raceman {

SimulationScene::SimulationScene(std::shared_ptr<Renderer> renderer)
    : Scene("Simulation", std::move(renderer)) {}

void SimulationScene::SetPhysicsLayer(std::shared_ptr<PhysicsLayer> physics) {
    physics_ = std::move(physics);
}

void SimulationScene::OnSceneActivated() {
    if (!skyboxShader_) {
        skyboxShader_ = std::make_unique<Shader>("src/shaders/skybox/skybox.vs", "src/shaders/skybox/skybox.fs");
        skyboxShader_->use();
        skyboxShader_->setInt("skybox", 0);
    }

    if (!skybox_) {
        skybox_ = std::make_unique<Skybox>(BuildRacetrackSkyboxFaces(), skyboxShader_->getID());
    }

    renderer_->SetupEnvironment("assets/environments/track_day.hdr");
    renderer_->CreateShadowMaps(4096);

    const auto& rendererConfig = renderer_->GetConfig();
    projectionMatrix_ = glm::perspective(glm::radians(60.0f),
                                         static_cast<float>(rendererConfig.width) / static_cast<float>(rendererConfig.height),
                                         0.1f,
                                         150.0f);
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

    const glm::mat4 view = glm::lookAt(cameraPosition_, cameraTarget_, cameraUp_);

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

    if (skybox_) {
        skybox_->draw(view, projectionMatrix_);
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

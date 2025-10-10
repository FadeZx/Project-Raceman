#include "SimulationScene.h"

#include "../rendering/Renderer.h"
#include "../rendering/Skybox.h"
#include "../rendering/shader.h"


#include "../ui/DebugUI.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <filesystem>


#include <cmath>
#include <string>
#include <vector>




namespace raceman {

SimulationScene::SimulationScene(std::shared_ptr<Renderer> renderer)
    : Scene("Simulation", std::move(renderer)) {}

void SimulationScene::SetPhysicsLayer(std::shared_ptr<PhysicsLayer> physics) {
    physics_ = std::move(physics);
}


void SimulationScene::Init() {
    LoadSkyboxConfig();
    EnsureSkyboxReady();
}

void SimulationScene::Update(float) {
   
}

void SimulationScene::Render(Renderer& renderer) {
    if (skybox_) {
        glm::mat4 view = renderer.GetView();
        glm::mat4 proj = renderer.GetProj();
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
        skybox_->draw(viewNoTranslation, proj);
    }
}

void SimulationScene::RenderDebugUi(DebugUI&) {
  
}

void SimulationScene::Save() {
    // Persist current skybox configuration
    SaveSkyboxConfig();
}


void SimulationScene::Clean() {}


    void SimulationScene::EnsureSkyboxReady() {
        namespace fs = std::filesystem;
        bool anyEmpty = false;
        for (const auto& f : skyboxFaces_) { if (f.empty()) { anyEmpty = true; break; } }
        if (anyEmpty) {
            fs::path base("assets/skybox/racetrack");
            skyboxFaces_[0] = (base / "px.jpg").string();
            skyboxFaces_[1] = (base / "nx.jpg").string();
            skyboxFaces_[2] = (base / "py.jpg").string();
            skyboxFaces_[3] = (base / "ny.jpg").string();
            skyboxFaces_[4] = (base / "pz.jpg").string();
            skyboxFaces_[5] = (base / "nz.jpg").string();
        }

        if (!skyboxShader_) {
            skyboxShader_ = std::make_unique<Shader>("src/shaders/skybox/skybox.vs", "src/shaders/skybox/skybox.fs");
        }
        std::vector<std::string> facesVec(skyboxFaces_.begin(), skyboxFaces_.end());
        skybox_ = std::make_unique<Skybox>(facesVec, skyboxShader_->getID());
    }

    void SimulationScene::LoadSkyboxConfig() {
        namespace fs = std::filesystem;
        fs::path path("config/scenes/SimulationScene.txt");
        if (!fs::exists(path)) return;
        std::ifstream in(path);
        if (!in.good()) return;
        for (int i = 0; i < 6 && in; ++i) {
            std::getline(in, skyboxFaces_[i]);
        }
    }

    void SimulationScene::SaveSkyboxConfig() const {
        namespace fs = std::filesystem;
        fs::create_directories("config/scenes");
        std::ofstream out("config/scenes/SimulationScene.txt", std::ios::trunc);
        if (!out.good()) return;
        for (int i = 0; i < 6; ++i) out << skyboxFaces_[i] << "\n";
    }

    void SimulationScene::SetSkyboxFaces(const std::array<std::string,6>& faces) {
        skyboxFaces_ = faces;
        SaveSkyboxConfig();
        EnsureSkyboxReady();
    }
} // namespace raceman

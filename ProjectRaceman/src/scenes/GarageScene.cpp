#include "GarageScene.h"

#include "../rendering/Renderer.h"
#include "../rendering/Skybox.h"
#include "../rendering/shader.h"

#include "../ui/DebugUI.h"




#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <filesystem>


#include <cmath>

#include <string>
#include <vector>
#include <array>
#include <iterator>

#include <iostream>


namespace raceman {

    GarageScene::GarageScene(std::shared_ptr<Renderer> renderer)
        : Scene("Garage", std::move(renderer)) {
    }

    void GarageScene::Init() {
        // Load persisted faces and ensure skybox is created
        LoadSkyboxConfig();
        EnsureSkyboxReady();
        BuildSkyboxIfNeeded();
    }

    void GarageScene::Update(float deltaTime) {

    }

    void GarageScene::Render(Renderer& renderer) {
        BuildSkyboxIfNeeded();
        // Draw skybox using current camera (remove translation so it surrounds the camera)
        if (skybox_) {
            glm::mat4 view = renderer.GetView();
            glm::mat4 proj = renderer.GetProj();
            glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
            skybox_->draw(viewNoTranslation, proj);
        }
    }

    void GarageScene::EnsureSkyboxReady() {
        namespace fs = std::filesystem;
        // Default faces if none provided
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
            skyboxDirty_ = true;
        }
    }

    void GarageScene::RenderDebugUi(DebugUI&) {
        // No per-frame skybox rebuild here.
    }

    void GarageScene::Save() {
        // Persist current skybox configuration
        SaveSkyboxConfig();
    }


    void GarageScene::LoadSkyboxConfig() {
        namespace fs = std::filesystem;
        fs::path path("config/scenes/GarageScene.txt");
        if (!fs::exists(path)) return;
        std::ifstream in(path);
        if (!in.good()) return;
        for (int i = 0; i < 6 && in; ++i) {
            std::getline(in, skyboxFaces_[i]);
        }
    }

    void GarageScene::SaveSkyboxConfig() const {
        namespace fs = std::filesystem;
        fs::create_directories("config/scenes");
        std::ofstream out("config/scenes/GarageScene.txt", std::ios::trunc);
        if (!out.good()) return;
        for (int i = 0; i < 6; ++i) out << skyboxFaces_[i] << "\n";
    }

    void GarageScene::SetSkyboxFaces(const std::array<std::string,6>& faces) {
        skyboxFaces_ = faces;
        skyboxDirty_ = true;
        SaveSkyboxConfig();
    }

    void GarageScene::BuildSkyboxIfNeeded() {
        if (!skyboxShader_) {
            skyboxShader_ = std::make_unique<Shader>("src/shaders/skybox/skybox.vs", "src/shaders/skybox/skybox.fs");
        }
        if (skyboxDirty_) {
            std::vector<std::string> facesVec(skyboxFaces_.begin(), skyboxFaces_.end());
            skybox_ = std::make_unique<Skybox>(facesVec, skyboxShader_->getID());
            skyboxDirty_ = false;
        }
    }
}




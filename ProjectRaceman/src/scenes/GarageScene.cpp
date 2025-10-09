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

#include <iostream>


namespace raceman {

    GarageScene::GarageScene(std::shared_ptr<Renderer> renderer)
        : Scene("Garage", std::move(renderer)) {
    }

    void GarageScene::Init() {
        // Load persisted faces and ensure skybox is created
        LoadSkyboxConfig();
        EnsureSkyboxReady();
    }

    void GarageScene::Update(float deltaTime) {

    }

    void GarageScene::Render(Renderer& renderer) {
        // Draw skybox if available (basic camera for now)
        if (skybox_) {
            const auto& cfg = renderer.GetConfig();
            float aspect = cfg.height != 0 ? (float)cfg.width / (float)cfg.height : 16.0f/9.0f;
            glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                         glm::vec3(0.0f, 0.0f, 0.0f),
                                         glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
            skybox_->draw(view, proj);
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
        }

    }

    void GarageScene::RenderDebugUi(DebugUI&) {

       

        if (!skyboxShader_) {
            skyboxShader_ = std::make_unique<Shader>("src/shaders/skybox/skybox.vs", "src/shaders/skybox/skybox.fs");
        }
        std::vector<std::string> facesVec(skyboxFaces_.begin(), skyboxFaces_.end());
        skybox_ = std::make_unique<Skybox>(facesVec, skyboxShader_->getID());
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
        SaveSkyboxConfig();
        EnsureSkyboxReady();
    }
}




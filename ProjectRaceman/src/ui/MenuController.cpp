#include "MenuController.h"

#include "../ui/DebugUI.h"
#include "../rendering/Renderer.h"
#include "../scenes/Scene.h"

#include <imgui/imgui.h>
#include <filesystem>
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

namespace raceman {

MenuController::MenuController() { LoadState(); }
MenuController::~MenuController() { SaveState(); }

void MenuController::Render(DebugUI& ui,
                            Renderer& renderer,
                            const std::vector<std::shared_ptr<Scene>>& scenes,
                            std::size_t activeScene,
                            const std::function<void(std::size_t)>& switchScene,
                            const std::function<void(std::size_t, const SkyboxFaces&)>& onSkyboxChosen,
                            bool vsyncEnabled,
                            const std::function<void(bool)>& setVSync,
                            const std::function<void()>& onAddMeshPlane) {
    (void)ui; // We call ui-specific panels below
    (void)renderer;

    // Draw top-level compact menu (now includes Scenes list and Add menu)
    RenderMainMenu(scenes, activeScene, switchScene, onAddMeshPlane);

    // Rendering panel: reuse existing DebugUI metrics by just showing a small window that calls them
    if (showRendering_) {
        ImGui::Begin("Rendering");
        // Note: Application already calls DebugUI::RenderAppMetrics; to avoid duplication,
        // we just mirror a few controls, or we could display a note.
        auto& settings = renderer.GetSettings();
        ImGui::TextUnformatted("Renderer Settings");
        ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 5.0f, "%.2f");
        ImGui::SliderFloat("Gamma", &settings.gamma, 1.0f, 3.0f, "%.2f");
        ImGui::ColorEdit3("Clear Color", &settings.clearColor.x);
        ImGui::Checkbox("Enable Shadows", &settings.enableShadows);
        ImGui::Checkbox("Show Env Debug", &settings.showEnvironmentDebugView);

        bool vs = vsyncEnabled;
        if (ImGui::Checkbox("VSync", &vs)) {
            if (setVSync) setVSync(vs);
        }
        ImGui::End();
    }

    // Scenes panel is handled by DebugUI::RenderSceneSwitcher; avoid duplication here.

    if (showSkybox_) {
        RenderSkyboxPanel(activeScene, onSkyboxChosen);
    }
}

void MenuController::RenderMainMenu(const std::vector<std::shared_ptr<Scene>>& scenes,
                                    std::size_t activeScene,
                                    const std::function<void(std::size_t)>& switchScene,
                                    const std::function<void()>& onAddMeshPlane) {
    if (ImGui::Begin("Menu")) {
        bool prevR = showRendering_, prevB = showSkybox_;
        ImGui::Checkbox("Rendering", &showRendering_);
        ImGui::Checkbox("Skybox", &showSkybox_);
        if (prevR != showRendering_ || prevB != showSkybox_) {
            SaveState();
        }

        ImGui::Separator();
        // Add button + popup: Mesh -> Plane
        if (ImGui::Button("Add")) {
            ImGui::OpenPopup("add_popup");
        }
        if (ImGui::BeginPopup("add_popup")) {
            if (ImGui::BeginMenu("Mesh")) {
                if (ImGui::MenuItem("Plane")) {
                    if (onAddMeshPlane) onAddMeshPlane();
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Scenes");
        for (std::size_t i = 0; i < scenes.size(); ++i) {
            const bool isActive = (i == activeScene);
            if (ImGui::Selectable(scenes[i]->GetName().c_str(), isActive)) {
                if (switchScene) switchScene(i);
            }
        }
    }
    ImGui::End();
}

void MenuController::RenderScenesPanel(const std::vector<std::shared_ptr<Scene>>& scenes,
                                       std::size_t activeScene,
                                       const std::function<void(std::size_t)>& switchScene) {
    if (ImGui::Begin("Scenes")) {
        for (std::size_t i = 0; i < scenes.size(); ++i) {
            bool isActive = (i == activeScene);
            if (ImGui::Selectable(scenes[i]->GetName().c_str(), isActive)) {
                switchScene(i);
            }
        }
    }
    ImGui::End();
}

bool MenuController::LooksLikeFaceSet(const std::vector<std::string>& names) {
    // Accept either px/nx/py/ny/pz/nz OR posx/negx/posy/negy/posz/negz
    const char* a[6] = {"px.jpg","nx.jpg","py.jpg","ny.jpg","pz.jpg","nz.jpg"};
    const char* b[6] = {"posx.jpg","negx.jpg","posy.jpg","negy.jpg","posz.jpg","negz.jpg"};
    int countA = 0, countB = 0;
    for (const auto& n : names) {
        for (auto s : a) if (n == s) ++countA;
        for (auto s : b) if (n == s) ++countB;
    }
    return (countA == 6) || (countB == 6);
}

SkyboxFaces MenuController::BuildFacesFromFolder(const std::string& folder) {
    // Prefer px/nx... else posx/negx...
    SkyboxFaces faces{};
    std::array<const char*,6> a = {"px.jpg","nx.jpg","py.jpg","ny.jpg","pz.jpg","nz.jpg"};
    std::array<const char*,6> b = {"posx.jpg","negx.jpg","posy.jpg","negy.jpg","posz.jpg","negz.jpg"};

    // Read names in folder into a set for quick lookup
    std::vector<std::string> found;
    try {
        for (auto& entry : fs::directory_iterator(folder)) {
            if (entry.is_regular_file()) {
                auto name = entry.path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name.size() >= 4) {
                    if (name.find(".jpg") != std::string::npos || name.find(".png") != std::string::npos) {
                        found.push_back(name);
                    }
                }
            }
        }
    } catch (...) {
        // ignore FS errors
    }

    auto has = [&](const std::string& needle){ return std::find(found.begin(), found.end(), needle) != found.end(); };

    bool useA = true;
    for (auto s : a) if (!has(s)) { useA = false; break; }
    const auto& order = useA ? a : b;

    for (size_t i = 0; i < 6; ++i) {
        faces[i] = (fs::path(folder) / order[i]).string();
    }
    return faces;
}

void MenuController::RefreshSkyboxSets() {
    skyboxFolders_.clear();
    const fs::path base = fs::path("assets") / "skybox";
    if (!fs::exists(base)) return;

    // Collect folders that contain a valid set of 6 faces
    try {
        for (auto& entry : fs::recursive_directory_iterator(base)) {
            if (entry.is_directory()) {
                std::vector<std::string> names;
                for (auto& sub : fs::directory_iterator(entry.path())) {
                    if (sub.is_regular_file()) {
                        auto n = sub.path().filename().string();
                        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                        names.push_back(n);
                    }
                }
                if (LooksLikeFaceSet(names)) {
                    skyboxFolders_.push_back(entry.path().string());
                }
            }
        }
    } catch (...) {
        // ignore FS errors
    }
    std::sort(skyboxFolders_.begin(), skyboxFolders_.end());
}

void MenuController::RenderSkyboxPanel(std::size_t activeScene,
                                       const std::function<void(std::size_t, const SkyboxFaces&)>& onSkyboxChosen) {
    if (skyboxFolders_.empty()) {
        RefreshSkyboxSets();
    }

    if (ImGui::Begin("Skybox")) {
        ImGui::TextUnformatted("Pick a skybox folder (auto-detects px/nx/py/ny/pz/nz or posx/negx/...)");
        if (ImGui::BeginListBox("##skybox-folders", ImVec2(0, 200))) {
            for (int i = 0; i < static_cast<int>(skyboxFolders_.size()); ++i) {
                bool selected = (i == selectedFolder_);
                if (ImGui::Selectable(skyboxFolders_[i].c_str(), selected)) {
                    selectedFolder_ = i;
                    // Precompute current faces for this scene
                    if (selectedFolder_ >= 0) {
                        perSceneFaces_[activeScene] = BuildFacesFromFolder(skyboxFolders_[selectedFolder_]);
                    }
                }
            }
            ImGui::EndListBox();
        }

        auto it = perSceneFaces_.find(activeScene);
        if (it != perSceneFaces_.end()) {
            const auto& faces = it->second;
            const char* labels[6] = {"+X","-X","+Y","-Y","+Z","-Z"};
            for (int i = 0; i < 6; ++i) {
                ImGui::InputText(labels[i], const_cast<char*>(faces[i].c_str()), faces[i].size()+1, ImGuiInputTextFlags_ReadOnly);
            }
            if (onSkyboxChosen) {
                if (ImGui::Button("Apply To Scene")) {
                    onSkyboxChosen(activeScene, faces);
                }
            } else {
                ImGui::TextDisabled("Apply callback not wired; selection is stored only.");
            }
        } else {
            ImGui::TextDisabled("No skybox selected for this scene.");
        }
    }
    ImGui::End();
}

void MenuController::LoadState() {
    std::ifstream in(stateFile_);
    if (!in.good()) return;
    std::string line;
    while (std::getline(in, line)) {
        // very simple key=value
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        auto key = line.substr(0, pos);
        auto val = line.substr(pos + 1);
        if (key == "showRendering") showRendering_ = (val == "1");
        else if (key == "showScenes") showScenes_ = (val == "1");
        else if (key == "showSkybox") showSkybox_ = (val == "1");
        else if (key == "selectedFolder") selectedFolder_ = std::stoi(val);
    }
}

void MenuController::SaveState() const {
    // ensure directory exists
    try {
        fs::create_directories(fs::path(stateFile_).parent_path());
    } catch (...) {}
    std::ofstream out(stateFile_, std::ios::trunc);
    if (!out.good()) return;
    out << "showRendering=" << (showRendering_ ? "1" : "0") << "\n";
    out << "showScenes=" << (showScenes_ ? "1" : "0") << "\n";
    out << "showSkybox=" << (showSkybox_ ? "1" : "0") << "\n";
    out << "selectedFolder=" << selectedFolder_ << "\n";
}

} // namespace raceman
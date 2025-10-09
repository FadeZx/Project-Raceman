#pragma once

#include <memory>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <unordered_map>

namespace raceman {

class DebugUI;
class Renderer;
class Scene;

using SkyboxFaces = std::array<std::string, 6>; // +X, -X, +Y, -Y, +Z, -Z

class MenuController {
public:
    MenuController();
    ~MenuController();

    // Render the main menu and panels. switchScene is invoked when a new scene is selected.
    // onSkyboxChosen is invoked when user clicks Apply in the Skybox panel (you can wire it later).
    void Render(DebugUI& ui,
                Renderer& renderer,
                const std::vector<std::shared_ptr<Scene>>& scenes,
                std::size_t activeScene,
                const std::function<void(std::size_t)>& switchScene,
                const std::function<void(std::size_t, const SkyboxFaces&)>& onSkyboxChosen = {});

private:
    // Panels
    void RenderMainMenu();
    void RenderRenderingPanel(DebugUI& ui, Renderer& renderer, float deltaTime);
    void RenderScenesPanel(const std::vector<std::shared_ptr<Scene>>& scenes,
                           std::size_t activeScene,
                           const std::function<void(std::size_t)>& switchScene);
    void RenderSkyboxPanel(std::size_t activeScene,
                           const std::function<void(std::size_t, const SkyboxFaces&)>& onSkyboxChosen);

    // Helpers
    void RefreshSkyboxSets();
    static bool LooksLikeFaceSet(const std::vector<std::string>& names);
    static SkyboxFaces BuildFacesFromFolder(const std::string& folder);

private:
    // Persistence
    void LoadState();
    void SaveState() const;

    // Toggles
    bool showRendering_{false};
    bool showScenes_{false};
    bool showSkybox_{false};

    // Cached skybox folders under assets/skybox
    std::vector<std::string> skyboxFolders_;

    // Per-scene chosen faces (by activeScene index)
    std::unordered_map<std::size_t, SkyboxFaces> perSceneFaces_;
    // Selected folder index for UI
    int selectedFolder_{-1};

    // State file
    std::string stateFile_{"config/menu.json"};

    // Last frame dt for metrics
    float lastDt_{0.0f};
};

} // namespace raceman
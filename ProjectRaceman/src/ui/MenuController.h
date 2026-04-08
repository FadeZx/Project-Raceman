#pragma once

#include <string>
#include <vector>
#include <array>
#include <functional>

namespace raceman {

class Renderer;
class Console;

using SkyboxFaces = std::array<std::string, 6>; // +X, -X, +Y, -Y, +Z, -Z

struct EditorProjectMenu {
    std::string projectName;
    std::string currentScenePath;
    std::vector<std::string> sceneAssets;
    std::function<void(const std::string&)> onNewScene;
    std::function<void()> onSaveScene;
    std::function<void(const std::string&)> onOpenScene;
    std::function<void()> onSaveProject;
};

class MenuController {
public:
    MenuController();
    ~MenuController();

    // Render the editor menu and supporting panels.
    void Render(Renderer& renderer,
                bool vsyncEnabled = true,
                const std::function<void(bool)>& setVSync = std::function<void(bool)>(),
                const std::function<void()>& onAddMeshPlane = std::function<void()>(),
                Console* console = nullptr,
                EditorProjectMenu projectMenu = {},
                const std::function<void(const SkyboxFaces&)>& onSkyboxChosen = {});

private:
    // Panels
    void RenderMainMenu(const std::function<void()>& onAddMeshPlane,
                        const EditorProjectMenu& projectMenu);

    void RenderSkyboxPanel(const std::function<void(const SkyboxFaces&)>& onSkyboxChosen);

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
    bool showSkybox_{false};
    bool showConsole_{false};

    // Cached skybox folders under assets/skybox
    std::vector<std::string> skyboxFolders_;

    SkyboxFaces selectedSkyboxFaces_{};
    bool hasSelectedSkyboxFaces_{false};
    // Selected folder index for UI
    int selectedFolder_{-1};

    // State file
    std::string stateFile_{"config/menu.json"};

    // Last frame dt for metrics
    float lastDt_{0.0f};
    char newSceneNameBuffer_[128]{"NewScene"};
    bool focusNewSceneName_{false};
    bool openNewScenePopup_{false};
};

} // namespace raceman

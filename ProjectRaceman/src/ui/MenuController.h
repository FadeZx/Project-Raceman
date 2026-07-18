#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <array>

#include "../rendering/Renderer.h"

namespace raceman {

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
    std::function<void(const std::string&)> onBuildProject;
    std::function<void()> onOpenProjectLauncher;
    std::function<void()> renderInputSettings;
    std::function<void()> renderProjectSettings;
    std::function<void()> renderTagsAndLayersSettings;
    std::function<void()> onGraphicsSettingsChanged;
};

class MenuController {
public:
    MenuController();
    ~MenuController();

    // Render the editor menu and supporting panels.
    void Render(Renderer& renderer,
                bool vsyncEnabled = true,
                const std::function<void(bool)>& setVSync = std::function<void(bool)>(),
                bool profilerVisible = true,
                const std::function<void(bool)>& setProfilerVisible = std::function<void(bool)>(),
                const std::function<void()>& onAddMeshPlane = std::function<void()>(),
                Console* console = nullptr,
                EditorProjectMenu projectMenu = {},
                const std::function<void(const SkyboxFaces&)>& onSkyboxChosen = {},
                bool* frustumCullingEnabled = nullptr,
                bool* physicsCullingEnabled = nullptr,
                float* sceneCameraNearClip = nullptr,
                float* sceneCameraFarClip = nullptr);

    bool IsProjectSettingsShortcutTarget() const { return projectSettingsShortcutTarget_; }
    void SetProjectSkyboxFaces(const SkyboxFaces& faces);
    void UndoGraphicsSettings(Renderer& renderer);
    void RedoGraphicsSettings(Renderer& renderer);

private:
    // Panels
    void RenderMainMenu(const std::function<void()>& onAddMeshPlane,
                        const EditorProjectMenu& projectMenu,
                        bool profilerVisible,
                        const std::function<void(bool)>& setProfilerVisible);

    void RenderSkyboxPanel(const std::function<void(const SkyboxFaces&)>& onSkyboxChosen);

    // Helpers
    static bool TryBuildFacesFromFolder(const std::string& folder, SkyboxFaces& faces, std::string& error);

private:
    // Persistence
    void LoadState();
    void SaveState() const;

    // Toggles
    bool showProjectSettings_{false};
    bool showSkybox_{false};
    bool showConsole_{false};
    int selectedProjectSettingsTab_{0};
    bool restoreProjectSettingsTab_{true};
    bool projectSettingsShortcutTarget_{false};

    RendererSettings graphicsEditStart_{};
    bool graphicsEditActive_{false};
    std::vector<RendererSettings> graphicsUndoStack_;
    std::vector<RendererSettings> graphicsRedoStack_;
    std::function<void()> graphicsChangedCallback_;

    SkyboxFaces selectedSkyboxFaces_{};
    bool hasSelectedSkyboxFaces_{false};
    bool selectedSkyboxSaved_{false};
    std::string selectedSkyboxFolder_;
    std::string skyboxSelectionError_;

    // Async folder picker for Build...
    struct FolderPickerState {
        std::atomic<bool> isDone{false};
        std::mutex resultMutex;
        std::string result;
    };
    std::unique_ptr<std::thread> folderPickerThread_;
    std::shared_ptr<FolderPickerState> folderPickerState_;
    std::function<void(const std::string&)> pendingBuildCallback_;

    // State file
    std::string stateFile_{"config/menu.json"};

    // Last frame dt for metrics
    float lastDt_{0.0f};
    char newSceneNameBuffer_[128]{"NewScene"};
    bool focusNewSceneName_{false};
    bool openNewScenePopup_{false};
};

} // namespace raceman

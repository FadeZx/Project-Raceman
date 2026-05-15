#include "MenuController.h"

#include "../rendering/Renderer.h"
#include "Console.h"

#include <imgui/imgui.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace fs = std::filesystem;

namespace raceman {

namespace {
fs::path ProjectRootPath() {
    if (fs::exists("ProjectRaceman/src") && fs::is_directory("ProjectRaceman/src")) {
        return fs::absolute("ProjectRaceman/Project").lexically_normal();
    }
    if (fs::exists("src") && fs::is_directory("src")) {
        return fs::absolute("Project").lexically_normal();
    }
    return fs::absolute("Project").lexically_normal();
}

fs::path EngineRootPath() {
    if (fs::exists("ProjectRaceman/src") && fs::is_directory("ProjectRaceman/src")) {
        return fs::absolute("ProjectRaceman").lexically_normal();
    }
    if (fs::exists("src") && fs::is_directory("src")) {
        return fs::absolute(".").lexically_normal();
    }
    return fs::absolute(".").lexically_normal();
}

std::string SceneDisplayName(const std::string& scenePath) {
    std::string filename = fs::path(scenePath).filename().string();
    const std::string suffix = ".scene.json";
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower.size() >= suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
        filename.resize(filename.size() - suffix.size());
        filename += ".scene";
    }
    return filename;
}

std::string PickBuildOutputFolder() {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitialize = SUCCEEDED(coInit);

    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return {};
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(L"Choose standalone build output folder");

    std::string result;
    hr = dialog->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        hr = dialog->GetResult(&item);
        if (SUCCEEDED(hr) && item != nullptr) {
            PWSTR widePath = nullptr;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &widePath);
            if (SUCCEEDED(hr) && widePath != nullptr) {
                const int size = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, nullptr, 0, nullptr, nullptr);
                if (size > 0) {
                    result.resize(static_cast<size_t>(size - 1));
                    WideCharToMultiByte(CP_UTF8, 0, widePath, -1, result.data(), size, nullptr, nullptr);
                }
                CoTaskMemFree(widePath);
            }
            item->Release();
        }
    }

    dialog->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return result;
#else
    return {};
#endif
}
} // namespace

MenuController::MenuController() { LoadState(); }
MenuController::~MenuController() {
    if (folderPickerThread_ && folderPickerThread_->joinable()) {
        folderPickerThread_->join();
    }
    SaveState();
}

void MenuController::Render(Renderer& renderer,
                            bool vsyncEnabled,
                            const std::function<void(bool)>& setVSync,
                            bool profilerVisible,
                            const std::function<void(bool)>& setProfilerVisible,
                            const std::function<void()>& onAddMeshPlane,
                            Console* console,
                            EditorProjectMenu projectMenu,
                            const std::function<void(const SkyboxFaces&)>& onSkyboxChosen,
                            bool* frustumCullingEnabled,
                            bool* physicsCullingEnabled,
                            float* sceneCameraNearClip,
                            float* sceneCameraFarClip) {

    // Tick async folder picker — fires onBuildProject once user picks a folder
    if (folderPickerState_ && folderPickerState_->isDone.load()) {
        if (folderPickerThread_ && folderPickerThread_->joinable()) {
            folderPickerThread_->join();
        }
        std::string folder;
        {
            std::lock_guard<std::mutex> lock(folderPickerState_->resultMutex);
            folder = folderPickerState_->result;
        }
        if (!folder.empty() && pendingBuildCallback_) {
            pendingBuildCallback_(folder);
        }
        folderPickerThread_.reset();
        folderPickerState_.reset();
        pendingBuildCallback_ = nullptr;
    }

    RenderMainMenu(onAddMeshPlane, projectMenu, profilerVisible, setProfilerVisible);

    if (showProjectSettings_) {
        if (ImGui::Begin("Project Settings", &showProjectSettings_, ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::BeginTabBar("GlobalProjectSettingsTabs")) {
                if (ImGui::BeginTabItem("Rendering")) {
                    auto& settings = renderer.GetSettings();
                    ImGui::ColorEdit3("Clear Color", &settings.clearColor.x);
                    ImGui::ColorEdit3("Ambient Light", &settings.ambientColor.x);

                    bool vs = vsyncEnabled;
                    if (ImGui::Checkbox("VSync", &vs)) {
                        if (setVSync) {
                            setVSync(vs);
                        }
                    }

                    ImGui::Separator();
                    ImGui::TextUnformatted("Optimizations");
                    if (frustumCullingEnabled) {
                        ImGui::Checkbox("Frustum Culling", frustumCullingEnabled);
                    }
                    ImGui::Checkbox("Draw Call Sorting", &settings.enableDrawCallSorting);

                    ImGui::Separator();
                    ImGui::TextUnformatted("Scene Camera");
                    if (sceneCameraNearClip && sceneCameraFarClip) {
                        float nearClip = *sceneCameraNearClip;
                        if (ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.001f, 100000.0f)) {
                            *sceneCameraNearClip = (std::max)(0.001f, nearClip);
                            *sceneCameraFarClip = (std::max)(*sceneCameraNearClip + 0.001f, *sceneCameraFarClip);
                        }

                        float farClip = *sceneCameraFarClip;
                        if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, 0.002f, 1000000.0f)) {
                            *sceneCameraFarClip = (std::max)(*sceneCameraNearClip + 0.001f, farClip);
                        }
                    } else {
                        ImGui::TextDisabled("Scene camera clipping is unavailable.");
                    }

                    ImGui::Separator();
                    if (showSkybox_) {
                        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                    }
                    if (ImGui::CollapsingHeader("Skybox")) {
                        if (skyboxFolders_.empty()) {
                            RefreshSkyboxSets();
                        }

                        ImGui::TextUnformatted("Pick a skybox folder (auto-detects px/nx/py/ny/pz/nz or posx/negx/...)");
                        if (ImGui::BeginListBox("##skybox-folders", ImVec2(0, 200))) {
                            for (int i = 0; i < static_cast<int>(skyboxFolders_.size()); ++i) {
                                bool selected = (i == selectedFolder_);
                                if (ImGui::Selectable(skyboxFolders_[i].c_str(), selected)) {
                                    selectedFolder_ = i;
                                    if (selectedFolder_ >= 0) {
                                        selectedSkyboxFaces_ = BuildFacesFromFolder(skyboxFolders_[selectedFolder_]);
                                        hasSelectedSkyboxFaces_ = true;
                                    }
                                }
                            }
                            ImGui::EndListBox();
                        }

                        if (hasSelectedSkyboxFaces_) {
                            const auto& faces = selectedSkyboxFaces_;
                            const char* labels[6] = {"+X","-X","+Y","-Y","+Z","-Z"};
                            for (int i = 0; i < 6; ++i) {
                                std::vector<char> buf(faces[i].begin(), faces[i].end());
                                buf.push_back('\0');
                                ImGui::InputText(labels[i], buf.data(), buf.size(), ImGuiInputTextFlags_ReadOnly);
                            }
                            if (onSkyboxChosen) {
                                if (ImGui::Button("Apply Skybox")) {
                                    onSkyboxChosen(faces);
                                }
                            } else {
                                ImGui::TextDisabled("Skybox selection is stored only.");
                            }
                        } else {
                            ImGui::TextDisabled("No skybox selected.");
                        }
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Physics")) {
                    if (physicsCullingEnabled) {
                        ImGui::Checkbox("Physics Body Culling", physicsCullingEnabled);
                    } else {
                        ImGui::TextDisabled("Physics settings are unavailable.");
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Tags & Layers")) {
                    if (projectMenu.renderTagsAndLayersSettings) {
                        projectMenu.renderTagsAndLayersSettings();
                    } else {
                        ImGui::TextDisabled("Tag and layer settings are unavailable.");
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Input")) {
                    if (projectMenu.renderInputSettings) {
                        projectMenu.renderInputSettings();
                    } else {
                        ImGui::TextDisabled("Input settings are unavailable.");
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        if (!showProjectSettings_) {
            SaveState();
        }
    }

    // Console is hosted inside the editor Browser tab window.
    (void)console;
}

void MenuController::RenderMainMenu(const std::function<void()>& onAddMeshPlane,
                                    const EditorProjectMenu& projectMenu,
                                    bool profilerVisible,
                                    const std::function<void(bool)>& setProfilerVisible) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene...")) {
                std::snprintf(newSceneNameBuffer_, sizeof(newSceneNameBuffer_), "%s", "NewScene");
                focusNewSceneName_ = true;
                openNewScenePopup_ = true;
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S") && projectMenu.onSaveScene) {
                projectMenu.onSaveScene();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Open Scene")) {
                if (projectMenu.sceneAssets.empty()) {
                    ImGui::TextDisabled("No scene assets found.");
                } else {
                    for (const std::string& scenePath : projectMenu.sceneAssets) {
                        const bool selected = (scenePath == projectMenu.currentScenePath);
                        const std::string label = SceneDisplayName(scenePath) + "##" + scenePath;
                        if (ImGui::MenuItem(label.c_str(), nullptr, selected) && projectMenu.onOpenScene) {
                            projectMenu.onOpenScene(scenePath);
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Project") && projectMenu.onSaveProject) {
                projectMenu.onSaveProject();
            }
            if (ImGui::MenuItem("Open Project...") && projectMenu.onOpenProjectLauncher) {
                projectMenu.onOpenProjectLauncher();
            }
            const bool pickerRunning = folderPickerState_ && !folderPickerState_->isDone.load();
            if (ImGui::MenuItem("Build...", nullptr, false, !pickerRunning) && projectMenu.onBuildProject) {
                pendingBuildCallback_ = projectMenu.onBuildProject;
                auto state = std::make_shared<FolderPickerState>();
                folderPickerState_ = state;
                folderPickerThread_ = std::make_unique<std::thread>([state]() {
                    const std::string folder = PickBuildOutputFolder();
                    std::lock_guard<std::mutex> lock(state->resultMutex);
                    state->result = folder;
                    state->isDone.store(true);
                });
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
            ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
            ImGui::EndMenu();
        }

        (void)onAddMeshPlane;

        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Project Settings...", nullptr, showProjectSettings_)) {
                showProjectSettings_ = !showProjectSettings_;
                SaveState();
            }
            bool showProfiler = profilerVisible;
            if (ImGui::MenuItem("Profiler", nullptr, showProfiler) && setProfilerVisible) {
                setProfilerVisible(!showProfiler);
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (openNewScenePopup_) {
        ImGui::OpenPopup("New Scene");
        openNewScenePopup_ = false;
    }

    if (ImGui::BeginPopupModal("New Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Scene name");
        if (focusNewSceneName_) {
            ImGui::SetKeyboardFocusHere();
            focusNewSceneName_ = false;
        }
        const bool enterPressed = ImGui::InputText("##newSceneName", newSceneNameBuffer_, sizeof(newSceneNameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if ((enterPressed || ImGui::Button("Create")) && projectMenu.onNewScene) {
            projectMenu.onNewScene(newSceneNameBuffer_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
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

    auto scanDir = [&](const fs::path& base) {
        if (!fs::exists(base)) return;
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
        } catch (...) {}
    };

    // Engine-bundled skyboxes first, then project skyboxes.
    scanDir(EngineRootPath() / "editor-assets" / "skybox");
    scanDir(ProjectRootPath() / "assets" / "skybox");

    std::sort(skyboxFolders_.begin(), skyboxFolders_.end());
    if (!hasSelectedSkyboxFaces_) {
        for (int i = 0; i < static_cast<int>(skyboxFolders_.size()); ++i) {
            std::string lower = skyboxFolders_[static_cast<std::size_t>(i)];
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (lower.find("sunset") != std::string::npos) {
                selectedFolder_ = i;
                selectedSkyboxFaces_ = BuildFacesFromFolder(skyboxFolders_[static_cast<std::size_t>(i)]);
                hasSelectedSkyboxFaces_ = true;
                break;
            }
        }
    }
}

void MenuController::RenderSkyboxPanel(const std::function<void(const SkyboxFaces&)>& onSkyboxChosen) {
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
                    if (selectedFolder_ >= 0) {
                        selectedSkyboxFaces_ = BuildFacesFromFolder(skyboxFolders_[selectedFolder_]);
                        hasSelectedSkyboxFaces_ = true;
                    }
                }
            }
            ImGui::EndListBox();
        }

        if (hasSelectedSkyboxFaces_) {
            const auto& faces = selectedSkyboxFaces_;
            const char* labels[6] = {"+X","-X","+Y","-Y","+Z","-Z"};
            for (int i = 0; i < 6; ++i) {
                std::vector<char> buf(faces[i].begin(), faces[i].end());
                buf.push_back('\0');
                ImGui::InputText(labels[i], buf.data(), buf.size(), ImGuiInputTextFlags_ReadOnly);
            }
            if (onSkyboxChosen) {
                if (ImGui::Button("Apply")) {
                    onSkyboxChosen(faces);
                }
            } else {
                ImGui::TextDisabled("Skybox selection is stored only.");
            }
        } else {
            ImGui::TextDisabled("No skybox selected.");
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
        if (key == "showProjectSettings") showProjectSettings_ = (val == "1");
        else if (key == "showSkybox") showSkybox_ = (val == "1");
        else if (key == "showConsole") showConsole_ = (val == "1");
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
    out << "showProjectSettings=" << (showProjectSettings_ ? "1" : "0") << "\n";
    out << "showSkybox=" << (showSkybox_ ? "1" : "0") << "\n";
    out << "showConsole=" << (showConsole_ ? "1" : "0") << "\n";
    out << "selectedFolder=" << selectedFolder_ << "\n";
}

} // namespace raceman

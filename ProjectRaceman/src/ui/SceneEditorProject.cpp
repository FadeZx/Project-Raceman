#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

void SceneEditor::RenderProjectPanel() {
    if (ImGui::Begin("Browser")) {
        if (ImGui::BeginTabBar("BrowserTabs")) {
            if (ImGui::BeginTabItem("Project")) {
                if (ImGui::Button("Refresh")) {
                    RefreshProjectFiles();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", selectedProjectDirectory_.c_str());
                ImGui::Separator();

                const float directoryWidth = 230.0f;
                ImGui::BeginChild("ProjectDirectories", ImVec2(directoryWidth, 0.0f), true);
                ImGui::TextUnformatted("Directories");
                ImGui::Separator();
                for (const std::string& directory : projectDirectories_) {
                    const bool selected = (directory == selectedProjectDirectory_);
                    if (ImGui::Selectable(directory.c_str(), selected)) {
                        selectedProjectDirectory_ = directory;
                        if (ParentProjectDirectory(selectedProjectFile_) != selectedProjectDirectory_) {
                            selectedProjectFile_.clear();
                        }
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("ProjectFiles", ImVec2(0.0f, 0.0f), true);
                ImGui::TextUnformatted("Files");
                ImGui::Separator();
                bool hasFiles = false;
                for (const std::string& file : projectFiles_) {
                    if (ParentProjectDirectory(file) != selectedProjectDirectory_) {
                        continue;
                    }

                    hasFiles = true;
                    const bool isObj = IsObjAssetPath(file);
                    const bool isMaterial = IsMaterialAssetPath(file);
                    std::string filename = fs::path(file).filename().string();
                    std::string label;
                    if (isObj) {
                        label = "[OBJ] " + filename;
                    } else if (isMaterial) {
                        label = "[MAT] " + filename;
                    } else {
                        label = "[FILE] " + filename;
                    }

                    ImGui::PushID(file.c_str());
                    if (renamingProjectFile_ == file) {
                        if (focusProjectRename_) {
                            ImGui::SetKeyboardFocusHere();
                            focusProjectRename_ = false;
                        }
                        const bool enterPressed = ImGui::InputText("##projectRename", projectRenameBuffer_, sizeof(projectRenameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                        if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
                            CommitProjectFileRename();
                        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                            renamingProjectFile_.clear();
                        }
                    } else {
                        const bool selected = (selectedProjectFile_ == file);
                        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                            SelectProjectFile(file);
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                if (OpenProjectAssetInDefaultEditor(file)) {
                                    if (console_) {
                                        console_->AddLog("Opened project file: " + file);
                                    }
                                } else if (console_) {
                                    console_->AddError("Failed to open project file: " + file);
                                }
                            }
                        }
                        const bool fileActive = (selectedProjectFile_ == file) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                        if ((ImGui::IsItemFocused() || fileActive) && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                            BeginProjectFileRename(selectedProjectFile_.empty() ? file : selectedProjectFile_);
                        }
                        if ((ImGui::IsItemFocused() || fileActive) && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
                            DeleteProjectFile(selectedProjectFile_.empty() ? file : selectedProjectFile_);
                            ImGui::PopID();
                            break;
                        }
                        if (isObj) {
                            BeginProjectAssetDrag(kObjAssetPayload, file, file);
                        } else if (isMaterial) {
                            const std::string materialId = MaterialIdFromAssetPath(file);
                            BeginProjectAssetDrag(kMaterialAssetPayload, materialId, materialId);
                        }
                    }
                    ImGui::PopID();
                }
                if (!hasFiles) {
                    ImGui::TextDisabled("No files in this directory.");
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Console")) {
                if (console_) {
                    console_->RenderContents();
                } else {
                    ImGui::TextDisabled("Console is not connected.");
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}


void SceneEditor::ImportObj(const std::string& path) {
    namespace fs = std::filesystem;
    if (path.empty()) return;
    try {
        const std::string importPath = PrepareObjImportPath(path);
        if (importPath.empty()) return;

        auto model = raceman::LoadModelFromFile(importPath);
        std::string baseName;
        try { baseName = fs::path(importPath).stem().string(); } catch (...) { baseName = "Mesh"; }

        const auto infos = raceman::GetMeshInfos(model);
        if (infos.empty()) {
            return;
        }
        PushUndoState();
        for (size_t i = 0; i < infos.size(); ++i) {
            const auto& info = infos[i];
            SceneObject o;
            o.id = MakeId("mesh");
            o.name = baseName + (infos.size() > 1 ? ("_" + std::to_string(i)) : "");
            o.type = "Mesh";
            o.transform.position = {0.0f, 0.0f, 0.0f};
            o.transform.rotationEuler = {0.0f, 0.0f, 0.0f};
            o.transform.scale = {1.0f, 1.0f, 1.0f};
            o.color = {1.0f, 1.0f, 1.0f, 1.0f};
            o.materialId = "pbr_default";
            o.sourcePath = importPath;
            ApplyMeshInfoToSceneObject(o, info, model);
            objects_.push_back(std::move(o));
        }

        if (!objects_.empty()) {
            Select(static_cast<int>(objects_.size()) - 1);
            if (console_) {
                console_->AddLog("Imported OBJ: " + importPath + " (" + std::to_string(infos.size()) + " mesh" + (infos.size() != 1 ? "es" : "") + ")");
            }
            RefreshProjectFiles();
            if (onDirty_) onDirty_();
        }
    } catch (const std::exception&) {
        // ignore
    }
}

bool SceneEditor::ReplaceSelectedMeshFromObj(const std::string& path) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size()) || path.empty()) {
        return false;
    }

    try {
        const std::string importPath = PrepareObjImportPath(path);
        if (importPath.empty()) {
            return false;
        }

        auto model = raceman::LoadModelFromFile(importPath);
        const auto infos = raceman::GetMeshInfos(model);
        if (infos.empty()) {
            return false;
        }

        PushUndoState();
        SceneObject& obj = objects_[selectedIndex_];
        obj.type = "Mesh";
        obj.sourcePath = importPath;
        ApplyMeshInfoToSceneObject(obj, infos.front(), model);
        if (obj.materialId.empty()) {
            obj.materialId = "pbr_default";
        }

        if (console_) {
            console_->AddLog("Replaced Mesh Filter on " + obj.name + " with " + importPath);
        }
        RefreshProjectFiles();
        if (onDirty_) onDirty_();
        return true;
    } catch (...) {
        if (console_) {
            console_->AddLog("Failed to replace Mesh Filter with OBJ: " + path);
        }
        return false;
    }
}

bool SceneEditor::ReplaceSelectedMeshWithPlane() {
    return ReplaceSelectedMeshFromObj(kPlaneObjAssetPath);
}

void SceneEditor::BeginProjectFileRename(const std::string& path) {
    if (path.empty()) {
        return;
    }
    SelectProjectFile(path);
    renamingProjectFile_ = path;
    std::snprintf(projectRenameBuffer_, sizeof(projectRenameBuffer_), "%s", fs::path(path).filename().string().c_str());
    focusProjectRename_ = true;
}

void SceneEditor::CommitProjectFileRename() {
    if (renamingProjectFile_.empty()) {
        return;
    }

    const std::string oldProjectPath = NormalizeSlashes(renamingProjectFile_);
    std::string newFilename = TrimCopyLocal(projectRenameBuffer_);
    if (newFilename.empty()) {
        renamingProjectFile_.clear();
        return;
    }

    const fs::path oldAbsolutePath = ProjectAssetPathToAbsolute(oldProjectPath);
    const fs::path newAbsolutePath = (oldAbsolutePath.parent_path() / fs::path(newFilename)).lexically_normal();

    const fs::path assetsRoot = FindAssetsRoot();
    if (!IsUnderPath(newAbsolutePath, assetsRoot)) {
        if (console_) {
            console_->AddError("Rename blocked outside assets: " + newFilename);
        }
        renamingProjectFile_.clear();
        return;
    }

    const std::string newProjectPath = ToProjectAssetPath(newAbsolutePath, assetsRoot);
    if (NormalizeSlashes(newProjectPath) == oldProjectPath) {
        renamingProjectFile_.clear();
        return;
    }

    try {
        if (fs::exists(newAbsolutePath)) {
            if (console_) {
                console_->AddError("Project file already exists: " + newProjectPath);
            }
            renamingProjectFile_.clear();
            return;
        }

        fs::rename(oldAbsolutePath, newAbsolutePath);

        const std::string oldMaterialId = MaterialIdFromAssetPath(oldProjectPath);
        const std::string newMaterialId = MaterialIdFromAssetPath(newProjectPath);
        if (IsMaterialAssetPath(oldProjectPath) && IsMaterialAssetPath(newProjectPath)) {
            for (auto& object : objects_) {
                if (object.materialId == oldMaterialId) {
                    object.materialId = newMaterialId;
                }
            }
            if (inspectedMaterialId_ == oldMaterialId) {
                inspectedMaterialId_ = newMaterialId;
            }
            materialManager_.LoadAll();
        }

        for (auto& object : objects_) {
            if (NormalizeSlashes(object.sourcePath) == oldProjectPath) {
                object.sourcePath = newProjectPath;
            }
            if (NormalizeSlashes(object.diffuseTexturePath) == oldProjectPath) {
                object.diffuseTexturePath = newProjectPath;
            }
        }

        if (console_) {
            console_->AddLog("Renamed project file: " + oldProjectPath + " -> " + newProjectPath);
        }
        selectedProjectFile_ = newProjectPath;
        renamingProjectFile_.clear();
        RefreshProjectFiles();
        if (onDirty_) onDirty_();
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to rename project file: " + oldProjectPath);
        }
        renamingProjectFile_.clear();
    }
}

void SceneEditor::DeleteProjectFile(const std::string& path) {
    if (path.empty()) {
        return;
    }

    const std::string projectPath = NormalizeSlashes(path);
    const fs::path absolutePath = ProjectAssetPathToAbsolute(projectPath);
    const std::string materialId = MaterialIdFromAssetPath(projectPath);

    try {
        if (!fs::exists(absolutePath) || !fs::is_regular_file(absolutePath)) {
            return;
        }

        fs::remove(absolutePath);

        if (IsMaterialAssetPath(projectPath)) {
            for (auto& object : objects_) {
                if (object.materialId == materialId) {
                    object.materialId = "pbr_default";
                }
            }
            if (inspectedMaterialId_ == materialId) {
                inspectMaterial_ = false;
                inspectedMaterialId_.clear();
            }
            materialManager_.LoadAll();
        }

        for (auto& object : objects_) {
            if (NormalizeSlashes(object.sourcePath) == projectPath) {
                object.sourcePath.clear();
                object.vao = 0;
                object.indexCount = 0;
                object.modelRef.reset();
            }
            if (NormalizeSlashes(object.diffuseTexturePath) == projectPath) {
                object.diffuseTexturePath.clear();
                object.diffuseTextureId = 0;
            }
        }

        if (console_) {
            console_->AddLog("Deleted project file: " + projectPath);
        }
        if (renamingProjectFile_ == projectPath) {
            renamingProjectFile_.clear();
        }
        if (selectedProjectFile_ == projectPath) {
            selectedProjectFile_.clear();
        }
        RefreshProjectFiles();
        if (onDirty_) onDirty_();
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to delete project file: " + projectPath);
        }
    }
}


void SceneEditor::RefreshProjectFiles() {
    projectDirectories_.clear();
    projectFiles_.clear();
    materialManager_.LoadAll();

    try {
        const fs::path assetsRoot = FindAssetsRoot();
        if (!fs::exists(assetsRoot) || !fs::is_directory(assetsRoot)) {
            return;
        }

        projectDirectories_.push_back("assets");
        for (const auto& entry : fs::recursive_directory_iterator(assetsRoot)) {
            if (entry.is_directory()) {
                projectDirectories_.push_back(ToProjectAssetPath(entry.path(), assetsRoot));
                continue;
            }
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string path = ToProjectAssetPath(entry.path(), assetsRoot);
            projectFiles_.push_back(path);
        }
        std::sort(projectDirectories_.begin(), projectDirectories_.end(), [](const std::string& a, const std::string& b) {
            return ToLowerCopy(a) < ToLowerCopy(b);
        });
        projectDirectories_.erase(std::unique(projectDirectories_.begin(), projectDirectories_.end()), projectDirectories_.end());

        std::sort(projectFiles_.begin(), projectFiles_.end(), [](const std::string& a, const std::string& b) {
            const bool aSpecial = IsObjAssetPath(a) || IsMaterialAssetPath(a);
            const bool bSpecial = IsObjAssetPath(b) || IsMaterialAssetPath(b);
            if (aSpecial != bSpecial) {
                return aSpecial > bSpecial;
            }
            return ToLowerCopy(a) < ToLowerCopy(b);
        });

        if (std::find(projectDirectories_.begin(), projectDirectories_.end(), selectedProjectDirectory_) == projectDirectories_.end()) {
            selectedProjectDirectory_ = "assets";
        }
        if (!selectedProjectFile_.empty() && std::find(projectFiles_.begin(), projectFiles_.end(), selectedProjectFile_) == projectFiles_.end()) {
            selectedProjectFile_.clear();
        }
    } catch (...) {
        projectDirectories_.clear();
        projectFiles_.clear();
        selectedProjectDirectory_ = "assets";
        selectedProjectFile_.clear();
    }
}

void SceneEditor::ScanObjDir(const std::string& dir) {
    objFiles_.clear();
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".obj") {
                std::filesystem::path p = entry.path();
                objFiles_.push_back(p.lexically_relative(dir).string());
            }
        }
        std::sort(objFiles_.begin(), objFiles_.end());
    } catch (...) {
        // ignore
    }
}
} // namespace raceman


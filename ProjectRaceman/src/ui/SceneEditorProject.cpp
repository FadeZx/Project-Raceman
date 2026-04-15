#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

const char* ProjectCreateAssetTypeTitle(ProjectCreateAssetType type) {
    if (type == ProjectCreateAssetType::Folder) return "Create Folder";
    if (type == ProjectCreateAssetType::Scene) return "Create Scene";
    if (type == ProjectCreateAssetType::Material) return "Create Material";
    if (type == ProjectCreateAssetType::VehicleProfile) return "Create Vehicle Profile";
    if (type == ProjectCreateAssetType::Script) return "Create C++ Script";
    return "Create Asset";
}

const char* ProjectCreateAssetTypeDefaultName(ProjectCreateAssetType type) {
    if (type == ProjectCreateAssetType::Folder) return "NewFolder";
    if (type == ProjectCreateAssetType::Scene) return "NewScene";
    if (type == ProjectCreateAssetType::Material) return "NewMaterial";
    if (type == ProjectCreateAssetType::VehicleProfile) return "NewVehicleProfile";
    if (type == ProjectCreateAssetType::Script) return "NewScript";
    return "NewAsset";
}

std::string SanitizeFolderName(std::string value) {
    value = TrimCopyLocal(std::move(value));
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            out.push_back(ch);
        }
    }
    return TrimCopyLocal(out);
}

std::string SanitizeAssetBaseName(std::string value) {
    value = TrimCopyLocal(std::move(value));
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            out.push_back(ch == ' ' ? '_' : ch);
        }
    }
    return out.empty() ? std::string("Asset") : out;
}

std::string ProjectFolderDisplayName(const std::string& path) {
    if (NormalizeSlashes(path) == "assets") {
        return "Assets";
    }
    std::string name = fs::path(path).filename().string();
    return name.empty() ? path : name;
}

bool IsDirectChildProjectPath(const std::string& path, const std::string& parent) {
    return NormalizeSlashes(ParentProjectDirectory(path)) == NormalizeSlashes(parent);
}

std::string AssetIconForProjectFile(const std::string& path) {
    const std::string lower = ToLowerCopy(NormalizeSlashes(path));
    const std::string extension = ToLowerCopy(fs::path(path).extension().string());
    if (IsSceneAssetPath(path)) return "asset-scene.png";
    if (IsMaterialAssetPath(path)) return "asset-material.png";
    if (IsVehicleConfigAssetPath(path)) return "asset-vehicle.png";
    if (IsMeshAssetPath(path)) return "asset-mesh.png";
    if (IsPrefabAssetPath(path)) return "asset-prefab.png";
    if (extension == ".h" || extension == ".cpp") return "asset-script.png";
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".webp" || extension == ".hdr") return "asset-image.png";
    if (lower.find("/skybox/") != std::string::npos) return "asset-skybox.png";
    return "asset-file.png";
}

std::vector<std::string> BreadcrumbParts(const std::string& directory) {
    std::vector<std::string> parts;
    fs::path path(NormalizeSlashes(directory));
    for (const auto& part : path) {
        parts.push_back(part.string());
    }
    if (parts.empty()) {
        parts.push_back("assets");
    }
    return parts;
}

std::string EllipsizeTextToWidth(const std::string& text, float maxWidth) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }

    constexpr const char* kEllipsis = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;
    if (ellipsisWidth >= maxWidth) {
        return kEllipsis;
    }

    std::string clipped = text;
    while (!clipped.empty() && ImGui::CalcTextSize(clipped.c_str()).x + ellipsisWidth > maxWidth) {
        clipped.pop_back();
    }
    return clipped.empty() ? std::string(kEllipsis) : clipped + kEllipsis;
}

int ProjectDirectoryDepth(const std::string& path) {
    int depth = 0;
    fs::path normalized(NormalizeSlashes(path));
    for (const auto& part : normalized) {
        (void)part;
        ++depth;
    }
    return (std::max)(0, depth - 1);
}

std::string SanitizeImportedMaterialId(std::string value) {
    value = TrimCopyLocal(std::move(value));
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            out.push_back(ch == ' ' ? '_' : static_cast<char>(std::tolower(uch)));
        }
    }
    return out.empty() ? std::string("material") : out;
}

std::string ResolveImportedTextureAssetPath(const std::string& meshAssetPath, const std::string& texturePath) {
    if (meshAssetPath.empty() || texturePath.empty()) {
        return {};
    }

    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path meshAbsolutePath = ProjectAssetPathToAbsolute(meshAssetPath);
    fs::path textureAbsolutePath = fs::path(NormalizeSlashes(texturePath));
    if (!textureAbsolutePath.is_absolute()) {
        textureAbsolutePath = (meshAbsolutePath.parent_path() / textureAbsolutePath).lexically_normal();
    }

    if (!fs::exists(textureAbsolutePath) || !IsUnderPath(textureAbsolutePath, assetsRoot)) {
        return {};
    }

    return ToProjectAssetPath(textureAbsolutePath, assetsRoot);
}

std::string EnsureImportedMaterialAsset(MaterialManager& materialManager,
                                        const std::string& meshAssetPath,
                                        const std::string& packageBaseName,
                                        const ImportedMeshInfo& info) {
    const std::string baseId = SanitizeImportedMaterialId(packageBaseName);
    const std::string materialPart = SanitizeImportedMaterialId(
        info.materialName.empty() ? fs::path(meshAssetPath).stem().string() : info.materialName);
    const std::string materialId = baseId + "_" + materialPart;

    Material material;
    material.name = info.materialName.empty() ? materialId : info.materialName;
    material.shader = "pbr";
    material.texAlbedo = ResolveImportedTextureAssetPath(meshAssetPath, info.diffuseTexturePath);
    materialManager.Save(materialId, material);
    return materialId;
}

} // namespace

void SceneEditor::RenderProjectPanel() {
    if (ImGui::Begin("Browser", nullptr, ImGuiWindowFlags_NoCollapse)) {
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
                auto renderDirectoryTree = [&](auto&& self, const std::string& directory) -> void {
                    std::vector<std::string> childFolders;
                    for (const std::string& candidate : projectDirectories_) {
                        if (candidate != directory && IsDirectChildProjectPath(candidate, directory)) {
                            childFolders.push_back(candidate);
                        }
                    }

                    ImGui::PushID(directory.c_str());
                    const bool selected = (directory == selectedProjectDirectory_);
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (selected) {
                        flags |= ImGuiTreeNodeFlags_Selected;
                    }
                    if (directory == "assets") {
                        flags |= ImGuiTreeNodeFlags_DefaultOpen;
                    }
                    if (childFolders.empty()) {
                        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    }
                    const std::string directoryLabel = ProjectFolderDisplayName(directory);
                    const bool open = ImGui::TreeNodeEx(directoryLabel.c_str(), flags);
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
                        selectedProjectDirectory_ = directory;
                        if (ParentProjectDirectory(selectedProjectFile_) != selectedProjectDirectory_) {
                            selectedProjectFile_.clear();
                        }
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                            const char* payloadPath = static_cast<const char*>(payload->Data);
                            if (payloadPath != nullptr) {
                                MoveProjectFile(payloadPath, directory);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", directory.c_str());
                    }
                    if (open && !childFolders.empty()) {
                        for (const std::string& child : childFolders) {
                            self(self, child);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                };
                renderDirectoryTree(renderDirectoryTree, "assets");
                for (const std::string& directory : projectDirectories_) {
                    if (directory != "assets" && std::find(projectDirectories_.begin(), projectDirectories_.end(), ParentProjectDirectory(directory)) == projectDirectories_.end()) {
                        renderDirectoryTree(renderDirectoryTree, directory);
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("ProjectFiles", ImVec2(0.0f, 0.0f), true);
                std::string sceneToOpen;
                ImGui::TextUnformatted("Path:");
                ImGui::SameLine();
                if (selectedProjectDirectory_ != "assets") {
                    if (ImGui::SmallButton("Up")) {
                        selectedProjectDirectory_ = ParentProjectDirectory(selectedProjectDirectory_);
                        selectedProjectFile_.clear();
                    }
                    ImGui::SameLine();
                }
                const std::vector<std::string> breadcrumbs = BreadcrumbParts(selectedProjectDirectory_);
                std::string breadcrumbPath;
                for (std::size_t i = 0; i < breadcrumbs.size(); ++i) {
                    breadcrumbPath = i == 0 ? breadcrumbs[i] : breadcrumbPath + "/" + breadcrumbs[i];
                    if (i > 0) {
                        ImGui::TextDisabled(">");
                        ImGui::SameLine();
                    }
                    const std::string displayLabel = i == 0 ? std::string("Assets") : breadcrumbs[i];
                    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", displayLabel.c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                        selectedProjectDirectory_ = breadcrumbPath;
                        selectedProjectFile_.clear();
                    }
                    ImGui::SameLine();
                }
                const float refreshWidth = ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                const float refreshX = ImGui::GetWindowContentRegionMax().x - refreshWidth;
                if (ImGui::GetCursorPosX() < refreshX) {
                    ImGui::SetCursorPosX(refreshX);
                }
                if (ImGui::SmallButton("Refresh##ProjectFiles")) {
                    RefreshProjectFiles();
                }
                ImGui::Separator();

                const float tileWidth = 96.0f;
                const float tileHeight = 92.0f;
                const float iconSize = 46.0f;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float availableWidth = ImGui::GetContentRegionAvail().x;
                int columnIndex = 0;
                bool hasTiles = false;
                bool breakTileLoop = false;

                auto renderTile = [&](const std::string& id,
                                      const std::string& displayName,
                                      const std::string& iconFilename,
                                      bool selected,
                                      bool isFolder) -> bool {
                    ImGui::PushID(id.c_str());
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##projectTile", ImVec2(tileWidth, tileHeight));
                    const bool hovered = ImGui::IsItemHovered();
                    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                    const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const ImU32 background = selected
                        ? ImGui::GetColorU32(ImGuiCol_HeaderActive)
                        : hovered ? ImGui::GetColorU32(ImGuiCol_HeaderHovered) : 0;
                    if (background != 0) {
                        drawList->AddRectFilled(pos, ImVec2(pos.x + tileWidth, pos.y + tileHeight), background, 6.0f);
                    }
                    unsigned int icon = GetComponentIconTexture(iconFilename);
                    if (icon == 0 && iconFilename != "asset-file.png") {
                        icon = GetComponentIconTexture("asset-file.png");
                    }
                    const ImVec2 iconMin(pos.x + (tileWidth - iconSize) * 0.5f, pos.y + 8.0f);
                    const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
                    if (icon != 0) {
                        drawList->AddImage(static_cast<ImTextureID>(icon), iconMin, iconMax);
                    } else {
                        drawList->AddRect(iconMin, iconMax, ImGui::GetColorU32(ImGuiCol_TextDisabled), 4.0f);
                        drawList->AddText(ImVec2(iconMin.x + 8.0f, iconMin.y + 14.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), isFolder ? "DIR" : "FILE");
                    }
                    if (renamingProjectFile_ == id) {
                        ImGui::SetCursorScreenPos(ImVec2(pos.x + 4.0f, pos.y + iconSize + 18.0f));
                        ImGui::SetNextItemWidth(tileWidth - 8.0f);
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
                        const std::string clippedName = EllipsizeTextToWidth(displayName, tileWidth - 10.0f);
                        const ImVec2 textSize = ImGui::CalcTextSize(clippedName.c_str());
                        drawList->AddText(ImVec2(pos.x + (tileWidth - textSize.x) * 0.5f, pos.y + iconSize + 20.0f), ImGui::GetColorU32(ImGuiCol_Text), clippedName.c_str());
                    }
                    ImGui::PopID();
                    if ((columnIndex + 1) * (tileWidth + spacing) + tileWidth <= availableWidth) {
                        ImGui::SameLine();
                        ++columnIndex;
                    } else {
                        columnIndex = 0;
                    }
                    return clicked || doubleClicked;
                };

                std::vector<std::string> childFolders;
                for (const std::string& directory : projectDirectories_) {
                    if (directory != selectedProjectDirectory_ && IsDirectChildProjectPath(directory, selectedProjectDirectory_)) {
                        childFolders.push_back(directory);
                    }
                }

                for (const std::string& directory : childFolders) {
                    hasTiles = true;
                    const bool selected = selectedProjectFile_ == directory;
                    const bool activated = renderTile(directory, ProjectFolderDisplayName(directory), "asset-folder.png", selected, true);
                    if (activated) {
                        selectedProjectFile_ = directory;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            selectedProjectDirectory_ = directory;
                            selectedProjectFile_.clear();
                        }
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                            const char* payloadPath = static_cast<const char*>(payload->Data);
                            if (payloadPath != nullptr) {
                                MoveProjectFile(payloadPath, directory);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::BeginPopupContextItem(("FolderContext##" + directory).c_str())) {
                        selectedProjectFile_ = directory;
                        ImGui::TextDisabled("%s", ProjectFolderDisplayName(directory).c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Open")) {
                            selectedProjectDirectory_ = directory;
                            selectedProjectFile_.clear();
                        }
                        if (ImGui::BeginMenu("Vehicle")) {
                            if (ImGui::MenuItem("Add Vehicle Profile")) {
                                selectedProjectDirectory_ = directory;
                                selectedProjectFile_.clear();
                                createProjectAssetType_ = ProjectCreateAssetType::VehicleProfile;
                                std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                                showCreateProjectAssetPopup_ = true;
                            }
                            ImGui::EndMenu();
                        }
                        if (ImGui::MenuItem("Rename", "F2")) {
                            BeginProjectFileRename(directory);
                        }
                        if (ImGui::MenuItem("Delete Empty Folder", "Del")) {
                            DeleteProjectFolder(directory);
                            breakTileLoop = true;
                        }
                        ImGui::EndPopup();
                    }
                    if (breakTileLoop) {
                        break;
                    }
                }
                if (!breakTileLoop) {
                for (const std::string& file : projectFiles_) {
                    if (ParentProjectDirectory(file) != selectedProjectDirectory_) {
                        continue;
                    }

                    hasTiles = true;
                    const bool isMesh = IsMeshAssetPath(file);
                    const bool isMaterial = IsMaterialAssetPath(file);
                    const bool isScene = IsSceneAssetPath(file);
                    const bool isPrefab = IsPrefabAssetPath(file);
                    std::string filename = ProjectAssetDisplayFilename(file);

                    ImGui::PushID(file.c_str());
                    if (renamingProjectFile_ == file) {
                        renderTile(file, filename, AssetIconForProjectFile(file), selectedProjectFile_ == file, false);
                    } else {
                        bool deletedFromContext = false;
                        const bool selected = (selectedProjectFile_ == file);
                        if (renderTile(file, filename, AssetIconForProjectFile(file), selected, false)) {
                            SelectProjectFile(file);
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                if (isScene) {
                                    sceneToOpen = file;
                                } else if (isMaterial) {
                                    OpenMaterialEditor(MaterialIdFromAssetPath(file));
                                } else if (isPrefab) {
                                    if (InstantiatePrefab(file)) {
                                        if (console_) console_->AddLog("Instantiated prefab: " + file);
                                    } else if (console_) {
                                        console_->AddError("Failed to instantiate prefab: " + file);
                                    }
                                } else if (OpenProjectAssetInDefaultEditor(file)) {
                                    if (console_) {
                                        console_->AddLog("Opened project file: " + file);
                                    }
                                } else if (console_) {
                                    console_->AddError("Failed to open project file: " + file);
                                }
                            }
                        }
                        if (ImGui::BeginPopupContextItem("ProjectFileContext")) {
                            SelectProjectFile(file);
                            ImGui::TextDisabled("%s", ProjectAssetDisplayFilename(file).c_str());
                            ImGui::Separator();
                            if (isScene) {
                                if (ImGui::MenuItem("Open")) {
                                    sceneToOpen = file;
                                }
                            } else if (isPrefab) {
                                if (ImGui::MenuItem("Instantiate")) {
                                    if (InstantiatePrefab(file)) {
                                        if (console_) console_->AddLog("Instantiated prefab: " + file);
                                    } else if (console_) {
                                        console_->AddError("Failed to instantiate prefab: " + file);
                                    }
                                }
                            } else if (isMaterial) {
                                if (ImGui::MenuItem("Edit")) {
                                    OpenMaterialEditor(MaterialIdFromAssetPath(file));
                                }
                                if (ImGui::MenuItem("Open in Default App")) {
                                    if (OpenProjectAssetInDefaultEditor(file)) {
                                        if (console_) {
                                            console_->AddLog("Opened project file: " + file);
                                        }
                                    } else if (console_) {
                                        console_->AddError("Failed to open project file: " + file);
                                    }
                                }
                            } else {
                                if (ImGui::MenuItem("Open")) {
                                    if (OpenProjectAssetInDefaultEditor(file)) {
                                        if (console_) {
                                            console_->AddLog("Opened project file: " + file);
                                        }
                                    } else if (console_) {
                                        console_->AddError("Failed to open project file: " + file);
                                    }
                                }
                            }
                            ImGui::Separator();
                            if (ImGui::BeginMenu("Move To")) {
                                bool anyMoveTarget = false;
                                const std::string currentDirectory = ParentProjectDirectory(file);
                                for (const std::string& directory : projectDirectories_) {
                                    if (directory == currentDirectory) {
                                        continue;
                                    }
                                    anyMoveTarget = true;
                                    const std::string label = ProjectFolderDisplayName(directory) + "##moveTarget_" + directory;
                                    if (ImGui::MenuItem(label.c_str())) {
                                        MoveProjectFile(file, directory);
                                        deletedFromContext = true;
                                        break;
                                    }
                                }
                                if (!anyMoveTarget) {
                                    ImGui::TextDisabled("No other folders.");
                                }
                                ImGui::EndMenu();
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                                fileClipboard_.path = file;
                                fileClipboard_.isCut = false;
                            }
                            if (ImGui::MenuItem("Cut", "Ctrl+X")) {
                                fileClipboard_.path = file;
                                fileClipboard_.isCut = true;
                            }
                            if (ImGui::MenuItem("Rename", "F2")) {
                                BeginProjectFileRename(file);
                            }
                            if (ImGui::MenuItem("Delete", "Del")) {
                                DeleteProjectFile(file);
                                deletedFromContext = true;
                            }
                            ImGui::EndPopup();
                        }
                        if (deletedFromContext) {
                            ImGui::PopID();
                            breakTileLoop = true;
                            break;
                        }
                        const bool fileActive = (selectedProjectFile_ == file) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                        if ((ImGui::IsItemFocused() || fileActive) && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                            BeginProjectFileRename(selectedProjectFile_.empty() ? file : selectedProjectFile_);
                        }
                        if ((ImGui::IsItemFocused() || fileActive) && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
                            DeleteProjectFile(selectedProjectFile_.empty() ? file : selectedProjectFile_);
                            ImGui::PopID();
                            breakTileLoop = true;
                            break;
                        }
                    }
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        ImGui::SetDragDropPayload(kProjectFilePayload, file.c_str(), file.size() + 1);
                        ImGui::TextUnformatted(file.c_str());
                        ImGui::EndDragDropSource();
                    }
                    ImGui::PopID();
                }
                }
                if (!hasTiles) {
                    ImGui::TextDisabled("No assets in this folder.");
                }
                // Keyboard shortcuts for copy/cut/paste (when panel is focused, not typing)
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
                    const bool ctrl = ImGui::GetIO().KeyCtrl;
                    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C) && !selectedProjectFile_.empty() && !fs::is_directory(ProjectAssetPathToAbsolute(selectedProjectFile_))) {
                        fileClipboard_.path = selectedProjectFile_;
                        fileClipboard_.isCut = false;
                    }
                    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X) && !selectedProjectFile_.empty() && !fs::is_directory(ProjectAssetPathToAbsolute(selectedProjectFile_))) {
                        fileClipboard_.path = selectedProjectFile_;
                        fileClipboard_.isCut = true;
                    }
                    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V) && !fileClipboard_.path.empty()) {
                        if (fileClipboard_.isCut) {
                            if (MoveProjectFile(fileClipboard_.path, selectedProjectDirectory_)) {
                                fileClipboard_.path.clear();
                            }
                        } else {
                            CopyProjectFileTo(fileClipboard_.path, selectedProjectDirectory_);
                        }
                    }
                }
                if (ImGui::BeginPopupContextWindow("ProjectFilesEmptyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                    if (!fileClipboard_.path.empty()) {
                        const std::string pasteLabel = std::string("Paste  ") + ProjectAssetDisplayFilename(fileClipboard_.path) + (fileClipboard_.isCut ? "  (cut)" : "  (copy)");
                        if (ImGui::MenuItem(pasteLabel.c_str(), "Ctrl+V")) {
                            if (fileClipboard_.isCut) {
                                if (MoveProjectFile(fileClipboard_.path, selectedProjectDirectory_)) {
                                    fileClipboard_.path.clear();
                                }
                            } else {
                                CopyProjectFileTo(fileClipboard_.path, selectedProjectDirectory_);
                            }
                        }
                        ImGui::Separator();
                    }
                    if (ImGui::BeginMenu("Create")) {
                        if (ImGui::MenuItem("Folder")) {
                            createProjectAssetType_ = ProjectCreateAssetType::Folder;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        if (ImGui::MenuItem("Scene")) {
                            createProjectAssetType_ = ProjectCreateAssetType::Scene;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        if (ImGui::MenuItem("Material")) {
                            createProjectAssetType_ = ProjectCreateAssetType::Material;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        if (ImGui::MenuItem("C++ Script")) {
                            createProjectAssetType_ = ProjectCreateAssetType::Script;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Vehicle")) {
                        if (ImGui::MenuItem("Add Vehicle Profile")) {
                            createProjectAssetType_ = ProjectCreateAssetType::VehicleProfile;
                            std::snprintf(createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_), "%s", ProjectCreateAssetTypeDefaultName(createProjectAssetType_));
                            showCreateProjectAssetPopup_ = true;
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Refresh")) {
                        RefreshProjectFiles();
                    }
                    ImGui::EndPopup();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                        const char* payloadPath = static_cast<const char*>(payload->Data);
                        if (payloadPath != nullptr) {
                            MoveProjectFile(payloadPath, selectedProjectDirectory_);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 2.0f;
                const float footerY = ImGui::GetWindowHeight() - footerHeight;
                if (ImGui::GetCursorPosY() < footerY) {
                    ImGui::SetCursorPosY(footerY);
                }
                ImGui::Separator();
                const std::string selectedPath = selectedProjectFile_.empty() ? selectedProjectDirectory_ : selectedProjectFile_;
                ImGui::TextDisabled("Selected: %s", selectedPath.c_str());
                ImGui::EndChild();
                // Accept hierarchy-object drag onto the project files pane to create a prefab.
                // Must be called after EndChild() so the child window is the last item.
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                        if (payload->DataSize == sizeof(int)) {
                            pendingPrefabObjectIndex_ = *static_cast<const int*>(payload->Data);
                            const std::string defaultName =
                                (pendingPrefabObjectIndex_ >= 0 && pendingPrefabObjectIndex_ < (int)objects_.size())
                                ? SanitizeAssetBaseName(objects_[pendingPrefabObjectIndex_].name)
                                : std::string("NewPrefab");
                            std::snprintf(savePrefabNameBuffer_, sizeof(savePrefabNameBuffer_), "%s", defaultName.c_str());
                            showSavePrefabPopup_ = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (showSavePrefabPopup_) {
                    ImGui::OpenPopup("Save Prefab");
                    showSavePrefabPopup_ = false;
                }
                if (ImGui::BeginPopupModal("Save Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Save as Prefab");
                    ImGui::TextDisabled("Directory: %s", selectedProjectDirectory_.c_str());
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::IsWindowAppearing()) {
                        ImGui::SetKeyboardFocusHere();
                    }
                    const bool enterPressed = ImGui::InputText("Name##prefabName", savePrefabNameBuffer_, sizeof(savePrefabNameBuffer_),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    const bool submit = ImGui::Button("Save") || enterPressed;
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        pendingPrefabObjectIndex_ = -1;
                        ImGui::CloseCurrentPopup();
                    }
                    if (submit && pendingPrefabObjectIndex_ >= 0) {
                        const std::string sanitized = SanitizeAssetBaseName(savePrefabNameBuffer_);
                        if (!sanitized.empty()) {
                            const std::string prefabPath = selectedProjectDirectory_ + "/" + sanitized + ".prefab.json";
                            if (SaveObjectAsPrefab(pendingPrefabObjectIndex_, prefabPath)) {
                                RefreshProjectFiles();
                                selectedProjectFile_ = prefabPath;
                                if (console_) console_->AddLog("Saved prefab: " + prefabPath);
                            } else if (console_) {
                                console_->AddError("Failed to save prefab: " + prefabPath);
                            }
                            pendingPrefabObjectIndex_ = -1;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::EndPopup();
                }
                if (showCreateProjectAssetPopup_) {
                    ImGui::OpenPopup("Create Project Asset");
                    showCreateProjectAssetPopup_ = false;
                }
                if (ImGui::BeginPopupModal("Create Project Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted(ProjectCreateAssetTypeTitle(createProjectAssetType_));
                    ImGui::TextDisabled("Directory: %s", selectedProjectDirectory_.c_str());
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("Name", createProjectAssetNameBuffer_, sizeof(createProjectAssetNameBuffer_));
                    const bool submit = ImGui::Button("Create") || ImGui::IsKeyPressed(ImGuiKey_Enter);
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        createProjectAssetType_ = ProjectCreateAssetType::None;
                        ImGui::CloseCurrentPopup();
                    }
                    if (submit) {
                        bool created = false;
                        std::string createdScenePath;
                        std::string createdVehicleConfigPath;
                        if (createProjectAssetType_ == ProjectCreateAssetType::Folder) {
                            created = CreateProjectFolder(createProjectAssetNameBuffer_);
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::Scene) {
                            created = CreateSceneAsset(createProjectAssetNameBuffer_, &createdScenePath);
                            if (created) {
                                selectedProjectFile_ = createdScenePath;
                            }
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::Material) {
                            std::string materialId;
                            created = CreateMaterialAsset(createProjectAssetNameBuffer_, &materialId);
                            if (created) {
                                selectedProjectFile_ = selectedProjectDirectory_ + "/" + materialId + ".mat.json";
                            }
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::VehicleProfile) {
                            created = CreateVehicleConfigAsset(createProjectAssetNameBuffer_, &createdVehicleConfigPath);
                            if (created) {
                                selectedProjectFile_ = createdVehicleConfigPath;
                            }
                        } else if (createProjectAssetType_ == ProjectCreateAssetType::Script) {
                            created = CreateScriptAsset(createProjectAssetNameBuffer_, false);
                        }
                        if (created) {
                            createProjectAssetType_ = ProjectCreateAssetType::None;
                            createProjectAssetNameBuffer_[0] = '\0';
                            RefreshProjectFiles();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::EndPopup();
                }
                if (!sceneToOpen.empty() && sceneToOpen != savePath_) {
                    if (OpenSceneAsset(sceneToOpen)) {
                        if (console_) {
                            console_->AddLog("Opened scene asset: " + sceneToOpen);
                        }
                    } else if (console_) {
                        console_->AddError("Failed to open scene asset: " + sceneToOpen);
                    }
                }

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

void SceneEditor::RenderProjectPhysicsSettings() {
    bool projectSettingsChanged = false;
    if (ImGui::BeginTable("ProjectPhysicsLayerMatrix", kPhysicsLayerCount + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Layer");
        for (int column = 0; column < kPhysicsLayerCount; ++column) {
            ImGui::TableSetColumnIndex(column + 1);
            ImGui::PushID(("projectPhysicsLayerNameHeader_" + std::to_string(column)).c_str());
            ImGui::SetNextItemWidth(100.0f);
            std::string layerName = physicsLayerNames_[static_cast<std::size_t>(column)];
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "%s", layerName.c_str());
            if (ImGui::InputText("##layerName", buffer, sizeof(buffer))) {
                physicsLayerNames_[static_cast<std::size_t>(column)] = buffer;
                projectSettingsChanged = true;
            }
            ImGui::PopID();
        }

        for (int row = 0; row < kPhysicsLayerCount; ++row) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(GetPhysicsLayerName(row));
            for (int column = 0; column < kPhysicsLayerCount; ++column) {
                ImGui::TableSetColumnIndex(column + 1);
                bool collides = physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
                const std::string checkboxId = "##projectPhysicsLayerCollision_" + std::to_string(row) + "_" + std::to_string(column);
                if (ImGui::Checkbox(checkboxId.c_str(), &collides)) {
                    physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = collides;
                    physicsLayerCollisionMatrix_[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] = collides;
                    projectSettingsChanged = true;
                }
            }
        }

        ImGui::EndTable();
    }

    if (projectSettingsChanged) {
        for (int layerIndex = 0; layerIndex < kPhysicsLayerCount; ++layerIndex) {
            if (physicsLayerNames_[static_cast<std::size_t>(layerIndex)].empty()) {
                physicsLayerNames_[static_cast<std::size_t>(layerIndex)] = layerIndex == 0 ? "Default" : ("Layer" + std::to_string(layerIndex));
            }
        }
        SaveProject();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Assign each object's physics layer in the Inspector. This matrix controls which layers collide.");
}


void SceneEditor::ImportObj(const std::string& path) {
    if (path.empty()) {
        return;
    }
    pendingImportMeshPath_ = path;
    showImportMeshOptionsPopup_ = true;
}

void SceneEditor::ImportObjWithOptions(const std::string& path, int pivotMode) {
    namespace fs = std::filesystem;
    if (path.empty()) return;
    try {
        std::string importPath;
        std::shared_ptr<::Model> model;
        std::vector<ImportedMeshInfo> infos;
        if (!TryLoadMeshAsset(path, importPath, model, infos)) {
            return;
        }
        std::string baseName;
        try { baseName = fs::path(importPath).stem().string(); } catch (...) { baseName = "Mesh"; }
        PushUndoState();
        const bool centerPivot = pivotMode == 1;
        std::string packageRootId;
        int firstImportedIndex = -1;
        if (infos.size() > 1) {
            SceneObject packageRoot;
            packageRoot.id = MakeId("gameobject");
            packageRoot.name = baseName;
            packageRoot.type = "GameObject";
            packageRoot.transform.position = {0.0f, 0.0f, 0.0f};
            packageRoot.transform.rotationEuler = {0.0f, 0.0f, 0.0f};
            packageRoot.transform.scale = {1.0f, 1.0f, 1.0f};
            packageRoot.hasMeshFilter = false;
            packageRoot.hasMeshRenderer = false;
            packageRoot.hasScriptComponent = false;
            packageRoot.hasRigidbody = false;
            packageRoot.hasVehicle = false;
            packageRoot.hasCharacterController = false;
            packageRoot.hasBoxCollider = false;
            packageRoot.hasSphereCollider = false;
            packageRoot.hasCapsuleCollider = false;
            packageRoot.hasPlaneCollider = false;
            packageRoot.hasMeshCollider = false;
            packageRoot.hasCamera = false;
            packageRoot.hasLight = false;
            packageRootId = packageRoot.id;
            objects_.push_back(std::move(packageRoot));
            firstImportedIndex = static_cast<int>(objects_.size()) - 1;
        }

        for (size_t i = 0; i < infos.size(); ++i) {
            const auto& info = infos[i];
            const std::string meshBaseName = baseName + (infos.size() > 1 ? ("_" + std::to_string(i)) : "");
            const glm::vec3 meshCenter = (info.localBoundsMin + info.localBoundsMax) * 0.5f;

            SceneObject o;
            o.id = MakeId("mesh");
            o.name = meshBaseName;
            o.type = "GameObject";
            o.parentId = packageRootId;
            o.transform.position = centerPivot ? meshCenter : glm::vec3{0.0f, 0.0f, 0.0f};
            o.transform.rotationEuler = {0.0f, 0.0f, 0.0f};
            o.transform.scale = {1.0f, 1.0f, 1.0f};
            o.hasMeshRenderer = true;
            o.meshRenderer.color = {1.0f, 1.0f, 1.0f, 1.0f};
            o.meshFilter.sourcePath = importPath;
            if (centerPivot) {
                o.meshFilter.pivotOffset = meshCenter;
            }
            ApplyMeshInfoToSceneObject(o, info, model);
            o.meshRenderer.materialId = EnsureImportedMaterialAsset(materialManager_, importPath, baseName, info);
            objects_.push_back(std::move(o));
            if (firstImportedIndex < 0) {
                firstImportedIndex = static_cast<int>(objects_.size()) - 1;
            }
        }

        if (firstImportedIndex >= 0) {
            materialManager_.LoadAll();
            Select(firstImportedIndex);
            if (console_) {
                console_->AddLog("Imported mesh: " + importPath + " (" + std::to_string(infos.size()) + " mesh" + (infos.size() != 1 ? "es" : "") + ")");
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
        std::string importPath;
        std::shared_ptr<::Model> model;
        std::vector<ImportedMeshInfo> infos;
        if (!TryLoadMeshAsset(path, importPath, model, infos)) {
            return false;
        }

        PushUndoState();
        SceneObject& obj = objects_[selectedIndex_];
        obj.type = "Mesh";
        obj.hasMeshFilter = true;
        obj.hasMeshRenderer = true;
        obj.meshFilter.sourcePath = importPath;
        ApplyMeshInfoToSceneObject(obj, infos.front(), model);
        std::string baseName;
        try { baseName = fs::path(importPath).stem().string(); } catch (...) { baseName = "Mesh"; }
        obj.meshRenderer.materialId = EnsureImportedMaterialAsset(materialManager_, importPath, baseName, infos.front());
        materialManager_.LoadAll();

        if (console_) {
            console_->AddLog("Replaced Mesh Filter on " + obj.name + " with " + importPath);
        }
        RefreshProjectFiles();
        if (onDirty_) onDirty_();
        return true;
    } catch (...) {
        if (console_) {
            console_->AddLog("Failed to replace Mesh Filter with mesh asset: " + path);
        }
        return false;
    }
}

bool SceneEditor::ReplaceSelectedMeshWithPlane() {
    return ReplaceSelectedMeshWithBuiltIn("Plane");
}

bool SceneEditor::ReplaceSelectedMeshWithBuiltIn(const std::string& meshType) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size()) || meshType.empty()) {
        return false;
    }

    PushUndoState();
    SceneObject& obj = objects_[selectedIndex_];
    obj.type = "GameObject";
    obj.hasMeshFilter = true;
    obj.hasMeshRenderer = true;
    if (!ConfigureBuiltInPrimitive(obj, meshType, builtInPrimitiveMeshes_)) {
        return false;
    }
    if (console_) {
        console_->AddLog("Replaced Mesh Filter on " + obj.name + " with built-in " + meshType);
    }
    if (onDirty_) onDirty_();
    return true;
}

void SceneEditor::BeginProjectFileRename(const std::string& path) {
    if (path.empty()) {
        return;
    }
    SelectProjectFile(path);
    renamingProjectFile_ = path;
    std::string editableName = fs::path(path).filename().string();
    if (IsSceneAssetPath(path)) {
        editableName = ProjectAssetDisplayFilename(path);
        const std::string displaySuffix = ".scene";
        if (EndsWith(ToLowerCopy(editableName), displaySuffix)) {
            editableName.resize(editableName.size() - displaySuffix.size());
        }
    } else if (IsMaterialAssetPath(path)) {
        editableName = MaterialIdFromAssetPath(path);
    }
    std::snprintf(projectRenameBuffer_, sizeof(projectRenameBuffer_), "%s", editableName.c_str());
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
    if (IsSceneAssetPath(oldProjectPath)) {
        const std::string sceneSuffix = ".scene";
        const std::string storageSuffix = ".scene.json";
        if (EndsWith(ToLowerCopy(newFilename), storageSuffix)) {
            // Keep explicit full names working.
        } else {
            if (EndsWith(ToLowerCopy(newFilename), sceneSuffix)) {
                newFilename.resize(newFilename.size() - sceneSuffix.size());
            }
            newFilename += storageSuffix;
        }
    } else if (IsMaterialAssetPath(oldProjectPath)) {
        const std::string displaySuffix = ".mat";
        const std::string storageSuffix = ".mat.json";
        if (EndsWith(ToLowerCopy(newFilename), storageSuffix)) {
            // Keep explicit full names working.
        } else {
            if (EndsWith(ToLowerCopy(newFilename), displaySuffix)) {
                newFilename.resize(newFilename.size() - displaySuffix.size());
            }
            newFilename += storageSuffix;
        }
    }

    const fs::path oldAbsolutePath = ProjectAssetPathToAbsolute(oldProjectPath);
    const bool oldWasDirectory = fs::exists(oldAbsolutePath) && fs::is_directory(oldAbsolutePath);
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

        if (oldWasDirectory) {
            const std::string oldPrefix = oldProjectPath + "/";
            const std::string newPrefix = newProjectPath + "/";
            auto rewriteProjectPath = [&](std::string& value) {
                value = NormalizeSlashes(value);
                if (value == oldProjectPath) {
                    value = newProjectPath;
                } else if (value.rfind(oldPrefix, 0) == 0) {
                    value = newPrefix + value.substr(oldPrefix.size());
                }
            };

            rewriteProjectPath(defaultScenePath_);
            rewriteProjectPath(lastScenePath_);
            rewriteProjectPath(savePath_);
            rewriteProjectPath(selectedProjectDirectory_);
            rewriteProjectPath(selectedProjectFile_);
            for (auto& object : objects_) {
                rewriteProjectPath(object.meshFilter.sourcePath);
                rewriteProjectPath(object.meshFilter.diffuseTexturePath);
                rewriteProjectPath(object.vehicle.configPath);
                for (ObjectScriptAttachment& script : object.scriptComponent.attachments) {
                    rewriteProjectPath(script.scriptPath);
                }
            }
            materialManager_.LoadAll();
            SyncScriptProjectFiles();
        } else if (IsSceneAssetPath(oldProjectPath) || IsSceneAssetPath(newProjectPath)) {
            UpdateProjectSceneReference(oldProjectPath, IsSceneAssetPath(newProjectPath) ? newProjectPath : std::string{});
        }

        const bool renamedScript = ToLowerCopy(fs::path(newProjectPath).extension().string()) == ".cpp"
            || ToLowerCopy(fs::path(newProjectPath).extension().string()) == ".h";
        if (renamedScript) {
            SyncScriptProjectFiles();
        }

        const std::string oldMaterialId = MaterialIdFromAssetPath(oldProjectPath);
        const std::string newMaterialId = MaterialIdFromAssetPath(newProjectPath);
        if (IsMaterialAssetPath(oldProjectPath) && IsMaterialAssetPath(newProjectPath)) {
            for (auto& object : objects_) {
                if (object.meshRenderer.materialId == oldMaterialId) {
                    object.meshRenderer.materialId = newMaterialId;
                }
            }
            if (inspectedMaterialId_ == oldMaterialId) {
                inspectedMaterialId_ = newMaterialId;
            }
            materialManager_.LoadAll();
        }

        for (auto& object : objects_) {
            if (!oldWasDirectory && NormalizeSlashes(object.meshFilter.sourcePath) == oldProjectPath) {
                object.meshFilter.sourcePath = newProjectPath;
            }
            if (!oldWasDirectory && NormalizeSlashes(object.meshFilter.diffuseTexturePath) == oldProjectPath) {
                object.meshFilter.diffuseTexturePath = newProjectPath;
            }
            if (!oldWasDirectory && NormalizeSlashes(object.vehicle.configPath) == oldProjectPath) {
                object.vehicle.configPath = newProjectPath;
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
    const bool isSceneAsset = IsSceneAssetPath(projectPath);
    const bool isScriptAsset = ParentProjectDirectory(projectPath) == "assets/scripts"
        && (ToLowerCopy(fs::path(projectPath).extension().string()) == ".cpp"
            || ToLowerCopy(fs::path(projectPath).extension().string()) == ".h");

    try {
        if (!fs::exists(absolutePath) || !fs::is_regular_file(absolutePath)) {
            return;
        }

        fs::remove(absolutePath);

        if (isSceneAsset) {
            UpdateProjectSceneReference(projectPath, "");
        }

        if (isScriptAsset) {
            const std::string scriptName = absolutePath.stem().string();
            for (auto& object : objects_) {
                object.scriptComponent.attachments.erase(
                    std::remove_if(object.scriptComponent.attachments.begin(), object.scriptComponent.attachments.end(), [&](const ObjectScriptAttachment& script) {
                        return script.scriptName == scriptName || NormalizeSlashes(script.scriptPath) == projectPath;
                    }),
                    object.scriptComponent.attachments.end());
            }
            SyncScriptProjectFiles();
            if (scriptsRunning_) {
                RebuildScriptRuntime();
            }
        }

        if (IsMaterialAssetPath(projectPath)) {
            for (auto& object : objects_) {
                if (object.meshRenderer.materialId == materialId) {
                    object.meshRenderer.materialId = "pbr_default";
                }
            }
            if (inspectedMaterialId_ == materialId) {
                inspectMaterial_ = false;
                inspectedMaterialId_.clear();
            }
            materialManager_.LoadAll();
        }

        for (auto& object : objects_) {
            if (NormalizeSlashes(object.meshFilter.sourcePath) == projectPath) {
                object.meshFilter.sourcePath.clear();
                object.meshFilter.vao = 0;
                object.meshFilter.indexCount = 0;
                object.meshFilter.modelRef.reset();
            }
            if (NormalizeSlashes(object.meshFilter.diffuseTexturePath) == projectPath) {
                object.meshFilter.diffuseTexturePath.clear();
                object.meshFilter.diffuseTextureId = 0;
            }
            if (NormalizeSlashes(object.vehicle.configPath) == projectPath) {
                object.vehicle.configPath.clear();
                object.vehicle.wheelBindings.clear();
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
        if (isScriptAsset) {
            Save(savePath_);
        }
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to delete project file: " + projectPath);
        }
    }
}

void SceneEditor::DeleteProjectFolder(const std::string& path) {
    const std::string projectPath = NormalizeSlashes(path);
    if (projectPath.empty() || projectPath == "assets") {
        return;
    }

    try {
        const fs::path assetsRoot = FindAssetsRoot();
        const fs::path absolutePath = ProjectAssetPathToAbsolute(projectPath);
        if (!IsUnderPath(absolutePath, assetsRoot) || !fs::exists(absolutePath) || !fs::is_directory(absolutePath)) {
            return;
        }
        if (!fs::is_empty(absolutePath)) {
            if (console_) {
                console_->AddError("Folder is not empty: " + projectPath);
            }
            return;
        }

        fs::remove(absolutePath);
        if (selectedProjectDirectory_ == projectPath) {
            selectedProjectDirectory_ = ParentProjectDirectory(projectPath);
        }
        if (selectedProjectFile_ == projectPath) {
            selectedProjectFile_.clear();
        }
        if (renamingProjectFile_ == projectPath) {
            renamingProjectFile_.clear();
        }
        RefreshProjectFiles();
        SaveProject();
        if (console_) {
            console_->AddLog("Deleted folder: " + projectPath);
        }
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to delete folder: " + projectPath);
        }
    }
}

bool SceneEditor::CreateProjectFolder(const std::string& requestedName) {
    std::string folderName = SanitizeFolderName(requestedName);
    if (folderName.empty()) {
        if (console_) {
            console_->AddError("Folder name cannot be empty.");
        }
        return false;
    }

    try {
        const fs::path assetsRoot = FindAssetsRoot();
        const fs::path baseDirectory = ProjectAssetPathToAbsolute(selectedProjectDirectory_);
        fs::path folderPath = (baseDirectory / fs::path(folderName)).lexically_normal();
        if (!IsUnderPath(folderPath, assetsRoot)) {
            if (console_) {
                console_->AddError("Folder creation blocked outside assets: " + folderName);
            }
            return false;
        }

        fs::path uniqueFolderPath = folderPath;
        for (int i = 1; fs::exists(uniqueFolderPath) && i < 10000; ++i) {
            uniqueFolderPath = (baseDirectory / fs::path(folderName + "_" + std::to_string(i))).lexically_normal();
        }
        if (fs::exists(uniqueFolderPath)) {
            if (console_) {
                console_->AddError("Folder already exists: " + ToProjectAssetPath(uniqueFolderPath, assetsRoot));
            }
            return false;
        }

        fs::create_directories(uniqueFolderPath);
        selectedProjectDirectory_ = ToProjectAssetPath(uniqueFolderPath, assetsRoot);
        RefreshProjectFiles();
        SaveProject();
        if (console_) {
            console_->AddLog("Created folder: " + selectedProjectDirectory_);
        }
        return true;
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to create folder: " + folderName);
        }
        return false;
    }
}

bool SceneEditor::MoveProjectFile(const std::string& path, const std::string& targetDirectory) {
    const std::string oldProjectPath = NormalizeSlashes(path);
    const std::string targetProjectDirectory = NormalizeSlashes(targetDirectory.empty() ? std::string("assets") : targetDirectory);
    if (oldProjectPath.empty() || ParentProjectDirectory(oldProjectPath) == targetProjectDirectory) {
        return false;
    }

    try {
        const fs::path assetsRoot = FindAssetsRoot();
        const fs::path oldAbsolutePath = ProjectAssetPathToAbsolute(oldProjectPath);
        const fs::path targetAbsoluteDirectory = ProjectAssetPathToAbsolute(targetProjectDirectory);
        const fs::path newAbsolutePath = (targetAbsoluteDirectory / oldAbsolutePath.filename()).lexically_normal();
        const std::string oldExtension = ToLowerCopy(oldAbsolutePath.extension().string());
        const bool movedScript = oldExtension == ".cpp" || oldExtension == ".h";
        const fs::path oldScriptSiblingPath = movedScript
            ? (oldAbsolutePath.parent_path() / (oldAbsolutePath.stem().string() + (oldExtension == ".h" ? ".cpp" : ".h"))).lexically_normal()
            : fs::path{};
        const fs::path newScriptSiblingPath = movedScript
            ? (targetAbsoluteDirectory / oldScriptSiblingPath.filename()).lexically_normal()
            : fs::path{};
        if (!IsUnderPath(newAbsolutePath, assetsRoot) || !fs::exists(oldAbsolutePath) || !fs::is_regular_file(oldAbsolutePath)) {
            return false;
        }
        if (fs::exists(newAbsolutePath)) {
            if (console_) {
                console_->AddError("Move blocked because file already exists: " + ToProjectAssetPath(newAbsolutePath, assetsRoot));
            }
            return false;
        }
        if (movedScript && fs::exists(oldScriptSiblingPath) && fs::exists(newScriptSiblingPath)) {
            if (console_) {
                console_->AddError("Move blocked because script pair target already exists: " + ToProjectAssetPath(newScriptSiblingPath, assetsRoot));
            }
            return false;
        }

        fs::create_directories(targetAbsoluteDirectory);
        fs::rename(oldAbsolutePath, newAbsolutePath);
        if (movedScript && fs::exists(oldScriptSiblingPath) && IsUnderPath(newScriptSiblingPath, assetsRoot)) {
            fs::rename(oldScriptSiblingPath, newScriptSiblingPath);
        }

        const std::string newProjectPath = ToProjectAssetPath(newAbsolutePath, assetsRoot);
        const std::string oldScriptSourceProjectPath = movedScript
            ? ToProjectAssetPath(oldExtension == ".cpp" ? oldAbsolutePath : oldScriptSiblingPath, assetsRoot)
            : std::string{};
        const std::string newScriptSourceProjectPath = movedScript
            ? ToProjectAssetPath(oldExtension == ".cpp" ? newAbsolutePath : newScriptSiblingPath, assetsRoot)
            : std::string{};
        if (IsSceneAssetPath(oldProjectPath) || IsSceneAssetPath(newProjectPath)) {
            UpdateProjectSceneReference(oldProjectPath, IsSceneAssetPath(newProjectPath) ? newProjectPath : std::string{});
        }

        if (movedScript) {
            const fs::path movedHeaderPath = oldExtension == ".h" ? newAbsolutePath : newScriptSiblingPath;
            if (!movedHeaderPath.empty() && fs::exists(movedHeaderPath)) {
                std::string header;
                {
                    std::ifstream in(movedHeaderPath);
                    if (in.good()) {
                        std::stringstream buffer;
                        buffer << in.rdbuf();
                        header = buffer.str();
                    }
                }
                if (!header.empty()) {
                    const std::string objectScriptInclude = NormalizeSlashes(fs::relative(FindEngineRoot() / "src" / "scripting" / "ObjectScript.h", movedHeaderPath.parent_path()).string());
                    const std::string includeLine = "#include \"" + objectScriptInclude + "\"";
                    if (header.find("#include \"") != std::string::npos && header.find("src/scripting/ObjectScript.h") != std::string::npos) {
                        std::stringstream in(header);
                        std::string line;
                        std::string rewritten;
                        while (std::getline(in, line)) {
                            if (line.find("src/scripting/ObjectScript.h") != std::string::npos) {
                                rewritten += includeLine;
                            } else {
                                rewritten += line;
                            }
                            rewritten += "\n";
                        }
                        fs::create_directories(movedHeaderPath.parent_path());
                        std::ofstream out(movedHeaderPath, std::ios::trunc);
                        out << rewritten;
                    }
                }
            }
            SyncScriptProjectFiles();
        }

        if (IsMaterialAssetPath(oldProjectPath) && IsMaterialAssetPath(newProjectPath)) {
            materialManager_.LoadAll();
        }

        for (auto& object : objects_) {
            if (NormalizeSlashes(object.meshFilter.sourcePath) == oldProjectPath) {
                object.meshFilter.sourcePath = newProjectPath;
            }
            if (NormalizeSlashes(object.meshFilter.diffuseTexturePath) == oldProjectPath) {
                object.meshFilter.diffuseTexturePath = newProjectPath;
            }
            if (NormalizeSlashes(object.vehicle.configPath) == oldProjectPath) {
                object.vehicle.configPath = newProjectPath;
            }
            for (ObjectScriptAttachment& script : object.scriptComponent.attachments) {
                const std::string normalizedScriptPath = NormalizeSlashes(script.scriptPath);
                if (normalizedScriptPath == oldProjectPath || (movedScript && normalizedScriptPath == oldScriptSourceProjectPath)) {
                    script.scriptPath = movedScript ? newScriptSourceProjectPath : newProjectPath;
                }
            }
        }

        selectedProjectDirectory_ = targetProjectDirectory;
        selectedProjectFile_ = movedScript && oldExtension == ".h" && fs::exists(newScriptSiblingPath) ? newScriptSourceProjectPath : newProjectPath;
        RefreshProjectFiles();
        SaveProject();
        if (console_) {
            console_->AddLog("Moved project file: " + oldProjectPath + " -> " + newProjectPath);
        }
        if (onDirty_) onDirty_();
        return true;
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to move project file: " + oldProjectPath);
        }
        return false;
    }
}


bool SceneEditor::CopyProjectFileTo(const std::string& sourcePath, const std::string& targetDirectory) {
    const std::string srcProjectPath = NormalizeSlashes(sourcePath);
    const std::string targetProjectDir = NormalizeSlashes(targetDirectory.empty() ? std::string("assets") : targetDirectory);
    if (srcProjectPath.empty()) return false;

    try {
        const fs::path assetsRoot  = FindAssetsRoot();
        const fs::path srcAbsolute = ProjectAssetPathToAbsolute(srcProjectPath);
        const fs::path targetAbsDir = ProjectAssetPathToAbsolute(targetProjectDir);

        if (!IsUnderPath(srcAbsolute, assetsRoot) || !fs::exists(srcAbsolute) || !fs::is_regular_file(srcAbsolute)) {
            return false;
        }

        // Build a unique destination filename: "stem (1).ext", "stem (2).ext", …
        const std::string stem = srcAbsolute.stem().string();
        const std::string ext  = srcAbsolute.extension().string();
        fs::create_directories(targetAbsDir);

        fs::path destAbsolute = targetAbsDir / srcAbsolute.filename();
        if (fs::exists(destAbsolute)) {
            for (int n = 1; ; ++n) {
                destAbsolute = targetAbsDir / (stem + " (" + std::to_string(n) + ")" + ext);
                if (!fs::exists(destAbsolute)) break;
            }
        }

        if (!IsUnderPath(destAbsolute, assetsRoot)) return false;

        fs::copy_file(srcAbsolute, destAbsolute);

        const std::string destProjectPath = ToProjectAssetPath(destAbsolute, assetsRoot);
        selectedProjectDirectory_ = targetProjectDir;
        selectedProjectFile_ = destProjectPath;
        RefreshProjectFiles();
        if (console_) console_->AddLog("Copied " + srcProjectPath + " -> " + destProjectPath);
        return true;
    } catch (...) {
        if (console_) console_->AddError("Failed to copy project file: " + srcProjectPath);
        return false;
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
            const bool aSpecial = IsSceneAssetPath(a) || IsMeshAssetPath(a) || IsMaterialAssetPath(a);
            const bool bSpecial = IsSceneAssetPath(b) || IsMeshAssetPath(b) || IsMaterialAssetPath(b);
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
            if (IsSupportedMeshExtension(ext)) {
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


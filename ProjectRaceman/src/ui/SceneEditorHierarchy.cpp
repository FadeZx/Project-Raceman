#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>
#include <unordered_set>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

Transform TransformFromMatrixLocal(const glm::mat4& matrix) {
    Transform transform;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::quat orientation;
    if (glm::decompose(matrix, transform.scale, orientation, transform.position, skew, perspective)) {
        transform.rotationEuler = glm::degrees(glm::eulerAngles(orientation));
    }
    return transform;
}

std::string BuildHierarchyDragPayload(const std::vector<SceneObject>& objects, const std::vector<int>& selectedIndices) {
    std::string payload;
    bool first = true;
    for (int index : selectedIndices) {
        if (index < 0 || index >= static_cast<int>(objects.size())) {
            continue;
        }
        if (!first) {
            payload.push_back('\n');
        }
        payload += objects[index].id;
        first = false;
    }
    return payload;
}

std::vector<std::string> ParseHierarchyDragPayload(const void* data, int dataSize) {
    std::vector<std::string> ids;
    if (data == nullptr || dataSize <= 0) {
        return ids;
    }

    const char* bytes = static_cast<const char*>(data);
    std::string current;
    current.reserve(64);
    for (int i = 0; i < dataSize; ++i) {
        const char ch = bytes[i];
        if (ch == '\0') {
            break;
        }
        if (ch == '\n') {
            if (!current.empty()) {
                ids.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        ids.push_back(current);
    }
    return ids;
}

std::vector<int> ResolveIndicesFromIds(const std::vector<SceneObject>& objects, const std::vector<std::string>& ids) {
    std::vector<int> indices;
    indices.reserve(ids.size());
    for (const std::string& id : ids) {
        const auto it = std::find_if(objects.begin(), objects.end(), [&](const SceneObject& object) {
            return object.id == id;
        });
        if (it == objects.end()) {
            continue;
        }
        indices.push_back(static_cast<int>(std::distance(objects.begin(), it)));
    }
    return indices;
}

std::vector<int> SortedSelectedIndices(const std::vector<int>& selectedIndices, int fallbackIndex) {
    std::vector<int> result = selectedIndices;
    if (result.empty() && fallbackIndex >= 0) {
        result.push_back(fallbackIndex);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace

void SceneEditor::RenderScenePanel() {
    if (IsPanelHiddenByFullscreen("Scene")) {
        scenePanelHovered_ = false;
        scenePanelFocused_ = false;
        return;
    }

    ApplyPanelFullscreenWindowSetup("Scene");
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | PanelFullscreenWindowFlags("Scene");
    if (ImGui::Begin("Scene", nullptr, windowFlags)) {
        HandlePanelHeadingDoubleClick("Scene");
        scenePanelHovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        scenePanelFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        hierarchyKeyboardTargetObjectId_.clear();
        auto sanitizeHierarchyCycles = [&]() {
            bool changed = false;
            bool warned = false;
            for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                if (objects_[i].parentId.empty()) {
                    continue;
                }

                std::unordered_set<std::string> ancestry;
                ancestry.insert(objects_[i].id);
                std::string currentParentId = objects_[i].parentId;
                while (!currentParentId.empty()) {
                    if (!ancestry.insert(currentParentId).second) {
                        objects_[i].parentId.clear();
                        changed = true;
                        warned = true;
                        break;
                    }

                    const int parentIndex = FindObjectIndexById(currentParentId);
                    if (parentIndex < 0 || parentIndex >= static_cast<int>(objects_.size())) {
                        break;
                    }
                    currentParentId = objects_[parentIndex].parentId;
                }
            }

            if (changed && onDirty_) {
                onDirty_();
            }
            if (warned && console_) {
                console_->AddWarning("Detected and removed an invalid hierarchy cycle.");
            }
        };
        sanitizeHierarchyCycles();

        auto getObjStartDirectory = [&]() {
            std::string directory = selectedProjectDirectory_.empty() ? std::string("assets") : NormalizeSlashes(selectedProjectDirectory_);
            const fs::path absoluteDirectory = ProjectAssetPathToAbsolute(directory);
            if (fs::exists(absoluteDirectory) && fs::is_directory(absoluteDirectory)) {
                return absoluteDirectory.string();
            }
            return ProjectAssetPathToAbsolute("assets").string();
        };

        auto addEmptyObjectAsChild = [&](int parentIndex) {
            if (parentIndex < 0 || parentIndex >= static_cast<int>(objects_.size())) {
                AddEmptyObject();
                return;
            }

            PushUndoState();
            SceneObject object;
            object.id = MakeId("gameobject");
            object.name = "GameObject";
            object.type = "GameObject";
            object.parentId = objects_[parentIndex].id;
            object.hasMeshFilter = false;
            object.hasMeshRenderer = false;
            object.hasScriptComponent = false;

            const std::string newObjectId = object.id;
            const std::string parentId = object.parentId;
            objects_.push_back(std::move(object));
            hierarchyOpenStates_[parentId] = true;
            Select(static_cast<int>(objects_.size()) - 1);
            pendingHierarchyRevealObjectId_ = newObjectId;
            renamingObjectIndex_ = -1;
            inspectMaterial_ = false;
            if (console_) {
                console_->AddLog("Added child GameObject.");
            }
            if (onDirty_) onDirty_();
        };

        auto renderAddMenuItems = [&]() {
            if (ImGui::MenuItem("Empty GameObject")) {
                AddEmptyObject();
            }
            if (ImGui::BeginMenu("Mesh")) {
                if (ImGui::MenuItem("Plane")) {
                    AddPlane();
                }
                if (ImGui::MenuItem("Cube")) {
                    AddBuiltInPrimitiveObject("Cube");
                }
                if (ImGui::MenuItem("Sphere")) {
                    AddBuiltInPrimitiveObject("Sphere");
                }
                if (ImGui::MenuItem("Cone")) {
                    AddBuiltInPrimitiveObject("Cone");
                }
                if (ImGui::MenuItem("Capsule")) {
                    AddBuiltInPrimitiveObject("Capsule");
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Camera")) {
                AddCameraObject();
            }
            if (ImGui::MenuItem("Track Generator")) {
                AddTrackGeneratorObject();
            }
            if (ImGui::BeginMenu("Light")) {
                if (ImGui::MenuItem("Directional Light")) {
                    AddLightObject(LightType::Directional);
                }
                if (ImGui::MenuItem("Point Light")) {
                    AddLightObject(LightType::Point);
                }
                if (ImGui::MenuItem("Spot Light")) {
                    AddLightObject(LightType::Spot);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Model")) {
                if (ImGui::MenuItem("Import Mesh")) {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
                    std::string selected = OpenMeshFileDialogWin32(getObjStartDirectory());
                    if (!selected.empty()) {
                        ImportObj(selected);
                    }
#else
                    objScanDir_ = NormalizeSlashes(selectedProjectDirectory_.empty() ? std::string("assets") : selectedProjectDirectory_);
                    std::snprintf(importPath_, sizeof(importPath_), "%s", objScanDir_.c_str());
                    ScanObjDir(objScanDir_);
                    objSelectIndex_ = objFiles_.empty() ? -1 : 0;
                    showImportObjPopup_ = true;
#endif
                }
                ImGui::EndMenu();
            }
        };

        const bool isPlaying = scriptsRunning_ && !scriptsPaused_;
        const char* playPauseIcon = !scriptsRunning_
            ? "control-play.png"
            : (scriptsPaused_ ? "control-resume.png" : "control-pause.png");
        const unsigned int playPauseTexture = GetComponentIconTexture(playPauseIcon);
        const unsigned int stopTexture = GetComponentIconTexture("control-stop.png");
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
        const bool playPressed = playPauseTexture != 0
            ? ImGui::ImageButton("##PlayPause", static_cast<ImTextureID>(playPauseTexture), ImVec2(18.0f, 18.0f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f))
            : ImGui::Button(!scriptsRunning_ ? "Play" : (scriptsPaused_ ? "Resume" : "Pause"), ImVec2(74.0f, 0.0f));
        if (playPressed) {
            if (scriptsRunning_) {
                SetScriptsPaused(!scriptsPaused_);
            } else {
                SetScriptsRunning(true);
            }
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", !scriptsRunning_ ? "Start play mode" : (isPlaying ? "Pause play mode" : "Resume play mode"));
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!scriptsRunning_);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
        const bool stopPressed = stopTexture != 0
            ? ImGui::ImageButton("##Stop", static_cast<ImTextureID>(stopTexture), ImVec2(18.0f, 18.0f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f))
            : ImGui::Button("Stop", ImVec2(62.0f, 0.0f));
        if (stopPressed) {
            SetScriptsRunning(false);
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Stop");
        }
        ImGui::SameLine(0.0f, 7.0f);
        if (ImGui::Button("Add", ImVec2(48.0f, 0.0f))) {
            ImGui::OpenPopup("SceneAddPopup");
        }
        if (ImGui::BeginPopup("SceneAddPopup")) {
            renderAddMenuItems();
            ImGui::EndPopup();
        }
        ImGui::Separator();
        if (IsGameViewActive()) {
            glm::mat4 view;
            glm::mat4 proj;
            if (!TryGetGameCamera(view, proj, 1.0f)) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "No Camera");
                ImGui::TextDisabled("Add a Camera object or Camera component to render Game view.");
            }
        }
        ImGui::Separator();

        // Trigger popup if requested
        if (showImportObjPopup_) { ImGui::OpenPopup("Import Mesh"); showImportObjPopup_ = false; }
        if (ImGui::BeginPopupModal("Import Mesh", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Select a supported mesh file to import");
            ImGui::InputText("Directory", importPath_, sizeof(importPath_));
            ImGui::SameLine();
            if (ImGui::Button("Use")) {
                objScanDir_ = std::string(importPath_[0] ? importPath_ : objScanDir_.c_str());
                ScanObjDir(objScanDir_);
                objSelectIndex_ = objFiles_.empty() ? -1 : 0;
            }
            ImGui::Separator();
            const float listHeight = 200.0f;
            if (ImGui::BeginListBox("##objlist", ImVec2(500.0f, listHeight))) {
                for (int i = 0; i < static_cast<int>(objFiles_.size()); ++i) {
                    const bool selected = (i == objSelectIndex_);
                    if (ImGui::Selectable(objFiles_[i].c_str(), selected)) {
                        objSelectIndex_ = i;
                    }
                }
                ImGui::EndListBox();
            }
            ImGui::Separator();
            if (ImGui::Button("Import Selected")) {
                if (objSelectIndex_ >= 0 && objSelectIndex_ < static_cast<int>(objFiles_.size())) {
                    const std::string fullPath = (std::filesystem::path(objScanDir_) / objFiles_[objSelectIndex_]).string();
                    ImportObj(fullPath);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        bool hierarchyChanged = false;
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kMeshAssetPayload)) {
                const char* projectObjPath = static_cast<const char*>(payload->Data);
                if (projectObjPath != nullptr && projectObjPath[0] != '\0') {
                    ImportObj(projectObjPath);
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                const char* projectObjPath = static_cast<const char*>(payload->Data);
                if (projectObjPath != nullptr && projectObjPath[0] != '\0') {
                    if (IsPrefabAssetPath(projectObjPath)) {
                        InstantiatePrefab(projectObjPath);
                        hierarchyChanged = true;
                    } else if (IsMeshAssetPath(projectObjPath)) {
                        ImportObj(projectObjPath);
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kModelChildAssetPayload)) {
                std::string projectObjPath;
                int meshIndex = -1;
                if (ParseModelChildAssetPayload(payload->Data, payload->DataSize, projectObjPath, meshIndex)) {
                    ImportModelChild(projectObjPath, meshIndex);
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjAssetPayload)) {
                const char* projectObjPath = static_cast<const char*>(payload->Data);
                if (projectObjPath != nullptr && projectObjPath[0] != '\0') {
                    ImportObj(projectObjPath);
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                if (payload->DataSize == sizeof(int)) {
                    int childIndex = *static_cast<const int*>(payload->Data);
                    SetParent(childIndex, -1);
                    hierarchyChanged = true;
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyMultiObjectPayload)) {
                const std::vector<std::string> ids = ParseHierarchyDragPayload(payload->Data, payload->DataSize);
                const std::vector<int> indices = ResolveIndicesFromIds(objects_, ids);
                for (int childIndex : indices) {
                    SetParent(childIndex, -1);
                }
                if (!indices.empty()) {
                    hierarchyChanged = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginChild("SceneHierarchyTree", ImVec2(0.0f, 0.0f), false)) {
            bool hierarchyDeleted = false;
            std::function<void(int, int, std::unordered_set<std::string>&)> renderObjectRow = [&](int i, int depth, std::unordered_set<std::string>& renderPath) {
                if (hierarchyDeleted || hierarchyChanged) {
                    return;
                }
                const std::string objectId = objects_[i].id;
                if (!renderPath.insert(objectId).second) {
                    if (console_) {
                        console_->AddWarning("Skipped rendering an invalid recursive hierarchy branch.");
                    }
                    return;
                }
                const bool selected = IsSelected(i);
                std::vector<int> children;
                children.reserve(objects_.size());
                for (int childIndex = 0; childIndex < static_cast<int>(objects_.size()); ++childIndex) {
                    if (objects_[childIndex].parentId == objects_[i].id) {
                        children.push_back(childIndex);
                    }
                }

                ImGui::PushID(i);
                const ImVec2 rowBgMin = ImGui::GetCursorScreenPos();
                const float rowBgWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
                if (depth > 0) {
                    const float depthAlpha = (std::min)(0.11f, 0.045f + depth * 0.018f);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(
                        rowBgMin,
                        ImVec2(rowBgMin.x + rowBgWidth, rowBgMin.y + ImGui::GetFrameHeight()),
                        IM_COL32(255, 255, 255, static_cast<int>(depthAlpha * 255.0f)),
                        2.0f);
                }
                if (renamingObjectIndex_ == i) {
                    if (focusObjectRename_) {
                        ImGui::SetKeyboardFocusHere();
                        focusObjectRename_ = false;
                    }
                    const bool enterPressed = ImGui::InputText("##objectRename", objectRenameBuffer_, sizeof(objectRenameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
                        PushUndoState();
                        objects_[i].name = objectRenameBuffer_;
                        renamingObjectIndex_ = -1;
                        if (onDirty_) onDirty_();
                    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        renamingObjectIndex_ = -1;
                    }
                } else {
                    bool objectEnabled = objects_[i].enabled;
                    if (ImGui::Checkbox("##objectEnabled", &objectEnabled)) {
                        PushUndoState();
                        objects_[i].enabled = objectEnabled;
                        if (onDirty_) onDirty_();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Enable Object");
                    }
                    ImGui::SameLine();
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (children.empty()) {
                        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    }
                    if (selected) {
                        flags |= ImGuiTreeNodeFlags_Selected;
                    }
                    if (!children.empty()) {
                        bool openState = true;
                        const auto stateIt = hierarchyOpenStates_.find(objectId);
                        if (stateIt != hierarchyOpenStates_.end()) {
                            openState = stateIt->second;
                        }
                        if (pendingHierarchyToggleObjectId_ == objectId) {
                            openState = !openState;
                            hierarchyOpenStates_[objectId] = openState;
                            pendingHierarchyToggleObjectId_.clear();
                        }
                        ImGui::SetNextItemOpen(openState, ImGuiCond_Always);
                    }
                    const bool open = ImGui::TreeNodeEx(objects_[i].name.c_str(), flags);
                    if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) {
                        hierarchyKeyboardTargetObjectId_ = objectId;
                    }
                    if (!pendingHierarchyRevealObjectId_.empty() && pendingHierarchyRevealObjectId_ == objectId) {
                        ImGui::SetScrollHereY(0.3f);
                        ImGui::SetItemDefaultFocus();
                        pendingHierarchyRevealObjectId_.clear();
                    }
                    if (!children.empty() && ImGui::IsItemToggledOpen()) {
                        hierarchyOpenStates_[objectId] = open;
                    }
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            pendingHierarchySelectIndex_ = i;
                            pendingHierarchySelectToggle_ = false;
                            pendingHierarchySelectRange_ = false;
                            pendingHierarchyRangeAnchor_ = -1;
                            pendingHierarchyFocusObject_ = true;
                            pendingHierarchySelectionDragged_ = false;
                        } else {
                            pendingHierarchySelectIndex_ = i;
                            pendingHierarchySelectToggle_ = ImGui::GetIO().KeyCtrl;
                            pendingHierarchySelectRange_ = ImGui::GetIO().KeyShift;
                            pendingHierarchyRangeAnchor_ = selectedIndex_;
                            pendingHierarchyFocusObject_ = false;
                            pendingHierarchySelectionDragged_ = false;
                        }
                    }
                    if (ImGui::BeginDragDropSource()) {
                        if (pendingHierarchySelectIndex_ == i) {
                            pendingHierarchySelectionDragged_ = true;
                        }
                        const std::vector<int> dragSelection = SortedSelectedIndices(selectedIndices_, i);
                        if (dragSelection.size() > 1 && std::find(dragSelection.begin(), dragSelection.end(), i) != dragSelection.end()) {
                            const std::string payload = BuildHierarchyDragPayload(objects_, dragSelection);
                            ImGui::SetDragDropPayload(kHierarchyMultiObjectPayload, payload.data(), static_cast<int>(payload.size() + 1));
                        } else {
                            ImGui::SetDragDropPayload(kHierarchyObjectPayload, &i, sizeof(i));
                        }
                        ImGui::TextUnformatted(objects_[i].name.c_str());
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                            if (payload->DataSize == sizeof(int)) {
                                int childIndex = *static_cast<const int*>(payload->Data);
                                SetParent(childIndex, i);
                                hierarchyChanged = true;
                            }
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyMultiObjectPayload)) {
                            const std::vector<std::string> ids = ParseHierarchyDragPayload(payload->Data, payload->DataSize);
                            const std::vector<int> indices = ResolveIndicesFromIds(objects_, ids);
                            for (int childIndex : indices) {
                                SetParent(childIndex, i);
                            }
                            if (!indices.empty()) {
                                hierarchyChanged = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::BeginPopupContextItem("##HierarchyObjectCtx")) {
                        if (!IsSelected(i)) {
                            Select(i);
                        }
                        ImGui::TextDisabled("%s", objects_[i].name.c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                            CopySelectedObjectsToClipboard();
                        }
                        const bool canPaste = objectClipboard_.hasValue && !objectClipboard_.objects.empty();
                        if (ImGui::MenuItem("Paste as Child", nullptr, false, canPaste)) {
                            const std::size_t sizeBefore = objects_.size();
                            PasteObjectsFromClipboard();
                            // Reparent newly added root objects to i (undo already captured by Paste)
                            const std::string parentId = (i < (int)objects_.size()) ? objects_[i].id : std::string{};
                            if (!parentId.empty()) {
                                for (int idx = static_cast<int>(sizeBefore); idx < (int)objects_.size(); ++idx) {
                                    if (objects_[idx].parentId.empty()) {
                                        objects_[idx].parentId = parentId;
                                    }
                                }
                            }
                            hierarchyChanged = true;
                        }
                        if (ImGui::MenuItem("Add Empty Child")) {
                            addEmptyObjectAsChild(i);
                            hierarchyChanged = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Rename", "F2")) {
                            BeginObjectRename(i);
                        }
                        if (ImGui::MenuItem("Delete", "Del")) {
                            if (open && !children.empty()) {
                                ImGui::TreePop();
                            }
                            ImGui::EndPopup();
                            DeleteSelectedObject();
                            hierarchyDeleted = true;
                            renderPath.erase(objectId);
                            ImGui::PopID();
                            return;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Make Prefab")) {
                            pendingPrefabObjectIndex_ = i;
                            std::snprintf(savePrefabNameBuffer_, sizeof(savePrefabNameBuffer_), "%s", objects_[i].name.c_str());
                            showSavePrefabPopup_ = true;
                        }
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                        BeginObjectRename(i);
                    }
                    const bool hasChildTree = open && !children.empty();
                    if (selected && ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
                        if (hasChildTree) {
                            ImGui::TreePop();
                        }
                        DeleteSelectedObject();
                        hierarchyDeleted = true;
                        renderPath.erase(objectId);
                        ImGui::PopID();
                        return;
                    }
                    if (hasChildTree) {
                        if (!hierarchyChanged) {
                            for (int childIndex : children) {
                                renderObjectRow(childIndex, depth + 1, renderPath);
                                if (hierarchyChanged) {
                                    break;
                                }
                            }
                        }
                        ImGui::TreePop();
                    }

                    const float reorderHeight = 6.0f;
                    const ImVec2 reorderPos = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##reorderAfter", ImVec2((std::max)(1.0f, ImGui::GetContentRegionAvail().x), reorderHeight));
                    const bool reorderHovered = ImGui::IsItemHovered();
                    if (reorderHovered) {
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        drawList->AddLine(ImVec2(reorderPos.x, reorderPos.y + reorderHeight * 0.5f),
                                          ImVec2(reorderPos.x + ImGui::GetContentRegionAvail().x, reorderPos.y + reorderHeight * 0.5f),
                                          ImGui::GetColorU32(ImGuiCol_DragDropTarget),
                                          2.0f);
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                            if (payload->DataSize == sizeof(int)) {
                                const int childIndex = *static_cast<const int*>(payload->Data);
                                const int newParentIndex = FindObjectIndexById(objects_[i].parentId);
                                if (MoveObjectInHierarchy(childIndex, newParentIndex, i)) {
                                    hierarchyChanged = true;
                                }
                            }
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyMultiObjectPayload)) {
                            const std::vector<std::string> ids = ParseHierarchyDragPayload(payload->Data, payload->DataSize);
                            std::vector<int> indices = ResolveIndicesFromIds(objects_, ids);
                            std::sort(indices.begin(), indices.end());
                            indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
                            const std::string dropTargetId = objects_[i].id;
                            for (int childIndex : indices) {
                                const int currentTargetIndex = FindObjectIndexById(dropTargetId);
                                if (currentTargetIndex < 0) {
                                    break;
                                }
                                const int newParentIndex = FindObjectIndexById(objects_[currentTargetIndex].parentId);
                                if (MoveObjectInHierarchy(childIndex, newParentIndex, currentTargetIndex)) {
                                    hierarchyChanged = true;
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
                ImGui::PopID();
                renderPath.erase(objectId);
            };

            for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                if (hierarchyDeleted || hierarchyChanged) {
                    break;
                }
                if (!objects_[i].parentId.empty() && FindObjectIndexById(objects_[i].parentId) >= 0) {
                    continue;
                }
                std::unordered_set<std::string> renderPath;
                renderObjectRow(i, 0, renderPath);
            }

            const ImVec2 emptySpace = ImGui::GetContentRegionAvail();
            if (emptySpace.y > 0.0f) {
                ImGui::InvisibleButton("##HierarchyEmptyDropTarget", ImVec2((std::max)(1.0f, emptySpace.x), emptySpace.y));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kMeshAssetPayload)) {
                        const char* projectObjPath = static_cast<const char*>(payload->Data);
                        if (projectObjPath != nullptr && projectObjPath[0] != '\0') {
                            ImportObj(projectObjPath);
                        }
                    }
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                        const char* projectObjPath = static_cast<const char*>(payload->Data);
                        if (projectObjPath != nullptr && projectObjPath[0] != '\0') {
                            if (IsPrefabAssetPath(projectObjPath)) {
                                InstantiatePrefab(projectObjPath);
                                hierarchyChanged = true;
                            } else if (IsMeshAssetPath(projectObjPath)) {
                                ImportObj(projectObjPath);
                            }
                        }
                    }
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjAssetPayload)) {
                        const char* projectObjPath = static_cast<const char*>(payload->Data);
                        if (projectObjPath != nullptr && projectObjPath[0] != '\0') {
                            ImportObj(projectObjPath);
                        }
                    }
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                        if (payload->DataSize == sizeof(int)) {
                            int childIndex = *static_cast<const int*>(payload->Data);
                            if (MoveObjectInHierarchy(childIndex, -1, objects_.empty() ? -1 : static_cast<int>(objects_.size()) - 1)) {
                                hierarchyChanged = true;
                            }
                        }
                    }
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyMultiObjectPayload)) {
                        const std::vector<std::string> ids = ParseHierarchyDragPayload(payload->Data, payload->DataSize);
                        std::vector<int> indices = ResolveIndicesFromIds(objects_, ids);
                        std::sort(indices.begin(), indices.end());
                        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
                        for (int childIndex : indices) {
                            if (MoveObjectInHierarchy(childIndex, -1, objects_.empty() ? -1 : static_cast<int>(objects_.size()) - 1)) {
                                hierarchyChanged = true;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::BeginPopupContextItem("##HierarchyEmptyContext")) {
                    ImGui::TextDisabled("Add to Scene");
                    ImGui::Separator();
                    renderAddMenuItems();
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::EndChild();

        if (pendingHierarchySelectIndex_ >= 0) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                pendingHierarchySelectionDragged_ = true;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (!pendingHierarchySelectionDragged_) {
                    if (pendingHierarchySelectRange_ &&
                        pendingHierarchyRangeAnchor_ >= 0 &&
                        pendingHierarchyRangeAnchor_ < static_cast<int>(objects_.size())) {
                        selectedIndices_.clear();
                        const int minIndex = (std::min)(pendingHierarchyRangeAnchor_, pendingHierarchySelectIndex_);
                        const int maxIndex = (std::max)(pendingHierarchyRangeAnchor_, pendingHierarchySelectIndex_);
                        for (int index = minIndex; index <= maxIndex; ++index) {
                            selectedIndices_.push_back(index);
                        }
                        selectedIndex_ = pendingHierarchySelectIndex_;
                        inspectMaterial_ = false;
                        NormalizeSelection();
                    } else if (pendingHierarchySelectToggle_) {
                        ToggleSelect(pendingHierarchySelectIndex_);
                    } else {
                        Select(pendingHierarchySelectIndex_);
                    }
                    if (pendingHierarchyFocusObject_) {
                        RequestFocusSelectedObject();
                    }
                }
                pendingHierarchySelectIndex_ = -1;
                pendingHierarchySelectToggle_ = false;
                pendingHierarchySelectRange_ = false;
                pendingHierarchyRangeAnchor_ = -1;
                pendingHierarchyFocusObject_ = false;
                pendingHierarchySelectionDragged_ = false;
            }
        }

    }
    ImGui::End();
    if (!scenePanelHovered_ && !scenePanelFocused_) {
        hierarchyKeyboardTargetObjectId_.clear();
    }
}


void SceneEditor::DeleteSelectedObject() {
    NormalizeSelection();
    if (selectedIndices_.empty()) {
        return;
    }

    PushUndoState();
    std::unordered_set<std::string> rootDeleteIds;
    for (int index : selectedIndices_) {
        if (index >= 0 && index < static_cast<int>(objects_.size())) {
            rootDeleteIds.insert(objects_[index].id);
        }
    }

    std::unordered_set<std::string> deleteIds = rootDeleteIds;
    bool added = true;
    while (added) {
        added = false;
        for (const SceneObject& object : objects_) {
            if (object.parentId.empty() || deleteIds.find(object.id) != deleteIds.end()) {
                continue;
            }
            if (deleteIds.find(object.parentId) != deleteIds.end()) {
                deleteIds.insert(object.id);
                added = true;
            }
        }
    }

    std::vector<int> indicesToDelete;
    indicesToDelete.reserve(deleteIds.size());
    for (int index = 0; index < static_cast<int>(objects_.size()); ++index) {
        if (deleteIds.find(objects_[index].id) != deleteIds.end()) {
            indicesToDelete.push_back(index);
        }
    }

    std::sort(indicesToDelete.begin(), indicesToDelete.end(), [](int a, int b) { return a > b; });
    if (indicesToDelete.empty()) {
        return;
    }

    for (int index : indicesToDelete) {
        if (index >= 0 && index < static_cast<int>(objects_.size())) {
            objects_.erase(objects_.begin() + index);
        }
    }
    renamingObjectIndex_ = -1;
    inspectMaterial_ = false;
    if (onDirty_) onDirty_();

    if (objects_.empty()) {
        selectedIndex_ = -1;
        selectedIndices_.clear();
        return;
    }

    selectedIndex_ = (std::min)(indicesToDelete.back(), static_cast<int>(objects_.size()) - 1);
    selectedIndices_ = {selectedIndex_};
}


void SceneEditor::BeginObjectRename(int index) {
    if (index < 0 || index >= static_cast<int>(objects_.size())) {
        return;
    }
    Select(index);
    renamingObjectIndex_ = index;
    std::snprintf(objectRenameBuffer_, sizeof(objectRenameBuffer_), "%s", objects_[index].name.c_str());
    focusObjectRename_ = true;
}

} // namespace raceman

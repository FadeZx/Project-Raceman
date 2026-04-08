#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

void SceneEditor::RenderScenePanel() {
    if (ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse)) {
        // Add button with dropdown (Scene panel)
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Add")) {
                if (ImGui::MenuItem("Empty GameObject")) {
                    AddEmptyObject();
                }
                if (ImGui::BeginMenu("Mesh")) {
                    if (ImGui::MenuItem("Plane")) {
                        AddPlane();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Camera")) {
                    AddCameraObject();
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
                    if (ImGui::MenuItem(".obj")) {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
                        // Use native Windows file dialog
                        std::string selected = OpenObjFileDialogWin32();
                        if (!selected.empty()) {
                            ImportObj(selected);
                        }
#else
                        // Fallback: existing ImGui-based directory scanner
                        importPath_[0] = '\0';
                        objScanDir_ = "assets/mesh";
                        ScanObjDir(objScanDir_);
                        objSelectIndex_ = objFiles_.empty() ? -1 : 0;
                        showImportObjPopup_ = true;
#endif
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        if (ImGui::Button(scriptsRunning_ && !scriptsPaused_ ? "||" : ">")) {
            if (scriptsRunning_) {
                SetScriptsPaused(!scriptsPaused_);
            } else {
                SetScriptsRunning(true);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", scriptsRunning_ && !scriptsPaused_ ? "Pause" : "Play");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!scriptsRunning_);
        if (ImGui::Button("[]")) {
            SetScriptsRunning(false);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Stop");
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scene", viewportMode_ == SceneEditorViewportMode::Scene)) {
            viewportMode_ = SceneEditorViewportMode::Scene;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Game", viewportMode_ == SceneEditorViewportMode::Game)) {
            viewportMode_ = SceneEditorViewportMode::Game;
        }
        ImGui::Separator();
        if (viewportMode_ == SceneEditorViewportMode::Game) {
            glm::mat4 view;
            glm::mat4 proj;
            if (!TryGetGameCamera(view, proj, 1.0f)) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "No Camera");
                ImGui::TextDisabled("Add a Camera object or Camera component to render Game view.");
            }
        }
        ImGui::Separator();

        // Trigger popup if requested
        if (showImportObjPopup_) { ImGui::OpenPopup("Import OBJ"); showImportObjPopup_ = false; }
        // Import OBJ popup
        if (ImGui::BeginPopupModal("Import OBJ", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Select a .obj file to import");
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

        constexpr const char* kHierarchyObjectPayload = "SCENE_HIERARCHY_OBJECT_INDEX";
        ImGui::TextDisabled("Drag an object onto another object to parent it.");
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                if (payload->DataSize == sizeof(int)) {
                    int childIndex = *static_cast<const int*>(payload->Data);
                    SetParent(childIndex, -1);
                }
            }
            ImGui::EndDragDropTarget();
        }

        bool hierarchyDeleted = false;
        std::function<void(int)> renderObjectRow = [&](int i) {
            if (hierarchyDeleted) {
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
                const bool open = ImGui::TreeNodeEx(objects_[i].name.c_str(), flags);
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        Select(i);
                        RequestFocusSelectedObject();
                    } else if (ImGui::GetIO().KeyCtrl) {
                        ToggleSelect(i);
                    } else {
                        Select(i);
                    }
                }
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload(kHierarchyObjectPayload, &i, sizeof(i));
                    ImGui::TextUnformatted(objects_[i].name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                        if (payload->DataSize == sizeof(int)) {
                            int childIndex = *static_cast<const int*>(payload->Data);
                            SetParent(childIndex, i);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                    BeginObjectRename(i);
                }
                if (selected && ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
                    DeleteSelectedObject();
                    hierarchyDeleted = true;
                    ImGui::PopID();
                    return;
                }
                if (open && !children.empty()) {
                    for (int childIndex : children) {
                        renderObjectRow(childIndex);
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::PopID();
        };

        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            if (hierarchyDeleted) {
                break;
            }
            if (!objects_[i].parentId.empty() && FindObjectIndexById(objects_[i].parentId) >= 0) {
                continue;
            }
            renderObjectRow(i);
        }

    }
    ImGui::End();
}


void SceneEditor::DeleteSelectedObject() {
    NormalizeSelection();
    if (selectedIndices_.empty()) {
        return;
    }

    PushUndoState();
    std::vector<std::string> deleteIds;
    for (int index : selectedIndices_) {
        if (index >= 0 && index < static_cast<int>(objects_.size())) {
            deleteIds.push_back(objects_[index].id);
        }
    }
    std::vector<int> indices;
    for (int index = 0; index < static_cast<int>(objects_.size()); ++index) {
        const std::string& id = objects_[index].id;
        bool shouldDelete = std::find(deleteIds.begin(), deleteIds.end(), id) != deleteIds.end();
        if (!shouldDelete) {
            for (const std::string& deleteId : deleteIds) {
                if (IsDescendantOf(id, deleteId)) {
                    shouldDelete = true;
                    break;
                }
            }
        }
        if (shouldDelete) {
            indices.push_back(index);
        }
    }
    std::sort(indices.begin(), indices.end(), [](int a, int b) { return a > b; });
    if (indices.empty()) {
        return;
    }
    for (int index : indices) {
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

    selectedIndex_ = (std::min)(indices.back(), static_cast<int>(objects_.size()) - 1);
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


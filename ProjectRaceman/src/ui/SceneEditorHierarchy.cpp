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

} // namespace

void SceneEditor::RenderScenePanel() {
    if (ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse)) {
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
                    if (ImGui::MenuItem("Cube")) {
                        AddBuiltInPrimitiveObject("Cube");
                    }
                    if (ImGui::MenuItem("Sphere")) {
                        AddBuiltInPrimitiveObject("Sphere");
                    }
                    if (ImGui::MenuItem("Cone")) {
                        AddBuiltInPrimitiveObject("Cone");
                    }
                    if (ImGui::MenuItem("Cylinder")) {
                        AddBuiltInPrimitiveObject("Cylinder");
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

        ImGui::TextDisabled("Drag an object onto another object to parent it.");
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
                if (projectObjPath != nullptr && projectObjPath[0] != '\0' && IsMeshAssetPath(projectObjPath)) {
                    ImportObj(projectObjPath);
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
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginChild("SceneHierarchyTree", ImVec2(0.0f, 0.0f), false)) {
            bool hierarchyDeleted = false;
            std::function<void(int, std::unordered_set<std::string>&)> renderObjectRow = [&](int i, std::unordered_set<std::string>& renderPath) {
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
                            pendingHierarchySelectIndex_ = i;
                            pendingHierarchySelectToggle_ = false;
                            pendingHierarchyFocusObject_ = true;
                            pendingHierarchySelectionDragged_ = false;
                        } else {
                            pendingHierarchySelectIndex_ = i;
                            pendingHierarchySelectToggle_ = ImGui::GetIO().KeyCtrl;
                            pendingHierarchyFocusObject_ = false;
                            pendingHierarchySelectionDragged_ = false;
                        }
                    }
                    if (ImGui::BeginDragDropSource()) {
                        if (pendingHierarchySelectIndex_ == i) {
                            pendingHierarchySelectionDragged_ = true;
                        }
                        ImGui::SetDragDropPayload(kHierarchyObjectPayload, &i, sizeof(i));
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
                        ImGui::EndDragDropTarget();
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
                                renderObjectRow(childIndex, renderPath);
                                if (hierarchyChanged) {
                                    break;
                                }
                            }
                        }
                        ImGui::TreePop();
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
                renderObjectRow(i, renderPath);
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
                        if (projectObjPath != nullptr && projectObjPath[0] != '\0' && IsMeshAssetPath(projectObjPath)) {
                            ImportObj(projectObjPath);
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
                    ImGui::EndDragDropTarget();
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
                    if (pendingHierarchySelectToggle_) {
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
                pendingHierarchyFocusObject_ = false;
                pendingHierarchySelectionDragged_ = false;
            }
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
    indices.reserve(deleteIds.size());
    for (int index : selectedIndices_) {
        if (index >= 0 && index < static_cast<int>(objects_.size())) {
            indices.push_back(index);
        }
    }
    std::sort(indices.begin(), indices.end(), [](int a, int b) { return a > b; });
    if (indices.empty()) {
        return;
    }

    for (int deleteIndex : indices) {
        if (deleteIndex < 0 || deleteIndex >= static_cast<int>(objects_.size())) {
            continue;
        }
        const SceneObject& deletingObject = objects_[deleteIndex];
        const std::string parentId = deletingObject.parentId;
        const int parentIndex = FindObjectIndexById(parentId);
        const glm::mat4 parentWorld = parentIndex >= 0 ? GetObjectWorldMatrix(parentIndex) : glm::mat4(1.0f);

        for (int childIndex = 0; childIndex < static_cast<int>(objects_.size()); ++childIndex) {
            if (childIndex == deleteIndex || objects_[childIndex].parentId != deletingObject.id) {
                continue;
            }

            const glm::mat4 childWorld = GetObjectWorldMatrix(childIndex);
            objects_[childIndex].parentId = parentId;
            if (parentIndex >= 0) {
                objects_[childIndex].transform = TransformFromMatrixLocal(glm::inverse(parentWorld) * childWorld);
            } else {
                objects_[childIndex].transform = TransformFromMatrixLocal(childWorld);
            }
        }
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


#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

void SceneEditor::RenderScenePanel() {
    if (ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_MenuBar)) {
        // Add button with dropdown (Scene panel)
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Add")) {
                if (ImGui::BeginMenu("Mesh")) {
                    if (ImGui::MenuItem("Plane")) {
                        AddPlane();
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



        if (ImGui::Button(scriptsRunning_ ? "Pause Scripts" : "Run Scripts")) {
            SetScriptsRunning(!scriptsRunning_);
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

        // List of objects
        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            const bool selected = (i == selectedIndex_);
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
                if (ImGui::Selectable(objects_[i].name.c_str(), selected)) {
                    Select(i);
                }
                if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                    BeginObjectRename(i);
                }
                if (selected && ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
                    DeleteSelectedObject();
                    ImGui::PopID();
                    break;
                }
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::Button("Drop OBJ here to import", ImVec2(-1.0f, 0.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjAssetPayload)) {
                const char* path = static_cast<const char*>(payload->Data);
                if (path != nullptr) {
                    ImportObj(path);
                }
            }
            ImGui::EndDragDropTarget();
        }

    }
    ImGui::End();
}


void SceneEditor::DeleteSelectedObject() {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }

    PushUndoState();
    objects_.erase(objects_.begin() + selectedIndex_);
    renamingObjectIndex_ = -1;
    inspectMaterial_ = false;
    if (onDirty_) onDirty_();

    if (objects_.empty()) {
        selectedIndex_ = -1;
        return;
    }

    if (selectedIndex_ >= static_cast<int>(objects_.size())) {
        selectedIndex_ = static_cast<int>(objects_.size()) - 1;
    }
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


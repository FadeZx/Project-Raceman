#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"
#include "../scripting/ScriptRegistry.h"

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

void SceneEditor::RenderInspectorPanel() {
    if (ImGui::Begin("Inspector")) {
        if (inspectMaterial_) {
            RenderMaterialInspector();
        } else if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size())) {
            SceneObject& obj = objects_[selectedIndex_];

            // Name
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", obj.name.c_str());
            if (ImGui::InputText("Object Name##objectName", nameBuf, sizeof(nameBuf))) {
                if (!inspectorEditActive_) {
                    PushUndoState();
                    inspectorEditActive_ = true;
                }
                obj.name = nameBuf;
                if (onDirty_) onDirty_();
            }
            if (ImGui::IsItemDeactivated()) {
                inspectorEditActive_ = false;
            }

            // Type (read-only)
            ImGui::TextDisabled("Type: %s", obj.type.c_str());

            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                Transform before = obj.transform;
                if (ImGui::DragFloat3("Position", &obj.transform.position.x, 0.1f)) {
                    const glm::vec3 after = obj.transform.position;
                    if (!inspectorEditActive_) {
                        obj.transform = before;
                        PushUndoState();
                        obj.transform.position = after;
                        inspectorEditActive_ = true;
                    }
                    if (onDirty_) onDirty_();
                }
                if (ImGui::IsItemDeactivated()) {
                    inspectorEditActive_ = false;
                }

                before = obj.transform;
                if (ImGui::DragFloat3("Rotation (deg)", &obj.transform.rotationEuler.x, 0.5f)) {
                    const glm::vec3 after = obj.transform.rotationEuler;
                    if (!inspectorEditActive_) {
                        obj.transform = before;
                        PushUndoState();
                        obj.transform.rotationEuler = after;
                        inspectorEditActive_ = true;
                    }
                    if (onDirty_) onDirty_();
                }
                if (ImGui::IsItemDeactivated()) {
                    inspectorEditActive_ = false;
                }

                before = obj.transform;
                if (ImGui::DragFloat3("Scale", &obj.transform.scale.x, 0.1f)) {
                    const glm::vec3 after{
                        (std::max)(obj.transform.scale.x, 0.01f),
                        (std::max)(obj.transform.scale.y, 0.01f),
                        (std::max)(obj.transform.scale.z, 0.01f)
                    };
                    if (!inspectorEditActive_) {
                        obj.transform = before;
                        PushUndoState();
                        obj.transform.scale = after;
                        inspectorEditActive_ = true;
                    } else {
                        obj.transform.scale = after;
                    }
                    if (onDirty_) onDirty_();
                }
                if (ImGui::IsItemDeactivated()) {
                    inspectorEditActive_ = false;
                }
            }

            if (ImGui::CollapsingHeader("Mesh Filter", ImGuiTreeNodeFlags_DefaultOpen)) {
                const std::string meshButtonLabel = (obj.type == "Mesh" && !obj.sourcePath.empty())
                    ? (fs::path(obj.sourcePath).filename().string() + "##selectMeshFilter")
                    : (obj.type + "##selectMeshFilter");
                ImGui::TextDisabled("Mesh Type:");
                ImGui::SameLine();
                if (ImGui::Button(meshButtonLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                    assetPickerMode_ = ProjectAssetPickerMode::ReplaceMesh;
                    ImGui::OpenPopup("Select Project Asset");
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjAssetPayload)) {
                        const char* path = static_cast<const char*>(payload->Data);
                        if (path != nullptr) {
                            ReplaceSelectedMeshFromObj(path);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (obj.type == "Mesh") {
                    ImGui::TextWrapped("Source: %s", obj.sourcePath.empty() ? "(none)" : obj.sourcePath.c_str());
                    ImGui::TextDisabled("Submesh Index: %d", obj.meshIndex);
                    ImGui::TextWrapped("Imported Material: %s", obj.importedMaterialName.empty() ? "(none)" : obj.importedMaterialName.c_str());
                    ImGui::TextWrapped("OBJ Diffuse: %s", obj.diffuseTexturePath.empty() ? "(none)" : obj.diffuseTexturePath.c_str());
                } else {
                    ImGui::TextDisabled("Built-in mesh: %s", obj.type.c_str());
                }
            }

            if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                const std::string materialId = obj.materialId.empty() ? std::string("pbr_default") : obj.materialId;
                const Material* material = materialManager_.Get(materialId);
                std::string materialFilename = materialId + ".mat.json";
                for (const std::string& file : projectFiles_) {
                    if (IsMaterialAssetPath(file) && MaterialIdFromAssetPath(file) == materialId) {
                        materialFilename = fs::path(file).filename().string();
                        break;
                    }
                }
                const std::string materialButtonLabel = materialFilename + "##selectMaterial";
                ImGui::TextDisabled("Material:");
                ImGui::SameLine();
                if (ImGui::Button(materialButtonLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                    assetPickerMode_ = ProjectAssetPickerMode::AssignMaterial;
                    ImGui::OpenPopup("Select Project Asset");
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    OpenMaterialEditor(materialId);
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kMaterialAssetPayload)) {
                        const char* materialIdPayload = static_cast<const char*>(payload->Data);
                        if (materialIdPayload != nullptr) {
                            AssignMaterialToSelected(materialIdPayload);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (material != nullptr) {
                    ImGui::ColorButton("Albedo Preview", ImVec4(material->albedoColor[0], material->albedoColor[1], material->albedoColor[2], material->albedoColor[3]));
                } else {
                    ImGui::TextDisabled("Material asset not loaded.");
                }
            }

            if (ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("Add Component")) {
                    ImGui::OpenPopup("Add Script Component");
                }
                if (ImGui::BeginPopup("Add Script Component")) {
                    if (ImGui::BeginMenu("Script from Project")) {
                        const auto& registeredScripts = GetRegisteredScripts();
                        if (registeredScripts.empty()) {
                            ImGui::TextDisabled("No registered C++ scripts.");
                            ImGui::TextDisabled("Create a script, rebuild, then attach it.");
                        }
                        for (const ScriptDescriptor& script : registeredScripts) {
                            const std::string label = script.name + "##attachRegisteredScript";
                            if (ImGui::MenuItem(label.c_str())) {
                                AttachScriptToSelected(script.name, script.path);
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", script.path.c_str());
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Create C++ Script")) {
                        createScriptNameBuffer_[0] = '\0';
                        showCreateScriptPopup_ = true;
                    }
                    ImGui::EndPopup();
                }

                if (showCreateScriptPopup_) {
                    ImGui::OpenPopup("Create C++ Script");
                    showCreateScriptPopup_ = false;
                }

                if (ImGui::BeginPopupModal("Create C++ Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Create a compiled C++ object script.");
                    ImGui::TextDisabled("Rebuild the app after creating or editing script source.");
                    ImGui::InputText("Class Name", createScriptNameBuffer_, sizeof(createScriptNameBuffer_));
                    if (ImGui::Button("Create")) {
                        if (CreateScriptAsset(createScriptNameBuffer_)) {
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (obj.scriptAttachments.empty()) {
                    ImGui::TextDisabled("No script components.");
                }

                for (int scriptIndex = 0; scriptIndex < static_cast<int>(obj.scriptAttachments.size()); ++scriptIndex) {
                    ObjectScriptAttachment& script = obj.scriptAttachments[static_cast<std::size_t>(scriptIndex)];
                    ImGui::PushID(scriptIndex);

                    const std::string header = std::string("Script: ") + (script.scriptName.empty() ? "(missing)" : script.scriptName);
                    const bool scriptTreeOpen = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !script.scriptPath.empty()) {
                        if (OpenProjectAssetInDefaultEditor(script.scriptPath)) {
                            if (console_) {
                                console_->AddLog("Opened script file: " + script.scriptPath);
                            }
                        } else if (console_) {
                            console_->AddError("Failed to open script file: " + script.scriptPath);
                        }
                    }
                    if (scriptTreeOpen) {
                        const bool enabledBefore = script.enabled;
                        if (ImGui::Checkbox("Enabled", &script.enabled)) {
                            const bool enabledAfter = script.enabled;
                            script.enabled = enabledBefore;
                            PushUndoState();
                            script.enabled = enabledAfter;
                            if (scriptsRunning_) {
                                RebuildScriptRuntime();
                            }
                            if (onDirty_) onDirty_();
                        }

                        ImGui::TextWrapped("Class: %s", script.scriptName.empty() ? "(missing)" : script.scriptName.c_str());
                        ImGui::TextWrapped("Source: %s", script.scriptPath.empty() ? "(none)" : script.scriptPath.c_str());
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !script.scriptPath.empty()) {
                            if (OpenProjectAssetInDefaultEditor(script.scriptPath)) {
                                if (console_) {
                                    console_->AddLog("Opened script file: " + script.scriptPath);
                                }
                            } else if (console_) {
                                console_->AddError("Failed to open script file: " + script.scriptPath);
                            }
                        }
                        if (FindRegisteredScript(script.scriptName) == nullptr) {
                            ImGui::TextDisabled("Not registered in this build. Rebuild after creating or editing scripts.");
                        }

                        if (ImGui::Button("Remove Script")) {
                            PushUndoState();
                            obj.scriptAttachments.erase(obj.scriptAttachments.begin() + scriptIndex);
                            if (scriptsRunning_) {
                                RebuildScriptRuntime();
                            }
                            if (onDirty_) onDirty_();
                            ImGui::TreePop();
                            ImGui::PopID();
                            break;
                        }

                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Delete")) {
                DeleteSelectedObject();
            }
        } else {
            ImGui::TextDisabled("No object selected.");
        }

        RenderProjectAssetPickerPopup();
    }
    ImGui::End();
}

void SceneEditor::RenderProjectAssetPickerPopup() {
    if (assetPickerMode_ == ProjectAssetPickerMode::None) {
        return;
    }

    const bool pickingMesh = (assetPickerMode_ == ProjectAssetPickerMode::ReplaceMesh);
    if (ImGui::BeginPopupModal("Select Project Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(pickingMesh ? "Select an OBJ from the project" : "Select a material from the project");
        ImGui::Separator();

        if (ImGui::Button("Refresh")) {
            RefreshProjectFiles();
        }

        bool found = false;
        if (ImGui::BeginChild("ProjectAssetPickerList", ImVec2(420.0f, 260.0f), true)) {
            for (const std::string& file : projectFiles_) {
                const bool matches = pickingMesh ? IsObjAssetPath(file) : IsMaterialAssetPath(file);
                if (!matches) {
                    continue;
                }

                found = true;
                const std::string label = fs::path(file).filename().string() + "##" + file;
                if (ImGui::Selectable(label.c_str())) {
                    if (pickingMesh) {
                        ReplaceSelectedMeshFromObj(file);
                    } else {
                        AssignMaterialToSelected(MaterialIdFromAssetPath(file));
                    }
                    assetPickerMode_ = ProjectAssetPickerMode::None;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", file.c_str());
                }
            }

            if (!found) {
                ImGui::TextDisabled("%s", pickingMesh ? "No OBJ files found in project assets." : "No material files found in project assets.");
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Cancel")) {
            assetPickerMode_ = ProjectAssetPickerMode::None;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void SceneEditor::RenderMaterialInspector() {
    if (inspectedMaterialId_.empty()) {
        inspectMaterial_ = false;
        return;
    }

    if (!materialManager_.Exists(inspectedMaterialId_)) {
        materialManager_.LoadAll();
    }

    Material* material = materialManager_.Get(inspectedMaterialId_);
    if (material == nullptr) {
        ImGui::TextDisabled("Material not found: %s", inspectedMaterialId_.c_str());
        if (ImGui::Button("Back to Object")) {
            inspectMaterial_ = false;
        }
        return;
    }

    ImGui::TextUnformatted("Material Asset");
    ImGui::TextWrapped("ID: %s", inspectedMaterialId_.c_str());
    ImGui::Separator();

    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", material->name.c_str());
    if (ImGui::InputText("Name##materialName", nameBuf, sizeof(nameBuf))) {
        material->name = nameBuf;
    }

    char shaderBuf[128];
    std::snprintf(shaderBuf, sizeof(shaderBuf), "%s", material->shader.c_str());
    if (ImGui::InputText("Shader##materialShader", shaderBuf, sizeof(shaderBuf))) {
        material->shader = shaderBuf;
    }

    ImGui::ColorEdit4("Albedo Color", material->albedoColor);
    ImGui::SliderFloat("Metallic", &material->metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &material->roughness, 0.0f, 1.0f);
    ImGui::ColorEdit3("Emissive Color", material->emissiveColor);
    ImGui::DragFloat2("UV Tiling", material->uvTiling, 0.01f, 0.01f, 10.0f);
    ImGui::DragFloat2("UV Offset", material->uvOffset, 0.01f, -10.0f, 10.0f);

    ImGui::Separator();
    ImGui::TextUnformatted("Texture Paths");

    auto editTexturePath = [](const char* label, std::string& value) {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
        if (ImGui::InputText(label, buffer, sizeof(buffer))) {
            value = buffer;
        }
    };

    editTexturePath("Albedo##matTexAlbedo", material->texAlbedo);
    editTexturePath("Normal##matTexNormal", material->texNormal);
    editTexturePath("Metallic##matTexMetallic", material->texMetallic);
    editTexturePath("Roughness##matTexRoughness", material->texRoughness);
    editTexturePath("AO##matTexAo", material->texAo);

    ImGui::Separator();
    if (ImGui::Button("Save Material")) {
        if (materialManager_.Save(inspectedMaterialId_, *material)) {
            materialManager_.LoadAll();
            if (console_) {
                console_->AddLog("Saved material: " + inspectedMaterialId_);
            }
        } else if (console_) {
            console_->AddError("Failed to save material: " + inspectedMaterialId_);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Back to Object")) {
        inspectMaterial_ = false;
    }
}


bool SceneEditor::AssignMaterialToSelected(const std::string& materialId) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size()) || materialId.empty()) {
        return false;
    }

    if (!materialManager_.Exists(materialId)) {
        materialManager_.LoadAll();
    }

    SceneObject& obj = objects_[selectedIndex_];
    PushUndoState();
    obj.materialId = materialId;
    if (console_) {
        console_->AddLog("Assigned material " + materialId + " to " + obj.name);
    }
    if (onDirty_) onDirty_();
    return true;
}

void SceneEditor::OpenMaterialEditor(const std::string& materialId) {
    if (materialId.empty()) {
        return;
    }

    if (!materialManager_.Exists(materialId)) {
        materialManager_.LoadAll();
    }

    inspectedMaterialId_ = materialId;
    inspectMaterial_ = true;
}
} // namespace raceman


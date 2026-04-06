#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

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
                obj.name = nameBuf;
                if (onDirty_) onDirty_();
            }

            // Type (read-only)
            ImGui::TextDisabled("Type: %s", obj.type.c_str());

            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::DragFloat3("Position", &obj.transform.position.x, 0.1f)) { if (onDirty_) onDirty_(); }
                if (ImGui::DragFloat3("Rotation (deg)", &obj.transform.rotationEuler.x, 0.5f)) { if (onDirty_) onDirty_(); }
                if (ImGui::DragFloat3("Scale", &obj.transform.scale.x, 0.1f)) { if (onDirty_) onDirty_(); }
            }

            if (ImGui::CollapsingHeader("Mesh Filter", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("Mesh Type: %s", obj.type.c_str());
                if (obj.type == "Mesh") {
                    ImGui::TextWrapped("Source: %s", obj.sourcePath.empty() ? "(none)" : obj.sourcePath.c_str());
                    ImGui::TextDisabled("Submesh Index: %d", obj.meshIndex);
                    ImGui::TextWrapped("Imported Material: %s", obj.importedMaterialName.empty() ? "(none)" : obj.importedMaterialName.c_str());
                    ImGui::TextWrapped("OBJ Diffuse: %s", obj.diffuseTexturePath.empty() ? "(none)" : obj.diffuseTexturePath.c_str());
                } else {
                    ImGui::TextDisabled("Built-in mesh: %s", obj.type.c_str());
                }
                ImGui::Button("Drop OBJ here to replace Mesh Filter", ImVec2(-1.0f, 0.0f));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjAssetPayload)) {
                        const char* path = static_cast<const char*>(payload->Data);
                        if (path != nullptr) {
                            ReplaceSelectedMeshFromObj(path);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                const std::string materialId = obj.materialId.empty() ? std::string("pbr_default") : obj.materialId;
                const Material* material = materialManager_.Get(materialId);
                ImGui::TextWrapped("Material: %s", materialId.c_str());
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    OpenMaterialEditor(materialId);
                }
                if (material != nullptr) {
                    ImGui::TextWrapped("Name: %s", material->name.empty() ? materialId.c_str() : material->name.c_str());
                    ImGui::ColorButton("Albedo Preview", ImVec4(material->albedoColor[0], material->albedoColor[1], material->albedoColor[2], material->albedoColor[3]));
                } else {
                    ImGui::TextDisabled("Material asset not loaded.");
                }
                ImGui::TextDisabled("Double-click material name to edit asset.");
                ImGui::Button("Drop Material here", ImVec2(-1.0f, 0.0f));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kMaterialAssetPayload)) {
                        const char* materialIdPayload = static_cast<const char*>(payload->Data);
                        if (materialIdPayload != nullptr) {
                            AssignMaterialToSelected(materialIdPayload);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Delete")) {
                DeleteSelectedObject();
            }
        } else {
            ImGui::TextDisabled("No object selected.");
        }
    }
    ImGui::End();
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


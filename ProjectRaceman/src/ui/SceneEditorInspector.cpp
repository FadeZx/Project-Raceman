#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"
#include "../scripting/ScriptRegistry.h"

#include <glad/glad.h>
#include <stb_image.h>

#include <cstdint>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/norm.hpp>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

bool IsNarrowInspectorLayout() {
    return ImGui::GetContentRegionAvail().x < 260.0f;
}

void RenderInspectorLabel(const char* label) {
    ImGui::TextUnformatted(label);
}

bool RenderInspectorInputText(const char* label, const char* id, char* buffer, std::size_t bufferSize) {
    if (IsNarrowInspectorLayout()) {
        RenderInspectorLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::InputText(id, buffer, bufferSize);
    }
    return ImGui::InputText(label, buffer, bufferSize);
}

bool RenderInspectorDragFloat3(const char* label, const char* id, float* values, float speed, float min = 0.0f, float max = 0.0f) {
    if (IsNarrowInspectorLayout()) {
        RenderInspectorLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::DragFloat3(id, values, speed, min, max);
    }
    return ImGui::DragFloat3(label, values, speed, min, max);
}

bool RenderInspectorDragFloat(const char* label, const char* id, float* value, float speed, float min = 0.0f, float max = 0.0f) {
    if (IsNarrowInspectorLayout()) {
        RenderInspectorLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::DragFloat(id, value, speed, min, max);
    }
    return ImGui::DragFloat(label, value, speed, min, max);
}

void RenderInspectorWrappedValue(const char* label, const std::string& value) {
    if (IsNarrowInspectorLayout()) {
        ImGui::TextDisabled("%s", label);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextWrapped("%s %s", label, value.c_str());
    }
}

void RenderComponentIcon(unsigned int textureId) {
    if (textureId == 0) {
        return;
    }
    ImGui::Image(static_cast<ImTextureID>(textureId), ImVec2(18.0f, 18.0f));
    ImGui::SameLine();
}

bool RenderRemovableComponentHeader(const char* label, const char* id, unsigned int textureId, bool* enabled, bool& enabledChanged, bool& removeRequested) {
    ImGui::PushID(id);
    RenderComponentIcon(textureId);
    enabledChanged = false;
    if (enabled != nullptr) {
        enabledChanged = ImGui::Checkbox("##componentEnabled", enabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable Component");
        }
        ImGui::SameLine();
    }
    const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    const float removeButtonWidth = ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - removeButtonWidth);
    ImGui::SetNextItemAllowOverlap();
    removeRequested = ImGui::Button("Remove");
    ImGui::PopID();
    return open;
}

} // namespace

unsigned int SceneEditor::GetComponentIconTexture(const std::string& filename) {
    const auto existing = componentIconTextures_.find(filename);
    if (existing != componentIconTextures_.end()) {
        return existing->second;
    }

    const fs::path absolutePath = EditorAssetPathToAbsolute("icons/" + filename);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(absolutePath.string().c_str(), &width, &height, &channels, 4);
    if (data == nullptr || width <= 0 || height <= 0) {
        componentIconTextures_[filename] = 0;
        return 0;
    }

    unsigned int textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);

    componentIconTextures_[filename] = textureId;
    return textureId;
}

void SceneEditor::RenderInspectorPanel() {
    if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse)) {
        if (inspectMaterial_) {
            RenderMaterialInspector();
        } else if (selectedIndices_.size() > 1) {
            RenderMultiSelectionInspector();
        } else if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size())) {
            SceneObject& obj = objects_[selectedIndex_];
            auto beginInspectorContinuousEdit = [&]() {
                if (!inspectorEditActive_) {
                    PushUndoState();
                    inspectorEditActive_ = true;
                }
            };
            auto endInspectorContinuousEdit = [&]() {
                if (ImGui::IsItemDeactivated()) {
                    inspectorEditActive_ = false;
                }
            };

            // Name
            bool objectEnabled = obj.enabled;
            if (ImGui::Checkbox("##objectEnabledInspector", &objectEnabled)) {
                PushUndoState();
                obj.enabled = objectEnabled;
                if (onDirty_) onDirty_();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Enable Object");
            }
            ImGui::SameLine();
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", obj.name.c_str());
            if (RenderInspectorInputText("Object Name", "##objectName", nameBuf, sizeof(nameBuf))) {
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

            auto renderAddComponentMenu = [&]() {
                bool anyAvailable = false;
                if (!obj.hasMeshFilter) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Mesh Filter")) {
                        PushUndoState();
                        obj.hasMeshFilter = true;
                        obj.meshFilter = MeshFilterComponent{};
                        obj.meshFilter.meshType = obj.type.empty() ? std::string("Mesh") : obj.type;
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasMeshRenderer) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Mesh Renderer")) {
                        PushUndoState();
                        obj.hasMeshRenderer = true;
                        obj.meshRenderer = MeshRendererComponent{};
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasScriptComponent) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Scripts")) {
                        PushUndoState();
                        obj.hasScriptComponent = true;
                        obj.scriptComponent = ScriptComponent{};
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasRigidbody) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Rigidbody")) {
                        PushUndoState();
                        if (obj.hasCharacterController) {
                            obj.hasCharacterController = false;
                            obj.characterController = CharacterControllerComponent{};
                        }
                        obj.hasRigidbody = true;
                        obj.rigidbody = RigidbodyComponent{};
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasCharacterController) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Character Controller")) {
                        PushUndoState();
                        if (obj.hasRigidbody) {
                            obj.hasRigidbody = false;
                            obj.rigidbody = RigidbodyComponent{};
                        }
                        obj.hasCharacterController = true;
                        obj.characterController = CharacterControllerComponent{};
                        if (onDirty_) onDirty_();
                    }
                }
                const bool hasAvailableCollider = !obj.hasBoxCollider || !obj.hasSphereCollider || !obj.hasCapsuleCollider || !obj.hasPlaneCollider;
                if (hasAvailableCollider) {
                    anyAvailable = true;
                    if (ImGui::BeginMenu("Collider")) {
                        if (!obj.hasBoxCollider && ImGui::MenuItem("Box")) {
                            PushUndoState();
                            obj.hasBoxCollider = true;
                            obj.boxCollider = BoxColliderComponent{};
                            if (onDirty_) onDirty_();
                        }
                        if (!obj.hasSphereCollider && ImGui::MenuItem("Sphere")) {
                            PushUndoState();
                            obj.hasSphereCollider = true;
                            obj.sphereCollider = SphereColliderComponent{};
                            if (onDirty_) onDirty_();
                        }
                        if (!obj.hasCapsuleCollider && ImGui::MenuItem("Capsule")) {
                            PushUndoState();
                            obj.hasCapsuleCollider = true;
                            obj.capsuleCollider = CapsuleColliderComponent{};
                            if (onDirty_) onDirty_();
                        }
                        if (!obj.hasPlaneCollider && ImGui::MenuItem("Plane")) {
                            PushUndoState();
                            obj.hasPlaneCollider = true;
                            obj.planeCollider = PlaneColliderComponent{};
                            if (onDirty_) onDirty_();
                        }
                        ImGui::EndMenu();
                    }
                }
                if (!obj.hasCamera) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Camera")) {
                        bool hasAnyCamera = false;
                        for (const SceneObject& sceneObject : objects_) {
                            if (&sceneObject != &obj && sceneObject.hasCamera) {
                                hasAnyCamera = true;
                                break;
                            }
                        }
                        PushUndoState();
                        obj.hasCamera = true;
                        obj.camera = CameraComponent{};
                        obj.camera.isMain = !hasAnyCamera;
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasLight) {
                    anyAvailable = true;
                    if (ImGui::BeginMenu("Light")) {
                        if (ImGui::MenuItem("Directional")) {
                            PushUndoState();
                            obj.hasLight = true;
                            obj.light = LightComponent{};
                            obj.light.type = LightType::Directional;
                            obj.light.intensity = 1.5f;
                            obj.light.range = 100.0f;
                            if (onDirty_) onDirty_();
                        }
                        if (ImGui::MenuItem("Point")) {
                            PushUndoState();
                            obj.hasLight = true;
                            obj.light = LightComponent{};
                            obj.light.type = LightType::Point;
                            obj.light.intensity = 3.0f;
                            obj.light.range = 10.0f;
                            if (onDirty_) onDirty_();
                        }
                        if (ImGui::MenuItem("Spot")) {
                            PushUndoState();
                            obj.hasLight = true;
                            obj.light = LightComponent{};
                            obj.light.type = LightType::Spot;
                            obj.light.intensity = 4.0f;
                            obj.light.range = 12.0f;
                            obj.light.spotAngleDegrees = 35.0f;
                            if (onDirty_) onDirty_();
                        }
                        ImGui::EndMenu();
                    }
                }
                if (!anyAvailable) {
                    ImGui::TextDisabled("All supported components are already added.");
                }
            };

            if (ImGui::Button("Add Component")) {
                ImGui::OpenPopup("Add Object Component");
            }
            if (ImGui::BeginPopup("Add Object Component")) {
                if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    ImGui::CloseCurrentPopup();
                }

                renderAddComponentMenu();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupContextWindow("InspectorAddComponentContext", ImGuiPopupFlags_MouseButtonRight)) {
                ImGui::TextDisabled("Add Component");
                ImGui::Separator();
                renderAddComponentMenu();
                ImGui::EndPopup();
            }

            RenderComponentIcon(GetComponentIconTexture("component-transform.png"));
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                Transform before = obj.transform;
                if (RenderInspectorDragFloat3("Position", "##position", &obj.transform.position.x, 0.1f)) {
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
                if (RenderInspectorDragFloat3("Rotation (deg)", "##rotation", &obj.transform.rotationEuler.x, 0.5f)) {
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
                if (RenderInspectorDragFloat3("Scale", "##scale", &obj.transform.scale.x, 0.1f)) {
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

            bool removeMeshFilter = false;
            bool meshFilterOpen = false;
            bool meshFilterEnabledChanged = false;
            const bool meshFilterEnabledBefore = obj.meshFilter.enabled;
            if (obj.hasMeshFilter) {
                meshFilterOpen = RenderRemovableComponentHeader("Mesh Filter", "MeshFilterHeader", GetComponentIconTexture("component-mesh-filter.png"), &obj.meshFilter.enabled, meshFilterEnabledChanged, removeMeshFilter);
            }
            if (removeMeshFilter) {
                PushUndoState();
                obj.hasMeshFilter = false;
                obj.meshFilter = MeshFilterComponent{};
                if (onDirty_) onDirty_();
            } else {
                if (obj.hasMeshFilter && meshFilterEnabledChanged) {
                    const bool meshFilterEnabledAfter = obj.meshFilter.enabled;
                    obj.meshFilter.enabled = meshFilterEnabledBefore;
                    PushUndoState();
                    obj.meshFilter.enabled = meshFilterEnabledAfter;
                    if (onDirty_) onDirty_();
                }
            }
            if (obj.hasMeshFilter && meshFilterOpen) {
                const std::string meshType = obj.meshFilter.meshType.empty() ? obj.type : obj.meshFilter.meshType;
                const std::string meshButtonLabel = (meshType == "Mesh" && !obj.meshFilter.sourcePath.empty())
                    ? (fs::path(obj.meshFilter.sourcePath).filename().string() + "##selectMeshFilter")
                    : (meshType + "##selectMeshFilter");
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
                if (meshType == "Mesh") {
                    RenderInspectorWrappedValue("Source:", obj.meshFilter.sourcePath.empty() ? "(none)" : obj.meshFilter.sourcePath);
                    ImGui::TextDisabled("Submesh Index: %d", obj.meshFilter.meshIndex);
                    RenderInspectorWrappedValue("Imported Material:", obj.meshFilter.importedMaterialName.empty() ? "(none)" : obj.meshFilter.importedMaterialName);
                    RenderInspectorWrappedValue("OBJ Diffuse:", obj.meshFilter.diffuseTexturePath.empty() ? "(none)" : obj.meshFilter.diffuseTexturePath);
                } else {
                    ImGui::TextDisabled("Built-in mesh: %s", meshType.c_str());
                }
            }

            bool removeMeshRenderer = false;
            bool meshRendererOpen = false;
            bool meshRendererEnabledChanged = false;
            const bool meshRendererEnabledBefore = obj.meshRenderer.enabled;
            if (obj.hasMeshRenderer) {
                meshRendererOpen = RenderRemovableComponentHeader("Mesh Renderer", "MeshRendererHeader", GetComponentIconTexture("component-mesh-renderer.png"), &obj.meshRenderer.enabled, meshRendererEnabledChanged, removeMeshRenderer);
            }
            if (removeMeshRenderer) {
                PushUndoState();
                obj.hasMeshRenderer = false;
                obj.meshRenderer = MeshRendererComponent{};
                if (onDirty_) onDirty_();
            } else {
                if (obj.hasMeshRenderer && meshRendererEnabledChanged) {
                    const bool meshRendererEnabledAfter = obj.meshRenderer.enabled;
                    obj.meshRenderer.enabled = meshRendererEnabledBefore;
                    PushUndoState();
                    obj.meshRenderer.enabled = meshRendererEnabledAfter;
                    if (onDirty_) onDirty_();
                }
            }
            if (obj.hasMeshRenderer && meshRendererOpen) {
                const std::string materialId = obj.meshRenderer.materialId.empty() ? std::string("pbr_default") : obj.meshRenderer.materialId;
                const Material* material = materialManager_.Get(materialId);
                std::string materialFilename = materialId + ".mat";
                for (const std::string& file : projectFiles_) {
                    if (IsMaterialAssetPath(file) && MaterialIdFromAssetPath(file) == materialId) {
                        materialFilename = ProjectAssetDisplayFilename(file);
                        break;
                    }
                }
                const std::string materialButtonLabel = materialFilename + "##selectMaterial";
                ImGui::TextDisabled("Material:");
                ImGui::SameLine();
                const float editButtonWidth = ImGui::CalcTextSize("Edit").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                const float materialButtonWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - editButtonWidth - ImGui::GetStyle().ItemSpacing.x);
                if (ImGui::Button(materialButtonLabel.c_str(), ImVec2(materialButtonWidth, 0.0f))) {
                    assetPickerMode_ = ProjectAssetPickerMode::AssignMaterial;
                    ImGui::OpenPopup("Select Project Asset");
                }
                if (ImGui::BeginPopupContextItem("MaterialFieldContext")) {
                    if (ImGui::MenuItem("Edit")) {
                        OpenMaterialEditor(materialId);
                    }
                    ImGui::EndPopup();
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
                ImGui::SameLine();
                if (ImGui::Button("Edit##materialProperties")) {
                    OpenMaterialEditor(materialId);
                }
                if (material != nullptr) {
                    ImGui::ColorButton("Albedo Preview", ImVec4(material->albedoColor[0], material->albedoColor[1], material->albedoColor[2], material->albedoColor[3]));
                } else {
                    ImGui::TextDisabled("Material asset not loaded.");
                }
            }

            bool removeScripts = false;
            bool scriptsOpen = false;
            bool scriptsEnabledChanged = false;
            const bool scriptsEnabledBefore = obj.scriptComponent.enabled;
            if (obj.hasScriptComponent) {
                scriptsOpen = RenderRemovableComponentHeader("Scripts", "ScriptsHeader", GetComponentIconTexture("component-script.png"), &obj.scriptComponent.enabled, scriptsEnabledChanged, removeScripts);
            }
            if (removeScripts) {
                PushUndoState();
                obj.hasScriptComponent = false;
                obj.scriptComponent = ScriptComponent{};
                if (scriptsRunning_) {
                    RebuildScriptRuntime();
                }
                if (onDirty_) onDirty_();
            } else {
                if (obj.hasScriptComponent && scriptsEnabledChanged) {
                    const bool scriptsEnabledAfter = obj.scriptComponent.enabled;
                    obj.scriptComponent.enabled = scriptsEnabledBefore;
                    PushUndoState();
                    obj.scriptComponent.enabled = scriptsEnabledAfter;
                    if (scriptsRunning_) {
                        RebuildScriptRuntime();
                    }
                    if (onDirty_) onDirty_();
                }
            }
            if (obj.hasScriptComponent && scriptsOpen) {
                if (ImGui::Button("Add Script")) {
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
                    ImGui::TextDisabled("Run tools/watch-scripts.ps1 -AttachDebugger for rebuild/restart/debug attach.");
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

                if (obj.scriptComponent.attachments.empty()) {
                    ImGui::TextDisabled("No script components.");
                }

                for (int scriptIndex = 0; scriptIndex < static_cast<int>(obj.scriptComponent.attachments.size()); ++scriptIndex) {
                    ObjectScriptAttachment& script = obj.scriptComponent.attachments[static_cast<std::size_t>(scriptIndex)];
                    ImGui::PushID(scriptIndex);

                    const std::string header = std::string("Script: ") + (script.scriptName.empty() ? "(missing)" : script.scriptName);
                    const bool scriptTreeOpen = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                    if (ImGui::IsItemHovered() && !script.scriptPath.empty()) {
                        ImGui::SetTooltip("Click to show in Browser: %s", script.scriptPath.c_str());
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            SelectProjectFile(script.scriptPath);
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
                        if (ImGui::IsItemHovered() && !script.scriptPath.empty()) {
                            ImGui::SetTooltip("Click to show in Browser.");
                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                SelectProjectFile(script.scriptPath);
                            }
                        }
                        if (FindRegisteredScript(script.scriptName) == nullptr) {
                            ImGui::TextDisabled("Not registered in this build. Rebuild after creating or editing scripts.");
                        }

                        if (ImGui::Button("Remove Script")) {
                            PushUndoState();
                            obj.scriptComponent.attachments.erase(obj.scriptComponent.attachments.begin() + scriptIndex);
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

            bool removeRigidbody = false;
            bool rigidbodyOpen = false;
            bool rigidbodyEnabledChanged = false;
            const bool rigidbodyEnabledBefore = obj.rigidbody.enabled;
            if (obj.hasRigidbody) {
                rigidbodyOpen = RenderRemovableComponentHeader("Rigidbody", "RigidbodyHeader", GetComponentIconTexture("component-rigidbody.png"), &obj.rigidbody.enabled, rigidbodyEnabledChanged, removeRigidbody);
            }
            if (removeRigidbody) {
                PushUndoState();
                obj.hasRigidbody = false;
                obj.rigidbody = RigidbodyComponent{};
                if (onDirty_) onDirty_();
            } else {
                if (obj.hasRigidbody && rigidbodyEnabledChanged) {
                    const bool rigidbodyEnabledAfter = obj.rigidbody.enabled;
                    obj.rigidbody.enabled = rigidbodyEnabledBefore;
                    PushUndoState();
                    obj.rigidbody.enabled = rigidbodyEnabledAfter;
                    if (onDirty_) onDirty_();
                }
            }
            if (obj.hasRigidbody && rigidbodyOpen) {

                int bodyTypeIndex = obj.rigidbody.bodyType == RigidbodyBodyType::Static ? 0 : 1;
                const char* bodyTypes[] = {"Static", "Dynamic"};
                if (ImGui::Combo("Body Type", &bodyTypeIndex, bodyTypes, 2)) {
                    PushUndoState();
                    obj.rigidbody.bodyType = bodyTypeIndex == 0 ? RigidbodyBodyType::Static : RigidbodyBodyType::Dynamic;
                    if (obj.rigidbody.bodyType == RigidbodyBodyType::Static) {
                        obj.rigidbody.velocity = {0.0f, 0.0f, 0.0f};
                    }
                    if (onDirty_) onDirty_();
                }

                float mass = obj.rigidbody.mass;
                if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.0001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.rigidbody.mass = (std::max)(0.0001f, mass);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                const bool useGravityBefore = obj.rigidbody.useGravity;
                if (ImGui::Checkbox("Use Gravity", &obj.rigidbody.useGravity)) {
                    const bool useGravityAfter = obj.rigidbody.useGravity;
                    obj.rigidbody.useGravity = useGravityBefore;
                    PushUndoState();
                    obj.rigidbody.useGravity = useGravityAfter;
                    if (onDirty_) onDirty_();
                }

                glm::vec3 velocity = obj.rigidbody.velocity;
                if (RenderInspectorDragFloat3("Velocity", "##rigidbodyVelocity", &velocity.x, 0.1f)) {
                    beginInspectorContinuousEdit();
                    obj.rigidbody.velocity = velocity;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();
            }

            bool removeCharacterController = false;
            bool characterControllerOpen = false;
            bool characterControllerEnabledChanged = false;
            const bool characterControllerEnabledBefore = obj.characterController.enabled;
            if (obj.hasCharacterController) {
                characterControllerOpen = RenderRemovableComponentHeader("Character Controller", "CharacterControllerHeader", GetComponentIconTexture("component-capsule-collider.png"), &obj.characterController.enabled, characterControllerEnabledChanged, removeCharacterController);
            }
            if (removeCharacterController) {
                PushUndoState();
                obj.hasCharacterController = false;
                obj.characterController = CharacterControllerComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasCharacterController && characterControllerEnabledChanged) {
                const bool enabledAfter = obj.characterController.enabled;
                obj.characterController.enabled = characterControllerEnabledBefore;
                PushUndoState();
                obj.characterController.enabled = enabledAfter;
                if (onDirty_) onDirty_();
            }
            if (obj.hasCharacterController && characterControllerOpen) {
                ImGui::TextDisabled("Capsule-based Jolt CharacterVirtual controller.");

                float radius = obj.characterController.radius;
                if (ImGui::DragFloat("Radius##CharacterController", &radius, 0.01f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.characterController.radius = (std::max)(0.001f, radius);
                    obj.characterController.height = (std::max)(obj.characterController.height, obj.characterController.radius * 2.0f);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float height = obj.characterController.height;
                if (ImGui::DragFloat("Height##CharacterController", &height, 0.01f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.characterController.height = (std::max)(obj.characterController.radius * 2.0f, height);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float stepHeight = obj.characterController.stepHeight;
                if (ImGui::DragFloat("Step Height##CharacterController", &stepHeight, 0.01f, 0.0f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.characterController.stepHeight = (std::max)(0.0f, stepHeight);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float slopeLimit = obj.characterController.slopeLimitDegrees;
                if (ImGui::DragFloat("Slope Limit##CharacterController", &slopeLimit, 0.5f, 1.0f, 89.0f)) {
                    beginInspectorContinuousEdit();
                    obj.characterController.slopeLimitDegrees = (std::max)(1.0f, (std::min)(89.0f, slopeLimit));
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float maxStrength = obj.characterController.maxStrength;
                if (ImGui::DragFloat("Max Strength##CharacterController", &maxStrength, 1.0f, 0.0f, 1000000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.characterController.maxStrength = (std::max)(0.0f, maxStrength);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float mass = obj.characterController.mass;
                if (ImGui::DragFloat("Mass##CharacterController", &mass, 0.1f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.characterController.mass = (std::max)(0.001f, mass);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                ImGui::TextDisabled("Grounded: %s", obj.characterController.grounded ? "Yes" : "No");
                ImGui::TextDisabled("Velocity: %.2f, %.2f, %.2f", obj.characterController.velocity.x, obj.characterController.velocity.y, obj.characterController.velocity.z);
            }

            bool removeBoxCollider = false;
            bool boxColliderOpen = false;
            bool boxColliderEnabledChanged = false;
            const bool boxColliderEnabledBefore = obj.boxCollider.enabled;
            if (obj.hasBoxCollider) {
                boxColliderOpen = RenderRemovableComponentHeader("Box Collider", "BoxColliderHeader", GetComponentIconTexture("component-box-collider.png"), &obj.boxCollider.enabled, boxColliderEnabledChanged, removeBoxCollider);
            }
            if (removeBoxCollider) {
                PushUndoState();
                obj.hasBoxCollider = false;
                obj.boxCollider = BoxColliderComponent{};
                if (onDirty_) onDirty_();
            } else {
                if (obj.hasBoxCollider && boxColliderEnabledChanged) {
                    const bool colliderEnabledAfter = obj.boxCollider.enabled;
                    obj.boxCollider.enabled = boxColliderEnabledBefore;
                    PushUndoState();
                    obj.boxCollider.enabled = colliderEnabledAfter;
                    if (onDirty_) onDirty_();
                }
            }
            if (obj.hasBoxCollider && boxColliderOpen) {

                const bool isTriggerBefore = obj.boxCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger", &obj.boxCollider.isTrigger)) {
                    const bool isTriggerAfter = obj.boxCollider.isTrigger;
                    obj.boxCollider.isTrigger = isTriggerBefore;
                    PushUndoState();
                    obj.boxCollider.isTrigger = isTriggerAfter;
                    if (onDirty_) onDirty_();
                }

                glm::vec3 center = obj.boxCollider.center;
                if (RenderInspectorDragFloat3("Center", "##boxColliderCenter", &center.x, 0.05f)) {
                    beginInspectorContinuousEdit();
                    obj.boxCollider.center = center;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                glm::vec3 size = obj.boxCollider.size;
                if (RenderInspectorDragFloat3("Size", "##boxColliderSize", &size.x, 0.05f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.boxCollider.size = {
                        (std::max)(0.001f, size.x),
                        (std::max)(0.001f, size.y),
                        (std::max)(0.001f, size.z)
                    };
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();
            }

            bool removeSphereCollider = false;
            bool sphereColliderOpen = false;
            bool sphereColliderEnabledChanged = false;
            const bool sphereColliderEnabledBefore = obj.sphereCollider.enabled;
            if (obj.hasSphereCollider) {
                sphereColliderOpen = RenderRemovableComponentHeader("Sphere Collider", "SphereColliderHeader", GetComponentIconTexture("component-sphere-collider.png"), &obj.sphereCollider.enabled, sphereColliderEnabledChanged, removeSphereCollider);
            }
            if (removeSphereCollider) {
                PushUndoState();
                obj.hasSphereCollider = false;
                obj.sphereCollider = SphereColliderComponent{};
                if (onDirty_) onDirty_();
            } else {
                if (obj.hasSphereCollider && sphereColliderEnabledChanged) {
                    const bool colliderEnabledAfter = obj.sphereCollider.enabled;
                    obj.sphereCollider.enabled = sphereColliderEnabledBefore;
                    PushUndoState();
                    obj.sphereCollider.enabled = colliderEnabledAfter;
                    if (onDirty_) onDirty_();
                }
            }
            if (obj.hasSphereCollider && sphereColliderOpen) {

                const bool isTriggerBefore = obj.sphereCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger##SphereCollider", &obj.sphereCollider.isTrigger)) {
                    const bool isTriggerAfter = obj.sphereCollider.isTrigger;
                    obj.sphereCollider.isTrigger = isTriggerBefore;
                    PushUndoState();
                    obj.sphereCollider.isTrigger = isTriggerAfter;
                    if (onDirty_) onDirty_();
                }

                glm::vec3 center = obj.sphereCollider.center;
                if (RenderInspectorDragFloat3("Center", "##sphereColliderCenter", &center.x, 0.05f)) {
                    beginInspectorContinuousEdit();
                    obj.sphereCollider.center = center;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float radius = obj.sphereCollider.radius;
                if (ImGui::DragFloat("Radius##SphereCollider", &radius, 0.05f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.sphereCollider.radius = (std::max)(0.001f, radius);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();
            }

            bool removeCapsuleCollider = false;
            bool capsuleColliderOpen = false;
            bool capsuleColliderEnabledChanged = false;
            const bool capsuleColliderEnabledBefore = obj.capsuleCollider.enabled;
            if (obj.hasCapsuleCollider) {
                capsuleColliderOpen = RenderRemovableComponentHeader("Capsule Collider", "CapsuleColliderHeader", GetComponentIconTexture("component-capsule-collider.png"), &obj.capsuleCollider.enabled, capsuleColliderEnabledChanged, removeCapsuleCollider);
            }
            if (removeCapsuleCollider) {
                PushUndoState();
                obj.hasCapsuleCollider = false;
                obj.capsuleCollider = CapsuleColliderComponent{};
                if (onDirty_) onDirty_();
            } else {
                if (obj.hasCapsuleCollider && capsuleColliderEnabledChanged) {
                    const bool colliderEnabledAfter = obj.capsuleCollider.enabled;
                    obj.capsuleCollider.enabled = capsuleColliderEnabledBefore;
                    PushUndoState();
                    obj.capsuleCollider.enabled = colliderEnabledAfter;
                    if (onDirty_) onDirty_();
                }
            }
            if (obj.hasCapsuleCollider && capsuleColliderOpen) {

                const bool isTriggerBefore = obj.capsuleCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger##CapsuleCollider", &obj.capsuleCollider.isTrigger)) {
                    const bool isTriggerAfter = obj.capsuleCollider.isTrigger;
                    obj.capsuleCollider.isTrigger = isTriggerBefore;
                    PushUndoState();
                    obj.capsuleCollider.isTrigger = isTriggerAfter;
                    if (onDirty_) onDirty_();
                }

                glm::vec3 center = obj.capsuleCollider.center;
                if (RenderInspectorDragFloat3("Center", "##capsuleColliderCenter", &center.x, 0.05f)) {
                    beginInspectorContinuousEdit();
                    obj.capsuleCollider.center = center;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float radius = obj.capsuleCollider.radius;
                if (ImGui::DragFloat("Radius##CapsuleCollider", &radius, 0.05f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.capsuleCollider.radius = (std::max)(0.001f, radius);
                    obj.capsuleCollider.height = (std::max)(obj.capsuleCollider.height, obj.capsuleCollider.radius * 2.0f);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float height = obj.capsuleCollider.height;
                if (ImGui::DragFloat("Height##CapsuleCollider", &height, 0.05f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.capsuleCollider.height = (std::max)(obj.capsuleCollider.radius * 2.0f, height);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();
            }

            bool removePlaneCollider = false;
            bool planeColliderOpen = false;
            bool planeColliderEnabledChanged = false;
            const bool planeColliderEnabledBefore = obj.planeCollider.enabled;
            if (obj.hasPlaneCollider) {
                planeColliderOpen = RenderRemovableComponentHeader("Plane Collider", "PlaneColliderHeader", GetComponentIconTexture("component-box-collider.png"), &obj.planeCollider.enabled, planeColliderEnabledChanged, removePlaneCollider);
            }
            if (removePlaneCollider) {
                PushUndoState();
                obj.hasPlaneCollider = false;
                obj.planeCollider = PlaneColliderComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasPlaneCollider && planeColliderEnabledChanged) {
                const bool colliderEnabledAfter = obj.planeCollider.enabled;
                obj.planeCollider.enabled = planeColliderEnabledBefore;
                PushUndoState();
                obj.planeCollider.enabled = colliderEnabledAfter;
                if (onDirty_) onDirty_();
            }
            if (obj.hasPlaneCollider && planeColliderOpen) {
                const bool isTriggerBefore = obj.planeCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger##PlaneCollider", &obj.planeCollider.isTrigger)) {
                    const bool isTriggerAfter = obj.planeCollider.isTrigger;
                    obj.planeCollider.isTrigger = isTriggerBefore;
                    PushUndoState();
                    obj.planeCollider.isTrigger = isTriggerAfter;
                    if (onDirty_) onDirty_();
                }

                const bool infiniteBefore = obj.planeCollider.infinite;
                if (ImGui::Checkbox("Infinite Plane##PlaneCollider", &obj.planeCollider.infinite)) {
                    const bool infiniteAfter = obj.planeCollider.infinite;
                    obj.planeCollider.infinite = infiniteBefore;
                    PushUndoState();
                    obj.planeCollider.infinite = infiniteAfter;
                    if (onDirty_) onDirty_();
                }

                glm::vec3 normal = obj.planeCollider.normal;
                if (RenderInspectorDragFloat3("Normal", "##planeColliderNormal", &normal.x, 0.05f)) {
                    beginInspectorContinuousEdit();
                    if (glm::length2(normal) <= 0.000001f) {
                        normal = {0.0f, 1.0f, 0.0f};
                    } else {
                        normal = glm::normalize(normal);
                    }
                    obj.planeCollider.normal = normal;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float offset = obj.planeCollider.offset;
                if (RenderInspectorDragFloat("Offset", "##planeColliderOffset", &offset, 0.05f, -100000.0f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.planeCollider.offset = offset;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                if (!obj.planeCollider.infinite) {
                    float halfExtent = obj.planeCollider.halfExtent;
                    if (RenderInspectorDragFloat("Half Extent", "##planeColliderHalfExtent", &halfExtent, 0.5f, 0.001f, 100000.0f)) {
                        beginInspectorContinuousEdit();
                        obj.planeCollider.halfExtent = (std::max)(0.001f, halfExtent);
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                }

                ImGui::TextDisabled("Jolt plane shapes stay static.");
            }

            bool removeCamera = false;
            bool cameraOpen = false;
            bool cameraEnabledChanged = false;
            const bool cameraEnabledBefore = obj.camera.enabled;
            if (obj.hasCamera) {
                cameraOpen = RenderRemovableComponentHeader("Camera", "CameraHeader", GetComponentIconTexture("component-camera.png"), &obj.camera.enabled, cameraEnabledChanged, removeCamera);
            }
            if (removeCamera) {
                PushUndoState();
                obj.hasCamera = false;
                obj.camera = CameraComponent{};
                if (onDirty_) onDirty_();
            } else {
                if (obj.hasCamera && cameraEnabledChanged) {
                    const bool cameraEnabledAfter = obj.camera.enabled;
                    obj.camera.enabled = cameraEnabledBefore;
                    PushUndoState();
                    obj.camera.enabled = cameraEnabledAfter;
                    if (onDirty_) onDirty_();
                }
            }
            if (obj.hasCamera && cameraOpen) {

                const bool isMainBefore = obj.camera.isMain;
                if (ImGui::Checkbox("Main Camera", &obj.camera.isMain)) {
                    const bool isMainAfter = obj.camera.isMain;
                    obj.camera.isMain = isMainBefore;
                    PushUndoState();
                    obj.camera.isMain = isMainAfter;
                    if (onDirty_) onDirty_();
                }

                float fov = obj.camera.fieldOfViewDegrees;
                if (ImGui::DragFloat("Field of View", &fov, 0.5f, 1.0f, 179.0f)) {
                    beginInspectorContinuousEdit();
                    obj.camera.fieldOfViewDegrees = (std::max)(1.0f, (std::min)(179.0f, fov));
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float nearClip = obj.camera.nearClip;
                if (ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.camera.nearClip = (std::max)(0.001f, nearClip);
                    obj.camera.farClip = (std::max)(obj.camera.nearClip + 0.001f, obj.camera.farClip);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float farClip = obj.camera.farClip;
                if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, 0.002f, 1000000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.camera.farClip = (std::max)(obj.camera.nearClip + 0.001f, farClip);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                glm::vec4 clearColor = obj.camera.clearColor;
                if (ImGui::ColorEdit4("Clear Color", &clearColor.x)) {
                    beginInspectorContinuousEdit();
                    obj.camera.clearColor = clearColor;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();
            }

            bool removeLight = false;
            bool lightOpen = false;
            bool lightEnabledChanged = false;
            const bool lightEnabledBefore = obj.light.enabled;
            if (obj.hasLight) {
                lightOpen = RenderRemovableComponentHeader("Light", "LightHeader", GetComponentIconTexture("component-light.png"), &obj.light.enabled, lightEnabledChanged, removeLight);
            }
            if (removeLight) {
                PushUndoState();
                obj.hasLight = false;
                obj.light = LightComponent{};
                if (onDirty_) onDirty_();
            } else {
                if (obj.hasLight && lightEnabledChanged) {
                    const bool lightEnabledAfter = obj.light.enabled;
                    obj.light.enabled = lightEnabledBefore;
                    PushUndoState();
                    obj.light.enabled = lightEnabledAfter;
                    if (onDirty_) onDirty_();
                }
            }
            if (obj.hasLight && lightOpen) {

                int lightTypeIndex = 1;
                if (obj.light.type == LightType::Directional) {
                    lightTypeIndex = 0;
                } else if (obj.light.type == LightType::Spot) {
                    lightTypeIndex = 2;
                }
                const char* lightTypes[] = {"Directional", "Point", "Spot"};
                if (ImGui::Combo("Type##Light", &lightTypeIndex, lightTypes, 3)) {
                    PushUndoState();
                    obj.light.type = lightTypeIndex == 0 ? LightType::Directional : (lightTypeIndex == 2 ? LightType::Spot : LightType::Point);
                    if (onDirty_) onDirty_();
                }

                glm::vec3 color = obj.light.color;
                if (ImGui::ColorEdit3("Color##Light", &color.x)) {
                    beginInspectorContinuousEdit();
                    obj.light.color = color;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float intensity = obj.light.intensity;
                if (ImGui::DragFloat("Intensity##Light", &intensity, 0.05f, 0.0f, 1000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.light.intensity = (std::max)(0.0f, intensity);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                if (obj.light.type != LightType::Directional) {
                    float range = obj.light.range;
                    if (ImGui::DragFloat("Range##Light", &range, 0.1f, 0.001f, 100000.0f)) {
                        beginInspectorContinuousEdit();
                        obj.light.range = (std::max)(0.001f, range);
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                }

                if (obj.light.type == LightType::Spot) {
                    float spotAngle = obj.light.spotAngleDegrees;
                    if (ImGui::DragFloat("Spot Angle##Light", &spotAngle, 0.5f, 1.0f, 179.0f)) {
                        beginInspectorContinuousEdit();
                        obj.light.spotAngleDegrees = (std::max)(1.0f, (std::min)(179.0f, spotAngle));
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
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

void SceneEditor::RenderMultiSelectionInspector() {
    NormalizeSelection();
    if (selectedIndices_.size() <= 1 || selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }

    SceneObject& active = objects_[selectedIndex_];
    ImGui::Text("%zu objects selected", selectedIndices_.size());
    ImGui::TextDisabled("Showing components shared by every selected object. Edits apply to all selected objects.");

    auto beginInspectorContinuousEdit = [&]() {
        if (!inspectorEditActive_) {
            PushUndoState();
            inspectorEditActive_ = true;
        }
    };
    auto endInspectorContinuousEdit = [&]() {
        if (ImGui::IsItemDeactivated()) {
            inspectorEditActive_ = false;
        }
    };
    auto forEachSelected = [&](auto&& fn) {
        for (int index : selectedIndices_) {
            if (index >= 0 && index < static_cast<int>(objects_.size())) {
                fn(objects_[index]);
            }
        }
    };
    auto allSelected = [&](auto&& predicate) {
        for (int index : selectedIndices_) {
            if (index < 0 || index >= static_cast<int>(objects_.size()) || !predicate(objects_[index])) {
                return false;
            }
        }
        return true;
    };
    auto renderSharedHeader = [&](const char* label, const std::string& icon) {
        RenderComponentIcon(GetComponentIconTexture(icon));
        return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    };
    auto markDirty = [&]() {
        if (onDirty_) {
            onDirty_();
        }
    };
    auto renderSharedEnabledHeader = [&](const char* label, const char* id, const std::string& icon, bool enabled, auto&& setter) {
        ImGui::PushID(id);
        RenderComponentIcon(GetComponentIconTexture(icon));
        bool changedEnabled = enabled;
        if (ImGui::Checkbox("##multiComponentEnabled", &changedEnabled)) {
            PushUndoState();
            forEachSelected([&](SceneObject& object) { setter(object, changedEnabled); });
            markDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable Component");
        }
        ImGui::SameLine();
        const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopID();
        return open;
    };

    bool enabled = active.enabled;
    if (ImGui::Checkbox("##multiObjectEnabled", &enabled)) {
        PushUndoState();
        forEachSelected([&](SceneObject& object) { object.enabled = enabled; });
        markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable Selected Objects");
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Selected Objects");

    if (renderSharedHeader("Transform", "component-transform.png")) {
        glm::vec3 position = active.transform.position;
        if (RenderInspectorDragFloat3("Position", "##multiPosition", &position.x, 0.1f)) {
            beginInspectorContinuousEdit();
            forEachSelected([&](SceneObject& object) { object.transform.position = position; });
            markDirty();
        }
        endInspectorContinuousEdit();

        glm::vec3 rotation = active.transform.rotationEuler;
        if (RenderInspectorDragFloat3("Rotation (deg)", "##multiRotation", &rotation.x, 0.5f)) {
            beginInspectorContinuousEdit();
            forEachSelected([&](SceneObject& object) { object.transform.rotationEuler = rotation; });
            markDirty();
        }
        endInspectorContinuousEdit();

        glm::vec3 scale = active.transform.scale;
        if (RenderInspectorDragFloat3("Scale", "##multiScale", &scale.x, 0.1f)) {
            beginInspectorContinuousEdit();
            scale = {
                (std::max)(scale.x, 0.01f),
                (std::max)(scale.y, 0.01f),
                (std::max)(scale.z, 0.01f)
            };
            forEachSelected([&](SceneObject& object) { object.transform.scale = scale; });
            markDirty();
        }
        endInspectorContinuousEdit();
    }

    bool showedSharedComponent = false;
    if (allSelected([](const SceneObject& object) { return object.hasMeshFilter; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Mesh Filter", "MultiMeshFilterHeader", "component-mesh-filter.png", active.meshFilter.enabled, [](SceneObject& object, bool value) { object.meshFilter.enabled = value; })) {
            ImGui::TextDisabled("Mesh Filter is shared. Mesh replacement uses the active object for now.");
        }
    }

    if (allSelected([](const SceneObject& object) { return object.hasMeshRenderer; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Mesh Renderer", "MultiMeshRendererHeader", "component-mesh-renderer.png", active.meshRenderer.enabled, [](SceneObject& object, bool value) { object.meshRenderer.enabled = value; })) {
            const std::string materialId = active.meshRenderer.materialId.empty() ? std::string("pbr_default") : active.meshRenderer.materialId;
            std::string materialFilename = materialId + ".mat";
            for (const std::string& file : projectFiles_) {
                if (IsMaterialAssetPath(file) && MaterialIdFromAssetPath(file) == materialId) {
                    materialFilename = ProjectAssetDisplayFilename(file);
                    break;
                }
            }
            if (ImGui::Button((materialFilename + "##multiSelectMaterial").c_str(), ImVec2(-1.0f, 0.0f))) {
                assetPickerMode_ = ProjectAssetPickerMode::AssignMaterial;
                ImGui::OpenPopup("Select Project Asset");
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
            ImGui::TextDisabled("Material assignment applies to all selected Mesh Renderers.");
        }
    }

    if (allSelected([](const SceneObject& object) { return object.hasRigidbody; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Rigidbody", "MultiRigidbodyHeader", "component-rigidbody.png", active.rigidbody.enabled, [](SceneObject& object, bool value) { object.rigidbody.enabled = value; })) {
            int bodyTypeIndex = active.rigidbody.bodyType == RigidbodyBodyType::Static ? 0 : 1;
            const char* bodyTypes[] = {"Static", "Dynamic"};
            if (ImGui::Combo("Body Type##multiRigidbody", &bodyTypeIndex, bodyTypes, 2)) {
                PushUndoState();
                const RigidbodyBodyType bodyType = bodyTypeIndex == 0 ? RigidbodyBodyType::Static : RigidbodyBodyType::Dynamic;
                forEachSelected([&](SceneObject& object) { object.rigidbody.bodyType = bodyType; });
                markDirty();
            }

            float mass = active.rigidbody.mass;
            if (ImGui::DragFloat("Mass##multiRigidbody", &mass, 0.1f, 0.0001f, 100000.0f)) {
                beginInspectorContinuousEdit();
                mass = (std::max)(0.0001f, mass);
                forEachSelected([&](SceneObject& object) { object.rigidbody.mass = mass; });
                markDirty();
            }
            endInspectorContinuousEdit();

            bool useGravity = active.rigidbody.useGravity;
            if (ImGui::Checkbox("Use Gravity##multiRigidbody", &useGravity)) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) { object.rigidbody.useGravity = useGravity; });
                markDirty();
            }

            glm::vec3 velocity = active.rigidbody.velocity;
            if (RenderInspectorDragFloat3("Velocity", "##multiRigidbodyVelocity", &velocity.x, 0.1f)) {
                beginInspectorContinuousEdit();
                forEachSelected([&](SceneObject& object) { object.rigidbody.velocity = velocity; });
                markDirty();
            }
            endInspectorContinuousEdit();
        }
    }

    if (allSelected([](const SceneObject& object) { return object.hasBoxCollider; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Box Collider", "MultiBoxColliderHeader", "component-box-collider.png", active.boxCollider.enabled, [](SceneObject& object, bool value) { object.boxCollider.enabled = value; })) {
            bool isTrigger = active.boxCollider.isTrigger;
            if (ImGui::Checkbox("Is Trigger##multiBoxCollider", &isTrigger)) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) { object.boxCollider.isTrigger = isTrigger; });
                markDirty();
            }
            glm::vec3 center = active.boxCollider.center;
            if (RenderInspectorDragFloat3("Center", "##multiBoxColliderCenter", &center.x, 0.05f)) {
                beginInspectorContinuousEdit();
                forEachSelected([&](SceneObject& object) { object.boxCollider.center = center; });
                markDirty();
            }
            endInspectorContinuousEdit();
            glm::vec3 size = active.boxCollider.size;
            if (RenderInspectorDragFloat3("Size", "##multiBoxColliderSize", &size.x, 0.05f, 0.001f, 100000.0f)) {
                beginInspectorContinuousEdit();
                size = {(std::max)(0.001f, size.x), (std::max)(0.001f, size.y), (std::max)(0.001f, size.z)};
                forEachSelected([&](SceneObject& object) { object.boxCollider.size = size; });
                markDirty();
            }
            endInspectorContinuousEdit();
        }
    }

    if (allSelected([](const SceneObject& object) { return object.hasSphereCollider; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Sphere Collider", "MultiSphereColliderHeader", "component-sphere-collider.png", active.sphereCollider.enabled, [](SceneObject& object, bool value) { object.sphereCollider.enabled = value; })) {
            bool isTrigger = active.sphereCollider.isTrigger;
            if (ImGui::Checkbox("Is Trigger##multiSphereCollider", &isTrigger)) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) { object.sphereCollider.isTrigger = isTrigger; });
                markDirty();
            }
            glm::vec3 center = active.sphereCollider.center;
            if (RenderInspectorDragFloat3("Center", "##multiSphereColliderCenter", &center.x, 0.05f)) {
                beginInspectorContinuousEdit();
                forEachSelected([&](SceneObject& object) { object.sphereCollider.center = center; });
                markDirty();
            }
            endInspectorContinuousEdit();
            float radius = active.sphereCollider.radius;
            if (ImGui::DragFloat("Radius##multiSphereCollider", &radius, 0.05f, 0.001f, 100000.0f)) {
                beginInspectorContinuousEdit();
                radius = (std::max)(0.001f, radius);
                forEachSelected([&](SceneObject& object) { object.sphereCollider.radius = radius; });
                markDirty();
            }
            endInspectorContinuousEdit();
        }
    }

    if (allSelected([](const SceneObject& object) { return object.hasCapsuleCollider; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Capsule Collider", "MultiCapsuleColliderHeader", "component-capsule-collider.png", active.capsuleCollider.enabled, [](SceneObject& object, bool value) { object.capsuleCollider.enabled = value; })) {
            bool isTrigger = active.capsuleCollider.isTrigger;
            if (ImGui::Checkbox("Is Trigger##multiCapsuleCollider", &isTrigger)) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) { object.capsuleCollider.isTrigger = isTrigger; });
                markDirty();
            }
            glm::vec3 center = active.capsuleCollider.center;
            if (RenderInspectorDragFloat3("Center", "##multiCapsuleColliderCenter", &center.x, 0.05f)) {
                beginInspectorContinuousEdit();
                forEachSelected([&](SceneObject& object) { object.capsuleCollider.center = center; });
                markDirty();
            }
            endInspectorContinuousEdit();
            float radius = active.capsuleCollider.radius;
            if (ImGui::DragFloat("Radius##multiCapsuleCollider", &radius, 0.05f, 0.001f, 100000.0f)) {
                beginInspectorContinuousEdit();
                radius = (std::max)(0.001f, radius);
                forEachSelected([&](SceneObject& object) {
                    object.capsuleCollider.radius = radius;
                    object.capsuleCollider.height = (std::max)(object.capsuleCollider.height, radius * 2.0f);
                });
                markDirty();
            }
            endInspectorContinuousEdit();
            float height = active.capsuleCollider.height;
            if (ImGui::DragFloat("Height##multiCapsuleCollider", &height, 0.05f, 0.001f, 100000.0f)) {
                beginInspectorContinuousEdit();
                height = (std::max)(active.capsuleCollider.radius * 2.0f, height);
                forEachSelected([&](SceneObject& object) { object.capsuleCollider.height = height; });
                markDirty();
            }
            endInspectorContinuousEdit();
        }
    }

    if (allSelected([](const SceneObject& object) { return object.hasPlaneCollider; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Plane Collider", "MultiPlaneColliderHeader", "component-box-collider.png", active.planeCollider.enabled, [](SceneObject& object, bool value) { object.planeCollider.enabled = value; })) {
            bool isTrigger = active.planeCollider.isTrigger;
            if (ImGui::Checkbox("Is Trigger##multiPlaneCollider", &isTrigger)) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) { object.planeCollider.isTrigger = isTrigger; });
                markDirty();
            }
            bool infinite = active.planeCollider.infinite;
            if (ImGui::Checkbox("Infinite Plane##multiPlaneCollider", &infinite)) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) { object.planeCollider.infinite = infinite; });
                markDirty();
            }
            glm::vec3 normal = active.planeCollider.normal;
            if (RenderInspectorDragFloat3("Normal", "##multiPlaneColliderNormal", &normal.x, 0.05f)) {
                beginInspectorContinuousEdit();
                if (glm::length2(normal) <= 0.000001f) {
                    normal = {0.0f, 1.0f, 0.0f};
                } else {
                    normal = glm::normalize(normal);
                }
                forEachSelected([&](SceneObject& object) { object.planeCollider.normal = normal; });
                markDirty();
            }
            endInspectorContinuousEdit();
            float offset = active.planeCollider.offset;
            if (RenderInspectorDragFloat("Offset", "##multiPlaneColliderOffset", &offset, 0.05f, -100000.0f, 100000.0f)) {
                beginInspectorContinuousEdit();
                forEachSelected([&](SceneObject& object) { object.planeCollider.offset = offset; });
                markDirty();
            }
            endInspectorContinuousEdit();
            if (!active.planeCollider.infinite) {
                float halfExtent = active.planeCollider.halfExtent;
                if (RenderInspectorDragFloat("Half Extent", "##multiPlaneColliderHalfExtent", &halfExtent, 0.5f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    halfExtent = (std::max)(0.001f, halfExtent);
                    forEachSelected([&](SceneObject& object) { object.planeCollider.halfExtent = halfExtent; });
                    markDirty();
                }
                endInspectorContinuousEdit();
            }
            ImGui::TextDisabled("Jolt plane shapes stay static.");
        }
    }

    if (allSelected([](const SceneObject& object) { return object.hasCamera; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Camera", "MultiCameraHeader", "component-camera.png", active.camera.enabled, [](SceneObject& object, bool value) { object.camera.enabled = value; })) {
            bool isMain = active.camera.isMain;
            if (ImGui::Checkbox("Main Camera##multiCamera", &isMain)) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) { object.camera.isMain = isMain; });
                markDirty();
            }
            float fov = active.camera.fieldOfViewDegrees;
            if (ImGui::DragFloat("Field of View##multiCamera", &fov, 0.5f, 1.0f, 179.0f)) {
                beginInspectorContinuousEdit();
                fov = (std::max)(1.0f, (std::min)(179.0f, fov));
                forEachSelected([&](SceneObject& object) { object.camera.fieldOfViewDegrees = fov; });
                markDirty();
            }
            endInspectorContinuousEdit();
            float nearClip = active.camera.nearClip;
            if (ImGui::DragFloat("Near Clip##multiCamera", &nearClip, 0.01f, 0.001f, 100000.0f)) {
                beginInspectorContinuousEdit();
                nearClip = (std::max)(0.001f, nearClip);
                forEachSelected([&](SceneObject& object) {
                    object.camera.nearClip = nearClip;
                    object.camera.farClip = (std::max)(nearClip + 0.001f, object.camera.farClip);
                });
                markDirty();
            }
            endInspectorContinuousEdit();
            float farClip = active.camera.farClip;
            if (ImGui::DragFloat("Far Clip##multiCamera", &farClip, 1.0f, 0.002f, 1000000.0f)) {
                beginInspectorContinuousEdit();
                farClip = (std::max)(active.camera.nearClip + 0.001f, farClip);
                forEachSelected([&](SceneObject& object) { object.camera.farClip = farClip; });
                markDirty();
            }
            endInspectorContinuousEdit();
            glm::vec4 clearColor = active.camera.clearColor;
            if (ImGui::ColorEdit4("Clear Color##multiCamera", &clearColor.x)) {
                beginInspectorContinuousEdit();
                forEachSelected([&](SceneObject& object) { object.camera.clearColor = clearColor; });
                markDirty();
            }
            endInspectorContinuousEdit();
        }
    }

    if (allSelected([](const SceneObject& object) { return object.hasLight; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Light", "MultiLightHeader", "component-light.png", active.light.enabled, [](SceneObject& object, bool value) { object.light.enabled = value; })) {
            int lightTypeIndex = active.light.type == LightType::Directional ? 0 : (active.light.type == LightType::Spot ? 2 : 1);
            const char* lightTypes[] = {"Directional", "Point", "Spot"};
            if (ImGui::Combo("Type##multiLight", &lightTypeIndex, lightTypes, 3)) {
                PushUndoState();
                const LightType lightType = lightTypeIndex == 0 ? LightType::Directional : (lightTypeIndex == 2 ? LightType::Spot : LightType::Point);
                forEachSelected([&](SceneObject& object) { object.light.type = lightType; });
                markDirty();
            }
            glm::vec3 color = active.light.color;
            if (ImGui::ColorEdit3("Color##multiLight", &color.x)) {
                beginInspectorContinuousEdit();
                forEachSelected([&](SceneObject& object) { object.light.color = color; });
                markDirty();
            }
            endInspectorContinuousEdit();
            float intensity = active.light.intensity;
            if (ImGui::DragFloat("Intensity##multiLight", &intensity, 0.05f, 0.0f, 1000.0f)) {
                beginInspectorContinuousEdit();
                intensity = (std::max)(0.0f, intensity);
                forEachSelected([&](SceneObject& object) { object.light.intensity = intensity; });
                markDirty();
            }
            endInspectorContinuousEdit();
            if (active.light.type != LightType::Directional) {
                float range = active.light.range;
                if (ImGui::DragFloat("Range##multiLight", &range, 0.1f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    range = (std::max)(0.001f, range);
                    forEachSelected([&](SceneObject& object) { object.light.range = range; });
                    markDirty();
                }
                endInspectorContinuousEdit();
            }
            if (active.light.type == LightType::Spot) {
                float spotAngle = active.light.spotAngleDegrees;
                if (ImGui::DragFloat("Spot Angle##multiLight", &spotAngle, 0.5f, 1.0f, 179.0f)) {
                    beginInspectorContinuousEdit();
                    spotAngle = (std::max)(1.0f, (std::min)(179.0f, spotAngle));
                    forEachSelected([&](SceneObject& object) { object.light.spotAngleDegrees = spotAngle; });
                    markDirty();
                }
                endInspectorContinuousEdit();
            }
        }
    }

    if (!showedSharedComponent) {
        ImGui::TextDisabled("No optional components are shared by every selected object.");
    }

    ImGui::Separator();
    if (ImGui::Button("Delete Selected")) {
        DeleteSelectedObject();
    }
}

void SceneEditor::RenderProjectAssetPickerPopup() {
    if (assetPickerMode_ == ProjectAssetPickerMode::None) {
        return;
    }

    const bool pickingMesh = (assetPickerMode_ == ProjectAssetPickerMode::ReplaceMesh);
    if (ImGui::BeginPopupModal("Select Project Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            assetPickerMode_ = ProjectAssetPickerMode::None;
            ImGui::CloseCurrentPopup();
        }

        ImGui::TextUnformatted(pickingMesh ? "Select an OBJ from the project" : "Select a material from the project");
        ImGui::Separator();

        if (ImGui::Button("Refresh")) {
            RefreshProjectFiles();
        }

        if (!pickingMesh) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(190.0f);
            ImGui::InputText("##newMaterialName", createMaterialNameBuffer_, sizeof(createMaterialNameBuffer_));
            ImGui::SameLine();
            if (ImGui::Button("Add Material")) {
                std::string newMaterialId;
                if (CreateMaterialAsset(createMaterialNameBuffer_, &newMaterialId)) {
                    createMaterialNameBuffer_[0] = '\0';
                    AssignMaterialToSelected(newMaterialId);
                    assetPickerMode_ = ProjectAssetPickerMode::None;
                    ImGui::CloseCurrentPopup();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Create a material and assign it to the selected object.");
            }
        }

        bool found = false;
        if (ImGui::BeginChild("ProjectAssetPickerList", ImVec2(420.0f, 260.0f), true)) {
            for (const std::string& file : projectFiles_) {
                const bool matches = pickingMesh ? IsObjAssetPath(file) : IsMaterialAssetPath(file);
                if (!matches) {
                    continue;
                }

                found = true;
                const std::string label = ProjectAssetDisplayFilename(file) + "##" + file;
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

    RenderMaterialProperties(inspectedMaterialId_, true);
}

void SceneEditor::RenderMaterialProperties(const std::string& materialId, bool showBackButton) {
    if (materialId.empty()) {
        return;
    }

    if (!materialManager_.Exists(materialId)) {
        materialManager_.LoadAll();
    }

    Material* material = materialManager_.Get(materialId);
    if (material == nullptr) {
        ImGui::TextDisabled("Material not found: %s", materialId.c_str());
        if (showBackButton && ImGui::Button("Back to Object")) {
            inspectMaterial_ = false;
        }
        return;
    }

    ImGui::PushID(materialId.c_str());
    ImGui::TextUnformatted("Material Asset");
    ImGui::TextWrapped("ID: %s", materialId.c_str());
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
        if (materialManager_.Save(materialId, *material)) {
            materialManager_.LoadAll();
            if (console_) {
                console_->AddLog("Saved material: " + materialId);
            }
        } else if (console_) {
            console_->AddError("Failed to save material: " + materialId);
        }
    }
    if (showBackButton) {
        ImGui::SameLine();
        if (ImGui::Button("Back to Object")) {
            inspectMaterial_ = false;
        }
    }
    ImGui::PopID();
}

bool SceneEditor::CreateMaterialAsset(const std::string& requestedName, std::string* outMaterialId) {
    std::string materialId = TrimCopyLocal(requestedName);
    if (materialId.empty()) {
        if (console_) {
            console_->AddError("Material name cannot be empty.");
        }
        return false;
    }

    const std::string suffix = ".mat";
    if (EndsWith(ToLowerCopy(materialId), suffix)) {
        materialId.resize(materialId.size() - suffix.size());
    }

    for (char& ch : materialId) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isspace(uch)) {
            ch = '_';
        } else if (!std::isalnum(uch) && ch != '_' && ch != '-') {
            ch = '_';
        }
    }
    materialId = TrimCopyLocal(materialId);
    if (materialId.empty()) {
        if (console_) {
            console_->AddError("Material name must contain letters or numbers.");
        }
        return false;
    }

    materialManager_.LoadAll();
    if (materialManager_.Exists(materialId)) {
        if (console_) {
            console_->AddError("Material already exists: " + materialId);
        }
        return false;
    }

    const fs::path targetPath = ProjectAssetPathToAbsolute(selectedProjectDirectory_ + "/" + materialId + ".mat.json");
    const fs::path assetsRoot = FindAssetsRoot();
    if (!IsUnderPath(targetPath, assetsRoot)) {
        if (console_) {
            console_->AddError("Material creation blocked outside assets: " + materialId);
        }
        return false;
    }
    if (fs::exists(targetPath)) {
        if (console_) {
            console_->AddError("Material already exists: " + materialId);
        }
        return false;
    }

    Material material;
    material.name = materialId;
    try {
        fs::create_directories(targetPath.parent_path());
        std::ofstream out(targetPath, std::ios::trunc);
        if (!out.good()) {
            return false;
        }
        out << "{\n";
        out << "  \"version\": 1,\n";
        out << "  \"name\": \"" << JsonEscape(material.name) << "\",\n";
        out << "  \"shader\": \"pbr\",\n";
        out << "  \"albedoColor\": [1, 1, 1, 1],\n";
        out << "  \"metallic\": 0,\n";
        out << "  \"roughness\": 0.5,\n";
        out << "  \"emissiveColor\": [0, 0, 0],\n";
        out << "  \"uvTiling\": [1, 1],\n";
        out << "  \"uvOffset\": [0, 0],\n";
        out << "  \"textures\": {\n";
        out << "    \"albedo\": \"\",\n";
        out << "    \"normal\": \"\",\n";
        out << "    \"metallic\": \"\",\n";
        out << "    \"roughness\": \"\",\n";
        out << "    \"ao\": \"\"\n";
        out << "  }\n";
        out << "}\n";
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to create material: " + materialId);
        }
        return false;
    }
    materialManager_.LoadAll();
    RefreshProjectFiles();
    if (outMaterialId) {
        *outMaterialId = materialId;
    }
    if (console_) {
        console_->AddLog("Created material: " + materialId);
    }
    return true;
}
bool SceneEditor::AssignMaterialToSelected(const std::string& materialId) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size()) || materialId.empty()) {
        return false;
    }

    if (!materialManager_.Exists(materialId)) {
        materialManager_.LoadAll();
    }

    NormalizeSelection();
    PushUndoState();
    int assignedCount = 0;
    for (int index : selectedIndices_) {
        if (index < 0 || index >= static_cast<int>(objects_.size())) {
            continue;
        }
        SceneObject& obj = objects_[index];
        obj.hasMeshRenderer = true;
        obj.meshRenderer.materialId = materialId;
        ++assignedCount;
    }
    if (console_) {
        if (assignedCount == 1) {
            console_->AddLog("Assigned material " + materialId + " to " + objects_[selectedIndex_].name);
        } else {
            console_->AddLog("Assigned material " + materialId + " to " + std::to_string(assignedCount) + " selected objects.");
        }
    }
    if (onDirty_) onDirty_();
    return assignedCount > 0;
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


#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"
#include "../scripting/ScriptRegistry.h"

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

bool RenderRemovableComponentHeader(const char* label, const char* id, bool& removeRequested) {
    ImGui::PushID(id);
    const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    const float removeButtonWidth = ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - removeButtonWidth);
    ImGui::SetNextItemAllowOverlap();
    removeRequested = ImGui::Button("Remove");
    ImGui::PopID();
    return open;
}

} // namespace

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

            if (ImGui::Button("Add Component")) {
                ImGui::OpenPopup("Add Object Component");
            }
            if (ImGui::BeginPopup("Add Object Component")) {
                if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    ImGui::CloseCurrentPopup();
                }

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
                        obj.hasRigidbody = true;
                        obj.rigidbody = RigidbodyComponent{};
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasBoxCollider) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Box Collider")) {
                        PushUndoState();
                        obj.hasBoxCollider = true;
                        obj.boxCollider = BoxColliderComponent{};
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasSphereCollider) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Sphere Collider")) {
                        PushUndoState();
                        obj.hasSphereCollider = true;
                        obj.sphereCollider = SphereColliderComponent{};
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasCapsuleCollider) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Capsule Collider")) {
                        PushUndoState();
                        obj.hasCapsuleCollider = true;
                        obj.capsuleCollider = CapsuleColliderComponent{};
                        if (onDirty_) onDirty_();
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
                if (!anyAvailable) {
                    ImGui::TextDisabled("All supported components are already added.");
                }
                ImGui::EndPopup();
            }

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

            bool removeMeshFilter = false;
            bool meshFilterOpen = false;
            if (obj.hasMeshFilter) {
                meshFilterOpen = RenderRemovableComponentHeader("Mesh Filter", "MeshFilterHeader", removeMeshFilter);
            }
            if (removeMeshFilter) {
                PushUndoState();
                obj.hasMeshFilter = false;
                obj.meshFilter = MeshFilterComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasMeshFilter && meshFilterOpen) {
                const std::string meshType = obj.meshFilter.meshType.empty() ? obj.type : obj.meshFilter.meshType;
                const bool meshFilterEnabledBefore = obj.meshFilter.enabled;
                if (ImGui::Checkbox("Enabled##MeshFilter", &obj.meshFilter.enabled)) {
                    const bool meshFilterEnabledAfter = obj.meshFilter.enabled;
                    obj.meshFilter.enabled = meshFilterEnabledBefore;
                    PushUndoState();
                    obj.meshFilter.enabled = meshFilterEnabledAfter;
                    if (onDirty_) onDirty_();
                }
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
                    ImGui::TextWrapped("Source: %s", obj.meshFilter.sourcePath.empty() ? "(none)" : obj.meshFilter.sourcePath.c_str());
                    ImGui::TextDisabled("Submesh Index: %d", obj.meshFilter.meshIndex);
                    ImGui::TextWrapped("Imported Material: %s", obj.meshFilter.importedMaterialName.empty() ? "(none)" : obj.meshFilter.importedMaterialName.c_str());
                    ImGui::TextWrapped("OBJ Diffuse: %s", obj.meshFilter.diffuseTexturePath.empty() ? "(none)" : obj.meshFilter.diffuseTexturePath.c_str());
                } else {
                    ImGui::TextDisabled("Built-in mesh: %s", meshType.c_str());
                }
            }

            bool removeMeshRenderer = false;
            bool meshRendererOpen = false;
            if (obj.hasMeshRenderer) {
                meshRendererOpen = RenderRemovableComponentHeader("Mesh Renderer", "MeshRendererHeader", removeMeshRenderer);
            }
            if (removeMeshRenderer) {
                PushUndoState();
                obj.hasMeshRenderer = false;
                obj.meshRenderer = MeshRendererComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasMeshRenderer && meshRendererOpen) {
                const bool meshRendererEnabledBefore = obj.meshRenderer.enabled;
                if (ImGui::Checkbox("Enabled##MeshRenderer", &obj.meshRenderer.enabled)) {
                    const bool meshRendererEnabledAfter = obj.meshRenderer.enabled;
                    obj.meshRenderer.enabled = meshRendererEnabledBefore;
                    PushUndoState();
                    obj.meshRenderer.enabled = meshRendererEnabledAfter;
                    if (onDirty_) onDirty_();
                }
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
                    if (ImGui::CollapsingHeader("Material Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
                        RenderMaterialProperties(materialId, false);
                    }
                } else {
                    ImGui::TextDisabled("Material asset not loaded.");
                }
            }

            bool removeScripts = false;
            bool scriptsOpen = false;
            if (obj.hasScriptComponent) {
                scriptsOpen = RenderRemovableComponentHeader("Scripts", "ScriptsHeader", removeScripts);
            }
            if (removeScripts) {
                PushUndoState();
                obj.hasScriptComponent = false;
                obj.scriptComponent = ScriptComponent{};
                if (scriptsRunning_) {
                    RebuildScriptRuntime();
                }
                if (onDirty_) onDirty_();
            } else if (obj.hasScriptComponent && scriptsOpen) {
                const bool scriptsEnabledBefore = obj.scriptComponent.enabled;
                if (ImGui::Checkbox("Enabled##Scripts", &obj.scriptComponent.enabled)) {
                    const bool scriptsEnabledAfter = obj.scriptComponent.enabled;
                    obj.scriptComponent.enabled = scriptsEnabledBefore;
                    PushUndoState();
                    obj.scriptComponent.enabled = scriptsEnabledAfter;
                    if (scriptsRunning_) {
                        RebuildScriptRuntime();
                    }
                    if (onDirty_) onDirty_();
                }
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
            if (obj.hasRigidbody) {
                rigidbodyOpen = RenderRemovableComponentHeader("Rigidbody", "RigidbodyHeader", removeRigidbody);
            }
            if (removeRigidbody) {
                PushUndoState();
                obj.hasRigidbody = false;
                obj.rigidbody = RigidbodyComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasRigidbody && rigidbodyOpen) {
                const bool rigidbodyEnabledBefore = obj.rigidbody.enabled;
                if (ImGui::Checkbox("Enabled##Rigidbody", &obj.rigidbody.enabled)) {
                    const bool rigidbodyEnabledAfter = obj.rigidbody.enabled;
                    obj.rigidbody.enabled = rigidbodyEnabledBefore;
                    PushUndoState();
                    obj.rigidbody.enabled = rigidbodyEnabledAfter;
                    if (onDirty_) onDirty_();
                }

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
                    PushUndoState();
                    obj.rigidbody.mass = (std::max)(0.0001f, mass);
                    if (onDirty_) onDirty_();
                }

                const bool useGravityBefore = obj.rigidbody.useGravity;
                if (ImGui::Checkbox("Use Gravity", &obj.rigidbody.useGravity)) {
                    const bool useGravityAfter = obj.rigidbody.useGravity;
                    obj.rigidbody.useGravity = useGravityBefore;
                    PushUndoState();
                    obj.rigidbody.useGravity = useGravityAfter;
                    if (onDirty_) onDirty_();
                }

                glm::vec3 velocity = obj.rigidbody.velocity;
                if (ImGui::DragFloat3("Velocity", &velocity.x, 0.1f)) {
                    PushUndoState();
                    obj.rigidbody.velocity = velocity;
                    if (onDirty_) onDirty_();
                }
            }

            bool removeBoxCollider = false;
            bool boxColliderOpen = false;
            if (obj.hasBoxCollider) {
                boxColliderOpen = RenderRemovableComponentHeader("Box Collider", "BoxColliderHeader", removeBoxCollider);
            }
            if (removeBoxCollider) {
                PushUndoState();
                obj.hasBoxCollider = false;
                obj.boxCollider = BoxColliderComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasBoxCollider && boxColliderOpen) {
                const bool colliderEnabledBefore = obj.boxCollider.enabled;
                if (ImGui::Checkbox("Enabled##BoxCollider", &obj.boxCollider.enabled)) {
                    const bool colliderEnabledAfter = obj.boxCollider.enabled;
                    obj.boxCollider.enabled = colliderEnabledBefore;
                    PushUndoState();
                    obj.boxCollider.enabled = colliderEnabledAfter;
                    if (onDirty_) onDirty_();
                }

                const bool isTriggerBefore = obj.boxCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger", &obj.boxCollider.isTrigger)) {
                    const bool isTriggerAfter = obj.boxCollider.isTrigger;
                    obj.boxCollider.isTrigger = isTriggerBefore;
                    PushUndoState();
                    obj.boxCollider.isTrigger = isTriggerAfter;
                    if (onDirty_) onDirty_();
                }

                glm::vec3 center = obj.boxCollider.center;
                if (ImGui::DragFloat3("Center", &center.x, 0.05f)) {
                    PushUndoState();
                    obj.boxCollider.center = center;
                    if (onDirty_) onDirty_();
                }

                glm::vec3 size = obj.boxCollider.size;
                if (ImGui::DragFloat3("Size", &size.x, 0.05f, 0.001f, 100000.0f)) {
                    PushUndoState();
                    obj.boxCollider.size = {
                        (std::max)(0.001f, size.x),
                        (std::max)(0.001f, size.y),
                        (std::max)(0.001f, size.z)
                    };
                    if (onDirty_) onDirty_();
                }
            }

            bool removeSphereCollider = false;
            bool sphereColliderOpen = false;
            if (obj.hasSphereCollider) {
                sphereColliderOpen = RenderRemovableComponentHeader("Sphere Collider", "SphereColliderHeader", removeSphereCollider);
            }
            if (removeSphereCollider) {
                PushUndoState();
                obj.hasSphereCollider = false;
                obj.sphereCollider = SphereColliderComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasSphereCollider && sphereColliderOpen) {
                const bool colliderEnabledBefore = obj.sphereCollider.enabled;
                if (ImGui::Checkbox("Enabled##SphereCollider", &obj.sphereCollider.enabled)) {
                    const bool colliderEnabledAfter = obj.sphereCollider.enabled;
                    obj.sphereCollider.enabled = colliderEnabledBefore;
                    PushUndoState();
                    obj.sphereCollider.enabled = colliderEnabledAfter;
                    if (onDirty_) onDirty_();
                }

                const bool isTriggerBefore = obj.sphereCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger##SphereCollider", &obj.sphereCollider.isTrigger)) {
                    const bool isTriggerAfter = obj.sphereCollider.isTrigger;
                    obj.sphereCollider.isTrigger = isTriggerBefore;
                    PushUndoState();
                    obj.sphereCollider.isTrigger = isTriggerAfter;
                    if (onDirty_) onDirty_();
                }

                glm::vec3 center = obj.sphereCollider.center;
                if (ImGui::DragFloat3("Center##SphereCollider", &center.x, 0.05f)) {
                    PushUndoState();
                    obj.sphereCollider.center = center;
                    if (onDirty_) onDirty_();
                }

                float radius = obj.sphereCollider.radius;
                if (ImGui::DragFloat("Radius##SphereCollider", &radius, 0.05f, 0.001f, 100000.0f)) {
                    PushUndoState();
                    obj.sphereCollider.radius = (std::max)(0.001f, radius);
                    if (onDirty_) onDirty_();
                }
            }

            bool removeCapsuleCollider = false;
            bool capsuleColliderOpen = false;
            if (obj.hasCapsuleCollider) {
                capsuleColliderOpen = RenderRemovableComponentHeader("Capsule Collider", "CapsuleColliderHeader", removeCapsuleCollider);
            }
            if (removeCapsuleCollider) {
                PushUndoState();
                obj.hasCapsuleCollider = false;
                obj.capsuleCollider = CapsuleColliderComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasCapsuleCollider && capsuleColliderOpen) {
                const bool colliderEnabledBefore = obj.capsuleCollider.enabled;
                if (ImGui::Checkbox("Enabled##CapsuleCollider", &obj.capsuleCollider.enabled)) {
                    const bool colliderEnabledAfter = obj.capsuleCollider.enabled;
                    obj.capsuleCollider.enabled = colliderEnabledBefore;
                    PushUndoState();
                    obj.capsuleCollider.enabled = colliderEnabledAfter;
                    if (onDirty_) onDirty_();
                }

                const bool isTriggerBefore = obj.capsuleCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger##CapsuleCollider", &obj.capsuleCollider.isTrigger)) {
                    const bool isTriggerAfter = obj.capsuleCollider.isTrigger;
                    obj.capsuleCollider.isTrigger = isTriggerBefore;
                    PushUndoState();
                    obj.capsuleCollider.isTrigger = isTriggerAfter;
                    if (onDirty_) onDirty_();
                }

                glm::vec3 center = obj.capsuleCollider.center;
                if (ImGui::DragFloat3("Center##CapsuleCollider", &center.x, 0.05f)) {
                    PushUndoState();
                    obj.capsuleCollider.center = center;
                    if (onDirty_) onDirty_();
                }

                float radius = obj.capsuleCollider.radius;
                if (ImGui::DragFloat("Radius##CapsuleCollider", &radius, 0.05f, 0.001f, 100000.0f)) {
                    PushUndoState();
                    obj.capsuleCollider.radius = (std::max)(0.001f, radius);
                    obj.capsuleCollider.height = (std::max)(obj.capsuleCollider.height, obj.capsuleCollider.radius * 2.0f);
                    if (onDirty_) onDirty_();
                }

                float height = obj.capsuleCollider.height;
                if (ImGui::DragFloat("Height##CapsuleCollider", &height, 0.05f, 0.001f, 100000.0f)) {
                    PushUndoState();
                    obj.capsuleCollider.height = (std::max)(obj.capsuleCollider.radius * 2.0f, height);
                    if (onDirty_) onDirty_();
                }
            }

            bool removeCamera = false;
            bool cameraOpen = false;
            if (obj.hasCamera) {
                cameraOpen = RenderRemovableComponentHeader("Camera", "CameraHeader", removeCamera);
            }
            if (removeCamera) {
                PushUndoState();
                obj.hasCamera = false;
                obj.camera = CameraComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasCamera && cameraOpen) {
                const bool cameraEnabledBefore = obj.camera.enabled;
                if (ImGui::Checkbox("Enabled##Camera", &obj.camera.enabled)) {
                    const bool cameraEnabledAfter = obj.camera.enabled;
                    obj.camera.enabled = cameraEnabledBefore;
                    PushUndoState();
                    obj.camera.enabled = cameraEnabledAfter;
                    if (onDirty_) onDirty_();
                }

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
                    PushUndoState();
                    obj.camera.fieldOfViewDegrees = (std::max)(1.0f, (std::min)(179.0f, fov));
                    if (onDirty_) onDirty_();
                }

                float nearClip = obj.camera.nearClip;
                if (ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.001f, 100000.0f)) {
                    PushUndoState();
                    obj.camera.nearClip = (std::max)(0.001f, nearClip);
                    obj.camera.farClip = (std::max)(obj.camera.nearClip + 0.001f, obj.camera.farClip);
                    if (onDirty_) onDirty_();
                }

                float farClip = obj.camera.farClip;
                if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, 0.002f, 1000000.0f)) {
                    PushUndoState();
                    obj.camera.farClip = (std::max)(obj.camera.nearClip + 0.001f, farClip);
                    if (onDirty_) onDirty_();
                }

                glm::vec4 clearColor = obj.camera.clearColor;
                if (ImGui::ColorEdit4("Clear Color", &clearColor.x)) {
                    PushUndoState();
                    obj.camera.clearColor = clearColor;
                    if (onDirty_) onDirty_();
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

    if (!materialManager_.Exists(materialId)) {
        materialManager_.LoadAll();
    }
    if (materialManager_.Exists(materialId)) {
        if (console_) {
            console_->AddError("Material already exists: " + materialId);
        }
        return false;
    }

    Material& material = materialManager_.CreateDefault(materialId, true);
    material.name = materialId;
    materialManager_.Save(materialId, material);
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

    SceneObject& obj = objects_[selectedIndex_];
    PushUndoState();
    obj.hasMeshRenderer = true;
    obj.meshRenderer.materialId = materialId;
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


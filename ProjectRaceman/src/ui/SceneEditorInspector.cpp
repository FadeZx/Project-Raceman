#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"
#include "../physics/VehicleConfig.h"
#include "../physics/VehiclePhysics.h"
#include "../scripting/ScriptRegistry.h"

#include <glad/glad.h>
#include <stb_image.h>

#include <cstdio>
#include <cstdint>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/norm.hpp>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

constexpr const char* kInspectorComponentReorderPayload = "RM_COMP_REORDER";

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

bool RenderInspectorDragFloat2(const char* label, const char* id, float* values, float speed, float min = 0.0f, float max = 0.0f) {
    if (IsNarrowInspectorLayout()) {
        RenderInspectorLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::DragFloat2(id, values, speed, min, max);
    }
    return ImGui::DragFloat2(label, values, speed, min, max);
}

bool RenderInspectorDragFloat4(const char* label, const char* id, float* values, float speed, float min = 0.0f, float max = 0.0f) {
    if (IsNarrowInspectorLayout()) {
        RenderInspectorLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::DragFloat4(id, values, speed, min, max);
    }
    return ImGui::DragFloat4(label, values, speed, min, max);
}

bool RenderInspectorDragFloat(const char* label, const char* id, float* value, float speed, float min = 0.0f, float max = 0.0f) {
    if (IsNarrowInspectorLayout()) {
        RenderInspectorLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::DragFloat(id, value, speed, min, max);
    }
    return ImGui::DragFloat(label, value, speed, min, max);
}

bool RenderInspectorDragInt(const char* label, const char* id, int* value, float speed, int min = 0, int max = 0) {
    if (IsNarrowInspectorLayout()) {
        RenderInspectorLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::DragInt(id, value, speed, min, max);
    }
    return ImGui::DragInt(label, value, speed, min, max);
}

bool RenderInspectorAxisToggles(const char* label, const char* idPrefix, bool& x, bool& y, bool& z) {
    bool changed = false;
    RenderInspectorLabel(label);
    ImGui::PushID(idPrefix);
    changed |= ImGui::Checkbox("X", &x);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Y", &y);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Z", &z);
    ImGui::PopID();
    return changed;
}

bool RenderInspectorLinkedScaleEditor(const char* label,
                                      const char* idPrefix,
                                      float* values,
                                      float speed,
                                      bool& linked,
                                      bool* outDeactivated = nullptr) {
    const glm::vec3 before{values[0], values[1], values[2]};
    bool changed = false;

    ImGui::PushID(idPrefix);
    if (IsNarrowInspectorLayout()) {
        RenderInspectorLabel(label);
        if (ImGui::SmallButton(linked ? "Linked##scaleLink" : "Free##scaleLink")) {
            linked = !linked;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle uniform scale editing");
        }
        ImGui::SetNextItemWidth(-1.0f);
        changed = ImGui::DragFloat3("##scaleValues", values, speed);
    } else {
        changed = ImGui::DragFloat3(label, values, speed);
        ImGui::SameLine();
        if (ImGui::SmallButton(linked ? "Linked##scaleLink" : "Free##scaleLink")) {
            linked = !linked;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle uniform scale editing");
        }
    }
    if (outDeactivated != nullptr) {
        *outDeactivated = ImGui::IsItemDeactivated();
    }
    ImGui::PopID();

    if (!changed || !linked) {
        return changed;
    }

    const glm::vec3 after{values[0], values[1], values[2]};
    const float deltaX = std::fabs(after.x - before.x);
    const float deltaY = std::fabs(after.y - before.y);
    const float deltaZ = std::fabs(after.z - before.z);

    float uniformValue = after.x;
    if (deltaY >= deltaX && deltaY >= deltaZ) {
        uniformValue = after.y;
    } else if (deltaZ >= deltaX && deltaZ >= deltaY) {
        uniformValue = after.z;
    }

    values[0] = uniformValue;
    values[1] = uniformValue;
    values[2] = uniformValue;
    return true;
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

bool RenderScriptFieldEditor(const ScriptFieldDefinition& definition, ScriptFieldEntry& field) {
    const char* label = definition.label.empty() ? definition.name.c_str() : definition.label.c_str();
    const std::string id = "##scriptField_" + definition.name;
    switch (definition.type) {
    case ScriptFieldType::Bool: {
        bool value = std::get<bool>(field.value);
        if (ImGui::Checkbox(label, &value)) {
            field.value = value;
            return true;
        }
        return false;
    }
    case ScriptFieldType::Int: {
        int value = std::get<int>(field.value);
        if (RenderInspectorDragInt(label, id.c_str(), &value, 1.0f)) {
            field.value = value;
            return true;
        }
        return false;
    }
    case ScriptFieldType::Float: {
        float value = std::get<float>(field.value);
        if (RenderInspectorDragFloat(label, id.c_str(), &value, 0.05f)) {
            field.value = value;
            return true;
        }
        return false;
    }
    case ScriptFieldType::String: {
        std::string value = std::get<std::string>(field.value);
        char buffer[256]{};
        std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
        if (RenderInspectorInputText(label, id.c_str(), buffer, sizeof(buffer))) {
            field.value = std::string(buffer);
            return true;
        }
        return false;
    }
    case ScriptFieldType::Vec2: {
        glm::vec2 value = std::get<glm::vec2>(field.value);
        if (RenderInspectorDragFloat2(label, id.c_str(), &value.x, 0.05f)) {
            field.value = value;
            return true;
        }
        return false;
    }
    case ScriptFieldType::Vec3: {
        glm::vec3 value = std::get<glm::vec3>(field.value);
        if (RenderInspectorDragFloat3(label, id.c_str(), &value.x, 0.05f)) {
            field.value = value;
            return true;
        }
        return false;
    }
    case ScriptFieldType::Vec4: {
        glm::vec4 value = std::get<glm::vec4>(field.value);
        if (RenderInspectorDragFloat4(label, id.c_str(), &value.x, 0.05f)) {
            field.value = value;
            return true;
        }
        return false;
    }
    }
    return false;
}

bool TryLoadVehicleConfigForPath(const std::string& projectPath, raceman::physics::VehicleConfig& outConfig) {
    if (projectPath.empty()) {
        return false;
    }
    try {
        outConfig = raceman::physics::VehicleConfigLoader::loadFromFile(ProjectAssetPathToAbsolute(projectPath).string());
        return true;
    } catch (...) {
        return false;
    }
}

void SyncVehicleWheelBindings(VehicleComponent& vehicle, const raceman::physics::VehicleConfig& config) {
    std::vector<VehicleWheelBinding> syncedBindings;
    syncedBindings.reserve(config.wheels.size());
    for (const raceman::physics::WheelConfig& wheel : config.wheels) {
        VehicleWheelBinding binding;
        binding.wheelName = wheel.name;
        const auto existing = std::find_if(vehicle.wheelBindings.begin(), vehicle.wheelBindings.end(),
            [&](const VehicleWheelBinding& candidate) {
                return candidate.wheelName == wheel.name;
            });
        if (existing != vehicle.wheelBindings.end()) {
            binding.objectId = existing->objectId;
        }
        syncedBindings.push_back(std::move(binding));
    }
    vehicle.wheelBindings = std::move(syncedBindings);
}

bool RenderRemovableComponentHeader(const char* label,
                                    const char* id,
                                    unsigned int textureId,
                                    bool* enabled,
                                    bool& enabledChanged,
                                    bool& removeRequested,
                                    SceneInspectorComponentType* componentType = nullptr,
                                    SceneInspectorComponentType* outDraggedType = nullptr,
                                    SceneInspectorComponentType* outDropTargetType = nullptr,
                                    bool* outHeaderActive = nullptr,
                                    bool* outHeaderToggledOpen = nullptr) {
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
    if (outHeaderActive != nullptr) {
        *outHeaderActive = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    }
    if (outHeaderToggledOpen != nullptr) {
        *outHeaderToggledOpen = ImGui::IsItemToggledOpen();
    }
    if (componentType != nullptr) {
        if (ImGui::BeginDragDropSource()) {
            const SceneInspectorComponentType payloadType = *componentType;
            ImGui::SetDragDropPayload(kInspectorComponentReorderPayload, &payloadType, sizeof(payloadType));
            ImGui::TextUnformatted(label);
            if (outDraggedType != nullptr) {
                *outDraggedType = payloadType;
            }
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kInspectorComponentReorderPayload)) {
                if (payload->DataSize == sizeof(SceneInspectorComponentType) && outDropTargetType != nullptr) {
                    *outDropTargetType = *componentType;
                    if (outDraggedType != nullptr) {
                        *outDraggedType = *static_cast<const SceneInspectorComponentType*>(payload->Data);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    const float removeButtonWidth = ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - removeButtonWidth);
    ImGui::SetNextItemAllowOverlap();
    removeRequested = ImGui::Button("Remove");
    ImGui::PopID();
    return open;
}

void RenderComponentClipboardButtons(bool canPaste, bool& copyRequested, bool& pasteRequested) {
    copyRequested = ImGui::SmallButton("Copy");
    ImGui::SameLine();
    ImGui::BeginDisabled(!canPaste);
    pasteRequested = ImGui::SmallButton("Paste");
    ImGui::EndDisabled();
    ImGui::Separator();
}

bool RenderColliderTypeCombo(const char* label, const char* comboId, SceneColliderType currentType, bool allowNone, const char* previewOverride, SceneColliderType& outType) {
    const char* preview = previewOverride != nullptr ? previewOverride : SceneColliderTypeLabel(currentType);
    if (!ImGui::BeginCombo(label, preview)) {
        return false;
    }

    bool changed = false;
    const SceneColliderType types[] = {
        SceneColliderType::Box,
        SceneColliderType::Sphere,
        SceneColliderType::Capsule,
        SceneColliderType::Plane,
        SceneColliderType::Mesh
    };

    if (allowNone) {
        const bool selected = currentType == SceneColliderType::None;
        if (ImGui::Selectable("None", selected)) {
            outType = SceneColliderType::None;
            changed = true;
        }
    }

    for (SceneColliderType type : types) {
        const bool selected = currentType == type;
        const std::string selectableLabel = std::string(SceneColliderTypeLabel(type)) + "##" + comboId + "_" + SceneColliderTypeLabel(type);
        if (ImGui::Selectable(selectableLabel.c_str(), selected)) {
            outType = type;
            changed = true;
        }
    }

    ImGui::EndCombo();
    return changed;
}

const char* MeshColliderModeLabel(MeshColliderMode mode) {
    return mode == MeshColliderMode::ConvexHull ? "Convex Hull" : "Triangle Mesh";
}

bool RenderMeshColliderModeCombo(const char* label, const char* comboId, MeshColliderMode currentMode, const char* previewOverride, MeshColliderMode& outMode) {
    const char* preview = previewOverride != nullptr ? previewOverride : MeshColliderModeLabel(currentMode);
    if (!ImGui::BeginCombo(label, preview)) {
        return false;
    }

    bool changed = false;
    const MeshColliderMode modes[] = {
        MeshColliderMode::TriangleMesh,
        MeshColliderMode::ConvexHull
    };
    for (MeshColliderMode mode : modes) {
        const bool selected = currentMode == mode;
        const std::string selectableLabel = std::string(MeshColliderModeLabel(mode)) + "##" + comboId + "_" + MeshColliderModeLabel(mode);
        if (ImGui::Selectable(selectableLabel.c_str(), selected)) {
            outMode = mode;
            changed = true;
        }
        if (selected) {
            ImGui::SetItemDefaultFocus();
        }
    }

    ImGui::EndCombo();
    return changed;
}

bool RenderPhysicsLayerCombo(const char* label,
                             const char* comboId,
                             int currentLayer,
                             const PhysicsLayerNames& layerNames,
                             int& outLayer) {
    const int safeCurrentLayer = (std::max)(0, (std::min)(kPhysicsLayerCount - 1, currentLayer));
    const std::string preview = layerNames[static_cast<std::size_t>(safeCurrentLayer)].empty()
        ? ("Layer " + std::to_string(safeCurrentLayer))
        : layerNames[static_cast<std::size_t>(safeCurrentLayer)];
    if (!ImGui::BeginCombo(label, preview.c_str())) {
        return false;
    }

    bool changed = false;
    for (int layerIndex = 0; layerIndex < kPhysicsLayerCount; ++layerIndex) {
        const std::string layerName = layerNames[static_cast<std::size_t>(layerIndex)].empty()
            ? ("Layer " + std::to_string(layerIndex))
            : layerNames[static_cast<std::size_t>(layerIndex)];
        const bool selected = layerIndex == safeCurrentLayer;
        const std::string selectableLabel = layerName + "##" + comboId + "_" + std::to_string(layerIndex);
        if (ImGui::Selectable(selectableLabel.c_str(), selected)) {
            outLayer = layerIndex;
            changed = true;
        }
    }

    ImGui::EndCombo();
    return changed;
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
        inspectorPanelHovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        inspectorPanelFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        inspectorKeyboardTargetComponentKey_.clear();
        inspectorKeyboardTargetObjectId_.clear();
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

            // Type / Id (read-only)
            ImGui::TextDisabled("Type: GameObject");
            if (IsNarrowInspectorLayout()) {
                ImGui::TextDisabled("ID:");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(obj.id.c_str());
                ImGui::PopTextWrapPos();
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("| ID: %s", obj.id.c_str());
            }

            int physicsLayer = ClampPhysicsLayerIndex(obj.physicsLayer);
            if (RenderPhysicsLayerCombo("Physics Layer", "singlePhysicsLayer", obj.physicsLayer, physicsLayerNames_, physicsLayer)) {
                PushUndoState();
                obj.physicsLayer = physicsLayer;
                if (onDirty_) onDirty_();
            }

            auto renderAddComponentMenu = [&]() {
                bool anyAvailable = false;
                if (!obj.hasMeshFilter) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Mesh Filter")) {
                        PushUndoState();
                        obj.hasMeshFilter = true;
                        obj.meshFilter = MeshFilterComponent{};
                        obj.meshFilter.meshType = "Mesh";
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
                if (!obj.hasVehicle) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Vehicle")) {
                        PushUndoState();
                        obj.hasVehicle = true;
                        obj.vehicle = VehicleComponent{};
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
                if (!HasColliderComponent(obj)) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Collider")) {
                        PushUndoState();
                        SetActiveColliderType(obj, SceneColliderType::Box);
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
                if (!obj.hasCinemachine) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Cinemachine Camera")) {
                        PushUndoState();
                        obj.hasCinemachine = true;
                        obj.cinemachine = CinemachineCameraComponent{};
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

            SyncInspectorComponentOrder(obj);
            SceneInspectorComponentType reorderDraggedType = SceneInspectorComponentType::Transform;
            SceneInspectorComponentType reorderTargetType = SceneInspectorComponentType::Transform;
            auto componentKeyFor = [&](SceneInspectorComponentType type) {
                const char* typeName = "Transform";
                switch (type) {
                case SceneInspectorComponentType::Transform: typeName = "Transform"; break;
                case SceneInspectorComponentType::MeshFilter: typeName = "MeshFilter"; break;
                case SceneInspectorComponentType::MeshRenderer: typeName = "MeshRenderer"; break;
                case SceneInspectorComponentType::Script: typeName = "Script"; break;
                case SceneInspectorComponentType::Rigidbody: typeName = "Rigidbody"; break;
                case SceneInspectorComponentType::Vehicle: typeName = "Vehicle"; break;
                case SceneInspectorComponentType::CharacterController: typeName = "CharacterController"; break;
                case SceneInspectorComponentType::Collider: typeName = "Collider"; break;
                case SceneInspectorComponentType::Camera: typeName = "Camera"; break;
                case SceneInspectorComponentType::Cinemachine: typeName = "Cinemachine"; break;
                case SceneInspectorComponentType::Light: typeName = "Light"; break;
                }
                return obj.id + "|" + typeName;
            };
            auto prepareComponentOpenState = [&](SceneInspectorComponentType type) {
                const std::string key = componentKeyFor(type);
                bool openState = true;
                const auto it = inspectorComponentOpenStates_.find(key);
                if (it != inspectorComponentOpenStates_.end()) {
                    openState = it->second;
                }
                if (pendingInspectorToggleComponentKey_ == key) {
                    openState = !openState;
                    inspectorComponentOpenStates_[key] = openState;
                    pendingInspectorToggleComponentKey_.clear();
                }
                ImGui::SetNextItemOpen(openState, ImGuiCond_Always);
                return key;
            };
            auto finishComponentHeaderState = [&](const std::string& key,
                                                  SceneInspectorComponentType type,
                                                  bool headerActive,
                                                  bool headerToggledOpen,
                                                  bool open) {
                if (headerActive) {
                    inspectorKeyboardTargetComponentKey_ = key;
                    inspectorKeyboardTargetObjectId_ = obj.id;
                    inspectorKeyboardTargetComponentType_ = type;
                }
                if (headerToggledOpen) {
                    inspectorComponentOpenStates_[key] = open;
                }
            };

            for (SceneInspectorComponentType currentComponentToRender : obj.inspectorComponentOrder) {
            if (currentComponentToRender == SceneInspectorComponentType::Transform) {
            const std::string transformComponentKey = prepareComponentOpenState(SceneInspectorComponentType::Transform);
            RenderComponentIcon(GetComponentIconTexture("component-transform.png"));
            const bool transformOpen = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
            finishComponentHeaderState(
                transformComponentKey,
                SceneInspectorComponentType::Transform,
                ImGui::IsItemHovered() || ImGui::IsItemFocused(),
                ImGui::IsItemToggledOpen(),
                transformOpen);
            if (transformOpen) {
                bool copyRequested = false;
                bool pasteRequested = false;
                RenderComponentClipboardButtons(componentClipboard_.hasValue && componentClipboard_.type == SceneInspectorComponentType::Transform, copyRequested, pasteRequested);
                if (copyRequested) {
                    CopyInspectorComponentToClipboard(selectedIndex_, SceneInspectorComponentType::Transform);
                }
                if (pasteRequested) {
                    PasteInspectorComponentFromClipboard({selectedIndex_}, SceneInspectorComponentType::Transform);
                }
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
                bool scaleDeactivated = false;
                if (RenderInspectorLinkedScaleEditor("Scale", "##scale", &obj.transform.scale.x, 0.1f, linkedScaleValues_, &scaleDeactivated)) {
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
                if (scaleDeactivated) {
                    inspectorEditActive_ = false;
                }
            }
            }

            if (currentComponentToRender == SceneInspectorComponentType::MeshFilter) {
            bool removeMeshFilter = false;
            bool meshFilterOpen = false;
            bool meshFilterEnabledChanged = false;
            bool meshFilterHeaderActive = false;
            bool meshFilterHeaderToggledOpen = false;
            const bool meshFilterEnabledBefore = obj.meshFilter.enabled;
            if (obj.hasMeshFilter) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::MeshFilter;
                const std::string meshFilterComponentKey = prepareComponentOpenState(SceneInspectorComponentType::MeshFilter);
                meshFilterOpen = RenderRemovableComponentHeader("Mesh Filter", "MeshFilterHeader", GetComponentIconTexture("component-mesh-filter.png"), &obj.meshFilter.enabled, meshFilterEnabledChanged, removeMeshFilter, &componentType, &reorderDraggedType, &reorderTargetType, &meshFilterHeaderActive, &meshFilterHeaderToggledOpen);
                finishComponentHeaderState(meshFilterComponentKey, SceneInspectorComponentType::MeshFilter, meshFilterHeaderActive, meshFilterHeaderToggledOpen, meshFilterOpen);
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
                const std::string meshType = obj.meshFilter.meshType.empty() ? std::string("Mesh") : obj.meshFilter.meshType;
                const std::string meshButtonLabel = (meshType == "Mesh" && !obj.meshFilter.sourcePath.empty())
                    ? (fs::path(obj.meshFilter.sourcePath).filename().string() + "##selectMeshFilter")
                    : (meshType + "##selectMeshFilter");
                ImGui::TextDisabled("Mesh Type:");
                ImGui::SameLine();
                if (ImGui::Button(meshButtonLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                    assetPickerMode_ = ProjectAssetPickerMode::ReplaceMesh;
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kMeshAssetPayload)) {
                        const char* path = static_cast<const char*>(payload->Data);
                        if (path != nullptr) {
                            ReplaceSelectedMeshFromObj(path);
                        }
                    }
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                        const char* path = static_cast<const char*>(payload->Data);
                        if (path != nullptr && IsMeshAssetPath(path)) {
                            ReplaceSelectedMeshFromObj(path);
                        }
                    }
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
                    RenderInspectorWrappedValue("Imported Diffuse:", obj.meshFilter.diffuseTexturePath.empty() ? "(none)" : obj.meshFilter.diffuseTexturePath);
                } else {
                    ImGui::TextDisabled("Built-in mesh: %s", meshType.c_str());
                }
            }
            }

            if (currentComponentToRender == SceneInspectorComponentType::MeshRenderer) {
            bool removeMeshRenderer = false;
            bool meshRendererOpen = false;
            bool meshRendererEnabledChanged = false;
            bool meshRendererHeaderActive = false;
            bool meshRendererHeaderToggledOpen = false;
            const bool meshRendererEnabledBefore = obj.meshRenderer.enabled;
            if (obj.hasMeshRenderer) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::MeshRenderer;
                const std::string meshRendererComponentKey = prepareComponentOpenState(SceneInspectorComponentType::MeshRenderer);
                meshRendererOpen = RenderRemovableComponentHeader("Mesh Renderer", "MeshRendererHeader", GetComponentIconTexture("component-mesh-renderer.png"), &obj.meshRenderer.enabled, meshRendererEnabledChanged, removeMeshRenderer, &componentType, &reorderDraggedType, &reorderTargetType, &meshRendererHeaderActive, &meshRendererHeaderToggledOpen);
                finishComponentHeaderState(meshRendererComponentKey, SceneInspectorComponentType::MeshRenderer, meshRendererHeaderActive, meshRendererHeaderToggledOpen, meshRendererOpen);
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
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                        const char* projectPath = static_cast<const char*>(payload->Data);
                        if (projectPath != nullptr && IsMaterialAssetPath(projectPath)) {
                            AssignMaterialToSelected(MaterialIdFromAssetPath(projectPath));
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
            }

            if (currentComponentToRender == SceneInspectorComponentType::Script) {
            bool removeScripts = false;
            bool scriptsOpen = false;
            bool scriptsEnabledChanged = false;
            bool scriptsHeaderActive = false;
            bool scriptsHeaderToggledOpen = false;
            const bool scriptsEnabledBefore = obj.scriptComponent.enabled;
            if (obj.hasScriptComponent) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::Script;
                const std::string scriptComponentKey = prepareComponentOpenState(SceneInspectorComponentType::Script);
                scriptsOpen = RenderRemovableComponentHeader("Scripts", "ScriptsHeader", GetComponentIconTexture("component-script.png"), &obj.scriptComponent.enabled, scriptsEnabledChanged, removeScripts, &componentType, &reorderDraggedType, &reorderTargetType, &scriptsHeaderActive, &scriptsHeaderToggledOpen);
                finishComponentHeaderState(scriptComponentKey, SceneInspectorComponentType::Script, scriptsHeaderActive, scriptsHeaderToggledOpen, scriptsOpen);
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
                    bool removeScript = false;
                    bool scriptEnabledChanged = false;
                    const bool scriptEnabledBefore = script.enabled;
                    const bool scriptTreeOpen = RenderRemovableComponentHeader(header.c_str(), "ScriptAttachmentHeader", GetComponentIconTexture("component-script.png"), &script.enabled, scriptEnabledChanged, removeScript);
                    if (removeScript) {
                        PushUndoState();
                        obj.scriptComponent.attachments.erase(obj.scriptComponent.attachments.begin() + scriptIndex);
                        if (scriptsRunning_) {
                            RebuildScriptRuntime();
                        }
                        if (onDirty_) onDirty_();
                        ImGui::PopID();
                        break;
                    }
                    if (scriptEnabledChanged) {
                        const bool scriptEnabledAfter = script.enabled;
                        script.enabled = scriptEnabledBefore;
                        PushUndoState();
                        script.enabled = scriptEnabledAfter;
                        if (scriptsRunning_) {
                            RebuildScriptRuntime();
                        }
                        if (onDirty_) onDirty_();
                    }
                    if (scriptTreeOpen) {
                        if (ImGui::IsItemHovered() && !script.scriptPath.empty()) {
                            ImGui::SetTooltip("Click to show in Browser: %s", script.scriptPath.c_str());
                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                SelectProjectFile(script.scriptPath);
                            }
                        }
                        ImGui::TextWrapped("Class: %s", script.scriptName.empty() ? "(missing)" : script.scriptName.c_str());
                        ImGui::TextWrapped("Source: %s", script.scriptPath.empty() ? "(none)" : script.scriptPath.c_str());
                        if (ImGui::IsItemHovered() && !script.scriptPath.empty()) {
                            ImGui::SetTooltip("Click to show in Browser.");
                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                SelectProjectFile(script.scriptPath);
                            }
                        }
                        const bool scriptRegistered = FindRegisteredScript(script.scriptName) != nullptr;
                        if (!scriptRegistered) {
                            ImGui::TextDisabled("Not registered in this build. Rebuild after creating or editing scripts.");
                        } else {
                            const std::vector<ScriptFieldDefinition> fieldDefinitions = GetRegisteredScriptFieldDefinitions(script.scriptName);
                            if (!fieldDefinitions.empty()) {
                                if (SyncAttachmentScriptFields(script) && onDirty_) {
                                    onDirty_();
                                }
                                ImGui::Separator();
                                ImGui::TextUnformatted("Fields");
                                for (const ScriptFieldDefinition& definition : fieldDefinitions) {
                                    auto fieldIt = std::find_if(script.fields.begin(), script.fields.end(), [&](const ScriptFieldEntry& field) {
                                        return field.name == definition.name;
                                    });
                                    if (fieldIt == script.fields.end()) {
                                        continue;
                                    }
                                    const ScriptFieldEntry fieldBefore = *fieldIt;
                                    if (RenderScriptFieldEditor(definition, *fieldIt)) {
                                        const ScriptFieldEntry fieldAfter = *fieldIt;
                                        *fieldIt = fieldBefore;
                                        PushUndoState();
                                        *fieldIt = fieldAfter;
                                        if (onDirty_) onDirty_();
                                    }
                                }
                            }
                        }
                    }
                    ImGui::PopID();
                }
            }
            }

            if (currentComponentToRender == SceneInspectorComponentType::Rigidbody) {
            bool removeRigidbody = false;
            bool rigidbodyOpen = false;
            bool rigidbodyEnabledChanged = false;
            bool rigidbodyHeaderActive = false;
            bool rigidbodyHeaderToggledOpen = false;
            const bool rigidbodyEnabledBefore = obj.rigidbody.enabled;
            if (obj.hasRigidbody) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::Rigidbody;
                const std::string rigidbodyComponentKey = prepareComponentOpenState(SceneInspectorComponentType::Rigidbody);
                rigidbodyOpen = RenderRemovableComponentHeader("Rigidbody", "RigidbodyHeader", GetComponentIconTexture("component-rigidbody.png"), &obj.rigidbody.enabled, rigidbodyEnabledChanged, removeRigidbody, &componentType, &reorderDraggedType, &reorderTargetType, &rigidbodyHeaderActive, &rigidbodyHeaderToggledOpen);
                finishComponentHeaderState(rigidbodyComponentKey, SceneInspectorComponentType::Rigidbody, rigidbodyHeaderActive, rigidbodyHeaderToggledOpen, rigidbodyOpen);
            }
            if (removeRigidbody) {
                PushUndoState();
                obj.hasRigidbody = false;
                obj.rigidbody = RigidbodyComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasRigidbody && rigidbodyEnabledChanged) {
                const bool rigidbodyEnabledAfter = obj.rigidbody.enabled;
                obj.rigidbody.enabled = rigidbodyEnabledBefore;
                PushUndoState();
                obj.rigidbody.enabled = rigidbodyEnabledAfter;
                if (onDirty_) onDirty_();
            }
            if (obj.hasRigidbody && rigidbodyOpen) {
                if (ImGui::CollapsingHeader("Body", ImGuiTreeNodeFlags_DefaultOpen)) {
                    int bodyTypeIndex = obj.rigidbody.bodyType == RigidbodyBodyType::Static
                        ? 0
                        : (obj.rigidbody.bodyType == RigidbodyBodyType::Kinematic ? 1 : 2);
                    const char* bodyTypes[] = {"Static", "Kinematic", "Dynamic"};
                    if (ImGui::Combo("Body Type", &bodyTypeIndex, bodyTypes, 3)) {
                        PushUndoState();
                        obj.rigidbody.bodyType = bodyTypeIndex == 0
                            ? RigidbodyBodyType::Static
                            : (bodyTypeIndex == 1 ? RigidbodyBodyType::Kinematic : RigidbodyBodyType::Dynamic);
                        if (obj.rigidbody.bodyType == RigidbodyBodyType::Static) {
                            obj.rigidbody.velocity = {0.0f, 0.0f, 0.0f};
                            obj.rigidbody.angularVelocity = {0.0f, 0.0f, 0.0f};
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

                    float linearDamping = obj.rigidbody.linearDamping;
                    if (ImGui::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, 1000.0f)) {
                        beginInspectorContinuousEdit();
                        obj.rigidbody.linearDamping = (std::max)(0.0f, linearDamping);
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();

                    float angularDamping = obj.rigidbody.angularDamping;
                    if (ImGui::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, 1000.0f)) {
                        beginInspectorContinuousEdit();
                        obj.rigidbody.angularDamping = (std::max)(0.0f, angularDamping);
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                }

                if (ImGui::CollapsingHeader("Material")) {
                    float friction = obj.rigidbody.friction;
                    if (ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, 10.0f)) {
                        beginInspectorContinuousEdit();
                        obj.rigidbody.friction = (std::max)(0.0f, friction);
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();

                    float restitution = obj.rigidbody.restitution;
                    if (ImGui::DragFloat("Restitution", &restitution, 0.01f, 0.0f, 1.0f)) {
                        beginInspectorContinuousEdit();
                        obj.rigidbody.restitution = (std::max)(0.0f, restitution);
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                }

                if (ImGui::CollapsingHeader("Velocity")) {
                    glm::vec3 velocity = obj.rigidbody.velocity;
                    if (RenderInspectorDragFloat3("Velocity", "##rigidbodyVelocity", &velocity.x, 0.1f)) {
                        beginInspectorContinuousEdit();
                        obj.rigidbody.velocity = velocity;
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();

                    glm::vec3 angularVelocity = obj.rigidbody.angularVelocity;
                    if (RenderInspectorDragFloat3("Angular Velocity", "##rigidbodyAngularVelocity", &angularVelocity.x, 0.1f)) {
                        beginInspectorContinuousEdit();
                        obj.rigidbody.angularVelocity = angularVelocity;
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                }

                if (ImGui::CollapsingHeader("Constraints")) {
                    bool freezePositionX = obj.rigidbody.freezePositionX;
                    bool freezePositionY = obj.rigidbody.freezePositionY;
                    bool freezePositionZ = obj.rigidbody.freezePositionZ;
                    if (RenderInspectorAxisToggles("Freeze Position", "rigidbodyFreezePosition", freezePositionX, freezePositionY, freezePositionZ)) {
                        PushUndoState();
                        obj.rigidbody.freezePositionX = freezePositionX;
                        obj.rigidbody.freezePositionY = freezePositionY;
                        obj.rigidbody.freezePositionZ = freezePositionZ;
                        if (onDirty_) onDirty_();
                    }
                    bool freezeRotationX = obj.rigidbody.freezeRotationX;
                    bool freezeRotationY = obj.rigidbody.freezeRotationY;
                    bool freezeRotationZ = obj.rigidbody.freezeRotationZ;
                    if (RenderInspectorAxisToggles("Freeze Rotation", "rigidbodyFreezeRotation", freezeRotationX, freezeRotationY, freezeRotationZ)) {
                        PushUndoState();
                        obj.rigidbody.freezeRotationX = freezeRotationX;
                        obj.rigidbody.freezeRotationY = freezeRotationY;
                        obj.rigidbody.freezeRotationZ = freezeRotationZ;
                        if (onDirty_) onDirty_();
                    }
                }
            }
            }

            if (currentComponentToRender == SceneInspectorComponentType::Vehicle) {
            bool removeVehicle = false;
            bool vehicleOpen = false;
            bool vehicleEnabledChanged = false;
            bool vehicleHeaderActive = false;
            bool vehicleHeaderToggledOpen = false;
            const bool vehicleEnabledBefore = obj.vehicle.enabled;
            if (obj.hasVehicle) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::Vehicle;
                const std::string vehicleComponentKey = prepareComponentOpenState(SceneInspectorComponentType::Vehicle);
                vehicleOpen = RenderRemovableComponentHeader("Vehicle", "VehicleHeader", GetComponentIconTexture("component-vehicle.png"), &obj.vehicle.enabled, vehicleEnabledChanged, removeVehicle, &componentType, &reorderDraggedType, &reorderTargetType, &vehicleHeaderActive, &vehicleHeaderToggledOpen);
                finishComponentHeaderState(vehicleComponentKey, SceneInspectorComponentType::Vehicle, vehicleHeaderActive, vehicleHeaderToggledOpen, vehicleOpen);
            }
            if (removeVehicle) {
                PushUndoState();
                obj.hasVehicle = false;
                obj.vehicle = VehicleComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasVehicle && vehicleEnabledChanged) {
                const bool vehicleEnabledAfter = obj.vehicle.enabled;
                obj.vehicle.enabled = vehicleEnabledBefore;
                PushUndoState();
                obj.vehicle.enabled = vehicleEnabledAfter;
                if (onDirty_) onDirty_();
            }
            if (obj.hasVehicle && vehicleOpen) {
                if (ImGui::CollapsingHeader("Config", ImGuiTreeNodeFlags_DefaultOpen)) {
                std::string configDisplayName = "(none)";
                if (!obj.vehicle.configPath.empty()) {
                    configDisplayName = ProjectAssetDisplayFilename(obj.vehicle.configPath);
                }
                ImGui::TextDisabled("Asset:");
                ImGui::SameLine();
                const float vehicleConfigButtonWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
                if (ImGui::Button((configDisplayName + "##selectVehicleConfig").c_str(), ImVec2(vehicleConfigButtonWidth, 0.0f))) {
                    assetPickerMode_ = ProjectAssetPickerMode::AssignVehicleConfig;
                }
                if (!obj.vehicle.configPath.empty()) {
                    if (ImGui::Button("Edit Profile##vehicleConfigEdit")) {
                        OpenVehicleConfigEditor(obj.vehicle.configPath);
                    }
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                        const char* projectPath = static_cast<const char*>(payload->Data);
                        if (projectPath != nullptr && IsVehicleConfigAssetPath(projectPath)) {
                            AssignVehicleConfigToSelected(projectPath);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                } // Config header

                if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                const bool canTiltBefore = obj.vehicle.canTilt;
                if (ImGui::Checkbox("Can Tilt", &obj.vehicle.canTilt)) {
                    const bool canTiltAfter = obj.vehicle.canTilt;
                    obj.vehicle.canTilt = canTiltBefore;
                    PushUndoState();
                    obj.vehicle.canTilt = canTiltAfter;
                    if (onDirty_) onDirty_();
                }
                ImGui::TextDisabled("When disabled, vehicle cannot roll or pitch.");
                } // Settings header

                if (ImGui::CollapsingHeader("Chassis", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("Add Chassis Part")) {
                    PushUndoState();
                    obj.vehicle.chassisObjectIds.push_back({});
                    if (onDirty_) onDirty_();
                }
                if (obj.vehicle.chassisObjectIds.empty()) {
                    ImGui::TextDisabled("No chassis parts bound. Root colliders are still used if present.");
                }
                for (int chassisIndex = 0; chassisIndex < static_cast<int>(obj.vehicle.chassisObjectIds.size()); ++chassisIndex) {
                    std::string& chassisObjectId = obj.vehicle.chassisObjectIds[static_cast<std::size_t>(chassisIndex)];
                    int selectedObjectIndex = -1;
                    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                        if (objects_[i].id == chassisObjectId) {
                            selectedObjectIndex = i;
                            break;
                        }
                    }

                    std::string preview = "(none)";
                    if (selectedObjectIndex >= 0) {
                        preview = objects_[selectedObjectIndex].name.empty() ? std::string("(unnamed)") : objects_[selectedObjectIndex].name;
                    }

                    ImGui::PushID(chassisIndex);
                    const std::string comboLabel = "Chassis Part##vehicleChassisPart";
                    if (ImGui::BeginCombo(comboLabel.c_str(), preview.c_str())) {
                        const bool noneSelected = selectedObjectIndex < 0;
                        if (ImGui::Selectable("(none)", noneSelected)) {
                            PushUndoState();
                            chassisObjectId.clear();
                            if (onDirty_) onDirty_();
                        }
                        if (noneSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                            if (objects_[i].id == obj.id || !IsDescendantOf(objects_[i].id, obj.id)) {
                                continue;
                            }
                            const std::string itemName = objects_[i].name.empty() ? std::string("(unnamed)") : objects_[i].name;
                            const std::string itemLabel = itemName + "##vehicleChassisObject_" + objects_[i].id;
                            const bool isSelected = (i == selectedObjectIndex);
                            if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                                PushUndoState();
                                chassisObjectId = objects_[i].id;
                                if (onDirty_) onDirty_();
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                            if (payload->DataSize == sizeof(int)) {
                                const int droppedIndex = *static_cast<const int*>(payload->Data);
                                if (droppedIndex >= 0 &&
                                    droppedIndex < static_cast<int>(objects_.size()) &&
                                    objects_[droppedIndex].id != obj.id &&
                                    IsDescendantOf(objects_[droppedIndex].id, obj.id)) {
                                    PushUndoState();
                                    chassisObjectId = objects_[droppedIndex].id;
                                    if (onDirty_) onDirty_();
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove##vehicleChassisPart")) {
                        PushUndoState();
                        obj.vehicle.chassisObjectIds.erase(obj.vehicle.chassisObjectIds.begin() + chassisIndex);
                        if (onDirty_) onDirty_();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }

                } // Chassis header

                raceman::physics::VehicleConfig loadedConfig;
                if (TryLoadVehicleConfigForPath(obj.vehicle.configPath, loadedConfig)) {
                    if (scriptsRunning_) {
                        const auto runtimeVehicleIt = std::find_if(runtimeVehicles_.begin(), runtimeVehicles_.end(),
                            [&](const RuntimeVehicleInstance& runtimeVehicle) {
                                return runtimeVehicle.objectIndex == selectedIndex_ && runtimeVehicle.instance != nullptr;
                            });
                        if (runtimeVehicleIt != runtimeVehicles_.end()) {
                            if (ImGui::CollapsingHeader("Runtime Debug")) {
                            const raceman::physics::VehicleTelemetry& telemetry = runtimeVehicleIt->instance->getTelemetry();
                            const float speed = glm::length(glm::vec3(telemetry.linearVelocity.x, telemetry.linearVelocity.y, telemetry.linearVelocity.z));
                            ImGui::TextDisabled("Vehicle: %s  |  Wheels: %d", loadedConfig.name.c_str(), static_cast<int>(loadedConfig.wheels.size()));
                            ImGui::TextDisabled("Speed: %.2f m/s", speed);
                            ImGui::TextDisabled("Engine RPM: %.0f", telemetry.engineRPM);
                            if (telemetry.isReverse) {
                                ImGui::TextDisabled("Gear: R");
                            } else if (telemetry.isNeutral) {
                                ImGui::TextDisabled("Gear: N");
                            } else {
                                ImGui::TextDisabled("Gear: %d", telemetry.currentGear);
                            }
                            ImGui::TextDisabled("Throttle / Brake / Steering: %.2f / %.2f / %.2f",
                                telemetry.throttle,
                                telemetry.brake,
                                telemetry.steering);

                            // Per-wheel contact debug table
                            if (!telemetry.wheels.empty() &&
                                ImGui::BeginTable("##WheelDebug", 5,
                                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_SizingFixedFit)) {
                                ImGui::TableSetupColumn("Wheel");
                                ImGui::TableSetupColumn("Contact");
                                ImGui::TableSetupColumn("NormalF");
                                ImGui::TableSetupColumn("SuspTravel");
                                ImGui::TableSetupColumn("RPM");
                                ImGui::TableHeadersRow();
                                for (std::size_t wi = 0; wi < telemetry.wheels.size(); ++wi) {
                                    const raceman::physics::WheelTelemetry& wt = telemetry.wheels[wi];
                                    const bool grounded = wt.normalForce > 0.0f;
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    if (wi < loadedConfig.wheels.size()) {
                                        ImGui::TextUnformatted(loadedConfig.wheels[wi].name.c_str());
                                    } else {
                                        ImGui::Text("%d", static_cast<int>(wi));
                                    }
                                    ImGui::TableSetColumnIndex(1);
                                    if (grounded) {
                                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "ON");
                                    } else {
                                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "OFF");
                                    }
                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::Text("%.0f N", wt.normalForce);
                                    ImGui::TableSetColumnIndex(3);
                                    ImGui::Text("%.3f m", wt.suspensionTravel);
                                    ImGui::TableSetColumnIndex(4);
                                    const float rpm = wt.angularVelocity * (60.0f / (2.0f * 3.14159f));
                                    ImGui::Text("%.0f", rpm);
                                }
                                ImGui::EndTable();
                            }
                            } // Runtime Debug header
                        }
                    }
                    if (ImGui::CollapsingHeader("Wheels", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Config: %s  |  Wheels: %d", loadedConfig.name.c_str(), static_cast<int>(loadedConfig.wheels.size()));
                    for (const raceman::physics::WheelConfig& wheel : loadedConfig.wheels) {
                        auto bindingIt = std::find_if(obj.vehicle.wheelBindings.begin(), obj.vehicle.wheelBindings.end(),
                            [&](const VehicleWheelBinding& candidate) {
                                return candidate.wheelName == wheel.name;
                            });
                        int selectedObjectIndex = -1;
                        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                            if (bindingIt != obj.vehicle.wheelBindings.end() && objects_[i].id == bindingIt->objectId) {
                                selectedObjectIndex = i;
                                break;
                            }
                        }

                        std::string preview = "(none)";
                        if (selectedObjectIndex >= 0) {
                            preview = objects_[selectedObjectIndex].name;
                        }
                        if (preview.empty()) {
                            preview = "(unnamed)";
                        }

                        const std::string comboLabel = wheel.name + "##vehicleWheelBinding_" + wheel.name;
                        if (ImGui::BeginCombo(comboLabel.c_str(), preview.c_str())) {
                            const bool noneSelected = selectedObjectIndex < 0;
                            if (ImGui::Selectable("(none)", noneSelected)) {
                                PushUndoState();
                                if (bindingIt != obj.vehicle.wheelBindings.end()) {
                                    bindingIt->objectId.clear();
                                }
                                if (onDirty_) onDirty_();
                            }
                            if (noneSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                            for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                                if (objects_[i].id == obj.id) {
                                    continue;
                                }
                                const std::string itemName = objects_[i].name.empty() ? std::string("(unnamed)") : objects_[i].name;
                                const std::string itemLabel = itemName + "##vehicleBindingObject_" + objects_[i].id;
                                const bool isSelected = (i == selectedObjectIndex);
                                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                                    PushUndoState();
                                    if (bindingIt == obj.vehicle.wheelBindings.end()) {
                                        obj.vehicle.wheelBindings.push_back(VehicleWheelBinding{wheel.name, objects_[i].id});
                                    } else {
                                        bindingIt->objectId = objects_[i].id;
                                    }
                                    if (onDirty_) onDirty_();
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                                if (payload->DataSize == sizeof(int)) {
                                    const int droppedIndex = *static_cast<const int*>(payload->Data);
                                    if (droppedIndex >= 0 &&
                                        droppedIndex < static_cast<int>(objects_.size()) &&
                                        objects_[droppedIndex].id != obj.id) {
                                        PushUndoState();
                                        if (bindingIt == obj.vehicle.wheelBindings.end()) {
                                            obj.vehicle.wheelBindings.push_back(VehicleWheelBinding{wheel.name, objects_[droppedIndex].id});
                                        } else {
                                            bindingIt->objectId = objects_[droppedIndex].id;
                                        }
                                        if (onDirty_) onDirty_();
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        bindingIt = std::find_if(obj.vehicle.wheelBindings.begin(), obj.vehicle.wheelBindings.end(),
                            [&](const VehicleWheelBinding& candidate) {
                                return candidate.wheelName == wheel.name;
                            });
                        (void)bindingIt;
                    }
                    } // Wheels header
                } else if (!obj.vehicle.configPath.empty()) {
                    ImGui::TextDisabled("Vehicle config could not be loaded.");
                } else {
                    ImGui::TextDisabled("Assign a `.vehicle.json` asset in the Config section.");
                }
            }
            }

            if (currentComponentToRender == SceneInspectorComponentType::CharacterController) {
            bool removeCharacterController = false;
            bool characterControllerOpen = false;
            bool characterControllerEnabledChanged = false;
            bool characterControllerHeaderActive = false;
            bool characterControllerHeaderToggledOpen = false;
            const bool characterControllerEnabledBefore = obj.characterController.enabled;
            if (obj.hasCharacterController) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::CharacterController;
                const std::string characterControllerComponentKey = prepareComponentOpenState(SceneInspectorComponentType::CharacterController);
                characterControllerOpen = RenderRemovableComponentHeader("Character Controller", "CharacterControllerHeader", GetComponentIconTexture("component-character-controller.png"), &obj.characterController.enabled, characterControllerEnabledChanged, removeCharacterController, &componentType, &reorderDraggedType, &reorderTargetType, &characterControllerHeaderActive, &characterControllerHeaderToggledOpen);
                finishComponentHeaderState(characterControllerComponentKey, SceneInspectorComponentType::CharacterController, characterControllerHeaderActive, characterControllerHeaderToggledOpen, characterControllerOpen);
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

                glm::vec3 center = obj.characterController.center;
                if (RenderInspectorDragFloat3("Center", "##characterControllerCenter", &center.x, 0.05f)) {
                    beginInspectorContinuousEdit();
                    obj.characterController.center = center;
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
            }

            if (currentComponentToRender == SceneInspectorComponentType::Collider) {
            const SceneColliderType colliderType = GetActiveColliderType(obj);
            if (colliderType != SceneColliderType::None) {
                bool removeCollider = false;
                bool colliderOpen = false;
                bool colliderEnabledChanged = false;
                bool colliderHeaderActive = false;
                bool colliderHeaderToggledOpen = false;
                bool colliderEnabledBefore = false;
                switch (colliderType) {
                case SceneColliderType::Box: colliderEnabledBefore = obj.boxCollider.enabled; break;
                case SceneColliderType::Sphere: colliderEnabledBefore = obj.sphereCollider.enabled; break;
                case SceneColliderType::Capsule: colliderEnabledBefore = obj.capsuleCollider.enabled; break;
                case SceneColliderType::Plane: colliderEnabledBefore = obj.planeCollider.enabled; break;
                case SceneColliderType::Mesh: colliderEnabledBefore = obj.meshCollider.enabled; break;
                case SceneColliderType::None: break;
                }

                bool* enabledPtr = nullptr;
                switch (colliderType) {
                case SceneColliderType::Box: enabledPtr = &obj.boxCollider.enabled; break;
                case SceneColliderType::Sphere: enabledPtr = &obj.sphereCollider.enabled; break;
                case SceneColliderType::Capsule: enabledPtr = &obj.capsuleCollider.enabled; break;
                case SceneColliderType::Plane: enabledPtr = &obj.planeCollider.enabled; break;
                case SceneColliderType::Mesh: enabledPtr = &obj.meshCollider.enabled; break;
                case SceneColliderType::None: break;
                }

                SceneInspectorComponentType componentType = SceneInspectorComponentType::Collider;
                const std::string colliderComponentKey = prepareComponentOpenState(SceneInspectorComponentType::Collider);
                colliderOpen = RenderRemovableComponentHeader(
                    "Collider",
                    "ColliderHeader",
                    GetComponentIconTexture(SceneColliderTypeIcon(colliderType)),
                    enabledPtr,
                    colliderEnabledChanged,
                    removeCollider,
                    &componentType,
                    &reorderDraggedType,
                    &reorderTargetType,
                    &colliderHeaderActive,
                    &colliderHeaderToggledOpen);
                finishComponentHeaderState(colliderComponentKey, SceneInspectorComponentType::Collider, colliderHeaderActive, colliderHeaderToggledOpen, colliderOpen);

                if (removeCollider) {
                    PushUndoState();
                    ClearColliderComponent(obj);
                    if (onDirty_) onDirty_();
                } else if (colliderEnabledChanged && enabledPtr != nullptr) {
                    const bool colliderEnabledAfter = *enabledPtr;
                    *enabledPtr = colliderEnabledBefore;
                    PushUndoState();
                    *enabledPtr = colliderEnabledAfter;
                    if (onDirty_) onDirty_();
                }

                if (GetActiveColliderType(obj) != SceneColliderType::None && colliderOpen) {
                    SceneColliderType newColliderType = colliderType;
                    if (RenderColliderTypeCombo("Type", "singleColliderType", colliderType, false, nullptr, newColliderType) &&
                        newColliderType != colliderType) {
                        PushUndoState();
                        SetActiveColliderType(obj, newColliderType);
                        if (onDirty_) onDirty_();
                    }

                    const SceneColliderType activeColliderType = GetActiveColliderType(obj);
                    if (activeColliderType == SceneColliderType::Box) {
                        const bool isTriggerBefore = obj.boxCollider.isTrigger;
                        if (ImGui::Checkbox("Is Trigger##ColliderBox", &obj.boxCollider.isTrigger)) {
                            const bool isTriggerAfter = obj.boxCollider.isTrigger;
                            obj.boxCollider.isTrigger = isTriggerBefore;
                            PushUndoState();
                            obj.boxCollider.isTrigger = isTriggerAfter;
                            if (onDirty_) onDirty_();
                        }

                        glm::vec3 center = obj.boxCollider.center;
                        if (RenderInspectorDragFloat3("Center", "##colliderBoxCenter", &center.x, 0.05f)) {
                            beginInspectorContinuousEdit();
                            obj.boxCollider.center = center;
                            if (onDirty_) onDirty_();
                        }
                        endInspectorContinuousEdit();

                        glm::vec3 size = obj.boxCollider.size;
                        if (RenderInspectorDragFloat3("Size", "##colliderBoxSize", &size.x, 0.05f, 0.001f, 100000.0f)) {
                            beginInspectorContinuousEdit();
                            obj.boxCollider.size = {
                                (std::max)(0.001f, size.x),
                                (std::max)(0.001f, size.y),
                                (std::max)(0.001f, size.z)
                            };
                            if (onDirty_) onDirty_();
                        }
                        endInspectorContinuousEdit();
                    } else if (activeColliderType == SceneColliderType::Sphere) {
                        const bool isTriggerBefore = obj.sphereCollider.isTrigger;
                        if (ImGui::Checkbox("Is Trigger##ColliderSphere", &obj.sphereCollider.isTrigger)) {
                            const bool isTriggerAfter = obj.sphereCollider.isTrigger;
                            obj.sphereCollider.isTrigger = isTriggerBefore;
                            PushUndoState();
                            obj.sphereCollider.isTrigger = isTriggerAfter;
                            if (onDirty_) onDirty_();
                        }

                        glm::vec3 center = obj.sphereCollider.center;
                        if (RenderInspectorDragFloat3("Center", "##colliderSphereCenter", &center.x, 0.05f)) {
                            beginInspectorContinuousEdit();
                            obj.sphereCollider.center = center;
                            if (onDirty_) onDirty_();
                        }
                        endInspectorContinuousEdit();

                        float radius = obj.sphereCollider.radius;
                        if (ImGui::DragFloat("Radius##ColliderSphere", &radius, 0.05f, 0.001f, 100000.0f)) {
                            beginInspectorContinuousEdit();
                            obj.sphereCollider.radius = (std::max)(0.001f, radius);
                            if (onDirty_) onDirty_();
                        }
                        endInspectorContinuousEdit();
                    } else if (activeColliderType == SceneColliderType::Capsule) {
                        const bool isTriggerBefore = obj.capsuleCollider.isTrigger;
                        if (ImGui::Checkbox("Is Trigger##ColliderCapsule", &obj.capsuleCollider.isTrigger)) {
                            const bool isTriggerAfter = obj.capsuleCollider.isTrigger;
                            obj.capsuleCollider.isTrigger = isTriggerBefore;
                            PushUndoState();
                            obj.capsuleCollider.isTrigger = isTriggerAfter;
                            if (onDirty_) onDirty_();
                        }

                        glm::vec3 center = obj.capsuleCollider.center;
                        if (RenderInspectorDragFloat3("Center", "##colliderCapsuleCenter", &center.x, 0.05f)) {
                            beginInspectorContinuousEdit();
                            obj.capsuleCollider.center = center;
                            if (onDirty_) onDirty_();
                        }
                        endInspectorContinuousEdit();

                        float radius = obj.capsuleCollider.radius;
                        if (ImGui::DragFloat("Radius##ColliderCapsule", &radius, 0.05f, 0.001f, 100000.0f)) {
                            beginInspectorContinuousEdit();
                            obj.capsuleCollider.radius = (std::max)(0.001f, radius);
                            obj.capsuleCollider.height = (std::max)(obj.capsuleCollider.height, obj.capsuleCollider.radius * 2.0f);
                            if (onDirty_) onDirty_();
                        }
                        endInspectorContinuousEdit();

                        float height = obj.capsuleCollider.height;
                        if (ImGui::DragFloat("Height##ColliderCapsule", &height, 0.05f, 0.001f, 100000.0f)) {
                            beginInspectorContinuousEdit();
                            obj.capsuleCollider.height = (std::max)(obj.capsuleCollider.radius * 2.0f, height);
                            if (onDirty_) onDirty_();
                        }
                        endInspectorContinuousEdit();
                    } else if (activeColliderType == SceneColliderType::Plane) {
                        const bool isTriggerBefore = obj.planeCollider.isTrigger;
                        if (ImGui::Checkbox("Is Trigger##ColliderPlane", &obj.planeCollider.isTrigger)) {
                            const bool isTriggerAfter = obj.planeCollider.isTrigger;
                            obj.planeCollider.isTrigger = isTriggerBefore;
                            PushUndoState();
                            obj.planeCollider.isTrigger = isTriggerAfter;
                            if (onDirty_) onDirty_();
                        }

                        const bool infiniteBefore = obj.planeCollider.infinite;
                        if (ImGui::Checkbox("Infinite Plane##ColliderPlane", &obj.planeCollider.infinite)) {
                            const bool infiniteAfter = obj.planeCollider.infinite;
                            obj.planeCollider.infinite = infiniteBefore;
                            PushUndoState();
                            obj.planeCollider.infinite = infiniteAfter;
                            if (onDirty_) onDirty_();
                        }

                        glm::vec3 normal = obj.planeCollider.normal;
                        if (RenderInspectorDragFloat3("Normal", "##colliderPlaneNormal", &normal.x, 0.05f)) {
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
                        if (RenderInspectorDragFloat("Offset", "##colliderPlaneOffset", &offset, 0.05f, -100000.0f, 100000.0f)) {
                            beginInspectorContinuousEdit();
                            obj.planeCollider.offset = offset;
                            if (onDirty_) onDirty_();
                        }
                        endInspectorContinuousEdit();

                        if (!obj.planeCollider.infinite) {
                            float halfExtent = obj.planeCollider.halfExtent;
                            if (RenderInspectorDragFloat("Half Extent", "##colliderPlaneHalfExtent", &halfExtent, 0.5f, 0.001f, 100000.0f)) {
                                beginInspectorContinuousEdit();
                                obj.planeCollider.halfExtent = (std::max)(0.001f, halfExtent);
                                if (onDirty_) onDirty_();
                            }
                            endInspectorContinuousEdit();
                        }

                        ImGui::TextDisabled("Jolt plane shapes stay static.");
                    } else if (activeColliderType == SceneColliderType::Mesh) {
                        const bool isTriggerBefore = obj.meshCollider.isTrigger;
                        if (ImGui::Checkbox("Is Trigger##ColliderMesh", &obj.meshCollider.isTrigger)) {
                            const bool isTriggerAfter = obj.meshCollider.isTrigger;
                            obj.meshCollider.isTrigger = isTriggerBefore;
                            PushUndoState();
                            obj.meshCollider.isTrigger = isTriggerAfter;
                            if (onDirty_) onDirty_();
                        }

                        MeshColliderMode meshMode = obj.meshCollider.mode;
                        if (RenderMeshColliderModeCombo("Collider Mode", "meshColliderMode", meshMode, nullptr, meshMode) &&
                            meshMode != obj.meshCollider.mode) {
                            PushUndoState();
                            obj.meshCollider.mode = meshMode;
                            if (onDirty_) onDirty_();
                        }

                        ImGui::TextDisabled("Build Quality: Quality (fixed)");

                        const bool hasMeshSource = obj.hasMeshFilter && !obj.meshFilter.sourcePath.empty();
                        ImGui::TextDisabled("%s", hasMeshSource ? obj.meshFilter.sourcePath.c_str() : "Mesh Collider requires a Mesh Filter source.");
                        if (obj.meshCollider.mode == MeshColliderMode::TriangleMesh) {
                            ImGui::TextDisabled("Triangle mesh colliders are static-only in Jolt.");
                        } else {
                            ImGui::TextDisabled("Convex hull colliders support dynamic bodies.");
                        }
                    }
                }
            }
            }

            if (currentComponentToRender == SceneInspectorComponentType::Camera) {
            bool removeCamera = false;
            bool cameraOpen = false;
            bool cameraEnabledChanged = false;
            bool cameraHeaderActive = false;
            bool cameraHeaderToggledOpen = false;
            const bool cameraEnabledBefore = obj.camera.enabled;
            if (obj.hasCamera) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::Camera;
                const std::string cameraComponentKey = prepareComponentOpenState(SceneInspectorComponentType::Camera);
                cameraOpen = RenderRemovableComponentHeader("Camera", "CameraHeader", GetComponentIconTexture("component-camera.png"), &obj.camera.enabled, cameraEnabledChanged, removeCamera, &componentType, &reorderDraggedType, &reorderTargetType, &cameraHeaderActive, &cameraHeaderToggledOpen);
                finishComponentHeaderState(cameraComponentKey, SceneInspectorComponentType::Camera, cameraHeaderActive, cameraHeaderToggledOpen, cameraOpen);
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
            }

            if (currentComponentToRender == SceneInspectorComponentType::Cinemachine) {
            bool removeCinemachine = false;
            bool cinemachineOpen = false;
            bool cinemachineEnabledChanged = false;
            bool cinemachineHeaderActive = false;
            bool cinemachineHeaderToggledOpen = false;
            const bool cinemachineEnabledBefore = obj.cinemachine.enabled;
            if (obj.hasCinemachine) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::Cinemachine;
                const std::string cinemachineComponentKey = prepareComponentOpenState(SceneInspectorComponentType::Cinemachine);
                cinemachineOpen = RenderRemovableComponentHeader("Cinemachine Camera", "CinemachineHeader", GetComponentIconTexture("component-cinemachine.png"), &obj.cinemachine.enabled, cinemachineEnabledChanged, removeCinemachine, &componentType, &reorderDraggedType, &reorderTargetType, &cinemachineHeaderActive, &cinemachineHeaderToggledOpen);
                finishComponentHeaderState(cinemachineComponentKey, SceneInspectorComponentType::Cinemachine, cinemachineHeaderActive, cinemachineHeaderToggledOpen, cinemachineOpen);
            }
            if (removeCinemachine) {
                PushUndoState();
                obj.hasCinemachine = false;
                obj.cinemachine = CinemachineCameraComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasCinemachine && cinemachineEnabledChanged) {
                const bool enabledAfter = obj.cinemachine.enabled;
                obj.cinemachine.enabled = cinemachineEnabledBefore;
                PushUndoState();
                obj.cinemachine.enabled = enabledAfter;
                if (onDirty_) onDirty_();
            }
            if (obj.hasCinemachine && cinemachineOpen) {
                // Camera type combo
                const char* cineTypeNames[] = {"Follow", "Look At", "Follow & Look At"};
                int cineTypeIndex = static_cast<int>(obj.cinemachine.type);
                if (ImGui::Combo("Type##cinemachineType", &cineTypeIndex, cineTypeNames, 3)) {
                    PushUndoState();
                    obj.cinemachine.type = static_cast<CinemachineCameraType>(cineTypeIndex);
                    if (onDirty_) onDirty_();
                }

                // Follow target
                if (obj.cinemachine.type != CinemachineCameraType::LookAt) {
                    int followTargetIndex = -1;
                    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                        if (objects_[i].id == obj.cinemachine.followTargetId) {
                            followTargetIndex = i;
                            break;
                        }
                    }
                    std::string followPreview = followTargetIndex >= 0
                        ? (objects_[followTargetIndex].name.empty() ? "(unnamed)" : objects_[followTargetIndex].name)
                        : "(none)";
                    if (ImGui::BeginCombo("Follow Target##cinemachineFollow", followPreview.c_str())) {
                        if (ImGui::Selectable("(none)", followTargetIndex < 0)) {
                            PushUndoState();
                            obj.cinemachine.followTargetId.clear();
                            if (onDirty_) onDirty_();
                        }
                        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                            if (objects_[i].id == obj.id) continue;
                            const std::string itemName = objects_[i].name.empty() ? "(unnamed)" : objects_[i].name;
                            if (ImGui::Selectable((itemName + "##cinemachineFollowObj_" + objects_[i].id).c_str(), i == followTargetIndex)) {
                                PushUndoState();
                                obj.cinemachine.followTargetId = objects_[i].id;
                                if (onDirty_) onDirty_();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                            if (payload->DataSize == sizeof(int)) {
                                const int droppedIndex = *static_cast<const int*>(payload->Data);
                                if (droppedIndex >= 0 && droppedIndex < static_cast<int>(objects_.size()) && objects_[droppedIndex].id != obj.id) {
                                    PushUndoState();
                                    obj.cinemachine.followTargetId = objects_[droppedIndex].id;
                                    if (onDirty_) onDirty_();
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    glm::vec3 offset = obj.cinemachine.followOffset;
                    if (RenderInspectorDragFloat3("Follow Offset", "##cinemachineOffset", &offset.x, 0.05f)) {
                        beginInspectorContinuousEdit();
                        obj.cinemachine.followOffset = offset;
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                }

                // Look-at target (optional override)
                if (obj.cinemachine.type != CinemachineCameraType::Follow) {
                    int lookAtTargetIndex = -1;
                    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                        if (objects_[i].id == obj.cinemachine.lookAtTargetId) {
                            lookAtTargetIndex = i;
                            break;
                        }
                    }
                    const bool usingFollowAsLookAt = obj.cinemachine.lookAtTargetId.empty();
                    std::string lookAtPreview = usingFollowAsLookAt
                        ? "(same as follow)"
                        : (lookAtTargetIndex >= 0 ? (objects_[lookAtTargetIndex].name.empty() ? "(unnamed)" : objects_[lookAtTargetIndex].name) : "(none)");
                    if (ImGui::BeginCombo("Look At Target##cinemachineLookAt", lookAtPreview.c_str())) {
                        if (ImGui::Selectable("(same as follow)", usingFollowAsLookAt)) {
                            PushUndoState();
                            obj.cinemachine.lookAtTargetId.clear();
                            if (onDirty_) onDirty_();
                        }
                        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                            if (objects_[i].id == obj.id) continue;
                            const std::string itemName = objects_[i].name.empty() ? "(unnamed)" : objects_[i].name;
                            if (ImGui::Selectable((itemName + "##cinemachineLookAtObj_" + objects_[i].id).c_str(), i == lookAtTargetIndex)) {
                                PushUndoState();
                                obj.cinemachine.lookAtTargetId = objects_[i].id;
                                if (onDirty_) onDirty_();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyObjectPayload)) {
                            if (payload->DataSize == sizeof(int)) {
                                const int droppedIndex = *static_cast<const int*>(payload->Data);
                                if (droppedIndex >= 0 && droppedIndex < static_cast<int>(objects_.size()) && objects_[droppedIndex].id != obj.id) {
                                    PushUndoState();
                                    obj.cinemachine.lookAtTargetId = objects_[droppedIndex].id;
                                    if (onDirty_) onDirty_();
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }

                // Rotation offsets
                if (obj.cinemachine.type != CinemachineCameraType::Follow) {
                    float pitchOffset = obj.cinemachine.pitchOffset;
                    if (ImGui::DragFloat("Pitch Offset", &pitchOffset, 0.5f, -89.0f, 89.0f, "%.1f deg")) {
                        beginInspectorContinuousEdit();
                        obj.cinemachine.pitchOffset = (std::max)(-89.0f, (std::min)(89.0f, pitchOffset));
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();

                    float yawOffset = obj.cinemachine.yawOffset;
                    if (ImGui::DragFloat("Yaw Offset", &yawOffset, 0.5f, -180.0f, 180.0f, "%.1f deg")) {
                        beginInspectorContinuousEdit();
                        obj.cinemachine.yawOffset = yawOffset;
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                }

                // Damping
                float posDamping = obj.cinemachine.positionDamping;
                if (ImGui::DragFloat("Position Damping", &posDamping, 0.1f, 0.0f, 50.0f)) {
                    beginInspectorContinuousEdit();
                    obj.cinemachine.positionDamping = (std::max)(0.0f, posDamping);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float rotDamping = obj.cinemachine.rotationDamping;
                if (ImGui::DragFloat("Rotation Damping", &rotDamping, 0.1f, 0.0f, 50.0f)) {
                    beginInspectorContinuousEdit();
                    obj.cinemachine.rotationDamping = (std::max)(0.0f, rotDamping);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                if (!scriptsRunning_) {
                    ImGui::TextDisabled("Activates in Play mode.");
                }
            }
            }

            if (currentComponentToRender == SceneInspectorComponentType::Light) {
            bool removeLight = false;
            bool lightOpen = false;
            bool lightEnabledChanged = false;
            bool lightHeaderActive = false;
            bool lightHeaderToggledOpen = false;
            const bool lightEnabledBefore = obj.light.enabled;
            if (obj.hasLight) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::Light;
                const std::string lightComponentKey = prepareComponentOpenState(SceneInspectorComponentType::Light);
                lightOpen = RenderRemovableComponentHeader("Light", "LightHeader", GetComponentIconTexture("component-light.png"), &obj.light.enabled, lightEnabledChanged, removeLight, &componentType, &reorderDraggedType, &reorderTargetType, &lightHeaderActive, &lightHeaderToggledOpen);
                finishComponentHeaderState(lightComponentKey, SceneInspectorComponentType::Light, lightHeaderActive, lightHeaderToggledOpen, lightOpen);
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
            }
            }
            if (reorderDraggedType != reorderTargetType) {
                SceneObject orderProbe = obj;
                if (MoveInspectorComponentBefore(orderProbe, reorderDraggedType, reorderTargetType)) {
                    PushUndoState();
                    MoveInspectorComponentBefore(obj, reorderDraggedType, reorderTargetType);
                    if (onDirty_) onDirty_();
                }
            }
            ImGui::Separator();
            if (!inspectorKeyboardTargetComponentKey_.empty()) {
                if (ImGui::Button("Copy Focused Component")) {
                    CopyInspectorComponentToClipboard(selectedIndex_, inspectorKeyboardTargetComponentType_);
                }
                ImGui::SameLine();
            }
            ImGui::BeginDisabled(!componentClipboard_.hasValue);
            if (ImGui::Button("Paste Clipboard Component")) {
                PasteInspectorComponentFromClipboard({selectedIndex_}, componentClipboard_.type);
            }
            ImGui::EndDisabled();
            if (!inspectorKeyboardTargetComponentKey_.empty() || componentClipboard_.hasValue) {
                ImGui::SameLine();
            }
            if (ImGui::Button("Delete")) {
                DeleteSelectedObject();
            }
            ImGui::Spacing();
            if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f))) {
                ImGui::OpenPopup("Add Object Component");
            }
            if (ImGui::BeginPopup("Add Object Component")) {
                if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    ImGui::CloseCurrentPopup();
                }
                renderAddComponentMenu();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupContextWindow("InspectorAddComponentContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                ImGui::TextDisabled("Add Component");
                ImGui::Separator();
                renderAddComponentMenu();
                ImGui::EndPopup();
            }
        } else {
            ImGui::TextDisabled("No object selected.");
        }

        RenderProjectAssetPickerPopup();
    }
    ImGui::End();
    if (!inspectorPanelHovered_ && !inspectorPanelFocused_) {
        inspectorKeyboardTargetComponentKey_.clear();
        inspectorKeyboardTargetObjectId_.clear();
    }
}

void SceneEditor::RenderMultiSelectionInspector() {
    NormalizeSelection();
    if (selectedIndices_.size() <= 1 || selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }

    SceneObject& active = objects_[selectedIndex_];
    ImGui::Text("%zu objects selected", selectedIndices_.size());
    ImGui::TextDisabled("Showing components shared by every selected object. Edits apply to all selected objects.");
    int sharedPhysicsLayer = ClampPhysicsLayerIndex(active.physicsLayer);
    if (RenderPhysicsLayerCombo("Physics Layer##multi", "multiPhysicsLayer", active.physicsLayer, physicsLayerNames_, sharedPhysicsLayer)) {
        PushUndoState();
        for (int index : selectedIndices_) {
            if (index >= 0 && index < static_cast<int>(objects_.size())) {
                objects_[index].physicsLayer = sharedPhysicsLayer;
            }
        }
        if (onDirty_) {
            onDirty_();
        }
    }

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
    ImGui::SameLine();

    auto anySelected = [&](auto&& predicate) {
        for (int index : selectedIndices_) {
            if (index >= 0 && index < static_cast<int>(objects_.size()) && predicate(objects_[index])) {
                return true;
            }
        }
        return false;
    };
    auto renderMultiAddComponentMenu = [&]() {
        bool anyAvailable = false;
        if (anySelected([](const SceneObject& object) { return !object.hasMeshFilter; })) {
            anyAvailable = true;
            if (ImGui::MenuItem("Mesh Filter")) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) {
                    if (!object.hasMeshFilter) {
                        object.hasMeshFilter = true;
                        object.meshFilter = MeshFilterComponent{};
                        object.meshFilter.meshType = "Mesh";
                    }
                });
                markDirty();
            }
        }
        if (anySelected([](const SceneObject& object) { return !object.hasMeshRenderer; })) {
            anyAvailable = true;
            if (ImGui::MenuItem("Mesh Renderer")) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) {
                    if (!object.hasMeshRenderer) {
                        object.hasMeshRenderer = true;
                        object.meshRenderer = MeshRendererComponent{};
                    }
                });
                markDirty();
            }
        }
        if (anySelected([](const SceneObject& object) { return !object.hasScriptComponent; })) {
            anyAvailable = true;
            if (ImGui::MenuItem("Scripts")) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) {
                    if (!object.hasScriptComponent) {
                        object.hasScriptComponent = true;
                        object.scriptComponent = ScriptComponent{};
                    }
                });
                markDirty();
            }
        }
        if (anySelected([](const SceneObject& object) { return !object.hasRigidbody; })) {
            anyAvailable = true;
            if (ImGui::MenuItem("Rigidbody")) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) {
                    if (object.hasCharacterController) {
                        object.hasCharacterController = false;
                        object.characterController = CharacterControllerComponent{};
                    }
                    if (!object.hasRigidbody) {
                        object.hasRigidbody = true;
                        object.rigidbody = RigidbodyComponent{};
                    }
                });
                markDirty();
            }
        }
        if (anySelected([](const SceneObject& object) { return !object.hasVehicle; })) {
            anyAvailable = true;
            if (ImGui::MenuItem("Vehicle")) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) {
                    if (!object.hasVehicle) {
                        object.hasVehicle = true;
                        object.vehicle = VehicleComponent{};
                    }
                });
                markDirty();
            }
        }
        if (anySelected([](const SceneObject& object) { return !object.hasCharacterController; })) {
            anyAvailable = true;
            if (ImGui::MenuItem("Character Controller")) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) {
                    if (object.hasRigidbody) {
                        object.hasRigidbody = false;
                        object.rigidbody = RigidbodyComponent{};
                    }
                    if (!object.hasCharacterController) {
                        object.hasCharacterController = true;
                        object.characterController = CharacterControllerComponent{};
                    }
                });
                markDirty();
            }
        }
        if (anySelected([](const SceneObject& object) { return !HasColliderComponent(object); })) {
            anyAvailable = true;
            if (ImGui::MenuItem("Collider")) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) {
                    if (!HasColliderComponent(object)) {
                        SetActiveColliderType(object, SceneColliderType::Box);
                    }
                });
                markDirty();
            }
        }
        if (anySelected([](const SceneObject& object) { return !object.hasCamera; })) {
            anyAvailable = true;
            if (ImGui::MenuItem("Camera")) {
                PushUndoState();
                bool hasAnyCamera = false;
                for (const SceneObject& sceneObject : objects_) {
                    if (sceneObject.hasCamera) {
                        hasAnyCamera = true;
                        break;
                    }
                }
                bool assignedMainCamera = false;
                forEachSelected([&](SceneObject& object) {
                    if (!object.hasCamera) {
                        object.hasCamera = true;
                        object.camera = CameraComponent{};
                        object.camera.isMain = !hasAnyCamera && !assignedMainCamera;
                        assignedMainCamera = assignedMainCamera || object.camera.isMain;
                    }
                });
                markDirty();
            }
        }
        if (anySelected([](const SceneObject& object) { return !object.hasCinemachine; })) {
            anyAvailable = true;
            if (ImGui::MenuItem("Cinemachine Camera")) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) {
                    if (!object.hasCinemachine) {
                        object.hasCinemachine = true;
                        object.cinemachine = CinemachineCameraComponent{};
                    }
                });
                markDirty();
            }
        }
        if (anySelected([](const SceneObject& object) { return !object.hasLight; })) {
            anyAvailable = true;
            if (ImGui::BeginMenu("Light")) {
                auto addLightToSelection = [&](LightType type, float intensity, float range) {
                    PushUndoState();
                    forEachSelected([&](SceneObject& object) {
                        if (!object.hasLight) {
                            object.hasLight = true;
                            object.light = LightComponent{};
                            object.light.type = type;
                            object.light.intensity = intensity;
                            object.light.range = range;
                        }
                    });
                    markDirty();
                };
                if (ImGui::MenuItem("Directional")) {
                    addLightToSelection(LightType::Directional, 1.5f, 100.0f);
                }
                if (ImGui::MenuItem("Point")) {
                    addLightToSelection(LightType::Point, 3.0f, 10.0f);
                }
                if (ImGui::MenuItem("Spot")) {
                    addLightToSelection(LightType::Spot, 3.0f, 10.0f);
                }
                ImGui::EndMenu();
            }
        }
        if (!anyAvailable) {
            ImGui::TextDisabled("All selected objects already have every optional component.");
        }
    };

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
        bool scaleDeactivated = false;
        if (RenderInspectorLinkedScaleEditor("Scale", "##multiScale", &scale.x, 0.1f, linkedMultiScaleValues_, &scaleDeactivated)) {
            beginInspectorContinuousEdit();
            scale = {
                (std::max)(scale.x, 0.01f),
                (std::max)(scale.y, 0.01f),
                (std::max)(scale.z, 0.01f)
            };
            forEachSelected([&](SceneObject& object) { object.transform.scale = scale; });
            markDirty();
        }
        if (scaleDeactivated) {
            inspectorEditActive_ = false;
        }
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
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kMaterialAssetPayload)) {
                    const char* materialIdPayload = static_cast<const char*>(payload->Data);
                    if (materialIdPayload != nullptr) {
                        AssignMaterialToSelected(materialIdPayload);
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                    const char* projectPath = static_cast<const char*>(payload->Data);
                    if (projectPath != nullptr && IsMaterialAssetPath(projectPath)) {
                        AssignMaterialToSelected(MaterialIdFromAssetPath(projectPath));
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
            if (ImGui::CollapsingHeader("Body", ImGuiTreeNodeFlags_DefaultOpen)) {
                int bodyTypeIndex = active.rigidbody.bodyType == RigidbodyBodyType::Static
                    ? 0
                    : (active.rigidbody.bodyType == RigidbodyBodyType::Kinematic ? 1 : 2);
                const char* bodyTypes[] = {"Static", "Kinematic", "Dynamic"};
                if (ImGui::Combo("Body Type##multiRigidbody", &bodyTypeIndex, bodyTypes, 3)) {
                    PushUndoState();
                    const RigidbodyBodyType bodyType = bodyTypeIndex == 0
                        ? RigidbodyBodyType::Static
                        : (bodyTypeIndex == 1 ? RigidbodyBodyType::Kinematic : RigidbodyBodyType::Dynamic);
                    forEachSelected([&](SceneObject& object) {
                        object.rigidbody.bodyType = bodyType;
                        if (bodyType == RigidbodyBodyType::Static) {
                            object.rigidbody.velocity = glm::vec3{0.0f};
                            object.rigidbody.angularVelocity = glm::vec3{0.0f};
                        }
                    });
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

                float linearDamping = active.rigidbody.linearDamping;
                if (ImGui::DragFloat("Linear Damping##multiRigidbody", &linearDamping, 0.01f, 0.0f, 1000.0f)) {
                    beginInspectorContinuousEdit();
                    linearDamping = (std::max)(0.0f, linearDamping);
                    forEachSelected([&](SceneObject& object) { object.rigidbody.linearDamping = linearDamping; });
                    markDirty();
                }
                endInspectorContinuousEdit();

                float angularDamping = active.rigidbody.angularDamping;
                if (ImGui::DragFloat("Angular Damping##multiRigidbody", &angularDamping, 0.01f, 0.0f, 1000.0f)) {
                    beginInspectorContinuousEdit();
                    angularDamping = (std::max)(0.0f, angularDamping);
                    forEachSelected([&](SceneObject& object) { object.rigidbody.angularDamping = angularDamping; });
                    markDirty();
                }
                endInspectorContinuousEdit();
            }

            if (ImGui::CollapsingHeader("Material")) {
                float friction = active.rigidbody.friction;
                if (ImGui::DragFloat("Friction##multiRigidbody", &friction, 0.01f, 0.0f, 10.0f)) {
                    beginInspectorContinuousEdit();
                    friction = (std::max)(0.0f, friction);
                    forEachSelected([&](SceneObject& object) { object.rigidbody.friction = friction; });
                    markDirty();
                }
                endInspectorContinuousEdit();

                float restitution = active.rigidbody.restitution;
                if (ImGui::DragFloat("Restitution##multiRigidbody", &restitution, 0.01f, 0.0f, 1.0f)) {
                    beginInspectorContinuousEdit();
                    restitution = (std::max)(0.0f, restitution);
                    forEachSelected([&](SceneObject& object) { object.rigidbody.restitution = restitution; });
                    markDirty();
                }
                endInspectorContinuousEdit();
            }

            if (ImGui::CollapsingHeader("Velocity")) {
                glm::vec3 velocity = active.rigidbody.velocity;
                if (RenderInspectorDragFloat3("Velocity", "##multiRigidbodyVelocity", &velocity.x, 0.1f)) {
                    beginInspectorContinuousEdit();
                    forEachSelected([&](SceneObject& object) { object.rigidbody.velocity = velocity; });
                    markDirty();
                }
                endInspectorContinuousEdit();

                glm::vec3 angularVelocity = active.rigidbody.angularVelocity;
                if (RenderInspectorDragFloat3("Angular Velocity", "##multiRigidbodyAngularVelocity", &angularVelocity.x, 0.1f)) {
                    beginInspectorContinuousEdit();
                    forEachSelected([&](SceneObject& object) { object.rigidbody.angularVelocity = angularVelocity; });
                    markDirty();
                }
                endInspectorContinuousEdit();
            }

            if (ImGui::CollapsingHeader("Constraints")) {
                bool freezePositionX = active.rigidbody.freezePositionX;
                bool freezePositionY = active.rigidbody.freezePositionY;
                bool freezePositionZ = active.rigidbody.freezePositionZ;
                if (RenderInspectorAxisToggles("Freeze Position", "multiRigidbodyFreezePosition", freezePositionX, freezePositionY, freezePositionZ)) {
                    PushUndoState();
                    forEachSelected([&](SceneObject& object) {
                        object.rigidbody.freezePositionX = freezePositionX;
                        object.rigidbody.freezePositionY = freezePositionY;
                        object.rigidbody.freezePositionZ = freezePositionZ;
                    });
                    markDirty();
                }
                bool freezeRotationX = active.rigidbody.freezeRotationX;
                bool freezeRotationY = active.rigidbody.freezeRotationY;
                bool freezeRotationZ = active.rigidbody.freezeRotationZ;
                if (RenderInspectorAxisToggles("Freeze Rotation", "multiRigidbodyFreezeRotation", freezeRotationX, freezeRotationY, freezeRotationZ)) {
                    PushUndoState();
                    forEachSelected([&](SceneObject& object) {
                        object.rigidbody.freezeRotationX = freezeRotationX;
                        object.rigidbody.freezeRotationY = freezeRotationY;
                        object.rigidbody.freezeRotationZ = freezeRotationZ;
                    });
                    markDirty();
                }
            }
        }
    }

    if (allSelected([](const SceneObject& object) { return object.hasVehicle; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Vehicle", "MultiVehicleHeader", "component-vehicle.png", active.vehicle.enabled, [](SceneObject& object, bool value) { object.vehicle.enabled = value; })) {
            std::string configDisplayName = "(mixed)";
            if (!active.vehicle.configPath.empty()) {
                configDisplayName = ProjectAssetDisplayFilename(active.vehicle.configPath);
            }
            const bool sameVehicleConfig = allSelected([&](const SceneObject& object) {
                return object.vehicle.configPath == active.vehicle.configPath;
            });
            if (!sameVehicleConfig) {
                configDisplayName = "(mixed)";
            }
            ImGui::TextDisabled("Config Asset:");
            ImGui::SameLine();
            const float multiVehicleConfigButtonWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
            if (ImGui::Button((configDisplayName + "##selectMultiVehicleConfig").c_str(), ImVec2(multiVehicleConfigButtonWidth, 0.0f))) {
                assetPickerMode_ = ProjectAssetPickerMode::AssignVehicleConfig;
            }
            if (sameVehicleConfig && !active.vehicle.configPath.empty()) {
                if (ImGui::Button("Edit Profile##multiVehicleConfigEdit")) {
                    OpenVehicleConfigEditor(active.vehicle.configPath);
                }
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                    const char* projectPath = static_cast<const char*>(payload->Data);
                    if (projectPath != nullptr && IsVehicleConfigAssetPath(projectPath)) {
                        AssignVehicleConfigToSelected(projectPath);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            raceman::physics::VehicleConfig loadedConfig;
            if (TryLoadVehicleConfigForPath(active.vehicle.configPath, loadedConfig)) {
                ImGui::TextDisabled("Vehicle: %s", loadedConfig.name.c_str());
                ImGui::TextDisabled("Wheels: %d", static_cast<int>(loadedConfig.wheels.size()));
            } else if (!active.vehicle.configPath.empty()) {
                ImGui::TextDisabled("Vehicle config could not be loaded.");
            } else {
                ImGui::TextDisabled("Assign a `.vehicle.json` asset to all selected objects.");
            }
            ImGui::TextDisabled("Wheel bindings are edited per object in the single-object inspector.");
        }
    }

    if (allSelected([](const SceneObject& object) { return HasColliderComponent(object); })) {
        showedSharedComponent = true;
        const SceneColliderType activeColliderType = GetActiveColliderType(active);
        const bool sameColliderType = allSelected([&](const SceneObject& object) { return GetActiveColliderType(object) == activeColliderType; });
        auto applyColliderEnabled = [&](SceneObject& object, bool value) {
            switch (GetActiveColliderType(object)) {
            case SceneColliderType::Box: object.boxCollider.enabled = value; break;
            case SceneColliderType::Sphere: object.sphereCollider.enabled = value; break;
            case SceneColliderType::Capsule: object.capsuleCollider.enabled = value; break;
            case SceneColliderType::Plane: object.planeCollider.enabled = value; break;
            case SceneColliderType::Mesh: object.meshCollider.enabled = value; break;
            case SceneColliderType::None: break;
            }
        };
        const bool enabled = activeColliderType == SceneColliderType::Box ? active.boxCollider.enabled :
                             activeColliderType == SceneColliderType::Sphere ? active.sphereCollider.enabled :
                             activeColliderType == SceneColliderType::Capsule ? active.capsuleCollider.enabled :
                             activeColliderType == SceneColliderType::Plane ? active.planeCollider.enabled :
                             active.meshCollider.enabled;
        if (renderSharedEnabledHeader("Collider", "MultiColliderHeader", SceneColliderTypeIcon(activeColliderType), enabled, applyColliderEnabled)) {
            SceneColliderType newColliderType = activeColliderType;
            if (RenderColliderTypeCombo("Type##multiCollider", "multiColliderType", activeColliderType, false, sameColliderType ? nullptr : "Mixed", newColliderType) &&
                newColliderType != activeColliderType) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) { SetActiveColliderType(object, newColliderType); });
                markDirty();
            }

            const SceneColliderType resolvedType = sameColliderType ? activeColliderType : newColliderType;
            if (resolvedType == SceneColliderType::Box && sameColliderType) {
                bool isTrigger = active.boxCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger##multiColliderBox", &isTrigger)) {
                    PushUndoState();
                    forEachSelected([&](SceneObject& object) { object.boxCollider.isTrigger = isTrigger; });
                    markDirty();
                }
                glm::vec3 center = active.boxCollider.center;
                if (RenderInspectorDragFloat3("Center", "##multiColliderBoxCenter", &center.x, 0.05f)) {
                    beginInspectorContinuousEdit();
                    forEachSelected([&](SceneObject& object) { object.boxCollider.center = center; });
                    markDirty();
                }
                endInspectorContinuousEdit();
                glm::vec3 size = active.boxCollider.size;
                if (RenderInspectorDragFloat3("Size", "##multiColliderBoxSize", &size.x, 0.05f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    size = {(std::max)(0.001f, size.x), (std::max)(0.001f, size.y), (std::max)(0.001f, size.z)};
                    forEachSelected([&](SceneObject& object) { object.boxCollider.size = size; });
                    markDirty();
                }
                endInspectorContinuousEdit();
            } else if (resolvedType == SceneColliderType::Sphere && sameColliderType) {
                bool isTrigger = active.sphereCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger##multiColliderSphere", &isTrigger)) {
                    PushUndoState();
                    forEachSelected([&](SceneObject& object) { object.sphereCollider.isTrigger = isTrigger; });
                    markDirty();
                }
                glm::vec3 center = active.sphereCollider.center;
                if (RenderInspectorDragFloat3("Center", "##multiColliderSphereCenter", &center.x, 0.05f)) {
                    beginInspectorContinuousEdit();
                    forEachSelected([&](SceneObject& object) { object.sphereCollider.center = center; });
                    markDirty();
                }
                endInspectorContinuousEdit();
                float radius = active.sphereCollider.radius;
                if (ImGui::DragFloat("Radius##multiColliderSphere", &radius, 0.05f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    radius = (std::max)(0.001f, radius);
                    forEachSelected([&](SceneObject& object) { object.sphereCollider.radius = radius; });
                    markDirty();
                }
                endInspectorContinuousEdit();
            } else if (resolvedType == SceneColliderType::Capsule && sameColliderType) {
                bool isTrigger = active.capsuleCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger##multiColliderCapsule", &isTrigger)) {
                    PushUndoState();
                    forEachSelected([&](SceneObject& object) { object.capsuleCollider.isTrigger = isTrigger; });
                    markDirty();
                }
                glm::vec3 center = active.capsuleCollider.center;
                if (RenderInspectorDragFloat3("Center", "##multiColliderCapsuleCenter", &center.x, 0.05f)) {
                    beginInspectorContinuousEdit();
                    forEachSelected([&](SceneObject& object) { object.capsuleCollider.center = center; });
                    markDirty();
                }
                endInspectorContinuousEdit();
                float radius = active.capsuleCollider.radius;
                if (ImGui::DragFloat("Radius##multiColliderCapsule", &radius, 0.05f, 0.001f, 100000.0f)) {
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
                if (ImGui::DragFloat("Height##multiColliderCapsule", &height, 0.05f, 0.001f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    height = (std::max)(active.capsuleCollider.radius * 2.0f, height);
                    forEachSelected([&](SceneObject& object) { object.capsuleCollider.height = height; });
                    markDirty();
                }
                endInspectorContinuousEdit();
            } else if (resolvedType == SceneColliderType::Plane && sameColliderType) {
                bool isTrigger = active.planeCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger##multiColliderPlane", &isTrigger)) {
                    PushUndoState();
                    forEachSelected([&](SceneObject& object) { object.planeCollider.isTrigger = isTrigger; });
                    markDirty();
                }
                bool infinite = active.planeCollider.infinite;
                if (ImGui::Checkbox("Infinite Plane##multiColliderPlane", &infinite)) {
                    PushUndoState();
                    forEachSelected([&](SceneObject& object) { object.planeCollider.infinite = infinite; });
                    markDirty();
                }
                glm::vec3 normal = active.planeCollider.normal;
                if (RenderInspectorDragFloat3("Normal", "##multiColliderPlaneNormal", &normal.x, 0.05f)) {
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
                if (RenderInspectorDragFloat("Offset", "##multiColliderPlaneOffset", &offset, 0.05f, -100000.0f, 100000.0f)) {
                    beginInspectorContinuousEdit();
                    forEachSelected([&](SceneObject& object) { object.planeCollider.offset = offset; });
                    markDirty();
                }
                endInspectorContinuousEdit();
                if (!active.planeCollider.infinite) {
                    float halfExtent = active.planeCollider.halfExtent;
                    if (RenderInspectorDragFloat("Half Extent", "##multiColliderPlaneHalfExtent", &halfExtent, 0.5f, 0.001f, 100000.0f)) {
                        beginInspectorContinuousEdit();
                        halfExtent = (std::max)(0.001f, halfExtent);
                        forEachSelected([&](SceneObject& object) { object.planeCollider.halfExtent = halfExtent; });
                        markDirty();
                    }
                    endInspectorContinuousEdit();
                }
                ImGui::TextDisabled("Jolt plane shapes stay static.");
            } else if (resolvedType == SceneColliderType::Mesh && sameColliderType) {
                bool isTrigger = active.meshCollider.isTrigger;
                if (ImGui::Checkbox("Is Trigger##multiColliderMesh", &isTrigger)) {
                    PushUndoState();
                    forEachSelected([&](SceneObject& object) { object.meshCollider.isTrigger = isTrigger; });
                    markDirty();
                }
                const bool sameMeshMode = allSelected([&](const SceneObject& object) { return object.meshCollider.mode == active.meshCollider.mode; });
                MeshColliderMode meshMode = active.meshCollider.mode;
                const char* previewOverride = sameMeshMode ? nullptr : "Mixed";
                if (RenderMeshColliderModeCombo("Collider Mode##multiMeshCollider", "multiMeshColliderMode", meshMode, previewOverride, meshMode)) {
                    PushUndoState();
                    forEachSelected([&](SceneObject& object) { object.meshCollider.mode = meshMode; });
                    markDirty();
                }
                ImGui::TextDisabled("Build Quality: Quality (fixed)");
                ImGui::TextDisabled("Mesh colliders use each object's Mesh Filter source.");
                if (sameMeshMode && meshMode == MeshColliderMode::TriangleMesh) {
                    ImGui::TextDisabled("Triangle mesh colliders are static-only in Jolt.");
                } else if (sameMeshMode && meshMode == MeshColliderMode::ConvexHull) {
                    ImGui::TextDisabled("Convex hull colliders support dynamic bodies.");
                }
            } else if (!sameColliderType) {
                ImGui::TextDisabled("Type-specific fields are hidden while the selection has mixed collider types.");
            }
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
    ImGui::Spacing();
    if (ImGui::Button("Add Component##multi", ImVec2(-1.0f, 0.0f))) {
        ImGui::OpenPopup("MultiAddComponentPopup");
    }
    if (ImGui::BeginPopup("MultiAddComponentPopup")) {
        ImGui::TextDisabled("Add Component");
        ImGui::Separator();
        renderMultiAddComponentMenu();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupContextWindow("MultiInspectorAddComponentContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        ImGui::TextDisabled("Add Component");
        ImGui::Separator();
        renderMultiAddComponentMenu();
        ImGui::EndPopup();
    }
}

void SceneEditor::RenderProjectAssetPickerPopup() {
    if (assetPickerMode_ == ProjectAssetPickerMode::None) {
        return;
    }

    const bool pickingMesh = (assetPickerMode_ == ProjectAssetPickerMode::ReplaceMesh);
    const bool pickingMaterial = (assetPickerMode_ == ProjectAssetPickerMode::AssignMaterial);
    const bool pickingVehicleConfig = (assetPickerMode_ == ProjectAssetPickerMode::AssignVehicleConfig);
    ImGui::SetNextWindowSize(ImVec2(460.0f, 360.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    const char* windowTitle = pickingMesh ? "Select Project Mesh" : (pickingVehicleConfig ? "Select Vehicle Config" : "Select Project Material");
    bool pickerOpen = true;
    if (ImGui::Begin(windowTitle, &pickerOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        if (!pickerOpen || (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            assetPickerMode_ = ProjectAssetPickerMode::None;
            pickerOpen = false;
        } else {
            ImGui::TextUnformatted(
                pickingMesh ? "Select a mesh asset from the project"
                : (pickingVehicleConfig ? "Select a vehicle config asset from the project" : "Select a material from the project"));
            ImGui::Separator();

            if (pickingMesh) {
                const char* builtIns[] = {"Plane", "Cube", "Sphere", "Cone", "Cylinder"};
                for (int i = 0; i < 5; ++i) {
                    if (ImGui::Button(builtIns[i], ImVec2(90.0f, 0.0f))) {
                        ReplaceSelectedMeshWithBuiltIn(builtIns[i]);
                        assetPickerMode_ = ProjectAssetPickerMode::None;
                        pickerOpen = false;
                    }
                    if (i < 4) {
                        ImGui::SameLine();
                    }
                }
                ImGui::Separator();
            }

            if (ImGui::Button("Refresh")) {
                RefreshProjectFiles();
            }

            if (pickingMaterial) {
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
                        pickerOpen = false;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Create a material and assign it to the selected object.");
                }
            } else if (pickingVehicleConfig) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(190.0f);
                ImGui::InputText("##newVehicleConfigName", createVehicleConfigNameBuffer_, sizeof(createVehicleConfigNameBuffer_));
                ImGui::SameLine();
                if (ImGui::Button("Add Vehicle Profile")) {
                    std::string newConfigPath;
                    if (CreateVehicleConfigAsset(createVehicleConfigNameBuffer_, &newConfigPath)) {
                        createVehicleConfigNameBuffer_[0] = '\0';
                        AssignVehicleConfigToSelected(newConfigPath);
                        OpenVehicleConfigEditor(newConfigPath);
                        assetPickerMode_ = ProjectAssetPickerMode::None;
                        pickerOpen = false;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Create a vehicle profile and assign it to the selected vehicle.");
                }
            }

            bool found = false;
            if (ImGui::BeginChild("ProjectAssetPickerList", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.0f), true)) {
                for (const std::string& file : projectFiles_) {
                    const bool matches = pickingMesh
                        ? IsMeshAssetPath(file)
                        : (pickingVehicleConfig ? IsVehicleConfigAssetPath(file) : IsMaterialAssetPath(file));
                    if (!matches) {
                        continue;
                    }

                    found = true;
                    const std::string label = ProjectAssetDisplayFilename(file) + "##" + file;
                    if (ImGui::Selectable(label.c_str())) {
                        if (pickingMesh) {
                            ReplaceSelectedMeshFromObj(file);
                        } else if (pickingVehicleConfig) {
                            AssignVehicleConfigToSelected(file);
                        } else {
                            AssignMaterialToSelected(MaterialIdFromAssetPath(file));
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", file.c_str());
                    }
                }

                if (!found) {
                    ImGui::TextDisabled("%s",
                        pickingMesh
                            ? "No mesh assets found in project assets."
                            : (pickingVehicleConfig ? "No vehicle config files found in project assets." : "No material files found in project assets."));
                }
            }
            ImGui::EndChild();

            ImGui::Separator();
            if (ImGui::Button("Close")) {
                assetPickerMode_ = ProjectAssetPickerMode::None;
                pickerOpen = false;
            }
        }
    }
    ImGui::End();
}

void SceneEditor::RenderVehicleConfigEditorWindow() {
    if (!showVehicleConfigEditor_) {
        return;
    }
    if (inspectedVehicleConfigPath_.empty()) {
        showVehicleConfigEditor_ = false;
        return;
    }

    if (!inspectedVehicleConfigLoaded_) {
        inspectedVehicleConfigError_.clear();
        try {
            inspectedVehicleConfig_ = raceman::physics::VehicleConfigLoader::loadFromFile(
                ProjectAssetPathToAbsolute(inspectedVehicleConfigPath_).string());
            inspectedVehicleConfigLoaded_ = true;
        } catch (const std::exception& ex) {
            inspectedVehicleConfigLoaded_ = false;
            inspectedVehicleConfigError_ = ex.what();
        }
    }

    const ImVec4 accentPrimary{0.92f, 0.22f, 0.10f, 1.0f};
    const ImVec4 accentSecondary{0.98f, 0.63f, 0.16f, 1.0f};
    const ImVec4 cardBg{0.10f, 0.11f, 0.14f, 0.98f};

    ImGui::SetNextWindowSize(ImVec2(860.0f, 760.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    if (ImGui::Begin("Vehicle Profile Editor", &showVehicleConfigEditor_, ImGuiWindowFlags_NoCollapse)) {
        vehicleConfigEditorHovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        vehicleConfigEditorFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        auto beginVehicleConfigContinuousEdit = [&]() {
            if (!vehicleConfigEditActive_) {
                PushVehicleConfigUndoState();
                vehicleConfigEditActive_ = true;
            }
        };
        auto endVehicleConfigContinuousEdit = [&]() {
            if (ImGui::IsItemDeactivated()) {
                vehicleConfigEditActive_ = false;
            }
        };
        auto applyTextEdit = [&](const char* label, const char* id, std::string& value, std::size_t bufferSize = 256) {
            std::vector<char> buffer(bufferSize, '\0');
            std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
            if (RenderInspectorInputText(label, id, buffer.data(), buffer.size())) {
                beginVehicleConfigContinuousEdit();
                value = buffer.data();
            }
            endVehicleConfigContinuousEdit();
        };
        auto applyDragFloatEdit = [&](const char* label, const char* id, float& value, float speed, float minValue = 0.0f, float maxValue = 0.0f) {
            float edited = value;
            if (RenderInspectorDragFloat(label, id, &edited, speed, minValue, maxValue)) {
                beginVehicleConfigContinuousEdit();
                value = edited;
            }
            endVehicleConfigContinuousEdit();
        };
        auto applyDragFloat3Edit = [&](const char* label, const char* id, auto& value, float speed) {
            auto edited = value;
            if (RenderInspectorDragFloat3(label, id, &edited.x, speed)) {
                beginVehicleConfigContinuousEdit();
                value = edited;
            }
            endVehicleConfigContinuousEdit();
        };
        auto applyCheckboxEdit = [&](const char* label, bool& value) {
            bool edited = value;
            if (ImGui::Checkbox(label, &edited)) {
                PushVehicleConfigUndoState();
                vehicleConfigEditActive_ = false;
                value = edited;
            }
        };
        auto applyTransmissionModeEdit = [&](const char* label, raceman::physics::TransmissionConfig::Mode& value) {
            int currentIndex = value == raceman::physics::TransmissionConfig::Mode::Manual ? 1 : 0;
            const char* options[] = {"Automatic", "Manual"};
            if (ImGui::Combo(label, &currentIndex, options, IM_ARRAYSIZE(options))) {
                PushVehicleConfigUndoState();
                vehicleConfigEditActive_ = false;
                value = currentIndex == 1
                    ? raceman::physics::TransmissionConfig::Mode::Manual
                    : raceman::physics::TransmissionConfig::Mode::Automatic;
            }
        };
        auto beginCard = [&](const char* id, const char* title, const char* subtitle, const ImVec4& accent, float minHeight = 0.0f) {
            ImGui::PushID(id);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
            ImGui::BeginChild("##card", ImVec2(0.0f, minHeight), true);
            ImGui::TextColored(accent, "%s", title);
            if (subtitle != nullptr && subtitle[0] != '\0') {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", subtitle);
            }
            ImGui::Separator();
        };
        auto endCard = [&]() {
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopID();
        };
        auto renderSuspensionSection = [&](const char* idPrefix, const char* title, raceman::physics::SuspensionConfig& suspension, const ImVec4& accent) {
            beginCard(idPrefix, title, "Spring and damping", accent);
            applyDragFloatEdit("Rest Length", (std::string("##") + idPrefix + "_restLength").c_str(), suspension.restLength, 0.01f, 0.001f, 100000.0f);
            applyDragFloatEdit("Spring Rate", (std::string("##") + idPrefix + "_springRate").c_str(), suspension.springRate, 10.0f, 0.0f, 1000000.0f);
            applyDragFloatEdit("Bump Stop Rate", (std::string("##") + idPrefix + "_bumpStopRate").c_str(), suspension.bumpStopRate, 10.0f, 0.0f, 1000000.0f);
            applyDragFloatEdit("Compression Damping", (std::string("##") + idPrefix + "_compressionDamping").c_str(), suspension.compressionDamping, 10.0f, 0.0f, 1000000.0f);
            applyDragFloatEdit("Rebound Damping", (std::string("##") + idPrefix + "_reboundDamping").c_str(), suspension.reboundDamping, 10.0f, 0.0f, 1000000.0f);
            applyDragFloatEdit("Anti-Roll Stiffness", (std::string("##") + idPrefix + "_antiRollStiffness").c_str(), suspension.antiRollStiffness, 10.0f, 0.0f, 1000000.0f);
            endCard();
        };

        beginCard("vehicleProfileHero", "Garage Tuning", "Race setup profile", accentPrimary, 108.0f);
        ImGui::TextWrapped("%s", inspectedVehicleConfigPath_.c_str());
        ImGui::Spacing();
        if (ImGui::BeginTable("VehicleProfileHeroStats", 3, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("WHEELS");
            ImGui::Text("%.0f", static_cast<float>(inspectedVehicleConfig_.wheels.size()));
            ImGui::TableNextColumn();
            ImGui::TextDisabled("MASS");
            ImGui::Text("%.0f kg", inspectedVehicleConfig_.chassis.mass);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("REDLINE");
            ImGui::Text("%.0f rpm", inspectedVehicleConfig_.engine.redlineRPM);
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Button("Save Profile##vehicleConfigAsset", ImVec2(150.0f, 0.0f))) {
            SaveActiveAsset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Undo##vehicleConfigAsset", ImVec2(90.0f, 0.0f))) {
            UndoVehicleConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("Redo##vehicleConfigAsset", ImVec2(90.0f, 0.0f))) {
            RedoVehicleConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload##vehicleConfigAsset", ImVec2(120.0f, 0.0f))) {
            inspectedVehicleConfigLoaded_ = false;
            inspectedVehicleConfigError_.clear();
            vehicleConfigUndoStack_.clear();
            vehicleConfigRedoStack_.clear();
            vehicleConfigEditActive_ = false;
            endCard();
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }
        ImGui::TextDisabled("Shortcuts: Ctrl+S / Ctrl+Z / Ctrl+Y");
        endCard();

        if (!inspectedVehicleConfigLoaded_) {
            beginCard("vehicleProfileLoadError", "Load Failed", "Config could not be parsed", accentPrimary);
            ImGui::TextWrapped("%s", inspectedVehicleConfigError_.empty() ? "Unknown vehicle config load error." : inspectedVehicleConfigError_.c_str());
            if (ImGui::Button("Retry##vehicleConfigLoadRetry")) {
                inspectedVehicleConfigLoaded_ = false;
                inspectedVehicleConfigError_.clear();
            }
            endCard();
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }

        if (ImGui::BeginTabBar("VehicleProfileEditorTabs")) {
            if (ImGui::BeginTabItem("Setup")) {
                beginCard("vehicleProfileSetupCard", "Profile Identity", "Asset metadata", accentPrimary);
                applyTextEdit("Name", "##vehicleProfileName", inspectedVehicleConfig_.name);
                RenderInspectorWrappedValue("Asset:", inspectedVehicleConfigPath_);
                endCard();

                beginCard("vehicleProfileChassisCard", "Chassis", "Mass, inertia and balance", accentSecondary);
                applyDragFloatEdit("Mass", "##vehicleProfileChassisMass", inspectedVehicleConfig_.chassis.mass, 1.0f, 0.001f, 100000.0f);
                applyDragFloatEdit("Yaw Inertia", "##vehicleProfileChassisYawInertia", inspectedVehicleConfig_.chassis.yawInertia, 1.0f, 0.001f, 100000.0f);
                applyDragFloatEdit("Roll Inertia", "##vehicleProfileChassisRollInertia", inspectedVehicleConfig_.chassis.rollInertia, 1.0f, 0.001f, 100000.0f);
                applyDragFloatEdit("Pitch Inertia", "##vehicleProfileChassisPitchInertia", inspectedVehicleConfig_.chassis.pitchInertia, 1.0f, 0.001f, 100000.0f);
                applyDragFloat3Edit("Center of Mass", "##vehicleProfileChassisCenterOfMass", inspectedVehicleConfig_.chassis.centerOfMassOffset, 0.01f);
                endCard();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Handling")) {
                if (ImGui::BeginTable("VehicleHandlingColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    renderSuspensionSection("vehicleProfileFrontSuspension", "Front Suspension", inspectedVehicleConfig_.frontSuspension, accentPrimary);
                    ImGui::TableNextColumn();
                    renderSuspensionSection("vehicleProfileRearSuspension", "Rear Suspension", inspectedVehicleConfig_.rearSuspension, accentSecondary);
                    ImGui::EndTable();
                }

                beginCard("vehicleProfileDifferentialCard", "Differential", "Axle split and lock response", accentPrimary);
                applyDragFloatEdit("Torque Split", "##vehicleProfileDiffTorqueSplit", inspectedVehicleConfig_.differential.torqueSplit, 0.01f, 0.0f, 1.0f);
                applyDragFloatEdit("Locking Coefficient", "##vehicleProfileDiffLockingCoefficient", inspectedVehicleConfig_.differential.lockingCoefficient, 0.01f, 0.0f, 1.0f);
                applyCheckboxEdit("Limited Slip##vehicleProfileDiffLimitedSlip", inspectedVehicleConfig_.differential.limitedSlip);
                endCard();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Powertrain")) {
                if (ImGui::BeginTable("VehiclePowertrainColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    beginCard("vehicleProfileEngineCard", "Engine", "RPM and torque delivery", accentPrimary);
                    applyDragFloatEdit("Idle RPM", "##vehicleProfileEngineIdleRpm", inspectedVehicleConfig_.engine.idleRPM, 10.0f, 0.0f, 100000.0f);
                    applyDragFloatEdit("Redline RPM", "##vehicleProfileEngineRedlineRpm", inspectedVehicleConfig_.engine.redlineRPM, 10.0f, 0.0f, 100000.0f);
                    applyDragFloatEdit("Stall RPM", "##vehicleProfileEngineStallRpm", inspectedVehicleConfig_.engine.stallRPM, 10.0f, 0.0f, 100000.0f);
                    applyDragFloatEdit("Inertia", "##vehicleProfileEngineInertia", inspectedVehicleConfig_.engine.inertia, 0.01f, 0.0f, 1000.0f);
                    ImGui::Spacing();
                    ImGui::TextDisabled("Torque Curve");
                    if (ImGui::Button("Add Torque Point##vehicleProfileTorquePoint")) {
                        PushVehicleConfigUndoState();
                        vehicleConfigEditActive_ = false;
                        inspectedVehicleConfig_.engine.torqueCurve.push_back({1000.0f, 100.0f});
                    }
                    if (ImGui::BeginTable("VehicleTorqueCurveTable", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("RPM");
                        ImGui::TableSetupColumn("Torque");
                        ImGui::TableSetupColumn("");
                        for (int pointIndex = 0; pointIndex < static_cast<int>(inspectedVehicleConfig_.engine.torqueCurve.size()); ++pointIndex) {
                            raceman::physics::TorquePoint& point = inspectedVehicleConfig_.engine.torqueCurve[static_cast<std::size_t>(pointIndex)];
                            ImGui::PushID(pointIndex);
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::SetNextItemWidth(-1.0f);
                            float pointRpm = point.rpm;
                            if (ImGui::DragFloat("##rpm", &pointRpm, 10.0f, 0.0f, 100000.0f, "%.0f")) {
                                beginVehicleConfigContinuousEdit();
                                point.rpm = pointRpm;
                            }
                            endVehicleConfigContinuousEdit();
                            ImGui::TableNextColumn();
                            ImGui::SetNextItemWidth(-1.0f);
                            float pointTorque = point.torque;
                            if (ImGui::DragFloat("##torque", &pointTorque, 1.0f, 0.0f, 100000.0f, "%.1f")) {
                                beginVehicleConfigContinuousEdit();
                                point.torque = pointTorque;
                            }
                            endVehicleConfigContinuousEdit();
                            ImGui::TableNextColumn();
                            if (ImGui::SmallButton("X##removeTorquePoint")) {
                                PushVehicleConfigUndoState();
                                vehicleConfigEditActive_ = false;
                                inspectedVehicleConfig_.engine.torqueCurve.erase(inspectedVehicleConfig_.engine.torqueCurve.begin() + pointIndex);
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    endCard();

                    ImGui::TableNextColumn();
                    beginCard("vehicleProfileTransmissionCard", "Transmission", "Gearbox and final drive", accentSecondary);
                    applyTransmissionModeEdit("Mode##vehicleProfileTransmissionMode", inspectedVehicleConfig_.transmission.mode);
                    applyDragFloatEdit("Final Drive Ratio", "##vehicleProfileTransmissionFinalDrive", inspectedVehicleConfig_.transmission.finalDriveRatio, 0.01f, -1000.0f, 1000.0f);
                    applyDragFloatEdit("Reverse Ratio", "##vehicleProfileTransmissionReverseRatio", inspectedVehicleConfig_.transmission.reverseRatio, 0.01f, -1000.0f, 1000.0f);
                    applyDragFloatEdit("Shift Time", "##vehicleProfileTransmissionShiftTime", inspectedVehicleConfig_.transmission.shiftTime, 0.01f, 0.0f, 1000.0f);
                    if (inspectedVehicleConfig_.transmission.mode == raceman::physics::TransmissionConfig::Mode::Manual) {
                        ImGui::TextDisabled("Manual: E/PageUp shift up, Q/PageDown shift down, N neutral, R reverse.");
                    } else {
                        ImGui::TextDisabled("Automatic: W drive, S brake then auto-reverse near stop.");
                    }
                    ImGui::Spacing();
                    ImGui::TextDisabled("Gear Ratios");
                    if (ImGui::Button("Add Gear##vehicleProfileGearRatio")) {
                        PushVehicleConfigUndoState();
                        vehicleConfigEditActive_ = false;
                        inspectedVehicleConfig_.transmission.gearRatios.push_back(1.0f);
                    }
                    if (ImGui::BeginTable("VehicleGearRatioTable", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("Gear");
                        ImGui::TableSetupColumn("Ratio");
                        ImGui::TableSetupColumn("");
                        for (int gearIndex = 0; gearIndex < static_cast<int>(inspectedVehicleConfig_.transmission.gearRatios.size()); ++gearIndex) {
                            ImGui::PushID(gearIndex);
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("G%d", gearIndex + 1);
                            ImGui::TableNextColumn();
                            ImGui::SetNextItemWidth(-1.0f);
                            float gearRatio = inspectedVehicleConfig_.transmission.gearRatios[static_cast<std::size_t>(gearIndex)];
                            if (ImGui::DragFloat("##ratio", &gearRatio, 0.01f, -1000.0f, 1000.0f, "%.2f")) {
                                beginVehicleConfigContinuousEdit();
                                inspectedVehicleConfig_.transmission.gearRatios[static_cast<std::size_t>(gearIndex)] = gearRatio;
                            }
                            endVehicleConfigContinuousEdit();
                            ImGui::TableNextColumn();
                            if (ImGui::SmallButton("X##removeGearRatio")) {
                                PushVehicleConfigUndoState();
                                vehicleConfigEditActive_ = false;
                                inspectedVehicleConfig_.transmission.gearRatios.erase(inspectedVehicleConfig_.transmission.gearRatios.begin() + gearIndex);
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    endCard();
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Wheels")) {
                if (ImGui::Button("Add Wheel##vehicleProfileWheelAdd")) {
                    PushVehicleConfigUndoState();
                    vehicleConfigEditActive_ = false;
                    inspectedVehicleConfig_.wheels.push_back({});
                }
                ImGui::Spacing();
                for (int wheelIndex = 0; wheelIndex < static_cast<int>(inspectedVehicleConfig_.wheels.size()); ++wheelIndex) {
                    raceman::physics::WheelConfig& wheel = inspectedVehicleConfig_.wheels[static_cast<std::size_t>(wheelIndex)];
                    ImGui::PushID(wheelIndex);
                    const bool frontAxle = wheel.mountPosition.z >= 0.0f;
                    const ImVec4 wheelAccent = frontAxle ? accentPrimary : accentSecondary;
                    const std::string title = wheel.name.empty() ? ("Wheel " + std::to_string(wheelIndex + 1)) : wheel.name;
                    const std::string subtitle = std::string(frontAxle ? "Front axle" : "Rear axle")
                        + (wheel.driven ? " | Driven" : "")
                        + (wheel.hasBrake ? " | Brake" : "");
                    beginCard("vehicleProfileWheelCard", title.c_str(), subtitle.c_str(), wheelAccent);
                    auto renderWheelScalarPair = [&](const char* leftLabel,
                                                     const char* leftId,
                                                     float& leftValue,
                                                     float leftSpeed,
                                                     float leftMin,
                                                     float leftMax,
                                                     const char* rightLabel,
                                                     const char* rightId,
                                                     float& rightValue,
                                                     float rightSpeed,
                                                     float rightMin,
                                                     float rightMax) {
                        if (ImGui::BeginTable("##wheelScalarPair", 2, ImGuiTableFlags_SizingStretchSame)) {
                            ImGui::TableNextColumn();
                            applyDragFloatEdit(leftLabel, leftId, leftValue, leftSpeed, leftMin, leftMax);
                            ImGui::TableNextColumn();
                            applyDragFloatEdit(rightLabel, rightId, rightValue, rightSpeed, rightMin, rightMax);
                            ImGui::EndTable();
                        }
                    };

                    applyTextEdit("Name", "##vehicleProfileWheelName", wheel.name, 128);
                    applyDragFloat3Edit("Mount Position", "##vehicleProfileWheelMountPosition", wheel.mountPosition, 0.01f);

                    ImGui::Spacing();
                    ImGui::TextDisabled("Geometry");
                    renderWheelScalarPair("Radius", "##vehicleProfileWheelRadius", wheel.radius, 0.01f, 0.001f, 1000.0f,
                                          "Width", "##vehicleProfileWheelWidth", wheel.width, 0.01f, 0.001f, 1000.0f);

                    ImGui::TextDisabled("Mass");
                    renderWheelScalarPair("Mass", "##vehicleProfileWheelMass", wheel.mass, 0.1f, 0.001f, 10000.0f,
                                          "Inertia", "##vehicleProfileWheelInertia", wheel.inertia, 0.01f, 0.001f, 10000.0f);

                    ImGui::TextDisabled("Alignment");
                    if (ImGui::BeginTable("##wheelAlignmentRow", 3, ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableNextColumn();
                        applyDragFloatEdit("Steer", "##vehicleProfileWheelMaxSteerAngle", wheel.maxSteerAngle, 0.01f, -10.0f, 10.0f);
                        ImGui::TableNextColumn();
                        applyDragFloatEdit("Camber", "##vehicleProfileWheelCamber", wheel.camber, 0.01f, -10.0f, 10.0f);
                        ImGui::TableNextColumn();
                        applyDragFloatEdit("Toe", "##vehicleProfileWheelToe", wheel.toe, 0.01f, -10.0f, 10.0f);
                        ImGui::EndTable();
                    }

                    ImGui::TextDisabled("Tire");
                    renderWheelScalarPair("Grip", "##vehicleProfileWheelGripFactor", wheel.gripFactor, 0.01f, 0.0f, 1000.0f,
                                          "Brake Torque", "##vehicleProfileWheelMaxBrakingTorque", wheel.maxBrakingTorque, 10.0f, 0.0f, 1000000.0f);
                    renderWheelScalarPair("Long Stiffness", "##vehicleProfileWheelLongitudinalStiffness", wheel.longitudinalStiffness, 10.0f, 0.0f, 1000000.0f,
                                          "Lat Stiffness", "##vehicleProfileWheelLateralStiffness", wheel.lateralStiffness, 10.0f, 0.0f, 1000000.0f);

                    if (ImGui::BeginTable("##wheelFlagsRow", 3, ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableNextColumn();
                        applyCheckboxEdit("Driven##vehicleProfileWheelDriven", wheel.driven);
                        ImGui::TableNextColumn();
                        applyCheckboxEdit("Has Brake##vehicleProfileWheelHasBrake", wheel.hasBrake);
                        ImGui::TableNextColumn();
                        ImGui::Dummy(ImVec2(0.0f, 0.0f));
                        ImGui::EndTable();
                    }
                    if (ImGui::Button("Remove Wheel##vehicleProfileWheel")) {
                        PushVehicleConfigUndoState();
                        vehicleConfigEditActive_ = false;
                        inspectedVehicleConfig_.wheels.erase(inspectedVehicleConfig_.wheels.begin() + wheelIndex);
                        endCard();
                        ImGui::PopID();
                        break;
                    }
                    endCard();
                    ImGui::PopID();
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    if (!showVehicleConfigEditor_) {
        vehicleConfigEditorHovered_ = false;
        vehicleConfigEditorFocused_ = false;
        vehicleConfigEditActive_ = false;
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

    std::string materialProjectPath;
    for (const std::string& file : projectFiles_) {
        if (IsMaterialAssetPath(file) && MaterialIdFromAssetPath(file) == materialId) {
            materialProjectPath = file;
            break;
        }
    }

    const fs::path materialDirectory = materialProjectPath.empty()
        ? FindAssetsRoot()
        : ProjectAssetPathToAbsolute(ParentProjectDirectory(materialProjectPath));

    auto resolveTexturePath = [&](const std::string& value) -> fs::path {
        if (value.empty()) {
            return {};
        }
        fs::path path = fs::path(NormalizeSlashes(value));
        if (path.is_absolute()) {
            return path.lexically_normal();
        }
        if (!value.empty() && NormalizeSlashes(value).rfind("assets/", 0) == 0) {
            return ProjectAssetPathToAbsolute(value);
        }
        return (materialDirectory / path).lexically_normal();
    };

    auto storeTexturePath = [&](const fs::path& absolutePath, std::string& value) {
        const fs::path normalized = absolutePath.lexically_normal();
        const fs::path assetsRoot = FindAssetsRoot();
        if (IsUnderPath(normalized, assetsRoot)) {
            value = ToProjectAssetPath(normalized, assetsRoot);
        } else {
            value = NormalizeSlashes(normalized.string());
        }
    };

    auto editTexturePath = [&](const char* label, const char* idSuffix, std::string& value) {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
        if (RenderInspectorInputText(label, idSuffix, buffer, sizeof(buffer))) {
            value = buffer;
        }
        const fs::path resolvedPath = resolveTexturePath(value);
        const std::string wrappedValue = value.empty() ? std::string("(none)") : NormalizeSlashes(value);
        RenderInspectorWrappedValue("Path:", wrappedValue);
        if (ImGui::Button((std::string("Browse##") + idSuffix).c_str())) {
            const fs::path initialDirectory = resolvedPath.empty() ? materialDirectory : resolvedPath.parent_path();
            const std::string selected = OpenTextureFileDialogWin32(initialDirectory.string());
            if (!selected.empty()) {
                storeTexturePath(fs::path(selected), value);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string("Show##") + idSuffix).c_str())) {
            if (!resolvedPath.empty() && !RevealAbsolutePathInExplorer(resolvedPath) && console_) {
                console_->AddError("Failed to reveal texture path: " + NormalizeSlashes(resolvedPath.string()));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string("Clear##") + idSuffix).c_str())) {
            value.clear();
        }
    };

    editTexturePath("Albedo", "matTexAlbedo", material->texAlbedo);
    editTexturePath("Normal", "matTexNormal", material->texNormal);
    editTexturePath("Metallic", "matTexMetallic", material->texMetallic);
    editTexturePath("Roughness", "matTexRoughness", material->texRoughness);
    editTexturePath("AO", "matTexAo", material->texAo);

    ImGui::Separator();
    if (ImGui::Button("Save Material (Ctrl+S)")) {
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

bool SceneEditor::AssignVehicleConfigToSelected(const std::string& configPath) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        return false;
    }

    const std::string normalizedConfigPath = NormalizeSlashes(configPath);
    NormalizeSelection();

    raceman::physics::VehicleConfig loadedConfig;
    const bool configLoaded = !normalizedConfigPath.empty() && TryLoadVehicleConfigForPath(normalizedConfigPath, loadedConfig);
    int assignableCount = 0;
    for (int index : selectedIndices_) {
        if (index < 0 || index >= static_cast<int>(objects_.size())) {
            continue;
        }

        const SceneObject& object = objects_[index];
        if (!object.hasVehicle) {
            continue;
        }

        const bool configChanged = object.vehicle.configPath != normalizedConfigPath;
        const bool shouldClearBindings = normalizedConfigPath.empty() && !object.vehicle.wheelBindings.empty();
        if (configChanged || shouldClearBindings || configLoaded) {
            ++assignableCount;
        }
    }

    if (assignableCount <= 0) {
        return false;
    }

    PushUndoState();
    int assignedCount = 0;
    for (int index : selectedIndices_) {
        if (index < 0 || index >= static_cast<int>(objects_.size())) {
            continue;
        }

        SceneObject& object = objects_[index];
        if (!object.hasVehicle) {
            continue;
        }

        const bool configChanged = object.vehicle.configPath != normalizedConfigPath;
        const bool shouldClearBindings = normalizedConfigPath.empty() && !object.vehicle.wheelBindings.empty();
        if (!configChanged && !shouldClearBindings && !configLoaded) {
            continue;
        }

        object.vehicle.configPath = normalizedConfigPath;
        if (configLoaded) {
            SyncVehicleWheelBindings(object.vehicle, loadedConfig);
        } else if (normalizedConfigPath.empty()) {
            object.vehicle.wheelBindings.clear();
        }

        ++assignedCount;
    }

    if (console_) {
        if (normalizedConfigPath.empty()) {
            console_->AddLog("Cleared vehicle config on " + std::to_string(assignedCount) + " selected objects.");
        } else if (assignedCount == 1) {
            console_->AddLog("Assigned vehicle config " + normalizedConfigPath + " to " + objects_[selectedIndex_].name);
        } else {
            console_->AddLog("Assigned vehicle config " + normalizedConfigPath + " to " + std::to_string(assignedCount) + " selected objects.");
        }
    }
    if (onDirty_) onDirty_();
    return true;
}

void SceneEditor::OpenVehicleConfigEditor(const std::string& configPath) {
    if (configPath.empty()) {
        return;
    }

    const std::string normalizedPath = NormalizeSlashes(configPath);
    const bool pathChanged = inspectedVehicleConfigPath_ != normalizedPath;
    inspectedVehicleConfigPath_ = normalizedPath;
    selectedProjectFile_ = inspectedVehicleConfigPath_;
    selectedProjectDirectory_ = ParentProjectDirectory(inspectedVehicleConfigPath_);
    if (pathChanged) {
        inspectedVehicleConfigLoaded_ = false;
        inspectedVehicleConfigError_.clear();
        vehicleConfigUndoStack_.clear();
        vehicleConfigRedoStack_.clear();
        vehicleConfigEditActive_ = false;
    }
    showVehicleConfigEditor_ = true;
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


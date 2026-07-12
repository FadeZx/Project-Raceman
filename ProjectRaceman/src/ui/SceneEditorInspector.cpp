#include "SceneEditorInternal.h"
#include "../rendering/ShaderRegistry.h"
#include "../physics/SimpleJson.h"
#include "../physics/VehicleConfig.h"
#include "../physics/VehiclePhysics.h"
#include "../scripting/ScriptRegistry.h"

#include <glad/glad.h>
#include <stb_image.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/norm.hpp>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

constexpr const char* kInspectorComponentReorderPayload = "RM_COMP_REORDER";

const char* InputDevicePreferenceLabel(InputDevicePreference value) {
    switch (value) {
    case InputDevicePreference::Keyboard: return "Keyboard";
    case InputDevicePreference::Gamepad: return "Gamepad";
    case InputDevicePreference::Wheel: return "Wheel";
    case InputDevicePreference::Specific: return "Specific Device";
    case InputDevicePreference::Any:
    default: return "Any";
    }
}

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
    const std::string fullId = std::string(label) + (id != nullptr && std::strncmp(id, "##", 2) == 0 ? id : (std::string("##") + (id != nullptr ? id : label)));
    return ImGui::InputText(fullId.c_str(), buffer, bufferSize);
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
    const std::string fullId = std::string(label) + (id != nullptr && std::strncmp(id, "##", 2) == 0 ? id : (std::string("##") + (id != nullptr ? id : label)));
    return ImGui::DragFloat(fullId.c_str(), value, speed, min, max);
}

bool RenderInspectorDragInt(const char* label, const char* id, int* value, float speed, int min = 0, int max = 0) {
    if (IsNarrowInspectorLayout()) {
        RenderInspectorLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::DragInt(id, value, speed, min, max);
    }
    return ImGui::DragInt(label, value, speed, min, max);
}

const char* MaterialPropertyTypeName(MaterialPropertyType type) {
    switch (type) {
    case MaterialPropertyType::Float: return "float";
    case MaterialPropertyType::Vec2: return "vec2";
    case MaterialPropertyType::Vec3: return "vec3";
    case MaterialPropertyType::Vec4: return "vec4";
    case MaterialPropertyType::Bool: return "bool";
    case MaterialPropertyType::Texture2D: return "texture2D";
    default: return "float";
    }
}

int MaterialPropertyComponentCount(MaterialPropertyType type) {
    switch (type) {
    case MaterialPropertyType::Vec2: return 2;
    case MaterialPropertyType::Vec3: return 3;
    case MaterialPropertyType::Vec4: return 4;
    default: return 1;
    }
}

void RenderShaderGraphParametersPreview(const std::string& graphPath) {
    if (graphPath.empty()) return;

    std::ifstream in(ProjectAssetPathToAbsolute(graphPath));
    if (!in.good()) {
        ImGui::TextDisabled("Graph parameters unavailable");
        return;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    try {
        using namespace raceman::physics::json;
        Value root = parse(buffer.str());
        if (!root.is_object()) return;
        const auto& object = root.as_object();
        auto nodesIt = object.find("nodes");
        if (nodesIt == object.end() || !nodesIt->second.is_array()) return;

        bool drewHeader = false;
        int visibleCount = 0;
        for (const Value& nodeValue : nodesIt->second.as_array()) {
            if (!nodeValue.is_object()) continue;
            const auto& nodeObject = nodeValue.as_object();
            auto typeIt = nodeObject.find("type");
            if (typeIt == nodeObject.end() || !typeIt->second.is_string()) continue;
            const std::string type = typeIt->second.as_string();
            if (type != "Color" && type != "Float" && type != "Vector2" && type != "Vector3" && type != "Vector4" && type != "TextureSample") continue;

            auto propsIt = nodeObject.find("properties");
            if (propsIt == nodeObject.end() || !propsIt->second.is_object()) continue;
            const auto& props = propsIt->second.as_object();

            std::string title = type;
            if (auto titleIt = nodeObject.find("title"); titleIt != nodeObject.end() && titleIt->second.is_string()) {
                title = titleIt->second.as_string();
            }

            if (!drewHeader) {
                ImGui::Separator();
                ImGui::TextUnformatted("Graph Parameters");
                drewHeader = true;
            }

            ImGui::PushID(visibleCount++);
            if (type == "TextureSample") {
                std::string slot = "albedo";
                if (auto slotIt = props.find("textureSlot"); slotIt != props.end() && slotIt->second.is_string()) {
                    slot = slotIt->second.as_string();
                }
                ImGui::BeginDisabled();
                char slotBuffer[64];
                std::snprintf(slotBuffer, sizeof(slotBuffer), "%s", slot.c_str());
                ImGui::InputText(title.c_str(), slotBuffer, sizeof(slotBuffer));
                ImGui::EndDisabled();
            } else if (type == "Float") {
                float value = 0.0f;
                if (auto valueIt = props.find("value"); valueIt != props.end() && valueIt->second.is_number()) {
                    value = static_cast<float>(valueIt->second.as_number());
                }
                ImGui::BeginDisabled();
                ImGui::DragFloat(title.c_str(), &value, 0.01f);
                ImGui::EndDisabled();
            } else {
                float values[4]{0.0f, 0.0f, 0.0f, 1.0f};
                const char* key = type == "Color" ? "color" : "vector";
                if (auto valueIt = props.find(key); valueIt != props.end() && valueIt->second.is_array()) {
                    const auto& array = valueIt->second.as_array();
                    for (std::size_t i = 0; i < array.size() && i < 4; ++i) {
                        if (array[i].is_number()) values[i] = static_cast<float>(array[i].as_number());
                    }
                }
                ImGui::BeginDisabled();
                if (type == "Color") ImGui::ColorEdit4(title.c_str(), values);
                else if (type == "Vector2") ImGui::DragFloat2(title.c_str(), values, 0.01f);
                else if (type == "Vector3") ImGui::DragFloat3(title.c_str(), values, 0.01f);
                else ImGui::DragFloat4(title.c_str(), values, 0.01f);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
    } catch (...) {
        ImGui::TextDisabled("Graph parameters unavailable");
    }
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
    struct CachedVehicleConfig {
        raceman::physics::VehicleConfig config;
        fs::file_time_type modifiedTime{};
        bool valid{false};
    };
    static std::unordered_map<std::string, CachedVehicleConfig> cache;

    try {
        const fs::path absolutePath = ProjectAssetPathToAbsolute(projectPath);
        std::error_code ec;
        const fs::file_time_type modifiedTime = fs::last_write_time(absolutePath, ec);
        if (ec) {
            return false;
        }

        CachedVehicleConfig& cached = cache[absolutePath.string()];
        if (!cached.valid || cached.modifiedTime != modifiedTime) {
            cached.config = raceman::physics::VehicleConfigLoader::loadFromFile(absolutePath.string());
            cached.modifiedTime = modifiedTime;
            cached.valid = true;
        }
        outConfig = cached.config;
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

ImVec4 ScaleColor(const ImVec4& color, float scale) {
    return ImVec4(
        (std::min)(1.0f, color.x * scale),
        (std::min)(1.0f, color.y * scale),
        (std::min)(1.0f, color.z * scale),
        color.w);
}

ImVec4 ComponentHeaderAccent(const char* label) {
    if (std::strcmp(label, "Rigidbody") == 0) {
        return ImVec4(0.30f, 0.40f, 0.26f, 1.0f);
    }
    if (std::strcmp(label, "Vehicle") == 0 || std::strcmp(label, "Vehicle Sound") == 0) {
        return ImVec4(0.16f, 0.34f, 0.52f, 1.0f);
    }
    if (std::strcmp(label, "Collider") == 0 || std::strcmp(label, "Character Controller") == 0) {
        return ImVec4(0.34f, 0.28f, 0.48f, 1.0f);
    }
    if (std::strcmp(label, "Camera") == 0 || std::strcmp(label, "Cinemachine Camera") == 0) {
        return ImVec4(0.22f, 0.40f, 0.46f, 1.0f);
    }
    if (std::strcmp(label, "Light") == 0) {
        return ImVec4(0.52f, 0.40f, 0.14f, 1.0f);
    }
    if (std::strcmp(label, "Audio Listener") == 0 || std::strcmp(label, "Audio Source") == 0) {
        return ImVec4(0.38f, 0.28f, 0.44f, 1.0f);
    }
    if (std::strcmp(label, "Mesh Filter") == 0 || std::strcmp(label, "Mesh Renderer") == 0) {
        return ImVec4(0.20f, 0.34f, 0.50f, 1.0f);
    }
    return ImVec4(0.16f, 0.32f, 0.50f, 1.0f);
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
    const ImVec4 accent = ComponentHeaderAccent(label);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID openId = ImGui::GetID("##componentOpen");
    bool open = storage->GetBool(openId, true);
    enabledChanged = false;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowHeight = (std::max)(24.0f, ImGui::GetFrameHeight());
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const ImVec2 rowSize(rowWidth, rowHeight);
    const ImVec2 rowMax(rowMin.x + rowSize.x, rowMin.y + rowSize.y);
    const float removeButtonWidth = ImGui::CalcTextSize("Remove").x + style.FramePadding.x * 2.0f;
    const ImVec2 removeMin(rowMax.x - removeButtonWidth - 4.0f, rowMin.y + 2.0f);
    const ImVec2 removeMax(rowMax.x - 4.0f, rowMax.y - 2.0f);
    const ImVec2 checkMin(rowMin.x + 30.0f, rowMin.y + (rowHeight - 14.0f) * 0.5f);
    const ImVec2 checkMax(checkMin.x + 14.0f, checkMin.y + 14.0f);
    const ImVec2 iconMin(rowMin.x + 52.0f, rowMin.y + (rowHeight - 18.0f) * 0.5f);
    const ImVec2 iconMax(iconMin.x + 18.0f, iconMin.y + 18.0f);
    const ImVec2 textPos(rowMin.x + (textureId != 0 ? 76.0f : 54.0f), rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    auto containsPoint = [](const ImVec2& min, const ImVec2& max, const ImVec2& point) {
        return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
    };
    const bool mouseOnCheckbox = enabled != nullptr && containsPoint(checkMin, checkMax, mousePos);
    const bool mouseOnRemove = containsPoint(removeMin, removeMax, mousePos);

    ImGui::SetNextItemAllowOverlap();
    const bool rowPressed = ImGui::InvisibleButton("##componentHeaderRow", rowSize);
    const bool rowHovered = ImGui::IsItemHovered();
    const bool rowFocused = ImGui::IsItemFocused();
    const bool rowActive = ImGui::IsItemActive();
    bool toggledOpen = false;
    if (rowPressed && !mouseOnCheckbox && !mouseOnRemove) {
        open = !open;
        storage->SetBool(openId, open);
        toggledOpen = true;
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

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool rowHighlighted = rowFocused || rowActive;
    const ImVec4 bg = rowHighlighted ? ScaleColor(accent, 1.08f) : (rowHovered ? ScaleColor(accent, 0.92f) : ScaleColor(accent, 0.74f));
    drawList->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(bg), 2.0f);
    if (rowHighlighted) {
        const ImU32 focusColor = IM_COL32(105, 190, 255, 255);
        drawList->AddRect(rowMin, rowMax, focusColor, 2.0f, 0, 2.0f);
        drawList->AddRect(ImVec2(rowMin.x + 1.0f, rowMin.y + 1.0f), ImVec2(rowMax.x - 1.0f, rowMax.y - 1.0f), IM_COL32(230, 248, 255, 170), 2.0f, 0, 1.0f);
        drawList->AddRectFilled(ImVec2(rowMin.x, rowMin.y), ImVec2(rowMin.x + 4.0f, rowMax.y), focusColor, 2.0f);
    }
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const float arrowX = rowMin.x + 12.0f;
    const float arrowY = rowMin.y + rowHeight * 0.5f;
    const float arrowHalf = 4.0f;
    if (open) {
        drawList->AddTriangleFilled(
            ImVec2(arrowX - arrowHalf, arrowY - arrowHalf * 0.55f),
            ImVec2(arrowX + arrowHalf, arrowY - arrowHalf * 0.55f),
            ImVec2(arrowX, arrowY + arrowHalf * 0.75f),
            textColor);
    } else {
        drawList->AddTriangleFilled(
            ImVec2(arrowX - arrowHalf * 0.55f, arrowY - arrowHalf),
            ImVec2(arrowX - arrowHalf * 0.55f, arrowY + arrowHalf),
            ImVec2(arrowX + arrowHalf * 0.75f, arrowY),
            textColor);
    }

    if (enabled != nullptr) {
        const ImVec2 restorePos = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(checkMin);
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InvisibleButton("##componentEnabled", ImVec2(14.0f, 14.0f))) {
            *enabled = !*enabled;
            enabledChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable Component");
        }
        ImGui::SetCursorScreenPos(restorePos);
        drawList->AddRectFilled(checkMin, checkMax, IM_COL32(12, 18, 24, 255), 2.0f);
        drawList->AddRect(checkMin, checkMax, IM_COL32(120, 160, 210, 210), 2.0f);
        if (*enabled) {
            drawList->AddLine(ImVec2(checkMin.x + 3.0f, checkMin.y + 7.0f), ImVec2(checkMin.x + 6.0f, checkMin.y + 10.0f), IM_COL32(100, 190, 255, 255), 2.0f);
            drawList->AddLine(ImVec2(checkMin.x + 6.0f, checkMin.y + 10.0f), ImVec2(checkMin.x + 11.0f, checkMin.y + 3.0f), IM_COL32(100, 190, 255, 255), 2.0f);
        }
    }
    if (textureId != 0) {
        drawList->AddImage(static_cast<ImTextureID>(textureId), iconMin, iconMax);
    }
    drawList->AddText(textPos, textColor, label);
    if (outHeaderActive != nullptr) {
        *outHeaderActive = rowHovered || rowFocused || rowActive;
    }
    if (outHeaderToggledOpen != nullptr) {
        *outHeaderToggledOpen = toggledOpen;
    }
    {
        const ImVec2 restorePos = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(removeMin);
        ImGui::SetNextItemAllowOverlap();
        removeRequested = ImGui::Button("Remove", ImVec2(removeButtonWidth, removeMax.y - removeMin.y));
        ImGui::SetCursorScreenPos(restorePos);
    }
    ImGui::PopID();
    return open;
}

bool BeginInspectorSubsection(const char* label,
                              const char* id,
                              const ImVec4& accent,
                              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen) {
    ImGui::Spacing();
    ImGui::Indent(14.0f);
    ImGui::PushStyleColor(ImGuiCol_Header, ScaleColor(accent, 0.74f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ScaleColor(accent, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ScaleColor(accent, 1.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
    const std::string headerId = std::string(label) + "##" + id;
    const bool open = ImGui::CollapsingHeader(headerId.c_str(), flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (open) {
        ImGui::Indent(12.0f);
    } else {
        ImGui::Unindent(14.0f);
    }
    return open;
}

void EndInspectorSubsection() {
    ImGui::Unindent(12.0f);
    ImGui::Unindent(14.0f);
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

bool RenderTagCombo(const char* label,
                    const char* comboId,
                    const std::string& currentTag,
                    const std::vector<std::string>& tags,
                    std::string& outTag) {
    const std::string safeCurrentTag = currentTag.empty() ? "Untagged" : currentTag;
    if (!ImGui::BeginCombo(label, safeCurrentTag.c_str())) {
        return false;
    }

    bool changed = false;
    bool currentTagWasListed = false;
    for (const std::string& tag : tags) {
        if (tag.empty()) {
            continue;
        }
        const bool selected = tag == safeCurrentTag;
        currentTagWasListed = currentTagWasListed || selected;
        const std::string selectableLabel = tag + "##" + comboId + "_" + tag;
        if (ImGui::Selectable(selectableLabel.c_str(), selected)) {
            outTag = tag;
            changed = true;
        }
        if (selected) {
            ImGui::SetItemDefaultFocus();
        }
    }

    if (!currentTagWasListed && safeCurrentTag != "Untagged") {
        ImGui::Separator();
        const std::string missingLabel = safeCurrentTag + " (missing)##" + comboId + "_missing";
        if (ImGui::Selectable(missingLabel.c_str(), true)) {
            outTag = safeCurrentTag;
            changed = true;
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Add tags in Project Settings > Tags & Layers");
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
    if (IsPanelHiddenByFullscreen("Inspector")) {
        inspectorPanelHovered_ = false;
        inspectorPanelFocused_ = false;
        return;
    }

    ApplyPanelFullscreenWindowSetup("Inspector");
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | PanelFullscreenWindowFlags("Inspector");
    if (ImGui::Begin("Inspector", nullptr, windowFlags)) {
        HandlePanelHeadingDoubleClick("Inspector");
        inspectorPanelHovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        inspectorPanelFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        inspectorKeyboardTargetComponentKey_.clear();
        inspectorKeyboardTargetObjectId_.clear();
        if (inspectMaterial_) {
            RenderMaterialInspector();
        } else if (selectedIndex_ < 0 && IsMeshAssetPath(selectedProjectFile_) && selectedModelChildMeshIndex_ >= 0) {
            RenderModelChildAssetInspector();
        } else if (selectedIndex_ < 0 && IsMeshAssetPath(selectedProjectFile_)) {
            RenderModelAssetInspector();
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

            EnsureProjectTags();
            if (ImGui::BeginTable("ObjectTagLayerRow", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                std::string selectedTag = obj.tag.empty() ? "Untagged" : obj.tag;
                if (RenderTagCombo("Tag", "singleTag", selectedTag, projectTags_, selectedTag)) {
                    PushUndoState();
                    obj.tag = selectedTag.empty() ? "Untagged" : selectedTag;
                    if (onDirty_) onDirty_();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                int physicsLayer = ClampPhysicsLayerIndex(obj.physicsLayer);
                if (RenderPhysicsLayerCombo("Layer", "singlePhysicsLayer", obj.physicsLayer, physicsLayerNames_, physicsLayer)) {
                    PushUndoState();
                    obj.physicsLayer = physicsLayer;
                    if (onDirty_) onDirty_();
                }
                ImGui::EndTable();
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
                {
                    auto isAttachedByName = [&](const std::string& name) {
                        if (!obj.hasScriptComponent) return false;
                        for (const ObjectScriptAttachment& a : obj.scriptComponent.attachments) {
                            if (a.scriptName == name) return true;
                        }
                        return false;
                    };
                    anyAvailable = true;
                    if (ImGui::BeginMenu("Script")) {
                        const std::vector<std::pair<std::string, std::string>> projectScripts = ScanProjectScripts();
                        std::vector<std::pair<std::string, std::string>> available;
                        available.reserve(projectScripts.size());
                        for (const auto& entry : projectScripts) {
                            if (!isAttachedByName(entry.first)) {
                                available.push_back(entry);
                            }
                        }
                        if (available.empty()) {
                            if (projectScripts.empty()) {
                                ImGui::TextDisabled("No scripts in project.");
                            } else {
                                ImGui::TextDisabled("All project scripts attached.");
                            }
                        }
                        for (const auto& entry : available) {
                            const std::string label = entry.first + "##addProjectScript";
                            if (ImGui::MenuItem(label.c_str())) {
                                AttachScriptToSelected(entry.first, entry.second);
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", entry.second.c_str());
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("New C++ Script...")) {
                            createScriptNameBuffer_[0] = '\0';
                            showCreateScriptPopup_ = true;
                        }
                        ImGui::EndMenu();
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
                if (!obj.hasAudioListener) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Audio Listener")) {
                        PushUndoState();
                        obj.hasAudioListener = true;
                        obj.audioListener = AudioListenerComponent{};
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasAudioSource) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Audio Source")) {
                        PushUndoState();
                        obj.hasAudioSource = true;
                        obj.audioSource = AudioSourceComponent{};
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasVehicleSound) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Vehicle Sound")) {
                        PushUndoState();
                        obj.hasVehicleSound = true;
                        obj.vehicleSound = VehicleSoundComponent{};
                        if (onDirty_) onDirty_();
                    }
                }
                if (!obj.hasTrackGenerator) {
                    anyAvailable = true;
                    if (ImGui::MenuItem("Track Generator")) {
                        PushUndoState();
                        obj.hasTrackGenerator = true;
                        obj.trackGenerator = TrackGeneratorComponent{};
                        if (onDirty_) onDirty_();
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
                case SceneInspectorComponentType::AudioListener: typeName = "AudioListener"; break;
                case SceneInspectorComponentType::AudioSource: typeName = "AudioSource"; break;
                case SceneInspectorComponentType::VehicleSound: typeName = "VehicleSound"; break;
                case SceneInspectorComponentType::TrackGenerator: typeName = "TrackGenerator"; break;
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
                    if (onDirty_) onDirty_();
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
                    if (onDirty_) onDirty_();
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
                    RenderInspectorWrappedValue("Mesh Filter ID:", obj.meshFilter.assetId.empty() ? ModelChildAssetId(obj.meshFilter.sourcePath, obj.meshFilter.meshIndex) : obj.meshFilter.assetId);
                    ImGui::TextDisabled("Submesh Index: %d", obj.meshFilter.meshIndex);
                    RenderInspectorWrappedValue("Submesh Name:", obj.meshFilter.meshName.empty() ? "(unnamed)" : obj.meshFilter.meshName);
                    RenderInspectorWrappedValue("Imported Material:", obj.meshFilter.importedMaterialName.empty() ? "(none)" : obj.meshFilter.importedMaterialName);
                    RenderInspectorWrappedValue("Imported Diffuse:", obj.meshFilter.diffuseTexturePath.empty() ? "(none)" : obj.meshFilter.diffuseTexturePath);
                } else {
                    ImGui::TextDisabled("Built-in mesh: %s", meshType.c_str());
                }
                ImGui::SeparatorText("Mesh Data");
                const unsigned int indexCount = obj.meshFilter.indexCount;
                const unsigned int polygonCount = indexCount / 3;
                const std::size_t vertexCount = !obj.meshFilter.pickVertices.empty()
                    ? obj.meshFilter.pickVertices.size()
                    : 0;
                ImGui::TextDisabled("Vertices: %s",
                    vertexCount > 0 ? std::to_string(vertexCount).c_str() : "(not cached)");
                ImGui::TextDisabled("Indices: %u", indexCount);
                ImGui::TextDisabled("Polygons: %u triangles", polygonCount);
                const glm::vec3 boundsSize = obj.meshFilter.localBoundsMax - obj.meshFilter.localBoundsMin;
                ImGui::TextDisabled("Bounds Min: %.3f, %.3f, %.3f",
                    obj.meshFilter.localBoundsMin.x,
                    obj.meshFilter.localBoundsMin.y,
                    obj.meshFilter.localBoundsMin.z);
                ImGui::TextDisabled("Bounds Max: %.3f, %.3f, %.3f",
                    obj.meshFilter.localBoundsMax.x,
                    obj.meshFilter.localBoundsMax.y,
                    obj.meshFilter.localBoundsMax.z);
                ImGui::TextDisabled("Bounds Size: %.3f, %.3f, %.3f",
                    boundsSize.x,
                    boundsSize.y,
                    boundsSize.z);
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
            // Each script attachment is rendered as its own top-level component (Unity-style).
            // No wrapping "Scripts" header — the data still lives in obj.scriptComponent.attachments,
            // but the user sees each script as a separate component with its class name as the title.
            if (obj.hasScriptComponent) {
                int removeScriptAt = -1;
                for (int scriptIndex = 0; scriptIndex < static_cast<int>(obj.scriptComponent.attachments.size()); ++scriptIndex) {
                    ObjectScriptAttachment& script = obj.scriptComponent.attachments[static_cast<std::size_t>(scriptIndex)];
                    ImGui::PushID(scriptIndex);

                    const std::string headerKey = std::string("ScriptInstanceHeader_") + std::to_string(scriptIndex);
                    const std::string header = script.scriptName.empty() ? std::string("(missing script)") : script.scriptName;
                    bool removeScript = false;
                    bool scriptEnabledChanged = false;
                    const bool scriptEnabledBefore = script.enabled;
                    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
                    const bool scriptTreeOpen = RenderRemovableComponentHeader(
                        header.c_str(), headerKey.c_str(),
                        GetComponentIconTexture("component-script.png"),
                        &script.enabled, scriptEnabledChanged, removeScript);

                    if (removeScript) {
                        removeScriptAt = scriptIndex;
                    } else if (scriptEnabledChanged) {
                        const bool scriptEnabledAfter = script.enabled;
                        script.enabled = scriptEnabledBefore;
                        PushUndoState();
                        script.enabled = scriptEnabledAfter;
                        if (scriptsRunning_) {
                            RebuildScriptRuntime();
                        }
                        if (onDirty_) onDirty_();
                    }

                    if (removeScriptAt < 0 && scriptTreeOpen) {
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
                            ImGui::TextDisabled("Not loaded yet. Press Play to build/load scripts.");
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

                if (removeScriptAt >= 0) {
                    PushUndoState();
                    obj.scriptComponent.attachments.erase(obj.scriptComponent.attachments.begin() + removeScriptAt);
                    if (obj.scriptComponent.attachments.empty()) {
                        obj.hasScriptComponent = false;
                        obj.scriptComponent = ScriptComponent{};
                    }
                    if (scriptsRunning_) {
                        RebuildScriptRuntime();
                    }
                    if (onDirty_) onDirty_();
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
                const ImVec4 rigidbodyAccent = ComponentHeaderAccent("Rigidbody");
                if (BeginInspectorSubsection("Body", "RigidbodyBodySection", rigidbodyAccent)) {
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
                    EndInspectorSubsection();
                }

                if (BeginInspectorSubsection("Material", "RigidbodyMaterialSection", rigidbodyAccent, ImGuiTreeNodeFlags_None)) {
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
                    EndInspectorSubsection();
                }

                if (BeginInspectorSubsection("Velocity", "RigidbodyVelocitySection", rigidbodyAccent, ImGuiTreeNodeFlags_None)) {
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
                    EndInspectorSubsection();
                }

                if (BeginInspectorSubsection("Constraints", "RigidbodyConstraintsSection", rigidbodyAccent, ImGuiTreeNodeFlags_None)) {
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
                    EndInspectorSubsection();
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
                const ImVec4 vehicleAccent = ComponentHeaderAccent("Vehicle");
                if (BeginInspectorSubsection("Config", "VehicleConfigSection", vehicleAccent)) {
                std::string configDisplayName = "(none)";
                if (!obj.vehicle.configPath.empty()) {
                    configDisplayName = ProjectAssetDisplayFilename(obj.vehicle.configPath);
                }
                ImGui::TextDisabled("Asset:");
                ImGui::SameLine();
                const bool hasVehicleConfigAsset = !obj.vehicle.configPath.empty();
                const float editProfileButtonWidth = hasVehicleConfigAsset
                    ? (ImGui::CalcTextSize("Edit Profile").x + ImGui::GetStyle().FramePadding.x * 2.0f)
                    : 0.0f;
                const float configRowSpacing = hasVehicleConfigAsset ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
                const float vehicleConfigButtonWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - editProfileButtonWidth - configRowSpacing);
                if (ImGui::Button((configDisplayName + "##selectVehicleConfig").c_str(), ImVec2(vehicleConfigButtonWidth, 0.0f))) {
                    assetPickerMode_ = ProjectAssetPickerMode::AssignVehicleConfig;
                }
                if (hasVehicleConfigAsset) {
                    ImGui::SameLine();
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
                EndInspectorSubsection();
                } // Config header

                if (BeginInspectorSubsection("Input", "VehicleInputSection", vehicleAccent)) {
                const std::string activeProfileId = obj.vehicle.inputProfileId.empty() ? std::string("default_vehicle") : obj.vehicle.inputProfileId;
                std::string profilePreview = activeProfileId;
                const auto profileIt = std::find_if(inputProfiles_.begin(), inputProfiles_.end(), [&](const InputProfile& profile) {
                    return profile.id == activeProfileId;
                });
                if (profileIt != inputProfiles_.end() && !profileIt->displayName.empty()) {
                    profilePreview = profileIt->displayName;
                }
                if (ImGui::BeginCombo("Profile", profilePreview.c_str())) {
                    for (const InputProfile& profile : inputProfiles_) {
                        const bool selected = profile.id == activeProfileId;
                        const std::string label = (profile.displayName.empty() ? profile.id : profile.displayName) + "##vehicleInputProfile_" + profile.id;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            PushUndoState();
                            obj.vehicle.inputProfileId = profile.id;
                            if (onDirty_) onDirty_();
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                int preferenceIndex = static_cast<int>(obj.vehicle.preferredInputDevice);
                const char* preferenceLabels[] = {"Any", "Keyboard", "Gamepad", "Wheel", "Specific Device"};
                if (ImGui::Combo("Preferred Device", &preferenceIndex, preferenceLabels, IM_ARRAYSIZE(preferenceLabels))) {
                    PushUndoState();
                    obj.vehicle.preferredInputDevice = static_cast<InputDevicePreference>(preferenceIndex);
                    if (obj.vehicle.preferredInputDevice != InputDevicePreference::Specific) {
                        obj.vehicle.preferredInputDeviceId.clear();
                    }
                    if (onDirty_) onDirty_();
                }

                if (obj.vehicle.preferredInputDevice == InputDevicePreference::Specific) {
                    std::string devicePreview = obj.vehicle.preferredInputDeviceId.empty() ? std::string("(none)") : obj.vehicle.preferredInputDeviceId;
                    if (inputManager_ != nullptr) {
                        const auto& devices = inputManager_->GetConnectedDevices();
                        if (ImGui::BeginCombo("Specific Device", devicePreview.c_str())) {
                            if (ImGui::Selectable("(none)", obj.vehicle.preferredInputDeviceId.empty())) {
                                PushUndoState();
                                obj.vehicle.preferredInputDeviceId.clear();
                                if (onDirty_) onDirty_();
                            }
                            for (const InputDeviceInfo& device : devices) {
                                const bool selected = device.runtimeId == obj.vehicle.preferredInputDeviceId;
                                const std::string label = device.displayName + "##vehicleSpecificDevice_" + device.runtimeId;
                                if (ImGui::Selectable(label.c_str(), selected)) {
                                    PushUndoState();
                                    obj.vehicle.preferredInputDeviceId = device.runtimeId;
                                    if (onDirty_) onDirty_();
                                }
                                if (selected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                    } else {
                        ImGui::TextDisabled("Input manager is not connected.");
                    }
                }
                ImGui::TextDisabled("Runtime uses the selected profile with the preferred device fallback rules.");
                EndInspectorSubsection();
                } // Input header

                if (BeginInspectorSubsection("Chassis", "VehicleChassisSection", vehicleAccent)) {
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

                EndInspectorSubsection();
                } // Chassis header

                raceman::physics::VehicleConfig loadedConfig;
                if (TryLoadVehicleConfigForPath(obj.vehicle.configPath, loadedConfig)) {
                    if (scriptsRunning_) {
                        const auto runtimeVehicleIt = std::find_if(runtimeVehicles_.begin(), runtimeVehicles_.end(),
                            [&](const RuntimeVehicleInstance& runtimeVehicle) {
                                return runtimeVehicle.objectIndex == selectedIndex_ && runtimeVehicle.instance != nullptr;
                            });
                        if (runtimeVehicleIt != runtimeVehicles_.end()) {
                            if (BeginInspectorSubsection("Runtime Debug", "VehicleRuntimeDebugSection", vehicleAccent, ImGuiTreeNodeFlags_None)) {
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
                            EndInspectorSubsection();
                            } // Runtime Debug header
                        }
                    }
                    if (BeginInspectorSubsection("Wheels", "VehicleWheelsSection", vehicleAccent)) {
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
                    EndInspectorSubsection();
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
                    colliderEditMode_ = false;
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
                        if (newColliderType == SceneColliderType::Mesh || newColliderType == SceneColliderType::Plane) {
                            colliderEditMode_ = false;
                        }
                        if (onDirty_) onDirty_();
                        if (newColliderType == SceneColliderType::Mesh) {
                            StartMeshColliderAutoBake(obj, "Baking mesh collider cache");
                        }
                    }

                    const SceneColliderType activeColliderType = GetActiveColliderType(obj);
                    if (activeColliderType == SceneColliderType::Mesh) {
                        colliderEditMode_ = false;
                    } else {
                        const bool supportedColliderEdit =
                            activeColliderType == SceneColliderType::Box ||
                            activeColliderType == SceneColliderType::Sphere ||
                            activeColliderType == SceneColliderType::Capsule;
                        ImGui::BeginDisabled(!supportedColliderEdit);
                        ImGui::PushStyleColor(ImGuiCol_Button,
                            colliderEditMode_ ? ImVec4(0.22f, 0.45f, 0.88f, 0.92f)
                                              : ImVec4(0.13f, 0.15f, 0.19f, 0.82f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.52f, 0.95f, 0.95f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.18f, 0.38f, 0.80f, 1.00f));
                        if (ImGui::Button(colliderEditMode_ ? "Editing Collider" : "Edit Collider")) {
                            colliderEditMode_ = !colliderEditMode_;
                        }
                        ImGui::PopStyleColor(3);
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Toggle Scene View handles for collider center and size.");
                        }
                    }
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
                            StartMeshColliderAutoBake(obj, "Baking mesh collider cache");
                        }

                        ImGui::TextDisabled("Build Quality: Quality (fixed)");

                        const bool hasMeshSource = obj.hasMeshFilter && !obj.meshFilter.sourcePath.empty();
                        ImGui::TextDisabled("%s", hasMeshSource ? obj.meshFilter.sourcePath.c_str() : "Mesh Collider requires a Mesh Filter source.");
                        if (hasMeshSource) {
                            PhysicsColliderDesc collider;
                            collider.type = PhysicsColliderType::Mesh;
                            collider.meshAssetPath = obj.meshFilter.sourcePath;
                            collider.meshName = obj.meshFilter.meshName;
                            collider.meshIndex = obj.meshFilter.meshIndex;
                            collider.meshPivotOffset = obj.meshFilter.pivotOffset;
                            collider.meshMode = obj.meshCollider.mode;
                            collider.meshBuildQuality = obj.meshCollider.buildQuality;
                            const CollisionShapeCacheInfo cacheInfo = PhysicsWorld::GetCollisionShapeCacheInfo(collider);
                            ImGui::TextDisabled("Cache:");
                            ImGui::SameLine();
                            if (cacheInfo.status == CollisionShapeCacheStatus::Ready) {
                                ImGui::TextColored(ImVec4(0.48f, 0.82f, 0.58f, 1.0f), "Ready");
                            } else if (cacheInfo.status == CollisionShapeCacheStatus::Stale) {
                                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "Stale");
                            } else if (cacheInfo.status == CollisionShapeCacheStatus::Failed) {
                                ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.35f, 1.0f), "Failed");
                            } else {
                                ImGui::TextDisabled("Missing");
                            }
                            ImGui::SameLine();
                            ImGui::TextDisabled("Triangles: %llu", static_cast<unsigned long long>(cacheInfo.triangleCount));
                            ImGui::BeginDisabled(collisionBake_.active);
                            if (ImGui::Button("Bake Now##MeshColliderBake")) {
                                std::string label = obj.meshFilter.meshName.empty() ? obj.meshFilter.sourcePath : obj.meshFilter.meshName;
                                std::vector<std::pair<PhysicsColliderDesc, std::string>> jobs;
                                jobs.emplace_back(collider, std::move(label));
                                StartCollisionBake(std::move(jobs), "Baking mesh collider cache");
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            if (ImGui::Button("Open Model Importer##MeshColliderImporter")) {
                                selectedProjectFile_ = obj.meshFilter.sourcePath;
                                selectedModelChildMeshIndex_ = obj.meshFilter.meshIndex;
                                RefreshModelAssetInspectorCache(true);
                            }
                            RenderCollisionBakeInlineStatus();
                        }
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
                    auto setFollowTargetPreservingCameraPosition = [&](const std::string& targetId) {
                        const int cameraIndex = selectedIndex_;
                        if (cameraIndex < 0 || cameraIndex >= static_cast<int>(objects_.size())) {
                            obj.cinemachine.followTargetId = targetId;
                            return;
                        }

                        auto findById = [this](const std::string& id) { return FindObjectIndexById(id); };
                        auto getMatrix = [this](int idx) { return GetObjectWorldMatrix(idx); };
                        glm::mat4 currentWorld = GetObjectWorldMatrix(cameraIndex);
                        glm::mat4 drivenWorld(1.0f);
                        if (ComputeCinemachineDesiredWorldMatrix(obj.cinemachine, cameraIndex, objects_, findById, getMatrix, drivenWorld)) {
                            currentWorld = drivenWorld;
                        }

                        const glm::vec3 currentWorldPosition = glm::vec3(currentWorld[3]);
                        const int targetIndex = targetId.empty() ? -1 : FindObjectIndexById(targetId);
                        if (targetIndex >= 0) {
                            obj.transform.position = CinemachineWorldPositionToOffset(GetObjectWorldMatrix(targetIndex), currentWorldPosition);
                        } else {
                            obj.transform.position = currentWorldPosition;
                        }
                        obj.cinemachine.followOffset = obj.transform.position;
                        obj.cinemachine.followTargetId = targetId;
                    };
                    if (ImGui::BeginCombo("Follow Target##cinemachineFollow", followPreview.c_str())) {
                        if (ImGui::Selectable("(none)", followTargetIndex < 0)) {
                            PushUndoState();
                            setFollowTargetPreservingCameraPosition("");
                            if (onDirty_) onDirty_();
                        }
                        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                            if (objects_[i].id == obj.id) continue;
                            const std::string itemName = objects_[i].name.empty() ? "(unnamed)" : objects_[i].name;
                            if (ImGui::Selectable((itemName + "##cinemachineFollowObj_" + objects_[i].id).c_str(), i == followTargetIndex)) {
                                PushUndoState();
                                setFollowTargetPreservingCameraPosition(objects_[i].id);
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
                                    setFollowTargetPreservingCameraPosition(objects_[droppedIndex].id);
                                    if (onDirty_) onDirty_();
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

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
            // ---- Audio Listener ----
            if (currentComponentToRender == SceneInspectorComponentType::AudioListener) {
            bool removeAudioListener = false;
            bool audioListenerOpen = false;
            bool audioListenerEnabledChanged = false;
            bool audioListenerHeaderActive = false;
            bool audioListenerHeaderToggledOpen = false;
            const bool audioListenerEnabledBefore = obj.audioListener.enabled;
            if (obj.hasAudioListener) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::AudioListener;
                const std::string audioListenerComponentKey = prepareComponentOpenState(SceneInspectorComponentType::AudioListener);
                audioListenerOpen = RenderRemovableComponentHeader("Audio Listener", "AudioListenerHeader", GetComponentIconTexture("component-audio-listener.png"), &obj.audioListener.enabled, audioListenerEnabledChanged, removeAudioListener, &componentType, &reorderDraggedType, &reorderTargetType, &audioListenerHeaderActive, &audioListenerHeaderToggledOpen);
                finishComponentHeaderState(audioListenerComponentKey, SceneInspectorComponentType::AudioListener, audioListenerHeaderActive, audioListenerHeaderToggledOpen, audioListenerOpen);
            }
            if (removeAudioListener) {
                PushUndoState();
                obj.hasAudioListener = false;
                obj.audioListener = AudioListenerComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasAudioListener && audioListenerEnabledChanged) {
                const bool after = obj.audioListener.enabled;
                obj.audioListener.enabled = audioListenerEnabledBefore;
                PushUndoState();
                obj.audioListener.enabled = after;
                if (onDirty_) onDirty_();
            }
            if (obj.hasAudioListener && audioListenerOpen) {
                ImGui::TextDisabled("Receives spatial audio from the scene.");
            }
            }
            // ---- Audio Source ----
            if (currentComponentToRender == SceneInspectorComponentType::AudioSource) {
            bool removeAudioSource = false;
            bool audioSourceOpen = false;
            bool audioSourceEnabledChanged = false;
            bool audioSourceHeaderActive = false;
            bool audioSourceHeaderToggledOpen = false;
            const bool audioSourceEnabledBefore = obj.audioSource.enabled;
            if (obj.hasAudioSource) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::AudioSource;
                const std::string audioSourceComponentKey = prepareComponentOpenState(SceneInspectorComponentType::AudioSource);
                audioSourceOpen = RenderRemovableComponentHeader("Audio Source", "AudioSourceHeader", GetComponentIconTexture("component-audio-source.png"), &obj.audioSource.enabled, audioSourceEnabledChanged, removeAudioSource, &componentType, &reorderDraggedType, &reorderTargetType, &audioSourceHeaderActive, &audioSourceHeaderToggledOpen);
                finishComponentHeaderState(audioSourceComponentKey, SceneInspectorComponentType::AudioSource, audioSourceHeaderActive, audioSourceHeaderToggledOpen, audioSourceOpen);
            }
            if (removeAudioSource) {
                PushUndoState();
                obj.hasAudioSource = false;
                obj.audioSource = AudioSourceComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasAudioSource && audioSourceEnabledChanged) {
                const bool after = obj.audioSource.enabled;
                obj.audioSource.enabled = audioSourceEnabledBefore;
                PushUndoState();
                obj.audioSource.enabled = after;
                if (onDirty_) onDirty_();
            }
            if (obj.hasAudioSource && audioSourceOpen) {
                // Clip path picker
                char clipBuf[512]{};
                std::snprintf(clipBuf, sizeof(clipBuf), "%s", obj.audioSource.clipPath.c_str());
                ImGui::InputText("Clip##AudioSource", clipBuf, sizeof(clipBuf), ImGuiInputTextFlags_ReadOnly);
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                        const char* dragPath = static_cast<const char*>(payload->Data);
                        if (dragPath && IsAudioAssetPath(dragPath)) {
                            PushUndoState();
                            obj.audioSource.clipPath = dragPath;
                            if (onDirty_) onDirty_();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                float volume = obj.audioSource.volume;
                if (ImGui::DragFloat("Volume##AudioSource", &volume, 0.01f, 0.0f, 4.0f)) {
                    beginInspectorContinuousEdit();
                    obj.audioSource.volume = (std::max)(0.0f, volume);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float pitch = obj.audioSource.pitch;
                if (ImGui::DragFloat("Pitch##AudioSource", &pitch, 0.01f, 0.01f, 4.0f)) {
                    beginInspectorContinuousEdit();
                    obj.audioSource.pitch = (std::max)(0.01f, pitch);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                bool loop = obj.audioSource.loop;
                if (ImGui::Checkbox("Loop##AudioSource", &loop)) {
                    PushUndoState();
                    obj.audioSource.loop = loop;
                    if (onDirty_) onDirty_();
                }
                bool playOnAwake = obj.audioSource.playOnAwake;
                if (ImGui::Checkbox("Play On Awake##AudioSource", &playOnAwake)) {
                    PushUndoState();
                    obj.audioSource.playOnAwake = playOnAwake;
                    if (onDirty_) onDirty_();
                }

                float spatialBlend = obj.audioSource.spatialBlend;
                if (ImGui::SliderFloat("Spatial Blend##AudioSource", &spatialBlend, 0.0f, 1.0f)) {
                    beginInspectorContinuousEdit();
                    obj.audioSource.spatialBlend = spatialBlend;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float minDist = obj.audioSource.minDistance;
                if (ImGui::DragFloat("Min Distance##AudioSource", &minDist, 0.1f, 0.01f, 10000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.audioSource.minDistance = (std::max)(0.01f, minDist);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                float maxDist = obj.audioSource.maxDistance;
                if (ImGui::DragFloat("Max Distance##AudioSource", &maxDist, 0.5f, 0.01f, 10000.0f)) {
                    beginInspectorContinuousEdit();
                    obj.audioSource.maxDistance = (std::max)(obj.audioSource.minDistance + 0.01f, maxDist);
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();
            }
            }
            // ---- Vehicle Sound ----
            if (currentComponentToRender == SceneInspectorComponentType::VehicleSound) {
            bool removeVehicleSound = false;
            bool vehicleSoundOpen = false;
            bool vehicleSoundEnabledChanged = false;
            bool vehicleSoundHeaderActive = false;
            bool vehicleSoundHeaderToggledOpen = false;
            const bool vehicleSoundEnabledBefore = obj.vehicleSound.enabled;
            if (obj.hasVehicleSound) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::VehicleSound;
                const std::string vehicleSoundComponentKey = prepareComponentOpenState(SceneInspectorComponentType::VehicleSound);
                vehicleSoundOpen = RenderRemovableComponentHeader("Vehicle Sound", "VehicleSoundHeader", GetComponentIconTexture("component-vehicle-sound.png"), &obj.vehicleSound.enabled, vehicleSoundEnabledChanged, removeVehicleSound, &componentType, &reorderDraggedType, &reorderTargetType, &vehicleSoundHeaderActive, &vehicleSoundHeaderToggledOpen);
                finishComponentHeaderState(vehicleSoundComponentKey, SceneInspectorComponentType::VehicleSound, vehicleSoundHeaderActive, vehicleSoundHeaderToggledOpen, vehicleSoundOpen);
            }
            if (removeVehicleSound) {
                PushUndoState();
                obj.hasVehicleSound = false;
                obj.vehicleSound = VehicleSoundComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasVehicleSound && vehicleSoundEnabledChanged) {
                const bool after = obj.vehicleSound.enabled;
                obj.vehicleSound.enabled = vehicleSoundEnabledBefore;
                PushUndoState();
                obj.vehicleSound.enabled = after;
                if (onDirty_) onDirty_();
            }
            if (obj.hasVehicleSound && vehicleSoundOpen) {
                const float editBtnWidth = 50.0f;
                char profileBuf[512]{};
                std::snprintf(profileBuf, sizeof(profileBuf), "%s", obj.vehicleSound.profilePath.c_str());
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - editBtnWidth - ImGui::GetStyle().ItemSpacing.x);
                ImGui::InputText("##VehicleSoundProfile", profileBuf, sizeof(profileBuf), ImGuiInputTextFlags_ReadOnly);
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                        const char* dragPath = static_cast<const char*>(payload->Data);
                        if (dragPath && IsVehicleSoundAssetPath(dragPath)) {
                            PushUndoState();
                            obj.vehicleSound.profilePath = dragPath;
                            if (onDirty_) onDirty_();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(obj.vehicleSound.profilePath.empty());
                if (ImGui::Button("Edit##VS", ImVec2(editBtnWidth, 0.0f))) {
                    OpenVehicleSoundEditor(obj.vehicleSound.profilePath);
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled("Profile");
            }
            }

            // ---- Track Generator ----
            if (currentComponentToRender == SceneInspectorComponentType::TrackGenerator) {
            bool removeTrackGenerator = false;
            bool trackGeneratorOpen = false;
            bool trackGeneratorEnabledChanged = false;
            bool trackGeneratorHeaderActive = false;
            bool trackGeneratorHeaderToggledOpen = false;
            const bool trackGeneratorEnabledBefore = obj.trackGenerator.enabled;
            if (obj.hasTrackGenerator) {
                SceneInspectorComponentType componentType = SceneInspectorComponentType::TrackGenerator;
                const std::string trackGeneratorComponentKey = prepareComponentOpenState(SceneInspectorComponentType::TrackGenerator);
                trackGeneratorOpen = RenderRemovableComponentHeader("Track Generator", "TrackGeneratorHeader", GetComponentIconTexture("asset-track.png"), &obj.trackGenerator.enabled, trackGeneratorEnabledChanged, removeTrackGenerator, &componentType, &reorderDraggedType, &reorderTargetType, &trackGeneratorHeaderActive, &trackGeneratorHeaderToggledOpen);
                finishComponentHeaderState(trackGeneratorComponentKey, SceneInspectorComponentType::TrackGenerator, trackGeneratorHeaderActive, trackGeneratorHeaderToggledOpen, trackGeneratorOpen);
            }
            if (removeTrackGenerator) {
                PushUndoState();
                obj.hasTrackGenerator = false;
                obj.trackGenerator = TrackGeneratorComponent{};
                if (onDirty_) onDirty_();
            } else if (obj.hasTrackGenerator && trackGeneratorEnabledChanged) {
                const bool after = obj.trackGenerator.enabled;
                obj.trackGenerator.enabled = trackGeneratorEnabledBefore;
                PushUndoState();
                obj.trackGenerator.enabled = after;
                if (onDirty_) onDirty_();
            }
            if (obj.hasTrackGenerator && trackGeneratorOpen) {
                char sourceBuf[512]{};
                std::snprintf(sourceBuf, sizeof(sourceBuf), "%s", obj.trackGenerator.trackSourcePath.c_str());
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("Source##TrackGeneratorSource", sourceBuf, sizeof(sourceBuf), ImGuiInputTextFlags_ReadOnly);
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                        const char* dragPath = static_cast<const char*>(payload->Data);
                        if (dragPath && IsTrackAssetPath(dragPath)) {
                            PushUndoState();
                            obj.trackGenerator.trackSourcePath = dragPath;
                            if (onDirty_) onDirty_();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::Button("Open Generator", ImVec2(-1.0f, 0.0f))) {
                    OpenTrackGenerator(obj.trackGenerator.trackSourcePath);
                }
                if (ImGui::Button("Use Current Generator Source", ImVec2(-1.0f, 0.0f))) {
                    PushUndoState();
                    obj.trackGenerator.trackSourcePath = inspectedTrackPath_;
                    if (onDirty_) onDirty_();
                }
                ImGui::TextDisabled("Road Object: %s", obj.trackGenerator.roadObjectId.empty() ? "none" : obj.trackGenerator.roadObjectId.c_str());
                ImGui::TextDisabled("Shoulder Object: %s", obj.trackGenerator.shoulderObjectId.empty() ? "none" : obj.trackGenerator.shoulderObjectId.c_str());
            }
            }
            } // end for (currentComponentToRender)
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

            // Inspector-wide drop zone for scripts (Unity-style: drag a script anywhere on the
            // inspector to attach it as a new component).
            {
                ImVec2 dropSize = ImGui::GetContentRegionAvail();
                if (dropSize.y < ImGui::GetTextLineHeightWithSpacing() * 2.0f) {
                    dropSize.y = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
                }
                const ImVec2 dropMin = ImGui::GetCursorScreenPos();
                const ImVec2 dropMax(dropMin.x + dropSize.x, dropMin.y + dropSize.y);
                ImGui::InvisibleButton("##inspectorScriptDropZone", dropSize);
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                        const char* projectPath = static_cast<const char*>(payload->Data);
                        if (projectPath != nullptr && IsScriptAssetPath(projectPath)) {
                            const std::string scriptName = ScriptNameFromAssetPath(projectPath);
                            bool already = false;
                            if (obj.hasScriptComponent) {
                                for (const ObjectScriptAttachment& a : obj.scriptComponent.attachments) {
                                    if (a.scriptName == scriptName) { already = true; break; }
                                }
                            }
                            if (!already && !scriptName.empty()) {
                                AttachScriptToSelected(scriptName, ScriptSourcePathFromAssetPath(projectPath));
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
                bool scriptDragActive = false;
                if (activePayload != nullptr && activePayload->IsDataType(kProjectFilePayload)) {
                    const char* projectPath = static_cast<const char*>(activePayload->Data);
                    scriptDragActive = projectPath != nullptr && IsScriptAssetPath(projectPath);
                }
                if (scriptDragActive) {
                    ImGui::GetWindowDrawList()->AddRect(dropMin, dropMax,
                        ImGui::GetColorU32(ImGuiCol_DragDropTarget), 4.0f, 0, 2.0f);
                    const char* hint = "Drop script here to add as component";
                    const ImVec2 textSize = ImGui::CalcTextSize(hint);
                    const ImVec2 textPos(dropMin.x + (dropSize.x - textSize.x) * 0.5f,
                                         dropMin.y + (dropSize.y - textSize.y) * 0.5f);
                    ImGui::GetWindowDrawList()->AddText(textPos,
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
                }
            }

            // Create-Script modal (triggered from the Add Component > Script > New C++ Script... menu).
            if (showCreateScriptPopup_) {
                ImGui::OpenPopup("Create C++ Script");
                showCreateScriptPopup_ = false;
            }
            if (ImGui::BeginPopupModal("Create C++ Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Create a compiled C++ object script.");
                ImGui::TextDisabled("Scripts are built into ProjectScripts.dll when Play starts.");
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

    EnsureProjectTags();
    if (ImGui::BeginTable("MultiObjectTagLayerRow", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::SetNextItemWidth(-1.0f);
        std::string sharedTag = active.tag.empty() ? "Untagged" : active.tag;
        if (RenderTagCombo("Tag", "multiTag", sharedTag, projectTags_, sharedTag)) {
            PushUndoState();
            forEachSelected([&](SceneObject& object) {
                object.tag = sharedTag.empty() ? "Untagged" : sharedTag;
            });
            markDirty();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        int sharedPhysicsLayer = ClampPhysicsLayerIndex(active.physicsLayer);
        if (RenderPhysicsLayerCombo("Layer", "multiPhysicsLayer", active.physicsLayer, physicsLayerNames_, sharedPhysicsLayer)) {
            PushUndoState();
            forEachSelected([&](SceneObject& object) {
                object.physicsLayer = sharedPhysicsLayer;
            });
            markDirty();
        }
        ImGui::EndTable();
    }

    auto renderSharedEnabledHeader = [&](const char* label, const char* id, const std::string& icon, bool enabled, auto&& setter, auto&& remover) {
        bool changedEnabled = enabled;
        bool enabledChanged = false;
        bool removeRequested = false;
        const bool open = RenderRemovableComponentHeader(label, id, GetComponentIconTexture(icon), &changedEnabled, enabledChanged, removeRequested);
        if (enabledChanged) {
            PushUndoState();
            forEachSelected([&](SceneObject& object) { setter(object, changedEnabled); });
            markDirty();
        }
        if (removeRequested) {
            PushUndoState();
            forEachSelected([&](SceneObject& object) { remover(object); });
            markDirty();
            return false;
        }
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
        {
            auto allSelectedHaveScript = [&](const std::string& name) {
                bool anyMissing = false;
                forEachSelected([&](SceneObject& object) {
                    bool hasIt = false;
                    if (object.hasScriptComponent) {
                        for (const ObjectScriptAttachment& a : object.scriptComponent.attachments) {
                            if (a.scriptName == name) { hasIt = true; break; }
                        }
                    }
                    if (!hasIt) anyMissing = true;
                });
                return !anyMissing;
            };
            anyAvailable = true;
            if (ImGui::BeginMenu("Script")) {
                const std::vector<std::pair<std::string, std::string>> projectScripts = ScanProjectScripts();
                std::vector<std::pair<std::string, std::string>> available;
                available.reserve(projectScripts.size());
                for (const auto& entry : projectScripts) {
                    if (!allSelectedHaveScript(entry.first)) {
                        available.push_back(entry);
                    }
                }
                if (available.empty()) {
                    if (projectScripts.empty()) {
                        ImGui::TextDisabled("No scripts in project.");
                    } else {
                        ImGui::TextDisabled("All project scripts attached.");
                    }
                }
                for (const auto& entry : available) {
                    const std::string label = entry.first + "##multiAddProjectScript";
                    if (ImGui::MenuItem(label.c_str())) {
                        PushUndoState();
                        forEachSelected([&](SceneObject& object) {
                            bool hasIt = false;
                            if (object.hasScriptComponent) {
                                for (const ObjectScriptAttachment& a : object.scriptComponent.attachments) {
                                    if (a.scriptName == entry.first) { hasIt = true; break; }
                                }
                            }
                            if (hasIt) return;
                            object.hasScriptComponent = true;
                            ObjectScriptAttachment attachment;
                            attachment.enabled = true;
                            attachment.scriptName = entry.first;
                            attachment.scriptPath = NormalizeSlashes(entry.second);
                            SyncAttachmentScriptFields(attachment);
                            object.scriptComponent.attachments.push_back(std::move(attachment));
                        });
                        if (scriptsRunning_) RebuildScriptRuntime();
                        markDirty();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", entry.second.c_str());
                    }
                }
                ImGui::EndMenu();
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
        if (renderSharedEnabledHeader("Mesh Filter", "MultiMeshFilterHeader", "component-mesh-filter.png", active.meshFilter.enabled,
            [](SceneObject& object, bool value) { object.meshFilter.enabled = value; },
            [](SceneObject& object) {
                object.hasMeshFilter = false;
                object.meshFilter = MeshFilterComponent{};
            })) {
            ImGui::TextDisabled("Mesh Filter is shared. Mesh replacement uses the active object for now.");
        }
    }

    if (allSelected([](const SceneObject& object) { return object.hasMeshRenderer; })) {
        showedSharedComponent = true;
        if (renderSharedEnabledHeader("Mesh Renderer", "MultiMeshRendererHeader", "component-mesh-renderer.png", active.meshRenderer.enabled,
            [](SceneObject& object, bool value) { object.meshRenderer.enabled = value; },
            [](SceneObject& object) {
                object.hasMeshRenderer = false;
                object.meshRenderer = MeshRendererComponent{};
            })) {
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
        if (renderSharedEnabledHeader("Rigidbody", "MultiRigidbodyHeader", "component-rigidbody.png", active.rigidbody.enabled,
            [](SceneObject& object, bool value) { object.rigidbody.enabled = value; },
            [](SceneObject& object) {
                object.hasRigidbody = false;
                object.rigidbody = RigidbodyComponent{};
            })) {
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
        if (renderSharedEnabledHeader("Vehicle", "MultiVehicleHeader", "component-vehicle.png", active.vehicle.enabled,
            [](SceneObject& object, bool value) { object.vehicle.enabled = value; },
            [](SceneObject& object) {
                object.hasVehicle = false;
                object.vehicle = VehicleComponent{};
            })) {
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
        if (renderSharedEnabledHeader("Collider", "MultiColliderHeader", SceneColliderTypeIcon(activeColliderType), enabled,
            applyColliderEnabled,
            [](SceneObject& object) { SetActiveColliderType(object, SceneColliderType::None); })) {
            SceneColliderType newColliderType = activeColliderType;
            if (RenderColliderTypeCombo("Type##multiCollider", "multiColliderType", activeColliderType, false, sameColliderType ? nullptr : "Mixed", newColliderType) &&
                newColliderType != activeColliderType) {
                PushUndoState();
                forEachSelected([&](SceneObject& object) { SetActiveColliderType(object, newColliderType); });
                markDirty();
                if (newColliderType == SceneColliderType::Mesh) {
                    StartSelectedMeshColliderAutoBake("Baking mesh collider cache");
                }
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
                    StartSelectedMeshColliderAutoBake("Baking mesh collider cache");
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
        if (renderSharedEnabledHeader("Camera", "MultiCameraHeader", "component-camera.png", active.camera.enabled,
            [](SceneObject& object, bool value) { object.camera.enabled = value; },
            [](SceneObject& object) {
                object.hasCamera = false;
                object.camera = CameraComponent{};
            })) {
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
        if (renderSharedEnabledHeader("Light", "MultiLightHeader", "component-light.png", active.light.enabled,
            [](SceneObject& object, bool value) { object.light.enabled = value; },
            [](SceneObject& object) {
                object.hasLight = false;
                object.light = LightComponent{};
            })) {
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
                const char* builtIns[] = {"Plane", "Cube", "Sphere", "Cone", "Capsule"};
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
    if (vehicleConfigEditorFocusRequested_) {
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    if (ImGui::Begin("Vehicle Profile Editor", &showVehicleConfigEditor_, ImGuiWindowFlags_NoCollapse)) {
        if (vehicleConfigEditorFocusRequested_) {
            ImGui::SetWindowFocus();
            vehicleConfigEditorFocusRequested_ = false;
        }
        const double highlightRemaining = vehicleConfigEditorHighlightUntil_ - ImGui::GetTime();
        if (highlightRemaining > 0.0) {
            const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ImGui::GetTime() * 18.0));
            const float alpha = static_cast<float>((std::min)(1.0, highlightRemaining / 1.15)) * pulse;
            ImGui::GetForegroundDrawList()->AddRect(
                ImGui::GetWindowPos(),
                ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.72f, 0.18f, alpha)),
                8.0f,
                0,
                4.0f);
        }
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
            const ImGuiChildFlags childFlags = ImGuiChildFlags_Borders
                | (minHeight <= 0.0f ? ImGuiChildFlags_AutoResizeY : ImGuiChildFlags_None);
            ImGui::BeginChild("##card", ImVec2(0.0f, minHeight), childFlags);
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
            applyDragFloatEdit("Rest Length (m)", (std::string("##") + idPrefix + "_restLength").c_str(), suspension.restLength, 0.01f, 0.001f, 100000.0f);
            applyDragFloatEdit("Spring Rate (N/m)", (std::string("##") + idPrefix + "_springRate").c_str(), suspension.springRate, 10.0f, 0.0f, 1000000.0f);
            applyDragFloatEdit("Bump Stop Rate (N/m)", (std::string("##") + idPrefix + "_bumpStopRate").c_str(), suspension.bumpStopRate, 10.0f, 0.0f, 1000000.0f);
            applyDragFloatEdit("Compression Damping (N*s/m)", (std::string("##") + idPrefix + "_compressionDamping").c_str(), suspension.compressionDamping, 10.0f, 0.0f, 1000000.0f);
            applyDragFloatEdit("Rebound Damping (N*s/m)", (std::string("##") + idPrefix + "_reboundDamping").c_str(), suspension.reboundDamping, 10.0f, 0.0f, 1000000.0f);
            applyDragFloatEdit("Anti-Roll Stiffness (N/m)", (std::string("##") + idPrefix + "_antiRollStiffness").c_str(), suspension.antiRollStiffness, 10.0f, 0.0f, 1000000.0f);
            endCard();
        };
        auto renderMetric = [&](const char* label, const char* value, const char* hint = nullptr) {
            ImGui::BeginGroup();
            ImGui::TextDisabled("%s", label);
            ImGui::TextUnformatted(value);
            if (hint != nullptr && hint[0] != '\0') {
                ImGui::TextDisabled("%s", hint);
            }
            ImGui::EndGroup();
        };
        auto actionButton = [&](const char* label, const ImVec2& size, bool enabled) {
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::Button(label, size);
            ImGui::EndDisabled();
            return clicked && enabled;
        };
        int drivenWheelCount = 0;
        int brakeWheelCount = 0;
        for (const auto& wheel : inspectedVehicleConfig_.wheels) {
            drivenWheelCount += wheel.driven ? 1 : 0;
            brakeWheelCount += wheel.hasBrake ? 1 : 0;
        }
        const std::string wheelCountText = std::to_string(inspectedVehicleConfig_.wheels.size());
        const std::string massText = std::to_string(static_cast<int>(inspectedVehicleConfig_.chassis.mass + 0.5f)) + " kg";
        const std::string redlineText = std::to_string(static_cast<int>(inspectedVehicleConfig_.engine.redlineRPM + 0.5f)) + " rpm";
        const std::string gearCountText = std::to_string(inspectedVehicleConfig_.transmission.gearRatios.size());
        const std::string drivenCountText = std::to_string(drivenWheelCount);
        const std::string brakeCountText = std::to_string(brakeWheelCount);

        beginCard("vehicleProfileHero", inspectedVehicleConfig_.name.empty() ? "Vehicle Profile" : inspectedVehicleConfig_.name.c_str(), "Race setup profile", accentPrimary);
        RenderInspectorWrappedValue("Asset:", inspectedVehicleConfigPath_);
        ImGui::Spacing();
        if (ImGui::BeginTable("VehicleProfileHeroStats", 6, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            renderMetric("WHEELS", wheelCountText.c_str());
            ImGui::TableNextColumn();
            renderMetric("DRIVEN", drivenCountText.c_str());
            ImGui::TableNextColumn();
            renderMetric("BRAKES", brakeCountText.c_str());
            ImGui::TableNextColumn();
            renderMetric("MASS", massText.c_str());
            ImGui::TableNextColumn();
            renderMetric("REDLINE", redlineText.c_str());
            ImGui::TableNextColumn();
            renderMetric("GEARS", gearCountText.c_str(), inspectedVehicleConfig_.transmission.mode == raceman::physics::TransmissionConfig::Mode::Manual ? "Manual" : "Auto");
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (actionButton("Save Profile##vehicleConfigAsset", ImVec2(150.0f, 0.0f), inspectedVehicleConfigLoaded_)) {
            SaveActiveAsset();
        }
        ImGui::SameLine();
        if (actionButton("Undo##vehicleConfigAsset", ImVec2(90.0f, 0.0f), !vehicleConfigUndoStack_.empty())) {
            UndoVehicleConfig();
        }
        ImGui::SameLine();
        if (actionButton("Redo##vehicleConfigAsset", ImVec2(90.0f, 0.0f), !vehicleConfigRedoStack_.empty())) {
            RedoVehicleConfig();
        }
        ImGui::SameLine();
        if (actionButton("Reload##vehicleConfigAsset", ImVec2(120.0f, 0.0f), true)) {
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
        if (!vehicleConfigUndoStack_.empty() || !vehicleConfigRedoStack_.empty() || vehicleConfigEditActive_) {
            ImGui::SameLine();
            ImGui::TextColored(accentSecondary, "Edit history available");
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("No edit history");
        }
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
                applyDragFloatEdit("Mass (kg)", "##vehicleProfileChassisMass", inspectedVehicleConfig_.chassis.mass, 1.0f, 0.001f, 100000.0f);
                applyDragFloatEdit("Yaw Inertia (kg*m^2)", "##vehicleProfileChassisYawInertia", inspectedVehicleConfig_.chassis.yawInertia, 1.0f, 0.001f, 100000.0f);
                applyDragFloatEdit("Roll Inertia (kg*m^2)", "##vehicleProfileChassisRollInertia", inspectedVehicleConfig_.chassis.rollInertia, 1.0f, 0.001f, 100000.0f);
                applyDragFloatEdit("Pitch Inertia (kg*m^2)", "##vehicleProfileChassisPitchInertia", inspectedVehicleConfig_.chassis.pitchInertia, 1.0f, 0.001f, 100000.0f);
                applyDragFloat3Edit("Center of Mass (m)", "##vehicleProfileChassisCenterOfMass", inspectedVehicleConfig_.chassis.centerOfMassOffset, 0.01f);
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
                applyDragFloatEdit("Torque Split (0-1)", "##vehicleProfileDiffTorqueSplit", inspectedVehicleConfig_.differential.torqueSplit, 0.01f, 0.0f, 1.0f);
                const float split = std::clamp(inspectedVehicleConfig_.differential.torqueSplit, 0.0f, 1.0f);
                const std::string splitLabel = std::to_string(static_cast<int>((1.0f - split) * 100.0f + 0.5f))
                    + "% rear / " + std::to_string(static_cast<int>(split * 100.0f + 0.5f)) + "% front";
                ImGui::ProgressBar(split, ImVec2(-1.0f, 0.0f), splitLabel.c_str());
                applyDragFloatEdit("Locking Coefficient (0-1)", "##vehicleProfileDiffLockingCoefficient", inspectedVehicleConfig_.differential.lockingCoefficient, 0.01f, 0.0f, 1.0f);
                applyCheckboxEdit("Limited Slip##vehicleProfileDiffLimitedSlip", inspectedVehicleConfig_.differential.limitedSlip);
                endCard();

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Powertrain")) {
                if (ImGui::BeginTable("VehiclePowertrainColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    beginCard("vehicleProfileEngineCard", "Engine", "RPM and torque delivery", accentPrimary);
                    applyDragFloatEdit("Idle RPM (rpm)", "##vehicleProfileEngineIdleRpm", inspectedVehicleConfig_.engine.idleRPM, 10.0f, 0.0f, 100000.0f);
                    applyDragFloatEdit("Redline RPM (rpm)", "##vehicleProfileEngineRedlineRpm", inspectedVehicleConfig_.engine.redlineRPM, 10.0f, 0.0f, 100000.0f);
                    applyDragFloatEdit("Stall RPM (rpm)", "##vehicleProfileEngineStallRpm", inspectedVehicleConfig_.engine.stallRPM, 10.0f, 0.0f, 100000.0f);
                    applyDragFloatEdit("Inertia (kg*m^2)", "##vehicleProfileEngineInertia", inspectedVehicleConfig_.engine.inertia, 0.01f, 0.0f, 1000.0f);
                    ImGui::Spacing();
                    ImGui::TextDisabled("Torque Curve");
                    if (ImGui::Button("Add Torque Point##vehicleProfileTorquePoint")) {
                        PushVehicleConfigUndoState();
                        vehicleConfigEditActive_ = false;
                        inspectedVehicleConfig_.engine.torqueCurve.push_back({1000.0f, 100.0f});
                    }
                    if (inspectedVehicleConfig_.engine.torqueCurve.empty()) {
                        ImGui::TextDisabled("No torque points. Add at least one point to define engine output.");
                    }
                    if (ImGui::BeginTable("VehicleTorqueCurveTable", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("RPM (rpm)");
                        ImGui::TableSetupColumn("Torque (Nm)");
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 32.0f);
                        ImGui::TableHeadersRow();
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
                    applyDragFloatEdit("Final Drive Ratio (ratio)", "##vehicleProfileTransmissionFinalDrive", inspectedVehicleConfig_.transmission.finalDriveRatio, 0.01f, -1000.0f, 1000.0f);
                    applyDragFloatEdit("Reverse Ratio (ratio)", "##vehicleProfileTransmissionReverseRatio", inspectedVehicleConfig_.transmission.reverseRatio, 0.01f, -1000.0f, 1000.0f);
                    applyDragFloatEdit("Shift Time (s)", "##vehicleProfileTransmissionShiftTime", inspectedVehicleConfig_.transmission.shiftTime, 0.01f, 0.0f, 1000.0f);
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
                    if (inspectedVehicleConfig_.transmission.gearRatios.empty()) {
                        ImGui::TextDisabled("No forward gears. Add a gear before using this profile.");
                    }
                    if (ImGui::BeginTable("VehicleGearRatioTable", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("Gear", ImGuiTableColumnFlags_WidthFixed, 54.0f);
                        ImGui::TableSetupColumn("Ratio (unitless)");
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 32.0f);
                        ImGui::TableHeadersRow();
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
                beginCard("vehicleProfileWheelSummary", "Wheel Layout", "Mounts, tire grip and brake roles", accentSecondary);
                if (ImGui::BeginTable("VehicleWheelSummaryStats", 4, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    renderMetric("TOTAL", wheelCountText.c_str());
                    ImGui::TableNextColumn();
                    renderMetric("DRIVEN", drivenCountText.c_str());
                    ImGui::TableNextColumn();
                    renderMetric("BRAKES", brakeCountText.c_str());
                    ImGui::TableNextColumn();
                    renderMetric("STEERABLE", std::to_string(static_cast<int>(std::count_if(
                        inspectedVehicleConfig_.wheels.begin(),
                        inspectedVehicleConfig_.wheels.end(),
                        [](const raceman::physics::WheelConfig& wheel) { return wheel.maxSteerAngle != 0.0f; }))).c_str());
                    ImGui::EndTable();
                }
                if (ImGui::Button("Add Wheel##vehicleProfileWheelAdd", ImVec2(130.0f, 0.0f))) {
                    PushVehicleConfigUndoState();
                    vehicleConfigEditActive_ = false;
                    inspectedVehicleConfig_.wheels.push_back({});
                }
                endCard();

                if (inspectedVehicleConfig_.wheels.empty()) {
                    beginCard("vehicleProfileNoWheels", "No Wheels", "Add a wheel to make this profile drivable", accentPrimary);
                    ImGui::TextWrapped("Vehicle profiles need wheel entries with mount positions, tire values, brakes and driven flags.");
                    endCard();
                }
                ImGui::Spacing();
                for (int wheelIndex = 0; wheelIndex < static_cast<int>(inspectedVehicleConfig_.wheels.size()); ++wheelIndex) {
                    raceman::physics::WheelConfig& wheel = inspectedVehicleConfig_.wheels[static_cast<std::size_t>(wheelIndex)];
                    ImGui::PushID(wheelIndex);
                    const bool frontAxle = wheel.mountPosition.y >= 0.0f;
                    const ImVec4 wheelAccent = frontAxle ? accentPrimary : accentSecondary;
                    const std::string title = wheel.name.empty() ? ("Wheel " + std::to_string(wheelIndex + 1)) : wheel.name;
                    const std::string subtitle = std::string(frontAxle ? "Front axle" : "Rear axle")
                        + (wheel.driven ? " | Driven" : "")
                        + (wheel.hasBrake ? " | Brake" : "");
                    const std::string header = title + "  -  " + subtitle + "##wheelHeader";
                    ImGui::SetNextItemOpen(wheelIndex < 4, ImGuiCond_Once);
                    if (!ImGui::CollapsingHeader(header.c_str())) {
                        ImGui::PopID();
                        continue;
                    }
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
                    applyDragFloat3Edit("Mount Position (m)", "##vehicleProfileWheelMountPosition", wheel.mountPosition, 0.01f);

                    ImGui::Spacing();
                    ImGui::TextDisabled("Geometry");
                    renderWheelScalarPair("Radius (m)", "##vehicleProfileWheelRadius", wheel.radius, 0.01f, 0.001f, 1000.0f,
                                          "Width (m)", "##vehicleProfileWheelWidth", wheel.width, 0.01f, 0.001f, 1000.0f);

                    ImGui::TextDisabled("Mass");
                    renderWheelScalarPair("Mass (kg)", "##vehicleProfileWheelMass", wheel.mass, 0.1f, 0.001f, 10000.0f,
                                          "Inertia (kg*m^2)", "##vehicleProfileWheelInertia", wheel.inertia, 0.01f, 0.001f, 10000.0f);

                    ImGui::TextDisabled("Alignment");
                    if (ImGui::BeginTable("##wheelAlignmentRow", 3, ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableNextColumn();
                        applyDragFloatEdit("Steer (rad)", "##vehicleProfileWheelMaxSteerAngle", wheel.maxSteerAngle, 0.01f, -10.0f, 10.0f);
                        ImGui::TableNextColumn();
                        applyDragFloatEdit("Camber (deg)", "##vehicleProfileWheelCamber", wheel.camber, 0.01f, -10.0f, 10.0f);
                        ImGui::TableNextColumn();
                        applyDragFloatEdit("Toe (deg)", "##vehicleProfileWheelToe", wheel.toe, 0.01f, -10.0f, 10.0f);
                        ImGui::EndTable();
                    }

                    ImGui::TextDisabled("Tire");
                    renderWheelScalarPair("Grip (x normal)", "##vehicleProfileWheelGripFactor", wheel.gripFactor, 0.01f, 0.0f, 1000.0f,
                                          "Brake Torque (Nm)", "##vehicleProfileWheelMaxBrakingTorque", wheel.maxBrakingTorque, 10.0f, 0.0f, 1000000.0f);
                    renderWheelScalarPair("Long Stiffness (N)", "##vehicleProfileWheelLongitudinalStiffness", wheel.longitudinalStiffness, 10.0f, 0.0f, 1000000.0f,
                                          "Lat Stiffness (N/rad)", "##vehicleProfileWheelLateralStiffness", wheel.lateralStiffness, 10.0f, 0.0f, 1000000.0f);

                    if (ImGui::BeginTable("##wheelFlagsRow", 3, ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableNextColumn();
                        applyCheckboxEdit("Driven##vehicleProfileWheelDriven", wheel.driven);
                        ImGui::TableNextColumn();
                        applyCheckboxEdit("Has Brake##vehicleProfileWheelHasBrake", wheel.hasBrake);
                        ImGui::TableNextColumn();
                        ImGui::Dummy(ImVec2(0.0f, 0.0f));
                        ImGui::EndTable();
                    }
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.10f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.14f, 0.10f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.18f, 0.12f, 1.0f));
                    if (ImGui::Button("Remove Wheel##vehicleProfileWheel")) {
                        ImGui::PopStyleColor(3);
                        PushVehicleConfigUndoState();
                        vehicleConfigEditActive_ = false;
                        inspectedVehicleConfig_.wheels.erase(inspectedVehicleConfig_.wheels.begin() + wheelIndex);
                        endCard();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopStyleColor(3);
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

void SceneEditor::RenderVehicleSoundEditorWindow() {
    if (!showVehicleSoundEditor_) {
        return;
    }
    if (inspectedVehicleSoundPath_.empty()) {
        showVehicleSoundEditor_ = false;
        return;
    }

    if (!inspectedVehicleSoundLoaded_) {
        inspectedVehicleSoundError_.clear();
        try {
            inspectedVehicleSound_ = VehicleSoundProfileLoader::loadFromFile(
                ProjectAssetPathToAbsolute(inspectedVehicleSoundPath_).string());
            inspectedVehicleSoundLoaded_ = true;
        } catch (const std::exception& ex) {
            inspectedVehicleSoundLoaded_ = false;
            inspectedVehicleSoundError_ = ex.what();
        }
    }

    ImGui::SetNextWindowSize(ImVec2(680.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (vehicleSoundEditorFocusRequested_) {
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    if (ImGui::Begin("Vehicle Sound Editor", &showVehicleSoundEditor_, ImGuiWindowFlags_NoCollapse)) {
        if (vehicleSoundEditorFocusRequested_) {
            ImGui::SetWindowFocus();
            vehicleSoundEditorFocusRequested_ = false;
        }
        const double highlightRemaining = vehicleSoundEditorHighlightUntil_ - ImGui::GetTime();
        if (highlightRemaining > 0.0) {
            const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ImGui::GetTime() * 18.0));
            const float alpha = static_cast<float>((std::min)(1.0, highlightRemaining / 1.15)) * pulse;
            ImGui::GetForegroundDrawList()->AddRect(
                ImGui::GetWindowPos(),
                ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.72f, 0.18f, alpha)),
                8.0f,
                0,
                4.0f);
        }
        vehicleSoundEditorHovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        vehicleSoundEditorFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        auto beginEdit = [&]() {
            if (!vehicleSoundEditActive_) {
                PushVehicleSoundUndoState();
                vehicleSoundEditActive_ = true;
            }
        };
        auto endEdit = [&]() {
            if (ImGui::IsItemDeactivated()) vehicleSoundEditActive_ = false;
        };

        if (!inspectedVehicleSoundError_.empty()) {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Error: %s", inspectedVehicleSoundError_.c_str());
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }
        if (!inspectedVehicleSoundLoaded_) {
            ImGui::TextDisabled("Loading...");
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }

        VehicleSoundProfile& p = inspectedVehicleSound_;

        // Header row: name + save button
        {
            char nameBuf[256]{};
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", p.name.c_str());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f);
            if (ImGui::InputText("##SoundName", nameBuf, sizeof(nameBuf))) {
                beginEdit();
                p.name = nameBuf;
            }
            endEdit();
            ImGui::SameLine();
            if (ImGui::Button("Save", ImVec2(90.0f, 0.0f))) {
                std::string err;
                if (VehicleSoundProfileLoader::saveToFile(
                        ProjectAssetPathToAbsolute(inspectedVehicleSoundPath_).string(), p, &err)) {
                    inspectedVehicleSoundLoaded_ = false;
                    if (console_) console_->AddLog("Saved vehicle sound profile: " + inspectedVehicleSoundPath_);
                } else if (console_) {
                    console_->AddError(err.empty() ? ("Failed to save: " + inspectedVehicleSoundPath_) : err);
                }
            }
        }
        ImGui::Separator();

        // Master settings
        if (ImGui::CollapsingHeader("Master Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            float mv = p.masterVolume;
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::DragFloat("Master Volume##VS", &mv, 0.01f, 0.0f, 4.0f)) { beginEdit(); p.masterVolume = (std::max)(0.0f, mv); }
            endEdit();

            float sb = p.spatialBlend;
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::SliderFloat("Spatial Blend##VS", &sb, 0.0f, 1.0f)) { beginEdit(); p.spatialBlend = sb; }
            endEdit();
            ImGui::SameLine(); ImGui::TextDisabled("(0=2D  1=3D)");

            float minD = p.minDistance;
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::DragFloat("Min Distance##VS", &minD, 0.1f, 0.01f, 1000.0f)) { beginEdit(); p.minDistance = (std::max)(0.01f, minD); }
            endEdit();

            float maxD = p.maxDistance;
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::DragFloat("Max Distance##VS", &maxD, 0.5f, p.minDistance + 0.01f, 10000.0f)) { beginEdit(); p.maxDistance = (std::max)(p.minDistance + 0.01f, maxD); }
            endEdit();
        }

        // Engine layers
        if (ImGui::CollapsingHeader("Engine Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Looping clips whose pitch/volume track RPM.");
            ImGui::Spacing();
            for (int i = 0; i < (int)p.engineLayers.size(); ++i) {
                VehicleSoundEngineLayer& L = p.engineLayers[i];
                ImGui::PushID(i);
                const bool layerOpen = ImGui::TreeNodeEx("##layer", ImGuiTreeNodeFlags_DefaultOpen,
                    "Layer %d  (%s)", i, L.clipPath.empty() ? "no clip" : fs::path(L.clipPath).filename().string().c_str());
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
                if (ImGui::SmallButton("X")) {
                    PushVehicleSoundUndoState();
                    p.engineLayers.erase(p.engineLayers.begin() + i);
                    ImGui::PopID();
                    if (layerOpen) ImGui::TreePop();
                    break;
                }
                if (layerOpen) {
                    // Clip path
                    char clipBuf[512]{};
                    std::snprintf(clipBuf, sizeof(clipBuf), "%s", L.clipPath.c_str());
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputText("Clip##L", clipBuf, sizeof(clipBuf), ImGuiInputTextFlags_ReadOnly);
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                            const char* dp = static_cast<const char*>(pl->Data);
                            if (dp && IsAudioAssetPath(dp)) { PushVehicleSoundUndoState(); L.clipPath = dp; }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::TextDisabled("Drag an audio file onto the field above.");

                    float rpmMin = L.rpmMin, rpmMax = L.rpmMax;
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::DragFloat("RPM Min##L", &rpmMin, 10.0f, 0.0f, 20000.0f)) { beginEdit(); L.rpmMin = (std::max)(0.0f, rpmMin); }
                    endEdit();
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::DragFloat("RPM Max##L", &rpmMax, 10.0f, L.rpmMin + 1.0f, 20000.0f)) { beginEdit(); L.rpmMax = (std::max)(L.rpmMin + 1.0f, rpmMax); }
                    endEdit();

                    float pMin = L.pitchAtRpmMin, pMax = L.pitchAtRpmMax;
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::DragFloat("Pitch@Min##L", &pMin, 0.01f, 0.01f, 8.0f)) { beginEdit(); L.pitchAtRpmMin = (std::max)(0.01f, pMin); }
                    endEdit();
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::DragFloat("Pitch@Max##L", &pMax, 0.01f, 0.01f, 8.0f)) { beginEdit(); L.pitchAtRpmMax = (std::max)(0.01f, pMax); }
                    endEdit();

                    float vMin = L.volumeAtRpmMin, vMax = L.volumeAtRpmMax;
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::DragFloat("Vol@Min##L", &vMin, 0.01f, 0.0f, 4.0f)) { beginEdit(); L.volumeAtRpmMin = (std::max)(0.0f, vMin); }
                    endEdit();
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::DragFloat("Vol@Max##L", &vMax, 0.01f, 0.0f, 4.0f)) { beginEdit(); L.volumeAtRpmMax = (std::max)(0.0f, vMax); }
                    endEdit();

                    float vts = L.volumeThrottleScale;
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::DragFloat("Throttle Vol Scale##L", &vts, 0.01f, 0.0f, 2.0f)) { beginEdit(); L.volumeThrottleScale = (std::max)(0.0f, vts); }
                    endEdit();

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::Spacing();
            if (ImGui::Button("+ Add Engine Layer")) {
                PushVehicleSoundUndoState();
                p.engineLayers.push_back(VehicleSoundEngineLayer{});
            }
        }

        // Trigger sounds
        if (ImGui::CollapsingHeader("Trigger Sounds", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("One-shot clips fired on discrete events.");
            ImGui::Spacing();
            const char* triggerNames[] = { "Gear Up", "Gear Down", "Backfire", "Engine Start", "Engine Stop", "Tire Squeal" };
            const VehicleSoundTrigger triggerValues[] = {
                VehicleSoundTrigger::GearUp, VehicleSoundTrigger::GearDown,
                VehicleSoundTrigger::Backfire, VehicleSoundTrigger::EngineStart,
                VehicleSoundTrigger::EngineStop, VehicleSoundTrigger::TireSqueal
            };
            for (int i = 0; i < (int)p.triggerSounds.size(); ++i) {
                VehicleSoundTriggerEntry& T = p.triggerSounds[i];
                ImGui::PushID(i + 1000);
                const bool tOpen = ImGui::TreeNodeEx("##trigger", ImGuiTreeNodeFlags_DefaultOpen,
                    "Trigger %d  (%s)", i, triggerNames[(int)T.trigger]);
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
                if (ImGui::SmallButton("X")) {
                    PushVehicleSoundUndoState();
                    p.triggerSounds.erase(p.triggerSounds.begin() + i);
                    ImGui::PopID();
                    if (tOpen) ImGui::TreePop();
                    break;
                }
                if (tOpen) {
                    // Clip path
                    char clipBuf[512]{};
                    std::snprintf(clipBuf, sizeof(clipBuf), "%s", T.clipPath.c_str());
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputText("Clip##T", clipBuf, sizeof(clipBuf), ImGuiInputTextFlags_ReadOnly);
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                            const char* dp = static_cast<const char*>(pl->Data);
                            if (dp && IsAudioAssetPath(dp)) { PushVehicleSoundUndoState(); T.clipPath = dp; }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::TextDisabled("Drag an audio file onto the field above.");

                    // Trigger type combo
                    int trigIdx = (int)T.trigger;
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::Combo("Event##T", &trigIdx, triggerNames, 6)) {
                        PushVehicleSoundUndoState();
                        T.trigger = triggerValues[trigIdx];
                    }

                    float vol = T.volume;
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::DragFloat("Volume##T", &vol, 0.01f, 0.0f, 4.0f)) { beginEdit(); T.volume = (std::max)(0.0f, vol); }
                    endEdit();

                    if (T.trigger == VehicleSoundTrigger::Backfire) {
                        float minRpm = T.minRpmForBackfire;
                        ImGui::SetNextItemWidth(160.0f);
                        if (ImGui::DragFloat("Min RPM##T", &minRpm, 10.0f, 0.0f, 20000.0f)) { beginEdit(); T.minRpmForBackfire = (std::max)(0.0f, minRpm); }
                        endEdit();
                    }
                    if (T.trigger == VehicleSoundTrigger::TireSqueal) {
                        float minSpd = T.minLateralSpeedForSqueal;
                        ImGui::SetNextItemWidth(160.0f);
                        if (ImGui::DragFloat("Min Lateral Speed##T", &minSpd, 0.1f, 0.0f, 100.0f)) { beginEdit(); T.minLateralSpeedForSqueal = (std::max)(0.0f, minSpd); }
                        endEdit();
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::Spacing();
            if (ImGui::Button("+ Add Trigger Sound")) {
                PushVehicleSoundUndoState();
                p.triggerSounds.push_back(VehicleSoundTriggerEntry{});
            }
        }

        // Undo/redo hint
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::BeginDisabled(vehicleSoundUndoStack_.empty());
        if (ImGui::Button("Undo")) UndoVehicleSound();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(vehicleSoundRedoStack_.empty());
        if (ImGui::Button("Redo")) RedoVehicleSound();
        ImGui::EndDisabled();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    if (!showVehicleSoundEditor_) {
        vehicleSoundEditorHovered_ = false;
        vehicleSoundEditorFocused_ = false;
        vehicleSoundEditActive_ = false;
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

    const Material beforeEdit = *material;
    bool materialChanged = false;

    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", material->name.c_str());
    if (ImGui::InputText("Name##materialName", nameBuf, sizeof(nameBuf))) {
        material->name = nameBuf;
        materialChanged = true;
    }

    std::string shaderId = ShaderRegistry::NormalizeShaderId(material->shader);
    int currentShaderIndex = 0;
    const auto& shaders = ShaderRegistry::BuiltInShaders();
    for (int i = 0; i < static_cast<int>(shaders.size()); ++i) {
        if (shaders[static_cast<std::size_t>(i)].id == shaderId) {
            currentShaderIndex = i;
            break;
        }
    }
    std::vector<std::pair<std::string, std::string>> graphShaders;
    std::unordered_map<std::string, bool> seenGraphShaderIds;
    std::string editableShaderGraphPath;
    for (const std::string& file : projectFiles_) {
        if (IsShaderGraphAssetPath(file)) {
            const std::string graphShaderId = ShaderRegistry::MakeGraphShaderId(file);
            if (seenGraphShaderIds.find(graphShaderId) != seenGraphShaderIds.end()) {
                if (graphShaderId == shaderId && editableShaderGraphPath.empty()) {
                    editableShaderGraphPath = file;
                }
                continue;
            }
            seenGraphShaderIds[graphShaderId] = true;
            std::string graphName = ProjectAssetDisplayFilename(file);
            const std::string suffix = ".shadergraph.json";
            if (graphName.size() >= suffix.size() &&
                ToLowerCopy(graphName.substr(graphName.size() - suffix.size())) == suffix) {
                graphName.resize(graphName.size() - suffix.size());
            }
            graphShaders.push_back({graphShaderId, graphName});
            if (graphShaderId == shaderId) {
                editableShaderGraphPath = file;
            }
        }
    }

    std::string shaderPreview = shaders[static_cast<std::size_t>(currentShaderIndex)].displayName;
    if (ShaderRegistry::IsGraphShaderId(shaderId)) {
        shaderPreview = shaderId;
        for (const auto& graphShader : graphShaders) {
            if (graphShader.first == shaderId) {
                shaderPreview = graphShader.second + " (Shader Graph)";
                break;
            }
        }
    }

    const float shaderLabelWidth = ImGui::CalcTextSize("Shader").x;
    const float editButtonWidth = ImGui::CalcTextSize("Edit").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float shaderComboWidth = (std::max)(1.0f,
        ImGui::CalcItemWidth() - editButtonWidth - shaderLabelWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::SetNextItemWidth(shaderComboWidth);
    if (ImGui::BeginCombo("##materialShader", shaderPreview.c_str())) {
        for (int i = 0; i < static_cast<int>(shaders.size()); ++i) {
            const ShaderDefinition& shader = shaders[static_cast<std::size_t>(i)];
            const bool selected = shader.id == shaderId;
            const std::string label = shader.displayName + "##" + shader.id;
            if (ImGui::Selectable(label.c_str(), selected)) {
                material->shader = shader.id;
                materialChanged = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        if (!graphShaders.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Shader Graphs");
            for (const auto& graphShader : graphShaders) {
                const bool selected = graphShader.first == shaderId;
                const std::string label = graphShader.second + "##" + graphShader.first;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    material->shader = graphShader.first;
                    materialChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        } else if (ShaderRegistry::IsGraphShaderId(shaderId)) {
            ImGui::Separator();
            ImGui::Selectable(shaderId.c_str(), true);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(editableShaderGraphPath.empty());
    if (ImGui::Button("Edit##materialShaderEdit", ImVec2(editButtonWidth, 0.0f))) {
        OpenShaderGraphEditor(editableShaderGraphPath);
    }
    if (editableShaderGraphPath.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Built-in shaders are read-only engine shaders. Select or create a Shader Graph to edit shader logic.");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted("Shader");

    shaderId = ShaderRegistry::NormalizeShaderId(material->shader);
    const ShaderDefinition& shaderDefinition = ShaderRegistry::Resolve(shaderId);
    if (ShaderRegistry::IsGraphShaderId(shaderId)) {
        RenderShaderGraphParametersPreview(editableShaderGraphPath);
    }

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
            materialChanged = true;
        }
        const fs::path resolvedPath = resolveTexturePath(value);
        const std::string wrappedValue = value.empty() ? std::string("(none)") : NormalizeSlashes(value);
        RenderInspectorWrappedValue("Path:", wrappedValue);
        if (ImGui::Button((std::string("Browse##") + idSuffix).c_str())) {
            const fs::path initialDirectory = resolvedPath.empty() ? materialDirectory : resolvedPath.parent_path();
            const std::string selected = OpenTextureFileDialogWin32(initialDirectory.string());
            if (!selected.empty()) {
                storeTexturePath(fs::path(selected), value);
                materialChanged = true;
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
            materialChanged = true;
        }
    };

    auto defaultPropertyValue = [](const ShaderDefinition::Property& property) {
        MaterialPropertyValue value;
        value.type = property.type;
        value.values[0] = property.defaultValues[0];
        value.values[1] = property.defaultValues[1];
        value.values[2] = property.defaultValues[2];
        value.values[3] = property.defaultValues[3];
        value.boolValue = property.defaultBool;
        return value;
    };

    auto editDynamicProperty = [&](const ShaderDefinition::Property& property) {
        const bool hadValue = material->properties.find(property.id) != material->properties.end();
        MaterialPropertyValue& value = material->properties[property.id];
        if (!hadValue || value.type != property.type) {
            value = defaultPropertyValue(property);
        }
        switch (property.type) {
        case MaterialPropertyType::Float:
            materialChanged |= ImGui::SliderFloat(property.label.c_str(), &value.values[0], property.minValue, property.maxValue);
            break;
        case MaterialPropertyType::Vec2:
            materialChanged |= ImGui::DragFloat2(property.label.c_str(), value.values.data(), 0.01f, property.minValue, property.maxValue);
            break;
        case MaterialPropertyType::Vec3:
            if (property.color) {
                materialChanged |= ImGui::ColorEdit3(property.label.c_str(), value.values.data());
            } else {
                materialChanged |= ImGui::DragFloat3(property.label.c_str(), value.values.data(), 0.01f, property.minValue, property.maxValue);
            }
            break;
        case MaterialPropertyType::Vec4:
            if (property.color) {
                materialChanged |= ImGui::ColorEdit4(property.label.c_str(), value.values.data());
            } else {
                materialChanged |= ImGui::DragFloat4(property.label.c_str(), value.values.data(), 0.01f, property.minValue, property.maxValue);
            }
            break;
        case MaterialPropertyType::Bool:
            materialChanged |= ImGui::Checkbox(property.label.c_str(), &value.boolValue);
            break;
        case MaterialPropertyType::Texture2D:
            editTexturePath(property.label.c_str(), property.id.c_str(), value.texturePath);
            break;
        }
    };

    bool textureHeaderShown = false;
    const std::vector<ShaderDefinition::Property>* properties = &shaderDefinition.properties;
    std::vector<ShaderDefinition::Property> graphProperties;
    if (ShaderRegistry::IsGraphShaderId(shaderId)) {
        graphProperties = ShaderRegistry::Resolve("pbr").properties;
        properties = &graphProperties;
    }
    for (const ShaderDefinition::Property& property : *properties) {
        if (property.type == MaterialPropertyType::Texture2D && !textureHeaderShown) {
            ImGui::Separator();
            ImGui::TextUnformatted("Texture Paths");
            textureHeaderShown = true;
        }
        if (property.id == "albedoColor") {
            materialChanged |= ImGui::ColorEdit4(property.label.c_str(), material->albedoColor);
        } else if (property.id == "emissiveColor") {
            materialChanged |= ImGui::ColorEdit3(property.label.c_str(), material->emissiveColor);
        } else if (property.id == "metallic") {
            materialChanged |= ImGui::SliderFloat(property.label.c_str(), &material->metallic, property.minValue, property.maxValue);
        } else if (property.id == "roughness") {
            materialChanged |= ImGui::SliderFloat(property.label.c_str(), &material->roughness, property.minValue, property.maxValue);
        } else if (property.id == "uvTiling") {
            materialChanged |= ImGui::DragFloat2(property.label.c_str(), material->uvTiling, 0.01f, property.minValue, property.maxValue);
        } else if (property.id == "uvOffset") {
            materialChanged |= ImGui::DragFloat2(property.label.c_str(), material->uvOffset, 0.01f, property.minValue, property.maxValue);
        } else if (property.id == "albedoTexture") {
            editTexturePath(property.label.c_str(), "matTexAlbedo", material->texAlbedo);
        } else if (property.id == "normalTexture") {
            editTexturePath(property.label.c_str(), "matTexNormal", material->texNormal);
        } else if (property.id == "metallicTexture") {
            editTexturePath(property.label.c_str(), "matTexMetallic", material->texMetallic);
        } else if (property.id == "roughnessTexture") {
            editTexturePath(property.label.c_str(), "matTexRoughness", material->texRoughness);
        } else if (property.id == "aoTexture") {
            editTexturePath(property.label.c_str(), "matTexAo", material->texAo);
        } else {
            editDynamicProperty(property);
        }
    }

    if (materialChanged && !materialEditActive_) {
        PushMaterialUndoState(beforeEdit);
        materialEditActive_ = true;
    }
    if (materialChanged && !materialManager_.Save(materialId, *material) && console_) {
        console_->AddError("Failed to auto-save material: " + materialId);
    }
    if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        materialEditActive_ = false;
    }

    if (showBackButton) {
        ImGui::Separator();
        if (ImGui::Button("Back to Object")) {
            inspectMaterial_ = false;
        }
    }
    ImGui::PopID();
}

bool SceneEditor::CreateMaterialAsset(const std::string& requestedName, std::string* outMaterialId, const std::string& shaderId) {
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

    Material material = ShaderRegistry::MakeDefaultMaterial(shaderId, materialId);
    try {
        fs::create_directories(targetPath.parent_path());
        std::ofstream out(targetPath, std::ios::trunc);
        if (!out.good()) {
            return false;
        }
        out << "{\n";
        out << "  \"version\": 1,\n";
        out << "  \"name\": \"" << JsonEscape(material.name) << "\",\n";
        out << "  \"shader\": \"" << JsonEscape(material.shader) << "\",\n";
        out << "  \"albedoColor\": [" << material.albedoColor[0] << ", " << material.albedoColor[1] << ", " << material.albedoColor[2] << ", " << material.albedoColor[3] << "],\n";
        out << "  \"metallic\": " << material.metallic << ",\n";
        out << "  \"roughness\": " << material.roughness << ",\n";
        out << "  \"emissiveColor\": [" << material.emissiveColor[0] << ", " << material.emissiveColor[1] << ", " << material.emissiveColor[2] << "],\n";
        out << "  \"uvTiling\": [1, 1],\n";
        out << "  \"uvOffset\": [0, 0],\n";
        out << "  \"textures\": {\n";
        out << "    \"albedo\": \"\",\n";
        out << "    \"normal\": \"\",\n";
        out << "    \"metallic\": \"\",\n";
        out << "    \"roughness\": \"\",\n";
        out << "    \"ao\": \"\"\n";
        out << "  },\n";
        out << "  \"properties\": {\n";
        std::size_t propertyIndex = 0;
        for (const auto& entry : material.properties) {
            const MaterialPropertyValue& property = entry.second;
            out << "    \"" << JsonEscape(entry.first) << "\": { \"type\": \"" << MaterialPropertyTypeName(property.type) << "\", \"value\": ";
            if (property.type == MaterialPropertyType::Bool) {
                out << (property.boolValue ? "true" : "false");
            } else if (property.type == MaterialPropertyType::Texture2D) {
                out << "\"" << JsonEscape(property.texturePath) << "\"";
            } else if (property.type == MaterialPropertyType::Float) {
                out << property.values[0];
            } else {
                const int count = MaterialPropertyComponentCount(property.type);
                out << "[";
                for (int i = 0; i < count; ++i) {
                    if (i > 0) out << ", ";
                    out << property.values[static_cast<std::size_t>(i)];
                }
                out << "]";
            }
            out << " }";
            if (++propertyIndex < material.properties.size()) {
                out << ",";
            }
            out << "\n";
        }
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
    vehicleConfigEditorFocusRequested_ = true;
    vehicleConfigEditorHighlightUntil_ = ImGui::GetTime() + 1.15;
}

void SceneEditor::OpenVehicleSoundEditor(const std::string& profilePath) {
    if (profilePath.empty()) {
        return;
    }

    const std::string normalizedPath = NormalizeSlashes(profilePath);
    const bool pathChanged = inspectedVehicleSoundPath_ != normalizedPath;
    inspectedVehicleSoundPath_ = normalizedPath;
    selectedProjectFile_ = inspectedVehicleSoundPath_;
    selectedProjectDirectory_ = ParentProjectDirectory(inspectedVehicleSoundPath_);
    if (pathChanged) {
        inspectedVehicleSoundLoaded_ = false;
        inspectedVehicleSoundError_.clear();
        vehicleSoundUndoStack_.clear();
        vehicleSoundRedoStack_.clear();
        vehicleSoundEditActive_ = false;
    }
    showVehicleSoundEditor_ = true;
    vehicleSoundEditorFocusRequested_ = true;
    vehicleSoundEditorHighlightUntil_ = ImGui::GetTime() + 1.15;
}

void SceneEditor::OpenMaterialEditor(const std::string& materialId) {
    if (materialId.empty()) {
        return;
    }

    if (!materialManager_.Exists(materialId)) {
        materialManager_.LoadAll();
    }

    const bool materialChanged = inspectedMaterialId_ != materialId;
    inspectedMaterialId_ = materialId;
    if (materialChanged) {
        materialUndoStack_.clear();
        materialRedoStack_.clear();
        materialEditActive_ = false;
    }
    inspectMaterial_ = true;
}
} // namespace raceman


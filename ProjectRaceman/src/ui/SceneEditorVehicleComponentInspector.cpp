#include "SceneEditorInternal.h"
#include "SceneEditorVehicleDebug.h"
#include "../physics/VehicleConfig.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {
constexpr const char* kInspectorComponentReorderPayload = "RM_COMP_REORDER";

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
} // namespace

void SceneEditor::RenderVehicleComponentInspector(SceneObject& obj,
                                                   SceneInspectorComponentType& reorderDraggedType,
                                                   SceneInspectorComponentType& reorderTargetType) {
    auto prepareComponentOpenState = [&](SceneInspectorComponentType) {
        const std::string key = obj.id + "|Vehicle";
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
                                return runtimeVehicle.objectIndex == selectedIndex_;
                            });
                        if (runtimeVehicleIt != runtimeVehicles_.end()) {
                            const RuntimeVehicleInstance& runtimeVehicle = *runtimeVehicleIt;
                            if (BeginInspectorSubsection("Runtime Debug", "VehicleRuntimeDebugSection", vehicleAccent, ImGuiTreeNodeFlags_None)) {
                            RenderVehicleRuntimeDebugPanel(loadedConfig, runtimeVehicle);
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

} // namespace raceman
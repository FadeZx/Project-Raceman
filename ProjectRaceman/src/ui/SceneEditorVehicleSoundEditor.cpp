#include "SceneEditorInternal.h"
#include "../audio/VehicleSoundProfile.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

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

} // namespace raceman
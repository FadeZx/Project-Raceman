#include "SceneEditorInternal.h"
#include "../physics/PhysicsWorld.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

namespace raceman {
using namespace scene_editor_internal;
void SceneEditor::RenderProjectPhysicsSettings() {
    bool projectSettingsChanged = false;
    if (ImGui::BeginTable("ProjectPhysicsLayerMatrix", kPhysicsLayerCount + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Layer");
        for (int column = 0; column < kPhysicsLayerCount; ++column) {
            ImGui::TableSetColumnIndex(column + 1);
            ImGui::PushID(("projectPhysicsLayerNameHeader_" + std::to_string(column)).c_str());
            ImGui::SetNextItemWidth(100.0f);
            std::string layerName = physicsLayerNames_[static_cast<std::size_t>(column)];
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "%s", layerName.c_str());
            if (ImGui::InputText("##layerName", buffer, sizeof(buffer))) {
                physicsLayerNames_[static_cast<std::size_t>(column)] = buffer;
                projectSettingsChanged = true;
            }
            ImGui::PopID();
        }

        for (int row = 0; row < kPhysicsLayerCount; ++row) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(GetPhysicsLayerName(row));
            for (int column = 0; column < kPhysicsLayerCount; ++column) {
                ImGui::TableSetColumnIndex(column + 1);
                bool collides = physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
                const std::string checkboxId = "##projectPhysicsLayerCollision_" + std::to_string(row) + "_" + std::to_string(column);
                if (ImGui::Checkbox(checkboxId.c_str(), &collides)) {
                    physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = collides;
                    physicsLayerCollisionMatrix_[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)] = collides;
                    projectSettingsChanged = true;
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Assign each object's physics layer in the Inspector. This matrix controls which layers collide.");
    ImGui::Spacing();
    ImGui::SeparatorText("Track Surface Layers");
    if (ImGui::BeginTable("ProjectTrackSurfaceSettings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Surface");
        ImGui::TableSetupColumn("Grip");
        ImGui::TableSetupColumn("Rolling Drag");
        ImGui::TableHeadersRow();

        for (int surfaceIndex = 0; surfaceIndex < kTrackSurfaceTypeCount; ++surfaceIndex) {
            ColliderSurfaceConfig& surface = trackSurfaceSettings_[static_cast<std::size_t>(surfaceIndex)];
            surface.type = kTrackSurfaceTypes[static_cast<std::size_t>(surfaceIndex)];

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(TrackSurfaceTypeLabel(surface.type));

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(("projectTrackSurfaceGrip_" + std::to_string(surfaceIndex)).c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            float grip = surface.gripMultiplier;
            if (ImGui::DragFloat("##grip", &grip, 0.01f, 0.0f, 10.0f, "%.2f")) {
                surface.gripMultiplier = (std::max)(0.0f, grip);
                projectSettingsChanged = true;
            }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(("projectTrackSurfaceRollingDrag_" + std::to_string(surfaceIndex)).c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            float rollingDrag = surface.rollingDrag;
            if (ImGui::DragFloat("##rollingDrag", &rollingDrag, 0.01f, 0.0f, 20.0f, "%.2f")) {
                surface.rollingDrag = (std::max)(0.0f, rollingDrag);
                projectSettingsChanged = true;
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
    if (ImGui::Button("Reset Surface Defaults")) {
        ResetTrackSurfaceSettings();
        projectSettingsChanged = true;
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Collision Bake Cache");
    ImGui::TextDisabled("Path: %s", PhysicsWorld::GetCollisionShapeCacheDirectory().c_str());
    if (ImGui::Button("Clear Collision Cache")) {
        std::string error;
        const int removedCount = PhysicsWorld::ClearCollisionShapeCache(&error);
        lastPhysicsCacheStatus_ = "Ready";
        if (console_) {
            if (!error.empty()) {
                console_->AddWarning("Cleared " + std::to_string(removedCount) + " collision cache files, but some files could not be removed: " + error);
            } else {
                console_->AddLog("Cleared " + std::to_string(removedCount) + " collision cache files.");
            }
        }
    }

    if (projectSettingsChanged) {
        for (int layerIndex = 0; layerIndex < kPhysicsLayerCount; ++layerIndex) {
            if (physicsLayerNames_[static_cast<std::size_t>(layerIndex)].empty()) {
                physicsLayerNames_[static_cast<std::size_t>(layerIndex)] = layerIndex == 0 ? "Default" : ("Layer" + std::to_string(layerIndex));
            }
        }
        for (int surfaceIndex = 0; surfaceIndex < kTrackSurfaceTypeCount; ++surfaceIndex) {
            trackSurfaceSettings_[static_cast<std::size_t>(surfaceIndex)].type =
                kTrackSurfaceTypes[static_cast<std::size_t>(surfaceIndex)];
        }
        SaveProject();
    }
}


} // namespace raceman
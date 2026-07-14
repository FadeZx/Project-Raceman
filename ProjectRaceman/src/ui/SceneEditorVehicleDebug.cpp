#include "SceneEditorVehicleDebug.h"

#include "SceneEditorInternal.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace raceman {
using namespace scene_editor_internal;

void RenderVehicleRuntimeDebugPanel(const raceman::physics::VehicleConfig& loadedConfig,
                                    const RuntimeVehicleInstance& runtimeVehicle) {
    const float speed = std::fabs(runtimeVehicle.arcadeSpeed);
    auto gearText = [&]() {
        if (runtimeVehicle.arcadeGear < 0) {
            return std::string("R");
        }
        if (runtimeVehicle.arcadeGear == 0) {
            return std::string("N");
        }
        return std::to_string(runtimeVehicle.arcadeGear);
    };
    auto runtimeMetric = [](const char* label, const char* value) {
        ImGui::TextDisabled("%s", label);
        ImGui::TextUnformatted(value);
    };

    ImGui::TextDisabled("%s  |  %d wheels", loadedConfig.name.c_str(), static_cast<int>(loadedConfig.wheels.size()));
    if (ImGui::BeginTable("##VehicleRuntimeSummary", 3, ImGuiTableFlags_SizingStretchSame)) {
        char speedText[32];
        char rpmText[32];
        const std::string gearValue = gearText();
        std::snprintf(speedText, sizeof(speedText), "%.2f m/s", speed);
        std::snprintf(rpmText, sizeof(rpmText), "%.0f", runtimeVehicle.arcadeEngineRPM);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        runtimeMetric("Speed", speedText);
        ImGui::TableSetColumnIndex(1);
        runtimeMetric("RPM", rpmText);
        ImGui::TableSetColumnIndex(2);
        runtimeMetric("Gear", gearValue.c_str());
        ImGui::EndTable();
    }

    if (ImGui::BeginTable("##VehicleRuntimeInputs", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Throttle %.2f / %.2f", runtimeVehicle.arcadeThrottle, runtimeVehicle.arcadeRawThrottle);
        ImGui::ProgressBar((std::clamp)(runtimeVehicle.arcadeThrottle, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), "");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Brake %.2f / %.2f", runtimeVehicle.arcadeBrake, runtimeVehicle.arcadeRawBrake);
        ImGui::ProgressBar((std::clamp)(runtimeVehicle.arcadeBrake, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), "");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("Steer %.2f", runtimeVehicle.arcadeSteering);
        ImGui::ProgressBar((std::clamp)((runtimeVehicle.arcadeSteering + 1.0f) * 0.5f, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), "");
        ImGui::EndTable();
    }

    if (ImGui::BeginTable("##VehicleRuntimeDynamics", 4, ImGuiTableFlags_SizingStretchSame)) {
        char slipText[32];
        char tractionText[32];
        char loadText[48];
        char yawText[48];
        std::snprintf(slipText, sizeof(slipText), "%.2f m/s  %.1f deg", runtimeVehicle.arcadeLateralSpeed, runtimeVehicle.arcadeVelocitySlipAngle);
        std::snprintf(tractionText, sizeof(tractionText), "%.2f", runtimeVehicle.arcadeTractionScale);
        std::snprintf(loadText, sizeof(loadText), "L %.2f  S %.2f  A %.2f",
            runtimeVehicle.arcadeLongitudinalLoad,
            runtimeVehicle.arcadeLateralLoad,
            runtimeVehicle.arcadeAeroGripBoost);
        std::snprintf(yawText, sizeof(yawText), "%.1f deg/s  T %.1f",
            runtimeVehicle.arcadeYawRate,
            runtimeVehicle.arcadeYawTorque);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        runtimeMetric("Slip", slipText);
        ImGui::TableSetColumnIndex(1);
        runtimeMetric("Traction", tractionText);
        ImGui::TableSetColumnIndex(2);
        runtimeMetric("Load / Aero", loadText);
        ImGui::TableSetColumnIndex(3);
        runtimeMetric("Yaw", yawText);
        ImGui::EndTable();
    }
    if (ImGui::BeginTable("##VehicleRuntimeAids", 3, ImGuiTableFlags_SizingStretchSame)) {
        char tcText[32];
        char absText[32];
        char diffText[32];
        std::snprintf(tcText, sizeof(tcText), "%.2f", runtimeVehicle.arcadeTractionControlCut);
        std::snprintf(absText, sizeof(absText), "%.2f", runtimeVehicle.arcadeAbsBrakeScale);
        std::snprintf(diffText, sizeof(diffText), "%.2f", runtimeVehicle.arcadeDifferentialLock);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        runtimeMetric("TC Cut", tcText);
        ImGui::TableSetColumnIndex(1);
        runtimeMetric("ABS Brake", absText);
        ImGui::TableSetColumnIndex(2);
        runtimeMetric("Diff Lock", diffText);
        ImGui::EndTable();
    }
    if (ImGui::BeginTable("##VehicleRuntimeTireDynamics", 4, ImGuiTableFlags_SizingStretchSame)) {
        char frontSlipText[32];
        char rearSlipText[32];
        char gripBalanceText[32];
        char sideSlipText[48];
        std::snprintf(frontSlipText, sizeof(frontSlipText), "%.2f", runtimeVehicle.arcadeFrontSlip);
        std::snprintf(rearSlipText, sizeof(rearSlipText), "%.2f", runtimeVehicle.arcadeRearSlip);
        std::snprintf(gripBalanceText, sizeof(gripBalanceText), "%.2f", runtimeVehicle.arcadeGripBalance);
        std::snprintf(sideSlipText, sizeof(sideSlipText), "%.2f m/s  scrub %.2f",
            runtimeVehicle.arcadeSideSlipVelocity,
            runtimeVehicle.arcadeTireScrub);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        runtimeMetric("Front Slip", frontSlipText);
        ImGui::TableSetColumnIndex(1);
        runtimeMetric("Rear Slip", rearSlipText);
        ImGui::TableSetColumnIndex(2);
        runtimeMetric("Grip Balance", gripBalanceText);
        ImGui::TableSetColumnIndex(3);
        runtimeMetric("Side Slip", sideSlipText);
        ImGui::EndTable();
    }

    if (!loadedConfig.wheels.empty() &&
        ImGui::BeginTable("##WheelDebug", 4,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Wheel", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Ground", ImGuiTableColumnFlags_WidthStretch, 0.75f);
        ImGui::TableSetupColumn("Susp / Load", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Slip / RPM", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (std::size_t wi = 0; wi < loadedConfig.wheels.size(); ++wi) {
            const raceman::physics::WheelConfig& wt = loadedConfig.wheels[wi];
            const float wheelRadius = (std::max)(0.05f, wt.radius);
            const bool hasContact = wi < runtimeVehicle.arcadeWheelContacts.size();
            const auto* contact = hasContact ? &runtimeVehicle.arcadeWheelContacts[wi] : nullptr;
            const bool grounded = contact != nullptr && contact->grounded;
            const float normalForce = contact != nullptr ? contact->normalForce : 0.0f;
            const float suspensionTravel = contact != nullptr ? contact->suspensionTravel : 0.0f;
            const float loadMultiplier = contact != nullptr ? contact->loadMultiplier : 1.0f;
            const float slipAngle = contact != nullptr ? contact->slipAngle : 0.0f;
            const float tractionScale = contact != nullptr ? contact->tractionScale : 1.0f;
            const float angularVelocity = contact != nullptr ? contact->angularVelocity : (runtimeVehicle.arcadeSpeed / wheelRadius);
            const float wheelRpm = angularVelocity * (60.0f / (2.0f * 3.14159f));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(wt.name.c_str());
            ImGui::TextDisabled("%s", contact != nullptr ? TrackSurfaceTypeLabel(contact->surfaceType) : "-");
            ImGui::TableSetColumnIndex(1);
            if (grounded) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "ON");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "OFF");
            }
            ImGui::TextDisabled("%.0f N", normalForce);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f m", suspensionTravel);
            ImGui::TextDisabled("x%.2f", loadMultiplier);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.1f deg", slipAngle);
            ImGui::TextDisabled("%.2f  %.0f rpm", tractionScale, wheelRpm);
        }
        ImGui::EndTable();
    }

}

} // namespace raceman

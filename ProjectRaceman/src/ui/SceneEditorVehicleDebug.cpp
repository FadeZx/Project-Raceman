#include "SceneEditorVehicleDebug.h"

#include "SceneEditorInternal.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace raceman {
using namespace scene_editor_internal;

namespace {

struct VehicleDiagnosticSummary {
    float frontNormalForce{0.0f};
    float rearNormalForce{0.0f};
    float frontGripBudget{0.0f};
    float rearGripBudget{0.0f};
    float averageSurfaceGrip{1.0f};
    float averageRollingDrag{0.0f};
    float estimatedDriveForce{0.0f};
    float estimatedBrakeForce{0.0f};
};

void RuntimeMetric(const char* label, const char* value) {
    ImGui::TextDisabled("%s", label);
    ImGui::TextUnformatted(value);
}

bool IsFrontWheel(const raceman::physics::WheelConfig& wheel) {
    return wheel.mountPosition.y >= 0.0f;
}

VehicleDiagnosticSummary BuildDiagnosticSummary(const raceman::physics::VehicleConfig& config,
                                                const RuntimeVehicleInstance& runtimeVehicle) {
    VehicleDiagnosticSummary summary;
    float gripSamples = 0.0f;
    float dragSamples = 0.0f;
    int contactSamples = 0;

    for (std::size_t wi = 0; wi < config.wheels.size() && wi < runtimeVehicle.arcadeWheelContacts.size(); ++wi) {
        const raceman::physics::WheelConfig& wheel = config.wheels[wi];
        const RuntimeVehicleWheelContact& contact = runtimeVehicle.arcadeWheelContacts[wi];
        if (!contact.grounded) {
            continue;
        }

        const raceman::physics::ResolvedWheelTireConfig tire = raceman::physics::resolveWheelTire(config, wheel);
        const float wheelGrip = (std::max)(0.0f, tire.gripFactor);
        const float surfaceGrip = (std::max)(0.0f, contact.surfaceGripMultiplier);
        const float traction = (std::clamp)(contact.tractionScale, 0.0f, 1.5f);
        const float load = (std::max)(0.0f, contact.normalForce);
        const float lateralBudget = load * wheelGrip * surfaceGrip * traction;

        if (IsFrontWheel(wheel)) {
            summary.frontNormalForce += load;
            summary.frontGripBudget += lateralBudget * (std::max)(0.05f, config.tireDynamics.frontGripBias);
        } else {
            summary.rearNormalForce += load;
            summary.rearGripBudget += lateralBudget * (std::max)(0.05f, config.tireDynamics.rearGripBias);
        }

        gripSamples += surfaceGrip;
        dragSamples += (std::max)(0.0f, contact.surfaceRollingDrag);
        ++contactSamples;
    }

    if (contactSamples > 0) {
        summary.averageSurfaceGrip = gripSamples / static_cast<float>(contactSamples);
        summary.averageRollingDrag = dragSamples / static_cast<float>(contactSamples);
    }

    const float throttleScale = (std::clamp)(runtimeVehicle.arcadeThrottle * (1.0f - runtimeVehicle.arcadeTractionControlCut), 0.0f, 1.0f);
    const float speedRatio = config.arcadeHandling.maxForwardSpeed > 0.1f
        ? (std::clamp)(std::fabs(runtimeVehicle.arcadeSpeed) / config.arcadeHandling.maxForwardSpeed, 0.0f, 1.0f)
        : 0.0f;
    summary.estimatedDriveForce =
        (std::max)(1.0f, config.chassis.mass) *
        (std::max)(0.0f, config.arcadeHandling.acceleration) *
        throttleScale *
        (1.0f - speedRatio * 0.55f);
    summary.estimatedBrakeForce =
        (std::max)(1.0f, config.chassis.mass) *
        (std::max)(0.0f, config.arcadeHandling.brakeDeceleration) *
        (std::clamp)(runtimeVehicle.arcadeBrake * runtimeVehicle.arcadeAbsBrakeScale, 0.0f, 1.0f);

    return summary;
}

std::vector<std::string> BuildVehicleConfigWarnings(const raceman::physics::VehicleConfig& config,
                                                    const RuntimeVehicleInstance& runtimeVehicle,
                                                    const VehicleDiagnosticSummary& summary) {
    std::vector<std::string> warnings;

    if (config.wheels.size() < 4) {
        warnings.push_back("Vehicle has fewer than 4 wheels; contact and axle slip telemetry may be incomplete.");
    }
    if (config.arcadeHandling.maxForwardSpeed <= 0.1f) {
        warnings.push_back("Max forward speed is near zero; speed ratio and drive force estimates are invalid.");
    }
    if (config.arcadeHandling.acceleration > config.arcadeHandling.brakeDeceleration * 0.85f) {
        warnings.push_back("Acceleration is close to brake deceleration; braking may feel weak compared with power.");
    }
    if (config.tireGrip.lateralGrip < 2.0f) {
        warnings.push_back("Lateral grip is very low; the car will skate sideways before yaw dynamics can recover it.");
    }
    if (config.tireDynamics.slideFriction < 1.0f) {
        warnings.push_back("Slide friction is low; slides will preserve speed and feel frictionless.");
    }
    if (config.tireDynamics.tireScrub < 1.0f) {
        warnings.push_back("Tire scrub is low; side slip will decay slowly and may feel like ice.");
    }
    if (config.tireDynamics.velocityAlignmentRate > 3.0f) {
        warnings.push_back("Velocity alignment is high; counter-steer/recovery may snap back too quickly.");
    }
    if (config.tireDynamics.velocityAlignmentRate < 0.45f) {
        warnings.push_back("Velocity alignment is low; steering response can feel delayed and heavy.");
    }
    if (config.tireDynamics.yawDrag > 5.0f) {
        warnings.push_back("Yaw drag is high; steering and drift rotation can feel damped or delayed.");
    }
    if (config.tireDynamics.yawInertiaScale > 2.0f) {
        warnings.push_back("Yaw inertia scale is high; steering correction can feel late and heavy.");
    }
    if (config.yawDynamics.enabled && config.yawDynamics.maxYawRate < 70.0f) {
        warnings.push_back("Max yaw rate is low; the car may refuse to rotate during oversteer.");
    }
    if (config.tireDynamics.rearGripBias < config.tireDynamics.frontGripBias * 0.78f) {
        warnings.push_back("Rear grip bias is much lower than front; expect frequent oversteer/spin.");
    }
    if (summary.rearGripBudget > 1.0f && summary.estimatedDriveForce > summary.rearGripBudget * 1.35f) {
        warnings.push_back("Estimated drive force exceeds rear grip budget; throttle oversteer/wheelspin is likely.");
    }
    if (summary.frontGripBudget > 1.0f && summary.estimatedBrakeForce > (summary.frontGripBudget + summary.rearGripBudget) * 1.5f) {
        warnings.push_back("Estimated brake force is high versus tire grip; ABS/brake tuning may dominate corner entry.");
    }
    if (std::fabs(runtimeVehicle.arcadeVelocitySlipAngle) > config.yawDynamics.spinSlipAngle * 0.8f &&
        std::fabs(runtimeVehicle.arcadeYawRate) > config.yawDynamics.maxYawRate * 0.7f) {
        warnings.push_back("Runtime slip angle and yaw rate are near spin range.");
    }

    return warnings;
}

void RenderBar(float value, float maxValue, const ImVec4& color) {
    const float fraction = maxValue > 0.0001f ? (std::clamp)(value / maxValue, 0.0f, 1.0f) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), "");
    ImGui::PopStyleColor();
}

} // namespace

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
    const VehicleDiagnosticSummary diagnostics = BuildDiagnosticSummary(loadedConfig, runtimeVehicle);
    const std::vector<std::string> warnings = BuildVehicleConfigWarnings(loadedConfig, runtimeVehicle, diagnostics);

    ImGui::TextDisabled("%s  |  %d wheels", loadedConfig.name.c_str(), static_cast<int>(loadedConfig.wheels.size()));
    if (ImGui::BeginTable("##VehicleRuntimeSummary", 3, ImGuiTableFlags_SizingStretchSame)) {
        char speedText[32];
        char rpmText[32];
        const std::string gearValue = gearText();
        std::snprintf(speedText, sizeof(speedText), "%.1f km/h", speed * 3.6f);
        std::snprintf(rpmText, sizeof(rpmText), "%.0f", runtimeVehicle.arcadeEngineRPM);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        RuntimeMetric("Speed", speedText);
        ImGui::TableSetColumnIndex(1);
        RuntimeMetric("RPM", rpmText);
        ImGui::TableSetColumnIndex(2);
        RuntimeMetric("Gear", gearValue.c_str());
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
        RuntimeMetric("Slip", slipText);
        ImGui::TableSetColumnIndex(1);
        RuntimeMetric("Traction", tractionText);
        ImGui::TableSetColumnIndex(2);
        RuntimeMetric("Load / Aero", loadText);
        ImGui::TableSetColumnIndex(3);
        RuntimeMetric("Yaw", yawText);
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
        RuntimeMetric("TC Cut", tcText);
        ImGui::TableSetColumnIndex(1);
        RuntimeMetric("ABS Brake", absText);
        ImGui::TableSetColumnIndex(2);
        RuntimeMetric("Diff Lock", diffText);
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
        RuntimeMetric("Front Slip", frontSlipText);
        ImGui::TableSetColumnIndex(1);
        RuntimeMetric("Rear Slip", rearSlipText);
        ImGui::TableSetColumnIndex(2);
        RuntimeMetric("Grip Balance", gripBalanceText);
        ImGui::TableSetColumnIndex(3);
        RuntimeMetric("Side Slip", sideSlipText);
        ImGui::EndTable();
    }

    if (ImGui::BeginTable("##VehicleRuntimeGripBudget", 4, ImGuiTableFlags_SizingStretchSame)) {
        char frontBudgetText[48];
        char rearBudgetText[48];
        char forceText[48];
        char surfaceText[48];
        std::snprintf(frontBudgetText, sizeof(frontBudgetText), "%.0f N / %.0f N",
            diagnostics.frontGripBudget,
            diagnostics.frontNormalForce);
        std::snprintf(rearBudgetText, sizeof(rearBudgetText), "%.0f N / %.0f N",
            diagnostics.rearGripBudget,
            diagnostics.rearNormalForce);
        std::snprintf(forceText, sizeof(forceText), "D %.0f  B %.0f N",
            diagnostics.estimatedDriveForce,
            diagnostics.estimatedBrakeForce);
        std::snprintf(surfaceText, sizeof(surfaceText), "Grip %.2f  Drag %.2f",
            diagnostics.averageSurfaceGrip,
            diagnostics.averageRollingDrag);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        RuntimeMetric("Front Budget / Load", frontBudgetText);
        ImGui::TableSetColumnIndex(1);
        RuntimeMetric("Rear Budget / Load", rearBudgetText);
        ImGui::TableSetColumnIndex(2);
        RuntimeMetric("Drive / Brake", forceText);
        ImGui::TableSetColumnIndex(3);
        RuntimeMetric("Surface", surfaceText);
        ImGui::EndTable();
    }

    if (ImGui::BeginTable("##VehicleRuntimeBudgetBars", 2, ImGuiTableFlags_SizingStretchSame)) {
        const float maxGripBudget = (std::max)(1.0f, (std::max)(diagnostics.frontGripBudget, diagnostics.rearGripBudget));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Front Grip Budget");
        RenderBar(diagnostics.frontGripBudget, maxGripBudget, ImVec4(0.26f, 0.58f, 0.95f, 1.0f));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Rear Grip Budget");
        RenderBar(diagnostics.rearGripBudget, maxGripBudget, ImVec4(0.95f, 0.46f, 0.22f, 1.0f));
        ImGui::EndTable();
    }

    if (!warnings.empty() && ImGui::CollapsingHeader("Tuning Warnings", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const std::string& warning : warnings) {
            ImGui::BulletText("%s", warning.c_str());
        }
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

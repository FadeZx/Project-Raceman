#include "SceneEditorInternal.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

bool RenderInspectorDragFloat(const char* label, const char* id, float* value, float speed, float min = 0.0f, float max = 0.0f) {
    if (IsNarrowInspectorLayout()) {
        RenderInspectorLabel(label);
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::DragFloat(id, value, speed, min, max);
    }
    const std::string fullId = std::string(label) + (id != nullptr && std::strncmp(id, "##", 2) == 0 ? id : (std::string("##") + (id != nullptr ? id : label)));
    return ImGui::DragFloat(fullId.c_str(), value, speed, min, max);
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

} // namespace
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
        auto applyDifferentialTypeEdit = [&](const char* label, raceman::physics::DifferentialConfig::Type& value) {
            int currentIndex = 1;
            if (value == raceman::physics::DifferentialConfig::Type::Open) {
                currentIndex = 0;
            } else if (value == raceman::physics::DifferentialConfig::Type::Locked) {
                currentIndex = 2;
            }
            const char* options[] = {"Open", "Limited Slip", "Locked"};
            if (ImGui::Combo(label, &currentIndex, options, IM_ARRAYSIZE(options))) {
                PushVehicleConfigUndoState();
                vehicleConfigEditActive_ = false;
                value = currentIndex == 0
                    ? raceman::physics::DifferentialConfig::Type::Open
                    : (currentIndex == 2
                        ? raceman::physics::DifferentialConfig::Type::Locked
                        : raceman::physics::DifferentialConfig::Type::LimitedSlip);
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
        auto renderGroundContactSection = [&]() {
            raceman::physics::VehicleGroundContactConfig& groundContact = inspectedVehicleConfig_.groundContact;
            beginCard("vehicleProfileGroundContact", "Ground Contact", "Raycast ride and wall probing", accentPrimary);
            applyCheckboxEdit("Enabled##vehicleProfileGroundContactEnabled", groundContact.enabled);
            applyDragFloatEdit("Probe Up (m)", "##vehicleProfileGroundContactProbeUp", groundContact.probeUp, 0.01f, 0.0f, 100.0f);
            applyDragFloatEdit("Extra Probe Length (m)", "##vehicleProfileGroundContactExtraProbeLength", groundContact.extraProbeLength, 0.01f, 0.0f, 100.0f);
            applyDragFloatEdit("Ride Height Offset (m)", "##vehicleProfileGroundContactRideHeightOffset", groundContact.rideHeightOffset, 0.01f, -10.0f, 10.0f);
            applyDragFloatEdit("Height Smoothing", "##vehicleProfileGroundContactHeightSmoothing", groundContact.heightSmoothing, 0.1f, 0.0f, 1000.0f);
            applyDragFloatEdit("Tilt Smoothing", "##vehicleProfileGroundContactTiltSmoothing", groundContact.tiltSmoothing, 0.1f, 0.0f, 1000.0f);
            applyDragFloatEdit("Min Ground Normal Y", "##vehicleProfileGroundContactMinGroundNormalY", groundContact.minGroundNormalY, 0.01f, -1.0f, 1.0f);
            applyDragFloatEdit("Obstacle Probe Height (m)", "##vehicleProfileGroundContactObstacleProbeHeight", groundContact.obstacleProbeHeight, 0.01f, 0.0f, 100.0f);
            applyDragFloatEdit("Obstacle Skin (m)", "##vehicleProfileGroundContactObstacleSkin", groundContact.obstacleSkin, 0.01f, 0.0f, 100.0f);
            applyDragFloatEdit("Wall Normal Y Max", "##vehicleProfileGroundContactWallNormalYMax", groundContact.wallNormalYMax, 0.01f, -1.0f, 1.0f);
            applyDragFloatEdit("Airborne Gravity (m/s^2)", "##vehicleProfileGroundContactAirborneGravity", groundContact.airborneGravity, 0.1f, 0.0f, 1000.0f);
            endCard();
        };
        auto renderTireGripSection = [&]() {
            raceman::physics::VehicleTireGripConfig& tireGrip = inspectedVehicleConfig_.tireGrip;
            beginCard("vehicleProfileTireGrip", "Tire Grip", "Arcade slip and surface response", accentSecondary);
            applyCheckboxEdit("Enabled##vehicleProfileTireGripEnabled", tireGrip.enabled);
            applyDragFloatEdit("Lateral Grip", "##vehicleProfileTireGripLateralGrip", tireGrip.lateralGrip, 0.05f, 0.0f, 100.0f);
            applyDragFloatEdit("Longitudinal Grip", "##vehicleProfileTireGripLongitudinalGrip", tireGrip.longitudinalGrip, 0.01f, 0.0f, 10.0f);
            applyDragFloatEdit("Slip Angle Limit (deg)", "##vehicleProfileTireGripSlipAngleLimit", tireGrip.slipAngleLimit, 0.1f, 0.1f, 90.0f);
            applyDragFloatEdit("Slide Grip Loss", "##vehicleProfileTireGripSlideGripLoss", tireGrip.slideGripLoss, 0.01f, 0.0f, 1.0f);
            applyDragFloatEdit("Recovery Rate", "##vehicleProfileTireGripRecoveryRate", tireGrip.recoveryRate, 0.05f, 0.0f, 100.0f);
            applyDragFloatEdit("Handbrake Grip Scale", "##vehicleProfileTireGripHandbrakeGripScale", tireGrip.handbrakeGripScale, 0.01f, 0.0f, 1.0f);
            applyDragFloatEdit("Downforce Grip Scale", "##vehicleProfileTireGripDownforceGripScale", tireGrip.downforceGripScale, 0.01f, 0.0f, 10.0f);
            applyDragFloatEdit("Min Traction Scale", "##vehicleProfileTireGripMinTractionScale", tireGrip.minTractionScale, 0.01f, 0.0f, 1.0f);
            endCard();
        };
        auto renderArcadeHandlingSection = [&]() {
            raceman::physics::VehicleArcadeHandlingConfig& arcadeHandling = inspectedVehicleConfig_.arcadeHandling;
            beginCard("vehicleProfileArcadeHandling", "Arcade Handling", "Movement, braking, steering and runtime RPM", accentPrimary);
            if (ImGui::BeginTable("VehicleProfileArcadeHandlingGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                applyDragFloatEdit("Max Forward Speed (m/s)", "##vehicleProfileArcadeMaxForward", arcadeHandling.maxForwardSpeed, 0.1f, 1.0f, 500.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Max Reverse Speed (m/s)", "##vehicleProfileArcadeMaxReverse", arcadeHandling.maxReverseSpeed, 0.1f, 0.0f, 100.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Acceleration", "##vehicleProfileArcadeAcceleration", arcadeHandling.acceleration, 0.1f, 0.0f, 500.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Reverse Acceleration", "##vehicleProfileArcadeReverseAcceleration", arcadeHandling.reverseAcceleration, 0.1f, 0.0f, 500.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Brake Deceleration", "##vehicleProfileArcadeBrakeDeceleration", arcadeHandling.brakeDeceleration, 0.1f, 0.0f, 500.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Coast Deceleration", "##vehicleProfileArcadeCoastDeceleration", arcadeHandling.coastDeceleration, 0.05f, 0.0f, 100.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Handbrake Deceleration", "##vehicleProfileArcadeHandbrakeDeceleration", arcadeHandling.handbrakeDeceleration, 0.1f, 0.0f, 500.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Fallback Steer Rate (deg/s)", "##vehicleProfileArcadeFallbackSteer", arcadeHandling.fallbackSteerDegreesPerSecond, 0.5f, 0.0f, 1000.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Low Speed Steer Speed (m/s)", "##vehicleProfileArcadeLowSpeedSteerSpeed", arcadeHandling.lowSpeedSteerSpeed, 0.01f, 0.01f, 100.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Low Speed Steer Floor", "##vehicleProfileArcadeLowSpeedSteerFloor", arcadeHandling.lowSpeedSteerFloor, 0.01f, 0.0f, 1.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Low Speed Input Boost", "##vehicleProfileArcadeLowSpeedInputBoost", arcadeHandling.lowSpeedSteerInputBoost, 0.01f, 0.0f, 1.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("High Speed Steer Cut", "##vehicleProfileArcadeHighSpeedSteerCut", arcadeHandling.highSpeedSteerCut, 0.01f, 0.0f, 0.95f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Idle RPM", "##vehicleProfileArcadeIdleRpm", arcadeHandling.idleRPM, 10.0f, 0.0f, 20000.0f);
                ImGui::TableNextColumn();
                applyDragFloatEdit("Redline RPM", "##vehicleProfileArcadeRedlineRpm", arcadeHandling.redlineRPM, 10.0f, 100.0f, 30000.0f);
                ImGui::EndTable();
            }
            endCard();
        };
        auto renderTireDynamicsSection = [&]() {
            raceman::physics::VehicleTireDynamicsConfig& tireDynamics = inspectedVehicleConfig_.tireDynamics;
            beginCard("vehicleProfileTireDynamics", "Tire Dynamics", "Front/rear slip persistence and transient grip loss", accentSecondary);
            applyDragFloatEdit("Front Grip Bias", "##vehicleProfileTireDynamicsFrontGripBias", tireDynamics.frontGripBias, 0.01f, 0.05f, 5.0f);
            applyDragFloatEdit("Rear Grip Bias", "##vehicleProfileTireDynamicsRearGripBias", tireDynamics.rearGripBias, 0.01f, 0.05f, 5.0f);
            applyDragFloatEdit("Lateral Relaxation Rate", "##vehicleProfileTireDynamicsRelaxation", tireDynamics.lateralRelaxationRate, 0.05f, 0.0f, 50.0f);
            applyDragFloatEdit("Grip Recovery Rate", "##vehicleProfileTireDynamicsRecovery", tireDynamics.gripRecoveryRate, 0.05f, 0.0f, 50.0f);
            applyDragFloatEdit("Slide Friction", "##vehicleProfileTireDynamicsSlideFriction", tireDynamics.slideFriction, 0.05f, 0.0f, 50.0f);
            applyDragFloatEdit("Max Side Slip Speed Scale", "##vehicleProfileTireDynamicsMaxSideSlip", tireDynamics.maxSideSlipSpeedScale, 0.01f, 0.0f, 1.0f);
            applyDragFloatEdit("Lift-Off Rear Grip Loss", "##vehicleProfileTireDynamicsLiftOffLoss", tireDynamics.liftOffRearGripLoss, 0.01f, 0.0f, 2.0f);
            applyDragFloatEdit("Brake Rear Grip Loss", "##vehicleProfileTireDynamicsBrakeLoss", tireDynamics.brakeRearGripLoss, 0.01f, 0.0f, 2.0f);
            applyDragFloatEdit("Throttle Rear Grip Loss", "##vehicleProfileTireDynamicsThrottleLoss", tireDynamics.throttleRearGripLoss, 0.01f, 0.0f, 2.0f);
            applyDragFloatEdit("Handbrake Rear Grip Loss", "##vehicleProfileTireDynamicsHandbrakeLoss", tireDynamics.handbrakeRearGripLoss, 0.01f, 0.0f, 2.0f);
            applyDragFloatEdit("Over-Speed Grip Loss", "##vehicleProfileTireDynamicsOverSpeedLoss", tireDynamics.overSpeedGripLoss, 0.01f, 0.0f, 2.0f);
            applyDragFloatEdit("Yaw From Rear Slip", "##vehicleProfileTireDynamicsYawRear", tireDynamics.yawFromRearSlip, 0.5f, 0.0f, 500.0f);
            applyDragFloatEdit("Yaw From Front Slip", "##vehicleProfileTireDynamicsYawFront", tireDynamics.yawFromFrontSlip, 0.5f, 0.0f, 500.0f);
            applyDragFloatEdit("Yaw Inertia Scale", "##vehicleProfileTireDynamicsYawInertia", tireDynamics.yawInertiaScale, 0.05f, 0.1f, 20.0f);
            applyDragFloatEdit("Yaw Drag", "##vehicleProfileTireDynamicsYawDrag", tireDynamics.yawDrag, 0.05f, 0.0f, 50.0f);
            applyDragFloatEdit("Counter-Steer Torque", "##vehicleProfileTireDynamicsCounterTorque", tireDynamics.counterSteerTorque, 0.01f, 0.0f, 10.0f);
            applyDragFloatEdit("Tire Scrub", "##vehicleProfileTireDynamicsTireScrub", tireDynamics.tireScrub, 0.05f, 0.0f, 50.0f);
            applyDragFloatEdit("Velocity Alignment Rate", "##vehicleProfileTireDynamicsVelocityAlign", tireDynamics.velocityAlignmentRate, 0.05f, 0.0f, 50.0f);
            applyDragFloatEdit("Rear Slip Yaw Torque", "##vehicleProfileTireDynamicsRearTorque", tireDynamics.rearSlipYawTorque, 0.01f, 0.0f, 10.0f);
            applyDragFloatEdit("Front Slip Yaw Damping", "##vehicleProfileTireDynamicsFrontDamping", tireDynamics.frontSlipYawDamping, 0.01f, 0.0f, 10.0f);
            applyDragFloatEdit("Brake Yaw Instability", "##vehicleProfileTireDynamicsBrakeYaw", tireDynamics.brakeYawInstability, 0.01f, 0.0f, 10.0f);
            applyDragFloatEdit("Load Memory", "##vehicleProfileTireDynamicsLoadMemory", tireDynamics.loadMemory, 0.01f, 0.0f, 10.0f);
            endCard();
        };
        auto renderLoadTransferSection = [&]() {
            raceman::physics::VehicleLoadTransferConfig& loadTransfer = inspectedVehicleConfig_.loadTransfer;
            beginCard("vehicleProfileLoadTransfer", "Load Transfer / Aero", "Visual weight shift and speed grip", accentPrimary);
            applyCheckboxEdit("Enabled##vehicleProfileLoadTransferEnabled", loadTransfer.enabled);
            applyDragFloatEdit("Brake Pitch Amount (deg)", "##vehicleProfileLoadTransferBrakePitch", loadTransfer.brakePitchAmount, 0.05f, 0.0f, 20.0f);
            applyDragFloatEdit("Throttle Squat Amount (deg)", "##vehicleProfileLoadTransferThrottleSquat", loadTransfer.throttleSquatAmount, 0.05f, 0.0f, 20.0f);
            applyDragFloatEdit("Lateral Roll Amount (deg)", "##vehicleProfileLoadTransferLateralRoll", loadTransfer.lateralRollAmount, 0.05f, 0.0f, 30.0f);
            applyDragFloatEdit("Load Grip Effect", "##vehicleProfileLoadTransferGripEffect", loadTransfer.loadGripEffect, 0.01f, 0.0f, 2.0f);
            applyDragFloatEdit("Aero Downforce", "##vehicleProfileLoadTransferAeroDownforce", loadTransfer.aeroDownforce, 0.01f, 0.0f, 10.0f);
            applyDragFloatEdit("Aero Balance (Front)", "##vehicleProfileLoadTransferAeroBalance", loadTransfer.aeroBalance, 0.01f, 0.0f, 1.0f);
            applyDragFloatEdit("Max Aero Grip Boost", "##vehicleProfileLoadTransferMaxAeroGripBoost", loadTransfer.maxAeroGripBoost, 0.01f, 0.0f, 5.0f);
            applyDragFloatEdit("Visual Smoothing", "##vehicleProfileLoadTransferVisualSmoothing", loadTransfer.visualSmoothing, 0.1f, 0.0f, 100.0f);
            endCard();
        };
        auto renderYawDynamicsSection = [&]() {
            raceman::physics::VehicleYawDynamicsConfig& yawDynamics = inspectedVehicleConfig_.yawDynamics;
            beginCard("vehicleProfileYawDynamics", "Yaw Dynamics", "Oversteer and spin response from tire slip", accentSecondary);
            applyCheckboxEdit("Enabled##vehicleProfileYawDynamicsEnabled", yawDynamics.enabled);
            applyDragFloatEdit("Min Speed (m/s)", "##vehicleProfileYawDynamicsMinSpeed", yawDynamics.minSpeed, 0.1f, 0.0f, 100.0f);
            applyDragFloatEdit("Steering Yaw Response", "##vehicleProfileYawDynamicsSteeringResponse", yawDynamics.steeringYawResponse, 0.5f, 0.0f, 500.0f);
            applyDragFloatEdit("Slip Yaw Response", "##vehicleProfileYawDynamicsSlipResponse", yawDynamics.slipYawResponse, 0.5f, 0.0f, 500.0f);
            applyDragFloatEdit("Max Yaw Rate", "##vehicleProfileYawDynamicsMaxRate", yawDynamics.maxYawRate, 0.5f, 1.0f, 1000.0f);
            applyDragFloatEdit("Yaw Damping", "##vehicleProfileYawDynamicsDamping", yawDynamics.yawDamping, 0.05f, 0.0f, 100.0f);
            applyDragFloatEdit("Counter-Steer Recovery", "##vehicleProfileYawDynamicsCounterSteer", yawDynamics.counterSteerRecovery, 0.01f, 1.0f, 10.0f);
            applyDragFloatEdit("Handbrake Rear Slip Boost", "##vehicleProfileYawDynamicsHandbrakeBoost", yawDynamics.handbrakeRearSlipBoost, 0.01f, 0.0f, 10.0f);
            applyDragFloatEdit("Throttle Rear Slip Boost", "##vehicleProfileYawDynamicsThrottleBoost", yawDynamics.throttleRearSlipBoost, 0.01f, 0.0f, 10.0f);
            applyDragFloatEdit("Spin Slip Angle (deg)", "##vehicleProfileYawDynamicsSpinSlip", yawDynamics.spinSlipAngle, 0.1f, 0.1f, 180.0f);
            applyDragFloatEdit("Spin Yaw Boost", "##vehicleProfileYawDynamicsSpinBoost", yawDynamics.spinYawBoost, 0.01f, 0.0f, 10.0f);
            applyDragFloatEdit("Spin Recovery", "##vehicleProfileYawDynamicsSpinRecovery", yawDynamics.spinRecovery, 0.05f, 0.0f, 100.0f);
            applyDragFloatEdit("Side Slip To Yaw", "##vehicleProfileYawDynamicsSideSlipToYaw", yawDynamics.sideSlipToYaw, 0.01f, 0.0f, 2.0f);
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

                renderGroundContactSection();
                renderArcadeHandlingSection();
                renderTireGripSection();
                renderTireDynamicsSection();
                renderLoadTransferSection();
                renderYawDynamicsSection();

                beginCard("vehicleProfileDriverAidsCard", "Driver Aids / Differential", "ABS, traction control and axle lock", accentPrimary);
                raceman::physics::VehicleBrakeAssistConfig& brakes = inspectedVehicleConfig_.brakes;
                raceman::physics::VehicleTractionControlConfig& tractionControl = inspectedVehicleConfig_.tractionControl;
                raceman::physics::DifferentialConfig& differential = inspectedVehicleConfig_.differential;
                ImGui::TextDisabled("Braking");
                if (ImGui::BeginTable("VehicleProfileBrakeAidGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("Front Bias", "##vehicleProfileBrakesFrontBias", brakes.frontBias, 0.01f, 0.0f, 1.0f);
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("Max Brake Force", "##vehicleProfileBrakesMaxForce", brakes.maxBrakeForce, 0.01f, 0.0f, 5.0f);
                    ImGui::TableNextColumn();
                    applyCheckboxEdit("ABS Enabled##vehicleProfileBrakesAbsEnabled", brakes.absEnabled);
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("ABS Slip Limit", "##vehicleProfileBrakesAbsSlipLimit", brakes.absSlipLimit, 0.01f, 0.01f, 5.0f);
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("ABS Release Rate", "##vehicleProfileBrakesAbsReleaseRate", brakes.absReleaseRate, 0.1f, 0.0f, 100.0f);
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("ABS Recover Rate", "##vehicleProfileBrakesAbsRecoverRate", brakes.absRecoverRate, 0.1f, 0.0f, 100.0f);
                    ImGui::EndTable();
                }
                ImGui::Spacing();
                ImGui::TextDisabled("Traction Control");
                if (ImGui::BeginTable("VehicleProfileTcAidGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    applyCheckboxEdit("TC Enabled##vehicleProfileTcEnabled", tractionControl.enabled);
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("Slip Limit", "##vehicleProfileTcSlipLimit", tractionControl.slipLimit, 0.01f, 0.01f, 5.0f);
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("Cut Strength", "##vehicleProfileTcCutStrength", tractionControl.cutStrength, 0.01f, 0.0f, 5.0f);
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("Recovery Rate", "##vehicleProfileTcRecoveryRate", tractionControl.recoveryRate, 0.1f, 0.0f, 100.0f);
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("Min Throttle Scale", "##vehicleProfileTcMinThrottle", tractionControl.minThrottleScale, 0.01f, 0.0f, 1.0f);
                    ImGui::TableNextColumn();
                    ImGui::Dummy(ImVec2(0.0f, 0.0f));
                    ImGui::EndTable();
                }
                ImGui::Spacing();
                ImGui::TextDisabled("Differential");
                applyDifferentialTypeEdit("Type##vehicleProfileDiffType", differential.type);
                applyDragFloatEdit("Torque Split (0-1)", "##vehicleProfileDiffTorqueSplit", inspectedVehicleConfig_.differential.torqueSplit, 0.01f, 0.0f, 1.0f);
                const float split = std::clamp(inspectedVehicleConfig_.differential.torqueSplit, 0.0f, 1.0f);
                const std::string splitLabel = std::to_string(static_cast<int>((1.0f - split) * 100.0f + 0.5f))
                    + "% rear / " + std::to_string(static_cast<int>(split * 100.0f + 0.5f)) + "% front";
                ImGui::ProgressBar(split, ImVec2(-1.0f, 0.0f), splitLabel.c_str());
                if (ImGui::BeginTable("VehicleProfileDiffGrid", 3, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("Lock Strength", "##vehicleProfileDiffLockStrength", differential.lockStrength, 0.01f, 0.0f, 1.0f);
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("Power Lock", "##vehicleProfileDiffPowerLock", differential.powerLock, 0.01f, 0.0f, 1.0f);
                    ImGui::TableNextColumn();
                    applyDragFloatEdit("Coast Lock", "##vehicleProfileDiffCoastLock", differential.coastLock, 0.01f, 0.0f, 1.0f);
                    ImGui::EndTable();
                }
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


} // namespace raceman
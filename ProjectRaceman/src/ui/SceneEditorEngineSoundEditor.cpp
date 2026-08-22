#include "SceneEditorInternal.h"
#include "../audio/AudioManager.h"
#include "../audio/EngineSoundProfile.h"
#include "../audio/EngineSynth.h"

#include <imgui/imgui.h>
#include <implot/implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

constexpr EngineLayoutPreset kLayoutPresets[] = {
    EngineLayoutPreset::I3, EngineLayoutPreset::I4, EngineLayoutPreset::I5, EngineLayoutPreset::I6,
    EngineLayoutPreset::Flat4, EngineLayoutPreset::Flat6, EngineLayoutPreset::V6,
    EngineLayoutPreset::V8CrossPlane, EngineLayoutPreset::V8FlatPlane,
    EngineLayoutPreset::V10, EngineLayoutPreset::V12, EngineLayoutPreset::VTwin90,
};

// Dominant order of a four-stroke: one firing event per cylinder per two revs.
float DominantOrder(const EngineSoundProfile& profile) {
    const float cylinders = static_cast<float>((std::max)(std::size_t(1), profile.cylinders.size()));
    return (profile.strokes == 2) ? cylinders : cylinders * 0.5f;
}

// Which preset does the current cylinder table correspond to, if any? Firing
// angles and bank assignment must both match, since that pair is exactly what
// separates a cross-plane V8 from a flat-plane one.
const char* MatchLayoutPresetName(const EngineSoundProfile& profile) {
    for (EngineLayoutPreset preset : kLayoutPresets) {
        const std::vector<EngineCylinder> candidate = MakeEngineLayout(preset);
        if (candidate.size() != profile.cylinders.size()) {
            continue;
        }
        bool match = true;
        for (std::size_t i = 0; i < candidate.size() && match; ++i) {
            if (std::fabs(candidate[i].fireAngleDeg - profile.cylinders[i].fireAngleDeg) > 0.5f ||
                candidate[i].bankId != profile.cylinders[i].bankId) {
                match = false;
            }
        }
        if (match) {
            return EngineLayoutPresetName(preset);
        }
    }
    return "Custom";
}

} // namespace

void SceneEditor::RenderEngineSoundEditorWindow() {
    if (!showEngineSoundEditor_) {
        return;
    }
    if (inspectedEngineSoundPath_.empty()) {
        showEngineSoundEditor_ = false;
        StopEngineSoundAudition();
        return;
    }

    if (!inspectedEngineSoundLoaded_) {
        inspectedEngineSoundError_.clear();
        try {
            inspectedEngineSound_ = EngineSoundProfileLoader::loadFromFile(
                ProjectAssetPathToAbsolute(inspectedEngineSoundPath_).string());
            inspectedEngineSoundLoaded_ = true;
            engineSoundProfileDirty_ = true;
        } catch (const std::exception& ex) {
            inspectedEngineSoundLoaded_ = false;
            inspectedEngineSoundError_ = ex.what();
        }
    }

    ImGui::SetNextWindowSize(ImVec2(840.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (engineSoundEditorFocusRequested_) {
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);

    if (!ImGui::Begin("Engine Sound Editor", &showEngineSoundEditor_, ImGuiWindowFlags_NoCollapse)) {
        engineSoundEditorHovered_ = false;
        engineSoundEditorFocused_ = false;
        ImGui::End();
        ImGui::PopStyleVar(3);
        engineSoundEditorFocusRequested_ = false;
        return;
    }

    if (!inspectedEngineSoundLoaded_) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load: %s",
                           inspectedEngineSoundError_.c_str());
        ImGui::End();
        ImGui::PopStyleVar(3);
        engineSoundEditorFocusRequested_ = false;
        return;
    }

    EngineSoundProfile& p = inspectedEngineSound_;

    // Same undo idiom as the other asset editors: capture once when a drag
    // begins, release when the widget is deactivated.
    auto beginEdit = [&]() {
        if (!engineSoundEditActive_) {
            PushEngineSoundUndoState();
            engineSoundEditActive_ = true;
        }
        engineSoundProfileDirty_ = true;
    };
    auto endEdit = [&]() {
        if (ImGui::IsItemDeactivated()) {
            engineSoundEditActive_ = false;
        }
    };
    auto dragFloat = [&](const char* label, float* value, float speed, float lo, float hi,
                         const char* fmt = "%.3f") {
        if (ImGui::DragFloat(label, value, speed, lo, hi, fmt)) {
            beginEdit();
            *value = (std::clamp)(*value, lo, hi);
        }
        endEdit();
    };

    // ----------------------------------------------------------------------
    // Audition strip
    // ----------------------------------------------------------------------
    ImGui::TextUnformatted(inspectedEngineSoundPath_.c_str());
    ImGui::Separator();

    const bool audioReady = audioManager_ != nullptr && audioManager_->IsInitialized();
    const bool auditioning = engineSoundAuditionVoice_ != nullptr;

    if (!audioReady) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f), "Audio engine unavailable - audition disabled.");
    }

    ImGui::BeginDisabled(!audioReady);
    if (ImGui::Button(auditioning ? "Stop##ESAudition" : "Audition##ESAudition", ImVec2(90.0f, 0.0f))) {
        if (auditioning) {
            StopEngineSoundAudition();
        } else {
            engineSoundAuditionSynth_ = std::make_shared<EngineSynth>();
            engineSoundProfileDirty_ = true;
            engineSoundAuditionVoice_ = audioManager_->CreateSynthVoice2D(engineSoundAuditionSynth_);
            if (engineSoundAuditionVoice_ == nullptr && console_ != nullptr) {
                console_->AddError("Could not start engine sound audition voice.");
                engineSoundAuditionSynth_.reset();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Sweep##ESAudition", ImVec2(80.0f, 0.0f))) {
        engineSoundAuditionSweep_ = true;
        engineSoundAuditionSweepT_ = 0.0f;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::DragFloat("Idle##ES", &engineSoundAuditionIdle_, 10.0f, 300.0f, 3000.0f, "%.0f rpm");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Redline##ES", &engineSoundAuditionRedline_, 50.0f, 2000.0f, 20000.0f, "%.0f rpm");

    engineSoundAuditionRedline_ = (std::max)(engineSoundAuditionIdle_ + 500.0f, engineSoundAuditionRedline_);

    if (engineSoundAuditionSweep_) {
        // idle -> redline under load -> lift -> back to idle
        engineSoundAuditionSweepT_ += ImGui::GetIO().DeltaTime / 6.0f;
        if (engineSoundAuditionSweepT_ >= 1.0f) {
            engineSoundAuditionSweep_ = false;
            engineSoundAuditionSweepT_ = 0.0f;
        } else {
            const float t = engineSoundAuditionSweepT_;
            if (t < 0.12f) {
                engineSoundAuditionRpm_ = engineSoundAuditionIdle_;
                engineSoundAuditionThrottle_ = 0.0f;
            } else if (t < 0.70f) {
                const float k = (t - 0.12f) / 0.58f;
                engineSoundAuditionRpm_ = engineSoundAuditionIdle_ + k * (engineSoundAuditionRedline_ - engineSoundAuditionIdle_);
                engineSoundAuditionThrottle_ = 1.0f;
            } else if (t < 0.80f) {
                engineSoundAuditionRpm_ = engineSoundAuditionRedline_;
                engineSoundAuditionThrottle_ = 0.0f;
            } else {
                const float k = (t - 0.80f) / 0.20f;
                engineSoundAuditionRpm_ = engineSoundAuditionRedline_ - k * (engineSoundAuditionRedline_ - engineSoundAuditionIdle_);
                engineSoundAuditionThrottle_ = 0.0f;
            }
        }
    }

    ImGui::SetNextItemWidth(-160.0f);
    ImGui::SliderFloat("RPM##ES", &engineSoundAuditionRpm_, engineSoundAuditionIdle_,
                       engineSoundAuditionRedline_, "%.0f");
    ImGui::SetNextItemWidth(-160.0f);
    ImGui::SliderFloat("Throttle / load##ES", &engineSoundAuditionThrottle_, 0.0f, 1.0f, "%.2f");

    // Live-apply to any vehicle already playing this profile, so tuning is heard
    // on the car itself and not only through the audition voice.
    if (engineSoundProfileDirty_ && scriptsRunning_) {
        ApplyEngineSoundEditsToRuntime();
    }

    // Push the current profile and parameters to the live voice.
    if (engineSoundAuditionSynth_) {
        if (engineSoundProfileDirty_) {
            engineSoundAuditionSynth_->SetProfile(EngineSynthBaked::Bake(
                p, engineSoundAuditionIdle_, engineSoundAuditionRedline_, kEngineSynthSampleRate));
        }
        EngineSynthParams params;
        params.rpm = engineSoundAuditionRpm_;
        params.idleRpm = engineSoundAuditionIdle_;
        params.redlineRpm = engineSoundAuditionRedline_;
        params.load = engineSoundAuditionThrottle_;
        params.throttle = engineSoundAuditionThrottle_;
        params.volume = 1.0f;
        engineSoundAuditionSynth_->SetParams(params);

        const float crankHz = engineSoundAuditionRpm_ / 60.0f;
        ImGui::Text("crank %.1f Hz   firing %.1f Hz   dominant order %.1f   peak %.3f",
                    crankHz, crankHz * DominantOrder(p), DominantOrder(p),
                    engineSoundAuditionSynth_->GetLastPeak());
    } else {
        const float crankHz = engineSoundAuditionRpm_ / 60.0f;
        ImGui::TextDisabled("crank %.1f Hz   firing %.1f Hz   dominant order %.1f",
                            crankHz, crankHz * DominantOrder(p), DominantOrder(p));
    }

    engineSoundProfileDirty_ = false;

    ImGui::Separator();

    // ----------------------------------------------------------------------
    // Tabs
    // ----------------------------------------------------------------------
    // Remember which tab the panel was left on. The pending name is consumed on
    // the first frame after a restore, so tab clicks behave normally after that.
    auto beginTab = [&](const char* name) {
        const ImGuiTabItemFlags flags = (engineSoundEditorPendingTab_ == name)
                                            ? ImGuiTabItemFlags_SetSelected
                                            : ImGuiTabItemFlags_None;
        if (!ImGui::BeginTabItem(name, nullptr, flags)) {
            return false;
        }
        engineSoundEditorActiveTab_ = name;
        return true;
    };

    if (ImGui::BeginTabBar("EngineSoundTabs")) {

        if (beginTab("Engine")) {
            ImGui::TextDisabled("Firing layout decides the engine's character more than any filter.");
            ImGui::Spacing();

            int strokes = p.strokes;
            if (ImGui::RadioButton("4-stroke", strokes == 4)) { beginEdit(); p.strokes = 4; }
            ImGui::SameLine();
            if (ImGui::RadioButton("2-stroke", strokes == 2)) { beginEdit(); p.strokes = 2; }

            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("Layout preset", MatchLayoutPresetName(p))) {
                const char* activeName = MatchLayoutPresetName(p);
                for (EngineLayoutPreset preset : kLayoutPresets) {
                    const bool selected = std::strcmp(activeName, EngineLayoutPresetName(preset)) == 0;
                    if (ImGui::Selectable(EngineLayoutPresetName(preset), selected)) {
                        PushEngineSoundUndoState();
                        p.cylinders = MakeEngineLayout(preset);
                        p.orders = MakeDefaultOrderBank(static_cast<int>(p.cylinders.size()),
                                                        engineSoundAuditionRedline_);
                        engineSoundProfileDirty_ = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%d cylinders)", static_cast<int>(p.cylinders.size()));

            ImGui::Spacing();
            dragFloat("Idle instability", &p.idleInstability, 0.001f, 0.0f, 0.25f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A dead-steady idle reads as synthetic.");
            dragFloat("Combustion variance", &p.combustionVariance, 0.005f, 0.0f, 1.0f);
            dragFloat("Combustion duration (ms)", &p.combustionDurationMs, 0.1f, 0.3f, 40.0f, "%.1f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Short bursts are clicks that make the pipes ring - the "
                                  "'tin can' sound. Raise this for a fatter thump.");
            }
            dragFloat("Combustion noise", &p.combustionNoise, 0.01f, 0.0f, 1.0f, "%.2f");
            dragFloat("Attack gain", &p.combustionAttackGain, 0.01f, 0.0f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The crack. A fast pressure-rise spike on top of the slower "
                                  "blowdown. Raise this if the engine sounds soft or breathy.");
            }
            dragFloat("Attack time (ms)", &p.combustionAttackMs, 0.01f, 0.05f, 10.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = pure thump, 1 = all hiss.");

            ImGui::Spacing();
            ImGui::TextDisabled("Cylinders - fire angle within the %.0f degree cycle, and which bank's runner it feeds",
                                p.strokes == 2 ? 360.0f : 720.0f);
            if (ImGui::BeginTable("EngineCylinderTable", 6,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26.0f);
                ImGui::TableSetupColumn("Fire angle");
                ImGui::TableSetupColumn("Bank", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Gain");
                ImGui::TableSetupColumn("Jitter");
                ImGui::TableSetupColumn("Primary");
                ImGui::TableHeadersRow();

                const float cycle = (p.strokes == 2) ? 360.0f : 720.0f;
                for (std::size_t i = 0; i < p.cylinders.size(); ++i) {
                    EngineCylinder& cyl = p.cylinders[i];
                    ImGui::TableNextRow();
                    ImGui::PushID(static_cast<int>(i));

                    ImGui::TableNextColumn();
                    ImGui::Text("%d", static_cast<int>(i) + 1);

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    dragFloat("##angle", &cyl.fireAngleDeg, 1.0f, 0.0f, cycle, "%.0f deg");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::DragInt("##bank", &cyl.bankId, 0.05f, 0, 1)) {
                        beginEdit();
                        cyl.bankId = (std::clamp)(cyl.bankId, 0, 1);
                    }
                    endEdit();

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    dragFloat("##gain", &cyl.gain, 0.01f, 0.0f, 2.0f, "%.2f");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    dragFloat("##jitter", &cyl.timingJitter, 0.05f, 0.0f, 10.0f, "%.1f");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    dragFloat("##primary", &cyl.runnerLengthScale, 0.005f, 0.1f, 4.0f, "%.2f");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("This cylinder's header primary, relative to the bank runner. "
                                          "All 1.00 = equal-length race headers and identical-sounding "
                                          "cylinders; spread them for the unequal-length rumble.");
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (beginTab("Exhaust")) {
            ImGui::TextDisabled("Each bank has its own runner; they merge at the collector.");
            ImGui::Spacing();
            dragFloat("Runner length (m)", &p.exhaust.runnerLengthM, 0.01f, 0.05f, 3.0f, "%.2f");
            dragFloat("Collector length (m)", &p.exhaust.collectorLengthM, 0.01f, 0.05f, 4.0f, "%.2f");
            dragFloat("Gas speed (m/s)", &p.exhaust.gasSpeedMs, 1.0f, 200.0f, 700.0f, "%.0f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Hot exhaust gas carries sound faster than ambient air (~450 m/s).");
            }
            dragFloat("Runner reflection", &p.exhaust.runnerReflection, 0.01f, 0.0f, 0.95f, "%.2f");
            dragFloat("Runner damping", &p.exhaust.runnerDamping, 0.01f, 0.0f, 0.99f, "%.2f");
            ImGui::Spacing();
            int stages = p.exhaust.mufflerStages;
            if (ImGui::SliderInt("Muffler stages", &stages, 0, 4)) {
                beginEdit();
                p.exhaust.mufflerStages = stages;
            }
            endEdit();
            dragFloat("Muffler length (m)", &p.exhaust.mufflerLengthM, 0.01f, 0.05f, 2.0f, "%.2f");
            dragFloat("Muffler reflection", &p.exhaust.mufflerReflection, 0.01f, 0.0f, 0.95f, "%.2f");
            dragFloat("Tailpipe brightness", &p.exhaust.tailpipeBrightness, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::EndTabItem();
        }

        if (beginTab("Intake")) {
            dragFloat("Length (m)", &p.intake.lengthM, 0.01f, 0.05f, 2.0f, "%.2f");
            dragFloat("Air speed (m/s)", &p.intake.airSpeedMs, 1.0f, 200.0f, 500.0f, "%.0f");
            dragFloat("Reflection", &p.intake.reflection, 0.01f, 0.0f, 0.95f, "%.2f");
            dragFloat("Damping", &p.intake.damping, 0.01f, 0.0f, 0.99f, "%.2f");
            dragFloat("Air noise", &p.intake.noise, 0.01f, 0.0f, 2.0f, "%.2f");
            dragFloat("Pulse gain", &p.intake.pulseGain, 0.01f, 0.0f, 2.0f, "%.2f");
            ImGui::EndTabItem();
        }

        if (beginTab("Orders")) {
            ImGui::TextDisabled("Sinusoids at multiples of crank frequency. The dominant order is %.1f.",
                                DominantOrder(p));
            ImGui::TextDisabled("Two curves per order: on-load and off-load, crossfaded by engine load.");
            ImGui::Spacing();

            if (ImGui::Button("Reset to layout default")) {
                PushEngineSoundUndoState();
                p.orders = MakeDefaultOrderBank(static_cast<int>(p.cylinders.size()),
                                                engineSoundAuditionRedline_);
                engineSoundProfileDirty_ = true;
            }

            engineSoundSelectedOrder_ = (std::clamp)(engineSoundSelectedOrder_, 0,
                                                     (std::max)(0, static_cast<int>(p.orders.size()) - 1));

            ImGui::BeginChild("OrderList", ImVec2(150.0f, 260.0f), true);
            for (std::size_t i = 0; i < p.orders.size(); ++i) {
                char label[64];
                std::snprintf(label, sizeof(label), "order %.1f##o%d", p.orders[i].order, static_cast<int>(i));
                if (ImGui::Selectable(label, engineSoundSelectedOrder_ == static_cast<int>(i))) {
                    engineSoundSelectedOrder_ = static_cast<int>(i);
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();

            ImGui::BeginChild("OrderCurve", ImVec2(0.0f, 260.0f), false);
            if (!p.orders.empty()) {
                EngineOrder& order = p.orders[static_cast<std::size_t>(engineSoundSelectedOrder_)];
                ImGui::SetNextItemWidth(140.0f);
                dragFloat("Order", &order.order, 0.05f, 0.25f, 32.0f, "%.2f");
                ImGui::SameLine();
                ImGui::TextDisabled("= %.0f Hz at %.0f rpm",
                                    order.order * engineSoundAuditionRpm_ / 60.0f, engineSoundAuditionRpm_);

                // Draggable gain-vs-RPM curves. ImPlot is already initialised for
                // the whole editor session, so this needs no new dependency.
                if (ImPlot::BeginPlot("##OrderGain", ImVec2(-1.0f, 200.0f))) {
                    ImPlot::SetupAxes("rpm", "gain");
                    ImPlot::SetupAxesLimits(engineSoundAuditionIdle_, engineSoundAuditionRedline_,
                                            0.0, 1.25, ImPlotCond_Always);

                    auto plotCurve = [&](const char* name, std::vector<EngineCurvePoint>& curve, int idBase) {
                        if (curve.empty()) return;
                        std::vector<double> xs, ys;
                        xs.reserve(curve.size());
                        ys.reserve(curve.size());
                        for (const EngineCurvePoint& point : curve) {
                            xs.push_back(point.rpm);
                            ys.push_back(point.value);
                        }
                        ImPlot::PlotLine(name, xs.data(), ys.data(), static_cast<int>(xs.size()));
                        for (std::size_t i = 0; i < curve.size(); ++i) {
                            double x = curve[i].rpm;
                            double y = curve[i].value;
                            if (ImPlot::DragPoint(idBase + static_cast<int>(i), &x, &y,
                                                  ImVec4(1, 1, 1, 0.7f), 4.0f)) {
                                beginEdit();
                                curve[i].rpm = static_cast<float>((std::clamp)(
                                    x, static_cast<double>(engineSoundAuditionIdle_),
                                    static_cast<double>(engineSoundAuditionRedline_)));
                                curve[i].value = static_cast<float>((std::clamp)(y, 0.0, 1.25));
                                engineSoundEditActive_ = false;
                            }
                        }
                    };
                    plotCurve("on load", order.gainOnLoad, 1000);
                    plotCurve("off load", order.gainOffLoad, 2000);
                    ImPlot::EndPlot();
                }
                ImGui::TextDisabled("Drag the points. On-load is what you hear pulling; off-load when coasting.");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (beginTab("Turbo")) {
            bool enabled = p.turbo.enabled;
            if (ImGui::Checkbox("Enabled", &enabled)) { beginEdit(); p.turbo.enabled = enabled; }
            bool super = p.turbo.supercharger;
            if (ImGui::Checkbox("Supercharger (no spool lag)", &super)) { beginEdit(); p.turbo.supercharger = super; }
            ImGui::BeginDisabled(!p.turbo.enabled);
            dragFloat("Whistle ratio", &p.turbo.whistleRatio, 0.1f, 1.0f, 60.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Whistle frequency = ratio x crank frequency.");
            dragFloat("Whistle gain", &p.turbo.whistleGain, 0.01f, 0.0f, 1.0f, "%.2f");
            dragFloat("Whistle resonance", &p.turbo.whistleResonance, 0.1f, 0.5f, 20.0f, "%.1f");
            dragFloat("Blow-off gain", &p.turbo.blowOffGain, 0.01f, 0.0f, 2.0f, "%.2f");
            dragFloat("Blow-off decay (s)", &p.turbo.blowOffDecaySeconds, 0.01f, 0.02f, 1.5f, "%.2f");
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        if (beginTab("Drivetrain")) {
            dragFloat("Gear whine gain", &p.drivetrain.whineGain, 0.005f, 0.0f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Straight-cut gearboxes sing; helical ones do not.");
            dragFloat("Whine ratio", &p.drivetrain.whineRatio, 0.1f, 1.0f, 80.0f, "%.1f");
            dragFloat("Whine resonance", &p.drivetrain.whineResonance, 0.1f, 0.5f, 20.0f, "%.1f");
            dragFloat("Shift cut depth", &p.drivetrain.shiftCutDepth, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::EndTabItem();
        }

        if (beginTab("Pops")) {
            bool enabled = p.overrun.enabled;
            if (ImGui::Checkbox("Overrun pops enabled", &enabled)) { beginEdit(); p.overrun.enabled = enabled; }
            ImGui::BeginDisabled(!p.overrun.enabled);
            dragFloat("Minimum rpm", &p.overrun.minRpm, 25.0f, 0.0f, 15000.0f, "%.0f");
            dragFloat("Density", &p.overrun.density, 0.01f, 0.0f, 1.0f, "%.2f");
            dragFloat("Gain", &p.overrun.gain, 0.01f, 0.0f, 2.0f, "%.2f");
            dragFloat("Limiter pop gain", &p.overrun.limiterPopGain, 0.01f, 0.0f, 2.0f, "%.2f");
            ImGui::EndDisabled();
            ImGui::TextDisabled("Pops are injected into the exhaust waveguide, so they travel out\n"
                                "through the muffler and tailpipe like any other pulse.");
            ImGui::EndTabItem();
        }

        if (beginTab("Body")) {
            ImGui::TextDisabled("Fixed low resonances. These do NOT track rpm, so the firing");
            ImGui::TextDisabled("harmonics sweep through them as the engine revs - that moving");
            ImGui::TextDisabled("relationship is what stops a sweep sounding like a siren.");
            ImGui::Spacing();
            dragFloat("Resonance 1 (Hz)", &p.body.resonance1Hz, 1.0f, 20.0f, 600.0f, "%.0f");
            dragFloat("Resonance 1 Q", &p.body.resonance1Q, 0.05f, 0.3f, 12.0f, "%.2f");
            dragFloat("Resonance 1 gain", &p.body.resonance1Gain, 0.01f, 0.0f, 2.0f, "%.2f");
            ImGui::Spacing();
            dragFloat("Resonance 2 (Hz)", &p.body.resonance2Hz, 1.0f, 20.0f, 900.0f, "%.0f");
            dragFloat("Resonance 2 Q", &p.body.resonance2Q, 0.05f, 0.3f, 12.0f, "%.2f");
            dragFloat("Resonance 2 gain", &p.body.resonance2Gain, 0.01f, 0.0f, 2.0f, "%.2f");
            ImGui::Spacing();
            dragFloat("Sub gain", &p.body.subGain, 0.01f, 0.0f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Low-passed firing energy. Adds weight and chest thump.");
            dragFloat("Sub cutoff (Hz)", &p.body.subCutoffHz, 1.0f, 20.0f, 400.0f, "%.0f");
            ImGui::Spacing();
            dragFloat("Tone tilt", &p.body.toneTilt, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::Spacing();
            ImGui::SeparatorText("Roar");
            ImGui::TextDisabled("Broadband gas noise gated by each firing event. Pipe resonance");
            ImGui::TextDisabled("alone gives discrete comb peaks - that is the 'tin can'. This is");
            ImGui::TextDisabled("what turns a hollow boing into a roar.");
            dragFloat("Roar gain", &p.roar.gain, 0.01f, 0.0f, 3.0f, "%.2f");
            dragFloat("Roar low (Hz)", &p.roar.lowHz, 1.0f, 20.0f, 800.0f, "%.0f");
            dragFloat("Roar high (Hz)", &p.roar.highHz, 5.0f, 60.0f, 6000.0f, "%.0f");
            dragFloat("Roar mod depth", &p.roar.modDepth, 0.01f, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = steady hiss, 1 = fully pulsed by each firing.");
            dragFloat("Growl", &p.roar.growl, 0.01f, 0.0f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Asymmetric saturation. Even harmonics fatten the low end.");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("0 = raw and bright, 1 = dark and distant. "
                                  "Raise this if the engine sounds sharp or fizzy.");
            }
            ImGui::EndTabItem();
        }

        if (beginTab("Space")) {
            ImGui::TextDisabled("A completely dry engine cannot sound natural. Outdoors you always");
            ImGui::TextDisabled("hear the ground bounce and nearby surfaces; without them the synth");
            ImGui::TextDisabled("sounds like it was recorded in a vacuum.");
            ImGui::Spacing();
            bool reverbEnabled = p.reverb.enabled;
            if (ImGui::Checkbox("Reverb enabled", &reverbEnabled)) { beginEdit(); p.reverb.enabled = reverbEnabled; }
            ImGui::BeginDisabled(!p.reverb.enabled);
            dragFloat("Early gain", &p.reverb.earlyGain, 0.01f, 0.0f, 1.5f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ground bounce and nearby surfaces. Gives the car size.");
            dragFloat("Early spread (ms)", &p.reverb.earlySpreadMs, 0.5f, 2.0f, 80.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far away those surfaces are.");
            ImGui::Spacing();
            dragFloat("Tail gain", &p.reverb.tailGain, 0.01f, 0.0f, 1.0f, "%.2f");
            dragFloat("Tail decay (s)", &p.reverb.tailDecaySeconds, 0.01f, 0.05f, 4.0f, "%.2f");
            dragFloat("Tail damping", &p.reverb.tailDamping, 0.01f, 0.0f, 0.95f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("High frequencies die first, as they do in open air.");
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        if (beginTab("Perspective")) {
            ImGui::TextDisabled("An engine heard from behind is mostly exhaust; from in front,");
            ImGui::TextDisabled("mostly intake. From far away it is mostly low frequencies and");
            ImGui::TextDisabled("reflections, because air eats the top end. Modelling none of");
            ImGui::TextDisabled("that is what makes an accurate synth sound glued to the camera.");
            ImGui::Spacing();
            bool perspEnabled = p.perspective.enabled;
            if (ImGui::Checkbox("Perspective mixing enabled", &perspEnabled)) {
                beginEdit();
                p.perspective.enabled = perspEnabled;
            }
            ImGui::BeginDisabled(!p.perspective.enabled);
            ImGui::SeparatorText("Directivity");
            dragFloat("Exhaust behind", &p.perspective.exhaustRear, 0.01f, 0.0f, 3.0f, "%.2f");
            dragFloat("Exhaust ahead", &p.perspective.exhaustFront, 0.01f, 0.0f, 3.0f, "%.2f");
            dragFloat("Intake behind", &p.perspective.intakeRear, 0.01f, 0.0f, 3.0f, "%.2f");
            dragFloat("Intake ahead", &p.perspective.intakeFront, 0.01f, 0.0f, 3.0f, "%.2f");
            dragFloat("Block falloff (m)", &p.perspective.blockFalloffMetres, 0.5f, 1.0f, 200.0f, "%.0f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Mechanical clatter is local and dies well before the exhaust does.");
            }
            ImGui::SeparatorText("Air and space");
            dragFloat("Air absorption (m)", &p.perspective.airAbsorptionMetres, 1.0f, 5.0f, 500.0f, "%.0f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Distance at which the top end is essentially gone.");
            }
            dragFloat("Reverb distance (m)", &p.perspective.reverbDistanceMetres, 1.0f, 2.0f, 300.0f, "%.0f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Distance at which the reverberant field dominates the direct sound.");
            }
            dragFloat("Doppler factor", &p.perspective.dopplerFactor, 0.01f, 0.0f, 3.0f, "%.2f");
            ImGui::SeparatorText("Octave / hood");
            ImGui::TextDisabled("Up close you sit among the cylinders. From outside, the bodywork");
            ImGui::TextDisabled("radiates the low orders and the firing detail is lost, so the");
            ImGui::TextDisabled("engine reads an octave bigger and deeper.");
            dragFloat("Octave tilt (m)", &p.perspective.octaveTiltMetres, 0.5f, 1.0f, 200.0f, "%.0f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance at which the outside balance is fully reached.");
            dragFloat("Octave tilt amount", &p.perspective.octaveTiltAmount, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();
            ImGui::TextDisabled("These only apply in play mode: the audition voice is 2D.");
            ImGui::EndTabItem();
        }

        if (beginTab("Mix")) {
            dragFloat("Exhaust", &p.mix.exhaustGain, 0.01f, 0.0f, 3.0f, "%.2f");
            dragFloat("Intake", &p.mix.intakeGain, 0.01f, 0.0f, 3.0f, "%.2f");
            dragFloat("Block", &p.mix.blockGain, 0.01f, 0.0f, 3.0f, "%.2f");
            dragFloat("Drive (soft clip)", &p.mix.drive, 0.01f, 0.0f, 2.0f, "%.2f");
            dragFloat("Master volume", &p.mix.masterVolume, 0.01f, 0.0f, 4.0f, "%.2f");
            ImGui::Spacing();
            dragFloat("Valvetrain gain", &p.noise.valvetrainGain, 0.005f, 0.0f, 1.0f, "%.3f");
            dragFloat("Valvetrain freq", &p.noise.valvetrainFreq, 25.0f, 200.0f, 12000.0f, "%.0f");
            dragFloat("Valvetrain Q", &p.noise.valvetrainQ, 0.05f, 0.1f, 10.0f, "%.2f");
            ImGui::Spacing();
            ImGui::SeparatorText("Legacy");
            ImGui::TextDisabled("3D settings and trigger clips now live on the VehicleSound");
            ImGui::TextDisabled("component: they describe how a particular car emits, not how");
            ImGui::TextDisabled("the engine sounds. These remain only so older scenes keep");
            ImGui::TextDisabled("working - edit them on the vehicle instead.");
            ImGui::BeginDisabled(true);
            ImGui::DragFloat("Spatial blend (legacy)", &p.spatialBlend, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::DragFloat("Min distance (legacy)", &p.minDistance, 0.1f, 0.1f, 100.0f, "%.1f");
            ImGui::DragFloat("Max distance (legacy)", &p.maxDistance, 1.0f, 1.0f, 1000.0f, "%.0f");
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        engineSoundEditorPendingTab_.clear();
    }

    // ----------------------------------------------------------------------
    // Footer
    // ----------------------------------------------------------------------
    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(90.0f, 0.0f))) {
        std::string error;
        const std::string absolutePath = ProjectAssetPathToAbsolute(inspectedEngineSoundPath_).string();
        if (EngineSoundProfileLoader::saveToFile(absolutePath, p, &error)) {
            if (console_) console_->AddLog("Saved engine sound profile: " + inspectedEngineSoundPath_);
        } else if (console_) {
            console_->AddError(error.empty() ? "Failed to save engine sound profile." : error);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(engineSoundUndoStack_.empty());
    if (ImGui::Button("Undo", ImVec2(70.0f, 0.0f))) UndoEngineSound();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(engineSoundRedoStack_.empty());
    if (ImGui::Button("Redo", ImVec2(70.0f, 0.0f))) RedoEngineSound();
    ImGui::EndDisabled();

    engineSoundEditorHovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    engineSoundEditorFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    ImGui::End();
    ImGui::PopStyleVar(3);
    engineSoundEditorFocusRequested_ = false;

    // The audition voice is only meaningful while the panel is open.
    if (!showEngineSoundEditor_) {
        StopEngineSoundAudition();
    }
}

} // namespace raceman

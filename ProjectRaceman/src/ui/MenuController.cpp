#include "MenuController.h"

#include "../rendering/Renderer.h"
#include "Console.h"
#include "NativeDialogs.h"

#include <imgui/imgui.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <utility>

namespace fs = std::filesystem;

namespace raceman {

namespace {
std::string SceneDisplayName(const std::string& scenePath) {
    std::string filename = fs::path(scenePath).filename().string();
    const std::string suffix = ".scene.json";
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower.size() >= suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
        filename.resize(filename.size() - suffix.size());
        filename += ".scene";
    }
    return filename;
}

void ApplyGraphicsPreset(GraphicsProfile& profile, GraphicsQualityTier tier) {
    profile.quality = tier;
    profile.ssaoDebugView = false;
    profile.motionBlurDebugView = false;
    profile.taaDebugView = false;
    profile.ssrDebugView = false;
    profile.shadowCascadeDebugView = false;
    profile.iblDebugMode = 0;
    profile.lod = true;
    profile.particles = true;
    profile.colorGrading = true;
    profile.colorSaturation = 1.0f;
    profile.colorContrast = 1.0f;
    profile.colorTemperature = 0.0f;
    profile.colorTint = 0.0f;
    profile.vignette = false;
    profile.filmGrain = false;
    profile.depthOfField = false;
    profile.dynamicResolutionTargetFps = 60;
    switch (tier) {
    case GraphicsQualityTier::Low:
        profile.antiAliasing = AntiAliasingMode::FXAA;
        profile.bloom = false;
        profile.motionBlur = false;
        profile.ssao = false;
        profile.shadows = true;
        profile.shadowResolution = 1024;
        profile.shadowSoftness = 1.0f;
        profile.shadowCascadeCount = 1;
        profile.shadowDistance = 60.0f;
        profile.localShadowLightLimit = 0;
        profile.reflections = false;
        profile.screenSpaceReflections = false;
        profile.dynamicResolution = true;
        profile.minimumResolutionScale = 0.625f;
        break;
    case GraphicsQualityTier::Medium:
        profile.antiAliasing = AntiAliasingMode::FXAA;
        profile.bloom = true;
        profile.bloomIntensity = 0.5f;
        profile.motionBlur = false;
        profile.ssao = true;
        profile.ssaoIntensity = 0.8f;
        profile.shadows = true;
        profile.shadowResolution = 1024;
        profile.shadowSoftness = 1.5f;
        profile.shadowCascadeCount = 2;
        profile.shadowDistance = 100.0f;
        profile.localShadowLightLimit = 1;
        profile.reflections = true;
        profile.screenSpaceReflections = false;
        profile.dynamicResolution = true;
        profile.minimumResolutionScale = 0.75f;
        break;
    case GraphicsQualityTier::Ultra:
        profile.antiAliasing = AntiAliasingMode::TAA;
        profile.taaFeedback = 0.92f;
        profile.taaSharpness = 0.15f;
        profile.taaJitterStrength = 0.75f;
        profile.bloom = true;
        profile.bloomIntensity = 0.7f;
        profile.motionBlur = true;
        profile.motionBlurSamples = 20;
        profile.motionBlurMinimumVelocityPixels = 1.0f;
        profile.filmGrain = true;
        profile.filmGrainIntensity = 0.025f;
        profile.ssao = true;
        profile.ssaoIntensity = 1.0f;
        profile.shadows = true;
        profile.shadowResolution = 4096;
        profile.shadowSoftness = 2.5f;
        profile.shadowCascadeCount = 4;
        profile.shadowDistance = 250.0f;
        profile.localShadowLightLimit = 4;
        profile.reflections = true;
        profile.screenSpaceReflections = true;
        profile.ssrSteps = 64;
        profile.dynamicResolution = false;
        profile.minimumResolutionScale = 0.85f;
        break;
    case GraphicsQualityTier::High:
    default:
        profile.antiAliasing = AntiAliasingMode::TAA;
        profile.taaFeedback = 0.90f;
        profile.taaSharpness = 0.20f;
        profile.taaJitterStrength = 0.50f;
        profile.bloom = true;
        profile.bloomIntensity = 0.7f;
        profile.motionBlur = true;
        profile.motionBlurSamples = 12;
        profile.motionBlurMinimumVelocityPixels = 1.5f;
        profile.ssao = true;
        profile.ssaoIntensity = 1.0f;
        profile.shadows = true;
        profile.shadowResolution = 2048;
        profile.shadowSoftness = 2.0f;
        profile.shadowCascadeCount = 4;
        profile.shadowDistance = 150.0f;
        profile.localShadowLightLimit = 2;
        profile.reflections = true;
        profile.screenSpaceReflections = true;
        profile.ssrSteps = 40;
        profile.dynamicResolution = false;
        profile.minimumResolutionScale = 0.75f;
        break;
    }
}
} // namespace

MenuController::MenuController() { LoadState(); }
MenuController::~MenuController() {
    if (folderPickerThread_ && folderPickerThread_->joinable()) {
        folderPickerThread_->join();
    }
    SaveState();
}

void MenuController::SetProjectSkyboxFaces(const SkyboxFaces& faces) {
    selectedSkyboxFaces_ = faces;
    selectedSkyboxFolder_.clear();
    skyboxSelectionError_.clear();

    bool complete = true;
    bool filesExist = true;
    bool haveFolder = false;
    bool sameFolder = true;
    fs::path commonFolder;
    for (const std::string& face : selectedSkyboxFaces_) {
        if (face.empty()) {
            complete = false;
            filesExist = false;
            continue;
        }

        const fs::path facePath(face);
        std::error_code errorCode;
        if (!fs::is_regular_file(facePath, errorCode)) {
            filesExist = false;
        }

        const fs::path folder = facePath.parent_path();
        if (!haveFolder) {
            commonFolder = folder;
            haveFolder = true;
        } else if (commonFolder != folder) {
            sameFolder = false;
        }
    }

    if (haveFolder && sameFolder) {
        selectedSkyboxFolder_ = commonFolder.string();
    }
    hasSelectedSkyboxFaces_ = complete && filesExist;
    selectedSkyboxSaved_ = complete;
    if (complete && !filesExist) {
        skyboxSelectionError_ = "The saved skybox references one or more missing image files.";
    } else if (!complete && std::any_of(
                   selectedSkyboxFaces_.begin(), selectedSkyboxFaces_.end(),
                   [](const std::string& face) { return !face.empty(); })) {
        skyboxSelectionError_ = "The saved skybox is incomplete. Assign all six faces.";
    }
}

void MenuController::UndoGraphicsSettings(Renderer& renderer) {
    if (graphicsEditActive_) {
        graphicsRedoStack_.push_back(renderer.GetSettings());
        renderer.GetSettings() = graphicsEditStart_;
        graphicsEditActive_ = false;
        if (graphicsChangedCallback_) graphicsChangedCallback_();
        return;
    }
    if (graphicsUndoStack_.empty()) return;

    graphicsRedoStack_.push_back(renderer.GetSettings());
    renderer.GetSettings() = graphicsUndoStack_.back();
    graphicsUndoStack_.pop_back();
    if (graphicsChangedCallback_) graphicsChangedCallback_();
}

void MenuController::RedoGraphicsSettings(Renderer& renderer) {
    if (graphicsRedoStack_.empty()) return;

    graphicsUndoStack_.push_back(renderer.GetSettings());
    renderer.GetSettings() = graphicsRedoStack_.back();
    graphicsRedoStack_.pop_back();
    if (graphicsChangedCallback_) graphicsChangedCallback_();
}

void MenuController::Render(Renderer& renderer,
                            bool vsyncEnabled,
                            const std::function<void(bool)>& setVSync,
                            bool profilerVisible,
                            const std::function<void(bool)>& setProfilerVisible,
                            const std::function<void()>& onAddMeshPlane,
                            Console* console,
                            EditorProjectMenu projectMenu,
                            const std::function<void(const SkyboxFaces&)>& onSkyboxChosen,
                            bool* frustumCullingEnabled,
                            bool* physicsCullingEnabled,
                            float* sceneCameraNearClip,
                            float* sceneCameraFarClip) {

    graphicsChangedCallback_ = projectMenu.onGraphicsSettingsChanged;
    projectSettingsShortcutTarget_ = false;

    // Tick async folder picker — fires onBuildProject once user picks a folder
    if (folderPickerState_ && folderPickerState_->isDone.load()) {
        if (folderPickerThread_ && folderPickerThread_->joinable()) {
            folderPickerThread_->join();
        }
        std::string folder;
        {
            std::lock_guard<std::mutex> lock(folderPickerState_->resultMutex);
            folder = folderPickerState_->result;
        }
        if (!folder.empty() && pendingBuildCallback_) {
            pendingBuildCallback_(folder);
        }
        folderPickerThread_.reset();
        folderPickerState_.reset();
        pendingBuildCallback_ = nullptr;
    }

    RenderMainMenu(onAddMeshPlane, projectMenu, profilerVisible, setProfilerVisible);

    if (showProjectSettings_) {
        if (ImGui::Begin("Project Settings", &showProjectSettings_, ImGuiWindowFlags_NoCollapse)) {
            const int previousProjectSettingsTab = selectedProjectSettingsTab_;
            if (ImGui::BeginTabBar("GlobalProjectSettingsTabs")) {
                ImGuiTabItemFlags renderingTabFlags = restoreProjectSettingsTab_ && selectedProjectSettingsTab_ == 0 ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem("Rendering", nullptr, renderingTabFlags)) {
                    selectedProjectSettingsTab_ = 0;
                    auto& settings = renderer.GetSettings();
                    const RendererSettings graphicsBeforeFrame = settings;
                    bool graphicsChanged = false;
                    graphicsChanged |= ImGui::ColorEdit3("Ambient Light", &settings.profile.ambientColor.x);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Project-wide ambient light. Game background color is configured on each Camera.");
                    }

                    ImGui::Separator();
                    ImGui::TextUnformatted("Graphics Profile");
                    const char* styleNames[] = {"Realistic", "Stylized"};
                    int styleIndex = settings.profile.style == RenderStyle::Stylized ? 1 : 0;
                    if (ImGui::Combo("Render Style", &styleIndex, styleNames, 2)) {
                        settings.profile.style = styleIndex == 1 ? RenderStyle::Stylized : RenderStyle::Realistic;
                        graphicsChanged = true;
                    }
                    const char* qualityNames[] = {"Low", "Medium", "High", "Ultra"};
                    int qualityIndex = static_cast<int>(settings.profile.quality);
                    if (ImGui::Combo("Quality Tier", &qualityIndex, qualityNames, 4)) {
                        ApplyGraphicsPreset(settings.profile, static_cast<GraphicsQualityTier>(qualityIndex));
                        graphicsChanged = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Applies a complete performance preset. Individual controls can be customized afterward.");
                    }
                    const char* aaNames[] = {"None", "FXAA", "TAA"};
                    int aaIndex = (std::min)(2, static_cast<int>(settings.profile.antiAliasing));
                    if (ImGui::Combo("Anti-Aliasing", &aaIndex, aaNames, 3)) {
                        settings.profile.antiAliasing = static_cast<AntiAliasingMode>(aaIndex);
                        graphicsChanged = true;
                    }
                    if (settings.profile.antiAliasing == AntiAliasingMode::TAA) {
                        graphicsChanged |= ImGui::SliderFloat("TAA History", &settings.profile.taaFeedback, 0.0f, 0.98f, "%.2f");
                        graphicsChanged |= ImGui::SliderFloat("TAA Sharpness", &settings.profile.taaSharpness, 0.0f, 1.0f, "%.2f");
                        graphicsChanged |= ImGui::SliderFloat("TAA Jitter Strength", &settings.profile.taaJitterStrength, 0.0f, 1.0f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Subpixel sampling amount. 0.5 is the stable default; Ultra uses 0.75.");
                        }
                        if (ImGui::Checkbox("TAA Debug View", &settings.profile.taaDebugView)) {
                            if (settings.profile.taaDebugView) {
                                settings.profile.ssaoDebugView = false;
                                settings.profile.motionBlurDebugView = false;
                                settings.profile.shadowCascadeDebugView = false;
                                settings.profile.iblDebugMode = 0;
                                settings.profile.ssrDebugView = false;
                            }
                            graphicsChanged = true;
                        }
                    }
                    graphicsChanged |= ImGui::DragFloat("Exposure", &settings.profile.exposure, 0.02f, 0.05f, 8.0f, "%.2f");
                    const char* outputModeNames[] = {"SDR (sRGB)", "HDR (scRGB linear)"};
                    int outputMode = settings.profile.hdr ? 1 : 0;
                    if (ImGui::Combo("Output Mode", &outputMode, outputModeNames, 2)) {
                        settings.profile.hdr = outputMode == 1;
                        graphicsChanged = true;
                    }
                    ImGui::BeginDisabled(!settings.profile.hdr);
                    if (ImGui::SliderFloat("HDR Paper White", &settings.profile.hdrPaperWhiteNits,
                        80.0f, 500.0f, "%.0f nits")) {
                        settings.profile.hdrPeakBrightnessNits = (std::max)(
                            settings.profile.hdrPeakBrightnessNits, settings.profile.hdrPaperWhiteNits);
                        graphicsChanged = true;
                    }
                    graphicsChanged |= ImGui::SliderFloat("HDR Peak Brightness", &settings.profile.hdrPeakBrightnessNits,
                        settings.profile.hdrPaperWhiteNits, 4000.0f, "%.0f nits", ImGuiSliderFlags_Logarithmic);
                    ImGui::EndDisabled();
                    if (settings.profile.hdr) {
                        ImGui::TextDisabled("Output: RGBA16F linear scRGB (editor shows SDR preview)");
                    } else {
                        ImGui::TextDisabled("Output: RGBA8 display-referred sRGB");
                    }
                    const DisplayHdrCapabilities& hdrDisplay = renderer.GetDisplayHdrCapabilities();
                    if (!hdrDisplay.detected) {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                            "Display HDR: capability unavailable");
                    } else if (!hdrDisplay.hdrSupported) {
                        ImGui::TextDisabled("Display HDR: unsupported (%d-bit output)",
                            hdrDisplay.displayBitsPerColor);
                    } else if (!hdrDisplay.hdrEnabledInWindows) {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                            "Display HDR: supported but disabled in Windows");
                    } else {
                        ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f),
                            "Display HDR: enabled (%.0f nits, %d-bit)",
                            hdrDisplay.maximumLuminanceNits, hdrDisplay.displayBitsPerColor);
                    }
                    ImGui::TextDisabled("Window framebuffer: %d-bit; native HDR presenter: %s",
                        hdrDisplay.windowBitsPerColor,
                        hdrDisplay.nativePresentationAvailable ? "active" : "requires DXGI backend");
                    graphicsChanged |= ImGui::Checkbox("Bloom", &settings.profile.bloom);
                    ImGui::BeginDisabled(!settings.profile.bloom);
                    graphicsChanged |= ImGui::SliderFloat("Bloom Intensity", &settings.profile.bloomIntensity, 0.0f, 3.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderFloat("Bloom Threshold", &settings.profile.bloomThreshold, 0.0f, 8.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderFloat("Bloom Radius", &settings.profile.bloomRadius, 0.25f, 3.0f, "%.2f");
                    ImGui::EndDisabled();
                    ImGui::SeparatorText("Post Processing");
                    graphicsChanged |= ImGui::Checkbox("Motion Blur", &settings.profile.motionBlur);
                    ImGui::BeginDisabled(!settings.profile.motionBlur);
                    graphicsChanged |= ImGui::SliderFloat("Shutter Angle", &settings.profile.motionBlurShutterAngle, 0.0f, 360.0f, "%.0f deg");
                    graphicsChanged |= ImGui::SliderFloat("Motion Blur Intensity", &settings.profile.motionBlurIntensity, 0.0f, 2.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderInt("Motion Blur Samples", &settings.profile.motionBlurSamples, 4, 32);
                    graphicsChanged |= ImGui::SliderFloat("Maximum Blur Radius", &settings.profile.motionBlurMaxRadius, 1.0f, 64.0f, "%.0f px");
                    graphicsChanged |= ImGui::SliderFloat("Motion Blur Dead Zone", &settings.profile.motionBlurMinimumVelocityPixels, 0.0f, 8.0f, "%.1f px");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Ignores tiny screen-space movement from camera and physics jitter.");
                    }
                    if (ImGui::Checkbox("Motion Vector Debug View", &settings.profile.motionBlurDebugView)) {
                        if (settings.profile.motionBlurDebugView) {
                            settings.profile.ssaoDebugView = false;
                            settings.profile.shadowCascadeDebugView = false;
                            settings.profile.iblDebugMode = 0;
                            settings.profile.ssrDebugView = false;
                            settings.profile.taaDebugView = false;
                        }
                        graphicsChanged = true;
                    }
                    ImGui::TextDisabled("Uses camera and per-object motion vectors.");
                    ImGui::EndDisabled();
                    graphicsChanged |= ImGui::Checkbox("Depth of Field", &settings.profile.depthOfField);
                    ImGui::BeginDisabled(!settings.profile.depthOfField);
                    graphicsChanged |= ImGui::SliderFloat("Focus Distance", &settings.profile.depthOfFieldFocusDistance, 0.05f, 500.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
                    graphicsChanged |= ImGui::SliderFloat("Focus Range", &settings.profile.depthOfFieldFocusRange, 0.05f, 100.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
                    graphicsChanged |= ImGui::SliderFloat("DoF Maximum Radius", &settings.profile.depthOfFieldMaxRadius, 0.5f, 24.0f, "%.1f px");
                    ImGui::EndDisabled();
                    graphicsChanged |= ImGui::Checkbox("Color Grading", &settings.profile.colorGrading);
                    ImGui::BeginDisabled(!settings.profile.colorGrading);
                    graphicsChanged |= ImGui::SliderFloat("Saturation", &settings.profile.colorSaturation, 0.0f, 2.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderFloat("Contrast", &settings.profile.colorContrast, 0.5f, 2.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderFloat("Temperature", &settings.profile.colorTemperature, -1.0f, 1.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderFloat("Tint", &settings.profile.colorTint, -1.0f, 1.0f, "%.2f");
                    ImGui::EndDisabled();
                    graphicsChanged |= ImGui::Checkbox("Vignette", &settings.profile.vignette);
                    ImGui::BeginDisabled(!settings.profile.vignette);
                    graphicsChanged |= ImGui::SliderFloat("Vignette Intensity", &settings.profile.vignetteIntensity, 0.0f, 1.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderFloat("Vignette Smoothness", &settings.profile.vignetteSmoothness, 0.05f, 1.0f, "%.2f");
                    ImGui::EndDisabled();
                    graphicsChanged |= ImGui::Checkbox("Film Grain", &settings.profile.filmGrain);
                    ImGui::BeginDisabled(!settings.profile.filmGrain);
                    graphicsChanged |= ImGui::SliderFloat("Film Grain Intensity", &settings.profile.filmGrainIntensity, 0.0f, 0.25f, "%.3f");
                    ImGui::EndDisabled();
                    graphicsChanged |= ImGui::Checkbox("SSAO", &settings.profile.ssao);
                    ImGui::BeginDisabled(!settings.profile.ssao);
                    graphicsChanged |= ImGui::SliderFloat("SSAO Intensity", &settings.profile.ssaoIntensity, 0.0f, 3.0f, "%.2f");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Scales ambient occlusion darkness. Values above 1 allow a stronger artistic effect.");
                    }
                    graphicsChanged |= ImGui::SliderFloat("SSAO Radius", &settings.profile.ssaoRadius, 0.05f, 5.0f, "%.2f m");
                    graphicsChanged |= ImGui::SliderFloat("SSAO Bias", &settings.profile.ssaoBias, 0.001f, 0.2f, "%.3f m", ImGuiSliderFlags_Logarithmic);
                    if (ImGui::Checkbox("SSAO Debug View", &settings.profile.ssaoDebugView)) {
                        if (settings.profile.ssaoDebugView) {
                            settings.profile.iblDebugMode = 0;
                            settings.profile.shadowCascadeDebugView = false;
                            settings.profile.motionBlurDebugView = false;
                            settings.profile.taaDebugView = false;
                            settings.profile.ssrDebugView = false;
                        }
                        graphicsChanged = true;
                    }
                    const int ssaoDivisor = settings.profile.quality == GraphicsQualityTier::Low ? 4
                        : (settings.profile.quality == GraphicsQualityTier::Ultra ? 1 : 2);
                    const int ssaoSamples = settings.profile.quality == GraphicsQualityTier::Low ? 8
                        : (settings.profile.quality == GraphicsQualityTier::Medium ? 16
                        : (settings.profile.quality == GraphicsQualityTier::Ultra ? 32 : 24));
                    ImGui::TextDisabled("Active SSAO: 1/%d resolution, %d samples", ssaoDivisor, ssaoSamples);
                    ImGui::EndDisabled();
                    graphicsChanged |= ImGui::Checkbox("Shadows", &settings.profile.shadows);
                    const int shadowResolutions[] = {0, 512, 1024, 2048, 4096};
                    const char* shadowResolutionNames[] = {"Follow Quality Tier", "512", "1024", "2048", "4096"};
                    int shadowResolutionIndex = 0;
                    for (int i = 1; i < 5; ++i) {
                        if (settings.profile.shadowResolution == shadowResolutions[i]) shadowResolutionIndex = i;
                    }
                    ImGui::BeginDisabled(!settings.profile.shadows);
                    if (ImGui::Combo("Shadow Resolution", &shadowResolutionIndex, shadowResolutionNames, 5)) {
                        settings.profile.shadowResolution = shadowResolutions[shadowResolutionIndex];
                        graphicsChanged = true;
                    }
                    ImGui::EndDisabled();
                    if (settings.profile.shadows && settings.profile.shadowResolution == 0) {
                        const int tierResolution = settings.profile.quality == GraphicsQualityTier::Low ? 1024
                            : (settings.profile.quality == GraphicsQualityTier::Medium ? 1536
                            : (settings.profile.quality == GraphicsQualityTier::Ultra ? 4096 : 2048));
                        ImGui::TextDisabled("Active shadow map: %d x %d", tierResolution, tierResolution);
                    }
                    ImGui::BeginDisabled(!settings.profile.shadows);
                    graphicsChanged |= ImGui::SliderFloat("Shadow Softness", &settings.profile.shadowSoftness, 0.0f, 8.0f, "%.1f texels");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("0 produces hard edges. Higher values widen the filtered shadow edge.");
                    }
                    const char* cascadeCountNames[] = {"1 (Legacy)", "2", "3", "4"};
                    int cascadeCountIndex = (std::clamp)(settings.profile.shadowCascadeCount, 1, 4) - 1;
                    if (ImGui::Combo("Shadow Cascades", &cascadeCountIndex, cascadeCountNames, 4)) {
                        settings.profile.shadowCascadeCount = cascadeCountIndex + 1;
                        graphicsChanged = true;
                    }
                    graphicsChanged |= ImGui::SliderFloat("Shadow Distance", &settings.profile.shadowDistance,
                        10.0f, 500.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
                    graphicsChanged |= ImGui::SliderInt("Local Shadow Lights", &settings.profile.localShadowLightLimit, 0, 4);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Maximum point and spot lights rendered into shadow maps. Point lights cost six faces each.");
                    }
                    if (ImGui::Checkbox("Shadow Cascade Debug View", &settings.profile.shadowCascadeDebugView)) {
                        if (settings.profile.shadowCascadeDebugView) {
                            settings.profile.ssaoDebugView = false;
                            settings.profile.iblDebugMode = 0;
                            settings.profile.motionBlurDebugView = false;
                            settings.profile.taaDebugView = false;
                            settings.profile.ssrDebugView = false;
                        }
                        graphicsChanged = true;
                    }
                    ImGui::EndDisabled();
                    if (settings.profile.shadows) {
                        ImGui::TextDisabled("Active shadow map: %d cascade layer%s",
                            settings.profile.shadowCascadeCount,
                            settings.profile.shadowCascadeCount == 1 ? "" : "s");
                    }
                    graphicsChanged |= ImGui::Checkbox("Reflections", &settings.profile.reflections);
                    ImGui::BeginDisabled(!settings.profile.reflections);
                    graphicsChanged |= ImGui::SliderFloat("Environment Lighting", &settings.profile.environmentIntensity, 0.0f, 4.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderFloat("Reflection Intensity", &settings.profile.reflectionIntensity, 0.0f, 4.0f, "%.2f");
                    graphicsChanged |= ImGui::Checkbox("Screen-Space Reflections", &settings.profile.screenSpaceReflections);
                    ImGui::BeginDisabled(!settings.profile.screenSpaceReflections);
                    graphicsChanged |= ImGui::SliderFloat("SSR Intensity", &settings.profile.ssrIntensity, 0.0f, 2.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderFloat("SSR Maximum Distance", &settings.profile.ssrMaxDistance, 1.0f, 200.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
                    graphicsChanged |= ImGui::SliderFloat("SSR Thickness", &settings.profile.ssrThickness, 0.01f, 2.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
                    graphicsChanged |= ImGui::SliderInt("SSR Steps", &settings.profile.ssrSteps, 8, 96);
                    if (ImGui::Checkbox("SSR Debug View", &settings.profile.ssrDebugView)) {
                        if (settings.profile.ssrDebugView) {
                            settings.profile.ssaoDebugView = false;
                            settings.profile.shadowCascadeDebugView = false;
                            settings.profile.motionBlurDebugView = false;
                            settings.profile.taaDebugView = false;
                            settings.profile.iblDebugMode = 0;
                        }
                        graphicsChanged = true;
                    }
                    ImGui::TextDisabled("SSR hits on-screen geometry; skybox IBL remains the fallback.");
                    ImGui::EndDisabled();
                    const char* iblDebugNames[] = {"Off", "Diffuse Irradiance", "Raw Environment", "Final Specular"};
                    if (ImGui::Combo("IBL Debug View", &settings.profile.iblDebugMode, iblDebugNames, 4)) {
                        if (settings.profile.iblDebugMode != 0) {
                            settings.profile.ssaoDebugView = false;
                            settings.profile.shadowCascadeDebugView = false;
                            settings.profile.motionBlurDebugView = false;
                            settings.profile.taaDebugView = false;
                            settings.profile.ssrDebugView = false;
                        }
                        graphicsChanged = true;
                    }
                    ImGui::EndDisabled();
                    if (!renderer.HasEnvironmentSource()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "IBL status: no valid skybox cubemap");
                    } else if (renderer.IsEnvironmentBakeReady()) {
                        ImGui::TextDisabled("IBL status: baked (irradiance %.3f)", renderer.GetEnvironmentAverageLuminance());
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "IBL status: using skybox mip fallback");
                    }
                    graphicsChanged |= ImGui::Checkbox("Weather", &settings.profile.weather);
                    ImGui::BeginDisabled(!settings.profile.weather);
                    graphicsChanged |= ImGui::SliderFloat("Weather Intensity", &settings.profile.weatherIntensity, 0.0f, 1.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderFloat("Weather Wind", &settings.profile.weatherWind, -2.0f, 2.0f, "%.2f");
                    ImGui::EndDisabled();
                    graphicsChanged |= ImGui::Checkbox("Particles", &settings.profile.particles);
                    if (settings.profile.weather && settings.profile.weatherIntensity > 0.0f) {
                        const char* density = settings.profile.quality == GraphicsQualityTier::Low ? "Low"
                            : (settings.profile.quality == GraphicsQualityTier::Medium ? "Medium"
                            : (settings.profile.quality == GraphicsQualityTier::Ultra ? "Ultra" : "High"));
                        ImGui::TextDisabled("Weather particles: %s%s", settings.profile.particles ? density : "Off",
                            settings.profile.particles ? " density" : "");
                    }
                    graphicsChanged |= ImGui::Checkbox("LOD", &settings.profile.lod);
                    graphicsChanged |= ImGui::Checkbox("Dynamic Resolution", &settings.profile.dynamicResolution);
                    ImGui::BeginDisabled(!settings.profile.dynamicResolution);
                    graphicsChanged |= ImGui::SliderFloat("Minimum Resolution Scale", &settings.profile.minimumResolutionScale, 0.5f, 1.0f, "%.2f");
                    graphicsChanged |= ImGui::SliderInt("Resolution Target FPS", &settings.profile.dynamicResolutionTargetFps, 30, 240);
                    ImGui::TextDisabled("Current scale: Scene %.0f%%, Game %.0f%%",
                        renderer.GetDynamicResolutionScale(ViewportRenderTarget::Scene) * 100.0f,
                        renderer.GetDynamicResolutionScale(ViewportRenderTarget::Game) * 100.0f);
                    ImGui::EndDisabled();
                    if (settings.profile.style == RenderStyle::Stylized) {
                        graphicsChanged |= ImGui::DragFloat("Lighting Bands", &settings.profile.stylizedBands, 0.25f, 2.0f, 12.0f, "%.1f");
                        graphicsChanged |= ImGui::DragFloat("Rim Strength", &settings.profile.stylizedRimStrength, 0.01f, 0.0f, 2.0f, "%.2f");
                    }

                    bool vs = vsyncEnabled;
                    if (ImGui::Checkbox("VSync", &vs)) {
                        if (setVSync) {
                            setVSync(vs);
                        }
                    }

                    ImGui::Separator();
                    ImGui::TextUnformatted("Optimizations");
                    if (frustumCullingEnabled) {
                        ImGui::Checkbox("Frustum Culling", frustumCullingEnabled);
                    }
                    graphicsChanged |= ImGui::Checkbox("Draw Call Sorting", &settings.enableDrawCallSorting);

                    ImGui::Separator();
                    ImGui::TextUnformatted("Scene Camera");
                    if (sceneCameraNearClip && sceneCameraFarClip) {
                        float nearClip = *sceneCameraNearClip;
                        if (ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.001f, 100000.0f)) {
                            *sceneCameraNearClip = (std::max)(0.001f, nearClip);
                            *sceneCameraFarClip = (std::max)(*sceneCameraNearClip + 0.001f, *sceneCameraFarClip);
                        }

                        float farClip = *sceneCameraFarClip;
                        if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, 0.002f, 1000000.0f)) {
                            *sceneCameraFarClip = (std::max)(*sceneCameraNearClip + 0.001f, farClip);
                        }
                    } else {
                        ImGui::TextDisabled("Scene camera clipping is unavailable.");
                    }

                    ImGui::Separator();
                    if (showSkybox_) {
                        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                    }
                    if (ImGui::CollapsingHeader("Skybox")) {
                        ImGui::TextWrapped("Browse to any face image in a six-image skybox. The other faces are detected from the same folder.");
                        ImGui::SetNextItemWidth(-110.0f);
                        ImGui::InputText("##skybox-folder", selectedSkyboxFolder_.data(), selectedSkyboxFolder_.size() + 1, ImGuiInputTextFlags_ReadOnly);
                        ImGui::SameLine();
                        if (ImGui::Button("Browse...##skybox")) {
                            const std::string selectedFile = PickImageFileDialog(L"Select any skybox face image");
                            if (!selectedFile.empty()) {
                                const std::string folder = fs::path(selectedFile).parent_path().string();
                                SkyboxFaces faces{};
                                std::string error;
                                if (TryBuildFacesFromFolder(folder, faces, error)) {
                                    selectedSkyboxFolder_ = folder;
                                    selectedSkyboxFaces_ = std::move(faces);
                                    hasSelectedSkyboxFaces_ = true;
                                    selectedSkyboxSaved_ = false;
                                    skyboxSelectionError_.clear();
                                    if (onSkyboxChosen) {
                                        onSkyboxChosen(selectedSkyboxFaces_);
                                        selectedSkyboxSaved_ = true;
                                    }
                                } else {
                                    selectedSkyboxFolder_ = folder;
                                    selectedSkyboxFaces_ = {};
                                    hasSelectedSkyboxFaces_ = false;
                                    selectedSkyboxSaved_ = false;
                                    skyboxSelectionError_ = std::move(error) + " Assign each face manually below.";
                                }
                            }
                        }
                        if (!skyboxSelectionError_.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", skyboxSelectionError_.c_str());
                        }

                        ImGui::SeparatorText("Faces");
                        const char* labels[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
                        const wchar_t* pickerTitles[6] = {
                            L"Select +X skybox face", L"Select -X skybox face",
                            L"Select +Y skybox face", L"Select -Y skybox face",
                            L"Select +Z skybox face", L"Select -Z skybox face",
                        };
                        for (int i = 0; i < 6; ++i) {
                            ImGui::PushID(i);
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted(labels[i]);
                            ImGui::SameLine(30.0f);
                            ImGui::SetNextItemWidth(-110.0f);
                            std::string& face = selectedSkyboxFaces_[static_cast<size_t>(i)];
                            ImGui::InputText("##face", face.data(), face.size() + 1, ImGuiInputTextFlags_ReadOnly);
                            ImGui::SameLine();
                            if (ImGui::Button("Browse...##face")) {
                                const std::string selectedFile = PickImageFileDialog(pickerTitles[i]);
                                if (!selectedFile.empty()) {
                                    face = selectedFile;
                                    selectedSkyboxSaved_ = false;
                                    hasSelectedSkyboxFaces_ = std::all_of(
                                        selectedSkyboxFaces_.begin(), selectedSkyboxFaces_.end(),
                                        [](const std::string& path) {
                                            std::error_code errorCode;
                                            return !path.empty() && fs::is_regular_file(fs::path(path), errorCode);
                                        });
                                    skyboxSelectionError_ = hasSelectedSkyboxFaces_
                                        ? std::string{}
                                        : "Manual selection: assign all six skybox faces.";
                                    if (hasSelectedSkyboxFaces_ && onSkyboxChosen) {
                                        onSkyboxChosen(selectedSkyboxFaces_);
                                        selectedSkyboxSaved_ = true;
                                    }
                                }
                            }
                            ImGui::PopID();
                        }

                        if (hasSelectedSkyboxFaces_ && selectedSkyboxSaved_) {
                            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Saved and applied to this project.");
                        } else if (!hasSelectedSkyboxFaces_) {
                            if (skyboxSelectionError_.empty()) {
                                ImGui::TextDisabled("Assign all six faces. The skybox saves automatically when complete.");
                            }
                        } else {
                            ImGui::TextDisabled("A complete skybox is selected but no project save callback is available.");
                        }
                    }
                    if (graphicsChanged && projectMenu.onGraphicsSettingsChanged) {
                        projectMenu.onGraphicsSettingsChanged();
                    }
                    if (graphicsChanged && !graphicsEditActive_) {
                        graphicsEditStart_ = graphicsBeforeFrame;
                        graphicsEditActive_ = true;
                        graphicsRedoStack_.clear();
                    }
                    if (graphicsEditActive_ && !ImGui::IsAnyItemActive()) {
                        graphicsUndoStack_.push_back(graphicsEditStart_);
                        constexpr size_t kMaxGraphicsHistory = 100;
                        if (graphicsUndoStack_.size() > kMaxGraphicsHistory) {
                            graphicsUndoStack_.erase(graphicsUndoStack_.begin());
                        }
                        graphicsEditActive_ = false;
                    }
                    ImGui::EndTabItem();
                }

                ImGuiTabItemFlags physicsTabFlags = restoreProjectSettingsTab_ && selectedProjectSettingsTab_ == 1 ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem("Physics", nullptr, physicsTabFlags)) {
                    selectedProjectSettingsTab_ = 1;
                    if (physicsCullingEnabled) {
                        ImGui::Checkbox("Physics Body Culling", physicsCullingEnabled);
                    } else {
                        ImGui::TextDisabled("Physics settings are unavailable.");
                    }
                    ImGui::EndTabItem();
                }

                ImGuiTabItemFlags tagsTabFlags = restoreProjectSettingsTab_ && selectedProjectSettingsTab_ == 2 ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem("Tags & Layers", nullptr, tagsTabFlags)) {
                    selectedProjectSettingsTab_ = 2;
                    if (projectMenu.renderTagsAndLayersSettings) {
                        projectMenu.renderTagsAndLayersSettings();
                    } else {
                        ImGui::TextDisabled("Tag and layer settings are unavailable.");
                    }
                    ImGui::EndTabItem();
                }

                ImGuiTabItemFlags inputTabFlags = restoreProjectSettingsTab_ && selectedProjectSettingsTab_ == 3 ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem("Input", nullptr, inputTabFlags)) {
                    selectedProjectSettingsTab_ = 3;
                    if (projectMenu.renderInputSettings) {
                        projectMenu.renderInputSettings();
                    } else {
                        ImGui::TextDisabled("Input settings are unavailable.");
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
                restoreProjectSettingsTab_ = false;
            }
            if (selectedProjectSettingsTab_ != previousProjectSettingsTab) {
                SaveState();
            }
            projectSettingsShortcutTarget_ =
                selectedProjectSettingsTab_ == 0 &&
                (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                 ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows));
        }
        ImGui::End();
        if (!showProjectSettings_) {
            SaveState();
        }
    }

    // Console is hosted inside the editor Browser tab window.
    (void)console;
}

void MenuController::RenderMainMenu(const std::function<void()>& onAddMeshPlane,
                                    const EditorProjectMenu& projectMenu,
                                    bool profilerVisible,
                                    const std::function<void(bool)>& setProfilerVisible) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene...")) {
                std::snprintf(newSceneNameBuffer_, sizeof(newSceneNameBuffer_), "%s", "NewScene");
                focusNewSceneName_ = true;
                openNewScenePopup_ = true;
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S") && projectMenu.onSaveScene) {
                projectMenu.onSaveScene();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Open Scene")) {
                if (projectMenu.sceneAssets.empty()) {
                    ImGui::TextDisabled("No scene assets found.");
                } else {
                    for (const std::string& scenePath : projectMenu.sceneAssets) {
                        const bool selected = (scenePath == projectMenu.currentScenePath);
                        const std::string label = SceneDisplayName(scenePath) + "##" + scenePath;
                        if (ImGui::MenuItem(label.c_str(), nullptr, selected) && projectMenu.onOpenScene) {
                            projectMenu.onOpenScene(scenePath);
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Project") && projectMenu.onSaveProject) {
                projectMenu.onSaveProject();
            }
            if (ImGui::MenuItem("Open Project...") && projectMenu.onOpenProjectLauncher) {
                projectMenu.onOpenProjectLauncher();
            }
            const bool pickerRunning = folderPickerState_ && !folderPickerState_->isDone.load();
            if (ImGui::MenuItem("Build...", nullptr, false, !pickerRunning) && projectMenu.onBuildProject) {
                pendingBuildCallback_ = projectMenu.onBuildProject;
                auto state = std::make_shared<FolderPickerState>();
                folderPickerState_ = state;
                folderPickerThread_ = std::make_unique<std::thread>([state]() {
                    const std::string folder = PickFolderDialog(L"Choose standalone build output folder");
                    std::lock_guard<std::mutex> lock(state->resultMutex);
                    state->result = folder;
                    state->isDone.store(true);
                });
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
            ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
            ImGui::EndMenu();
        }

        (void)onAddMeshPlane;

        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Project Settings...", nullptr, showProjectSettings_)) {
                showProjectSettings_ = !showProjectSettings_;
                if (showProjectSettings_) {
                    restoreProjectSettingsTab_ = true;
                }
                SaveState();
            }
            bool showProfiler = profilerVisible;
            if (ImGui::MenuItem("Profiler", nullptr, showProfiler) && setProfilerVisible) {
                setProfilerVisible(!showProfiler);
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (openNewScenePopup_) {
        ImGui::OpenPopup("New Scene");
        openNewScenePopup_ = false;
    }

    if (ImGui::BeginPopupModal("New Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Scene name");
        if (focusNewSceneName_) {
            ImGui::SetKeyboardFocusHere();
            focusNewSceneName_ = false;
        }
        const bool enterPressed = ImGui::InputText("##newSceneName", newSceneNameBuffer_, sizeof(newSceneNameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if ((enterPressed || ImGui::Button("Create")) && projectMenu.onNewScene) {
            projectMenu.onNewScene(newSceneNameBuffer_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

bool MenuController::TryBuildFacesFromFolder(const std::string& folder, SkyboxFaces& faces, std::string& error) {
    const std::array<std::array<const char*, 6>, 2> namingSets = {{
        {{"px", "nx", "py", "ny", "pz", "nz"}},
        {{"posx", "negx", "posy", "negy", "posz", "negz"}},
    }};
    std::vector<fs::path> images;
    try {
        for (const auto& entry : fs::directory_iterator(folder)) {
            if (entry.is_regular_file()) {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (extension == ".jpg" || extension == ".jpeg" || extension == ".png" ||
                    extension == ".bmp" || extension == ".tga" || extension == ".hdr") images.push_back(entry.path());
            }
        }
    } catch (...) {
        error = "The selected folder could not be read.";
        return false;
    }
    std::sort(images.begin(), images.end());
    for (const auto& names : namingSets) {
        SkyboxFaces candidate{};
        bool complete = true;
        for (size_t i = 0; i < names.size(); ++i) {
            for (const fs::path& image : images) {
                std::string stem = image.stem().string();
                std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (stem == names[i]) {
                    candidate[i] = image.string();
                    break;
                }
            }
            if (candidate[i].empty()) complete = false;
        }
        if (complete) {
            faces = std::move(candidate);
            error.clear();
            return true;
        }
    }
    error = "No complete skybox found. Expected px/nx/py/ny/pz/nz or posx/negx/posy/negy/posz/negz images.";
    return false;
}

void MenuController::RenderSkyboxPanel(const std::function<void(const SkyboxFaces&)>& onSkyboxChosen) {
    if (ImGui::Begin("Skybox")) {
        ImGui::TextUnformatted("Skybox selection is available in Project Settings > Rendering.");
        if (hasSelectedSkyboxFaces_) {
            const auto& faces = selectedSkyboxFaces_;
            const char* labels[6] = {"+X","-X","+Y","-Y","+Z","-Z"};
            for (int i = 0; i < 6; ++i) {
                std::vector<char> buf(faces[i].begin(), faces[i].end());
                buf.push_back('\0');
                ImGui::InputText(labels[i], buf.data(), buf.size(), ImGuiInputTextFlags_ReadOnly);
            }
            if (onSkyboxChosen) {
                if (ImGui::Button("Apply")) {
                    onSkyboxChosen(faces);
                }
            } else {
                ImGui::TextDisabled("Skybox selection is stored only.");
            }
        } else {
            ImGui::TextDisabled("No skybox selected.");
        }
    }
    ImGui::End();
}

void MenuController::LoadState() {
    std::ifstream in(stateFile_);
    if (!in.good()) return;
    std::string line;
    while (std::getline(in, line)) {
        // very simple key=value
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        auto key = line.substr(0, pos);
        auto val = line.substr(pos + 1);
        if (key == "showProjectSettings") showProjectSettings_ = (val == "1");
        else if (key == "showSkybox") showSkybox_ = (val == "1");
        else if (key == "showConsole") showConsole_ = (val == "1");
        else if (key == "selectedProjectSettingsTab") selectedProjectSettingsTab_ = std::clamp(std::stoi(val), 0, 3);
    }
}

void MenuController::SaveState() const {
    // ensure directory exists
    try {
        fs::create_directories(fs::path(stateFile_).parent_path());
    } catch (...) {}
    std::ofstream out(stateFile_, std::ios::trunc);
    if (!out.good()) return;
    out << "showProjectSettings=" << (showProjectSettings_ ? "1" : "0") << "\n";
    out << "showSkybox=" << (showSkybox_ ? "1" : "0") << "\n";
    out << "showConsole=" << (showConsole_ ? "1" : "0") << "\n";
    out << "selectedProjectSettingsTab=" << selectedProjectSettingsTab_ << "\n";
}

} // namespace raceman

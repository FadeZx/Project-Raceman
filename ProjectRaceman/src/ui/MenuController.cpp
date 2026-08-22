#include "MenuController.h"

#include "../rendering/Renderer.h"
#include "Console.h"
#include "DragDropPayloads.h"
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

// Paths arrive from three places - typed, dropped from the Project panel, and
// picked from the native dialog - and only the last uses backslashes. Storing
// one separator keeps the saved project file stable whichever route was used.
std::string NormalizeDroppedPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

// Matches SceneEditorInternal.h's IsPrefabAssetPath. The Project panel labels
// these files "Name.prefab", but the suffix on disk - and what the loader
// resolves - is .prefab.json.
bool IsPrefabPath(const std::string& path) {
    const std::string suffix = ".prefab.json";
    if (path.size() < suffix.size()) return false;
    std::string lower = path.substr(path.size() - suffix.size());
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == suffix;
}

} // namespace


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

// Writes the whole weather block as one coherent setup. Rain, wetness, droplets
// and visibility are physically linked - it does not rain hard onto a dry track,
// and a storm is not clear - so they are set together rather than left as
// independent sliders that have to be reconciled by hand.
//
// Fog is included: reduced visibility is most of what makes bad weather read as
// bad weather. Anything not weather-related (exposure, bloom, shadows) is left
// alone, so a preset never disturbs the project's look settings.
void ApplyWeatherPreset(GraphicsProfile& profile, WeatherPreset preset) {
    profile.weatherPreset = preset;
    if (preset == WeatherPreset::Custom) return;

    // Shared baseline; each case below only states what it changes.
    profile.weather = true;
    profile.weatherWind = 0.25f;
    profile.wetnessPuddleScale = 4.0f;
    profile.wetnessDropletScale = 0.06f;
    profile.wetnessDropletStrength = 0.6f;
    profile.wetnessRunoffSpeed = 0.15f;
    profile.weatherAutoWetness = true;

    switch (preset) {
    case WeatherPreset::Clear:
        profile.weather = false;
        profile.weatherIntensity = 0.0f;
        profile.wetness = 0.0f;
        profile.wetnessPuddleAmount = 0.0f;
        profile.wetnessRippleStrength = 0.0f;
        profile.fogMode = FogMode::ExponentialHeight;
        profile.fogDensity = 0.006f;
        profile.fogHeightFalloff = 0.02f;
        break;
    case WeatherPreset::Overcast:
        // Dry, but the flat light and shorter visibility of a grey day.
        profile.weather = false;
        profile.weatherIntensity = 0.0f;
        profile.wetness = 0.0f;
        profile.wetnessPuddleAmount = 0.0f;
        profile.wetnessRippleStrength = 0.0f;
        profile.fogMode = FogMode::ExponentialHeight;
        profile.fogDensity = 0.014f;
        profile.fogHeightFalloff = 0.02f;
        break;
    case WeatherPreset::Damp:
        // The interesting one: rain has stopped, the track is drying, and a dry
        // line is forming. Puddles linger, nothing is falling.
        profile.weather = false;
        profile.weatherIntensity = 0.0f;
        profile.wetness = 0.45f;
        profile.wetnessPuddleAmount = 0.35f;
        profile.wetnessPuddleScale = 2.5f;
        profile.wetnessRippleStrength = 0.0f;
        profile.wetnessRunoffSpeed = 0.05f;
        profile.fogMode = FogMode::ExponentialHeight;
        profile.fogDensity = 0.012f;
        profile.fogHeightFalloff = 0.02f;
        break;
    case WeatherPreset::LightRain:
        profile.weatherIntensity = 0.35f;
        profile.wetness = 0.65f;
        profile.wetnessPuddleAmount = 0.45f;
        profile.wetnessPuddleScale = 3.0f;
        profile.wetnessRippleStrength = 0.6f;
        profile.wetnessRippleSpeed = 1.0f;
        profile.fogMode = FogMode::ExponentialHeight;
        profile.fogDensity = 0.018f;
        profile.fogHeightFalloff = 0.02f;
        break;
    case WeatherPreset::HeavyRain:
        profile.weatherIntensity = 0.75f;
        profile.weatherWind = 0.5f;
        profile.wetness = 0.95f;
        profile.wetnessPuddleAmount = 0.75f;
        profile.wetnessPuddleScale = 5.0f;
        profile.wetnessRippleStrength = 1.6f;
        profile.wetnessRippleSpeed = 2.2f;
        profile.wetnessDropletStrength = 1.0f;
        profile.wetnessRunoffSpeed = 0.35f;
        profile.fogMode = FogMode::ExponentialHeight;
        profile.fogDensity = 0.032f;
        profile.fogHeightFalloff = 0.015f;
        break;
    case WeatherPreset::Storm:
        profile.weatherIntensity = 1.0f;
        profile.weatherWind = 1.1f;
        profile.wetness = 1.0f;
        profile.wetnessPuddleAmount = 0.95f;
        profile.wetnessPuddleScale = 7.0f;
        profile.wetnessRippleStrength = 2.6f;
        profile.wetnessRippleSpeed = 3.2f;
        profile.wetnessDropletStrength = 1.4f;
        profile.wetnessRunoffSpeed = 0.6f;
        profile.fogMode = FogMode::ExponentialHeight;
        profile.fogDensity = 0.055f;
        profile.fogHeightFalloff = 0.012f;
        break;
    case WeatherPreset::Custom:
        break;
    }
}

void ApplyGraphicsPreset(GraphicsProfile& profile, GraphicsQualityTier tier) {
    // Debug view flags are not touched here: they are Scene View state resolved
    // per viewport by Renderer::ResolveProfileForTarget, not quality settings.
    profile.quality = tier;
    profile.lod = true;
    // Weather state is authored, not a quality setting: changing the tier must
    // not switch the rain back on. Only its per-tier density, resolved at draw
    // time from profile.quality, varies with the preset.
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
        profile.taaFeedback = 0.95f;
        profile.taaSharpness = 0.15f;
        profile.taaJitterStrength = 1.00f;
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
        profile.taaFeedback = 0.94f;
        profile.taaSharpness = 0.10f;
        profile.taaJitterStrength = 1.00f;
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
                    // Environment is authored look, not performance: it sits above
                    // the Graphics Profile separator and the quality presets never
                    // touch it. Dropping Ultra to Low must not change the weather.
                    ImGui::SeparatorText("Environment");
                    graphicsChanged |= ImGui::ColorEdit3("Ambient Light", &settings.profile.ambientColor.x);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Project-wide ambient light. Game background color is configured on each Camera.");
                    }

                    if (ImGui::CollapsingHeader("Fog")) {
                        const char* fogModeNames[] = {"Off", "Linear", "Exponential Height"};
                        int fogModeIndex = static_cast<int>(settings.profile.fogMode);
                        if (ImGui::Combo("Fog Mode", &fogModeIndex, fogModeNames, 3)) {
                            settings.profile.fogMode = static_cast<FogMode>(fogModeIndex);
                            graphicsChanged = true;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Exponential Height: density falls off with altitude and is integrated along the view ray.\nLinear: a plain start/end depth ramp.");
                        }
                        const bool fogEnabled = settings.profile.fogMode != FogMode::Off;
                        const bool heightFog = settings.profile.fogMode == FogMode::ExponentialHeight;
                        ImGui::BeginDisabled(!fogEnabled);
                        graphicsChanged |= ImGui::Checkbox("Match Sky Color", &settings.profile.fogUseSkyColor);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Aerial perspective: distant surfaces fade toward the sky behind them\ninstead of a fixed colour, so fog tracks the time of day on its own.\nNeeds a baked environment; falls back to Fog Color without one.");
                        }
                        graphicsChanged |= ImGui::ColorEdit3("Fog Color", &settings.profile.fogColor.x);
                        if (settings.profile.fogUseSkyColor && ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Used only as the fallback while no environment is baked.");
                        }

                        if (heightFog) {
                            graphicsChanged |= ImGui::SliderFloat("Density", &settings.profile.fogDensity,
                                0.0f, 0.2f, "%.4f /m", ImGuiSliderFlags_Logarithmic);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Extinction per metre at Base Height. Useful range is roughly 0.002 to 0.05.");
                            }
                            graphicsChanged |= ImGui::SliderFloat("Height Falloff", &settings.profile.fogHeightFalloff,
                                0.0f, 0.5f, "%.3f");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("How quickly fog thins with altitude. 0 gives uniform fog at all heights.");
                            }
                            graphicsChanged |= ImGui::DragFloat("Base Height", &settings.profile.fogBaseHeight,
                                0.1f, -1000.0f, 1000.0f, "%.1f m");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("World Y at which Density applies. Set this near track level.");
                            }
                        } else {
                            graphicsChanged |= ImGui::SliderFloat("Linear Start", &settings.profile.fogLinearStart,
                                0.0f, 500.0f, "%.0f m");
                            graphicsChanged |= ImGui::SliderFloat("Linear End", &settings.profile.fogLinearEnd,
                                1.0f, 2000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
                        }

                        graphicsChanged |= ImGui::SliderFloat("Start Distance", &settings.profile.fogStartDistance,
                            0.0f, 100.0f, "%.1f m");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Clear air in front of the camera. Keeps the car and cockpit out of the fog.");
                        }
                        graphicsChanged |= ImGui::SliderFloat("Maximum Opacity", &settings.profile.fogMaxOpacity,
                            0.0f, 1.0f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Caps how opaque fog can become. Below 1.0 keeps distant silhouettes readable.");
                        }
                        ImGui::BeginDisabled(!heightFog);
                        graphicsChanged |= ImGui::Checkbox("Affect Sky", &settings.profile.fogAffectsSky);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Hazes the skybox toward the horizon so distant geometry blends into it.\nExponential Height only: the sky sits past any Linear end distance.");
                        }
                        ImGui::EndDisabled();

                        if (ImGui::TreeNode("Sun Inscattering")) {
                            graphicsChanged |= ImGui::SliderFloat("Sun Intensity", &settings.profile.fogSunIntensity,
                                0.0f, 1.0f, "%.2f");
                            ImGui::BeginDisabled(settings.profile.fogSunIntensity <= 0.0f);
                            graphicsChanged |= ImGui::ColorEdit3("Sun Color", &settings.profile.fogSunColor.x);
                            graphicsChanged |= ImGui::SliderFloat("Directional Exponent", &settings.profile.fogSunExponent,
                                1.0f, 64.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Tightness of the glow around the sun. Higher is a smaller, sharper halo.");
                            }
                            ImGui::EndDisabled();
                            ImGui::TextDisabled("Uses the shadow-casting directional light.");
                            ImGui::TreePop();
                        }
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader("Weather")) {
                        const char* weatherPresetNames[] = {
                            "Clear", "Overcast", "Damp (drying track)", "Light Rain", "Heavy Rain", "Storm", "Custom"};
                        int weatherPresetIndex = static_cast<int>(settings.profile.weatherPreset);
                        if (ImGui::Combo("Preset##Weather", &weatherPresetIndex, weatherPresetNames, 7)) {
                            ApplyWeatherPreset(settings.profile, static_cast<WeatherPreset>(weatherPresetIndex));
                            graphicsChanged = true;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Sets rain, surface wetness, droplets and visibility together.\nAlso adjusts fog, since reduced visibility is most of what sells bad weather.");
                        }

                        // Any manual edit below means the setup no longer matches
                        // the preset it came from; say so rather than lie.
                        const WeatherPreset presetBeforeEdits = settings.profile.weatherPreset;
                        bool weatherEdited = false;

                        ImGui::SeparatorText("Precipitation");
                        weatherEdited |= ImGui::Checkbox("Rain##Weather", &settings.profile.weather);
                        ImGui::BeginDisabled(!settings.profile.weather);
                        weatherEdited |= ImGui::SliderFloat("Rain Intensity", &settings.profile.weatherIntensity, 0.0f, 1.0f, "%.2f");
                        weatherEdited |= ImGui::SliderFloat("Wind", &settings.profile.weatherWind, -2.0f, 2.0f, "%.2f");
                        weatherEdited |= ImGui::SliderFloat("Fall Speed", &settings.profile.weatherFallSpeedScale, 0.2f, 3.0f, "%.2fx");
                        weatherEdited |= ImGui::SliderFloat("Draw Distance", &settings.profile.weatherDrawDistance, 15.0f, 120.0f, "%.0f m");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("How far out individual drops are simulated before it's just the\ndistant haze. Larger reads as heavier weather but costs more drops\nto stay equally dense.");
                        }
                        weatherEdited |= ImGui::SliderFloat("Streak Length", &settings.profile.weatherStreakLength, 0.1f, 2.0f, "%.2f m");
                        weatherEdited |= ImGui::SliderFloat("Streak Width", &settings.profile.weatherStreakWidth, 0.002f, 0.08f, "%.3f m", ImGuiSliderFlags_Logarithmic);
                        ImGui::EndDisabled();

                        ImGui::SeparatorText("Track Soaking");
                        weatherEdited |= ImGui::Checkbox("Rain Wets The Track", &settings.profile.weatherAutoWetness);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("While playing, the track soaks as it rains and dries out after,\nwhich is what produces a drying line. The slider below is the authored\nstarting value and is what gets saved; the simulation never overwrites it.");
                        }
                        ImGui::BeginDisabled(!settings.profile.weatherAutoWetness);
                        weatherEdited |= ImGui::SliderFloat("Soak Rate", &settings.profile.weatherWetRate, 0.005f, 1.0f, "%.3f /s", ImGuiSliderFlags_Logarithmic);
                        weatherEdited |= ImGui::SliderFloat("Dry Rate", &settings.profile.weatherDryRate, 0.001f, 1.0f, "%.3f /s", ImGuiSliderFlags_Logarithmic);
                        ImGui::EndDisabled();

                        ImGui::SeparatorText("Surface Water");
                        weatherEdited |= ImGui::SliderFloat("Surface Wetness", &settings.profile.wetness, 0.0f, 1.0f, "%.2f");
                        graphicsChanged |= weatherEdited;
                        if (weatherEdited && presetBeforeEdits != WeatherPreset::Custom) {
                            settings.profile.weatherPreset = WeatherPreset::Custom;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Darkens and smooths surfaces, and pools water on upward-facing ones.\nIndependent of the rain overlay, so a track can stay wet after the rain stops.");
                        }
                        ImGui::BeginDisabled(settings.profile.wetness <= 0.0f);
                        graphicsChanged |= ImGui::SliderFloat("Puddle Amount", &settings.profile.wetnessPuddleAmount, 0.0f, 1.0f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("How much of the wet area pools into standing water.\nPuddles appear progressively as wetness rises.");
                        }
                        graphicsChanged |= ImGui::SliderFloat("Puddle Scale", &settings.profile.wetnessPuddleScale, 0.05f, 50.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Size of an individual puddle in world units. Drop to a few centimetres\nfor scattered wet patches rather than large standing pools.");
                        }
                        graphicsChanged |= ImGui::SliderFloat("Ripple Strength", &settings.profile.wetnessRippleStrength, 0.0f, 4.0f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Surface disturbance on standing water. Set to 0 for still puddles once the rain stops.");
                        }
                        ImGui::BeginDisabled(settings.profile.wetnessRippleStrength <= 0.0f);
                        graphicsChanged |= ImGui::SliderFloat("Ripple Speed", &settings.profile.wetnessRippleSpeed, 0.0f, 8.0f, "%.2f");
                        ImGui::EndDisabled();

                        ImGui::SeparatorText("Droplets");
                        ImGui::TextDisabled("Used automatically where a surface is too steep to pool.");
                        graphicsChanged |= ImGui::SliderFloat("Droplet Scale", &settings.profile.wetnessDropletScale, 0.005f, 1.0f, "%.3f m", ImGuiSliderFlags_Logarithmic);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Bead size. Bodywork wants a few centimetres.");
                        }
                        graphicsChanged |= ImGui::SliderFloat("Droplet Strength", &settings.profile.wetnessDropletStrength, 0.0f, 4.0f, "%.2f");
                        graphicsChanged |= ImGui::SliderFloat("Runoff Speed", &settings.profile.wetnessRunoffSpeed, 0.0f, 4.0f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("How fast beads slide downhill. Scales with slope, so level surfaces stay still.");
                        }
                        if (settings.profile.wetness > 0.0f && !settings.profile.screenSpaceReflections) {
                            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                                "Screen-Space Reflections are off; wet surfaces will not reflect.");
                        }
                        ImGui::TextDisabled("Applies to upward-facing surfaces; vertical faces stay dry.");
                        // Session-only, like the other *DebugView flags: never part of
                        // graphicsChanged, so toggling it does not dirty or save the project.
                        ImGui::Checkbox("Debug View (Red = Droplet, Green = Puddle, Blue = Film)",
                            &settings.profile.wetnessDebugView);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Shows which regime each surface fell into, so a material that looks\nall-puddle-no-droplet can be diagnosed at a glance instead of guessed at.");
                        }
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader("Tyre Smoke")) {
                        graphicsChanged |= ImGui::Checkbox("Enabled##TyreSmoke", &settings.profile.tyreSmoke);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Smoke from sliding tyres, emitted from the same per-wheel slip\n"
                                              "the skid marks are drawn from - a locked wheel marks and smokes\n"
                                              "from one number, so the two always agree.");
                        }
                        ImGui::BeginDisabled(!settings.profile.tyreSmoke);
                        {
                            graphicsChanged |= ImGui::SliderFloat("Slip Threshold##TyreSmoke", &settings.profile.tyreSmokeSlipThreshold, 0.02f, 1.0f, "%.2f");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("How hard a tyre must slide before it smokes, on the same\n"
                                                  "0-1 scrub scale the marks use. Set above the mark\n"
                                                  "threshold: rubber goes down well before it gets hot\n"
                                                  "enough to make visible smoke.");
                            }
                            graphicsChanged |= ImGui::SliderFloat("Opacity##TyreSmoke", &settings.profile.tyreSmokeOpacity, 0.0f, 1.0f, "%.2f");
                            graphicsChanged |= ImGui::SliderFloat("Lifetime##TyreSmoke", &settings.profile.tyreSmokeLifetime, 0.2f, 6.0f, "%.2f s");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("How long a puff lives. Longer leaves a bigger hanging cloud\n"
                                                  "but costs proportionally more live particles.");
                            }
                            graphicsChanged |= ImGui::SliderFloat("Spawn Rate##TyreSmoke", &settings.profile.tyreSmokeSpawnRate, 5.0f, 200.0f, "%.0f /s");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Puffs per second per wheel at full slip. Density, not speed.");
                            }
                            graphicsChanged |= ImGui::SliderInt("Max Particles##TyreSmoke", &settings.profile.tyreSmokeMaxParticles, 64, 4000);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Hard cap across every wheel of every car. All puffs draw in\n"
                                                  "one instanced call, so this is a fill-rate knob rather\n"
                                                  "than a draw-call one.");
                            }
                        }
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader("Skid Marks")) {
                        // Label must differ from the enclosing CollapsingHeader:
                        // ImGui hashes the visible label into the widget ID, so a
                        // "Skid Marks" checkbox inside a "Skid Marks" header is an
                        // ID collision, not just a cosmetic repeat.
                        graphicsChanged |= ImGui::Checkbox("Enabled##SkidMarks", &settings.profile.skidMarks);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Rubber laid by sliding tyres while the game is running.\nEmitted as decals, so marks conform to the track and show in wet reflections.");
                        }
                        ImGui::BeginDisabled(!settings.profile.skidMarks);
                        {
                            const char* kPrefabTooltip =
                                "Drop a prefab here from the Project panel, or browse for one.\n\n"
                                "Its Decal component is the look of runtime marks: texture, colour,\n"
                                "blend, angle fade and UV tiling. Its Transform scale is the volume -\n"
                                "X the mark width, Y the projection depth, Z the metres of track per\n"
                                "texture repeat.\n\n"
                                "The prefab is a template, not a spawned object: marks stay out of the\n"
                                "hierarchy and the save file. Leave empty for untextured marks.";

                            char prefabBuffer[512];
                            std::snprintf(prefabBuffer, sizeof(prefabBuffer), "%s",
                                          settings.profile.skidMarkDecalPrefab.c_str());
                            const float clearWidth = ImGui::GetFrameHeight();
                            const float browseWidth = ImGui::CalcTextSize("Browse").x +
                                                      ImGui::GetStyle().FramePadding.x * 2.0f;
                            const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
                            ImGui::SetNextItemWidth(
                                (std::max)(80.0f, ImGui::GetContentRegionAvail().x -
                                                  browseWidth - clearWidth - spacing * 2.0f -
                                                  ImGui::CalcTextSize("Decal Prefab").x - spacing * 2.0f));
                            if (ImGui::InputText("##SkidDecalPrefab", prefabBuffer, sizeof(prefabBuffer))) {
                                settings.profile.skidMarkDecalPrefab = NormalizeDroppedPath(prefabBuffer);
                                graphicsChanged = true;
                            }
                            // The drop target has to be registered against the
                            // InputText itself, not a wrapping group: ImGui matches
                            // a drop against the last submitted item's rect.
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                                    const char* dropped = static_cast<const char*>(payload->Data);
                                    if (dropped != nullptr && IsPrefabPath(dropped)) {
                                        settings.profile.skidMarkDecalPrefab = NormalizeDroppedPath(dropped);
                                        graphicsChanged = true;
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kPrefabTooltip);

                            ImGui::SameLine(0.0f, spacing);
                            if (ImGui::Button("Browse##SkidDecalPrefab")) {
                                const std::string picked = PickPrefabFileDialog(L"Select skid mark decal prefab");
                                if (!picked.empty()) {
                                    settings.profile.skidMarkDecalPrefab = NormalizeDroppedPath(picked);
                                    graphicsChanged = true;
                                }
                            }
                            ImGui::SameLine(0.0f, spacing);
                            ImGui::BeginDisabled(settings.profile.skidMarkDecalPrefab.empty());
                            if (ImGui::Button("x##SkidDecalPrefabClear", ImVec2(clearWidth, 0.0f))) {
                                settings.profile.skidMarkDecalPrefab.clear();
                                graphicsChanged = true;
                            }
                            ImGui::EndDisabled();
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear the prefab.");
                            ImGui::SameLine(0.0f, spacing);
                            ImGui::TextUnformatted("Decal Prefab");

                            if (settings.profile.skidMarkDecalPrefab.empty()) {
                                ImGui::TextDisabled("No prefab: marks use Mark Color, untextured.");
                            } else if (!IsPrefabPath(settings.profile.skidMarkDecalPrefab)) {
                                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                                    "Not a .prefab.json file - marks will fall back to Mark Color.");
                            }
                        }
                        graphicsChanged |= ImGui::SliderFloat("Slip Threshold", &settings.profile.skidMarkSlipThreshold, 0.02f, 1.0f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("How fast a tyre's contact patch must slide before it marks,\n"
                                              "as a fraction of 6 m/s of scrub. 0.25 = 1.5 m/s.\n"
                                              "A gripping tyre reads zero however fast the car is going,\n"
                                              "so this only ever gates real sliding.");
                        }
                        graphicsChanged |= ImGui::SliderFloat("Segment Spacing", &settings.profile.skidMarkSpacing, 0.05f, 2.0f, "%.2f m");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Distance between segments. Smaller is smoother but burns the mark budget faster.");
                        }
                        graphicsChanged |= ImGui::SliderFloat("Mark Width", &settings.profile.skidMarkWidth, 0.05f, 1.0f, "%.2f m");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Ignored when the decal prefab authors a width through its Transform scale X.");
                        }
                        graphicsChanged |= ImGui::SliderFloat("Mark Opacity", &settings.profile.skidMarkOpacity, 0.0f, 1.0f, "%.2f");
                        graphicsChanged |= ImGui::ColorEdit3("Mark Color", &settings.profile.skidMarkColor.x);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Used only without a decal prefab; otherwise the prefab's decal colour wins.");
                        }
                        graphicsChanged |= ImGui::SliderFloat("Fade Time", &settings.profile.skidMarkFadeSeconds, 0.0f, 300.0f, "%.0f s");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("0 keeps marks for the whole session, so rubber builds up over a stint.");
                        }
                        graphicsChanged |= ImGui::SliderInt("Maximum Marks", &settings.profile.maxSkidMarks, 32, 2000);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Oldest marks are recycled past this. Every visible mark is its own draw call,\nso this is a real performance setting, not just a memory one.");
                        }
                        if (settings.profile.maxSkidMarks > 800) {
                            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                                "High mark counts cost one draw call each until the renderer has instancing.");
                        }
                        ImGui::EndDisabled();
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
                    const char* aaNames[] = {"None", "FXAA", "TAA", "SMAA"};
                    int aaIndex = (std::min)(3, static_cast<int>(settings.profile.antiAliasing));
                    if (ImGui::Combo("Anti-Aliasing", &aaIndex, aaNames, 4)) {
                        settings.profile.antiAliasing = static_cast<AntiAliasingMode>(aaIndex);
                        graphicsChanged = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("SMAA: sharp, stable edges with no temporal shimmer or ghosting.\nTAA: smoothest overall image, also removes specular/shading aliasing.");
                    }
                    if (settings.profile.antiAliasing == AntiAliasingMode::TAA) {
                        graphicsChanged |= ImGui::SliderFloat("TAA Smoothing", &settings.profile.taaFeedback, 0.85f, 0.98f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Temporal smoothing. Higher is smoother and more stable; 0.94 is the recommended default.");
                        }
                        graphicsChanged |= ImGui::SliderFloat("TAA Sharpness", &settings.profile.taaSharpness, 0.0f, 0.5f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Post-resolve sharpening. Keep low (around 0.10) for a soft, filmic image.");
                        }
                        graphicsChanged |= ImGui::SliderFloat("TAA Jitter Strength", &settings.profile.taaJitterStrength, 0.25f, 1.0f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Subpixel sampling amount. 1.0 gives the most edge anti-aliasing; smoothing hides the jitter.");
                        }
                    }
                    graphicsChanged |= ImGui::Checkbox("Auto Exposure", &settings.profile.autoExposure);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Meters the frame with a luminance histogram and adapts over time.\nEssential once a track runs through tunnels or into night.");
                    }
                    if (settings.profile.autoExposure) {
                        graphicsChanged |= ImGui::SliderFloat("Exposure Compensation", &settings.profile.autoExposureCompensation,
                            -4.0f, 4.0f, "%.2f EV");
                        graphicsChanged |= ImGui::SliderFloat("Adapt Speed (brighten)", &settings.profile.autoExposureSpeedUp,
                            0.05f, 10.0f, "%.2f /s", ImGuiSliderFlags_Logarithmic);
                        graphicsChanged |= ImGui::SliderFloat("Adapt Speed (darken)", &settings.profile.autoExposureSpeedDown,
                            0.05f, 10.0f, "%.2f /s", ImGuiSliderFlags_Logarithmic);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Separate rates: exiting a tunnel should recover fast, entering one should stay gradual.");
                        }
                        if (ImGui::TreeNode("Metering")) {
                            graphicsChanged |= ImGui::SliderFloat("Ignore Darkest", &settings.profile.autoExposureLowPercent,
                                0.0f, 0.9f, "%.2f");
                            graphicsChanged |= ImGui::SliderFloat("Ignore Above", &settings.profile.autoExposureHighPercent,
                                0.1f, 1.0f, "%.2f");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Histogram percentile trim. Keeps a bright sky or oncoming headlights\nfrom dragging the metered value off what the eye is actually adapted to.");
                            }
                            graphicsChanged |= ImGui::SliderFloat("Minimum Luminance", &settings.profile.autoExposureMinLuminance,
                                0.0001f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
                            graphicsChanged |= ImGui::SliderFloat("Maximum Luminance", &settings.profile.autoExposureMaxLuminance,
                                1.0f, 200.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
                            ImGui::TreePop();
                        }
                        ImGui::TextDisabled("Scene View > Auto Exposure shows the metered EV.");
                    }
                    ImGui::BeginDisabled(settings.profile.autoExposure);
                    graphicsChanged |= ImGui::DragFloat("Exposure", &settings.profile.exposure, 0.02f, 0.05f, 8.0f, "%.2f");
                    if (settings.profile.autoExposure && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Ignored while Auto Exposure is on. Use Exposure Compensation instead.");
                    }
                    ImGui::EndDisabled();
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
                    ImGui::TextDisabled("SSR hits on-screen geometry; skybox IBL remains the fallback.");
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    if (!renderer.HasEnvironmentSource()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "IBL status: no valid skybox cubemap");
                    } else if (renderer.IsEnvironmentBakeReady()) {
                        ImGui::TextDisabled("IBL status: baked (irradiance %.3f)", renderer.GetEnvironmentAverageLuminance());
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "IBL status: using skybox mip fallback");
                    }
                    // Rain and particles now live in Environment > Weather, next to
                    // the surface wetness they are physically tied to.
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

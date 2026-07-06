#include "DebugUI.h"

#include "SceneEditor.h"
#include "../physics/PhysicsWorld.h"
#include "../rendering/Renderer.h"

#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace raceman {

namespace {

static constexpr const char* kEditorStatePath = "config/editor_state.json";
static constexpr const char* kEditorFontPath = "editor-assets/third_party/JoltPhysics-master/Assets/Fonts/Roboto-Regular.ttf";

void ConfigureEditorTypography() {
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = 1.0f;

    if (std::filesystem::exists(kEditorFontPath)) {
        ImFontConfig fontConfig;
        fontConfig.SizePixels = 14.0f;
        fontConfig.PixelSnapH = true;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;
        ImFont* font = io.Fonts->AddFontFromFileTTF(kEditorFontPath, 14.0f, &fontConfig, io.Fonts->GetGlyphRangesDefault());
        if (font != nullptr) {
            io.FontDefault = font;
            return;
        }
    }

    io.FontDefault = io.Fonts->AddFontDefault();
}

void ApplyEditorTheme() {
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(7.0f, 6.0f);
    style.FramePadding = ImVec2(6.0f, 3.0f);
    style.CellPadding = ImVec2(5.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 3.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f;

    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.96f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.61f, 0.67f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.085f, 0.100f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.090f, 0.102f, 0.120f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.105f, 0.118f, 0.138f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.215f, 0.240f, 0.275f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.125f, 0.140f, 0.165f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.170f, 0.195f, 0.230f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.095f, 0.300f, 0.430f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.060f, 0.070f, 0.085f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.085f, 0.105f, 0.125f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.060f, 0.070f, 0.085f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.060f, 0.070f, 0.085f, 1.00f);

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.060f, 0.070f, 0.085f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.230f, 0.255f, 0.300f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.300f, 0.340f, 0.390f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.120f, 0.480f, 0.680f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.200f, 0.720f, 0.900f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.160f, 0.620f, 0.820f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.260f, 0.760f, 0.960f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.145f, 0.170f, 0.205f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.180f, 0.285f, 0.345f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.095f, 0.390f, 0.560f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.125f, 0.175f, 0.215f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.145f, 0.320f, 0.420f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.090f, 0.430f, 0.600f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.215f, 0.240f, 0.275f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.180f, 0.540f, 0.710f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.220f, 0.680f, 0.880f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.140f, 0.420f, 0.560f, 0.35f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.180f, 0.560f, 0.740f, 0.75f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.240f, 0.740f, 0.940f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.105f, 0.122f, 0.145f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.155f, 0.330f, 0.430f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.130f, 0.185f, 0.230f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.200f, 0.720f, 0.900f, 1.00f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.080f, 0.092f, 0.110f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.105f, 0.125f, 0.150f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.130f, 0.420f, 0.560f, 1.00f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.160f, 0.640f, 0.860f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.050f, 0.058f, 0.070f, 1.00f);

    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.105f, 0.125f, 0.150f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.270f, 0.300f, 0.340f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.190f, 0.215f, 0.250f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.000f, 1.000f, 1.000f, 0.035f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.130f, 0.520f, 0.720f, 0.40f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.240f, 0.780f, 0.980f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.200f, 0.720f, 0.900f, 0.80f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.55f);
}

void SaveEditorState(bool showProfiler) {
    std::filesystem::create_directories("config");
    std::ofstream f(kEditorStatePath, std::ios::trunc);
    if (!f.is_open()) {
        return;
    }
    f << "{\n  \"showProfiler\": " << (showProfiler ? "true" : "false") << "\n}\n";
}

bool LoadEditorStateShowProfiler(bool defaultValue) {
    std::ifstream f(kEditorStatePath);
    if (!f.is_open()) {
        return defaultValue;
    }
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto pos = content.find("\"showProfiler\"");
    if (pos == std::string::npos) {
        return defaultValue;
    }
    const auto colon = content.find(':', pos);
    if (colon == std::string::npos) {
        return defaultValue;
    }
    const auto valStart = content.find_first_not_of(" \t\r\n", colon + 1);
    if (valStart == std::string::npos) {
        return defaultValue;
    }
    return content.compare(valStart, 4, "true") == 0;
}

template <typename Contributor>
void RenderContributorRows(const std::vector<Contributor>& contributors, std::size_t maxCount) {
    const std::size_t rowCount = (std::min)(contributors.size(), maxCount);
    for (std::size_t i = 0; i < rowCount; ++i) {
        const Contributor& contributor = contributors[i];
        const char* modeLabel = contributor.meshMode == MeshColliderMode::ConvexHull ? "ConvexHull" : "TriangleMesh";
        const std::string& subName = contributor.meshName;
        if (subName.empty()) {
            ImGui::BulletText("%s [mesh %d] x%u, %llu tris, %s",
                              contributor.meshAssetPath.c_str(),
                              contributor.meshIndex,
                              contributor.usageCount,
                              static_cast<unsigned long long>(contributor.triangleCount),
                              modeLabel);
        } else {
            ImGui::BulletText("%s [%s] x%u, %llu tris, %s",
                              contributor.meshAssetPath.c_str(),
                              subName.c_str(),
                              contributor.usageCount,
                              static_cast<unsigned long long>(contributor.triangleCount),
                              modeLabel);
        }
    }
}

void RenderSceneContributorRows(const std::vector<SceneMeshContributorStats>& contributors, std::size_t maxCount) {
    const std::size_t rowCount = (std::min)(contributors.size(), maxCount);
    for (std::size_t i = 0; i < rowCount; ++i) {
        const SceneMeshContributorStats& contributor = contributors[i];
        const char* modeLabel = contributor.meshMode == MeshColliderMode::ConvexHull ? "ConvexHull" : "TriangleMesh";
        const std::string& subName = contributor.meshName;
        if (subName.empty()) {
            ImGui::BulletText("%s [mesh %d] x%u, %llu tris, %s",
                              contributor.meshAssetPath.c_str(),
                              contributor.meshIndex,
                              contributor.objectCount,
                              static_cast<unsigned long long>(contributor.triangleCount),
                              modeLabel);
        } else {
            ImGui::BulletText("%s [%s] x%u, %llu tris, %s",
                              contributor.meshAssetPath.c_str(),
                              subName.c_str(),
                              contributor.objectCount,
                              static_cast<unsigned long long>(contributor.triangleCount),
                              modeLabel);
        }
    }
}

} // namespace

DebugUI::DebugUI(bool enabled) : enabled_(enabled) {}

DebugUI::~DebugUI() = default;

void DebugUI::Initialize(GLFWwindow* window) {
    if (!enabled_) {
        return;
    }  

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyEditorTheme();
    ConfigureEditorTypography();

    showProfiler_ = LoadEditorStateShowProfiler(showProfiler_);

    // Persist window positions/sizes to config/imgui.ini
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "config/imgui.ini"; // ImGui will auto-load/save this
    io.ConfigWindowsResizeFromEdges = true;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 450");
}

void DebugUI::Shutdown() {
    if (!enabled_) {
        return;
    }

    // Ensure settings are flushed to disk
    ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename ? ImGui::GetIO().IniFilename : "imgui.ini");
    SaveEditorState(showProfiler_);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void DebugUI::BeginFrame() {
    if (!enabled_) {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::EndFrame() {
    if (!enabled_) {
        return;
    }

    ImGui::EndFrame();
}

void DebugUI::RenderDrawData() {
    if (!enabled_) {
        return;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void DebugUI::RenderProfilerHud() {
    // Toggle button moved to the Game View toolbar ("Stats" button).
    // This function is intentionally empty — kept for API compatibility.
}

void DebugUI::RenderAppMetrics(float deltaTime,
                               Renderer& renderer,
                               const SceneProfilerStats* sceneStats,
                               const PhysicsWorldStats* physicsStats,
                               const AppFrameTimings* frameTimings,
                               const SceneEditorFrameTimings* editorTimings,
                               glm::vec2 windowAnchor) {
    if (!enabled_ || !showProfiler_) {
        return;
    }

    const float frameTimeMs = deltaTime * 1000.0f;
    const float fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
    rollingFrameTimeMs_ = rollingFrameTimeMs_ <= 0.0f ? frameTimeMs : (rollingFrameTimeMs_ * 0.9f + frameTimeMs * 0.1f);
    if (fps > 0.0f) {
        averageFpsAccum_ += fps;
        ++averageFpsSamples_;
        averageFps_ = averageFpsAccum_ / static_cast<float>(averageFpsSamples_);
    }

    const RendererFrameStats& rendererStats = renderer.GetFrameStats();

    // Position the window: if the caller supplies a game-viewport anchor, float
    // the overlay near the top-left of that anchor region (like Unity's Stats
    // panel).  Otherwise fall back to the top-right of the main viewport.
    if (windowAnchor.x >= 0.0f && windowAnchor.y >= 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(windowAnchor.x + 8.0f, windowAnchor.y + 30.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(380.0f, 500.0f), ImGuiCond_Appearing);
    } else {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 420.0f, viewport->WorkPos.y + 52.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400.0f, 520.0f), ImGuiCond_FirstUseEver);
    }

    if (ImGui::Begin("Profiler", &showProfiler_)) {
        ImGui::Text("Frame time: %.2f ms", frameTimeMs);
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Rolling frame: %.2f ms", rollingFrameTimeMs_);
        ImGui::Text("Average FPS: %.1f  (%d samples)", averageFps_, averageFpsSamples_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            averageFpsAccum_ = 0.0f;
            averageFpsSamples_ = 0;
            averageFps_ = 0.0f;
            rollingFrameTimeMs_ = 0.0f;
        }
        ImGui::Separator();

        if (frameTimings) {
            ImGui::TextUnformatted("Frame Breakdown");
            ImGui::Text("Poll: %.2f ms", frameTimings->pollMs);
            ImGui::Text("Update/UI: %.2f ms", frameTimings->updateMs);
            ImGui::Text("Render total: %.2f ms", frameTimings->renderMs);
            ImGui::Text("Scene pass: %.2f ms", frameTimings->scenePassMs);
            ImGui::Text("Game pass: %.2f ms", frameTimings->gamePassMs);
            ImGui::Text("ImGui draw: %.2f ms", frameTimings->imguiRenderMs);
            ImGui::Text("Swap buffers: %.2f ms", frameTimings->swapMs);
            ImGui::Separator();
        }

        if (editorTimings) {
            ImGui::TextUnformatted("Editor UI Breakdown");
            ImGui::Text("Shortcuts: %.2f ms", editorTimings->shortcutsMs);
            ImGui::Text("Play popup: %.2f ms", editorTimings->playModePopupMs);
            ImGui::Text("Runtime updates: %.2f ms", editorTimings->runtimeUpdatesMs);
            ImGui::Text("Dockspace: %.2f ms", editorTimings->dockspaceMs);
            ImGui::Text("Scene panel: %.2f ms", editorTimings->scenePanelMs);
            ImGui::Text("Inspector: %.2f ms", editorTimings->inspectorMs);
            ImGui::Text("Browser: %.2f ms", editorTimings->browserMs);
            ImGui::Text("Viewport UI: %.2f ms", editorTimings->viewportPanelMs);
            ImGui::Text("Aux windows: %.2f ms", editorTimings->auxiliaryWindowsMs);
            ImGui::Separator();
        }

        ImGui::TextUnformatted("Rendering");
        ImGui::Text("Submitted meshes: %u", rendererStats.submittedMeshCount);
        ImGui::Text("Frustum culled: %u", rendererStats.frustumCulledMeshCount);
        ImGui::SameLine();
        if (ImGui::SmallButton(showFrustumCullDebug_ ? "Hide##fcd" : "Show##fcd")) {
            showFrustumCullDebug_ = !showFrustumCullDebug_;
        }
        ImGui::Text("Submitted lights: %u", rendererStats.submittedLightCount);
        ImGui::Text("Draw calls: %u", rendererStats.drawCallCount);
        ImGui::Text("Submitted triangles: %llu", static_cast<unsigned long long>(rendererStats.submittedTriangleCount));

        if (sceneStats) {
            ImGui::Separator();
            ImGui::TextUnformatted("Scene");
            ImGui::Text("Visible meshes: %u", sceneStats->visibleMeshCount);
            ImGui::Text("Visible lights: %u", sceneStats->visibleLightCount);
            ImGui::Text("Bodies: %u", sceneStats->bodyCount);
            ImGui::Text("Characters: %u", sceneStats->characterCount);
            ImGui::Text("Box/Sphere/Capsule: %u / %u / %u",
                        sceneStats->boxColliderCount,
                        sceneStats->sphereColliderCount,
                        sceneStats->capsuleColliderCount);
            ImGui::Text("Plane/Mesh: %u / %u", sceneStats->planeColliderCount, sceneStats->meshColliderCount);
            ImGui::Text("TriangleMesh/ConvexHull: %u / %u",
                        sceneStats->triangleMeshColliderCount,
                        sceneStats->convexHullColliderCount);
            if (!sceneStats->meshContributors.empty()) {
                ImGui::Separator();
                ImGui::TextUnformatted("Top Mesh Collider Sources");
                RenderSceneContributorRows(sceneStats->meshContributors, 6);
            }
        }

        if (physicsStats) {
            ImGui::Separator();
            ImGui::TextUnformatted("Physics Runtime");
            ImGui::Text("Build time: %.2f ms", physicsStats->lastBuildTimeMs);
            ImGui::Text("Step time: %.3f ms", physicsStats->lastStepTimeMs);
            ImGui::Text("Bodies/Characters: %u / %u", physicsStats->bodyCount, physicsStats->characterCount);
            ImGui::Text("Box/Sphere/Capsule: %u / %u / %u",
                        physicsStats->boxColliderCount,
                        physicsStats->sphereColliderCount,
                        physicsStats->capsuleColliderCount);
            ImGui::Text("Plane/Mesh: %u / %u", physicsStats->planeColliderCount, physicsStats->meshColliderCount);
            ImGui::Text("TriangleMesh/ConvexHull: %u / %u",
                        physicsStats->triangleMeshColliderCount,
                        physicsStats->convexHullColliderCount);
            if (physicsStats->dynamicBodyCount > 0) {
                const bool cullingActive = physicsStats->activeDynamicCount < physicsStats->dynamicBodyCount;
                if (cullingActive) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                        "Dynamic active: %u / %u (culling on)",
                        physicsStats->activeDynamicCount, physicsStats->dynamicBodyCount);
                } else {
                    ImGui::Text("Dynamic active: %u / %u",
                        physicsStats->activeDynamicCount, physicsStats->dynamicBodyCount);
                }
                ImGui::Checkbox("Show Culling Zones", &showCullingDebug_);
            }
            if (!physicsStats->meshContributors.empty()) {
                ImGui::Separator();
                ImGui::TextUnformatted("Cooked Mesh Contributors");
                RenderContributorRows(physicsStats->meshContributors, 6);
            }
        }

        if (sceneStats && sceneStats->triangleMeshColliderCount >= 16) {
            ImGui::Separator();
            ImGui::TextWrapped("Recommendation: this scene has many triangle-mesh colliders. Keep them for large static track surfaces, but prefer primitive colliders, convex hulls, or dedicated low-poly collision meshes for movable and decorative geometry.");
        }

    }
    ImGui::End();
}

} // namespace raceman

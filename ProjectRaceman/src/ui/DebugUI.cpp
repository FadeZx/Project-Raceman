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
    ImGui::StyleColorsDark();

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
                               glm::vec2 windowAnchor) {
    if (!enabled_ || !showProfiler_) {
        return;
    }

    const float frameTimeMs = deltaTime * 1000.0f;
    const float fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
    rollingFrameTimeMs_ = rollingFrameTimeMs_ <= 0.0f ? frameTimeMs : (rollingFrameTimeMs_ * 0.9f + frameTimeMs * 0.1f);
    lowestFps_ = (fps > 0.0f && (lowestFps_ <= 0.0f || fps < lowestFps_)) ? fps : lowestFps_;

    auto& settings = renderer.GetSettings();
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
        ImGui::Text("Lowest FPS: %.1f", lowestFps_);
        ImGui::Separator();

        ImGui::TextUnformatted("Rendering");
        ImGui::Text("Submitted meshes: %u", rendererStats.submittedMeshCount);
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

        ImGui::Separator();
        ImGui::TextUnformatted("Renderer Settings");

        ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 5.0f, "%.2f");
        ImGui::SliderFloat("Gamma", &settings.gamma, 1.0f, 3.0f, "%.2f");
        ImGui::ColorEdit3("Clear Color", &settings.clearColor.x);
        ImGui::Checkbox("Enable Shadows", &settings.enableShadows);
        ImGui::Checkbox("Show Env Debug", &settings.showEnvironmentDebugView);
    }
    ImGui::End();
}

} // namespace raceman

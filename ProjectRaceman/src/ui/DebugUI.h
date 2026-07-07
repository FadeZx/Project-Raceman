#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>

struct GLFWwindow;

namespace raceman {

class Renderer;
class SceneEditor;
class PhysicsWorld;

struct RendererFrameStats;
struct SceneProfilerStats;
struct SceneEditorFrameTimings;
struct PhysicsWorldStats;

struct AppFrameTimings {
    float pollMs{0.0f};
    float updateMs{0.0f};
    float renderMs{0.0f};
    float scenePassMs{0.0f};
    float gamePassMs{0.0f};
    float imguiRenderMs{0.0f};
    float swapMs{0.0f};
};

class DebugUI {
public:
    explicit DebugUI(bool enabled);
    ~DebugUI();

    void Initialize(GLFWwindow* window);
    void Shutdown();

    void BeginFrame();
    void EndFrame();
    void RenderDrawData();

    void RenderProfilerHud();
    void RenderAppMetrics(float deltaTime,
                          Renderer& renderer,
                          const SceneProfilerStats* sceneStats = nullptr,
                          const PhysicsWorldStats* physicsStats = nullptr,
                          const AppFrameTimings* frameTimings = nullptr,
                          const SceneEditorFrameTimings* editorTimings = nullptr,
                          glm::vec2 windowAnchor = glm::vec2(-1.0f));

    bool IsEnabled() const { return enabled_; }
    bool IsProfilerVisible() const { return showProfiler_; }
    void SetProfilerVisible(bool visible) { showProfiler_ = visible; }
    bool ShowCullingDebug() const { return showCullingDebug_; }
    bool ShowFrustumCullDebug() const { return showFrustumCullDebug_; }

private:
    static constexpr int kProfilerHistoryCount = 180;

    bool enabled_{false};
    bool showProfiler_{true};
    float rollingFrameTimeMs_{0.0f};
    float averageFpsAccum_{0.0f};
    int   averageFpsSamples_{0};
    float averageFps_{0.0f};
    bool showCullingDebug_{false};
    bool showFrustumCullDebug_{false};
    int profilerHistoryCount_{0};
    std::array<float, kProfilerHistoryCount> frameMsHistory_{};
    std::array<float, kProfilerHistoryCount> fpsHistory_{};
    std::array<float, kProfilerHistoryCount> updateMsHistory_{};
    std::array<float, kProfilerHistoryCount> renderMsHistory_{};
    std::array<float, kProfilerHistoryCount> imguiMsHistory_{};
    std::array<float, kProfilerHistoryCount> swapMsHistory_{};
    std::array<float, kProfilerHistoryCount> editorUiMsHistory_{};
    std::array<float, kProfilerHistoryCount> physicsStepMsHistory_{};
};

} // namespace raceman

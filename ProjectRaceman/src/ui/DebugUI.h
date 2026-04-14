#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

namespace raceman {

class Renderer;
class SceneEditor;
class PhysicsWorld;

struct RendererFrameStats;
struct SceneProfilerStats;
struct PhysicsWorldStats;

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
                          glm::vec2 windowAnchor = glm::vec2(-1.0f));

    bool IsEnabled() const { return enabled_; }
    bool IsProfilerVisible() const { return showProfiler_; }
    void SetProfilerVisible(bool visible) { showProfiler_ = visible; }
    bool ShowCullingDebug() const { return showCullingDebug_; }

private:
    bool enabled_{false};
    bool showProfiler_{true};
    float rollingFrameTimeMs_{0.0f};
    float lowestFps_{0.0f};
    bool showCullingDebug_{false};
};

} // namespace raceman

#pragma once

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
                          const PhysicsWorldStats* physicsStats = nullptr);

    bool IsEnabled() const { return enabled_; }
    bool IsProfilerVisible() const { return showProfiler_; }
    void SetProfilerVisible(bool visible) { showProfiler_ = visible; }

private:
    bool enabled_{false};
    bool showProfiler_{true};
    float rollingFrameTimeMs_{0.0f};
    float worstFrameTimeMs_{0.0f};
};

} // namespace raceman

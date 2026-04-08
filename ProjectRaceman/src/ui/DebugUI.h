#pragma once

struct GLFWwindow;

namespace raceman {

class Renderer;

class DebugUI {
public:
    explicit DebugUI(bool enabled);
    ~DebugUI();

    void Initialize(GLFWwindow* window);
    void Shutdown();

    void BeginFrame();
    void EndFrame();
    void RenderDrawData();

    void RenderAppMetrics(float deltaTime, Renderer& renderer);

    bool IsEnabled() const { return enabled_; }

private:
    bool enabled_{false};
};

} // namespace raceman

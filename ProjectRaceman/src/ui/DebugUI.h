#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace raceman {

class Scene;

class DebugUI {
public:
    explicit DebugUI(bool enabled);
    ~DebugUI();

    void Initialize(GLFWwindow* window);
    void Shutdown();

    void BeginFrame();
    void EndFrame();
    void RenderDrawData();

    void RenderSceneSwitcher(const std::vector<std::shared_ptr<Scene>>& scenes,
                             std::size_t activeScene,
                             const std::function<void(std::size_t)>& switchSceneCallback);

    bool IsEnabled() const { return enabled_; }

private:
    bool enabled_{false};
};

} // namespace raceman

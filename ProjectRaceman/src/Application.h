#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct GLFWwindow;

namespace raceman {

class Scene;
class Renderer;
class InputManager;
class DebugUI;
class MenuController;

struct ApplicationConfig {
    std::string windowTitle{"Project Raceman"};
    int width{1920};
    int height{1080};
    bool enableImGui{true};
};

class Application {
public:
    explicit Application(const ApplicationConfig& config = {});
    ~Application();

    void Run();

    Renderer& GetRenderer();
    std::shared_ptr<Renderer> GetRendererPtr();
    InputManager& GetInputManager();
    DebugUI& GetDebugUI();

    void RegisterScene(const std::shared_ptr<Scene>& scene);
    void SwitchScene(std::size_t index);

private:
    void InitializeGlfw();
    void InitializeGlad();
    void InitializeImGui();
    void ShutdownImGui();
    void ShutdownGlfw();
    void PollEvents();
    void Update(float deltaTime);
    void Render();

    ApplicationConfig config_{};
    GLFWwindow* window_{nullptr};

    std::shared_ptr<Renderer> renderer_;
    std::unique_ptr<InputManager> inputManager_;
    std::unique_ptr<DebugUI> debugUi_;
    std::unique_ptr<MenuController> menuController_;

    std::vector<std::shared_ptr<Scene>> scenes_;
    std::size_t activeScene_{0};
    bool running_{true};
    double lastFrameTime_{0.0};
};

} // namespace raceman

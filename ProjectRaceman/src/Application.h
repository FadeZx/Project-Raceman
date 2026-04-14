#pragma once

#include <memory>
#include <string>

#include <glm/glm.hpp>



struct GLFWwindow;

namespace raceman {

class Renderer;
class InputManager;
class DebugUI;
class MenuController;
class Console;
class SceneEditor;
class SkyboxController;

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

    void SetVSync(bool enabled);

private:
    void InitializeGlfw();
    void InitializeGlad();
    void InitializeImGui();
    void ShutdownImGui();
    void ShutdownGlfw();
    void PollEvents();
    void Update(float deltaTime);
    void Render();
    void FocusEditorCameraOn(const glm::vec3& target, float radius);

    ApplicationConfig config_{};
    GLFWwindow* window_{nullptr};

    std::shared_ptr<Renderer> renderer_;
    std::unique_ptr<InputManager> inputManager_;
    std::unique_ptr<DebugUI> debugUi_;
    std::unique_ptr<MenuController> menuController_;
    std::unique_ptr<Console> console_;
    std::unique_ptr<SceneEditor> sceneEditor_;
    std::unique_ptr<SkyboxController> skyboxController_;

    bool running_{true};
    double lastFrameTime_{0.0};

    // Render/physics debug toggles (edited via Project Settings)
    bool frustumCullingEnabled_{true};
    bool physicsCullingEnabled_{true};

    // FPS tracking for window title
    double fpsAccum_{0.0};
    int fpsFrames_{0};
    std::string baseTitle_{};
    bool vsyncEnabled_{true};

    // Editor camera state
    float camPosX_{0.0f}, camPosY_{5.0f}, camPosZ_{10.0f};
    float camYaw_{-90.0f};   // looking towards -Z initially
    float camPitch_{-20.0f};
    float camBaseSpeed_{5.0f};
    float camFastMultiplier_{4.0f};
    float camSlowMultiplier_{0.25f};
    float mouseSensitivity_{0.1f};
    bool rmbHeld_{false};
    bool firstMouse_{true};
    double lastMouseX_{0.0}, lastMouseY_{0.0};
    bool cameraFocusActive_{false};
    glm::vec3 cameraFocusStart_{0.0f};
    glm::vec3 cameraFocusTarget_{0.0f};
    float cameraFocusElapsed_{0.0f};
    float cameraFocusDuration_{0.25f};
};

} // namespace raceman

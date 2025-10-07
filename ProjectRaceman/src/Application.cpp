#include "Application.h"

#include "input/InputManager.h"
#include "rendering/Renderer.h"
#include "scenes/Scene.h"
#include "ui/DebugUI.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <stdexcept>

namespace raceman {

namespace {
void GlfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW Error (%d): %s\n", error, description);
}
} // namespace

Application::Application(const ApplicationConfig& config) : config_(config) {
    InitializeGlfw();
    InitializeGlad();

    renderer_ = std::make_shared<Renderer>(RendererConfig{config.width, config.height});
    if (!inputManager_) {
        inputManager_ = std::make_unique<InputManager>();
        inputManager_->AttachToWindow(window_);
    }
    debugUi_ = std::make_unique<DebugUI>(config.enableImGui);

    if (config.enableImGui) {
        InitializeImGui();
    }

    inputManager_->RegisterKeyCallback([this](int key) {
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
            std::size_t index = static_cast<std::size_t>(key - GLFW_KEY_1);
            SwitchScene(index);
        }
    });

    lastFrameTime_ = glfwGetTime();
}

Application::~Application() {
    if (config_.enableImGui) {
        ShutdownImGui();
    }
    scenes_.clear();
    renderer_.reset();
    debugUi_.reset();
    inputManager_.reset();
    ShutdownGlfw();
}

void Application::Run() {
    while (running_ && !glfwWindowShouldClose(window_)) {
        PollEvents();

        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - lastFrameTime_);
        lastFrameTime_ = currentTime;

        Update(deltaTime);
        Render();
    }
}

Renderer& Application::GetRenderer() { return *renderer_; }
std::shared_ptr<Renderer> Application::GetRendererPtr() { return renderer_; }
InputManager& Application::GetInputManager() { return *inputManager_; }
DebugUI& Application::GetDebugUI() { return *debugUi_; }

void Application::RegisterScene(const std::shared_ptr<Scene>& scene) {
    scenes_.push_back(scene);
}

void Application::SwitchScene(std::size_t index) {
    if (index < scenes_.size()) {
        activeScene_ = index;
        scenes_[activeScene_]->OnSceneActivated();
    }
}

void Application::InitializeGlfw() {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwSetErrorCallback(GlfwErrorCallback);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(config_.width, config_.height, config_.windowTitle.c_str(), nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    if (!inputManager_) {
        inputManager_ = std::make_unique<InputManager>();
    }
    inputManager_->AttachToWindow(window_);
}

void Application::InitializeGlad() {
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        throw std::runtime_error("Failed to initialize GLAD");
    }
}

void Application::InitializeImGui() {
    debugUi_->Initialize(window_);
}

void Application::ShutdownImGui() { debugUi_->Shutdown(); }

void Application::ShutdownGlfw() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

void Application::PollEvents() {
    glfwPollEvents();
    inputManager_->Update();

    if (inputManager_->WasKeyPressed(GLFW_KEY_ESCAPE)) {
        running_ = false;
    }
}

void Application::Update(float deltaTime) {
    if (scenes_.empty()) {
        return;
    }

    auto& scene = scenes_[activeScene_];
    scene->Update(deltaTime);

    if (config_.enableImGui) {
        debugUi_->BeginFrame();
        debugUi_->RenderAppMetrics(deltaTime, *renderer_);
        scene->RenderDebugUi(*debugUi_);
        debugUi_->RenderSceneSwitcher(scenes_, activeScene_, [this](std::size_t index) { SwitchScene(index); });
        debugUi_->EndFrame();
    }
}

void Application::Render() {
    if (scenes_.empty()) {
        return;
    }

    auto& scene = scenes_[activeScene_];
    renderer_->BeginFrame();
    scene->Render(*renderer_);
    renderer_->EndFrame();

    if (config_.enableImGui) {
        debugUi_->RenderDrawData();
    }

    glfwSwapBuffers(window_);
}

} // namespace raceman

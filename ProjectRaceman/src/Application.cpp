#include "Application.h"

#include "input/InputManager.h"
#include "rendering/Renderer.h"
#include "scenes/Scene.h"
#include "ui/DebugUI.h"
#include "ui/MenuController.h"
#include "ui/SceneEditor.h"
#include "ui/Console.h"
#include "scenes/GarageScene.h"
#include "scenes/SimulationScene.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifndef GLFW_TRUE
#define GLFW_TRUE 1
#endif
#ifndef GLFW_FALSE
#define GLFW_FALSE 0
#endif

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
    menuController_ = std::make_unique<MenuController>();

    if (config.enableImGui) {
        InitializeImGui();
        sceneEditor_ = std::make_unique<SceneEditor>();
    }

    inputManager_->RegisterKeyCallback([this](int key) {
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
            std::size_t index = static_cast<std::size_t>(key - GLFW_KEY_1);
            SwitchScene(index);
        }
    });

    lastFrameTime_ = glfwGetTime();
    baseTitle_ = config_.windowTitle;
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

        // FPS accumulation and window title update (once per second)
        fpsAccum_ += deltaTime;
        ++fpsFrames_;
        if (fpsAccum_ >= 1.0) {
            double fps = static_cast<double>(fpsFrames_) / fpsAccum_;
            std::string title = baseTitle_ + " - FPS: " + std::to_string(static_cast<int>(fps + 0.5));
            glfwSetWindowTitle(window_, title.c_str());
            fpsAccum_ = 0.0;
            fpsFrames_ = 0;
        }

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
        scenes_[activeScene_]->Init();
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
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(config_.width, config_.height, config_.windowTitle.c_str(), nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(vsyncEnabled_ ? 1 : 0);

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

void Application::SetVSync(bool enabled) {
    vsyncEnabled_ = enabled;
    // Apply immediately if context is current
    glfwSwapInterval(vsyncEnabled_ ? 1 : 0);
}

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

        // Old renderer panel removed
        // Old per-scene skybox panels are removed from scenes

        // Keep scene-specific debug (non-skybox)
        scene->RenderDebugUi(*debugUi_);

        // Unity-like Scene Editor panels (Scene hierarchy + Inspector)
        if (sceneEditor_) {
            sceneEditor_->RenderUI(deltaTime);
        }


        // Centralized menu (no renderer panel duplication; skybox selection only if wired)
        menuController_->Render(*debugUi_, *renderer_, scenes_, activeScene_,
            [this](std::size_t index) { SwitchScene(index); },
            [this](std::size_t targetScene, const std::array<std::string,6>& faces) {
                if (targetScene < scenes_.size()) {
                    auto scene = scenes_[targetScene];
                    if (auto gs = std::dynamic_pointer_cast<GarageScene>(scene)) {
                        gs->SetSkyboxFaces(faces);
                    } else if (auto ss = std::dynamic_pointer_cast<SimulationScene>(scene)) {
                        ss->SetSkyboxFaces(faces);
                    }
                }
            },
            vsyncEnabled_,
            [this](bool enabled){ SetVSync(enabled); },
            [this](){ if (sceneEditor_) sceneEditor_->AddMeshPlane(); });

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

    // Submit editor meshes (PBR default material) before finalizing the frame
    if (sceneEditor_) {
        sceneEditor_->SubmitDraws(*renderer_);
    }

    renderer_->EndFrame();

    if (config_.enableImGui) {
        debugUi_->RenderDrawData();
    }

    glfwSwapBuffers(window_);
}

} // namespace raceman

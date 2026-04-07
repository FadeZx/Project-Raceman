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
#include <imgui/imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
    console_ = std::make_unique<Console>();

    if (config.enableImGui) {
        InitializeImGui();
        sceneEditor_ = std::make_unique<SceneEditor>();
        sceneEditor_->SetConsole(console_.get());
        sceneEditor_->SetInputManager(inputManager_.get());
        sceneEditor_->SetOnDirty([this](){
            if (!scenes_.empty()) { scenes_[activeScene_]->MarkDirty(); }
        });
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
            std::string sceneName = (!scenes_.empty() ? scenes_[activeScene_]->GetName() : "");
            bool dirty = (!scenes_.empty() ? scenes_[activeScene_]->IsDirty() : false);
            std::string title = std::string("Project Race man") +
                                (sceneName.empty() ? "" : " - " + sceneName) +
                                (dirty ? " *" : "") +
                                " - FPS:" + std::to_string(static_cast<int>(fps + 0.5));
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
        // Set editor file path per scene and load it
        if (sceneEditor_) {
            std::string path = std::string("config/scenes/") + scenes_[activeScene_]->GetName() + ".json";
            sceneEditor_->SetSavePath(path);
            sceneEditor_->Load(path);
        }
        // Reset dirty flag when switching scenes
        scenes_[activeScene_]->MarkClean();
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

    if (inputManager_->WasKeyPressed(GLFW_KEY_ESCAPE)) {
        running_ = false;
    }

    // Ctrl+S: Save active scene if dirty
    bool ctrlDown = inputManager_ && (inputManager_->IsKeyDown(GLFW_KEY_LEFT_CONTROL) || inputManager_->IsKeyDown(GLFW_KEY_RIGHT_CONTROL));
    if (ctrlDown && inputManager_ && inputManager_->WasKeyPressed(GLFW_KEY_S)) {
        if (!scenes_.empty()) {
            auto& scene = scenes_[activeScene_];
            // Save editor scene file
            if (sceneEditor_) {
                std::string path = std::string("config/scenes/") + scene->GetName() + ".json";
                sceneEditor_->SetSavePath(path);
                sceneEditor_->Save(path);
            }
            // Also call scene->Save() if overridden
            if (scene->IsDirty()) {
                scene->Save();
                scene->MarkClean();
                if (console_) {
                    console_->AddLog(std::string("Scene '") + scene->GetName() + "' saved");
                }
            } else {
                if (console_) {
                    console_->AddLog(std::string("No changes to save for scene '") + scene->GetName() + "'");
                }
            }
        }
    }

    // Reset per-frame pressed flags after handling
    inputManager_->Update();
}

void Application::Update(float deltaTime) {
    // Update input
    // (PollEvents already called; here we process per-frame states)
    // Editor camera controls (Unity-like)
    {
        bool wantCaptureMouse = false;

        // RMB hold toggles free look with cursor disabled
        const bool runMode = sceneEditor_ != nullptr && sceneEditor_->IsRunMode();
        int rmb = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT);
        if (!runMode && rmb == GLFW_PRESS && !rmbHeld_) {
            rmbHeld_ = true;
            firstMouse_ = true;
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else if ((runMode || rmb == GLFW_RELEASE) && rmbHeld_) {
            rmbHeld_ = false;
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        if (rmbHeld_) {
            // Mouse look
            double mx, my;
            glfwGetCursorPos(window_, &mx, &my);
            if (firstMouse_) {
                lastMouseX_ = mx; lastMouseY_ = my; firstMouse_ = false;
            }
            double dx = mx - lastMouseX_;
            double dy = my - lastMouseY_;
            lastMouseX_ = mx; lastMouseY_ = my;

            camYaw_   = camYaw_   + static_cast<float>(dx) * mouseSensitivity_;
            camPitch_ = camPitch_ - static_cast<float>(dy) * mouseSensitivity_;
            if (camPitch_ > 89.0f) camPitch_ = 89.0f;
            if (camPitch_ < -89.0f) camPitch_ = -89.0f;

            wantCaptureMouse = true;
        }

        const bool allowKeyboardMove = !runMode && (rmbHeld_
            || !(config_.enableImGui && ImGui::GetCurrentContext() != nullptr
                && ImGui::GetIO().WantTextInput));

        // Keyboard move
        float speed = camBaseSpeed_;
        if (inputManager_ && inputManager_->IsKeyDown(GLFW_KEY_LEFT_SHIFT))  speed *= camFastMultiplier_;
        if (inputManager_ && inputManager_->IsKeyDown(GLFW_KEY_LEFT_CONTROL)) speed *= camSlowMultiplier_;
        float dist = speed * deltaTime;

        // Compute basis from yaw/pitch
        float yawRad = glm::radians(camYaw_);
        float pitchRad = glm::radians(camPitch_);
        glm::vec3 front{
            cosf(yawRad) * cosf(pitchRad),
            sinf(pitchRad),
            sinf(yawRad) * cosf(pitchRad)
        };
        front = glm::normalize(front);
        glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
        glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
        glm::vec3 up = glm::normalize(glm::cross(right, front));

        if (allowKeyboardMove && inputManager_ && inputManager_->IsKeyDown(GLFW_KEY_W)) { camPosX_ += front.x * dist; camPosY_ += front.y * dist; camPosZ_ += front.z * dist; }
        if (allowKeyboardMove && inputManager_ && inputManager_->IsKeyDown(GLFW_KEY_S)) { camPosX_ -= front.x * dist; camPosY_ -= front.y * dist; camPosZ_ -= front.z * dist; }
        if (allowKeyboardMove && inputManager_ && inputManager_->IsKeyDown(GLFW_KEY_A)) { camPosX_ -= right.x * dist; camPosY_ -= right.y * dist; camPosZ_ -= right.z * dist; }
        if (allowKeyboardMove && inputManager_ && inputManager_->IsKeyDown(GLFW_KEY_D)) { camPosX_ += right.x * dist; camPosY_ += right.y * dist; camPosZ_ += right.z * dist; }
        if (allowKeyboardMove && inputManager_ && inputManager_->IsKeyDown(GLFW_KEY_Q)) { camPosX_ -= up.x * dist;    camPosY_ -= up.y * dist;    camPosZ_ -= up.z * dist; }
        if (allowKeyboardMove && inputManager_ && inputManager_->IsKeyDown(GLFW_KEY_E)) { camPosX_ += up.x * dist;    camPosY_ += up.y * dist;    camPosZ_ += up.z * dist; }

        (void)wantCaptureMouse; // reserved for future UI focus logic
    }

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
        menuController_->Render(*renderer_, scenes_, activeScene_,
            [this](std::size_t index) { SwitchScene(index); },
            [this](std::size_t targetScene, const std::array<std::string,6>& faces) {
                if (targetScene < scenes_.size()) {
                    auto scene = scenes_[targetScene];
                    if (auto gs = std::dynamic_pointer_cast<GarageScene>(scene)) {
                        gs->SetSkyboxFaces(faces);
                    } else if (auto ss = std::dynamic_pointer_cast<SimulationScene>(scene)) {
                        ss->SetSkyboxFaces(faces);
                    }
                    // Mark scene dirty after applying skybox
                    scene->MarkDirty();
                }
            },
            vsyncEnabled_,
            [this](bool enabled){ SetVSync(enabled); },
            [this](){
                if (sceneEditor_) sceneEditor_->AddMeshPlane();
                // Mark active scene dirty after adding a mesh
                if (!scenes_.empty()) { scenes_[activeScene_]->MarkDirty(); }
            },
            console_.get());

        debugUi_->EndFrame();
    }
}

void Application::Render() {
    if (scenes_.empty()) {
        return;
    }

    auto& scene = scenes_[activeScene_];
    renderer_->BeginFrame();

    // Set editor camera (Unity-like free fly)
    {
        const auto& cfg = renderer_->GetConfig();
        float aspect = (cfg.height != 0) ? (static_cast<float>(cfg.width) / static_cast<float>(cfg.height)) : 1.0f;

        float yawRad = glm::radians(camYaw_);
        float pitchRad = glm::radians(camPitch_);
        glm::vec3 front{
            cosf(yawRad) * cosf(pitchRad),
            sinf(pitchRad),
            sinf(yawRad) * cosf(pitchRad)
        };
        front = glm::normalize(front);
        glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
        glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
        glm::vec3 up = glm::normalize(glm::cross(right, front));

        glm::vec3 camPos(camPosX_, camPosY_, camPosZ_);
        glm::mat4 view = glm::lookAt(camPos, camPos + front, up);
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 500.0f);
        renderer_->SetCamera(view, proj);
    }
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

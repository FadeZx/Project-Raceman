#include "Application.h"

#include "input/InputManager.h"
#include "physics/PhysicsWorld.h"
#include "rendering/Renderer.h"
#include "rendering/SkyboxController.h"
#include "ui/DebugUI.h"
#include "ui/MenuController.h"
#include "ui/SceneEditor.h"
#include "ui/Console.h"

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
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace raceman {

namespace {
void GlfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW Error (%d): %s\n", error, description);
}

std::string SceneDisplayName(const std::string& scenePath) {
    std::string filename = std::filesystem::path(scenePath).filename().string();
    const std::string suffix = ".scene.json";
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower.size() >= suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
        filename.resize(filename.size() - suffix.size());
        filename += ".scene";
    }
    return filename;
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
    skyboxController_ = std::make_unique<SkyboxController>();
    skyboxController_->Reload();

    if (config.enableImGui) {
        InitializeImGui();
        sceneEditor_ = std::make_unique<SceneEditor>();
        sceneEditor_->SetConsole(console_.get());
        sceneEditor_->SetInputManager(inputManager_.get());
        sceneEditor_->SetOnFocusObject([this](const glm::vec3& target, float radius) {
            FocusEditorCameraOn(target, radius);
        });
        // Wire the Game View "Stats" button to DebugUI's profiler visibility
        sceneEditor_->SetProfilerCallbacks(
            [this]() { return debugUi_->IsProfilerVisible(); },
            [this](bool v) { debugUi_->SetProfilerVisible(v); });
    }

    lastFrameTime_ = glfwGetTime();
    baseTitle_ = config_.windowTitle;
}

Application::~Application() {
    if (config_.enableImGui) {
        ShutdownImGui();
    }
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
            std::string sceneName = sceneEditor_ ? SceneDisplayName(sceneEditor_->GetCurrentScenePath()) : "";
            std::string title = std::string("Project Race man") +
                                (sceneName.empty() ? "" : " - " + sceneName) +
                                " - FPS:" + std::to_string(static_cast<int>(fps + 0.5));
            glfwSetWindowTitle(window_, title.c_str());
            fpsAccum_ = 0.0;
            fpsFrames_ = 0;
        }

        Update(deltaTime);
        Render();
        if (inputManager_) {
            inputManager_->Update();
        }
    }
}

Renderer& Application::GetRenderer() { return *renderer_; }
std::shared_ptr<Renderer> Application::GetRendererPtr() { return renderer_; }
InputManager& Application::GetInputManager() { return *inputManager_; }
DebugUI& Application::GetDebugUI() { return *debugUi_; }

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

    if (renderer_) {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window_, &framebufferWidth, &framebufferHeight);
        renderer_->Resize(framebufferWidth, framebufferHeight);
    }
}

void Application::FocusEditorCameraOn(const glm::vec3& target, float radius) {
    const float yawRad = glm::radians(camYaw_);
    const float pitchRad = glm::radians(camPitch_);
    glm::vec3 front{
        cosf(yawRad) * cosf(pitchRad),
        sinf(pitchRad),
        sinf(yawRad) * cosf(pitchRad)
    };
    if (glm::length(front) < 0.0001f) {
        front = {0.0f, 0.0f, -1.0f};
    } else {
        front = glm::normalize(front);
    }

    const float focusDistance = (std::max)(4.0f, radius * 2.5f);
    const glm::vec3 newPosition = target - front * focusDistance;
    cameraFocusStart_ = {camPosX_, camPosY_, camPosZ_};
    cameraFocusTarget_ = newPosition;
    cameraFocusElapsed_ = 0.0f;
    cameraFocusDuration_ = 0.25f;
    cameraFocusActive_ = true;
    firstMouse_ = true;
}

void Application::Update(float deltaTime) {
    // Update input
    // (PollEvents already called; here we process per-frame states)
    // Editor camera controls (Unity-like)
    {
        bool wantCaptureMouse = false;
        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window_, &mouseX, &mouseY);
        const bool mouseInEditorViewport = sceneEditor_ == nullptr || sceneEditor_->ContainsSceneViewportPoint(static_cast<float>(mouseX), static_cast<float>(mouseY));

        // RMB hold toggles free look with cursor disabled. Play mode keeps Scene view editable,
        // but Game view stays controlled by the runtime camera.
        const bool allowEditorCamera = sceneEditor_ == nullptr || sceneEditor_->IsSceneViewportActiveForEditorControls();
        if (allowEditorCamera && cameraFocusActive_ && !rmbHeld_) {
            cameraFocusElapsed_ += deltaTime;
            const float t = (std::min)(1.0f, cameraFocusElapsed_ / (std::max)(0.001f, cameraFocusDuration_));
            const float eased = 1.0f - (1.0f - t) * (1.0f - t);
            const glm::vec3 position = cameraFocusStart_ + (cameraFocusTarget_ - cameraFocusStart_) * eased;
            camPosX_ = position.x;
            camPosY_ = position.y;
            camPosZ_ = position.z;
            if (t >= 1.0f) {
                cameraFocusActive_ = false;
            }
        }
        int rmb = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT);
        if (allowEditorCamera && mouseInEditorViewport && rmb == GLFW_PRESS && !rmbHeld_) {
            rmbHeld_ = true;
            cameraFocusActive_ = false;
            firstMouse_ = true;
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else if ((!allowEditorCamera || rmb == GLFW_RELEASE) && rmbHeld_) {
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

        const bool allowKeyboardMove = allowEditorCamera && (rmbHeld_
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

    if (config_.enableImGui) {
        debugUi_->BeginFrame();
        // Note: RenderProfilerHud() removed — toggle button is now in the Game View toolbar

        // Unity-like Scene Editor panels (Scene hierarchy + Inspector)
        if (sceneEditor_) {
            sceneEditor_->SetSceneViewportTexture(renderer_->GetViewportRenderTargetTexture(ViewportRenderTarget::Scene));
            sceneEditor_->SetGameViewportTexture(renderer_->GetViewportRenderTargetTexture(ViewportRenderTarget::Game));
            sceneEditor_->RenderUI(deltaTime);
        }
        const SceneProfilerStats sceneStats = sceneEditor_ ? sceneEditor_->CollectProfilerStats() : SceneProfilerStats{};
        const PhysicsWorldStats* physicsStats = nullptr;
        if (sceneEditor_ && sceneEditor_->GetPhysicsWorld()) {
            physicsStats = &sceneEditor_->GetPhysicsWorld()->GetStats();
        }

        // Centralized menu (no renderer panel duplication; skybox selection only if wired)
        menuController_->Render(*renderer_,
            vsyncEnabled_,
            [this](bool enabled){ SetVSync(enabled); },
            debugUi_->IsProfilerVisible(),
            [this](bool visible){ debugUi_->SetProfilerVisible(visible); },
            [this](){
                if (sceneEditor_) sceneEditor_->AddMeshPlane();
            },
            console_.get(),
            sceneEditor_ ? EditorProjectMenu{
                sceneEditor_->GetProjectName(),
                sceneEditor_->GetCurrentScenePath(),
                sceneEditor_->GetSceneAssetPaths(),
                [this](const std::string& sceneName) {
                    if (sceneEditor_) sceneEditor_->NewScene(sceneName);
                },
                [this]() {
                    if (sceneEditor_) sceneEditor_->SaveCurrentScene();
                },
                [this](const std::string& scenePath) {
                    if (sceneEditor_) sceneEditor_->OpenSceneAsset(scenePath);
                },
                [this]() {
                    if (sceneEditor_) sceneEditor_->SaveProject();
                },
                [this]() {
                    if (sceneEditor_) sceneEditor_->RenderProjectPhysicsSettings();
                }
            } : EditorProjectMenu{},
            [this](const SkyboxFaces& faces) {
                if (skyboxController_) {
                    skyboxController_->SetFaces(faces);
                    skyboxController_->Reload();
                }
            },
            &frustumCullingEnabled_,
            &physicsCullingEnabled_);

        // Anchor the stats overlay to the top-left of the Game View
        glm::vec2 statsAnchor(-1.0f);
        if (sceneEditor_) {
            const glm::vec2 gpSize = sceneEditor_->GetGameViewportSize();
            if (gpSize.x > 1.0f && gpSize.y > 1.0f) {
                statsAnchor = sceneEditor_->GetGameViewportPos();
            }
        }
        debugUi_->RenderAppMetrics(deltaTime, *renderer_, sceneEditor_ ? &sceneStats : nullptr, physicsStats, statsAnchor);

        if (sceneEditor_) {
            sceneEditor_->SetShowCullingDebug(debugUi_->ShowCullingDebug());
            sceneEditor_->SetShowFrustumCullDebug(debugUi_->ShowFrustumCullDebug());
            sceneEditor_->SetFrustumCullingEnabled(frustumCullingEnabled_);
            sceneEditor_->SetPhysicsCullingEnabled(physicsCullingEnabled_);
        }

        debugUi_->EndFrame();
    }
}

void Application::Render() {
    const auto& cfg = renderer_->GetConfig();
    RendererSettings& rendererSettings = renderer_->GetSettings();
    const glm::vec3 previousClearColor = rendererSettings.clearColor;
    renderer_->ResetFrameStats();

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, cfg.width, cfg.height);
    glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto renderScenePass = [&](const RendererViewport& viewport) {
        if (viewport.width <= 1 || viewport.height <= 1) {
            return;
        }

        const float aspect = static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
        const float yawRad = glm::radians(camYaw_);
        const float pitchRad = glm::radians(camPitch_);
        glm::vec3 front{
            cosf(yawRad) * cosf(pitchRad),
            sinf(pitchRad),
            sinf(yawRad) * cosf(pitchRad)
        };
        front = glm::normalize(front);
        const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
        const glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
        const glm::vec3 up = glm::normalize(glm::cross(right, front));

        const glm::vec3 camPos(camPosX_, camPosY_, camPosZ_);
        const glm::mat4 view = glm::lookAt(camPos, camPos + front, up);
        const glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 500.0f);

        renderer_->SetViewport(viewport);
        renderer_->EnsureViewportRenderTarget(ViewportRenderTarget::Scene, viewport.width, viewport.height);
        renderer_->BeginFrameToViewportTarget(ViewportRenderTarget::Scene, previousClearColor);
        renderer_->SetCamera(view, proj);
        if (skyboxController_) {
            skyboxController_->Draw(view, proj);
        }
        if (sceneEditor_) {
            sceneEditor_->SubmitDraws(*renderer_, true);
        }
        renderer_->EndFrameToViewportTarget();
    };

    auto renderGamePass = [&](const RendererViewport& viewport) {
        if (viewport.width <= 1 || viewport.height <= 1) {
            return;
        }
        // Reset stats here so the profiler shows game-pass numbers only,
        // not the cumulative total across both scene and game passes.
        renderer_->ResetFrameStats();

        const float aspect = static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
        glm::vec4 gameClearColor{0.02f, 0.02f, 0.02f, 1.0f};
        const bool usingGameCamera = sceneEditor_ && sceneEditor_->TryGetGameCamera(view, proj, aspect, &gameClearColor);

        renderer_->SetViewport(viewport);
        const glm::vec3 passClearColor = usingGameCamera
            ? glm::vec3(gameClearColor.r, gameClearColor.g, gameClearColor.b)
            : glm::vec3(0.02f, 0.02f, 0.02f);
        renderer_->EnsureViewportRenderTarget(ViewportRenderTarget::Game, viewport.width, viewport.height);
        renderer_->BeginFrameToViewportTarget(ViewportRenderTarget::Game, passClearColor);
        if (usingGameCamera) {
            renderer_->SetCamera(view, proj);
            if (skyboxController_) {
                skyboxController_->Draw(view, proj);
            }
            if (sceneEditor_) {
                sceneEditor_->SubmitDraws(*renderer_, false);
            }
        }
        renderer_->EndFrameToViewportTarget();
    };

    if (sceneEditor_) {
        renderScenePass(sceneEditor_->GetSceneRenderViewport(cfg.width, cfg.height));
        renderGamePass(sceneEditor_->GetGameRenderViewport(cfg.width, cfg.height));
    } else {
        renderScenePass(RendererViewport{0, 0, cfg.width, cfg.height});
    }

    rendererSettings.clearColor = previousClearColor;

    if (config_.enableImGui) {
        debugUi_->RenderDrawData();
    }

    glfwSwapBuffers(window_);
}

} // namespace raceman

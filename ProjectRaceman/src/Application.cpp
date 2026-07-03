#include "Application.h"

#include "audio/AudioManager.h"
#include "build/BuildSystem.h"
#include "input/InputManager.h"
#include "physics/PhysicsWorld.h"
#include "rendering/Renderer.h"
#include "rendering/SkyboxController.h"
#include "ui/DebugUI.h"
#include "ui/MenuController.h"
#include "ui/ProjectLauncher.h"
#include "ui/SceneEditor.h"
#include "ui/Console.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#include <GLFW/glfw3native.h>
#endif
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
#include <cmath>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

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

void OpenFolderInExplorer(const std::string& folder) {
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)folder;
#endif
}

std::filesystem::path FindEngineRootForApplication() {
    namespace fs = std::filesystem;
    if (fs::exists("ProjectRaceman/src") && fs::is_directory("ProjectRaceman/src")) {
        return fs::absolute("ProjectRaceman").lexically_normal();
    }
    if (fs::exists("src") && fs::is_directory("src")) {
        return fs::absolute(".").lexically_normal();
    }
    return fs::absolute(".").lexically_normal();
}

void ApplyRacemanWindowIcon(GLFWwindow* window) {
#if defined(_WIN32)
    if (window == nullptr) {
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr) {
        return;
    }

    static HICON bigIcon = nullptr;
    static HICON smallIcon = nullptr;
    const std::filesystem::path iconPath =
        FindEngineRootForApplication() / "editor-assets" / "icons" / "RaceMan_icon.ico";
    const std::string iconFile = iconPath.string();
    if (bigIcon == nullptr) {
        bigIcon = static_cast<HICON>(LoadImageA(
            nullptr, iconFile.c_str(), IMAGE_ICON, 256, 256, LR_LOADFROMFILE));
    }
    if (smallIcon == nullptr) {
        smallIcon = static_cast<HICON>(LoadImageA(
            nullptr, iconFile.c_str(), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
            LR_LOADFROMFILE));
    }
    if (bigIcon != nullptr) {
        SendMessageA(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
    }
    if (smallIcon != nullptr) {
        SendMessageA(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }
#else
    (void)window;
#endif
}

void PlayerStartupLog(const ApplicationConfig& config, const char* message) {
    if (config.playerMode) {
        std::cout << "[Player] " << message << std::endl;
    }
}
} // namespace

Application::Application(const ApplicationConfig& config) : config_(config) {
    PlayerStartupLog(config_, "Initializing GLFW...");
    InitializeGlfw();
    PlayerStartupLog(config_, "Initializing OpenGL...");
    InitializeGlad();

    PlayerStartupLog(config_, "Creating renderer...");
    renderer_ = std::make_shared<Renderer>(RendererConfig{config.width, config.height});
    PlayerStartupLog(config_, "Creating input...");
    if (!inputManager_) {
        inputManager_ = std::make_unique<InputManager>();
        inputManager_->AttachToWindow(window_);
    }
    PlayerStartupLog(config_, "Initializing audio...");
    audioManager_ = std::make_unique<AudioManager>();
    audioManager_->Initialize();
    PlayerStartupLog(config_, "Creating editor/player services...");

    debugUi_ = std::make_unique<DebugUI>(config.enableImGui);
    menuController_ = std::make_unique<MenuController>();
    console_ = std::make_unique<Console>();
    skyboxController_ = std::make_unique<SkyboxController>();
    PlayerStartupLog(config_, "Loading skybox...");
    skyboxController_->Reload();
    PlayerStartupLog(config_, "Skybox loaded.");

    if (config.enableImGui) {
        InitializeImGui();
    }

    if (config.playerMode && config.enableImGui) {
        playerDebugMode_ = true;
    }

    if (config.playerMode) {
        auto consoleLog = [&](const char* msg) {
            PlayerStartupLog(config_, msg);
            if (console_) console_->AddLog(std::string("[Player] ") + msg);
        };
        // Player mode: create editor directly with the specified project root.
        consoleLog("Creating scene editor...");
        sceneEditor_ = std::make_unique<SceneEditor>();
        consoleLog("Scene editor created.");
        sceneEditor_->SetConsole(console_.get());
        sceneEditor_->SetInputManager(inputManager_.get());
        sceneEditor_->SetAudioManager(audioManager_.get());
        consoleLog("Loading project metadata...");
        sceneEditor_->SetProjectRoot(config.projectRoot);
        const std::string loadingTitle = config_.windowTitle + " - Loading...";
        glfwSetWindowTitle(window_, loadingTitle.c_str());
        consoleLog("Startup complete; runtime will begin after first frame.");
    } else if (config.enableImGui) {
        // Editor mode: open the most recent project, or show the launcher.
        const auto recent = ProjectLauncher::LoadRegistry();
        if (!recent.empty() && std::filesystem::exists(recent.front().path)) {
            InitializeEditor(recent.front().path);
        } else {
            launcher_ = std::make_unique<ProjectLauncher>();
        }
    }

    lastFrameTime_ = glfwGetTime();
    baseTitle_ = config_.windowTitle;
}

void Application::InitializeEditor(const std::string& projectPath) {
    if (sceneEditor_) {
        sceneEditor_->StopRuntime();
        sceneEditor_.reset();
    }
    launcher_.reset();

#if defined(_WIN32)
    _putenv_s("RACEMAN_PROJECT_ROOT", projectPath.c_str());
#else
    setenv("RACEMAN_PROJECT_ROOT", projectPath.c_str(), 1);
#endif

    sceneEditor_ = std::make_unique<SceneEditor>();
    sceneEditor_->SetConsole(console_.get());
    sceneEditor_->SetInputManager(inputManager_.get());
    sceneEditor_->SetAudioManager(audioManager_.get());
    sceneEditor_->SetOnFocusObject([this](const glm::vec3& target, float radius) {
        FocusEditorCameraOn(target, radius);
    });
    sceneEditor_->SetOnEditorCameraViewChanged([this](const glm::mat4& view) {
        const glm::mat4 inv = glm::inverse(view);
        const glm::vec3 pos(inv[3]);
        glm::vec3 forward = -glm::vec3(inv[2]);
        const float len = glm::length(forward);
        if (len < 1e-5f) return;
        forward /= len;
        float pitch = glm::degrees(asinf(glm::clamp(forward.y, -1.0f, 1.0f)));
        float yaw   = glm::degrees(atan2f(forward.z, forward.x));
        if (pitch >  89.0f) pitch =  89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
        camPosX_ = pos.x; camPosY_ = pos.y; camPosZ_ = pos.z;
        camYaw_  = yaw;   camPitch_ = pitch;
        cameraFocusActive_ = false;
    });
    sceneEditor_->SetProfilerCallbacks(
        [this]() { return debugUi_->IsProfilerVisible(); },
        [this](bool v) { debugUi_->SetProfilerVisible(v); });

    ProjectLauncher::AddToRegistry(std::filesystem::path(projectPath).filename().string(), projectPath);

    // Apply the project's saved skybox (if any face is non-empty) to the controller.
    const SkyboxFaces& savedFaces = sceneEditor_->GetSkyboxFaces();
    if (!savedFaces[0].empty() && skyboxController_) {
        skyboxController_->SetFaces(savedFaces);
        skyboxController_->Reload();
    }
}

void Application::RenderPlayerDebugOverlay(float deltaTime) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))  playerDebugStatsOpen_   = !playerDebugStatsOpen_;
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) playerDebugConsoleOpen_ = !playerDebugConsoleOpen_;

    // Stats strip — toggled by F1
    if (playerDebugStatsOpen_) {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f));
    ImGui::Begin("##dbg_stats", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("FPS: %.0f  dt: %.2f ms  [F1 stats | F12 console]", io.Framerate, deltaTime * 1000.0f);
    ImGui::Text("Runtime: %s", playerRuntimeStarted_ ? "Running" : "Loading...");
    // Snapshot physics build progress once — used in both the stats strip and the console panel.
    const PhysicsBuildProgress* prog = sceneEditor_ ? sceneEditor_->GetPhysicsBuildProgress() : nullptr;
    const int progDone  = prog ? prog->stepsDone.load() : 0;
    const int progTotal = prog ? prog->stepsTotal.load() : 0;
    const std::string progTask = prog ? prog->GetTask() : std::string{};
    const float progFraction = (prog && progTotal > 0) ? static_cast<float>(progDone) / static_cast<float>(progTotal) : -1.0f;

    if (sceneEditor_) {
        if (prog) {
            if (progTotal > 0) {
                ImGui::Text("Physics: %d / %d", progDone, progTotal);
            } else {
                ImGui::TextUnformatted("Physics: cooking...");
            }
            // Thin progress bar in the stats strip (marquee when total unknown).
            ImGui::ProgressBar(progFraction, ImVec2(-1.0f, 4.0f), "");
        } else {
            const bool hasPhysics = sceneEditor_->GetPhysicsWorld() != nullptr;
            ImGui::Text("Physics: %s", hasPhysics ? "Active" : "None");
        }
        const std::string& projName = config_.playerMode ? config_.windowTitle : sceneEditor_->GetProjectName();
        ImGui::Text("Project: %s", projName.empty() ? "(none)" : projName.c_str());
    }
    ImGui::End();
    } // end playerDebugStatsOpen_

    // Console slides up from the bottom of the screen.
    const float consoleH = vp->Size.y * 0.40f;
    const float openY   = vp->Size.y - consoleH - 10.0f;
    const float closedY = vp->Size.y + 10.0f;
    const float targetY = playerDebugConsoleOpen_ ? openY : closedY;
    consoleSlideY_ += (targetY - consoleSlideY_) * (std::min)(1.0f, deltaTime * 14.0f);
    if (std::abs(consoleSlideY_ - targetY) < 0.5f) consoleSlideY_ = targetY;

    // Console panel — slides up from the bottom.
    ImGui::SetNextWindowPos(ImVec2(10.0f, consoleSlideY_), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x - 20.0f, consoleH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.90f);
    ImGui::Begin("##dbg_console", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    if (console_) {
        console_->RenderFlat();
    }
    ImGui::End();
}

Application::~Application() {
    if (standaloneBuildThread_ && standaloneBuildThread_->joinable()) {
        standaloneBuildThread_->join();
    }
    if (sceneEditor_) {
        sceneEditor_->StopRuntime();
    }
    sceneEditor_.reset(); // stop audio sources before audio engine shuts down
    launcher_.reset();
    if (config_.enableImGui) {
        ShutdownImGui();
    }
    audioManager_.reset(); // shuts down irrKlang
    renderer_.reset();
    debugUi_.reset();
    inputManager_.reset();
    ShutdownGlfw();
}

void Application::Run() {
    while (running_) {
        const double pollStart = glfwGetTime();
        PollEvents();
        frameTimings_.pollMs = static_cast<float>((glfwGetTime() - pollStart) * 1000.0);
        if (inputManager_) {
            inputManager_->BeginFrame();
        }

        // Intercept the OS close request here — safe because all members are alive.
        if (glfwWindowShouldClose(window_)) {
            if (sceneEditor_ && sceneEditor_->IsSceneDirty()) {
                // Cancel GLFW's close flag and ask the user via the ImGui modal.
                glfwSetWindowShouldClose(window_, GLFW_FALSE);
                pendingExit_ = true;
            } else {
                break;  // clean scene — exit immediately
            }
        }

        if (config_.playerMode && sceneEditor_ && !playerRuntimeStarted_) {
            glfwSetWindowTitle(window_, "Project Raceman - Loading project...");
            if (console_) console_->AddLog("[Player] Starting runtime...");
            Render();
            if (inputManager_) {
                inputManager_->EndFrame();
            }

            std::cout << "[Player] Calling StartRuntime()..." << std::endl;
            if (console_) console_->AddLog("[Player] Starting runtime (building physics in background)...");
            sceneEditor_->StartRuntime();
            playerRuntimeStarted_ = true;
            std::cout << "[Player] StartRuntime() returned. Physics cooking in background." << std::endl;
            lastFrameTime_ = glfwGetTime();
            glfwSetWindowTitle(window_, config_.windowTitle.c_str());
            continue;
        }

        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - lastFrameTime_);
        lastFrameTime_ = currentTime;

        // FPS accumulation and window title update (once per second)
        fpsAccum_ += deltaTime;
        ++fpsFrames_;
        if (fpsAccum_ >= 1.0) {
            double fps = static_cast<double>(fpsFrames_) / fpsAccum_;
            std::string projectName = config_.playerMode
                ? config_.windowTitle
                : (sceneEditor_ ? sceneEditor_->GetProjectName() : "");
            if (projectName.empty()) projectName = baseTitle_;
            std::string sceneName = sceneEditor_ ? SceneDisplayName(sceneEditor_->GetCurrentScenePath()) : "";
            std::string title = projectName +
                                (sceneName.empty() ? "" : " - " + sceneName) +
                                " - FPS:" + std::to_string(static_cast<int>(fps + 0.5));
            glfwSetWindowTitle(window_, title.c_str());
            fpsAccum_ = 0.0;
            fpsFrames_ = 0;
        }

        const double updateStart = glfwGetTime();
        Update(deltaTime);
        frameTimings_.updateMs = static_cast<float>((glfwGetTime() - updateStart) * 1000.0);

        const double renderStart = glfwGetTime();
        Render();
        frameTimings_.renderMs = static_cast<float>((glfwGetTime() - renderStart) * 1000.0);
        if (inputManager_) {
            inputManager_->EndFrame();
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
    ApplyRacemanWindowIcon(window_);

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
    if (standaloneBuildStatus_ && standaloneBuildStatus_->isDone.load()) {
        if (standaloneBuildThread_ && standaloneBuildThread_->joinable()) {
            standaloneBuildThread_->join();
        }
        bool ok;
        std::string msg, folder;
        {
            std::lock_guard<std::mutex> lock(standaloneBuildStatus_->resultMutex);
            ok     = standaloneBuildStatus_->success;
            msg    = standaloneBuildStatus_->message;
            folder = standaloneBuildStatus_->outputFolder;
        }
        if (console_) {
            if (ok) {
                console_->AddLog(msg);
                OpenFolderInExplorer(folder);
            } else {
                console_->AddError(msg);
            }
        }
        standaloneBuildThread_.reset();
        standaloneBuildStatus_.reset();
    }

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
        if (sceneEditor_) {
            sceneEditor_->SetEditorCameraNavigating(rmbHeld_);
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

        const bool allowKeyboardMove = allowEditorCamera && rmbHeld_
            && !(config_.enableImGui && ImGui::GetCurrentContext() != nullptr
                && ImGui::GetIO().WantTextInput);

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

    if (config_.playerMode && sceneEditor_) {
        sceneEditor_->UpdateRuntime(deltaTime);
    }

    if (config_.enableImGui) {
        debugUi_->BeginFrame();

        if (playerDebugMode_) {
            RenderPlayerDebugOverlay(deltaTime);
            debugUi_->EndFrame();
            return;
        }

        // Show project launcher if no project is open yet (or user opened the launcher)
        if (launcher_) {
            std::string pendingProject;
            launcher_->Render([&pendingProject](const std::string& path) {
                pendingProject = path;
            });
            debugUi_->EndFrame();
            if (!pendingProject.empty()) {
                InitializeEditor(pendingProject);
            }
            return;
        }

        // Standalone build progress popup — must appear before the dockspace so the
        // ID stack is clean, matching the pattern used by RenderPlayModeLoadingPopup().
        if (standaloneBuildStatus_ && !standaloneBuildStatus_->isDone.load()) {
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(460.0f, 140.0f), ImGuiCond_Always);
            ImGui::OpenPopup("###StandaloneBuild");
            if (ImGui::BeginPopupModal("Building Standalone...###StandaloneBuild", nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Compiling and packaging build...");
                ImGui::Spacing();
                const float pulse = static_cast<float>(std::fmod(ImGui::GetTime() * 0.45, 1.0));
                ImGui::ProgressBar(pulse, ImVec2(-1.0f, 0.0f), "build-game.ps1");
                ImGui::Spacing();
                ImGui::TextDisabled("Running MSBuild and copying project assets.");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::BeginDisabled();
                ImGui::Button("Cancel", ImVec2(100.0f, 0.0f));
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
        }

        // Unity-like Scene Editor panels (Scene hierarchy + Inspector)
        if (sceneEditor_) {
            sceneEditor_->SetSceneViewportTexture(renderer_->GetViewportRenderTargetTexture(ViewportRenderTarget::Scene));
            sceneEditor_->SetGameViewportTexture(renderer_->GetViewportRenderTargetTexture(ViewportRenderTarget::Game));
            const float sceneAspect = sceneEditor_->GetSceneViewportSize().y > 0.5f
                ? sceneEditor_->GetSceneViewportSize().x / sceneEditor_->GetSceneViewportSize().y
                : 1.0f;
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
            sceneEditor_->SetEditorCameraMatrices(
                glm::lookAt(camPos, camPos + front, up),
                glm::perspective(glm::radians(60.0f), sceneAspect, sceneCameraNearClip_, sceneCameraFarClip_));
            sceneEditor_->RenderUI(deltaTime);
        }
        const bool profilerVisible = debugUi_->IsProfilerVisible();
        const SceneProfilerStats sceneStats = (profilerVisible && sceneEditor_)
            ? sceneEditor_->CollectProfilerStats()
            : SceneProfilerStats{};
        const PhysicsWorldStats* physicsStats = nullptr;
        if (profilerVisible && sceneEditor_ && sceneEditor_->GetPhysicsWorld()) {
            physicsStats = &sceneEditor_->GetPhysicsWorld()->GetStats();
        }

        // Centralized menu (no renderer panel duplication; skybox selection only if wired)
        menuController_->Render(*renderer_,
            vsyncEnabled_,
            [this](bool enabled){ SetVSync(enabled); },
            profilerVisible,
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
                [this](const std::string& outputFolder) {
                    if (standaloneBuildStatus_ && !standaloneBuildStatus_->isDone.load()) {
                        if (console_) console_->AddWarning("A standalone build is already in progress.");
                        return;
                    }
                    if (console_) console_->AddLog("Building standalone game to: " + outputFolder);
                    if (sceneEditor_) {
                        sceneEditor_->SaveCurrentScene();
                        sceneEditor_->SaveProject();
                        sceneEditor_->SyncScripts();
                    }
                    const std::string projectRoot = sceneEditor_ ? sceneEditor_->GetProjectRoot() : std::string{};
                    auto status = std::make_shared<StandaloneBuildStatus>();
                    status->outputFolder = outputFolder;
                    standaloneBuildStatus_ = status;
                    standaloneBuildThread_ = std::make_unique<std::thread>([status, projectRoot]() {
                        const BuildResult result = BuildStandaloneGame(status->outputFolder, projectRoot);
                        std::lock_guard<std::mutex> lock(status->resultMutex);
                        status->success = result.success;
                        status->message = result.message;
                        status->isDone.store(true);
                    });
                },
                [this]() {
                    if (sceneEditor_) sceneEditor_->StopRuntime();
                    launcher_ = std::make_unique<ProjectLauncher>();
                },
                [this]() {
                    if (sceneEditor_) sceneEditor_->RenderProjectInputSettings();
                },
                [this]() {
                    if (sceneEditor_) sceneEditor_->RenderProjectPhysicsSettings();
                },
                [this]() {
                    if (sceneEditor_) sceneEditor_->RenderProjectTagsAndLayersSettings();
                }
            } : EditorProjectMenu{},
            [this](const SkyboxFaces& faces) {
                if (skyboxController_) {
                    skyboxController_->SetFaces(faces);
                    skyboxController_->Reload();
                }
                if (sceneEditor_) {
                    sceneEditor_->SetSkyboxFaces(faces);
                    sceneEditor_->SaveProject();
                }
            },
            &frustumCullingEnabled_,
            &physicsCullingEnabled_,
            &sceneCameraNearClip_,
            &sceneCameraFarClip_);

        // Anchor the stats overlay to the top-left of the Game View
        glm::vec2 statsAnchor(-1.0f);
        if (sceneEditor_) {
            const glm::vec2 gpSize = sceneEditor_->GetGameViewportSize();
            if (gpSize.x > 1.0f && gpSize.y > 1.0f) {
                statsAnchor = sceneEditor_->GetGameViewportPos();
            }
        }
        debugUi_->RenderAppMetrics(
            deltaTime,
            *renderer_,
            sceneEditor_ ? &sceneStats : nullptr,
            physicsStats,
            &frameTimings_,
            sceneEditor_ ? &sceneEditor_->GetFrameTimings() : nullptr,
            statsAnchor);

        if (sceneEditor_) {
            sceneEditor_->SetShowCullingDebug(debugUi_->ShowCullingDebug());
            sceneEditor_->SetShowFrustumCullDebug(debugUi_->ShowFrustumCullDebug());
            sceneEditor_->SetFrustumCullingEnabled(frustumCullingEnabled_);
            sceneEditor_->SetPhysicsCullingEnabled(physicsCullingEnabled_);
        }

        // ── Unsaved-changes exit dialog ────────────────────────────────────────
        if (pendingExit_) {
            ImGui::OpenPopup("Unsaved Changes##exitDlg");
            pendingExit_ = false;
        }
        // Centre the popup over the main display area.
        ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Unsaved Changes##exitDlg", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::TextUnformatted("The scene has unsaved changes.");
            ImGui::TextDisabled("Do you want to save before exiting?");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const float btnW = 110.0f;
            if (ImGui::Button("Save & Exit", ImVec2(btnW, 0))) {
                if (sceneEditor_) sceneEditor_->SaveCurrentScene();
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard & Exit", ImVec2(btnW, 0))) {
                if (sceneEditor_) sceneEditor_->MarkSceneClean();
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(btnW, 0)) ||
                (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        debugUi_->EndFrame();
    }
}

void Application::Render() {
    const auto& cfg = renderer_->GetConfig();
    RendererSettings& rendererSettings = renderer_->GetSettings();
    const glm::vec3 previousClearColor = rendererSettings.clearColor;
    frameTimings_.scenePassMs = 0.0f;
    frameTimings_.gamePassMs = 0.0f;
    frameTimings_.imguiRenderMs = 0.0f;
    frameTimings_.swapMs = 0.0f;
    renderer_->ResetFrameStats();

    if (launcher_) {
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, cfg.width, cfg.height);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (config_.enableImGui) {
            const double imguiStart = glfwGetTime();
            debugUi_->RenderDrawData();
            frameTimings_.imguiRenderMs = static_cast<float>((glfwGetTime() - imguiStart) * 1000.0);
        }
        const double swapStart = glfwGetTime();
        glfwSwapBuffers(window_);
        frameTimings_.swapMs = static_cast<float>((glfwGetTime() - swapStart) * 1000.0);
        return;
    }

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, cfg.width, cfg.height);
    glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (config_.playerMode && sceneEditor_) {
        renderer_->ResetFrameStats();
        RendererViewport viewport{0, 0, cfg.width, cfg.height};
        renderer_->SetViewport(viewport);

        // Only render the game scene once physics is ready; show a clear screen while loading.
        if (sceneEditor_->GetPhysicsWorld() != nullptr) {
            const float aspect = cfg.height > 0 ? static_cast<float>(cfg.width) / static_cast<float>(cfg.height) : 1.0f;
            glm::mat4 view{1.0f};
            glm::mat4 proj{1.0f};
            glm::vec4 gameClearColor{0.02f, 0.02f, 0.02f, 1.0f};
            if (sceneEditor_->TryGetGameCamera(view, proj, aspect, &gameClearColor)) {
                RendererSettings& settings = renderer_->GetSettings();
                settings.clearColor = glm::vec3(gameClearColor.r, gameClearColor.g, gameClearColor.b);
                renderer_->SetCamera(view, proj);
                renderer_->BeginFrame();
                if (skyboxController_) {
                    skyboxController_->Draw(view, proj);
                }
                sceneEditor_->SubmitDraws(*renderer_, false);
                renderer_->EndFrame();
                settings.clearColor = previousClearColor;
            }
        }

        if (config_.enableImGui) {
            const double imguiStart = glfwGetTime();
            debugUi_->RenderDrawData();
            frameTimings_.imguiRenderMs = static_cast<float>((glfwGetTime() - imguiStart) * 1000.0);
        }
        const double swapStart = glfwGetTime();
        glfwSwapBuffers(window_);
        frameTimings_.swapMs = static_cast<float>((glfwGetTime() - swapStart) * 1000.0);
        return;
    }

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
        const glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, sceneCameraNearClip_, sceneCameraFarClip_);

        renderer_->SetViewport(viewport);
        renderer_->EnsureViewportRenderTarget(ViewportRenderTarget::Scene, viewport.width, viewport.height);
        const double passStart = glfwGetTime();
        renderer_->BeginFrameToViewportTarget(ViewportRenderTarget::Scene, previousClearColor);
        renderer_->SetCamera(view, proj);
        if (skyboxController_) {
            skyboxController_->Draw(view, proj);
        }
        if (sceneEditor_) {
            sceneEditor_->SubmitDraws(*renderer_, true);
        }
        renderer_->EndFrameToViewportTarget();
        frameTimings_.scenePassMs += static_cast<float>((glfwGetTime() - passStart) * 1000.0);
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
        const double passStart = glfwGetTime();
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
        frameTimings_.gamePassMs += static_cast<float>((glfwGetTime() - passStart) * 1000.0);
    };

    if (sceneEditor_) {
        const bool renderSceneViewport = !sceneEditor_->ShouldRouteInputToGame();
        if (renderSceneViewport) {
            renderScenePass(sceneEditor_->GetSceneRenderViewport(cfg.width, cfg.height));
        }
        const bool renderGameViewport = sceneEditor_->IsRunMode()
            || sceneEditor_->ShouldRouteInputToGame()
            || sceneEditor_->ShouldRenderGameViewportInEditMode();
        if (renderGameViewport) {
            renderGamePass(sceneEditor_->GetGameRenderViewport(cfg.width, cfg.height));
            sceneEditor_->MarkGameViewportRendered();
        }
    } else {
        renderScenePass(RendererViewport{0, 0, cfg.width, cfg.height});
    }

    rendererSettings.clearColor = previousClearColor;

    if (config_.enableImGui) {
        const double imguiStart = glfwGetTime();
        debugUi_->RenderDrawData();
        frameTimings_.imguiRenderMs = static_cast<float>((glfwGetTime() - imguiStart) * 1000.0);
    }

    const double swapStart = glfwGetTime();
    glfwSwapBuffers(window_);
    frameTimings_.swapMs = static_cast<float>((glfwGetTime() - swapStart) * 1000.0);
}

} // namespace raceman

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
#include <stb_image.h>

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

void ClearRectPixels(int x, int y, int width, int height, const glm::vec4& color) {
    if (width <= 0 || height <= 0) {
        return;
    }
    glScissor(x, y, width, height);
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

unsigned int LoadStartupLogoTexture(int& width, int& height) {
    const std::filesystem::path iconPath =
        FindEngineRootForApplication() / "editor-assets" / "icons" / "ProjectRaceman_icon_full.png";
    int channels = 0;
    unsigned char* data = stbi_load(iconPath.string().c_str(), &width, &height, &channels, 4);
    if (data == nullptr || width <= 0 || height <= 0) {
        if (data != nullptr) {
            stbi_image_free(data);
        }
        width = 0;
        height = 0;
        return 0;
    }

    unsigned int textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return textureId;
}

constexpr int kStartupSplashWidth = 680;
constexpr int kStartupSplashHeight = 320;

void CenterWindowOnPrimaryMonitor(GLFWwindow* window, int width, int height) {
    if (window == nullptr) {
        return;
    }
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr) {
        return;
    }
    int monitorX = 0;
    int monitorY = 0;
    glfwGetMonitorPos(monitor, &monitorX, &monitorY);
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (mode == nullptr) {
        return;
    }
    glfwSetWindowPos(
        window,
        monitorX + (std::max)(0, (mode->width - width) / 2),
        monitorY + (std::max)(0, (mode->height - height) / 2));
}

#if defined(_WIN32)
bool GetWindowMonitorWorkArea(GLFWwindow* window, RECT& workArea) {
    if (window == nullptr) {
        return false;
    }
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr) {
        return false;
    }

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor == nullptr || !GetMonitorInfoA(monitor, &monitorInfo)) {
        return false;
    }
    workArea = monitorInfo.rcWork;
    return true;
}

void FitNativeWindowOuterRectToWorkArea(GLFWwindow* window) {
    RECT work{};
    if (!GetWindowMonitorWorkArea(window, work)) {
        return;
    }
    if (HWND hwnd = glfwGetWin32Window(window)) {
        SetWindowPos(
            hwnd,
            nullptr,
            work.left,
            work.top,
            work.right - work.left,
            work.bottom - work.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

void FitCenteredClientSizeToWorkArea(GLFWwindow* window, int desiredClientWidth, int desiredClientHeight) {
    RECT work{};
    if (!GetWindowMonitorWorkArea(window, work)) {
        CenterWindowOnPrimaryMonitor(window, desiredClientWidth, desiredClientHeight);
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr) {
        return;
    }

    const DWORD style = static_cast<DWORD>(GetWindowLongA(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongA(hwnd, GWL_EXSTYLE));
    RECT desiredOuter{0, 0, desiredClientWidth, desiredClientHeight};
    AdjustWindowRectEx(&desiredOuter, style, FALSE, exStyle);
    const int decorationWidth = (desiredOuter.right - desiredOuter.left) - desiredClientWidth;
    const int decorationHeight = (desiredOuter.bottom - desiredOuter.top) - desiredClientHeight;
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;
    const int clientWidth = (std::max)(1, (std::min)(desiredClientWidth, workWidth - (std::max)(0, decorationWidth)));
    const int clientHeight = (std::max)(1, (std::min)(desiredClientHeight, workHeight - (std::max)(0, decorationHeight)));

    RECT fittedOuter{0, 0, clientWidth, clientHeight};
    AdjustWindowRectEx(&fittedOuter, style, FALSE, exStyle);
    const int outerWidth = fittedOuter.right - fittedOuter.left;
    const int outerHeight = fittedOuter.bottom - fittedOuter.top;
    const int outerX = work.left + (std::max)(0, workWidth - outerWidth) / 2;
    const int outerY = work.top + (std::max)(0, workHeight - outerHeight) / 2;

    SetWindowPos(
        hwnd,
        nullptr,
        outerX,
        outerY,
        outerWidth,
        outerHeight,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void ApplyLightNativeTitleBar(HWND hwnd) {
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwmapi = LoadLibraryA("dwmapi.dll");
    if (dwmapi == nullptr) {
        return;
    }

    auto setAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
    if (setAttribute != nullptr) {
        constexpr DWORD kDwmUseImmersiveDarkModeBefore20H1 = 19;
        constexpr DWORD kDwmUseImmersiveDarkMode = 20;
        constexpr DWORD kDwmCaptionColor = 35;
        constexpr DWORD kDwmTextColor = 36;
        const BOOL lightFrame = FALSE;
        const COLORREF whiteCaption = RGB(245, 245, 245);
        const COLORREF darkText = RGB(20, 20, 20);
        setAttribute(hwnd, kDwmUseImmersiveDarkModeBefore20H1, &lightFrame, sizeof(lightFrame));
        setAttribute(hwnd, kDwmUseImmersiveDarkMode, &lightFrame, sizeof(lightFrame));
        setAttribute(hwnd, kDwmCaptionColor, &whiteCaption, sizeof(whiteCaption));
        setAttribute(hwnd, kDwmTextColor, &darkText, sizeof(darkText));
    }
    FreeLibrary(dwmapi);
}
#endif

void HideNativeWindowFrame(GLFWwindow* window) {
    if (window == nullptr) {
        return;
    }
#if (GLFW_VERSION_MAJOR > 3) || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3)
    glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
#endif
#if defined(_WIN32)
    if (HWND hwnd = glfwGetWin32Window(window)) {
        LONG style = GetWindowLongA(hwnd, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME);
        style |= WS_POPUP;
        SetWindowLongA(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif
}

void RestoreNativeWindowFrame(GLFWwindow* window) {
    if (window == nullptr) {
        return;
    }
#if (GLFW_VERSION_MAJOR > 3) || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3)
    glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
#endif
#if defined(_WIN32)
    if (HWND hwnd = glfwGetWin32Window(window)) {
        LONG style = GetWindowLongA(hwnd, GWL_STYLE);
        style &= ~WS_POPUP;
        style |= WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_SYSMENU |
                 WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
        SetWindowLongA(hwnd, GWL_STYLE, style);

        LONG exStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
        exStyle &= ~WS_EX_TOOLWINDOW;
        exStyle |= WS_EX_APPWINDOW;
        SetWindowLongA(hwnd, GWL_EXSTYLE, exStyle);

        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
        ApplyLightNativeTitleBar(hwnd);
    }
#endif
}
} // namespace

Application::Application(const ApplicationConfig& config) : config_(config) {
    PlayerStartupLog(config_, "Initializing GLFW...");
    InitializeGlfw();
    PlayerStartupLog(config_, "Initializing OpenGL...");
    InitializeGlad();
    ShowStartupSplash("Initializing OpenGL...", 0.10f);

    PlayerStartupLog(config_, "Creating renderer...");
    ShowStartupSplash("Creating renderer...", 0.18f);
    renderer_ = std::make_shared<Renderer>(RendererConfig{config.width, config.height});
    PlayerStartupLog(config_, "Creating input...");
    ShowStartupSplash("Initializing input...", 0.28f);
    if (!inputManager_) {
        inputManager_ = std::make_unique<InputManager>();
        inputManager_->AttachToWindow(window_);
    }
    PlayerStartupLog(config_, "Initializing audio...");
    ShowStartupSplash("Initializing audio...", 0.38f);
    audioManager_ = std::make_unique<AudioManager>();
    audioManager_->Initialize();
    PlayerStartupLog(config_, "Creating editor/player services...");
    ShowStartupSplash("Creating editor services...", 0.48f);

    debugUi_ = std::make_unique<DebugUI>(config.enableImGui);
    menuController_ = std::make_unique<MenuController>();
    console_ = std::make_unique<Console>();
    skyboxController_ = std::make_unique<SkyboxController>();
    PlayerStartupLog(config_, "Loading skybox...");
    ShowStartupSplash("Loading skybox...", 0.58f);
    skyboxController_->Reload();
    PlayerStartupLog(config_, "Skybox loaded.");

    if (config.enableImGui) {
        InitializeImGui();
        ShowStartupSplash("Preparing editor UI...", 0.66f);
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
        sceneEditor_->SetRenderer(renderer_.get());
        sceneEditor_->SetConsole(console_.get());
        sceneEditor_->SetInputManager(inputManager_.get());
        sceneEditor_->SetAudioManager(audioManager_.get());
        consoleLog("Loading project metadata...");
        ShowStartupSplash("Loading project metadata...", 0.76f);
        sceneEditor_->SetProjectRoot(config.projectRoot);
        renderer_->GetSettings().profile = sceneEditor_->GetGraphicsProfile();
        const std::string loadingTitle = config_.windowTitle + " - Loading...";
        glfwSetWindowTitle(window_, loadingTitle.c_str());
        consoleLog("Startup complete; runtime will begin after first frame.");
        ShowStartupSplash("Runtime startup ready...", 0.92f);
    } else if (config.enableImGui) {
        // Editor mode: open the most recent project, or show the launcher.
        ShowStartupSplash("Loading recent projects...", 0.72f);
        const auto recent = ProjectLauncher::LoadRegistry();
        if (!recent.empty() && std::filesystem::exists(recent.front().path)) {
            InitializeEditor(recent.front().path);
        } else {
            ShowStartupSplash("Opening project launcher...", 0.90f);
            launcher_ = std::make_unique<ProjectLauncher>();
        }
    }

    lastFrameTime_ = glfwGetTime();
    baseTitle_ = config_.windowTitle;
    FinishStartupSplash();
}

void Application::InitializeEditor(const std::string& projectPath) {
    if (sceneEditor_) {
        sceneEditor_->StopRuntime();
        sceneEditor_.reset();
    }
    launcher_.reset();
    FitEditorWindowToScreen();

#if defined(_WIN32)
    _putenv_s("RACEMAN_PROJECT_ROOT", projectPath.c_str());
#else
    setenv("RACEMAN_PROJECT_ROOT", projectPath.c_str(), 1);
#endif

    sceneEditor_ = std::make_unique<SceneEditor>();
    renderer_->GetSettings().profile = sceneEditor_->GetGraphicsProfile();
    sceneEditor_->SetRenderer(renderer_.get());
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

void Application::FitEditorWindowToScreen() {
    if (window_ == nullptr || config_.playerMode || startupSplashActive_) {
        return;
    }

#if defined(_WIN32)
    HWND hwnd = glfwGetWin32Window(window_);
    if (hwnd != nullptr) {
        RestoreNativeWindowFrame(window_);
        ShowWindow(hwnd, SW_RESTORE);
        ShowWindow(hwnd, SW_MAXIMIZE);
    }
#else
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr) {
        return;
    }
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (mode == nullptr) {
        return;
    }
    glfwSetWindowPos(window_, 0, 0);
    glfwSetWindowSize(window_, mode->width, mode->height);
#endif
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
    if (startupLogoTexture_ != 0) {
        glDeleteTextures(1, &startupLogoTexture_);
        startupLogoTexture_ = 0;
    }
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

void Application::ShowStartupSplash(const std::string& message, float progress, bool showWindow) {
    if (window_ == nullptr) {
        return;
    }

    startupSplashActive_ = true;
    startupSplashMessage_ = message;
    startupSplashProgress_ = (std::clamp)(progress, 0.0f, 1.0f);

    const std::string title = config_.windowTitle + " - " + message;
    glfwSetWindowTitle(window_, title.c_str());
    if (console_) {
        console_->AddLog("[Startup] " + message);
    }
    if (config_.playerMode) {
        std::cout << "[Player] " << message << std::endl;
    }

    if (!startupWindowShown_) {
        HideNativeWindowFrame(window_);
        glfwSetWindowSize(window_, kStartupSplashWidth, kStartupSplashHeight);
        CenterWindowOnPrimaryMonitor(window_, kStartupSplashWidth, kStartupSplashHeight);
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window_, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        return;
    }

    glfwMakeContextCurrent(window_);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.015f, 0.017f, 0.021f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (startupLogoTexture_ == 0) {
        startupLogoTexture_ = LoadStartupLogoTexture(startupLogoWidth_, startupLogoHeight_);
    }

    if (config_.enableImGui && debugUi_ && ImGui::GetCurrentContext() != nullptr) {
        debugUi_->BeginFrame();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.015f, 0.017f, 0.021f, 1.0f));
        if (ImGui::Begin("##StartupSplash", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus)) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 windowMin = viewport->Pos;
            const ImVec2 windowMax(windowMin.x + viewport->Size.x, windowMin.y + viewport->Size.y);
            drawList->AddRectFilled(windowMin, windowMax, IM_COL32(8, 10, 14, 255));

            if (startupLogoTexture_ != 0) {
                const float windowWidth = (std::max)(1.0f, viewport->Size.x);
                const float windowHeight = (std::max)(1.0f, viewport->Size.y);
                const float imageAspect = startupLogoHeight_ > 0
                    ? static_cast<float>(startupLogoWidth_) / static_cast<float>(startupLogoHeight_)
                    : 1.0f;
                const float windowAspect = windowWidth / windowHeight;
                ImVec2 imageMin = windowMin;
                ImVec2 imageMax = windowMax;
                if (imageAspect > windowAspect) {
                    const float drawWidth = windowHeight * imageAspect;
                    imageMin.x = windowMin.x - (drawWidth - windowWidth) * 0.5f;
                    imageMax.x = imageMin.x + drawWidth;
                } else {
                    const float drawHeight = windowWidth / imageAspect;
                    imageMin.y = windowMin.y - (drawHeight - windowHeight) * 0.5f;
                    imageMax.y = imageMin.y + drawHeight;
                }
                drawList->AddImage(static_cast<ImTextureID>(startupLogoTexture_), imageMin, imageMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 210));
            } else {
                drawList->AddRectFilled(ImVec2(windowMin.x + 70.0f, windowMin.y + 28.0f), ImVec2(windowMax.x - 70.0f, windowMax.y - 62.0f), IM_COL32(235, 239, 246, 48), 18.0f);
            }
            drawList->AddRectFilled(windowMin, windowMax, IM_COL32(4, 6, 10, 92));
            drawList->AddText(ImVec2(windowMin.x + 34.0f, windowMin.y + 34.0f), IM_COL32(232, 237, 246, 255), config_.playerMode ? "Player Runtime" : "Editor Startup");
            drawList->AddText(ImVec2(windowMin.x + 34.0f, windowMin.y + 62.0f), IM_COL32(170, 180, 196, 255), "Loading engine modules");

            drawList->AddText(ImVec2(windowMin.x + 34.0f, windowMax.y - 76.0f), IM_COL32(148, 156, 170, 255), startupSplashMessage_.c_str());
            const ImVec2 barMin(windowMin.x + 34.0f, windowMax.y - 42.0f);
            const ImVec2 barMax(windowMax.x - 34.0f, windowMax.y - 34.0f);
            drawList->AddRectFilled(barMin, barMax, IM_COL32(32, 38, 48, 255), 4.0f);
            drawList->AddRectFilled(barMin, ImVec2(barMin.x + (barMax.x - barMin.x) * startupSplashProgress_, barMax.y), IM_COL32(42, 157, 240, 255), 4.0f);
            drawList->AddText(ImVec2(windowMax.x - 82.0f, windowMin.y + 34.0f), IM_COL32(202, 210, 224, 255), "2026.1");
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
        debugUi_->EndFrame();
        debugUi_->RenderDrawData();
    } else {
        glEnable(GL_SCISSOR_TEST);
        ClearRectPixels(0, 0, framebufferWidth, framebufferHeight, glm::vec4(0.03f, 0.04f, 0.055f, 1.0f));
        const int icon = 54;
        ClearRectPixels(34, framebufferHeight - 114, icon, icon, glm::vec4(0.92f, 0.94f, 0.97f, 1.0f));
        ClearRectPixels(42, framebufferHeight - 106, icon - 16, icon - 16, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        const int barX = 34;
        const int barY = 34;
        const int barW = framebufferWidth - 68;
        ClearRectPixels(barX, barY, barW, 8, glm::vec4(0.12f, 0.14f, 0.18f, 1.0f));
        ClearRectPixels(barX, barY, static_cast<int>(barW * startupSplashProgress_), 8, glm::vec4(0.16f, 0.62f, 0.94f, 1.0f));
        glDisable(GL_SCISSOR_TEST);
    }

    glfwSwapBuffers(window_);
    glfwPollEvents();
    if (showWindow && !startupWindowShown_) {
        glfwShowWindow(window_);
        startupWindowShown_ = true;
    }
}

void Application::FinishStartupSplash() {
    if (window_ == nullptr) {
        return;
    }
    ShowStartupSplash("Ready.", 1.0f, true);
    glfwHideWindow(window_);
    startupWindowShown_ = false;
    startupSplashActive_ = false;

    RestoreNativeWindowFrame(window_);
    glfwSetWindowSize(window_, config_.width, config_.height);
    if (!config_.playerMode && sceneEditor_) {
#if defined(_WIN32)
        FitNativeWindowOuterRectToWorkArea(window_);
#else
        glfwMaximizeWindow(window_);
#endif
    } else {
#if defined(_WIN32)
        FitCenteredClientSizeToWorkArea(window_, config_.width, config_.height);
#else
        CenterWindowOnPrimaryMonitor(window_, config_.width, config_.height);
#endif
    }
    glfwPollEvents();
    if (renderer_) {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window_, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth > 0 && framebufferHeight > 0) {
            renderer_->Resize(framebufferWidth, framebufferHeight);
        }
    }
    glfwSetWindowTitle(window_, config_.windowTitle.c_str());

    Update(0.0f);
    Render();
    if (inputManager_) {
        inputManager_->EndFrame();
    }

    glfwShowWindow(window_);
    startupWindowShown_ = true;
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
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

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
            rmbCaptureMouseX_ = mouseX;
            rmbCaptureMouseY_ = mouseY;
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else if ((!allowEditorCamera || rmb == GLFW_RELEASE) && rmbHeld_) {
            rmbHeld_ = false;
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glfwSetCursorPos(window_, rmbCaptureMouseX_, rmbCaptureMouseY_);
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
        if (allowEditorCamera && rmbHeld_ && inputManager_) {
            const float wheelDelta = inputManager_->GetMouseWheelDelta();
            if (wheelDelta != 0.0f) {
                camBaseSpeed_ = (std::max)(0.05f, (std::min)(200.0f, camBaseSpeed_ * std::pow(1.2f, wheelDelta)));
            }
        }
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
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGuiIO& io = ImGui::GetIO();
            if (rmbHeld_) {
                io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
                io.ClearInputMouse();
            } else {
                io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            }
        }
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
                    if (sceneEditor_) sceneEditor_->RequestSaveCurrentScene();
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
                },
                [this]() {
                    if (!sceneEditor_) return;
                    sceneEditor_->SetGraphicsProfile(renderer_->GetSettings().profile);
                    sceneEditor_->SaveProject();
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
                renderer_->EnsureViewportRenderTarget(ViewportRenderTarget::Game, cfg.width, cfg.height);
                renderer_->SetCamera(view, proj);
                renderer_->BeginFrameToViewportTarget(ViewportRenderTarget::Game, settings.clearColor);
                if (skyboxController_) {
                    skyboxController_->Draw(view, proj);
                }
                sceneEditor_->SubmitDraws(*renderer_, false);
                renderer_->EndFrameToViewportTarget();
                renderer_->PresentViewportTarget(ViewportRenderTarget::Game, viewport);
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
        const bool runMode = sceneEditor_->IsRunMode();
        const bool renderSceneViewport = runMode
            ? sceneEditor_->IsSceneViewportActiveForEditorControls()
            : !sceneEditor_->ShouldRouteInputToGame();
        if (renderSceneViewport) {
            renderScenePass(sceneEditor_->GetSceneRenderViewport(cfg.width, cfg.height));
        }
        const bool renderGameViewport = runMode
            ? !renderSceneViewport
            : (sceneEditor_->ShouldRouteInputToGame() || sceneEditor_->ShouldRenderGameViewportInEditMode());
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

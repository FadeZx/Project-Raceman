#include "DebugUI.h"

#include "../scenes/Scene.h"
#include "../rendering/Renderer.h"

#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

namespace raceman {

DebugUI::DebugUI(bool enabled) : enabled_(enabled) {}

DebugUI::~DebugUI() = default;

void DebugUI::Initialize(GLFWwindow* window) {
    if (!enabled_) {
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Persist window positions/sizes to config/imgui.ini
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "config/imgui.ini"; // ImGui will auto-load/save this

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 450");
}

void DebugUI::Shutdown() {
    if (!enabled_) {
        return;
    }

    // Ensure settings are flushed to disk
    ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename ? ImGui::GetIO().IniFilename : "imgui.ini");

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void DebugUI::BeginFrame() {
    if (!enabled_) {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::EndFrame() {
    if (!enabled_) {
        return;
    }

    ImGui::EndFrame();
}

void DebugUI::RenderDrawData() {
    if (!enabled_) {
        return;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void DebugUI::RenderSceneSwitcher(const std::vector<std::shared_ptr<Scene>>& scenes,
                                  std::size_t activeScene,
                                  const std::function<void(std::size_t)>& switchSceneCallback) {
    if (!enabled_) {
        return;
    }

    if (ImGui::Begin("Scenes")) {
        for (std::size_t i = 0; i < scenes.size(); ++i) {
            const bool isActive = i == activeScene;
            if (ImGui::Selectable(scenes[i]->GetName().c_str(), isActive)) {
                switchSceneCallback(i);
            }
        }
    }
    ImGui::End();
}

void DebugUI::RenderAppMetrics(float deltaTime, Renderer& renderer) {
    if (!enabled_) {
        return;
    }

    const float frameTimeMs = deltaTime * 1000.0f;
    const float fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;

    auto& settings = renderer.GetSettings();

    if (ImGui::Begin("Renderer")) {
        ImGui::Text("Frame time: %.2f ms", frameTimeMs);
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Separator();

        ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 5.0f, "%.2f");
        ImGui::SliderFloat("Gamma", &settings.gamma, 1.0f, 3.0f, "%.2f");
        ImGui::ColorEdit3("Clear Color", &settings.clearColor.x);
        ImGui::Checkbox("Enable Shadows", &settings.enableShadows);
        ImGui::Checkbox("Show Env Debug", &settings.showEnvironmentDebugView);
    }
    ImGui::End();
}

} // namespace raceman

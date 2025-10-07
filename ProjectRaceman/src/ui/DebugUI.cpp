#include "DebugUI.h"

#include "scenes/Scene.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

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

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 450");
}

void DebugUI::Shutdown() {
    if (!enabled_) {
        return;
    }

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

} // namespace raceman

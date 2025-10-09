#include "Console.h"
#include <imgui/imgui.h>

namespace raceman {

void Console::Add(MessageType type, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    entries_.push_back(ConsoleEntry{type, msg});
}

bool Console::RenderPanel(const char* title) {
    bool changed = false;
    if (ImGui::Begin(title)) {
        // Controls
        ImGui::Checkbox("Log", &showLog_); ImGui::SameLine();
        ImGui::Checkbox("Warning", &showWarning_); ImGui::SameLine();
        ImGui::Checkbox("Error", &showError_); ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            std::lock_guard<std::mutex> lock(mtx_);
            entries_.clear();
            changed = true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll_);

        // Input line
        ImGui::Separator();
        ImGui::PushItemWidth(-80);
        if (ImGui::InputText("##console-input", &input_)) { /* live edit */ }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Send")) {
            if (!input_.empty()) {
                AddLog(input_);
                input_.clear();
                changed = true;
            }
        }

        // Messages
        ImGui::Separator();
        ImGui::BeginChild("console-scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& e : entries_) {
            bool show = (e.type == MessageType::Log && showLog_) ||
                        (e.type == MessageType::Warning && showWarning_) ||
                        (e.type == MessageType::Error && showError_);
            if (!show) continue;

            ImVec4 col(0.8f, 0.8f, 0.8f, 1.0f);
            if (e.type == MessageType::Warning) col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            if (e.type == MessageType::Error)   col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(e.text.c_str());
            ImGui::PopStyleColor();
        }
        if (autoScroll_) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
    return changed;
}

} // namespace raceman
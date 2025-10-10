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
        static char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", input_.c_str());   // show current text
        if (ImGui::InputText("##console-input", buf, IM_ARRAYSIZE(buf))) {
            input_ = buf;  // copy
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Send")) {
            if (!input_.empty()) {
                AddLog(input_);
                input_.clear();
                changed = true;
            }
        }

        // Tabs
        ImGui::Separator();
        if (ImGui::BeginTabBar("ConsoleTabs")) {
            if (ImGui::BeginTabItem("Log")) {
                currentTab_ = Tab::Log;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Warning")) {
                currentTab_ = Tab::Warning;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Error")) {
                currentTab_ = Tab::Error;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // Messages
        ImGui::BeginChild("console-scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& e : entries_) {
            bool show = false;
            if (currentTab_ == Tab::Log) show = (e.type == MessageType::Log);
            else if (currentTab_ == Tab::Warning) show = (e.type == MessageType::Warning);
            else if (currentTab_ == Tab::Error) show = (e.type == MessageType::Error);
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
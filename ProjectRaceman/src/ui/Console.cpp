#include "Console.h"
#include <imgui/imgui.h>

#include <cstdio>
#include <sstream>

namespace raceman {

void Console::Add(MessageType type, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    entries_.push_back(ConsoleEntry{type, msg});
}

bool Console::RenderPanel(const char* title) {
    bool changed = false;
    if (ImGui::Begin(title)) {
        changed = RenderContents();
    }
    ImGui::End();
    return changed;
}

bool Console::RenderContents() {
    bool changed = false;
    int logCount = 0;
    int warningCount = 0;
    int errorCount = 0;
    std::vector<ConsoleEntry> entries;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        entries = entries_;
    }
    for (const ConsoleEntry& entry : entries) {
        if (entry.type == MessageType::Log) {
            ++logCount;
        } else if (entry.type == MessageType::Warning) {
            ++warningCount;
        } else if (entry.type == MessageType::Error) {
            ++errorCount;
        }
    }

    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(mtx_);
        entries_.clear();
        entries.clear();
        logCount = warningCount = errorCount = 0;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll_);
    ImGui::SameLine();
    if (ImGui::Button("Copy Visible")) {
        std::ostringstream text;
        for (const ConsoleEntry& entry : entries) {
            const bool show =
                (currentTab_ == Tab::Log && entry.type == MessageType::Log) ||
                (currentTab_ == Tab::Warning && entry.type == MessageType::Warning) ||
                (currentTab_ == Tab::Error && entry.type == MessageType::Error);
            if (show) {
                text << entry.text << '\n';
            }
        }
        ImGui::SetClipboardText(text.str().c_str());
    }

    ImGui::Separator();
    ImGui::PushItemWidth(-80);
    static char buf[512];
    std::snprintf(buf, sizeof(buf), "%s", input_.c_str());
    if (ImGui::InputText("##console-input", buf, IM_ARRAYSIZE(buf))) {
        input_ = buf;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Send")) {
        if (!input_.empty()) {
            const std::string command = input_;
            bool handled = false;
            if (commandHandler_) {
                handled = commandHandler_(command);
            }
            if (!handled) {
                AddLog(command);
            }
            input_.clear();
            changed = true;
        }
    }

    ImGui::Separator();
    if (ImGui::BeginTabBar("ConsoleTabs")) {
        char logLabel[32];
        char warningLabel[32];
        char errorLabel[32];
        std::snprintf(logLabel, sizeof(logLabel), "Log (%d)###Log", logCount);
        std::snprintf(warningLabel, sizeof(warningLabel), "Warning (%d)###Warning", warningCount);
        std::snprintf(errorLabel, sizeof(errorLabel), "Error (%d)###Error", errorCount);
        if (ImGui::BeginTabItem(logLabel)) {
            currentTab_ = Tab::Log;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(warningLabel)) {
            currentTab_ = Tab::Warning;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(errorLabel)) {
            currentTab_ = Tab::Error;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::BeginChild("console-scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    int visibleIndex = 0;
    for (const auto& e : entries) {
        bool show = false;
        if (currentTab_ == Tab::Log) show = (e.type == MessageType::Log);
        else if (currentTab_ == Tab::Warning) show = (e.type == MessageType::Warning);
        else if (currentTab_ == Tab::Error) show = (e.type == MessageType::Error);
        if (!show) continue;

        ImVec4 col(0.86f, 0.86f, 0.86f, 1.0f);
        if (e.type == MessageType::Warning) col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        if (e.type == MessageType::Error)   col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

        ImGui::PushID(visibleIndex++);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Selectable(e.text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            ImGui::SetClipboardText(e.text.c_str());
        }
        if (ImGui::BeginPopupContextItem("ConsoleLineMenu")) {
            if (ImGui::MenuItem("Copy Line")) {
                ImGui::SetClipboardText(e.text.c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (autoScroll_) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    return changed;
}

} // namespace raceman

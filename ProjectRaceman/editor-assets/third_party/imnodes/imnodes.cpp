#include "imnodes.h"

#include <unordered_map>

namespace imnodes {
namespace {
std::unordered_map<int, ImVec2> g_positions;
int g_depth = 0;
}

void Initialize() {}
void Shutdown() {}

void BeginNodeEditor() {
    ImGui::BeginChild("##imnodes_compat_editor", ImVec2(0.0f, 360.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    ++g_depth;
}

void EndNodeEditor() {
    if (g_depth > 0) {
        --g_depth;
    }
    ImGui::EndChild();
}

void BeginNode(int id) {
    ImGui::PushID(id);
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_Border));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::PushItemWidth(210.0f);
}

void EndNode() {
    ImGui::PopItemWidth();
    ImGui::Dummy(ImVec2(220.0f, 2.0f));
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    (void)min;
    (void)max;
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImGui::PopID();
}

void BeginNodeTitleBar() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
}

void EndNodeTitleBar() {
    ImGui::PopStyleColor();
    ImGui::Separator();
}

void BeginInputAttribute(int id, PinShape) {
    ImGui::PushID(id);
    ImGui::TextUnformatted("<");
    ImGui::SameLine();
}

void EndInputAttribute() {
    ImGui::PopID();
}

void BeginOutputAttribute(int id, PinShape) {
    ImGui::PushID(id);
}

void EndOutputAttribute() {
    ImGui::SameLine();
    ImGui::TextUnformatted(">");
    ImGui::PopID();
}

void Link(int, int, int) {}
bool IsLinkCreated(int*, int*) { return false; }
bool IsLinkDestroyed(int*) { return false; }

void SetNodeGridSpacePos(int nodeId, const ImVec2& pos) {
    g_positions[nodeId] = pos;
}

ImVec2 GetNodeGridSpacePos(int nodeId) {
    const auto it = g_positions.find(nodeId);
    return it == g_positions.end() ? ImVec2(0.0f, 0.0f) : it->second;
}

} // namespace imnodes

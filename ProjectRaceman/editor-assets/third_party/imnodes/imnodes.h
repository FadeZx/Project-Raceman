#pragma once

#include <imgui/imgui.h>

namespace imnodes {

enum PinShape {
    PinShape_Circle,
    PinShape_CircleFilled,
    PinShape_Quad,
    PinShape_QuadFilled,
    PinShape_Triangle,
    PinShape_TriangleFilled
};

void Initialize();
void Shutdown();
void BeginNodeEditor();
void EndNodeEditor();
void BeginNode(int id);
void EndNode();
void BeginNodeTitleBar();
void EndNodeTitleBar();
void BeginInputAttribute(int id, PinShape shape = PinShape_CircleFilled);
void EndInputAttribute();
void BeginOutputAttribute(int id, PinShape shape = PinShape_CircleFilled);
void EndOutputAttribute();
void Link(int id, int startAttributeId, int endAttributeId);
bool IsLinkCreated(int* startAttributeId, int* endAttributeId);
bool IsLinkDestroyed(int* linkId);
void SetNodeGridSpacePos(int nodeId, const ImVec2& pos);
ImVec2 GetNodeGridSpacePos(int nodeId);

} // namespace imnodes

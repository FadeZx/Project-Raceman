#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

void SceneEditor::SelectProjectFile(const std::string& path) {
    selectedProjectFile_ = NormalizeSlashes(path);
}

void SceneEditor::UpdateMoveGizmo(Renderer& renderer) {
    hoveredGizmoAxis_ = -1;
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        activeGizmoAxis_ = -1;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 mouse{io.MousePos.x, io.MousePos.y};
    const glm::vec3 origin = objects_[selectedIndex_].transform.position;
    constexpr float axisLength = 1.0f;
    constexpr float hitDistancePixels = 10.0f;

    float bestDistance = hitDistancePixels;
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec2 startScreen;
        glm::vec2 endScreen;
        const glm::vec3 end = origin + GizmoAxisVector(axis) * axisLength;
        if (!ProjectWorldToScreen(origin, renderer, startScreen) || !ProjectWorldToScreen(end, renderer, endScreen)) {
            continue;
        }

        const float distance = DistanceToScreenSegment(mouse, startScreen, endScreen);
        if (distance < bestDistance) {
            bestDistance = distance;
            hoveredGizmoAxis_ = axis;
        }
    }

    if (activeGizmoAxis_ < 0) {
        if (!io.WantTextInput && !io.WantCaptureMouse && !io.MouseDown[1] && io.MouseClicked[0] && hoveredGizmoAxis_ >= 0) {
            activeGizmoAxis_ = hoveredGizmoAxis_;
            gizmoDragStartMouse_ = mouse;
            gizmoDragStartPosition_ = origin;
            gizmoDirtyDuringDrag_ = false;
        }
        return;
    }

    if (!io.MouseDown[0]) {
        if (gizmoDirtyDuringDrag_ && onDirty_) {
            onDirty_();
        }
        activeGizmoAxis_ = -1;
        gizmoDirtyDuringDrag_ = false;
        return;
    }

    glm::vec2 startScreen;
    glm::vec2 endScreen;
    const glm::vec3 axisVector = GizmoAxisVector(activeGizmoAxis_);
    if (!ProjectWorldToScreen(gizmoDragStartPosition_, renderer, startScreen) ||
        !ProjectWorldToScreen(gizmoDragStartPosition_ + axisVector * axisLength, renderer, endScreen)) {
        return;
    }

    glm::vec2 axisScreen = endScreen - startScreen;
    const float axisScreenLength = glm::length(axisScreen);
    if (axisScreenLength < 0.001f) {
        return;
    }

    const glm::vec2 axisScreenDir = axisScreen / axisScreenLength;
    const float pixelsAlongAxis = glm::dot(mouse - gizmoDragStartMouse_, axisScreenDir);
    const float worldDelta = pixelsAlongAxis / axisScreenLength * axisLength;
    objects_[selectedIndex_].transform.position = gizmoDragStartPosition_ + axisVector * worldDelta;
    gizmoDirtyDuringDrag_ = true;
}

void SceneEditor::SubmitMoveGizmo(Renderer& renderer) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }

    const glm::vec3 origin = objects_[selectedIndex_].transform.position;
    constexpr float axisLength = 2.0f;
    constexpr float arrowHeadLength = 0.35f;
    constexpr float arrowHeadWidth = 0.14f;

    for (int axis = 0; axis < 3; ++axis) {
        const bool highlighted = (axis == hoveredGizmoAxis_ || axis == activeGizmoAxis_);
        const glm::vec3 axisVector = GizmoAxisVector(axis);
        const glm::vec4 color = GizmoAxisColor(axis, highlighted);
        const float width = highlighted ? 4.0f : 3.0f;
        const glm::vec3 end = origin + axisVector * axisLength;

        renderer.SubmitLine({origin, end, color, width});

        glm::vec3 sideA;
        glm::vec3 sideB;
        if (axis == 0) {
            sideA = {0.0f, arrowHeadWidth, 0.0f};
            sideB = {0.0f, 0.0f, arrowHeadWidth};
        } else if (axis == 1) {
            sideA = {arrowHeadWidth, 0.0f, 0.0f};
            sideB = {0.0f, 0.0f, arrowHeadWidth};
        } else {
            sideA = {arrowHeadWidth, 0.0f, 0.0f};
            sideB = {0.0f, arrowHeadWidth, 0.0f};
        }

        const glm::vec3 arrowBase = end - axisVector * arrowHeadLength;
        renderer.SubmitLine({end, arrowBase + sideA, color, width});
        renderer.SubmitLine({end, arrowBase - sideA, color, width});
        renderer.SubmitLine({end, arrowBase + sideB, color, width});
        renderer.SubmitLine({end, arrowBase - sideB, color, width});
    }
}

} // namespace raceman


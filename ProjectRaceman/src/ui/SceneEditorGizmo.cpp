#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

#include <limits>
#include <vector>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

glm::mat4 BuildObjectMatrix(const SceneObject& object) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, object.transform.position);
    const glm::vec3 rads = glm::radians(object.transform.rotationEuler);
    model = glm::rotate(model, rads.z, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, rads.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, rads.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, object.transform.scale);
    return model;
}

bool GetObjectLocalBounds(const SceneObject& object, glm::vec3& outMin, glm::vec3& outMax) {
    if (!object.hasMeshFilter) {
        return false;
    }

    if (object.type == "Plane") {
        outMin = {-0.5f, -0.05f, -0.5f};
        outMax = {0.5f, 0.05f, 0.5f};
        return true;
    }

    if (object.type == "Mesh") {
        outMin = object.meshFilter.localBoundsMin;
        outMax = object.meshFilter.localBoundsMax;
        constexpr float minThickness = 0.05f;
        for (int i = 0; i < 3; ++i) {
            if (std::abs(outMax[i] - outMin[i]) < minThickness) {
                outMin[i] -= minThickness * 0.5f;
                outMax[i] += minThickness * 0.5f;
            }
        }
        return true;
    }

    return false;
}

bool MakeMouseRay(const Renderer& renderer, const glm::vec2& mouse, glm::vec3& outOrigin, glm::vec3& outDirection) {
    const auto& cfg = renderer.GetConfig();
    if (cfg.width <= 0 || cfg.height <= 0) {
        return false;
    }

    const float x = (2.0f * mouse.x) / static_cast<float>(cfg.width) - 1.0f;
    const float y = 1.0f - (2.0f * mouse.y) / static_cast<float>(cfg.height);
    const glm::mat4 invViewProj = glm::inverse(renderer.GetProj() * renderer.GetView());
    glm::vec4 nearPoint = invViewProj * glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 farPoint = invViewProj * glm::vec4(x, y, 1.0f, 1.0f);
    if (std::abs(nearPoint.w) < 0.0001f || std::abs(farPoint.w) < 0.0001f) {
        return false;
    }

    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    const glm::vec3 delta = glm::vec3(farPoint - nearPoint);
    if (glm::length(delta) < 0.0001f) {
        return false;
    }
    outOrigin = glm::vec3(nearPoint);
    outDirection = glm::normalize(delta);
    return true;
}

bool IntersectRayAabb(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& boundsMin, const glm::vec3& boundsMax, float& outT) {
    float tMin = 0.0f;
    float tMax = (std::numeric_limits<float>::max)();

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 0.0001f) {
            if (origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis]) {
                return false;
            }
            continue;
        }

        float t1 = (boundsMin[axis] - origin[axis]) / direction[axis];
        float t2 = (boundsMax[axis] - origin[axis]) / direction[axis];
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tMin = (std::max)(tMin, t1);
        tMax = (std::min)(tMax, t2);
        if (tMin > tMax) {
            return false;
        }
    }

    outT = tMin;
    return true;
}

float DistanceToProjectedRing(const glm::vec2& mouse, const glm::vec3& origin, int axis, float radius, Renderer& renderer) {
    constexpr int segments = 48;
    float best = (std::numeric_limits<float>::max)();

    glm::vec2 previous;
    bool hasPrevious = false;
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
        glm::vec3 local(0.0f);
        if (axis == 0) {
            local = {0.0f, std::cos(t) * radius, std::sin(t) * radius};
        } else if (axis == 1) {
            local = {std::cos(t) * radius, 0.0f, std::sin(t) * radius};
        } else {
            local = {std::cos(t) * radius, std::sin(t) * radius, 0.0f};
        }

        glm::vec2 current;
        if (!ProjectWorldToScreen(origin + local, renderer, current)) {
            hasPrevious = false;
            continue;
        }
        if (hasPrevious) {
            best = (std::min)(best, DistanceToScreenSegment(mouse, previous, current));
        }
        previous = current;
        hasPrevious = true;
    }

    return best;
}

glm::vec3 TransformPoint(const glm::mat4& transform, const glm::vec3& point) {
    return glm::vec3(transform * glm::vec4(point, 1.0f));
}

void SubmitWireBox(Renderer& renderer, const glm::mat4& transform, const glm::vec3& center, const glm::vec3& size, const glm::vec4& color, float width) {
    const glm::vec3 halfSize = glm::abs(size) * 0.5f;
    const glm::vec3 corners[8] = {
        TransformPoint(transform, center + glm::vec3{-halfSize.x, -halfSize.y, -halfSize.z}),
        TransformPoint(transform, center + glm::vec3{ halfSize.x, -halfSize.y, -halfSize.z}),
        TransformPoint(transform, center + glm::vec3{ halfSize.x,  halfSize.y, -halfSize.z}),
        TransformPoint(transform, center + glm::vec3{-halfSize.x,  halfSize.y, -halfSize.z}),
        TransformPoint(transform, center + glm::vec3{-halfSize.x, -halfSize.y,  halfSize.z}),
        TransformPoint(transform, center + glm::vec3{ halfSize.x, -halfSize.y,  halfSize.z}),
        TransformPoint(transform, center + glm::vec3{ halfSize.x,  halfSize.y,  halfSize.z}),
        TransformPoint(transform, center + glm::vec3{-halfSize.x,  halfSize.y,  halfSize.z})
    };
    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    for (const auto& edge : edges) {
        renderer.SubmitLine({corners[edge[0]], corners[edge[1]], color, width});
    }
}

void SubmitWireCircle(Renderer& renderer, const glm::vec3& center, int axis, float radius, const glm::vec4& color, float width, int segments = 48) {
    glm::vec3 previous;
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
        glm::vec3 local{0.0f};
        if (axis == 0) {
            local = {0.0f, std::cos(t) * radius, std::sin(t) * radius};
        } else if (axis == 1) {
            local = {std::cos(t) * radius, 0.0f, std::sin(t) * radius};
        } else {
            local = {std::cos(t) * radius, std::sin(t) * radius, 0.0f};
        }
        const glm::vec3 current = center + local;
        if (i > 0) {
            renderer.SubmitLine({previous, current, color, width});
        }
        previous = current;
    }
}

void SubmitWireSphere(Renderer& renderer, const glm::vec3& center, float radius, const glm::vec4& color, float width) {
    SubmitWireCircle(renderer, center, 0, radius, color, width);
    SubmitWireCircle(renderer, center, 1, radius, color, width);
    SubmitWireCircle(renderer, center, 2, radius, color, width);
}

void SubmitWireCircleTransformed(Renderer& renderer, const glm::mat4& transform, const glm::vec3& center, int axis, float radius, const glm::vec4& color, float width, int segments = 48) {
    glm::vec3 previous;
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
        glm::vec3 local{0.0f};
        if (axis == 0) {
            local = {0.0f, std::cos(t) * radius, std::sin(t) * radius};
        } else if (axis == 1) {
            local = {std::cos(t) * radius, 0.0f, std::sin(t) * radius};
        } else {
            local = {std::cos(t) * radius, std::sin(t) * radius, 0.0f};
        }
        const glm::vec3 current = TransformPoint(transform, center + local);
        if (i > 0) {
            renderer.SubmitLine({previous, current, color, width});
        }
        previous = current;
    }
}

void SubmitWireCapsuleY(Renderer& renderer, const glm::mat4& transform, const glm::vec3& center, float radius, float height, const glm::vec4& color, float width) {
    const float cylinderHalfHeight = (std::max)(0.0f, height * 0.5f - radius);
    const glm::vec3 top = center + glm::vec3{0.0f, cylinderHalfHeight, 0.0f};
    const glm::vec3 bottom = center - glm::vec3{0.0f, cylinderHalfHeight, 0.0f};

    SubmitWireCircleTransformed(renderer, transform, top, 1, radius, color, width);
    SubmitWireCircleTransformed(renderer, transform, bottom, 1, radius, color, width);
    renderer.SubmitLine({TransformPoint(transform, top + glm::vec3{ radius, 0.0f, 0.0f}), TransformPoint(transform, bottom + glm::vec3{ radius, 0.0f, 0.0f}), color, width});
    renderer.SubmitLine({TransformPoint(transform, top + glm::vec3{-radius, 0.0f, 0.0f}), TransformPoint(transform, bottom + glm::vec3{-radius, 0.0f, 0.0f}), color, width});
    renderer.SubmitLine({TransformPoint(transform, top + glm::vec3{0.0f, 0.0f,  radius}), TransformPoint(transform, bottom + glm::vec3{0.0f, 0.0f,  radius}), color, width});
    renderer.SubmitLine({TransformPoint(transform, top + glm::vec3{0.0f, 0.0f, -radius}), TransformPoint(transform, bottom + glm::vec3{0.0f, 0.0f, -radius}), color, width});

    constexpr int segments = 24;
    for (int plane = 0; plane < 2; ++plane) {
        glm::vec3 previousTop;
        glm::vec3 previousBottom;
        for (int i = 0; i <= segments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(segments) * 3.14159265359f;
            const float side = std::cos(t) * radius;
            const float y = std::sin(t) * radius;
            const glm::vec3 topPoint = TransformPoint(transform, top + (plane == 0 ? glm::vec3{side, y, 0.0f} : glm::vec3{0.0f, y, side}));
            const glm::vec3 bottomPoint = TransformPoint(transform, bottom + (plane == 0 ? glm::vec3{side, -y, 0.0f} : glm::vec3{0.0f, -y, side}));
            if (i > 0) {
                renderer.SubmitLine({previousTop, topPoint, color, width});
                renderer.SubmitLine({previousBottom, bottomPoint, color, width});
            }
            previousTop = topPoint;
            previousBottom = bottomPoint;
        }
    }
}

} // namespace

void SceneEditor::SelectProjectFile(const std::string& path) {
    selectedProjectFile_ = NormalizeSlashes(path);
}

void SceneEditor::TrySelectObjectAtMouse(Renderer& renderer) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || io.WantCaptureMouse || io.MouseDown[1] || !io.MouseClicked[0]) {
        return;
    }

    glm::vec3 rayOrigin;
    glm::vec3 rayDirection;
    const glm::vec2 mouse{io.MousePos.x, io.MousePos.y};
    if (!MakeMouseRay(renderer, mouse, rayOrigin, rayDirection)) {
        return;
    }

    int bestIndex = -1;
    float bestT = (std::numeric_limits<float>::max)();
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        glm::vec3 boundsMin;
        glm::vec3 boundsMax;
        if (!GetObjectLocalBounds(objects_[i], boundsMin, boundsMax)) {
            continue;
        }

        const glm::mat4 model = BuildObjectMatrix(objects_[i]);
        const float det = glm::determinant(model);
        if (std::abs(det) < 0.000001f) {
            continue;
        }

        const glm::mat4 invModel = glm::inverse(model);
        const glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
        const glm::vec3 localDirectionRaw = glm::vec3(invModel * glm::vec4(rayDirection, 0.0f));
        if (glm::length(localDirectionRaw) < 0.0001f) {
            continue;
        }
        float t = 0.0f;
        if (IntersectRayAabb(localOrigin, localDirectionRaw, boundsMin, boundsMax, t) && t < bestT) {
            bestT = t;
            bestIndex = i;
        }
    }

    if (bestIndex >= 0) {
        Select(bestIndex);
    } else {
        selectedIndex_ = -1;
        inspectMaterial_ = false;
        activeGizmoAxis_ = -1;
    }
}

void SceneEditor::UpdateGizmo(Renderer& renderer) {
    hoveredGizmoAxis_ = -1;
    if (scriptsRunning_) {
        activeGizmoAxis_ = -1;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        activeGizmoAxis_ = -1;
        TrySelectObjectAtMouse(renderer);
        return;
    }

    const glm::vec2 mouse{io.MousePos.x, io.MousePos.y};
    const glm::vec3 origin = objects_[selectedIndex_].transform.position;
    constexpr float axisLength = 1.0f;
    constexpr float hitDistancePixels = 10.0f;

    float bestDistance = hitDistancePixels;
    for (int axis = 0; axis < 3; ++axis) {
        float distance = bestDistance;
        if (gizmoMode_ == GizmoMode::Rotate) {
            distance = DistanceToProjectedRing(mouse, origin, axis, axisLength, renderer);
        } else {
            glm::vec2 startScreen;
            glm::vec2 endScreen;
            const glm::vec3 end = origin + GizmoAxisVector(axis) * axisLength;
            if (!ProjectWorldToScreen(origin, renderer, startScreen) || !ProjectWorldToScreen(end, renderer, endScreen)) {
                continue;
            }
            distance = DistanceToScreenSegment(mouse, startScreen, endScreen);
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            hoveredGizmoAxis_ = axis;
        }
    }

    if (activeGizmoAxis_ < 0) {
        if (!io.WantTextInput && !io.WantCaptureMouse && !io.MouseDown[1] && io.MouseClicked[0] && hoveredGizmoAxis_ >= 0) {
            PushUndoState();
            activeGizmoAxis_ = hoveredGizmoAxis_;
            gizmoDragStartMouse_ = mouse;
            gizmoDragStartPosition_ = origin;
            gizmoDragStartRotation_ = objects_[selectedIndex_].transform.rotationEuler;
            gizmoDragStartScale_ = objects_[selectedIndex_].transform.scale;
            gizmoDirtyDuringDrag_ = false;
        } else {
            TrySelectObjectAtMouse(renderer);
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
    if (gizmoMode_ == GizmoMode::Rotate) {
        objects_[selectedIndex_].transform.rotationEuler = gizmoDragStartRotation_ + axisVector * (pixelsAlongAxis * 0.5f);
    } else {
        const float worldDelta = pixelsAlongAxis / axisScreenLength * axisLength;
        if (gizmoMode_ == GizmoMode::Scale) {
            const glm::vec3 scaled = gizmoDragStartScale_ + axisVector * worldDelta;
            objects_[selectedIndex_].transform.scale = {
                (std::max)(scaled.x, 0.01f),
                (std::max)(scaled.y, 0.01f),
                (std::max)(scaled.z, 0.01f)
            };
        } else {
            objects_[selectedIndex_].transform.position = gizmoDragStartPosition_ + axisVector * worldDelta;
        }
    }
    gizmoDirtyDuringDrag_ = true;
}

void SceneEditor::SubmitGizmo(Renderer& renderer) {
    if (scriptsRunning_) {
        return;
    }

    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }

    const glm::vec3 origin = objects_[selectedIndex_].transform.position;
    constexpr float axisLength = 1.0f;
    constexpr float arrowHeadLength = 0.35f;
    constexpr float arrowHeadWidth = 0.14f;

    for (int axis = 0; axis < 3; ++axis) {
        const bool highlighted = (axis == hoveredGizmoAxis_ || axis == activeGizmoAxis_);
        const glm::vec3 axisVector = GizmoAxisVector(axis);
        const glm::vec4 color = GizmoAxisColor(axis, highlighted);
        const float width = highlighted ? 4.0f : 3.0f;
        const glm::vec3 end = origin + axisVector * axisLength;

        if (gizmoMode_ == GizmoMode::Rotate) {
            constexpr int segments = 64;
            glm::vec3 previous;
            for (int i = 0; i <= segments; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
                glm::vec3 local(0.0f);
                if (axis == 0) {
                    local = {0.0f, std::cos(t) * axisLength, std::sin(t) * axisLength};
                } else if (axis == 1) {
                    local = {std::cos(t) * axisLength, 0.0f, std::sin(t) * axisLength};
                } else {
                    local = {std::cos(t) * axisLength, std::sin(t) * axisLength, 0.0f};
                }
                const glm::vec3 current = origin + local;
                if (i > 0) {
                    renderer.SubmitLine({previous, current, color, width});
                }
                previous = current;
            }
            continue;
        }

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
        renderer.SubmitLine({origin, end, color, width});
        if (gizmoMode_ == GizmoMode::Scale) {
            renderer.SubmitLine({end - sideA, end + sideA, color, width});
            renderer.SubmitLine({end - sideB, end + sideB, color, width});
        } else {
            renderer.SubmitLine({end, arrowBase + sideA, color, width});
            renderer.SubmitLine({end, arrowBase - sideA, color, width});
            renderer.SubmitLine({end, arrowBase + sideB, color, width});
            renderer.SubmitLine({end, arrowBase - sideB, color, width});
        }
    }

    const SceneObject& object = objects_[selectedIndex_];
    const glm::vec4 colliderColor{0.1f, 0.9f, 0.35f, 1.0f};
    constexpr float colliderWidth = 2.0f;
    const glm::mat4 objectMatrix = BuildObjectMatrix(object);
    if (object.hasBoxCollider && object.boxCollider.enabled) {
        SubmitWireBox(
            renderer,
            objectMatrix,
            object.boxCollider.center,
            object.boxCollider.size,
            colliderColor,
            colliderWidth);
    }
    if (object.hasSphereCollider && object.sphereCollider.enabled) {
        SubmitWireSphere(
            renderer,
            TransformPoint(objectMatrix, object.sphereCollider.center),
            object.sphereCollider.radius * (std::max)((std::max)(std::abs(object.transform.scale.x), std::abs(object.transform.scale.y)), std::abs(object.transform.scale.z)),
            colliderColor,
            colliderWidth);
    }
    if (object.hasCapsuleCollider && object.capsuleCollider.enabled) {
        const float radius = object.capsuleCollider.radius;
        const float height = (std::max)(object.capsuleCollider.height, radius * 2.0f);
        SubmitWireCapsuleY(renderer, objectMatrix, object.capsuleCollider.center, radius, height, colliderColor, colliderWidth);
    }
}

} // namespace raceman


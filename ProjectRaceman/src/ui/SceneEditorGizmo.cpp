#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"
#include "../../editor-assets/third_party/ImGuizmo/ImGuizmo.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>

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

glm::mat4 BuildRotationMatrix(const SceneObject& object) {
    glm::mat4 rotation(1.0f);
    const glm::vec3 rads = glm::radians(object.transform.rotationEuler);
    rotation = glm::rotate(rotation, rads.z, glm::vec3(0.0f, 0.0f, 1.0f));
    rotation = glm::rotate(rotation, rads.y, glm::vec3(0.0f, 1.0f, 0.0f));
    rotation = glm::rotate(rotation, rads.x, glm::vec3(1.0f, 0.0f, 0.0f));
    return rotation;
}

glm::mat4 BuildObjectMatrixNoScale(const SceneObject& object) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, object.transform.position);
    model *= BuildRotationMatrix(object);
    return model;
}

ImGuizmo::OPERATION ImGuizmoOperationFromMode(GizmoMode mode) {
    switch (mode) {
    case GizmoMode::Rotate:
        return ImGuizmo::ROTATE;
    case GizmoMode::Scale:
        return ImGuizmo::SCALE;
    case GizmoMode::Move:
    default:
        return ImGuizmo::TRANSLATE;
    }
}

ImGuizmo::MODE ImGuizmoTransformModeFromMode(GizmoMode mode) {
    return mode == GizmoMode::Move ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
}

struct CachedCollisionMesh {
    ImportedCollisionMesh mesh;
    bool valid{false};
};

CachedCollisionMesh& GetCollisionMeshCache(const std::string& key) {
    static std::unordered_map<std::string, CachedCollisionMesh> cache;
    return cache[key];
}

bool TryGetCollisionMeshForObject(const SceneObject& object, ImportedCollisionMesh& outMesh) {
    if (!object.hasMeshFilter || object.meshFilter.sourcePath.empty()) {
        return false;
    }

    const std::string key = NormalizeSlashes(object.meshFilter.sourcePath) + "#" + std::to_string(object.meshFilter.meshIndex);
    CachedCollisionMesh& cached = GetCollisionMeshCache(key);
    if (cached.valid) {
        outMesh = cached.mesh;
        return true;
    }

    std::shared_ptr<::Model> model = object.meshFilter.modelRef;
    if (!model) {
        std::string resolvedPath;
        std::vector<ImportedMeshInfo> infos;
        if (!TryLoadMeshAsset(object.meshFilter.sourcePath, resolvedPath, model, infos)) {
            return false;
        }
    }

    ImportedCollisionMesh mesh;
    if (!GetCollisionMesh(model, static_cast<std::size_t>(object.meshFilter.meshIndex), mesh)) {
        return false;
    }

    cached.mesh = std::move(mesh);
    cached.valid = true;
    outMesh = cached.mesh;
    return true;
}

int HierarchyDepth(const std::vector<SceneObject>& objects, int index) {
    if (index < 0 || index >= static_cast<int>(objects.size())) {
        return 0;
    }

    int depth = 0;
    int currentIndex = index;
    std::vector<std::string> visited;
    while (currentIndex >= 0 && currentIndex < static_cast<int>(objects.size())) {
        const SceneObject& object = objects[currentIndex];
        if (object.parentId.empty()) {
            break;
        }
        if (std::find(visited.begin(), visited.end(), object.id) != visited.end()) {
            break;
        }
        visited.push_back(object.id);

        currentIndex = -1;
        for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
            if (objects[i].id == object.parentId) {
                currentIndex = i;
                ++depth;
                break;
            }
        }
    }
    return depth;
}

glm::vec3 TransformDirection(const glm::mat4& transform, const glm::vec3& direction) {
    return glm::vec3(transform * glm::vec4(direction, 0.0f));
}

glm::vec3 NearestEulerEquivalent(glm::vec3 euler, const glm::vec3& reference) {
    for (int axis = 0; axis < 3; ++axis) {
        float* value = axis == 0 ? &euler.x : (axis == 1 ? &euler.y : &euler.z);
        const float ref = axis == 0 ? reference.x : (axis == 1 ? reference.y : reference.z);
        while (*value - ref > 180.0f) {
            *value -= 360.0f;
        }
        while (*value - ref < -180.0f) {
            *value += 360.0f;
        }
    }
    return euler;
}

bool GetObjectLocalBounds(const SceneObject& object, glm::vec3& outMin, glm::vec3& outMax) {
    if (!object.hasMeshFilter) {
        if (object.hasCamera || object.hasLight) {
            outMin = {-0.25f, -0.25f, -0.25f};
            outMax = {0.25f, 0.25f, 0.25f};
            return true;
        }
        return false;
    }

    const std::string meshType = object.meshFilter.meshType.empty() ? std::string("Mesh") : object.meshFilter.meshType;
    if (meshType == "Plane") {
        outMin = {-0.5f, -0.05f, -0.5f};
        outMax = {0.5f, 0.05f, 0.5f};
        return true;
    }

    if (IsBuiltInPrimitiveMeshType(meshType)) {
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

    if (meshType == "Mesh") {
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

bool ComputeWorldAabb(const glm::mat4& model, const glm::vec3& localMin, const glm::vec3& localMax, glm::vec3& outMin, glm::vec3& outMax) {
    const glm::vec3 corners[8] = {
        {localMin.x, localMin.y, localMin.z},
        {localMax.x, localMin.y, localMin.z},
        {localMax.x, localMax.y, localMin.z},
        {localMin.x, localMax.y, localMin.z},
        {localMin.x, localMin.y, localMax.z},
        {localMax.x, localMin.y, localMax.z},
        {localMax.x, localMax.y, localMax.z},
        {localMin.x, localMax.y, localMax.z}
    };

    outMin = TransformPoint(model, corners[0]);
    outMax = outMin;
    for (int i = 1; i < 8; ++i) {
        const glm::vec3 worldPoint = TransformPoint(model, corners[i]);
        outMin = {
            (std::min)(outMin.x, worldPoint.x),
            (std::min)(outMin.y, worldPoint.y),
            (std::min)(outMin.z, worldPoint.z)
        };
        outMax = {
            (std::max)(outMax.x, worldPoint.x),
            (std::max)(outMax.y, worldPoint.y),
            (std::max)(outMax.z, worldPoint.z)
        };
    }
    return true;
}

bool MakeMouseRay(const Renderer& renderer, const glm::vec2& mouse, glm::vec3& outOrigin, glm::vec3& outDirection) {
    const auto& viewport = renderer.GetViewport();
    if (viewport.width <= 0 || viewport.height <= 0) {
        return false;
    }

    const float localMouseX = mouse.x - static_cast<float>(viewport.x);
    const float localMouseY = mouse.y - static_cast<float>(viewport.y);
    const float x = (2.0f * localMouseX) / static_cast<float>(viewport.width) - 1.0f;
    const float y = 1.0f - (2.0f * localMouseY) / static_cast<float>(viewport.height);
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

    if (tMax < 0.0f) {
        return false;
    }

    outT = (tMin >= 0.0f) ? tMin : tMax;
    return true;
}

float DistanceToProjectedRing(const glm::vec2& mouse, const glm::vec3& origin, const glm::mat4& rotation, int axis, float radius, Renderer& renderer) {
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
        if (!ProjectWorldToScreen(origin + TransformDirection(rotation, local), renderer, current)) {
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

void SubmitWireBox(Renderer& renderer, const glm::mat4& transform, const glm::vec3& center, const glm::vec3& size, const glm::vec4& color, float width, DebugLineDepthMode depthMode = DebugLineDepthMode::AlwaysOnTop) {
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
        renderer.SubmitLine({corners[edge[0]], corners[edge[1]], color, width, depthMode});
    }
}

void SubmitWireCircle(Renderer& renderer, const glm::vec3& center, int axis, float radius, const glm::vec4& color, float width, int segments = 48, DebugLineDepthMode depthMode = DebugLineDepthMode::AlwaysOnTop) {
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
            renderer.SubmitLine({previous, current, color, width, depthMode});
        }
        previous = current;
    }
}

void SubmitWireSphere(Renderer& renderer, const glm::vec3& center, float radius, const glm::vec4& color, float width, DebugLineDepthMode depthMode = DebugLineDepthMode::AlwaysOnTop) {
    SubmitWireCircle(renderer, center, 0, radius, color, width, 48, depthMode);
    SubmitWireCircle(renderer, center, 1, radius, color, width, 48, depthMode);
    SubmitWireCircle(renderer, center, 2, radius, color, width, 48, depthMode);
}

void SubmitWireCircleTransformed(Renderer& renderer, const glm::mat4& transform, const glm::vec3& center, int axis, float radius, const glm::vec4& color, float width, int segments = 48, DebugLineDepthMode depthMode = DebugLineDepthMode::AlwaysOnTop) {
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
            renderer.SubmitLine({previous, current, color, width, depthMode});
        }
        previous = current;
    }
}

void SubmitWireCapsuleY(Renderer& renderer, const glm::mat4& transform, const glm::vec3& center, float radius, float height, const glm::vec4& color, float width, DebugLineDepthMode depthMode = DebugLineDepthMode::AlwaysOnTop) {
    const float cylinderHalfHeight = (std::max)(0.0f, height * 0.5f - radius);
    const glm::vec3 top = center + glm::vec3{0.0f, cylinderHalfHeight, 0.0f};
    const glm::vec3 bottom = center - glm::vec3{0.0f, cylinderHalfHeight, 0.0f};

    SubmitWireCircleTransformed(renderer, transform, top, 1, radius, color, width, 48, depthMode);
    SubmitWireCircleTransformed(renderer, transform, bottom, 1, radius, color, width, 48, depthMode);
    renderer.SubmitLine({TransformPoint(transform, top + glm::vec3{ radius, 0.0f, 0.0f}), TransformPoint(transform, bottom + glm::vec3{ radius, 0.0f, 0.0f}), color, width, depthMode});
    renderer.SubmitLine({TransformPoint(transform, top + glm::vec3{-radius, 0.0f, 0.0f}), TransformPoint(transform, bottom + glm::vec3{-radius, 0.0f, 0.0f}), color, width, depthMode});
    renderer.SubmitLine({TransformPoint(transform, top + glm::vec3{0.0f, 0.0f,  radius}), TransformPoint(transform, bottom + glm::vec3{0.0f, 0.0f,  radius}), color, width, depthMode});
    renderer.SubmitLine({TransformPoint(transform, top + glm::vec3{0.0f, 0.0f, -radius}), TransformPoint(transform, bottom + glm::vec3{0.0f, 0.0f, -radius}), color, width, depthMode});

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
                renderer.SubmitLine({previousTop, topPoint, color, width, depthMode});
                renderer.SubmitLine({previousBottom, bottomPoint, color, width, depthMode});
            }
            previousTop = topPoint;
            previousBottom = bottomPoint;
        }
    }
}

void SubmitWirePlane(Renderer& renderer, const glm::mat4& transform, const glm::vec3& localNormal, float offset, float halfExtent, bool infinite, const glm::vec4& color, float width, DebugLineDepthMode depthMode = DebugLineDepthMode::AlwaysOnTop) {
    glm::vec3 normal = localNormal;
    if (glm::length2(normal) <= 0.000001f) {
        normal = {0.0f, 1.0f, 0.0f};
    } else {
        normal = glm::normalize(normal);
    }

    const glm::vec3 planeOrigin = TransformPoint(transform, normal * offset);
    const glm::vec3 worldNormal = glm::normalize(TransformDirection(transform, normal));
    const glm::vec3 reference = std::abs(worldNormal.y) < 0.99f ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 tangent = glm::normalize(glm::cross(reference, worldNormal));
    const glm::vec3 bitangent = glm::normalize(glm::cross(worldNormal, tangent));
    const float previewExtent = infinite ? 10.0f : (std::max)(0.001f, halfExtent);

    const glm::vec3 corners[4] = {
        planeOrigin + (-tangent - bitangent) * previewExtent,
        planeOrigin + ( tangent - bitangent) * previewExtent,
        planeOrigin + ( tangent + bitangent) * previewExtent,
        planeOrigin + (-tangent + bitangent) * previewExtent
    };
    for (int i = 0; i < 4; ++i) {
        renderer.SubmitLine({corners[i], corners[(i + 1) % 4], color, width, depthMode});
    }

    const float crossExtent = previewExtent * 0.35f;
    renderer.SubmitLine({planeOrigin - tangent * crossExtent, planeOrigin + tangent * crossExtent, color, width, depthMode});
    renderer.SubmitLine({planeOrigin - bitangent * crossExtent, planeOrigin + bitangent * crossExtent, color, width, depthMode});

    const glm::vec3 arrowEnd = planeOrigin + worldNormal * (previewExtent * 0.4f);
    renderer.SubmitLine({planeOrigin, arrowEnd, color, width, depthMode});
    const glm::vec3 arrowSideA = glm::normalize(worldNormal + tangent * 0.35f) * (previewExtent * 0.12f);
    const glm::vec3 arrowSideB = glm::normalize(worldNormal - tangent * 0.35f) * (previewExtent * 0.12f);
    renderer.SubmitLine({arrowEnd, arrowEnd - arrowSideA, color, width, depthMode});
    renderer.SubmitLine({arrowEnd, arrowEnd - arrowSideB, color, width, depthMode});
}

void SubmitCameraFrustum(Renderer& renderer, const SceneObject& object, const glm::mat4& worldMatrix, const glm::vec4& color, float width, DebugLineDepthMode depthMode = DebugLineDepthMode::AlwaysOnTop) {
    if (!object.hasCamera || !object.camera.enabled) {
        return;
    }

    constexpr float aspect = 16.0f / 9.0f;
    const CameraComponent& camera = object.camera;
    const float fov = (std::max)(1.0f, (std::min)(camera.fieldOfViewDegrees, 179.0f));
    const float nearClip = (std::max)(0.001f, camera.nearClip);
    const float farClip = (std::max)(nearClip + 0.001f, camera.farClip);
    const float frustumFar = (std::min)(farClip, (std::max)(nearClip + 0.25f, 5.0f));
    const float nearHalfHeight = std::tan(glm::radians(fov) * 0.5f) * nearClip;
    const float nearHalfWidth = nearHalfHeight * aspect;
    const float farHalfHeight = std::tan(glm::radians(fov) * 0.5f) * frustumFar;
    const float farHalfWidth = farHalfHeight * aspect;

    const glm::vec3 origin = TransformPoint(worldMatrix, {0.0f, 0.0f, 0.0f});
    auto toWorld = [&](const glm::vec3& local) {
        return TransformPoint(worldMatrix, local);
    };

    const glm::vec3 nearCenter{0.0f, 0.0f, -nearClip};
    const glm::vec3 farCenter{0.0f, 0.0f, -frustumFar};
    const glm::vec3 nearCorners[4] = {
        toWorld(nearCenter + glm::vec3{-nearHalfWidth, -nearHalfHeight, 0.0f}),
        toWorld(nearCenter + glm::vec3{ nearHalfWidth, -nearHalfHeight, 0.0f}),
        toWorld(nearCenter + glm::vec3{ nearHalfWidth,  nearHalfHeight, 0.0f}),
        toWorld(nearCenter + glm::vec3{-nearHalfWidth,  nearHalfHeight, 0.0f})
    };
    const glm::vec3 farCorners[4] = {
        toWorld(farCenter + glm::vec3{-farHalfWidth, -farHalfHeight, 0.0f}),
        toWorld(farCenter + glm::vec3{ farHalfWidth, -farHalfHeight, 0.0f}),
        toWorld(farCenter + glm::vec3{ farHalfWidth,  farHalfHeight, 0.0f}),
        toWorld(farCenter + glm::vec3{-farHalfWidth,  farHalfHeight, 0.0f})
    };

    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        renderer.SubmitLine({nearCorners[i], nearCorners[next], color, width, depthMode});
        renderer.SubmitLine({farCorners[i], farCorners[next], color, width, depthMode});
        renderer.SubmitLine({origin, farCorners[i], color, width, depthMode});
        renderer.SubmitLine({nearCorners[i], farCorners[i], color, width, depthMode});
    }
}

void SubmitLightIcon(Renderer& renderer, const SceneObject& object, const glm::mat4& worldMatrix, DebugLineDepthMode depthMode = DebugLineDepthMode::DepthTestedOverlay) {
    if (!object.hasLight || !object.light.enabled) {
        return;
    }

    const glm::vec4 color{1.0f, 0.92f, 0.2f, 1.0f};
    const glm::vec3 origin = TransformPoint(worldMatrix, {0.0f, 0.0f, 0.0f});
    constexpr float width = 2.0f;
    constexpr float iconRadius = 0.25f;

    if (object.light.type == LightType::Directional) {
        const glm::vec3 forward = TransformDirection(worldMatrix, {0.0f, 0.0f, -1.0f});
        const glm::vec3 right = TransformDirection(worldMatrix, {1.0f, 0.0f, 0.0f});
        const glm::vec3 up = TransformDirection(worldMatrix, {0.0f, 1.0f, 0.0f});
        for (int i = -1; i <= 1; ++i) {
            const glm::vec3 offset = right * (static_cast<float>(i) * 0.18f);
            const glm::vec3 start = origin + offset - forward * 0.2f;
            const glm::vec3 end = origin + offset + forward * 0.55f;
            renderer.SubmitLine({start, end, color, width, depthMode});
            renderer.SubmitLine({end, end - forward * 0.18f + up * 0.10f, color, width, depthMode});
            renderer.SubmitLine({end, end - forward * 0.18f - up * 0.10f, color, width, depthMode});
        }
        return;
    }

    if (object.light.type == LightType::Spot) {
        const float previewRange = (std::min)((std::max)(object.light.range, 0.5f), 3.0f);
        const float angle = glm::radians((std::max)(1.0f, (std::min)(object.light.spotAngleDegrees, 179.0f))) * 0.5f;
        const float coneRadius = std::tan(angle) * previewRange;
        const glm::vec3 forward = TransformDirection(worldMatrix, {0.0f, 0.0f, -1.0f});
        const glm::vec3 center = origin + forward * previewRange;
        SubmitWireCircleTransformed(renderer, worldMatrix, glm::vec3{0.0f, 0.0f, -previewRange}, 2, coneRadius, color, width, 32, depthMode);
        renderer.SubmitLine({origin, center + TransformDirection(worldMatrix, { coneRadius, 0.0f, 0.0f}), color, width, depthMode});
        renderer.SubmitLine({origin, center + TransformDirection(worldMatrix, {-coneRadius, 0.0f, 0.0f}), color, width, depthMode});
        renderer.SubmitLine({origin, center + TransformDirection(worldMatrix, {0.0f,  coneRadius, 0.0f}), color, width, depthMode});
        renderer.SubmitLine({origin, center + TransformDirection(worldMatrix, {0.0f, -coneRadius, 0.0f}), color, width, depthMode});
        return;
    }

    SubmitWireSphere(renderer, origin, iconRadius, color, width, depthMode);
    renderer.SubmitLine({origin + glm::vec3{-iconRadius * 1.4f, 0.0f, 0.0f}, origin + glm::vec3{iconRadius * 1.4f, 0.0f, 0.0f}, color, width, depthMode});
    renderer.SubmitLine({origin + glm::vec3{0.0f, -iconRadius * 1.4f, 0.0f}, origin + glm::vec3{0.0f, iconRadius * 1.4f, 0.0f}, color, width, depthMode});
    renderer.SubmitLine({origin + glm::vec3{0.0f, 0.0f, -iconRadius * 1.4f}, origin + glm::vec3{0.0f, 0.0f, iconRadius * 1.4f}, color, width, depthMode});
}

struct PickCandidate {
    int   index;
    float tAabb;
};

bool IsPlanePickFallbackObject(const SceneObject& object) {
    return object.hasMeshFilter && object.meshFilter.meshType == "Plane";
}

// Möller–Trumbore ray-triangle intersection.
bool IntersectRayTriangle(
    const glm::vec3& orig, const glm::vec3& dir,
    const glm::vec3& v0,   const glm::vec3& v1, const glm::vec3& v2,
    float& outT)
{
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    const glm::vec3 h  = glm::cross(dir, e2);
    const float     a  = glm::dot(e1, h);
    if (a > -1e-6f && a < 1e-6f) return false;
    const float     f  = 1.0f / a;
    const glm::vec3 s  = orig - v0;
    const float     u  = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 q  = glm::cross(s, e1);
    const float     v  = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    outT = f * glm::dot(e2, q);
    return outT > 1e-5f;
}

} // namespace

void SceneEditor::SelectProjectFile(const std::string& path) {
    selectedProjectFile_ = NormalizeSlashes(path);
    selectedModelChildMeshIndex_ = -1;
    if (!selectedProjectFile_.empty()) {
        selectedProjectDirectory_ = ParentProjectDirectory(selectedProjectFile_);
    }
    inspectMaterial_ = false;
    inspectedMaterialId_.clear();
    if (IsMeshAssetPath(selectedProjectFile_)) {
        selectedIndex_ = -1;
        selectedIndices_.clear();
        inspectMaterial_ = false;
    }

    if (IsVehicleConfigAssetPath(selectedProjectFile_)) {
        // Opening a vehicle config — reset its state and close the sound editor
        inspectedVehicleConfigLoaded_ = false;
        inspectedVehicleConfigError_.clear();
        vehicleConfigUndoStack_.clear();
        vehicleConfigRedoStack_.clear();
        vehicleConfigEditActive_ = false;
        inspectedVehicleConfigPath_ = selectedProjectFile_;
        showVehicleConfigEditor_ = true;
        inspectedVehicleSoundPath_.clear();
        showVehicleSoundEditor_ = false;
        inspectedVehicleSoundLoaded_ = false;
        inspectedVehicleSoundError_.clear();
        vehicleSoundUndoStack_.clear();
        vehicleSoundRedoStack_.clear();
        vehicleSoundEditActive_ = false;
    } else if (IsVehicleSoundAssetPath(selectedProjectFile_)) {
        // Opening a vehicle sound profile — reset its state and close the config editor
        inspectedVehicleSoundLoaded_ = false;
        inspectedVehicleSoundError_.clear();
        vehicleSoundUndoStack_.clear();
        vehicleSoundRedoStack_.clear();
        vehicleSoundEditActive_ = false;
        inspectedVehicleSoundPath_ = selectedProjectFile_;
        showVehicleSoundEditor_ = true;
        inspectedVehicleConfigPath_.clear();
        showVehicleConfigEditor_ = false;
        inspectedVehicleConfigLoaded_ = false;
        inspectedVehicleConfigError_.clear();
        vehicleConfigUndoStack_.clear();
        vehicleConfigRedoStack_.clear();
        vehicleConfigEditActive_ = false;
    } else if (IsTrackAssetPath(selectedProjectFile_)) {
        OpenTrackGenerator(selectedProjectFile_);
    }
    // For neutral file types (audio clips, images, meshes, scripts, etc.)
    // do NOT change editor state — the user may be selecting a file to
    // drag into an open editor window (e.g. dragging a .wav into the
    // Vehicle Sound Profile editor's clip field).
}

void SubmitWireMesh(Renderer& renderer,
                    const glm::mat4& transform,
                    const std::vector<glm::vec3>& vertices,
                    const std::vector<unsigned int>& indices,
                    const glm::vec4& color,
                    float width,
                    DebugLineDepthMode depthMode,
                    std::size_t maxTriangles = 0) {
    if (vertices.empty() || indices.size() < 3) {
        return;
    }

    const std::size_t triangleCount = indices.size() / 3;
    const std::size_t trianglesToDraw = maxTriangles == 0 ? triangleCount : (std::min)(triangleCount, maxTriangles);

    for (std::size_t tri = 0; tri < trianglesToDraw; ++tri) {
        const std::size_t base = tri * 3;
        if (base + 2 >= indices.size()) {
            break;
        }

        const unsigned int i0 = indices[base + 0];
        const unsigned int i1 = indices[base + 1];
        const unsigned int i2 = indices[base + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }

        const glm::vec3 p0 = TransformPoint(transform, vertices[i0]);
        const glm::vec3 p1 = TransformPoint(transform, vertices[i1]);
        const glm::vec3 p2 = TransformPoint(transform, vertices[i2]);

        renderer.SubmitLine({p0, p1, color, width, depthMode});
        renderer.SubmitLine({p1, p2, color, width, depthMode});
        renderer.SubmitLine({p2, p0, color, width, depthMode});
    }
}

void SceneEditor::TrySelectObjectAtMouse(Renderer& renderer) {
    ImGuiIO& io = ImGui::GetIO();

    // Only pick when the scene viewport is the one under the mouse.
    // sceneViewportHovered_ tracks the actual ImGui widget hover, which is
    // more reliable than a raw bounds check (prevents clicks on the game
    // viewport from triggering scene picks).
    if (io.WantTextInput ||
        editorCameraNavigating_ ||
        io.MouseDown[1] ||
        !io.MouseClicked[0] ||
        !sceneViewportHovered_ ||
        activeGizmoAxis_ >= 0 ||
        ImGuizmo::IsUsing() ||
        ImGuizmo::IsOver()) {
        return;
    }

    glm::vec3 rayOrigin;
    glm::vec3 rayDirection;
    const glm::vec2 mouse{io.MousePos.x, io.MousePos.y};
    if (!MakeMouseRay(renderer, mouse, rayOrigin, rayDirection)) {
        return;
    }

    // --- Phase 1: broad phase — AABB test, collect candidates ---
    std::vector<PickCandidate> candidates;
    candidates.reserve(32);

    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (!IsObjectEffectivelyEnabled(i)) continue;

        glm::vec3 localMin, localMax;
        if (!GetObjectLocalBounds(objects_[i], localMin, localMax)) continue;

        // Apply pivotOffset to the model matrix (same as in SubmitDraws)
        glm::mat4 meshMatrix = GetObjectWorldMatrix(i);
        const glm::vec3& po = objects_[i].meshFilter.pivotOffset;
        if (po.x != 0.0f || po.y != 0.0f || po.z != 0.0f) {
            meshMatrix = meshMatrix * glm::translate(glm::mat4(1.0f), -po);
        }

        glm::vec3 worldMin, worldMax;
        if (!ComputeWorldAabb(meshMatrix, localMin, localMax, worldMin, worldMax)) continue;

        float tAabb = 0.0f;
        if (IntersectRayAabb(rayOrigin, rayDirection, worldMin, worldMax, tAabb)) {
            candidates.push_back({i, tAabb});
        }
    }

    // Sort nearest AABB hit first so narrow phase stops at the front-most hit
    std::sort(candidates.begin(), candidates.end(),
              [](const PickCandidate& a, const PickCandidate& b) { return a.tAabb < b.tAabb; });

    // --- Phase 2: narrow phase — mesh triangle test ---
    int   bestTriangleIndex = -1;
    float bestTriangleT     = (std::numeric_limits<float>::max)();
    int   bestFallbackIndex = -1;
    float bestFallbackT     = (std::numeric_limits<float>::max)();
    int   bestPlaneFallbackIndex = -1;
    float bestPlaneFallbackT     = (std::numeric_limits<float>::max)();

    for (const PickCandidate& cand : candidates) {
        // Early-out only after an exact triangle hit. AABB fallback objects,
        // especially large planes, should not block later exact mesh hits.
        if (bestTriangleIndex >= 0 && cand.tAabb >= bestTriangleT) break;

        const SceneObject& obj = objects_[cand.index];

        // Apply pivotOffset to the mesh matrix
        glm::mat4 meshMatrix = GetObjectWorldMatrix(cand.index);
        const glm::vec3& po = obj.meshFilter.pivotOffset;
        if (po.x != 0.0f || po.y != 0.0f || po.z != 0.0f) {
            meshMatrix = meshMatrix * glm::translate(glm::mat4(1.0f), -po);
        }

        // If the object has CPU pick data, test triangles in world space.
        // Vertices are transformed per-click, which is fine (picking is infrequent).
        bool narrowTested = false;
        const std::vector<glm::vec3>&    pv = obj.meshFilter.pickVertices;
        const std::vector<unsigned int>& pi = obj.meshFilter.pickIndices;
        if (!pv.empty() && pi.size() >= 3) {
            const std::size_t triCount = pi.size() / 3;
            for (std::size_t tri = 0; tri < triCount; ++tri) {
                const glm::vec3 wv0 = glm::vec3(meshMatrix * glm::vec4(pv[pi[tri * 3 + 0]], 1.0f));
                const glm::vec3 wv1 = glm::vec3(meshMatrix * glm::vec4(pv[pi[tri * 3 + 1]], 1.0f));
                const glm::vec3 wv2 = glm::vec3(meshMatrix * glm::vec4(pv[pi[tri * 3 + 2]], 1.0f));
                float tTri = 0.0f;
                if (IntersectRayTriangle(rayOrigin, rayDirection, wv0, wv1, wv2, tTri) && tTri < bestTriangleT) {
                    bestTriangleT     = tTri;
                    bestTriangleIndex = cand.index;
                }
            }
            narrowTested = true;
        }

        // Fallback for primitives (Plane, primitive meshes without modelRef):
        // accept the AABB hit as the pick result.
        if (!narrowTested) {
            if (IsPlanePickFallbackObject(obj)) {
                if (cand.tAabb < bestPlaneFallbackT) {
                    bestPlaneFallbackT = cand.tAabb;
                    bestPlaneFallbackIndex = cand.index;
                }
            } else if (cand.tAabb < bestFallbackT) {
                bestFallbackT = cand.tAabb;
                bestFallbackIndex = cand.index;
            }
        }
    }

    int bestIndex = bestTriangleIndex;
    if (bestIndex < 0) {
        bestIndex = bestFallbackIndex >= 0 ? bestFallbackIndex : bestPlaneFallbackIndex;
    }

    if (bestIndex >= 0) {
        if (io.KeyCtrl) {
            ToggleSelect(bestIndex);
        } else {
            Select(bestIndex);
        }
    } else if (!io.KeyCtrl) {
        selectedIndex_ = -1;
        selectedIndices_.clear();
        inspectMaterial_ = false;
        activeGizmoAxis_ = -1;
    }
}

void SceneEditor::UpdateGizmo(Renderer& renderer) {
    NormalizeSelection();
    hoveredGizmoAxis_ = -1;

    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        activeGizmoAxis_ = -1;
        gizmoDragSelectionIndices_.clear();
        gizmoDragStartLocalTransforms_.clear();
        gizmoDragStartWorldMatrices_.clear();
        TrySelectObjectAtMouse(renderer);
        return;
    }

    if (!IsObjectEffectivelyEnabled(selectedIndex_)) {
        activeGizmoAxis_ = -1;
        gizmoDragSelectionIndices_.clear();
        gizmoDragStartLocalTransforms_.clear();
        gizmoDragStartWorldMatrices_.clear();
        TrySelectObjectAtMouse(renderer);
        return;
    }

    TrySelectObjectAtMouse(renderer);
}

void SceneEditor::UpdateImGuizmo() {
    NormalizeSelection();
    if (!hasEditorCameraMatrices_ ||
        activeViewport_ != SceneEditorActiveViewport::Scene ||
        sceneViewportSize_.x <= 1.0f ||
        sceneViewportSize_.y <= 1.0f) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(
        sceneViewportPos_.x,
        sceneViewportPos_.y,
        sceneViewportSize_.x,
        sceneViewportSize_.y);

    // View orientation cube in the top-right of the scene viewport (Unity/Blender-style).
    // Click a face/axis to snap the camera; drag to orbit. Always drawn, regardless of selection.
    {
        const float cubeSize = 100.0f;
        const float pad = 10.0f;
        const ImVec2 cubePos(
            sceneViewportPos_.x + sceneViewportSize_.x - cubeSize - pad,
            sceneViewportPos_.y + pad);
        glm::mat4 viewBefore = editorCameraView_;
        ImGuizmo::ViewManipulate(
            glm::value_ptr(editorCameraView_),
            8.0f,
            cubePos,
            ImVec2(cubeSize, cubeSize),
            0x10101010);
        if (onEditorCameraViewChanged_ &&
            std::memcmp(glm::value_ptr(editorCameraView_),
                        glm::value_ptr(viewBefore),
                        sizeof(glm::mat4)) != 0) {
            onEditorCameraViewChanged_(editorCameraView_);
        }
    }

    // Object transform manipulator — needs a valid, enabled selection.
    if (selectedIndex_ < 0 ||
        selectedIndex_ >= static_cast<int>(objects_.size()) ||
        !IsObjectEffectivelyEnabled(selectedIndex_)) {
        return;
    }

    SceneObject& selectedObject = objects_[selectedIndex_];
    const SceneColliderType selectedColliderType = GetActiveColliderType(selectedObject);
    if (colliderEditMode_ && selectedColliderType != SceneColliderType::None) {
        glm::mat4 objectWorldMatrix = GetObjectDisplayWorldMatrix(selectedIndex_);
        glm::mat4 colliderLocalMatrix(1.0f);
        bool canManipulateCollider = false;

        if (selectedColliderType == SceneColliderType::Box && selectedObject.boxCollider.enabled) {
            const glm::vec3 boxSize{
                (std::max)(0.01f, selectedObject.boxCollider.size.x),
                (std::max)(0.01f, selectedObject.boxCollider.size.y),
                (std::max)(0.01f, selectedObject.boxCollider.size.z)
            };
            colliderLocalMatrix = glm::translate(glm::mat4(1.0f), selectedObject.boxCollider.center)
                * glm::scale(glm::mat4(1.0f), boxSize);
            canManipulateCollider = true;
        } else if (selectedColliderType == SceneColliderType::Sphere && selectedObject.sphereCollider.enabled) {
            const float diameter = (std::max)(0.01f, selectedObject.sphereCollider.radius * 2.0f);
            colliderLocalMatrix = glm::translate(glm::mat4(1.0f), selectedObject.sphereCollider.center)
                * glm::scale(glm::mat4(1.0f), glm::vec3(diameter));
            canManipulateCollider = true;
        } else if (selectedColliderType == SceneColliderType::Capsule && selectedObject.capsuleCollider.enabled) {
            const float radius = (std::max)(0.01f, selectedObject.capsuleCollider.radius);
            const float height = (std::max)(radius * 2.0f, selectedObject.capsuleCollider.height);
            colliderLocalMatrix = glm::translate(glm::mat4(1.0f), selectedObject.capsuleCollider.center)
                * glm::scale(glm::mat4(1.0f), glm::vec3(radius * 2.0f, height, radius * 2.0f));
            canManipulateCollider = true;
        }

        if (canManipulateCollider) {
            glm::mat4 colliderWorldMatrix = objectWorldMatrix * colliderLocalMatrix;
            glm::mat4 deltaMatrix(1.0f);
            const bool allowColliderGizmoInput = !io.WantTextInput && !io.MouseDown[1] &&
                (sceneViewportHovered_ || colliderGizmoActive_ || ImGuizmo::IsUsing());
            ImGuizmo::Enable(allowColliderGizmoInput);
            const bool manipulated = ImGuizmo::Manipulate(
                glm::value_ptr(editorCameraView_),
                glm::value_ptr(editorCameraProj_),
                static_cast<ImGuizmo::OPERATION>(ImGuizmo::TRANSLATE | ImGuizmo::SCALE),
                ImGuizmo::LOCAL,
                glm::value_ptr(colliderWorldMatrix),
                glm::value_ptr(deltaMatrix));
            ImGuizmo::Enable(true);

            if (ImGuizmo::IsUsing()) {
                if (!colliderGizmoActive_) {
                    PushUndoState();
                    colliderGizmoActive_ = true;
                    colliderGizmoDirtyDuringDrag_ = false;
                }
                if (manipulated) {
                    const glm::mat4 newColliderLocalMatrix = glm::inverse(objectWorldMatrix) * colliderWorldMatrix;
                    const Transform colliderTransform = TransformFromMatrix(newColliderLocalMatrix);
                    if (selectedColliderType == SceneColliderType::Box) {
                        selectedObject.boxCollider.center = colliderTransform.position;
                        selectedObject.boxCollider.size = {
                            (std::max)(0.01f, std::abs(colliderTransform.scale.x)),
                            (std::max)(0.01f, std::abs(colliderTransform.scale.y)),
                            (std::max)(0.01f, std::abs(colliderTransform.scale.z))
                        };
                    } else if (selectedColliderType == SceneColliderType::Sphere) {
                        selectedObject.sphereCollider.center = colliderTransform.position;
                        selectedObject.sphereCollider.radius = (std::max)(
                            0.005f,
                            ((std::abs(colliderTransform.scale.x) + std::abs(colliderTransform.scale.y) + std::abs(colliderTransform.scale.z)) / 3.0f) * 0.5f);
                    } else if (selectedColliderType == SceneColliderType::Capsule) {
                        selectedObject.capsuleCollider.center = colliderTransform.position;
                        const float radius = (std::max)(
                            0.005f,
                            (std::abs(colliderTransform.scale.x) + std::abs(colliderTransform.scale.z)) * 0.25f);
                        selectedObject.capsuleCollider.radius = radius;
                        selectedObject.capsuleCollider.height = (std::max)(radius * 2.0f, std::abs(colliderTransform.scale.y));
                    }
                    colliderGizmoDirtyDuringDrag_ = true;
                }
                return;
            }

            if (colliderGizmoActive_) {
                if (colliderGizmoDirtyDuringDrag_ && onDirty_) {
                    onDirty_();
                }
                colliderGizmoActive_ = false;
                colliderGizmoDirtyDuringDrag_ = false;
                return;
            }

            return;
        }
    }

    auto getObjectGizmoWorldMatrix = [&](int index) {
        glm::mat4 world = GetObjectDisplayWorldMatrix(index);
        if (index >= 0 && index < static_cast<int>(objects_.size())) {
            const SceneObject& object = objects_[index];
            if (object.hasCamera && object.camera.enabled && object.hasCinemachine && object.cinemachine.enabled) {
                world = world * glm::scale(glm::mat4(1.0f), object.transform.scale);
            }
        }
        return world;
    };
    glm::mat4 objectWorldMatrix = getObjectGizmoWorldMatrix(selectedIndex_);
    const glm::mat4 gizmoInputWorldMatrix = objectWorldMatrix;
    glm::mat4 deltaMatrix(1.0f);
    const bool allowGizmoInput = !io.WantTextInput && !io.MouseDown[1] &&
        (sceneViewportHovered_ || activeGizmoAxis_ >= 0 || ImGuizmo::IsUsing());
    ImGuizmo::Enable(allowGizmoInput);
    const bool manipulated = ImGuizmo::Manipulate(
        glm::value_ptr(editorCameraView_),
        glm::value_ptr(editorCameraProj_),
        ImGuizmoOperationFromMode(gizmoMode_),
        ImGuizmoTransformModeFromMode(gizmoMode_),
        glm::value_ptr(objectWorldMatrix),
        glm::value_ptr(deltaMatrix));
    ImGuizmo::Enable(true);

    if (ImGuizmo::IsUsing()) {
        if (activeGizmoAxis_ < 0) {
            PushUndoState();
            activeGizmoAxis_ = 3;
            gizmoDragSelectionIndices_ = selectedIndices_;
            if (gizmoDragSelectionIndices_.empty()) {
                gizmoDragSelectionIndices_.push_back(selectedIndex_);
            }
            std::sort(gizmoDragSelectionIndices_.begin(), gizmoDragSelectionIndices_.end(), [&](int a, int b) {
                const int depthA = HierarchyDepth(objects_, a);
                const int depthB = HierarchyDepth(objects_, b);
                if (depthA != depthB) {
                    return depthA < depthB;
                }
                return a < b;
            });
            gizmoDragSelectionIndices_.erase(
                std::remove_if(gizmoDragSelectionIndices_.begin(), gizmoDragSelectionIndices_.end(), [&](int index) {
                    if (index < 0 || index >= static_cast<int>(objects_.size())) {
                        return true;
                    }
                    std::string parentId = objects_[index].parentId;
                    std::vector<std::string> visited;
                    while (!parentId.empty()) {
                        if (std::find(visited.begin(), visited.end(), parentId) != visited.end()) {
                            break;
                        }
                        visited.push_back(parentId);
                        const int parentIndex = FindObjectIndexById(parentId);
                        if (parentIndex < 0) {
                            break;
                        }
                        if (std::find(gizmoDragSelectionIndices_.begin(), gizmoDragSelectionIndices_.end(), parentIndex) != gizmoDragSelectionIndices_.end()) {
                            return true;
                        }
                        parentId = objects_[parentIndex].parentId;
                    }
                    return false;
                }),
                gizmoDragSelectionIndices_.end());
            if (gizmoDragSelectionIndices_.empty()) {
                gizmoDragSelectionIndices_.push_back(selectedIndex_);
            }
            gizmoDragStartLocalTransforms_.clear();
            gizmoDragStartWorldMatrices_.clear();
            gizmoDragStartLocalTransforms_.reserve(gizmoDragSelectionIndices_.size());
            gizmoDragStartWorldMatrices_.reserve(gizmoDragSelectionIndices_.size());
            for (int index : gizmoDragSelectionIndices_) {
                gizmoDragStartLocalTransforms_.push_back(objects_[index].transform);
                gizmoDragStartWorldMatrices_.push_back(getObjectGizmoWorldMatrix(index));
            }
            gizmoDirtyDuringDrag_ = false;
        }

        if (manipulated && !gizmoDragStartWorldMatrices_.empty()) {
            auto applyWorldMatrixToObject = [&](int index, const glm::mat4& targetWorld) {
                if (index < 0 || index >= static_cast<int>(objects_.size())) {
                    return;
                }
                SceneObject& object = objects_[index];
                if (object.hasCamera && object.camera.enabled && object.hasCinemachine && object.cinemachine.enabled) {
                    if (gizmoMode_ == GizmoMode::Move) {
                        // Apply the incremental gizmo translation instead of
                        // converting the evaluated camera pose back to an absolute
                        // offset every frame. Look-at rotation and moving follow
                        // targets otherwise feed back into ImGuizmo and make the
                        // camera drift or jump while dragging.
                        const glm::vec3 worldDelta = glm::vec3(deltaMatrix[3]);
                        const bool followsTarget = object.cinemachine.type != CinemachineCameraType::LookAt &&
                            !object.cinemachine.followTargetId.empty();
                        const int followIndex = followsTarget
                            ? FindObjectIndexById(object.cinemachine.followTargetId)
                            : -1;
                        if (followIndex >= 0 && followIndex != index) {
                            const glm::quat followRotation = ExtractWorldRotationNoScale(GetObjectWorldMatrix(followIndex));
                            object.transform.position += glm::inverse(followRotation) * worldDelta;
                        } else {
                            const int parentIndex = FindObjectIndexById(object.parentId);
                            const glm::vec3 localDelta = parentIndex >= 0
                                ? glm::vec3(glm::inverse(GetObjectWorldMatrix(parentIndex)) * glm::vec4(worldDelta, 0.0f))
                                : worldDelta;
                            object.transform.position += localDelta;
                        }
                        object.cinemachine.followOffset = object.transform.position;
                        return;
                    }

                    if (gizmoMode_ == GizmoMode::Scale) {
                        const Transform manipulatedTransform = TransformFromMatrix(targetWorld);
                        object.transform.scale = {
                            (std::max)(std::abs(manipulatedTransform.scale.x), 0.01f),
                            (std::max)(std::abs(manipulatedTransform.scale.y), 0.01f),
                            (std::max)(std::abs(manipulatedTransform.scale.z), 0.01f)
                        };
                        return;
                    }

                    const glm::vec3 targetPosition = glm::vec3(targetWorld[3]);
                    if (gizmoMode_ == GizmoMode::Rotate) {
                        glm::quat baseRotation = ExtractWorldRotationNoScale(GetObjectWorldMatrix(index));
                        if (object.cinemachine.type == CinemachineCameraType::Follow && !object.cinemachine.followTargetId.empty()) {
                            const int followIndex = FindObjectIndexById(object.cinemachine.followTargetId);
                            if (followIndex >= 0) {
                                baseRotation = ExtractWorldRotationNoScale(GetObjectWorldMatrix(followIndex));
                            }
                        } else if ((object.cinemachine.type == CinemachineCameraType::LookAt || object.cinemachine.type == CinemachineCameraType::FollowAndLookAt)) {
                            const std::string& lookAtId = object.cinemachine.lookAtTargetId.empty() ? object.cinemachine.followTargetId : object.cinemachine.lookAtTargetId;
                            const int lookAtIndex = lookAtId.empty() ? -1 : FindObjectIndexById(lookAtId);
                            if (lookAtIndex >= 0 && lookAtIndex != index) {
                                const glm::vec3 lookAtPos = glm::vec3(GetObjectWorldMatrix(lookAtIndex)[3]);
                                const glm::vec3 dir = lookAtPos - targetPosition;
                                if (glm::length(dir) > 0.001f) {
                                    const glm::vec3 fwd = glm::normalize(dir);
                                    const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
                                    glm::vec3 right = glm::cross(fwd, worldUp);
                                    right = (glm::length(right) > 0.001f) ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
                                    const glm::vec3 up = glm::normalize(glm::cross(right, fwd));
                                    baseRotation = glm::quat_cast(glm::mat3(right, up, -fwd));
                                }
                            }
                        }
                        const glm::quat targetRotation = ExtractWorldRotationNoScale(targetWorld);
                        object.transform.rotationEuler = NearestEulerEquivalent(
                            glm::degrees(glm::eulerAngles(glm::normalize(glm::inverse(baseRotation) * targetRotation))),
                            object.transform.rotationEuler);
                    }
                    object.cinemachine.pitchOffset = object.transform.rotationEuler.x;
                    object.cinemachine.yawOffset = object.transform.rotationEuler.y;
                    return;
                }
                const int parentIndex = FindObjectIndexById(objects_[index].parentId);
                const glm::mat4 targetLocal = parentIndex >= 0
                    ? glm::inverse(GetObjectWorldMatrix(parentIndex)) * targetWorld
                    : targetWorld;
                Transform targetTransform = TransformFromMatrix(targetLocal);
                targetTransform.rotationEuler = NearestEulerEquivalent(targetTransform.rotationEuler, objects_[index].transform.rotationEuler);
                targetTransform.scale = {
                    (std::max)(targetTransform.scale.x, 0.01f),
                    (std::max)(targetTransform.scale.y, 0.01f),
                    (std::max)(targetTransform.scale.z, 0.01f)
                };
                objects_[index].transform = targetTransform;
            };

            if (gizmoDragSelectionIndices_.size() == 1 && gizmoDragSelectionIndices_.front() == selectedIndex_) {
                applyWorldMatrixToObject(selectedIndex_, objectWorldMatrix);
                gizmoDirtyDuringDrag_ = true;
                return;
            }

            const glm::mat4 worldDelta = objectWorldMatrix * glm::inverse(gizmoInputWorldMatrix);
            for (std::size_t i = 0; i < gizmoDragSelectionIndices_.size(); ++i) {
                const int index = gizmoDragSelectionIndices_[i];
                const glm::mat4 targetWorld = worldDelta * getObjectGizmoWorldMatrix(index);
                applyWorldMatrixToObject(index, targetWorld);
            }
            gizmoDirtyDuringDrag_ = true;
        }
        return;
    }

    if (activeGizmoAxis_ >= 0) {
        if (gizmoDirtyDuringDrag_ && onDirty_) {
            onDirty_();
        }
        activeGizmoAxis_ = -1;
        gizmoDirtyDuringDrag_ = false;
        gizmoDragSelectionIndices_.clear();
        gizmoDragStartLocalTransforms_.clear();
        gizmoDragStartWorldMatrices_.clear();
        return;
    }
}

void SceneEditor::SubmitColliderWireframe(Renderer& renderer, int objectIndex, const glm::vec4& colorOverride, bool useColorOverride) {
    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects_.size())) {
        return;
    }
    const SceneObject& object = objects_[objectIndex];
    if (!IsObjectEffectivelyEnabled(objectIndex)) {
        return;
    }

    const glm::vec4 colliderColor = useColorOverride ? colorOverride : glm::vec4{0.1f, 0.9f, 0.35f, 1.0f};
    constexpr float colliderWidth = 2.0f;
    constexpr DebugLineDepthMode helperDepthMode = DebugLineDepthMode::DepthTestedOverlay;
    const glm::mat4 objectMatrix = GetObjectDisplayWorldMatrix(objectIndex);
    const SceneColliderType colliderType = GetActiveColliderType(object);
    if (colliderType == SceneColliderType::Box && object.boxCollider.enabled) {
        SubmitWireBox(
            renderer,
            objectMatrix,
            object.boxCollider.center,
            object.boxCollider.size,
            colliderColor,
            colliderWidth,
            helperDepthMode);
    }
    if (colliderType == SceneColliderType::Sphere && object.sphereCollider.enabled) {
        SubmitWireSphere(
            renderer,
            TransformPoint(objectMatrix, object.sphereCollider.center),
            object.sphereCollider.radius * (std::max)((std::max)(std::abs(object.transform.scale.x), std::abs(object.transform.scale.y)), std::abs(object.transform.scale.z)),
            colliderColor,
            colliderWidth,
            helperDepthMode);
    }
    if (colliderType == SceneColliderType::Capsule && object.capsuleCollider.enabled) {
        const float radius = object.capsuleCollider.radius;
        const float height = (std::max)(object.capsuleCollider.height, radius * 2.0f);
        SubmitWireCapsuleY(renderer, objectMatrix, object.capsuleCollider.center, radius, height, colliderColor, colliderWidth, helperDepthMode);
    }
    if (colliderType == SceneColliderType::Plane && object.planeCollider.enabled) {
        const glm::mat4 planeMatrix = BuildObjectMatrixNoScale(object);
        SubmitWirePlane(
            renderer,
            planeMatrix,
            object.planeCollider.normal,
            object.planeCollider.offset,
            object.planeCollider.halfExtent,
            object.planeCollider.infinite,
            useColorOverride ? colorOverride : glm::vec4{0.45f, 0.8f, 1.0f, 1.0f},
            colliderWidth,
            helperDepthMode);
    }
    if (colliderType == SceneColliderType::Mesh && object.meshCollider.enabled) {
        // Mesh vertices are in the asset's original coordinate space. For center-pivot
        // objects the object sits at meshCenter in the hierarchy, but the pivot offset
        // is pre-subtracted at render time (T(-pivotOffset)). Apply the same correction
        // here so the wire overlay aligns with the rendered mesh.
        glm::mat4 meshGizmoMatrix = objectMatrix;
        const glm::vec3& po = object.meshFilter.pivotOffset;
        if (po.x != 0.0f || po.y != 0.0f || po.z != 0.0f) {
            meshGizmoMatrix = meshGizmoMatrix * glm::translate(glm::mat4(1.0f), -po);
        }
        const glm::vec4 meshColliderColor = useColorOverride ? colorOverride : glm::vec4{0.95f, 0.55f, 0.2f, 1.0f};
        ImportedCollisionMesh mesh;
        if (TryGetCollisionMeshForObject(object, mesh)) {
            SubmitWireMesh(
                renderer,
                meshGizmoMatrix,
                mesh.vertices,
                mesh.indices,
                meshColliderColor,
                colliderWidth,
                helperDepthMode);
        } else {
            glm::vec3 boundsMin{0.0f};
            glm::vec3 boundsMax{0.0f};
            if (GetObjectLocalBounds(object, boundsMin, boundsMax)) {
                const glm::vec3 size = boundsMax - boundsMin;
                const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                SubmitWireBox(renderer, meshGizmoMatrix, center, size, meshColliderColor, colliderWidth, helperDepthMode);
            }
        }
    }
    if (object.hasCharacterController && object.characterController.enabled) {
        const float radius = (std::max)(0.001f, object.characterController.radius);
        const float height = (std::max)(radius * 2.0f, object.characterController.height);
        const glm::mat4 controllerMatrix = BuildObjectMatrixNoScale(object);
        const glm::vec4 controllerColor = useColorOverride ? colorOverride : glm::vec4{0.2f, 0.75f, 1.0f, 1.0f};
        const glm::vec3 controllerCenter = object.characterController.center + glm::vec3{0.0f, height * 0.5f, 0.0f};
        SubmitWireCapsuleY(renderer, controllerMatrix, controllerCenter, radius, height, controllerColor, colliderWidth, helperDepthMode);
    }
}

void SceneEditor::SubmitAllColliders(Renderer& renderer) {
    const glm::vec4 allCollidersColor{0.95f, 0.75f, 0.15f, 0.85f};
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (i == selectedIndex_) {
            continue;
        }
        SubmitColliderWireframe(renderer, i, allCollidersColor, true);
    }
}

void SceneEditor::SubmitGizmo(Renderer& renderer) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }

    const SceneObject& object = objects_[selectedIndex_];
    if (!IsObjectEffectivelyEnabled(selectedIndex_)) {
        return;
    }

    constexpr DebugLineDepthMode helperDepthMode = DebugLineDepthMode::DepthTestedOverlay;
    const glm::mat4 objectMatrix = GetObjectDisplayWorldMatrix(selectedIndex_);
    SubmitColliderWireframe(renderer, selectedIndex_, glm::vec4{0.0f}, false);
    if (object.hasCamera && object.camera.enabled) {
        SubmitCameraFrustum(renderer, object, objectMatrix, glm::vec4{1.0f, 0.85f, 0.2f, 1.0f}, 2.0f, helperDepthMode);
    }
    if (object.hasLight && object.light.enabled) {
        SubmitLightIcon(renderer, object, objectMatrix, helperDepthMode);
    }
}

void SceneEditor::SubmitCullingDebug(Renderer& renderer) {
    if (!showCullingDebug_ || !physicsWorld_) {
        return;
    }

    const PhysicsCullingDebugInfo info = physicsWorld_->GetCullingDebugInfo();

    // Draw activation and deactivation radius rings around each activator.
    // Green  = wake zone  (body inside → stays/becomes active)
    // Orange = sleep zone (body outside → deactivated)
    if (info.hasActivators) {
        const glm::vec4 kWakeColor  {0.2f, 1.0f, 0.2f, 1.0f};
        const glm::vec4 kSleepColor {1.0f, 0.55f, 0.1f, 1.0f};
        constexpr int kSegments = 64;
        for (const glm::vec3& apos : info.activatorPositions) {
            SubmitWireCircle(renderer, apos, 1, info.activationRadius,   kWakeColor,  2.0f, kSegments, DebugLineDepthMode::DepthTestedOverlay);
            SubmitWireCircle(renderer, apos, 1, info.deactivationRadius, kSleepColor, 2.0f, kSegments, DebugLineDepthMode::DepthTestedOverlay);
        }
    }

    // Draw a small cross at each dynamic body position.
    // Bright green = currently active, dark grey = sleeping.
    const glm::vec4 kActiveColor  {0.1f, 1.0f, 0.1f, 1.0f};
    const glm::vec4 kSleepingColor{0.4f, 0.4f, 0.4f, 1.0f};
    constexpr float kCrossHalf = 0.4f;
    for (const PhysicsCullingDebugInfo::BodyDebug& bd : info.dynamicBodies) {
        const glm::vec4& col = bd.isActive ? kActiveColor : kSleepingColor;
        renderer.SubmitLine({bd.position - glm::vec3{kCrossHalf, 0, 0}, bd.position + glm::vec3{kCrossHalf, 0, 0}, col, 2.0f, DebugLineDepthMode::AlwaysOnTop});
        renderer.SubmitLine({bd.position - glm::vec3{0, kCrossHalf, 0}, bd.position + glm::vec3{0, kCrossHalf, 0}, col, 2.0f, DebugLineDepthMode::AlwaysOnTop});
        renderer.SubmitLine({bd.position - glm::vec3{0, 0, kCrossHalf}, bd.position + glm::vec3{0, 0, kCrossHalf}, col, 2.0f, DebugLineDepthMode::AlwaysOnTop});
    }
}

} // namespace raceman


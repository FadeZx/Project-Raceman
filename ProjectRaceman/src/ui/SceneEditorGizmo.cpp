#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/quaternion.hpp>
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

} // namespace

void SceneEditor::SelectProjectFile(const std::string& path) {
    selectedProjectFile_ = NormalizeSlashes(path);
    if (!selectedProjectFile_.empty()) {
        selectedProjectDirectory_ = ParentProjectDirectory(selectedProjectFile_);
    }
    inspectMaterial_ = false;
    inspectedMaterialId_.clear();
    inspectedVehicleConfigLoaded_ = false;
    inspectedVehicleConfigError_.clear();
    vehicleConfigUndoStack_.clear();
    vehicleConfigRedoStack_.clear();
    vehicleConfigEditActive_ = false;
    if (IsVehicleConfigAssetPath(selectedProjectFile_)) {
        inspectedVehicleConfigPath_ = selectedProjectFile_;
        showVehicleConfigEditor_ = true;
    } else {
        inspectedVehicleConfigPath_.clear();
        showVehicleConfigEditor_ = false;
    }
}

void SubmitWireMesh(Renderer& renderer,
                    const glm::mat4& transform,
                    const std::vector<glm::vec3>& vertices,
                    const std::vector<unsigned int>& indices,
                    const glm::vec4& color,
                    float width,
                    DebugLineDepthMode depthMode,
                    std::size_t maxTriangles = 2000) {
    if (vertices.empty() || indices.size() < 3) {
        return;
    }

    const std::size_t triangleCount = indices.size() / 3;
    const std::size_t stride = triangleCount > maxTriangles ? (triangleCount + maxTriangles - 1) / maxTriangles : 1;

    for (std::size_t tri = 0; tri < triangleCount; tri += stride) {
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
    if (io.WantTextInput || io.MouseDown[1] || !io.MouseClicked[0] || !ContainsSceneViewportPoint(io.MousePos.x, io.MousePos.y)) {
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
    int bestPlaneIndex = -1;
    float bestPlaneT = (std::numeric_limits<float>::max)();
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (!IsObjectEffectivelyEnabled(i)) {
            continue;
        }
        glm::vec3 boundsMin;
        glm::vec3 boundsMax;
        if (!GetObjectLocalBounds(objects_[i], boundsMin, boundsMax)) {
            continue;
        }

        const glm::mat4 model = GetObjectWorldMatrix(i);
        glm::vec3 worldMin;
        glm::vec3 worldMax;
        if (!ComputeWorldAabb(model, boundsMin, boundsMax, worldMin, worldMax)) {
            continue;
        }

        float t = 0.0f;
        if (IntersectRayAabb(rayOrigin, rayDirection, worldMin, worldMax, t)) {
            const std::string meshType = objects_[i].meshFilter.meshType;
            if (meshType == "Plane") {
                if (t < bestPlaneT) {
                    bestPlaneT = t;
                    bestPlaneIndex = i;
                }
            } else if (t < bestT) {
                bestT = t;
                bestIndex = i;
            }
        }
    }

    if (bestIndex < 0 && bestPlaneIndex >= 0) {
        bestIndex = bestPlaneIndex;
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
    ImGuiIO& io = ImGui::GetIO();
    const bool mouseInViewport = ContainsSceneViewportPoint(io.MousePos.x, io.MousePos.y);

    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        activeGizmoAxis_ = -1;
        gizmoDragSelectionIndices_.clear();
        gizmoDragStartLocalTransforms_.clear();
        gizmoDragStartWorldMatrices_.clear();
        TrySelectObjectAtMouse(renderer);
        return;
    }

    const SceneObject& selectedObject = objects_[selectedIndex_];
    if (!IsObjectEffectivelyEnabled(selectedIndex_)) {
        activeGizmoAxis_ = -1;
        gizmoDragSelectionIndices_.clear();
        gizmoDragStartLocalTransforms_.clear();
        gizmoDragStartWorldMatrices_.clear();
        TrySelectObjectAtMouse(renderer);
        return;
    }
    const glm::vec2 mouse{io.MousePos.x, io.MousePos.y};
    const glm::vec3 origin = GetObjectWorldPosition(selectedIndex_);
    const glm::mat4 gizmoRotation = BuildRotationMatrix(selectedObject);
    constexpr float axisLength = 1.0f;
    constexpr float hitDistancePixels = 10.0f;

    float bestDistance = hitDistancePixels;
    for (int axis = 0; axis < 3; ++axis) {
        float distance = bestDistance;
        if (gizmoMode_ == GizmoMode::Rotate) {
            distance = DistanceToProjectedRing(mouse, origin, gizmoRotation, axis, axisLength, renderer);
        } else {
            glm::vec2 startScreen;
            glm::vec2 endScreen;
            const glm::vec3 end = origin + (gizmoMode_ == GizmoMode::Rotate ? TransformDirection(gizmoRotation, GizmoAxisVector(axis)) : GizmoAxisVector(axis)) * axisLength;
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
        if (!io.WantTextInput && mouseInViewport && !io.MouseDown[1] && io.MouseClicked[0] && hoveredGizmoAxis_ >= 0) {
            PushUndoState();
            activeGizmoAxis_ = hoveredGizmoAxis_;
            gizmoDragStartMouse_ = mouse;
            gizmoDragStartPosition_ = origin;
            gizmoDragStartRotation_ = objects_[selectedIndex_].transform.rotationEuler;
            gizmoDragStartScale_ = objects_[selectedIndex_].transform.scale;
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
            gizmoDragStartLocalTransforms_.clear();
            gizmoDragStartWorldMatrices_.clear();
            gizmoDragStartLocalTransforms_.reserve(gizmoDragSelectionIndices_.size());
            gizmoDragStartWorldMatrices_.reserve(gizmoDragSelectionIndices_.size());
            for (int index : gizmoDragSelectionIndices_) {
                gizmoDragStartLocalTransforms_.push_back(objects_[index].transform);
                gizmoDragStartWorldMatrices_.push_back(GetObjectWorldMatrix(index));
            }
            gizmoDirtyDuringDrag_ = false;
        } else if (mouseInViewport) {
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
        gizmoDragSelectionIndices_.clear();
        gizmoDragStartLocalTransforms_.clear();
        gizmoDragStartWorldMatrices_.clear();
        return;
    }

    glm::vec2 startScreen;
    glm::vec2 endScreen;
    const glm::vec3 eulerAxisVector = GizmoAxisVector(activeGizmoAxis_);
    const glm::vec3 axisVector = (gizmoMode_ == GizmoMode::Rotate) ? TransformDirection(gizmoRotation, eulerAxisVector) : eulerAxisVector;

    if (gizmoMode_ == GizmoMode::Rotate) {
        glm::vec2 centerScreen;
        if (!ProjectWorldToScreen(gizmoDragStartPosition_, renderer, centerScreen)) {
            return;
        }

        const glm::vec2 startDelta = gizmoDragStartMouse_ - centerScreen;
        const glm::vec2 currentDelta = mouse - centerScreen;
        if (glm::length(startDelta) < 0.001f || glm::length(currentDelta) < 0.001f) {
            return;
        }

        float angleDelta = std::atan2(currentDelta.y, currentDelta.x) - std::atan2(startDelta.y, startDelta.x);
        constexpr float pi = 3.14159265359f;
        if (angleDelta > pi) {
            angleDelta -= pi * 2.0f;
        } else if (angleDelta < -pi) {
            angleDelta += pi * 2.0f;
        }

        const float direction = (activeGizmoAxis_ == 1 || activeGizmoAxis_ == 2) ? -1.0f : 1.0f;
        const float worldAngleDegrees = glm::degrees(angleDelta) * direction;
        const glm::mat4 rotationDelta = glm::rotate(glm::mat4(1.0f), glm::radians(worldAngleDegrees), axisVector);
        const glm::mat4 pivotToWorld = glm::translate(glm::mat4(1.0f), gizmoDragStartPosition_);
        const glm::mat4 worldToPivot = glm::translate(glm::mat4(1.0f), -gizmoDragStartPosition_);

        for (std::size_t i = 0; i < gizmoDragSelectionIndices_.size(); ++i) {
            const int index = gizmoDragSelectionIndices_[i];
            if (index < 0 || index >= static_cast<int>(objects_.size())) {
                continue;
            }

            const glm::mat4 rotatedWorld = pivotToWorld * rotationDelta * worldToPivot * gizmoDragStartWorldMatrices_[i];
            const int parentIndex = FindObjectIndexById(objects_[index].parentId);
            const glm::mat4 localMatrix = parentIndex >= 0
                ? glm::inverse(GetObjectWorldMatrix(parentIndex)) * rotatedWorld
                : rotatedWorld;
            objects_[index].transform = TransformFromMatrix(localMatrix);
        }
    } else {
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
        if (gizmoMode_ == GizmoMode::Scale) {
            for (std::size_t i = 0; i < gizmoDragSelectionIndices_.size(); ++i) {
                const int index = gizmoDragSelectionIndices_[i];
                if (index < 0 || index >= static_cast<int>(objects_.size())) {
                    continue;
                }

                const glm::vec3 scaled = gizmoDragStartLocalTransforms_[i].scale + axisVector * worldDelta;
                objects_[index].transform.scale = {
                    (std::max)(scaled.x, 0.01f),
                    (std::max)(scaled.y, 0.01f),
                    (std::max)(scaled.z, 0.01f)
                };
            }
        } else {
            const glm::vec3 worldOffset = axisVector * worldDelta;
            for (std::size_t i = 0; i < gizmoDragSelectionIndices_.size(); ++i) {
                const int index = gizmoDragSelectionIndices_[i];
                if (index < 0 || index >= static_cast<int>(objects_.size())) {
                    continue;
                }

                const glm::vec3 startWorldPosition = glm::vec3(gizmoDragStartWorldMatrices_[i] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                const glm::vec3 targetWorldPosition = startWorldPosition + worldOffset;
                const int parentIndex = FindObjectIndexById(objects_[index].parentId);
                if (parentIndex >= 0) {
                    objects_[index].transform.position = glm::vec3(glm::inverse(GetObjectWorldMatrix(parentIndex)) * glm::vec4(targetWorldPosition, 1.0f));
                } else {
                    objects_[index].transform.position = targetWorldPosition;
                }
            }
        }
    }
    gizmoDirtyDuringDrag_ = true;
}

void SceneEditor::SubmitGizmo(Renderer& renderer) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }

    const SceneObject& object = objects_[selectedIndex_];
    if (!IsObjectEffectivelyEnabled(selectedIndex_)) {
        return;
    }
    const glm::vec3 origin = GetObjectWorldPosition(selectedIndex_);
    const glm::mat4 gizmoRotation = BuildRotationMatrix(object);
    constexpr float axisLength = 1.0f;
    constexpr float arrowHeadLength = 0.35f;
    constexpr float arrowHeadWidth = 0.14f;

    for (int axis = 0; axis < 3; ++axis) {
        const bool highlighted = (axis == hoveredGizmoAxis_ || axis == activeGizmoAxis_);
        const glm::vec3 axisVector = GizmoAxisVector(axis);
        const glm::vec3 visualAxisVector = (gizmoMode_ == GizmoMode::Rotate) ? TransformDirection(gizmoRotation, axisVector) : axisVector;
        const glm::vec4 color = GizmoAxisColor(axis, highlighted);
        const float width = highlighted ? 4.0f : 3.0f;
        const glm::vec3 end = origin + visualAxisVector * axisLength;

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
                const glm::vec3 current = origin + TransformDirection(gizmoRotation, local);
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

        const glm::vec3 arrowBase = end - visualAxisVector * arrowHeadLength;
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

    const glm::vec4 colliderColor{0.1f, 0.9f, 0.35f, 1.0f};
    constexpr float colliderWidth = 2.0f;
    constexpr DebugLineDepthMode helperDepthMode = DebugLineDepthMode::DepthTestedOverlay;
    const glm::mat4 objectMatrix = GetObjectWorldMatrix(selectedIndex_);
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
            glm::vec4{0.45f, 0.8f, 1.0f, 1.0f},
            colliderWidth,
            helperDepthMode);
    }
    if (colliderType == SceneColliderType::Mesh && object.meshCollider.enabled) {
        ImportedCollisionMesh mesh;
        if (TryGetCollisionMeshForObject(object, mesh)) {
            SubmitWireMesh(
                renderer,
                objectMatrix,
                mesh.vertices,
                mesh.indices,
                glm::vec4{0.95f, 0.55f, 0.2f, 1.0f},
                colliderWidth,
                helperDepthMode);
        } else {
            glm::vec3 boundsMin{0.0f};
            glm::vec3 boundsMax{0.0f};
            if (GetObjectLocalBounds(object, boundsMin, boundsMax)) {
                const glm::vec3 size = boundsMax - boundsMin;
                const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                SubmitWireBox(renderer, objectMatrix, center, size, glm::vec4{0.95f, 0.55f, 0.2f, 1.0f}, colliderWidth, helperDepthMode);
            }
        }
    }
    if (object.hasCharacterController && object.characterController.enabled) {
        const float radius = (std::max)(0.001f, object.characterController.radius);
        const float height = (std::max)(radius * 2.0f, object.characterController.height);
        const glm::mat4 controllerMatrix = BuildObjectMatrixNoScale(object);
        const glm::vec4 controllerColor{0.2f, 0.75f, 1.0f, 1.0f};
        const glm::vec3 controllerCenter = object.characterController.center + glm::vec3{0.0f, height * 0.5f, 0.0f};
        SubmitWireCapsuleY(renderer, controllerMatrix, controllerCenter, radius, height, controllerColor, colliderWidth, helperDepthMode);
    }
    if (object.hasCamera && object.camera.enabled) {
        SubmitCameraFrustum(renderer, object, objectMatrix, glm::vec4{1.0f, 0.85f, 0.2f, 1.0f}, 2.0f, helperDepthMode);
    }
    if (object.hasLight && object.light.enabled) {
        SubmitLightIcon(renderer, object, objectMatrix, helperDepthMode);
    }
}

} // namespace raceman


#include "PhysicsWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/MotionQuality.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "../ui/ObjImport.h"

namespace JoltAssertBridge {
using AssertFailedFunction = bool (*)(const char*, const char*, const char*, JPH::uint);
}

namespace JPH {
extern JoltAssertBridge::AssertFailedFunction AssertFailed;
}

namespace raceman {

namespace {

void JoltTrace(const char* format, ...) {
    std::fprintf(stdout, "[Jolt] ");
    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);
    std::fprintf(stdout, "\n");
    std::fflush(stdout);
}

bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line) {
    std::fprintf(stdout,
                 "[JoltAssert] %s%s%s (%s:%u)\n",
                 expression != nullptr ? expression : "<no expression>",
                 message != nullptr ? " - " : "",
                 message != nullptr ? message : "",
                 file != nullptr ? file : "<unknown>",
                 static_cast<unsigned int>(line));
    std::fflush(stdout);
    return false;
}

struct CollisionMeshCacheEntry {
    std::shared_ptr<::Model> model;
    ImportedCollisionMesh collisionMesh;
    std::uint64_t triangleCount{0};
    bool valid{false};
};

struct ShapeCacheEntry {
    JPH::ShapeRefC shape;
    std::uint64_t triangleCount{0};
    bool valid{false};
};

std::unordered_map<std::string, CollisionMeshCacheEntry>& GetCollisionMeshCache() {
    static std::unordered_map<std::string, CollisionMeshCacheEntry> cache;
    return cache;
}

std::unordered_map<std::string, ShapeCacheEntry>& GetShapeCache() {
    static std::unordered_map<std::string, ShapeCacheEntry> cache;
    return cache;
}

std::filesystem::path FindProjectAssetAbsolutePath(const std::string& assetPath);

std::string BuildMeshCacheKey(const std::string& assetPath, int meshIndex) {
    return assetPath + "#" + std::to_string(meshIndex);
}

std::string BuildShapeCacheKey(const PhysicsColliderDesc& collider) {
    std::string key = BuildMeshCacheKey(collider.meshAssetPath, collider.meshIndex) + "#" +
                      std::to_string(static_cast<int>(collider.meshMode)) + "#" +
                      std::to_string(static_cast<int>(collider.meshBuildQuality));
    const glm::vec3& o = collider.meshPivotOffset;
    if (o.x != 0.0f || o.y != 0.0f || o.z != 0.0f) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "@%.4f_%.4f_%.4f", o.x, o.y, o.z);
        key += buf;
    }
    return key;
}

// Thread-local pointer to the active build progress reporter.
// Set by the progress-aware Build overload so free functions (CreateMeshShape)
// can report without needing an extra parameter through every call site.
static thread_local PhysicsBuildProgress* s_activeBuildProgress = nullptr;

// --- Disk-based shape cache ---

std::uint64_t FnvHash64(const std::string& s) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : s) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::filesystem::path GetShapeCacheDir() {
    return (std::filesystem::current_path() / "Project" / ".collision-cache").lexically_normal();
}

// Bump this whenever the cooking logic changes so old cached shapes are not loaded.
// v2: added degenerate-triangle filtering (zero-area / NaN vertex rejection).
static constexpr int kShapeCacheVersion = 2;

// Cache filename encodes both the key and the source file mtime so it
// auto-invalidates whenever the source mesh is modified.
std::filesystem::path BuildDiskCachePath(const std::string& cacheKey,
                                         const std::filesystem::file_time_type& mtime) {
    const auto mtimeCount = mtime.time_since_epoch().count();
    const std::string filename =
        "v" + std::to_string(kShapeCacheVersion) + "_" +
        std::to_string(FnvHash64(cacheKey)) + "_" + std::to_string(mtimeCount) + ".joltshape";
    return GetShapeCacheDir() / filename;
}

std::filesystem::path BuildDiskCacheMetaPath(const std::filesystem::path& shapePath) {
    std::filesystem::path metaPath = shapePath;
    metaPath += ".meta";
    return metaPath;
}

std::string DiskCachePrefixForKey(const std::string& cacheKey) {
    return "v" + std::to_string(kShapeCacheVersion) + "_" + std::to_string(FnvHash64(cacheKey)) + "_";
}

void PruneStaleDiskCacheEntries(const std::string& cacheKey, const std::filesystem::path& keepPath) {
    const std::filesystem::path cacheDir = GetShapeCacheDir();
    if (!std::filesystem::exists(cacheDir)) {
        return;
    }

    const std::string prefix = DiskCachePrefixForKey(cacheKey);
    const std::filesystem::path normalizedKeep = keepPath.lexically_normal();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path path = entry.path().lexically_normal();
        const std::string filename = path.filename().string();
        const bool isShape = path.extension() == ".joltshape";
        const bool isMeta = filename.size() > 5 && filename.substr(filename.size() - 5) == ".meta";
        if (!isShape && !isMeta) {
            continue;
        }
        const std::string baseFilename = isMeta
            ? filename.substr(0, filename.size() - 5)
            : filename;
        if (baseFilename.rfind(prefix, 0) != 0) {
            continue;
        }
        if (path == normalizedKeep || path == BuildDiskCacheMetaPath(normalizedKeep).lexically_normal()) {
            continue;
        }
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
    }
}

std::uint64_t TryLoadShapeTriangleCountFromDisk(const std::filesystem::path& shapePath) {
    std::ifstream f(BuildDiskCacheMetaPath(shapePath));
    std::uint64_t triangleCount = 0;
    if (f >> triangleCount) {
        return triangleCount;
    }
    return 0;
}

JPH::ShapeRefC TryLoadShapeFromDisk(const std::filesystem::path& path,
                                    std::uint64_t* outTriangleCount = nullptr) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::fprintf(stdout, "[CollisionCache] MISS  %s\n", path.string().c_str());
        std::fflush(stdout);
        return {};
    }
    JPH::StreamInWrapper stream(f);
    JPH::Shape::ShapeResult result = JPH::Shape::sRestoreFromBinaryState(stream);
    if (!result.IsValid()) {
        std::fprintf(stdout, "[CollisionCache] CORRUPT %s\n", path.string().c_str());
        std::fflush(stdout);
        return {};
    }
    std::fprintf(stdout, "[CollisionCache] HIT    %s\n", path.string().c_str());
    std::fflush(stdout);
    if (outTriangleCount) {
        *outTriangleCount = TryLoadShapeTriangleCountFromDisk(path);
    }
    return result.Get();
}

void SaveShapeToDisk(const std::filesystem::path& path,
                     const JPH::ShapeRefC& shape,
                     std::uint64_t triangleCount) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        std::fprintf(stdout, "[CollisionCache] SAVE FAILED (mkdir) %s : %s\n",
                     path.string().c_str(), ec.message().c_str());
        std::fflush(stdout);
        return;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        std::fprintf(stdout, "[CollisionCache] SAVE FAILED (open) %s\n", path.string().c_str());
        std::fflush(stdout);
        return;
    }
    JPH::StreamOutWrapper stream(f);
    shape->SaveBinaryState(stream);
    std::ofstream meta(BuildDiskCacheMetaPath(path), std::ios::trunc);
    if (meta.is_open()) {
        meta << triangleCount << '\n';
    }
    std::fprintf(stdout, "[CollisionCache] SAVED  %s\n", path.string().c_str());
    std::fflush(stdout);
}

bool GetCachedCollisionMesh(const PhysicsColliderDesc& collider,
                            ImportedCollisionMesh& outMesh,
                            std::uint64_t* outTriangleCount = nullptr) {
    if (collider.meshAssetPath.empty()) {
        return false;
    }

    const std::string cacheKey = BuildMeshCacheKey(collider.meshAssetPath, collider.meshIndex);
    CollisionMeshCacheEntry& entry = GetCollisionMeshCache()[cacheKey];
    if (!entry.valid) {
        const std::filesystem::path resolvedPath = FindProjectAssetAbsolutePath(collider.meshAssetPath);
        if (!std::filesystem::exists(resolvedPath)) {
            return false;
        }

        entry.model = raceman::LoadModelFromFile(resolvedPath.string());
        if (!entry.model) {
            return false;
        }

        ImportedCollisionMesh mesh;
        const std::size_t meshIndex = collider.meshIndex >= 0 ? static_cast<std::size_t>(collider.meshIndex) : 0;
        if (!raceman::GetCollisionMesh(entry.model, meshIndex, mesh)) {
            return false;
        }

        entry.collisionMesh = std::move(mesh);
        entry.triangleCount = entry.collisionMesh.indices.size() / 3;
        entry.valid = true;
    }

    outMesh = entry.collisionMesh;
    if (outTriangleCount) {
        *outTriangleCount = entry.triangleCount;
    }
    return true;
}

float MaxAbsComponent(const glm::vec3& value) {
    return (std::max)((std::max)(std::abs(value.x), std::abs(value.y)), std::abs(value.z));
}

namespace Layers {
static constexpr JPH::ObjectLayer NonMoving = 0;
static constexpr JPH::ObjectLayer Moving = 1;
static constexpr JPH::ObjectLayer Sensor = 2;
static constexpr JPH::ObjectLayer NumLayers = 3;
}

namespace BroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer NonMoving(0);
static constexpr JPH::BroadPhaseLayer Moving(1);
static constexpr JPH::BroadPhaseLayer Sensor(2);
static constexpr JPH::uint NumLayers = 3;
}

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerInterfaceImpl() {
        objectToBroadPhase_[Layers::NonMoving] = BroadPhaseLayers::NonMoving;
        objectToBroadPhase_[Layers::Moving] = BroadPhaseLayers::Moving;
        objectToBroadPhase_[Layers::Sensor] = BroadPhaseLayers::Sensor;
    }

    JPH::uint GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NumLayers;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return objectToBroadPhase_[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(layer)) {
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NonMoving): return "NonMoving";
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::Moving): return "Moving";
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::Sensor): return "Sensor";
        default: return "Invalid";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer objectToBroadPhase_[Layers::NumLayers];
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override {
        if (first == Layers::Sensor || second == Layers::Sensor) {
            return first != second;
        }
        if (first == Layers::NonMoving) {
            return second == Layers::Moving;
        }
        return true;
    }
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadphaseLayer) const override {
        if (layer == Layers::Sensor) {
            return broadphaseLayer != BroadPhaseLayers::Sensor;
        }
        if (layer == Layers::NonMoving) {
            return broadphaseLayer == BroadPhaseLayers::Moving;
        }
        return true;
    }
};

class ProjectLayerGroupFilter final : public JPH::GroupFilter {
public:
    explicit ProjectLayerGroupFilter(const PhysicsLayerCollisionMatrix& collisionMatrix)
        : collisionMatrix_(collisionMatrix) {}

    bool CanCollide(const JPH::CollisionGroup& first, const JPH::CollisionGroup& second) const override {
        const int firstLayer = (std::max)(0, (std::min)(kPhysicsLayerCount - 1, static_cast<int>(first.GetSubGroupID())));
        const int secondLayer = (std::max)(0, (std::min)(kPhysicsLayerCount - 1, static_cast<int>(second.GetSubGroupID())));
        return collisionMatrix_[static_cast<std::size_t>(firstLayer)][static_cast<std::size_t>(secondLayer)];
    }

private:
    PhysicsLayerCollisionMatrix collisionMatrix_{};
};

void EnsureJoltInitialized() {
    static bool initialized = false;
    if (initialized) {
        return;
    }

    JPH::Trace = JoltTrace;
    JPH::AssertFailed = JoltAssertFailed;
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    initialized = true;
}

JPH::Vec3 ToJoltVec3(const glm::vec3& value) {
    return JPH::Vec3(value.x, value.y, value.z);
}

JPH::RVec3 ToJoltRVec3(const glm::vec3& value) {
    return JPH::RVec3(value.x, value.y, value.z);
}

glm::vec3 FromJoltVec3(JPH::Vec3Arg value) {
    return {value.GetX(), value.GetY(), value.GetZ()};
}

glm::vec3 FromJoltRVec3(JPH::RVec3Arg value) {
    return {
        static_cast<float>(value.GetX()),
        static_cast<float>(value.GetY()),
        static_cast<float>(value.GetZ())
    };
}

JPH::Quat ToJoltQuat(const glm::vec3& rotationEuler) {
    const glm::quat quat = glm::quat(glm::radians(rotationEuler));
    return JPH::Quat(quat.x, quat.y, quat.z, quat.w);
}

glm::vec3 FromJoltQuat(JPH::QuatArg quat) {
    const glm::quat glmQuat(quat.GetW(), quat.GetX(), quat.GetY(), quat.GetZ());
    return glm::degrees(glm::eulerAngles(glmQuat));
}

JPH::ShapeRefC CreateCharacterShape(float height, float radius) {
    const float clampedRadius = (std::max)(0.001f, radius);
    const float clampedHeight = (std::max)(clampedRadius * 2.0f, height);
    const float halfCylinderHeight = (std::max)(0.001f, clampedHeight * 0.5f - clampedRadius);
    return new JPH::CapsuleShape(halfCylinderHeight, clampedRadius);
}

JPH::EAllowedDOFs ToAllowedDOFs(const PhysicsBodyDesc& body) {
    JPH::EAllowedDOFs allowed = JPH::EAllowedDOFs::All;
    if (body.freezePositionX) allowed &= ~JPH::EAllowedDOFs::TranslationX;
    if (body.freezePositionY) allowed &= ~JPH::EAllowedDOFs::TranslationY;
    if (body.freezePositionZ) allowed &= ~JPH::EAllowedDOFs::TranslationZ;
    if (body.freezeRotationX) allowed &= ~JPH::EAllowedDOFs::RotationX;
    if (body.freezeRotationY) allowed &= ~JPH::EAllowedDOFs::RotationY;
    if (body.freezeRotationZ) allowed &= ~JPH::EAllowedDOFs::RotationZ;
    return allowed == JPH::EAllowedDOFs::None ? JPH::EAllowedDOFs::All : allowed;
}

JPH::EMotionQuality ToMotionQuality(PhysicsMotionQuality quality) {
    return quality == PhysicsMotionQuality::Continuous
        ? JPH::EMotionQuality::LinearCast
        : JPH::EMotionQuality::Discrete;
}

JPH::MeshShapeSettings::EBuildQuality ToMeshBuildQuality(MeshColliderBuildQuality quality) {
    (void)quality;
    return JPH::MeshShapeSettings::EBuildQuality::FavorRuntimePerformance;
}

std::filesystem::path FindProjectAssetAbsolutePath(const std::string& assetPath) {
    std::filesystem::path normalized(assetPath);
    std::filesystem::path projectRoot = std::filesystem::current_path() / "Project";
#if defined(_WIN32)
    char* overrideRoot = nullptr;
    std::size_t overrideRootLength = 0;
    if (_dupenv_s(&overrideRoot, &overrideRootLength, "RACEMAN_PROJECT_ROOT") == 0 && overrideRoot != nullptr) {
        std::string value = overrideRoot;
        std::free(overrideRoot);
        if (!value.empty()) {
            projectRoot = value;
        }
    }
#else
    if (const char* overrideRoot = std::getenv("RACEMAN_PROJECT_ROOT")) {
        if (overrideRoot[0] != '\0') {
            projectRoot = overrideRoot;
        }
    }
#endif
    if (!normalized.empty() && *normalized.begin() == "assets") {
        normalized = normalized.lexically_relative("assets");
        return (projectRoot / "assets" / normalized).lexically_normal();
    }
    if (assetPath.rfind("editor-assets/", 0) == 0) {
        return (std::filesystem::current_path() / assetPath).lexically_normal();
    }
    return (std::filesystem::current_path() / assetPath).lexically_normal();
}

JPH::ShapeRefC CreateMeshShape(const PhysicsColliderDesc& collider,
                               std::uint64_t* outTriangleCount = nullptr,
                               bool allowCooking = false) {
    if (collider.meshAssetPath.empty()) {
        return {};
    }
    EnsureJoltInitialized();

    const std::string cacheKey = BuildShapeCacheKey(collider);
    ShapeCacheEntry& shapeCache = GetShapeCache()[cacheKey];
    if (shapeCache.valid && shapeCache.shape) {
        if (outTriangleCount) {
            *outTriangleCount = shapeCache.triangleCount;
        }
        return shapeCache.shape;
    }

    // Check disk cache before cooking. The cache file name includes the source
    // mesh's last-write-time, so it is automatically invalidated when the mesh
    // is modified on disk.
    const std::filesystem::path resolvedPath = FindProjectAssetAbsolutePath(collider.meshAssetPath);
    if (!std::filesystem::exists(resolvedPath)) {
        return {};
    }

    std::error_code ec;
    const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(resolvedPath, ec);
    const std::string meshFilenameStr = std::filesystem::path(collider.meshAssetPath).filename().string();
    if (!ec) {
        const std::filesystem::path diskCachePath = BuildDiskCachePath(cacheKey, mtime);
        if (s_activeBuildProgress) s_activeBuildProgress->SetTask("Cache check: " + meshFilenameStr);
        std::uint64_t cachedTriangleCount = 0;
        if (JPH::ShapeRefC loaded = TryLoadShapeFromDisk(diskCachePath, &cachedTriangleCount)) {
            if (s_activeBuildProgress) s_activeBuildProgress->SetTask("Loaded from cache: " + meshFilenameStr);
            shapeCache.shape = std::move(loaded);
            shapeCache.triangleCount = cachedTriangleCount;
            shapeCache.valid = true;
            if (outTriangleCount) {
                *outTriangleCount = cachedTriangleCount;
            }
            return shapeCache.shape;
        }
    }

    if (!allowCooking) {
        if (s_activeBuildProgress) s_activeBuildProgress->SetTask("Missing baked cache: " + meshFilenameStr);
        std::fprintf(stdout,
                     "[CollisionCache] MISSING baked mesh collider cache for %s. Add or rebake the Mesh Collider in the editor.\n",
                     collider.meshAssetPath.c_str());
        std::fflush(stdout);
        return {};
    }

    if (s_activeBuildProgress) s_activeBuildProgress->SetTask("Cooking: " + meshFilenameStr);

    ImportedCollisionMesh collisionMesh;
    std::uint64_t triangleCount = 0;
    if (!GetCachedCollisionMesh(collider, collisionMesh, &triangleCount)) {
        return {};
    }

    const glm::vec3 pivotOffset = collider.meshPivotOffset;
    const bool hasPivot = (pivotOffset.x != 0.0f || pivotOffset.y != 0.0f || pivotOffset.z != 0.0f);

    if (collider.meshMode == MeshColliderMode::ConvexHull) {
        constexpr std::size_t maxPoints = 256;
        const std::size_t totalPoints = collisionMesh.vertices.size();
        if (totalPoints == 0) {
            return {};
        }

        JPH::Array<JPH::Vec3> points;
        points.reserve((std::min)(totalPoints, maxPoints));
        if (totalPoints <= maxPoints) {
            for (const glm::vec3& vertex : collisionMesh.vertices) {
                points.push_back(ToJoltVec3(hasPivot ? (vertex - pivotOffset) : vertex));
            }
        } else {
            const float stride = static_cast<float>(totalPoints) / static_cast<float>(maxPoints);
            for (std::size_t i = 0; i < maxPoints; ++i) {
                const std::size_t index = (std::min)(totalPoints - 1, static_cast<std::size_t>(i * stride));
                const glm::vec3& v = collisionMesh.vertices[index];
                points.push_back(ToJoltVec3(hasPivot ? (v - pivotOffset) : v));
            }
        }

        JPH::ConvexHullShapeSettings settings(points);
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (!result.IsValid()) {
            return {};
        }

        shapeCache.shape = result.Get();
        shapeCache.triangleCount = triangleCount;
        shapeCache.valid = true;
        if (!ec) {
            const std::filesystem::path diskCachePath = BuildDiskCachePath(cacheKey, mtime);
            PruneStaleDiskCacheEntries(cacheKey, diskCachePath);
            SaveShapeToDisk(diskCachePath, shapeCache.shape, triangleCount);
        }
        if (outTriangleCount) {
            *outTriangleCount = triangleCount;
        }
        return shapeCache.shape;
    }

    JPH::VertexList vertices;
    vertices.reserve(collisionMesh.vertices.size());
    for (const glm::vec3& vertex : collisionMesh.vertices) {
        const glm::vec3 v = hasPivot ? (vertex - pivotOffset) : vertex;
        // Skip NaN/Inf vertices — replace with zero so indices still align;
        // any triangle referencing them will be culled below.
        const float vx = std::isfinite(v.x) ? v.x : 0.0f;
        const float vy = std::isfinite(v.y) ? v.y : 0.0f;
        const float vz = std::isfinite(v.z) ? v.z : 0.0f;
        vertices.push_back(JPH::Float3(vx, vy, vz));
    }

    // Minimum squared edge length and area threshold to reject degenerate triangles.
    // Jolt's EPA/GJK can crash or loop infinitely on zero-area faces.
    constexpr float kMinEdgeLenSq = 1e-8f;   // ~0.0001 m minimum edge
    constexpr float kMinAreaSq    = 1e-10f;  // discard near-zero area triangles

    const std::size_t vertexCount = vertices.size();
    JPH::IndexedTriangleList triangles;
    triangles.reserve(collisionMesh.indices.size() / 3);
    for (std::size_t i = 0; i + 2 < collisionMesh.indices.size(); i += 3) {
        const std::uint32_t i0 = collisionMesh.indices[i];
        const std::uint32_t i1 = collisionMesh.indices[i + 1];
        const std::uint32_t i2 = collisionMesh.indices[i + 2];

        // Reject out-of-range indices.
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;

        // Reject triangles with duplicate indices (guaranteed zero area).
        if (i0 == i1 || i1 == i2 || i0 == i2) continue;

        const JPH::Float3& f0 = vertices[i0];
        const JPH::Float3& f1 = vertices[i1];
        const JPH::Float3& f2 = vertices[i2];

        // Reject triangles whose vertices landed on a NaN-collapsed zero vertex
        // (all three at origin) — check via edge lengths.
        const float e0x = f1.x - f0.x, e0y = f1.y - f0.y, e0z = f1.z - f0.z;
        const float e1x = f2.x - f0.x, e1y = f2.y - f0.y, e1z = f2.z - f0.z;
        if (e0x*e0x + e0y*e0y + e0z*e0z < kMinEdgeLenSq) continue;
        if (e1x*e1x + e1y*e1y + e1z*e1z < kMinEdgeLenSq) continue;

        // Cross product magnitude squared = (2 * triangle area)^2
        const float cx = e0y*e1z - e0z*e1y;
        const float cy = e0z*e1x - e0x*e1z;
        const float cz = e0x*e1y - e0y*e1x;
        if (cx*cx + cy*cy + cz*cz < kMinAreaSq) continue;

        triangles.emplace_back(i0, i1, i2, 0);
    }

    if (triangles.empty()) {
        // Every triangle was degenerate — can't build a mesh shape.
        return {};
    }

    JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
    settings.mBuildQuality = ToMeshBuildQuality(collider.meshBuildQuality);
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (!result.IsValid()) {
        return {};
    }

    shapeCache.shape = result.Get();
    shapeCache.triangleCount = triangleCount;
    shapeCache.valid = true;
    if (!ec) {
        const std::filesystem::path diskCachePath = BuildDiskCachePath(cacheKey, mtime);
        PruneStaleDiskCacheEntries(cacheKey, diskCachePath);
        SaveShapeToDisk(diskCachePath, shapeCache.shape, triangleCount);
    }
    if (outTriangleCount) {
        *outTriangleCount = triangleCount;
    }
    return shapeCache.shape;
}

JPH::ShapeRefC CreateBaseShape(const PhysicsColliderDesc& collider, const glm::vec3& scale) {
    if (collider.type == PhysicsColliderType::Mesh) {
        JPH::ShapeRefC shape = CreateMeshShape(collider, nullptr, true);
        if (!shape) {
            return {};
        }
        if (glm::length2(scale - glm::vec3(1.0f)) <= 0.00000001f) {
            return shape;
        }
        JPH::ScaledShapeSettings scaledSettings(shape, ToJoltVec3(scale));
        JPH::ShapeSettings::ShapeResult result = scaledSettings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC{};
    }
    if (collider.type == PhysicsColliderType::Box) {
        const glm::vec3 halfExtent = (glm::max)(glm::abs(collider.size * scale) * 0.5f, glm::vec3(0.001f));
        return new JPH::BoxShape(ToJoltVec3(halfExtent));
    }
    if (collider.type == PhysicsColliderType::Sphere) {
        return new JPH::SphereShape((std::max)(0.001f, collider.radius * MaxAbsComponent(scale)));
    }
    if (collider.type == PhysicsColliderType::Plane) {
        glm::vec3 normal = collider.normal;
        if (glm::length2(normal) <= 0.000001f) {
            normal = {0.0f, 1.0f, 0.0f};
        } else {
            normal = glm::normalize(normal);
        }
        const JPH::Plane plane(ToJoltVec3(normal), -collider.offset);
        const float halfExtent = collider.infinite
            ? JPH::PlaneShapeSettings::cDefaultHalfExtent
            : (std::max)(0.001f, collider.halfExtent);
        return new JPH::PlaneShape(plane, nullptr, halfExtent);
    }

    const float radius = (std::max)(0.001f, collider.radius * (std::max)(std::abs(scale.x), std::abs(scale.z)));
    const float scaledHeight = (std::max)(radius * 2.0f, collider.height * std::abs(scale.y));
    const float halfCylinderHeight = (std::max)(0.001f, (scaledHeight * 0.5f) - radius);
    return new JPH::CapsuleShape(halfCylinderHeight, radius);
}

JPH::ShapeRefC CreateShape(const PhysicsBodyDesc& body, bool sensorOnly) {
    std::vector<JPH::ShapeRefC> shapes;
    for (const PhysicsColliderDesc& collider : body.colliders) {
        if (sensorOnly != collider.isTrigger) {
            continue;
        }
        const glm::vec3 combinedScale = body.scale * collider.scale;
        JPH::ShapeRefC shape = CreateBaseShape(collider, combinedScale);
        if (!shape) {
            continue;
        }

        const glm::vec3 scaledCenter = collider.center * body.scale;
        const JPH::Quat localRotation = ToJoltQuat(collider.rotationEuler);
        if (glm::length2(scaledCenter) > 0.000001f || std::abs(localRotation.GetW() - 1.0f) > 0.000001f || localRotation.GetX() != 0.0f || localRotation.GetY() != 0.0f || localRotation.GetZ() != 0.0f) {
            JPH::RotatedTranslatedShapeSettings offsetSettings(ToJoltVec3(scaledCenter), localRotation, shape);
            JPH::ShapeSettings::ShapeResult result = offsetSettings.Create();
            if (!result.IsValid()) {
                continue;
            }
            shape = result.Get();
        }
        shapes.push_back(shape);
    }

    if (shapes.empty()) {
        return {};
    }
    if (shapes.size() == 1) {
        return shapes[0];
    }

    JPH::StaticCompoundShapeSettings compoundSettings;
    for (const JPH::ShapeRefC& shape : shapes) {
        compoundSettings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), shape);
    }
    JPH::ShapeSettings::ShapeResult result = compoundSettings.Create();
    return result.IsValid() ? result.Get() : JPH::ShapeRefC{};
}

} // namespace

std::string PhysicsWorld::GetCollisionShapeCacheDirectory() {
    return GetShapeCacheDir().string();
}

int PhysicsWorld::ClearCollisionShapeCache(std::string* outError) {
    GetShapeCache().clear();
    GetCollisionMeshCache().clear();

    const std::filesystem::path cacheDir = GetShapeCacheDir();
    if (!std::filesystem::exists(cacheDir)) {
        return 0;
    }

    int removedCount = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
        if (ec) {
            if (outError) {
                *outError = ec.message();
            }
            return removedCount;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path path = entry.path();
        const std::string filename = path.filename().string();
        const bool isShape = path.extension() == ".joltshape";
        const bool isMeta = filename.size() > 5 && filename.substr(filename.size() - 5) == ".meta";
        if (!isShape && !isMeta) {
            continue;
        }
        std::error_code removeError;
        if (std::filesystem::remove(path, removeError)) {
            ++removedCount;
        } else if (removeError && outError && outError->empty()) {
            *outError = removeError.message();
        }
    }
    return removedCount;
}

CollisionShapeCacheInfo PhysicsWorld::GetCollisionShapeCacheInfo(const PhysicsColliderDesc& collider) {
    CollisionShapeCacheInfo info;
    if (collider.type != PhysicsColliderType::Mesh || collider.meshAssetPath.empty()) {
        info.status = CollisionShapeCacheStatus::Missing;
        info.message = "Mesh collider source is empty.";
        return info;
    }

    const std::filesystem::path resolvedPath = FindProjectAssetAbsolutePath(collider.meshAssetPath);
    if (!std::filesystem::exists(resolvedPath)) {
        info.status = CollisionShapeCacheStatus::Failed;
        info.message = "Source mesh not found.";
        return info;
    }

    std::error_code ec;
    const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(resolvedPath, ec);
    if (ec) {
        info.status = CollisionShapeCacheStatus::Failed;
        info.message = ec.message();
        return info;
    }

    const std::string cacheKey = BuildShapeCacheKey(collider);
    const std::filesystem::path diskCachePath = BuildDiskCachePath(cacheKey, mtime);
    info.cachePath = diskCachePath.string();
    info.triangleCount = TryLoadShapeTriangleCountFromDisk(diskCachePath);

    ShapeCacheEntry& memoryEntry = GetShapeCache()[cacheKey];
    if (memoryEntry.valid && memoryEntry.shape) {
        info.status = CollisionShapeCacheStatus::Ready;
        info.triangleCount = memoryEntry.triangleCount;
        info.message = "Ready in memory.";
        return info;
    }

    if (std::filesystem::exists(diskCachePath)) {
        info.status = CollisionShapeCacheStatus::Ready;
        info.message = "Ready on disk.";
        return info;
    }

    const std::string prefix = DiskCachePrefixForKey(cacheKey);
    const std::filesystem::path cacheDir = GetShapeCacheDir();
    if (std::filesystem::exists(cacheDir)) {
        std::error_code dirError;
        for (const auto& entry : std::filesystem::directory_iterator(cacheDir, dirError)) {
            if (dirError || !entry.is_regular_file()) {
                continue;
            }
            const std::string filename = entry.path().filename().string();
            if (filename.rfind(prefix, 0) == 0) {
                info.status = CollisionShapeCacheStatus::Stale;
                info.message = "Source mesh changed after bake.";
                return info;
            }
        }
    }

    info.status = CollisionShapeCacheStatus::Missing;
    info.message = "No baked collision shape.";
    return info;
}

bool PhysicsWorld::BakeCollisionShape(const PhysicsColliderDesc& collider, CollisionShapeCacheInfo* outInfo) {
    if (collider.type != PhysicsColliderType::Mesh || collider.meshAssetPath.empty()) {
        if (outInfo) {
            *outInfo = GetCollisionShapeCacheInfo(collider);
        }
        return false;
    }

    std::uint64_t triangleCount = 0;
    const bool baked = static_cast<bool>(CreateMeshShape(collider, &triangleCount, true));
    if (outInfo) {
        *outInfo = GetCollisionShapeCacheInfo(collider);
        if (baked && outInfo->triangleCount == 0) {
            outInfo->triangleCount = triangleCount;
        }
        if (!baked && outInfo->status != CollisionShapeCacheStatus::Failed) {
            outInfo->status = CollisionShapeCacheStatus::Failed;
            outInfo->message = "Failed to cook collision shape.";
        }
    }
    return baked;
}

class PhysicsWorld::Impl {
public:
    explicit Impl(const PhysicsLayerCollisionMatrix& collisionMatrix)
        : collisionMatrix_(collisionMatrix) {}

    struct CharacterRecord {
        PhysicsCharacterDesc desc;
        PhysicsCharacterState state;
        JPH::Ref<JPH::CharacterVirtual> character;
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        glm::vec3 desiredVelocity{0.0f};
        float pendingJumpImpulse{0.0f};
    };

    void Build(const std::vector<PhysicsBodyDesc>& inputBodies) {
        Build(inputBodies, {}, nullptr);
    }

    void Build(const std::vector<PhysicsBodyDesc>& inputBodies, const std::vector<PhysicsCharacterDesc>& inputCharacters) {
        Build(inputBodies, inputCharacters, nullptr);
    }

    void Build(const std::vector<PhysicsBodyDesc>& inputBodies, const std::vector<PhysicsCharacterDesc>& inputCharacters,
               PhysicsBuildProgress* progress) {
        s_activeBuildProgress = progress;
        if (progress) {
            progress->stepsDone.store(0);
            progress->stepsTotal.store(static_cast<int>(inputBodies.size()));
            progress->SetTask("Initializing...");
        }
        const auto buildStart = std::chrono::steady_clock::now();
        std::fprintf(stdout, "[Physics] Clear previous world...\n");
        std::fflush(stdout);
        Clear();
        std::fprintf(stdout, "[Physics] Initialize Jolt...\n");
        std::fflush(stdout);
        EnsureJoltInitialized();
        std::fprintf(stdout, "[Physics] Jolt initialized.\n");
        std::fflush(stdout);
        stats_ = {};

        bodies_ = inputBodies;
        stats_.bodyCount = static_cast<std::uint32_t>(inputBodies.size());
        stats_.characterCount = static_cast<std::uint32_t>(inputCharacters.size());
        for (const PhysicsBodyDesc& body : bodies_) {
            states_[body.objectId] = {body.objectId, body.position, body.rotationEuler, body.velocity, body.angularVelocity};
            for (const PhysicsColliderDesc& collider : body.colliders) {
                switch (collider.type) {
                case PhysicsColliderType::Box: ++stats_.boxColliderCount; break;
                case PhysicsColliderType::Sphere: ++stats_.sphereColliderCount; break;
                case PhysicsColliderType::Capsule: ++stats_.capsuleColliderCount; break;
                case PhysicsColliderType::Plane: ++stats_.planeColliderCount; break;
                case PhysicsColliderType::Mesh:
                    ++stats_.meshColliderCount;
                    if (collider.meshMode == MeshColliderMode::ConvexHull) {
                        ++stats_.convexHullColliderCount;
                    } else {
                        ++stats_.triangleMeshColliderCount;
                    }
                    break;
                }
            }
        }

        std::fprintf(stdout, "[Physics] Create allocator/system for %zu bodies...\n", bodies_.size());
        std::fflush(stdout);
        tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
        std::fprintf(stdout, "[Physics] Temp allocator created.\n");
        std::fflush(stdout);
        const int workerCount = (std::max)(1u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u);
        std::fprintf(stdout, "[Physics] Creating job system with %d workers...\n", workerCount);
        std::fflush(stdout);
        jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerCount);
        std::fprintf(stdout, "[Physics] Job system created.\n");
        std::fflush(stdout);
        physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
        std::fprintf(stdout, "[Physics] Physics system allocated.\n");
        std::fflush(stdout);
        const JPH::uint maxBodies = static_cast<JPH::uint>((std::max<std::size_t>)(1024, bodies_.size() * 4 + inputCharacters.size() * 2 + 64));
        const JPH::uint maxBodyPairs = static_cast<JPH::uint>((std::max<std::size_t>)(2048, bodies_.size() * 16 + 1024));
        const JPH::uint maxContactConstraints = static_cast<JPH::uint>((std::max<std::size_t>)(1024, bodies_.size() * 8 + 512));
        std::fprintf(stdout,
                     "[Physics] Init capacity: %u bodies, %u pairs, %u contacts.\n",
                     maxBodies,
                     maxBodyPairs,
                     maxContactConstraints);
        std::fflush(stdout);
        physicsSystem_->Init(maxBodies, 0, maxBodyPairs, maxContactConstraints, broadPhaseLayerInterface_, objectVsBroadPhaseLayerFilter_, objectLayerPairFilter_);
        std::fprintf(stdout, "[Physics] Physics system initialized.\n");
        std::fflush(stdout);
        collisionGroupFilter_ = new ProjectLayerGroupFilter(collisionMatrix_);

        JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
        int bodyProgressIndex = 0;
            for (const PhysicsBodyDesc& body : bodies_) {
            std::fprintf(stdout, "[Physics] Body %d/%zu: %s\n",
                         bodyProgressIndex + 1,
                         bodies_.size(),
                         body.objectId.c_str());
            std::fflush(stdout);
            if (progress) {
                if (progress->cancelRequested.load()) {
                    progress->wasCancelled.store(true);
                    progress->isDone.store(true);
                    s_activeBuildProgress = nullptr;
                    return;
                }
                progress->stepsDone.store(bodyProgressIndex);
                progress->SetTask("Building: " + body.objectId);
            }
            ++bodyProgressIndex;
            const bool hasStaticOnlyCollider = std::any_of(body.colliders.begin(), body.colliders.end(), [](const PhysicsColliderDesc& collider) {
                if (collider.type == PhysicsColliderType::Plane) {
                    return true;
                }
                if (collider.type == PhysicsColliderType::Mesh) {
                    return collider.meshMode == MeshColliderMode::TriangleMesh;
                }
                return false;
            });
            const bool hasSolidCollider = std::any_of(body.colliders.begin(), body.colliders.end(), [](const PhysicsColliderDesc& collider) {
                return !collider.isTrigger;
            });
            const bool sensorOnly = !hasSolidCollider;
            JPH::ShapeRefC shape = CreateShape(body, sensorOnly);
            if (!shape) {
                std::fprintf(stdout, "[Physics] Skipped body without valid shape: %s\n", body.objectId.c_str());
                std::fflush(stdout);
                continue;
            }

            if (shape && body.overrideCenterOfMass && glm::length2(body.centerOfMassOffset) > 0.000001f) {
                JPH::OffsetCenterOfMassShapeSettings offsetSettings(ToJoltVec3(body.centerOfMassOffset), shape);
                JPH::ShapeSettings::ShapeResult offsetResult = offsetSettings.Create();
                if (offsetResult.IsValid()) {
                    shape = offsetResult.Get();
                }
            }

            const bool movable = body.bodyType != PhysicsBodyType::Static && !hasStaticOnlyCollider;
            const bool dynamic = body.bodyType == PhysicsBodyType::Dynamic && !hasStaticOnlyCollider;
            const JPH::EMotionType motionType = dynamic
                ? JPH::EMotionType::Dynamic
                : (movable ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static);
            const JPH::ObjectLayer layer = sensorOnly ? Layers::Sensor : (movable ? Layers::Moving : Layers::NonMoving);
            JPH::BodyCreationSettings settings(shape, ToJoltRVec3(body.position), ToJoltQuat(body.rotationEuler), motionType, layer);
            settings.mIsSensor = sensorOnly;
            settings.mGravityFactor = body.useGravity ? 1.0f : 0.0f;
            settings.mLinearDamping = (std::max)(0.0f, body.linearDamping);
            settings.mAngularDamping = (std::max)(0.0f, body.angularDamping);
            settings.mFriction = (std::max)(0.0f, body.friction);
            settings.mRestitution = (std::max)(0.0f, body.restitution);
            settings.mMotionQuality = ToMotionQuality(body.motionQuality);
            settings.mAllowedDOFs = ToAllowedDOFs(body);
            settings.mLinearVelocity = ToJoltVec3(body.velocity);
            settings.mAngularVelocity = ToJoltVec3(body.angularVelocity);
            settings.mCollisionGroup = JPH::CollisionGroup(
                collisionGroupFilter_.GetPtr(),
                0,
                static_cast<JPH::CollisionGroup::SubGroupID>((std::max)(0, (std::min)(kPhysicsLayerCount - 1, body.collisionLayer))));
            if (movable) {
                settings.mAllowDynamicOrKinematic = true;
            }
            if (dynamic) {
                settings.mMassPropertiesOverride.mMass = (std::max)(0.001f, body.mass);
                if (body.overrideMassProperties) {
                    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
                    JPH::MassProperties massProperties;
                    massProperties.mMass = (std::max)(0.001f, body.mass);
                    massProperties.mInertia = JPH::Mat44::sZero();
                    massProperties.mInertia.SetDiagonal4(JPH::Vec4(
                        (std::max)(0.001f, body.inertiaDiagonal.x),
                        (std::max)(0.001f, body.inertiaDiagonal.y),
                        (std::max)(0.001f, body.inertiaDiagonal.z),
                        1.0f));
                    settings.mMassPropertiesOverride = massProperties;
                } else {
                    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                }
            }

            JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, movable ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            if (bodyId.IsInvalid()) {
                std::fprintf(stdout, "[Physics] Jolt returned invalid body id for: %s\n", body.objectId.c_str());
                std::fflush(stdout);
            }
            bodyIds_[body.objectId] = bodyId;
            bodyIdToObjectId_[bodyId] = body.objectId;
            if (movable) {
                activeBodies_.insert(body.objectId);
            }
            if (dynamic && !bodyId.IsInvalid()) {
                dynamicBodyList_.emplace_back(bodyId, body.objectId);
            }
        }

        physicsSystem_->OptimizeBroadPhase();

        const JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(objectVsBroadPhaseLayerFilter_, Layers::Moving);
        const JPH::DefaultObjectLayerFilter objectLayerFilter(objectLayerPairFilter_, Layers::Moving);
        const JPH::BodyFilter bodyFilter;
        const JPH::ShapeFilter shapeFilter;
        for (const PhysicsCharacterDesc& desc : inputCharacters) {
            CharacterRecord record;
            record.desc = desc;
            record.state.objectId = desc.objectId;
            record.state.position = desc.position;
            record.state.rotationEuler = desc.rotationEuler;

            JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
            settings->mMass = (std::max)(0.001f, desc.mass);
            settings->mMaxStrength = (std::max)(0.0f, desc.maxStrength);
            settings->mMaxSlopeAngle = JPH::DegreesToRadians((std::max)(1.0f, (std::min)(89.0f, desc.slopeLimitDegrees)));
            settings->mShape = CreateCharacterShape(desc.height, desc.radius);
            settings->mShapeOffset = ToJoltVec3(desc.center + glm::vec3(0.0f, (std::max)(desc.height, desc.radius * 2.0f) * 0.5f, 0.0f));

            record.updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -(std::max)(0.05f, desc.stepHeight + 0.05f), 0.0f);
            record.updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, (std::max)(0.0f, desc.stepHeight), 0.0f);

            record.character = new JPH::CharacterVirtual(settings.GetPtr(), ToJoltRVec3(desc.position), ToJoltQuat(desc.rotationEuler), physicsSystem_.get());
            record.character->SetCharacterVsCharacterCollision(&characterVsCharacterCollision_);
            characterVsCharacterCollision_.Add(record.character.GetPtr());
            record.character->RefreshContacts(broadPhaseFilter, objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator_);
            characterStates_[desc.objectId] = record.state;
            characters_[desc.objectId] = std::move(record);
        }

        RefreshMeshContributorStats();
        stats_.lastBuildTimeMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count();

        if (progress) {
            progress->stepsDone.store(static_cast<int>(inputBodies.size()));
            progress->isDone.store(true);
        }
        s_activeBuildProgress = nullptr;
    }

    void Clear() {
        characterVsCharacterCollision_.mCharacters.clear();
        characters_.clear();
        characterStates_.clear();
        if (physicsSystem_) {
            JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
            for (auto& [objectId, bodyId] : bodyIds_) {
                if (!bodyId.IsInvalid()) {
                    bodyInterface.RemoveBody(bodyId);
                    bodyInterface.DestroyBody(bodyId);
                }
            }
        }

        bodyIds_.clear();
        bodyIdToObjectId_.clear();
        activeBodies_.clear();
        dynamicBodyList_.clear();
        activatorPositions_.clear();
        physicsSystem_.reset();
        jobSystem_.reset();
        tempAllocator_.reset();
        collisionGroupFilter_ = nullptr;
        bodies_.clear();
        states_.clear();
    }

    void Step(float deltaTime) {
        if (deltaTime <= 0.0f || !physicsSystem_) {
            return;
        }
        const auto stepStart = std::chrono::steady_clock::now();

        const float step = (std::min)(deltaTime, 0.05f);
        JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
        for (const std::string& objectId : activeBodies_) {
            auto stateIt = states_.find(objectId);
            auto bodyIt = bodyIds_.find(objectId);
            if (stateIt == states_.end() || bodyIt == bodyIds_.end()) {
                continue;
            }
            bodyInterface.SetLinearVelocity(bodyIt->second, ToJoltVec3(stateIt->second.velocity));
            bodyInterface.SetAngularVelocity(bodyIt->second, ToJoltVec3(stateIt->second.angularVelocity));
        }

        physicsSystem_->Update(step, 1, tempAllocator_.get(), jobSystem_.get());

        // Spatial activation culling: deactivate dynamic bodies far from all activators,
        // reactivate those that come within range. Hysteresis prevents rapid toggling.
        std::uint32_t activeDynamicCount = 0;
        if (!activatorPositions_.empty() && !dynamicBodyList_.empty()) {
            JPH::BodyInterface& bi = physicsSystem_->GetBodyInterface();
            for (const auto& [bodyId, objectId] : dynamicBodyList_) {
                if (bodyId.IsInvalid()) {
                    continue;
                }
                const glm::vec3 bodyPos = FromJoltRVec3(bi.GetPosition(bodyId));
                float minDistSq = std::numeric_limits<float>::max();
                for (const auto& activatorPos : activatorPositions_) {
                    const glm::vec3 delta = bodyPos - activatorPos;
                    const float dSq = glm::dot(delta, delta);
                    if (dSq < minDistSq) {
                        minDistSq = dSq;
                    }
                }
                if (minDistSq < activationRadiusSq_) {
                    if (!bi.IsActive(bodyId)) {
                        bi.ActivateBody(bodyId);
                    }
                } else if (minDistSq > deactivationRadiusSq_ && bi.IsActive(bodyId)) {
                    bi.DeactivateBody(bodyId);
                }
                if (bi.IsActive(bodyId)) {
                    ++activeDynamicCount;
                }
            }
        } else {
            // Culling disabled — count all dynamic bodies as active.
            activeDynamicCount = static_cast<std::uint32_t>(dynamicBodyList_.size());
        }
        stats_.dynamicBodyCount = static_cast<std::uint32_t>(dynamicBodyList_.size());
        stats_.activeDynamicCount = activeDynamicCount;

        const JPH::Vec3 gravity = JPH::Vec3(0.0f, -9.81f, 0.0f);
        const JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(objectVsBroadPhaseLayerFilter_, Layers::Moving);
        const JPH::DefaultObjectLayerFilter objectLayerFilter(objectLayerPairFilter_, Layers::Moving);
        const JPH::BodyFilter bodyFilter;
        const JPH::ShapeFilter shapeFilter;
        for (auto& [objectId, record] : characters_) {
            JPH::CharacterVirtual* character = record.character.GetPtr();
            if (character == nullptr) {
                continue;
            }

            const glm::vec3 currentVelocity = FromJoltVec3(character->GetLinearVelocity());
            const glm::vec3 groundVelocity = FromJoltVec3(character->GetGroundVelocity());
            const bool supported = character->IsSupported();
            glm::vec3 nextVelocity = record.desiredVelocity;
            nextVelocity.y = supported ? 0.0f : currentVelocity.y + gravity.GetY() * step;
            if (supported) {
                nextVelocity += groundVelocity;
            }
            if (record.pendingJumpImpulse > 0.0f && supported) {
                nextVelocity.y = record.pendingJumpImpulse;
            }

            character->SetRotation(ToJoltQuat(record.state.rotationEuler));
            character->SetLinearVelocity(ToJoltVec3(nextVelocity));
            character->ExtendedUpdate(step, gravity, record.updateSettings, broadPhaseFilter, objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator_);

            record.pendingJumpImpulse = 0.0f;
            record.state.position = FromJoltRVec3(character->GetPosition());
            record.state.rotationEuler = FromJoltQuat(character->GetRotation());
            record.state.velocity = FromJoltVec3(character->GetLinearVelocity());
            record.state.groundVelocity = FromJoltVec3(character->GetGroundVelocity());
            record.state.grounded = character->IsSupported();
            characterStates_[objectId] = record.state;
        }

        // Sample body state after character updates so any push impulses applied by
        // the character controller survive into the next frame's sync.
        for (const std::string& objectId : activeBodies_) {
            auto stateIt = states_.find(objectId);
            auto bodyIt = bodyIds_.find(objectId);
            if (stateIt == states_.end() || bodyIt == bodyIds_.end()) {
                continue;
            }
            stateIt->second.position = FromJoltRVec3(bodyInterface.GetPosition(bodyIt->second));
            stateIt->second.rotationEuler = FromJoltQuat(bodyInterface.GetRotation(bodyIt->second));
            stateIt->second.velocity = FromJoltVec3(bodyInterface.GetLinearVelocity(bodyIt->second));
            stateIt->second.angularVelocity = FromJoltVec3(bodyInterface.GetAngularVelocity(bodyIt->second));
        }
        stats_.lastStepTimeMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stepStart).count();
    }

    const PhysicsWorldStats& GetStats() const {
        return stats_;
    }

    bool HasBody(const std::string& objectId) const {
        return bodyIds_.find(objectId) != bodyIds_.end();
    }

    bool GetBodyState(const std::string& objectId, PhysicsBodyState& outState) const {
        auto it = states_.find(objectId);
        if (it == states_.end()) {
            return false;
        }
        outState = it->second;
        return true;
    }

    glm::vec3 GetBodyVelocity(const std::string& objectId) const {
        auto it = states_.find(objectId);
        return it == states_.end() ? glm::vec3{0.0f} : it->second.velocity;
    }

    void SetBodyVelocity(const std::string& objectId, const glm::vec3& velocity) {
        auto stateIt = states_.find(objectId);
        if (stateIt != states_.end()) {
            stateIt->second.velocity = velocity;
        }
        if (!physicsSystem_ || activeBodies_.find(objectId) == activeBodies_.end()) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().SetLinearVelocity(bodyIt->second, ToJoltVec3(velocity));
        }
    }

    glm::vec3 GetBodyAngularVelocity(const std::string& objectId) const {
        auto it = states_.find(objectId);
        return it == states_.end() ? glm::vec3{0.0f} : it->second.angularVelocity;
    }

    void SetBodyAngularVelocity(const std::string& objectId, const glm::vec3& velocity) {
        auto stateIt = states_.find(objectId);
        if (stateIt != states_.end()) {
            stateIt->second.angularVelocity = velocity;
        }
        if (!physicsSystem_ || activeBodies_.find(objectId) == activeBodies_.end()) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().SetAngularVelocity(bodyIt->second, ToJoltVec3(velocity));
        }
    }

    void AddBodyForce(const std::string& objectId, const glm::vec3& force) {
        if (!physicsSystem_) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().AddForce(bodyIt->second, ToJoltVec3(force), JPH::EActivation::Activate);
        }
    }

    void AddBodyForceAtPosition(const std::string& objectId, const glm::vec3& force, const glm::vec3& position) {
        if (!physicsSystem_) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().AddForce(
                bodyIt->second,
                ToJoltVec3(force),
                ToJoltRVec3(position),
                JPH::EActivation::Activate);
        }
    }

    void MoveBodyKinematic(const std::string& objectId, const glm::vec3& position, const glm::vec3& rotationEuler, float deltaTime) {
        auto stateIt = states_.find(objectId);
        if (stateIt != states_.end()) {
            stateIt->second.position = position;
            stateIt->second.rotationEuler = rotationEuler;
        }
        if (!physicsSystem_) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt == bodyIds_.end()) {
            return;
        }
        physicsSystem_->GetBodyInterface().MoveKinematic(
            bodyIt->second,
            ToJoltRVec3(position),
            ToJoltQuat(rotationEuler),
            (std::max)(0.0f, deltaTime));
    }

    void AddBodyImpulse(const std::string& objectId, const glm::vec3& impulse) {
        if (!physicsSystem_) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().AddImpulse(bodyIt->second, ToJoltVec3(impulse));
        }
    }

    void AddBodyTorque(const std::string& objectId, const glm::vec3& torque) {
        if (!physicsSystem_) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().AddTorque(bodyIt->second, ToJoltVec3(torque), JPH::EActivation::Activate);
        }
    }

    void AddBodyAngularImpulse(const std::string& objectId, const glm::vec3& impulse) {
        if (!physicsSystem_) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().AddAngularImpulse(bodyIt->second, ToJoltVec3(impulse));
        }
    }

    void WakeBody(const std::string& objectId) {
        if (!physicsSystem_) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().ActivateBody(bodyIt->second);
        }
    }

    void SleepBody(const std::string& objectId) {
        if (!physicsSystem_) {
            return;
        }
        auto bodyIt = bodyIds_.find(objectId);
        if (bodyIt != bodyIds_.end()) {
            physicsSystem_->GetBodyInterface().DeactivateBody(bodyIt->second);
        }
    }

    void SetActivatorPositions(const std::vector<glm::vec3>& positions, float activationRadius, float deactivationRadius) {
        activatorPositions_ = positions;
        activationRadiusSq_ = activationRadius * activationRadius;
        deactivationRadiusSq_ = deactivationRadius * deactivationRadius;
    }

    bool HasCharacter(const std::string& objectId) const {
        return characters_.find(objectId) != characters_.end();
    }

    bool GetCharacterState(const std::string& objectId, PhysicsCharacterState& outState) const {
        auto it = characterStates_.find(objectId);
        if (it == characterStates_.end()) {
            return false;
        }
        outState = it->second;
        return true;
    }

    glm::vec3 GetCharacterVelocity(const std::string& objectId) const {
        auto it = characterStates_.find(objectId);
        return it == characterStates_.end() ? glm::vec3{0.0f} : it->second.velocity;
    }

    void SetCharacterTransform(const std::string& objectId, const glm::vec3& position, const glm::vec3& rotationEuler) {
        auto it = characters_.find(objectId);
        if (it == characters_.end() || it->second.character == nullptr) {
            return;
        }
        it->second.state.position = position;
        it->second.state.rotationEuler = rotationEuler;
        it->second.character->SetPosition(ToJoltRVec3(position));
        it->second.character->SetRotation(ToJoltQuat(rotationEuler));
        characterStates_[objectId] = it->second.state;
    }

    void SetCharacterDesiredVelocity(const std::string& objectId, const glm::vec3& velocity) {
        auto it = characters_.find(objectId);
        if (it == characters_.end()) {
            return;
        }
        it->second.desiredVelocity = velocity;
    }

    void AddCharacterJumpImpulse(const std::string& objectId, float impulse) {
        auto it = characters_.find(objectId);
        if (it == characters_.end()) {
            return;
        }
        it->second.pendingJumpImpulse += impulse;
    }

    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, PhysicsRaycastHit& outHit, const std::string* ignoreObjectId) const {
        std::unordered_set<std::string> ignoreObjectIds;
        if (ignoreObjectId != nullptr && !ignoreObjectId->empty()) {
            ignoreObjectIds.insert(*ignoreObjectId);
        }
        return RaycastIgnoring(origin, direction, maxDistance, outHit, ignoreObjectIds);
    }

    bool RaycastIgnoring(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, PhysicsRaycastHit& outHit, const std::unordered_set<std::string>& ignoreObjectIds) const {
        outHit = {};
        if (!physicsSystem_ || maxDistance <= 0.0f) {
            return false;
        }

        const float lengthSquared = glm::dot(direction, direction);
        if (lengthSquared <= 0.000001f) {
            return false;
        }

        const glm::vec3 dirNormalized = direction * (1.0f / std::sqrt(lengthSquared));
        const JPH::RRayCast ray(ToJoltRVec3(origin), ToJoltVec3(dirNormalized * maxDistance));
        JPH::RayCastSettings settings;
        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;

        const JPH::BodyFilter* bodyFilterPtr = nullptr;
        JPH::BodyFilter defaultFilter;
        const JPH::BodyFilter& bodyFilter = bodyFilterPtr ? *bodyFilterPtr : defaultFilter;
        // Use the same layer filters as the simulation so sensors are culled in broad phase
        // rather than after the narrow phase test.
        const JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(objectVsBroadPhaseLayerFilter_, Layers::Moving);
        const JPH::DefaultObjectLayerFilter objectLayerFilter(objectLayerPairFilter_, Layers::Moving);
        const JPH::ShapeFilter shapeFilter;
        physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, settings, collector, broadPhaseFilter, objectLayerFilter, bodyFilter, shapeFilter);

        if (!collector.HadHit()) {
            return false;
        }

        collector.Sort();
        for (const JPH::RayCastResult& hit : collector.mHits) {
            JPH::BodyLockRead lock(physicsSystem_->GetBodyLockInterface(), hit.mBodyID);
            if (!lock.Succeeded()) {
                continue;
            }
            const JPH::Body& body = lock.GetBody();
            if (body.IsSensor()) {
                continue;
            }
            auto idIt = bodyIdToObjectId_.find(hit.mBodyID);
            if (idIt != bodyIdToObjectId_.end() && ignoreObjectIds.find(idIt->second) != ignoreObjectIds.end()) {
                continue;
            }

            const JPH::RVec3 hitPosition = ray.GetPointOnRay(hit.mFraction);
            const JPH::Vec3 hitNormal = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPosition);

            outHit.hit = true;
            outHit.position = FromJoltRVec3(hitPosition);
            outHit.normal = FromJoltVec3(hitNormal);
            outHit.distance = maxDistance * hit.mFraction;
            if (idIt != bodyIdToObjectId_.end()) {
                outHit.objectId = idIt->second;
            }
            return true;
        }

        return false;
    }

    PhysicsCullingDebugInfo GetCullingDebugInfo() const {
        PhysicsCullingDebugInfo info;
        info.hasActivators = !activatorPositions_.empty();
        info.activationRadius = std::sqrt(activationRadiusSq_);
        info.deactivationRadius = std::sqrt(deactivationRadiusSq_);
        info.activatorPositions = activatorPositions_;
        if (physicsSystem_ && !dynamicBodyList_.empty()) {
            const JPH::BodyInterface& bi = physicsSystem_->GetBodyInterface();
            info.dynamicBodies.reserve(dynamicBodyList_.size());
            for (const auto& [bodyId, objectId] : dynamicBodyList_) {
                if (bodyId.IsInvalid()) {
                    continue;
                }
                PhysicsCullingDebugInfo::BodyDebug bd;
                bd.position = FromJoltRVec3(bi.GetPosition(bodyId));
                bd.isActive = bi.IsActive(bodyId);
                info.dynamicBodies.push_back(bd);
            }
        }
        return info;
    }

private:
    void RefreshMeshContributorStats() {
        std::unordered_map<std::string, PhysicsMeshContributorStats> meshStats;
        for (const PhysicsBodyDesc& body : bodies_) {
            for (const PhysicsColliderDesc& collider : body.colliders) {
                if (collider.type != PhysicsColliderType::Mesh || collider.meshAssetPath.empty()) {
                    continue;
                }

                // Read triangle count from the shape cache — it was stored there during
                // Build() (from disk cache or after cooking). Avoids re-loading the mesh
                // file via Assimp just to get stats.
                std::uint64_t triangleCount = 0;
                const auto& shapeCache = GetShapeCache();
                const auto shapeIt = shapeCache.find(BuildShapeCacheKey(collider));
                if (shapeIt != shapeCache.end() && shapeIt->second.valid) {
                    triangleCount = shapeIt->second.triangleCount;
                }

                const std::string key = BuildMeshCacheKey(collider.meshAssetPath, collider.meshIndex) + "#" +
                                        std::to_string(static_cast<int>(collider.meshMode));
                PhysicsMeshContributorStats& contributor = meshStats[key];
                contributor.meshAssetPath = collider.meshAssetPath;
                contributor.meshName = collider.meshName;
                contributor.meshIndex = collider.meshIndex;
                contributor.meshMode = collider.meshMode;
                contributor.triangleCount = triangleCount;
                ++contributor.usageCount;
            }
        }

        stats_.meshContributors.clear();
        stats_.meshContributors.reserve(meshStats.size());
        for (auto& [key, contributor] : meshStats) {
            (void)key;
            stats_.meshContributors.push_back(std::move(contributor));
        }
        std::sort(stats_.meshContributors.begin(), stats_.meshContributors.end(),
                  [](const PhysicsMeshContributorStats& a, const PhysicsMeshContributorStats& b) {
                      const std::uint64_t scoreA = static_cast<std::uint64_t>(a.usageCount) * (std::max<std::uint64_t>)(1, a.triangleCount);
                      const std::uint64_t scoreB = static_cast<std::uint64_t>(b.usageCount) * (std::max<std::uint64_t>)(1, b.triangleCount);
                      if (scoreA != scoreB) {
                          return scoreA > scoreB;
                      }
                      return a.meshAssetPath < b.meshAssetPath;
                  });
    }

    PhysicsLayerCollisionMatrix collisionMatrix_{};
    std::vector<PhysicsBodyDesc> bodies_;
    std::unordered_map<std::string, PhysicsBodyState> states_;
    std::unordered_map<std::string, JPH::BodyID> bodyIds_;
    std::unordered_map<JPH::BodyID, std::string> bodyIdToObjectId_;
    std::unordered_set<std::string> activeBodies_;

    // Spatial activation culling: dynamic props far from all activators are deactivated.
    std::vector<std::pair<JPH::BodyID, std::string>> dynamicBodyList_;
    std::vector<glm::vec3> activatorPositions_;
    float activationRadiusSq_{150.0f * 150.0f};
    float deactivationRadiusSq_{200.0f * 200.0f};
    std::unordered_map<std::string, CharacterRecord> characters_;
    std::unordered_map<std::string, PhysicsCharacterState> characterStates_;
    JPH::CharacterVsCharacterCollisionSimple characterVsCharacterCollision_;
    JPH::Ref<JPH::GroupFilter> collisionGroupFilter_;

    BroadPhaseLayerInterfaceImpl broadPhaseLayerInterface_;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter_;
    ObjectLayerPairFilterImpl objectLayerPairFilter_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    PhysicsWorldStats stats_{};
};

PhysicsWorld::PhysicsWorld(const PhysicsLayerCollisionMatrix& collisionMatrix)
    : impl_(std::make_unique<Impl>(collisionMatrix)) {}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::Build(const std::vector<PhysicsBodyDesc>& bodies) {
    impl_->Build(bodies);
}

void PhysicsWorld::Build(const std::vector<PhysicsBodyDesc>& bodies, const std::vector<PhysicsCharacterDesc>& characters) {
    impl_->Build(bodies, characters);
}

void PhysicsWorld::Build(const std::vector<PhysicsBodyDesc>& bodies, const std::vector<PhysicsCharacterDesc>& characters,
                         PhysicsBuildProgress* progress) {
    impl_->Build(bodies, characters, progress);
}

void PhysicsWorld::Clear() {
    impl_->Clear();
}

void PhysicsWorld::Step(float deltaTime) {
    impl_->Step(deltaTime);
}

bool PhysicsWorld::HasBody(const std::string& objectId) const {
    return impl_->HasBody(objectId);
}

bool PhysicsWorld::GetBodyState(const std::string& objectId, PhysicsBodyState& outState) const {
    return impl_->GetBodyState(objectId, outState);
}

glm::vec3 PhysicsWorld::GetBodyVelocity(const std::string& objectId) const {
    return impl_->GetBodyVelocity(objectId);
}

void PhysicsWorld::SetBodyVelocity(const std::string& objectId, const glm::vec3& velocity) {
    impl_->SetBodyVelocity(objectId, velocity);
}

glm::vec3 PhysicsWorld::GetBodyAngularVelocity(const std::string& objectId) const {
    return impl_->GetBodyAngularVelocity(objectId);
}

void PhysicsWorld::SetBodyAngularVelocity(const std::string& objectId, const glm::vec3& velocity) {
    impl_->SetBodyAngularVelocity(objectId, velocity);
}

void PhysicsWorld::MoveBodyKinematic(const std::string& objectId, const glm::vec3& position, const glm::vec3& rotationEuler, float deltaTime) {
    impl_->MoveBodyKinematic(objectId, position, rotationEuler, deltaTime);
}

void PhysicsWorld::AddBodyForce(const std::string& objectId, const glm::vec3& force) {
    impl_->AddBodyForce(objectId, force);
}

void PhysicsWorld::AddBodyForceAtPosition(const std::string& objectId, const glm::vec3& force, const glm::vec3& position) {
    impl_->AddBodyForceAtPosition(objectId, force, position);
}

void PhysicsWorld::AddBodyImpulse(const std::string& objectId, const glm::vec3& impulse) {
    impl_->AddBodyImpulse(objectId, impulse);
}

void PhysicsWorld::AddBodyTorque(const std::string& objectId, const glm::vec3& torque) {
    impl_->AddBodyTorque(objectId, torque);
}

void PhysicsWorld::AddBodyAngularImpulse(const std::string& objectId, const glm::vec3& impulse) {
    impl_->AddBodyAngularImpulse(objectId, impulse);
}

void PhysicsWorld::WakeBody(const std::string& objectId) {
    impl_->WakeBody(objectId);
}

void PhysicsWorld::SleepBody(const std::string& objectId) {
    impl_->SleepBody(objectId);
}

void PhysicsWorld::SetActivatorPositions(const std::vector<glm::vec3>& positions, float activationRadius, float deactivationRadius) {
    impl_->SetActivatorPositions(positions, activationRadius, deactivationRadius);
}

bool PhysicsWorld::HasCharacter(const std::string& objectId) const {
    return impl_->HasCharacter(objectId);
}

bool PhysicsWorld::GetCharacterState(const std::string& objectId, PhysicsCharacterState& outState) const {
    return impl_->GetCharacterState(objectId, outState);
}

glm::vec3 PhysicsWorld::GetCharacterVelocity(const std::string& objectId) const {
    return impl_->GetCharacterVelocity(objectId);
}

void PhysicsWorld::SetCharacterTransform(const std::string& objectId, const glm::vec3& position, const glm::vec3& rotationEuler) {
    impl_->SetCharacterTransform(objectId, position, rotationEuler);
}

void PhysicsWorld::SetCharacterDesiredVelocity(const std::string& objectId, const glm::vec3& velocity) {
    impl_->SetCharacterDesiredVelocity(objectId, velocity);
}

void PhysicsWorld::AddCharacterJumpImpulse(const std::string& objectId, float impulse) {
    impl_->AddCharacterJumpImpulse(objectId, impulse);
}

bool PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, PhysicsRaycastHit& outHit, const std::string* ignoreObjectId) const {
    return impl_->Raycast(origin, direction, maxDistance, outHit, ignoreObjectId);
}

bool PhysicsWorld::RaycastIgnoring(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, PhysicsRaycastHit& outHit, const std::unordered_set<std::string>& ignoreObjectIds) const {
    return impl_->RaycastIgnoring(origin, direction, maxDistance, outHit, ignoreObjectIds);
}

const PhysicsWorldStats& PhysicsWorld::GetStats() const {
    return impl_->GetStats();
}

PhysicsCullingDebugInfo PhysicsWorld::GetCullingDebugInfo() const {
    return impl_->GetCullingDebugInfo();
}

} // namespace raceman

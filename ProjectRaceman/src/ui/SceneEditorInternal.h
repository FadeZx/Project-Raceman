#pragma once

#include "SceneEditor.h"
#include "./ObjImport.h"
#include "Console.h"
#include "../rendering/PrimitivePlane.h"
#include "../rendering/Renderer.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

namespace raceman::scene_editor_internal {

namespace fs = std::filesystem;

enum class SceneColliderType {
    None,
    Box,
    Sphere,
    Capsule,
    Plane,
    Mesh
};

inline SceneColliderType GetActiveColliderType(const SceneObject& object) {
    if (object.hasBoxCollider) return SceneColliderType::Box;
    if (object.hasSphereCollider) return SceneColliderType::Sphere;
    if (object.hasCapsuleCollider) return SceneColliderType::Capsule;
    if (object.hasPlaneCollider) return SceneColliderType::Plane;
    if (object.hasMeshCollider) return SceneColliderType::Mesh;
    return SceneColliderType::None;
}

inline bool HasColliderComponent(const SceneObject& object) {
    return GetActiveColliderType(object) != SceneColliderType::None;
}

inline void ClearColliderComponent(SceneObject& object) {
    object.hasBoxCollider = false;
    object.hasSphereCollider = false;
    object.hasCapsuleCollider = false;
    object.hasPlaneCollider = false;
    object.hasMeshCollider = false;
    object.boxCollider = BoxColliderComponent{};
    object.sphereCollider = SphereColliderComponent{};
    object.capsuleCollider = CapsuleColliderComponent{};
    object.planeCollider = PlaneColliderComponent{};
    object.meshCollider = MeshColliderComponent{};
}

inline void SetActiveColliderType(SceneObject& object, SceneColliderType type) {
    if (GetActiveColliderType(object) == type) {
        return;
    }

    ClearColliderComponent(object);
    switch (type) {
    case SceneColliderType::Box:
        object.hasBoxCollider = true;
        break;
    case SceneColliderType::Sphere:
        object.hasSphereCollider = true;
        break;
    case SceneColliderType::Capsule:
        object.hasCapsuleCollider = true;
        break;
    case SceneColliderType::Plane:
        object.hasPlaneCollider = true;
        break;
    case SceneColliderType::Mesh:
        object.hasMeshCollider = true;
        break;
    case SceneColliderType::None:
        break;
    }
}

inline const char* SceneColliderTypeLabel(SceneColliderType type) {
    switch (type) {
    case SceneColliderType::Box: return "Box";
    case SceneColliderType::Sphere: return "Sphere";
    case SceneColliderType::Capsule: return "Capsule";
    case SceneColliderType::Plane: return "Plane";
    case SceneColliderType::Mesh: return "Mesh";
    case SceneColliderType::None: return "None";
    }
    return "None";
}

inline const char* SceneColliderTypeIcon(SceneColliderType type) {
    switch (type) {
    case SceneColliderType::Box: return "component-box-collider.png";
    case SceneColliderType::Sphere: return "component-sphere-collider.png";
    case SceneColliderType::Capsule: return "component-capsule-collider.png";
    case SceneColliderType::Plane: return "component-plane-collider.png";
    case SceneColliderType::Mesh: return "component-mesh-collider.png";
    case SceneColliderType::None: return "component-box-collider.png";
    }
    return "component-box-collider.png";
}

inline std::vector<SceneInspectorComponentType> DefaultInspectorComponentOrder() {
    return {
        SceneInspectorComponentType::Transform,
        SceneInspectorComponentType::MeshFilter,
        SceneInspectorComponentType::MeshRenderer,
        SceneInspectorComponentType::Script,
        SceneInspectorComponentType::Rigidbody,
        SceneInspectorComponentType::Vehicle,
        SceneInspectorComponentType::CharacterController,
        SceneInspectorComponentType::Collider,
        SceneInspectorComponentType::Camera,
        SceneInspectorComponentType::Light
    };
}

inline bool HasInspectorComponent(const SceneObject& object, SceneInspectorComponentType type) {
    switch (type) {
    case SceneInspectorComponentType::Transform:
        return true;
    case SceneInspectorComponentType::MeshFilter:
        return object.hasMeshFilter;
    case SceneInspectorComponentType::MeshRenderer:
        return object.hasMeshRenderer;
    case SceneInspectorComponentType::Script:
        return object.hasScriptComponent;
    case SceneInspectorComponentType::Rigidbody:
        return object.hasRigidbody;
    case SceneInspectorComponentType::Vehicle:
        return object.hasVehicle;
    case SceneInspectorComponentType::CharacterController:
        return object.hasCharacterController;
    case SceneInspectorComponentType::Collider:
        return HasColliderComponent(object);
    case SceneInspectorComponentType::Camera:
        return object.hasCamera;
    case SceneInspectorComponentType::Light:
        return object.hasLight;
    }
    return false;
}

inline void SyncInspectorComponentOrder(SceneObject& object) {
    const std::vector<SceneInspectorComponentType> defaults = DefaultInspectorComponentOrder();
    std::vector<SceneInspectorComponentType> synced;
    synced.reserve(defaults.size());

    for (SceneInspectorComponentType type : object.inspectorComponentOrder) {
        if (!HasInspectorComponent(object, type)) {
            continue;
        }
        if (std::find(synced.begin(), synced.end(), type) == synced.end()) {
            synced.push_back(type);
        }
    }

    for (SceneInspectorComponentType type : defaults) {
        if (!HasInspectorComponent(object, type)) {
            continue;
        }
        if (std::find(synced.begin(), synced.end(), type) == synced.end()) {
            synced.push_back(type);
        }
    }

    if (synced.empty()) {
        synced.push_back(SceneInspectorComponentType::Transform);
    }
    if (synced.front() != SceneInspectorComponentType::Transform) {
        synced.erase(std::remove(synced.begin(), synced.end(), SceneInspectorComponentType::Transform), synced.end());
        synced.insert(synced.begin(), SceneInspectorComponentType::Transform);
    }
    object.inspectorComponentOrder = std::move(synced);
}

inline bool MoveInspectorComponentBefore(SceneObject& object,
                                         SceneInspectorComponentType dragged,
                                         SceneInspectorComponentType target) {
    if (dragged == SceneInspectorComponentType::Transform || target == SceneInspectorComponentType::Transform || dragged == target) {
        return false;
    }

    SyncInspectorComponentOrder(object);
    auto draggedIt = std::find(object.inspectorComponentOrder.begin(), object.inspectorComponentOrder.end(), dragged);
    auto targetIt = std::find(object.inspectorComponentOrder.begin(), object.inspectorComponentOrder.end(), target);
    if (draggedIt == object.inspectorComponentOrder.end() || targetIt == object.inspectorComponentOrder.end()) {
        return false;
    }

    const SceneInspectorComponentType draggedType = *draggedIt;
    object.inspectorComponentOrder.erase(draggedIt);
    targetIt = std::find(object.inspectorComponentOrder.begin(), object.inspectorComponentOrder.end(), target);
    object.inspectorComponentOrder.insert(targetIt, draggedType);
    return true;
}

inline std::string OpenMeshFileDialogWin32(const std::string& initialDirectory = {}) {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
    char fileBuffer[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Supported Meshes (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0Wavefront OBJ (*.obj)\0*.obj\0glTF (*.gltf;*.glb)\0*.gltf;*.glb\0FBX (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = initialDirectory.empty() ? nullptr : initialDirectory.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(fileBuffer);
    }
#endif
    return std::string();
}

inline std::string TrimCopyLocal(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    return s;
}

inline std::string NormalizeSlashes(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

inline std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

inline std::string OpenTextureFileDialogWin32(const std::string& initialDirectory = {}) {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
    char fileBuffer[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Image Files (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.dds;*.webp)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.dds;*.webp\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = initialDirectory.empty() ? nullptr : initialDirectory.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(fileBuffer);
    }
#endif
    return std::string();
}

inline bool IsSupportedMeshExtension(const std::string& extension) {
    const std::string lower = ToLowerCopy(extension);
    return lower == ".obj" || lower == ".gltf" || lower == ".glb" || lower == ".fbx";
}

inline bool EndsWith(const std::string& value, const std::string& suffix) {
    if (suffix.size() > value.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

inline bool IsObjAssetPath(const std::string& path) {
    return ToLowerCopy(fs::path(path).extension().string()) == ".obj";
}

inline bool IsMeshAssetPath(const std::string& path) {
    return IsSupportedMeshExtension(fs::path(path).extension().string());
}

inline bool IsMaterialAssetPath(const std::string& path) {
    return EndsWith(ToLowerCopy(NormalizeSlashes(path)), ".mat.json");
}

inline bool IsVehicleConfigAssetPath(const std::string& path) {
    return EndsWith(ToLowerCopy(NormalizeSlashes(path)), ".vehicle.json");
}

inline bool IsSceneAssetPath(const std::string& path) {
    return EndsWith(ToLowerCopy(NormalizeSlashes(path)), ".scene.json");
}

inline std::string MaterialIdFromAssetPath(const std::string& path) {
    std::string filename = fs::path(path).filename().string();
    const std::string suffix = ".mat.json";
    if (EndsWith(ToLowerCopy(filename), suffix)) {
        filename.resize(filename.size() - suffix.size());
    }
    return filename.empty() ? "pbr_default" : filename;
}

inline std::string ProjectAssetDisplayFilename(const std::string& path) {
    if (IsSceneAssetPath(path)) {
        std::string filename = fs::path(path).filename().string();
        const std::string suffix = ".scene.json";
        if (EndsWith(ToLowerCopy(filename), suffix)) {
            filename.resize(filename.size() - suffix.size());
            filename += ".scene";
        }
        return filename;
    }
    if (IsMaterialAssetPath(path)) {
        return MaterialIdFromAssetPath(path) + ".mat";
    }
    if (IsVehicleConfigAssetPath(path)) {
        std::string filename = fs::path(path).filename().string();
        const std::string suffix = ".vehicle.json";
        if (EndsWith(ToLowerCopy(filename), suffix)) {
            filename.resize(filename.size() - suffix.size());
            filename += ".vehicle";
        }
        return filename;
    }
    return fs::path(path).filename().string();
}

inline std::string ParentProjectDirectory(const std::string& path) {
    std::string parent = NormalizeSlashes(fs::path(path).parent_path().string());
    return parent.empty() ? std::string("assets") : parent;
}

inline fs::path FindEngineRoot() {
    if (fs::exists("ProjectRaceman/src") && fs::is_directory("ProjectRaceman/src")) {
        return fs::absolute("ProjectRaceman").lexically_normal();
    }
    if (fs::exists("src") && fs::is_directory("src")) {
        return fs::absolute(".").lexically_normal();
    }
    return fs::absolute(".").lexically_normal();
}

inline fs::path FindProjectRoot() {
    return (FindEngineRoot() / "Project").lexically_normal();
}

inline fs::path FindAssetsRoot() {
    return (FindProjectRoot() / "assets").lexically_normal();
}

inline fs::path EditorAssetPathToAbsolute(const std::string& relativePath) {
    return (FindEngineRoot() / "editor-assets" / fs::path(NormalizeSlashes(relativePath))).lexically_normal();
}

inline bool IsEditorAssetPath(const std::string& path) {
    const std::string normalized = ToLowerCopy(NormalizeSlashes(path));
    return normalized == "editor-assets" || normalized.rfind("editor-assets/", 0) == 0;
}

inline fs::path EngineAssetPathToAbsolute(const std::string& path) {
    std::string normalized = NormalizeSlashes(path);
    if (IsEditorAssetPath(normalized)) {
        normalized = normalized.substr(std::string("editor-assets").size());
        if (!normalized.empty() && normalized.front() == '/') {
            normalized.erase(normalized.begin());
        }
    }
    return EditorAssetPathToAbsolute(normalized);
}

inline fs::path LegacyAssetsRoot() {
    return (FindEngineRoot() / "assets").lexically_normal();
}

inline fs::path LegacyProjectRoot() {
    return FindEngineRoot();
}

inline void CopyDirectoryIfMissing(const fs::path& from, const fs::path& to) {
    if (!fs::exists(from) || !fs::is_directory(from)) {
        return;
    }
    for (const auto& entry : fs::recursive_directory_iterator(from)) {
        const fs::path relative = fs::relative(entry.path(), from);
        const fs::path dest = to / relative;
        if (entry.is_directory()) {
            fs::create_directories(dest);
        } else if (entry.is_regular_file()) {
            fs::create_directories(dest.parent_path());
            if (!fs::exists(dest)) {
                fs::copy_file(entry.path(), dest, fs::copy_options::skip_existing);
            }
        }
    }
}

inline bool IsUnderPath(const fs::path& path, const fs::path& root) {
    auto absPath = fs::weakly_canonical(path);
    auto absRoot = fs::weakly_canonical(root);
    auto pathIt = absPath.begin();
    auto rootIt = absRoot.begin();
    for (; rootIt != absRoot.end(); ++rootIt, ++pathIt) {
        if (pathIt == absPath.end() || *pathIt != *rootIt) {
            return false;
        }
    }
    return true;
}

inline std::string ToProjectAssetPath(const fs::path& absolutePath, const fs::path& assetsRoot) {
    fs::path relative = fs::relative(fs::weakly_canonical(absolutePath), fs::weakly_canonical(assetsRoot));
    return NormalizeSlashes((fs::path("assets") / relative).lexically_normal().string());
}

inline fs::path ProjectAssetPathToAbsolute(const std::string& projectPath) {
    fs::path normalized = fs::path(NormalizeSlashes(projectPath));
    fs::path assetsRoot = FindAssetsRoot();
    auto it = normalized.begin();
    if (it != normalized.end() && *it == "assets") {
        ++it;
    }

    fs::path relative;
    for (; it != normalized.end(); ++it) {
        relative /= *it;
    }
    return (assetsRoot / relative).lexically_normal();
}

inline bool OpenAbsolutePathInDefaultEditor(const fs::path& absolutePath) {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
    const std::string path = absolutePath.string();
    HINSTANCE result = ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    (void)absolutePath;
    return false;
#endif
}

inline bool OpenProjectAssetInDefaultEditor(const std::string& projectPath) {
    if (projectPath.empty()) {
        return false;
    }
    const fs::path absolutePath = ProjectAssetPathToAbsolute(projectPath);
    if (!fs::exists(absolutePath) || !fs::is_regular_file(absolutePath)) {
        return false;
    }
    return OpenAbsolutePathInDefaultEditor(absolutePath);
}

inline glm::vec3 GizmoAxisVector(int axis) {
    if (axis == 0) return {1.0f, 0.0f, 0.0f};
    if (axis == 1) return {0.0f, 1.0f, 0.0f};
    return {0.0f, 0.0f, 1.0f};
}

inline glm::vec4 GizmoAxisColor(int axis, bool highlighted) {
    const float alpha = 1.0f;
    if (highlighted) return {1.0f, 1.0f, 0.2f, alpha};
    if (axis == 0) return {1.0f, 0.15f, 0.12f, alpha};
    if (axis == 1) return {0.2f, 0.9f, 0.2f, alpha};
    return {0.2f, 0.45f, 1.0f, alpha};
}

inline bool ProjectWorldToScreen(const glm::vec3& world, const Renderer& renderer, glm::vec2& outScreen) {
    const auto& viewport = renderer.GetViewport();
    if (viewport.width <= 0 || viewport.height <= 0) {
        return false;
    }

    glm::vec4 clip = renderer.GetProj() * renderer.GetView() * glm::vec4(world, 1.0f);
    if (std::abs(clip.w) < 0.0001f) {
        return false;
    }

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outScreen.x = static_cast<float>(viewport.x) + (ndc.x * 0.5f + 0.5f) * static_cast<float>(viewport.width);
    outScreen.y = static_cast<float>(viewport.y) + (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(viewport.height);
    return true;
}

inline float DistanceToScreenSegment(const glm::vec2& point, const glm::vec2& a, const glm::vec2& b) {
    glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    if (len2 <= 0.0001f) {
        return glm::length(point - a);
    }
    const float t = std::clamp(glm::dot(point - a, ab) / len2, 0.0f, 1.0f);
    return glm::length(point - (a + ab * t));
}

inline std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

inline void CopyFileCreatingDirs(const fs::path& from, const fs::path& to) {
    if (!fs::exists(from) || !fs::is_regular_file(from)) {
        return;
    }
    fs::create_directories(to.parent_path());
    fs::copy_file(from, to, fs::copy_options::overwrite_existing);
}

inline std::vector<std::string> ReadMaterialLibraries(const fs::path& objPath) {
    std::vector<std::string> libraries;
    std::ifstream in(objPath);
    std::string line;
    while (std::getline(in, line)) {
        line = TrimCopyLocal(line);
        if (line.rfind("mtllib ", 0) == 0) {
            std::string rest = TrimCopyLocal(line.substr(7));
            if (!rest.empty()) {
                libraries.push_back(rest);
            }
        }
    }
    return libraries;
}

inline std::vector<std::string> ReadDiffuseTextureRefs(const fs::path& mtlPath) {
    std::vector<std::string> textures;
    std::ifstream in(mtlPath);
    std::string line;
    while (std::getline(in, line)) {
        line = TrimCopyLocal(line);
        if (line.rfind("map_Kd ", 0) == 0) {
            std::string rest = TrimCopyLocal(line.substr(7));
            if (!rest.empty()) {
                textures.push_back(rest);
            }
        }
    }
    return textures;
}

inline std::string PrepareMeshImportPath(const std::string& inputPath) {
    fs::path sourcePath(inputPath);
    if (sourcePath.empty()) {
        return {};
    }

    fs::path assetsRoot = FindAssetsRoot();
    fs::path absoluteSource = fs::absolute(sourcePath).lexically_normal();
    if (fs::exists(absoluteSource)) {
        absoluteSource = fs::weakly_canonical(absoluteSource);
    }

    if (IsUnderPath(absoluteSource, assetsRoot)) {
        return ToProjectAssetPath(absoluteSource, assetsRoot);
    }

    std::string stem = absoluteSource.stem().string();
    if (stem.empty()) {
        stem = "imported_mesh";
    }

    fs::path importsRoot = assetsRoot / "imports";
    fs::path destDir;
    for (int i = 1; i < 10000; ++i) {
        destDir = importsRoot / (stem + "_" + std::to_string(i));
        if (!fs::exists(destDir)) {
            break;
        }
    }
    fs::create_directories(destDir);

    fs::path destAsset = destDir / absoluteSource.filename();
    CopyFileCreatingDirs(absoluteSource, destAsset);

    if (ToLowerCopy(absoluteSource.extension().string()) == ".obj") {
        for (const std::string& mtlRef : ReadMaterialLibraries(absoluteSource)) {
            fs::path sourceMtl = absoluteSource.parent_path() / fs::path(mtlRef);
            fs::path destMtl = destDir / fs::path(mtlRef);
            CopyFileCreatingDirs(sourceMtl, destMtl);

            if (!fs::exists(sourceMtl)) {
                continue;
            }
            for (const std::string& texRef : ReadDiffuseTextureRefs(sourceMtl)) {
                fs::path sourceTexture = sourceMtl.parent_path() / fs::path(texRef);
                fs::path destTexture = destMtl.parent_path() / fs::path(texRef);
                CopyFileCreatingDirs(sourceTexture, destTexture);
            }
        }
    }

    return ToProjectAssetPath(destAsset, assetsRoot);
}

inline bool RevealAbsolutePathInExplorer(const fs::path& absolutePath) {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
    const std::string command = "/select,\"" + absolutePath.string() + "\"";
    HINSTANCE result = ShellExecuteA(nullptr, "open", "explorer.exe", command.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    (void)absolutePath;
    return false;
#endif
}

inline void ApplyMeshInfoToSceneObject(SceneObject& object, const ImportedMeshInfo& info, const std::shared_ptr<::Model>& model) {
    object.hasMeshFilter = true;
    object.meshFilter.enabled = true;
    object.meshFilter.meshType = "Mesh";
    object.meshFilter.vao = info.vao;
    object.meshFilter.indexCount = info.indexCount;
    object.meshFilter.meshIndex = static_cast<int>(info.meshIndex);
    object.meshFilter.importedMaterialName = info.materialName;
    object.meshFilter.diffuseTexturePath = NormalizeSlashes(info.diffuseTexturePath);
    object.meshFilter.diffuseTextureId = info.diffuseTextureId;
    object.meshFilter.localBoundsMin = info.localBoundsMin;
    object.meshFilter.localBoundsMax = info.localBoundsMax;
    object.meshFilter.modelRef = model;
}

inline bool IsBuiltInPrimitiveMeshType(const std::string& meshType) {
    return meshType == "Plane" || meshType == "Cube" || meshType == "Sphere" || meshType == "Cone" || meshType == "Cylinder";
}

inline bool ConfigureBuiltInPrimitive(SceneObject& object, const std::string& meshType, std::unordered_map<std::string, PrimitiveMesh>& primitiveCache) {
    if (!IsBuiltInPrimitiveMeshType(meshType)) {
        return false;
    }

    auto existing = primitiveCache.find(meshType);
    if (existing == primitiveCache.end()) {
        PrimitiveMesh primitive;
        if (meshType == "Plane") {
            primitive = CreatePlanePrimitiveMesh();
        } else if (meshType == "Cube") {
            primitive = CreateCubePrimitiveMesh();
        } else if (meshType == "Sphere") {
            primitive = CreateSpherePrimitiveMesh();
        } else if (meshType == "Cone") {
            primitive = CreateConePrimitiveMesh();
        } else if (meshType == "Cylinder") {
            primitive = CreateCylinderPrimitiveMesh();
        }
        existing = primitiveCache.emplace(meshType, std::move(primitive)).first;
    }

    object.type = "GameObject";
    object.hasMeshFilter = true;
    object.hasMeshRenderer = true;
    object.meshFilter.enabled = true;
    object.meshFilter.meshType = meshType;
    object.meshFilter.sourcePath.clear();
    object.meshFilter.meshIndex = 0;
    object.meshFilter.importedMaterialName.clear();
    object.meshFilter.diffuseTexturePath.clear();
    object.meshFilter.diffuseTextureId = 0;
    object.meshFilter.vao = existing->second.vao();
    object.meshFilter.indexCount = existing->second.indexCount();
    object.meshFilter.modelRef.reset();
    object.meshFilter.localBoundsMin = {-0.5f, -0.5f, -0.5f};
    object.meshFilter.localBoundsMax = {0.5f, 0.5f, 0.5f};
    if (meshType == "Plane") {
        object.meshFilter.localBoundsMin = {-0.5f, 0.0f, -0.5f};
        object.meshFilter.localBoundsMax = {0.5f, 0.0f, 0.5f};
    }
    object.meshRenderer.color = {1.0f, 1.0f, 1.0f, 1.0f};
    if (object.meshRenderer.materialId.empty()) {
        object.meshRenderer.materialId = "pbr_default";
    }
    return true;
}

inline bool TryLoadMeshAsset(const std::string& inputPath,
                             std::string& outResolvedPath,
                             std::shared_ptr<::Model>& outModel,
                             std::vector<ImportedMeshInfo>& outInfos) {
    outResolvedPath.clear();
    outModel.reset();
    outInfos.clear();

    if (inputPath.empty()) {
        return false;
    }

    std::vector<std::string> candidatePaths;
    const std::string normalizedInput = NormalizeSlashes(inputPath);
    candidatePaths.push_back(normalizedInput);

    const std::string preparedPath = PrepareMeshImportPath(inputPath);
    if (!preparedPath.empty() && preparedPath != normalizedInput) {
        candidatePaths.push_back(preparedPath);
    }

    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path inputAbsolute = IsEditorAssetPath(normalizedInput)
        ? EngineAssetPathToAbsolute(normalizedInput)
        : ProjectAssetPathToAbsolute(normalizedInput);
    if (!fs::exists(inputAbsolute)) {
        const std::string targetFilename = ToLowerCopy(fs::path(normalizedInput).filename().string());
        const std::string targetExtension = ToLowerCopy(fs::path(normalizedInput).extension().string());
        if (!targetFilename.empty() && fs::exists(assetsRoot)) {
            for (const auto& entry : fs::recursive_directory_iterator(assetsRoot)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                const std::string candidateFilename = ToLowerCopy(entry.path().filename().string());
                const std::string candidateExtension = ToLowerCopy(entry.path().extension().string());
                if (candidateFilename == targetFilename &&
                    (targetExtension.empty() ? IsSupportedMeshExtension(candidateExtension) : candidateExtension == targetExtension)) {
                    const std::string candidateProjectPath = ToProjectAssetPath(entry.path(), assetsRoot);
                    if (std::find(candidatePaths.begin(), candidatePaths.end(), candidateProjectPath) == candidatePaths.end()) {
                        candidatePaths.push_back(candidateProjectPath);
                    }
                }
            }
        }
    }

    for (const std::string& candidatePath : candidatePaths) {
        try {
            const fs::path absolutePath = IsEditorAssetPath(candidatePath)
                ? EngineAssetPathToAbsolute(candidatePath)
                : ProjectAssetPathToAbsolute(candidatePath);
            if (!fs::exists(absolutePath)) {
                continue;
            }
            auto model = raceman::LoadModelFromFile(absolutePath.string());
            auto infos = raceman::GetMeshInfos(model);
            if (infos.empty()) {
                continue;
            }
            outResolvedPath = candidatePath;
            outModel = std::move(model);
            outInfos = std::move(infos);
            return true;
        } catch (...) {
        }
    }

    return false;
}

inline void BeginProjectAssetDrag(const std::string& payloadType, const std::string& payloadValue, const std::string& label) {
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload(payloadType.c_str(), payloadValue.c_str(), payloadValue.size() + 1);
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }
}

inline bool IsCtrlSPressed() {
    ImGuiIO& io = ImGui::GetIO();
    return (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S));
}

inline bool IsCtrlZPressed() {
    ImGuiIO& io = ImGui::GetIO();
    return (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z));
}

inline bool IsCtrlYPressed() {
    ImGuiIO& io = ImGui::GetIO();
    return (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y));
}

inline constexpr const char* kObjAssetPayload = "RACEMAN_PROJECT_OBJ";
inline constexpr const char* kMeshAssetPayload = "RACEMAN_PROJECT_MESH";
inline constexpr const char* kMaterialAssetPayload = "RACEMAN_PROJECT_MATERIAL";
inline constexpr const char* kProjectFilePayload = "RACEMAN_PROJECT_FILE";
inline constexpr const char* kHierarchyObjectPayload = "SCENE_HIERARCHY_OBJECT_INDEX";
inline constexpr const char* kPlaneObjAssetPath = "editor-assets/mesh/plane.obj";

} // namespace raceman::scene_editor_internal

#pragma once

#include "SceneEditor.h"
#include "./ObjImport.h"
#include "Console.h"
#include "../rendering/PrimitivePlane.h"
#include "../rendering/Renderer.h"
#include "../physics/PhysicsWorld.h"
#include "../physics/SimpleJson.h"
#include "../physics/VehiclePhysics.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

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

inline bool ReadVec2(const raceman::physics::json::Object& object, const std::string& key, glm::vec2& out);
inline bool ReadVec3(const raceman::physics::json::Object& object, const std::string& key, glm::vec3& out);
inline bool ReadVec4(const raceman::physics::json::Object& object, const std::string& key, glm::vec4& out);
inline bool ReadBool(const raceman::physics::json::Object& object, const std::string& key, bool& out);
inline bool ReadString(const raceman::physics::json::Object& object, const std::string& key, std::string& out);

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

inline bool HasEnabledColliderComponent(const SceneObject& object) {
    const SceneColliderType type = GetActiveColliderType(object);
    switch (type) {
    case SceneColliderType::Box: return object.boxCollider.enabled;
    case SceneColliderType::Sphere: return object.sphereCollider.enabled;
    case SceneColliderType::Capsule: return object.capsuleCollider.enabled;
    case SceneColliderType::Plane: return object.planeCollider.enabled;
    case SceneColliderType::Mesh: return object.meshCollider.enabled;
    case SceneColliderType::None: return false;
    }
    return false;
}

inline SceneColliderType GetEnabledColliderType(const SceneObject& object) {
    const SceneColliderType type = GetActiveColliderType(object);
    if (!HasEnabledColliderComponent(object)) {
        return SceneColliderType::None;
    }
    return type;
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
        SceneInspectorComponentType::Cinemachine,
        SceneInspectorComponentType::Light,
        SceneInspectorComponentType::AudioListener,
        SceneInspectorComponentType::AudioSource,
        SceneInspectorComponentType::VehicleSound
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
    case SceneInspectorComponentType::Cinemachine:
        return object.hasCinemachine;
    case SceneInspectorComponentType::Light:
        return object.hasLight;
    case SceneInspectorComponentType::AudioListener:
        return object.hasAudioListener;
    case SceneInspectorComponentType::AudioSource:
        return object.hasAudioSource;
    case SceneInspectorComponentType::VehicleSound:
        return object.hasVehicleSound;
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

inline bool IsPrefabAssetPath(const std::string& path) {
    return EndsWith(ToLowerCopy(NormalizeSlashes(path)), ".prefab.json");
}

inline bool IsVehicleSoundAssetPath(const std::string& path) {
    return EndsWith(ToLowerCopy(NormalizeSlashes(path)), ".vehiclesound.json");
}

inline bool IsAudioAssetPath(const std::string& path) {
    const std::string ext = ToLowerCopy(fs::path(path).extension().string());
    return ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac";
}

inline bool IsScriptAssetPath(const std::string& path) {
    const std::string ext = ToLowerCopy(fs::path(path).extension().string());
    return ext == ".cpp" || ext == ".h";
}

inline std::string ScriptNameFromAssetPath(const std::string& path) {
    return fs::path(path).stem().string();
}

inline std::string ScriptSourcePathFromAssetPath(const std::string& path) {
    fs::path p(path);
    p.replace_extension(".cpp");
    return NormalizeSlashes(p.string());
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
    if (IsPrefabAssetPath(path)) {
        std::string filename = fs::path(path).filename().string();
        const std::string suffix = ".prefab.json";
        if (EndsWith(ToLowerCopy(filename), suffix)) {
            filename.resize(filename.size() - suffix.size());
            filename += ".prefab";
        }
        return filename;
    }
    if (IsVehicleSoundAssetPath(path)) {
        std::string filename = fs::path(path).filename().string();
        const std::string suffix = ".vehiclesound.json";
        if (EndsWith(ToLowerCopy(filename), suffix)) {
            filename.resize(filename.size() - suffix.size());
            filename += ".vehiclesound";
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
#if defined(_WIN32)
    char* overrideRoot = nullptr;
    std::size_t overrideRootLength = 0;
    if (_dupenv_s(&overrideRoot, &overrideRootLength, "RACEMAN_PROJECT_ROOT") == 0 && overrideRoot != nullptr) {
        std::string value = overrideRoot;
        std::free(overrideRoot);
        if (!value.empty()) {
            return fs::absolute(value).lexically_normal();
        }
    }
#else
    if (const char* overrideRoot = std::getenv("RACEMAN_PROJECT_ROOT")) {
        if (overrideRoot[0] != '\0') {
            return fs::absolute(overrideRoot).lexically_normal();
        }
    }
#endif
    // Auto-discover: find any direct sub-directory of the engine root that contains project.raceman.json.
    const fs::path engineRoot = FindEngineRoot();
    try {
        for (const auto& entry : fs::directory_iterator(engineRoot)) {
            if (entry.is_directory()) {
                if (fs::exists(entry.path() / "project.raceman.json")) {
                    return entry.path().lexically_normal();
                }
            }
        }
    } catch (...) {}
    return (engineRoot / "Project").lexically_normal();
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

inline bool ContainsText(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

inline bool ShouldFallbackToBuiltInPlane(const SceneObject& object) {
    if (!object.hasMeshFilter || object.meshFilter.meshType != "Mesh") {
        return false;
    }
    const std::string sourcePath = ToLowerCopy(NormalizeSlashes(object.meshFilter.sourcePath));
    return sourcePath == "editor-assets/mesh/plane.obj" ||
           sourcePath == "assets/mesh/plane.obj" ||
           (ContainsText(sourcePath, "assets/imports/plane_") && EndsWith(sourcePath, "/plane.obj"));
}

inline std::string ScriptFieldTypeToString(ScriptFieldType type) {
    switch (type) {
    case ScriptFieldType::Bool: return "Bool";
    case ScriptFieldType::Int: return "Int";
    case ScriptFieldType::Float: return "Float";
    case ScriptFieldType::String: return "String";
    case ScriptFieldType::Vec2: return "Vec2";
    case ScriptFieldType::Vec3: return "Vec3";
    case ScriptFieldType::Vec4: return "Vec4";
    }
    return "Float";
}

inline ScriptFieldType ScriptFieldTypeFromString(const std::string& value) {
    if (value == "Bool") return ScriptFieldType::Bool;
    if (value == "Int") return ScriptFieldType::Int;
    if (value == "String") return ScriptFieldType::String;
    if (value == "Vec2") return ScriptFieldType::Vec2;
    if (value == "Vec3") return ScriptFieldType::Vec3;
    if (value == "Vec4") return ScriptFieldType::Vec4;
    return ScriptFieldType::Float;
}

inline void WriteScriptFieldValue(std::ostream& out, const ScriptFieldEntry& field) {
    switch (field.type) {
    case ScriptFieldType::Bool:
        out << "                \"value\": " << (std::get<bool>(field.value) ? "true" : "false") << "\n";
        break;
    case ScriptFieldType::Int:
        out << "                \"value\": " << std::get<int>(field.value) << "\n";
        break;
    case ScriptFieldType::Float:
        out << "                \"value\": " << std::get<float>(field.value) << "\n";
        break;
    case ScriptFieldType::String:
        out << "                \"value\": \"" << JsonEscape(std::get<std::string>(field.value)) << "\"\n";
        break;
    case ScriptFieldType::Vec2: {
        const glm::vec2 value = std::get<glm::vec2>(field.value);
        out << "                \"value\": [" << value.x << ", " << value.y << "]\n";
        break;
    }
    case ScriptFieldType::Vec3: {
        const glm::vec3 value = std::get<glm::vec3>(field.value);
        out << "                \"value\": [" << value.x << ", " << value.y << ", " << value.z << "]\n";
        break;
    }
    case ScriptFieldType::Vec4: {
        const glm::vec4 value = std::get<glm::vec4>(field.value);
        out << "                \"value\": [" << value.x << ", " << value.y << ", " << value.z << ", " << value.w << "]\n";
        break;
    }
    }
}

inline bool TryReadScriptFieldValue(const raceman::physics::json::Object& object, ScriptFieldType type, ScriptFieldValue& outValue) {
    switch (type) {
    case ScriptFieldType::Bool: {
        bool value = false;
        if (!ReadBool(object, "value", value)) return false;
        outValue = value;
        return true;
    }
    case ScriptFieldType::Int: {
        auto it = object.find("value");
        if (it == object.end() || !it->second.is_number()) return false;
        outValue = static_cast<int>(it->second.as_number());
        return true;
    }
    case ScriptFieldType::Float: {
        auto it = object.find("value");
        if (it == object.end() || !it->second.is_number()) return false;
        outValue = static_cast<float>(it->second.as_number());
        return true;
    }
    case ScriptFieldType::String: {
        std::string value;
        if (!ReadString(object, "value", value)) return false;
        outValue = value;
        return true;
    }
    case ScriptFieldType::Vec2: {
        glm::vec2 value{0.0f};
        if (!ReadVec2(object, "value", value)) return false;
        outValue = value;
        return true;
    }
    case ScriptFieldType::Vec3: {
        glm::vec3 value{0.0f};
        if (!ReadVec3(object, "value", value)) return false;
        outValue = value;
        return true;
    }
    case ScriptFieldType::Vec4: {
        glm::vec4 value{0.0f};
        if (!ReadVec4(object, "value", value)) return false;
        outValue = value;
        return true;
    }
    }
    return false;
}

inline void AddDefaultPlaneColliderToPlane(SceneObject& object) {
    SetActiveColliderType(object, SceneColliderType::Plane);
    object.planeCollider.normal = {0.0f, 1.0f, 0.0f};
    object.planeCollider.offset = 0.0f;
    object.planeCollider.infinite = true;
    object.planeCollider.halfExtent = 1000.0f;
}

inline void WriteTextFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << content;
}

inline bool ReadTextFile(const fs::path& path, std::string& out) {
    std::ifstream in(path);
    if (!in.good()) {
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

inline fs::path ProjectRootPath() {
    return FindProjectRoot();
}

inline fs::path EngineRootPath() {
    return FindEngineRoot();
}

inline fs::path ResolveEditorPath(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    if (IsEditorAssetPath(path)) {
        return EngineAssetPathToAbsolute(path);
    }

    const fs::path normalized = fs::path(NormalizeSlashes(path));
    if (normalized.is_absolute()) {
        return normalized.lexically_normal();
    }

    auto it = normalized.begin();
    if (it != normalized.end() && *it == "assets") {
        return ProjectAssetPathToAbsolute(path);
    }

    return (ProjectRootPath() / normalized).lexically_normal();
}

inline fs::path ResolveAssetPath(const std::string& path) {
    return ResolveEditorPath(path);
}

inline void MigrateLegacyProjectLayout() {
    const fs::path engineRoot = EngineRootPath();
    const fs::path projectRoot = ProjectRootPath();
    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path legacyAssets = LegacyAssetsRoot();

    try {
        fs::create_directories(projectRoot);
        fs::create_directories(assetsRoot);

        if (fs::exists(legacyAssets) && fs::is_directory(legacyAssets)) {
            for (const auto& entry : fs::directory_iterator(legacyAssets)) {
                const std::string name = entry.path().filename().string();
                if (name == "editor") {
                    CopyDirectoryIfMissing(entry.path(), engineRoot / "editor-assets");
                    continue;
                }

                const fs::path dest = assetsRoot / entry.path().filename();
                if (entry.is_directory()) {
                    CopyDirectoryIfMissing(entry.path(), dest);
                } else if (entry.is_regular_file() && !fs::exists(dest)) {
                    fs::create_directories(dest.parent_path());
                    fs::copy_file(entry.path(), dest, fs::copy_options::skip_existing);
                }
            }
        }
    } catch (...) {}
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
    object.meshFilter.meshName = info.meshName;
    object.meshFilter.importedMaterialName = info.materialName;
    object.meshFilter.diffuseTexturePath = NormalizeSlashes(info.diffuseTexturePath);
    object.meshFilter.diffuseTextureId = info.diffuseTextureId;
    object.meshFilter.localBoundsMin = info.localBoundsMin;
    object.meshFilter.localBoundsMax = info.localBoundsMax;
    object.meshFilter.pickVertices = info.pickVertices;
    object.meshFilter.pickIndices  = info.pickIndices;
    object.meshFilter.modelRef = model;
}

inline bool IsBuiltInPrimitiveMeshType(const std::string& meshType) {
    return meshType == "Plane" || meshType == "Cube" || meshType == "Sphere" || meshType == "Cone" || meshType == "Capsule" || meshType == "Cylinder";
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
        } else if (meshType == "Capsule") {
            primitive = CreateCapsulePrimitiveMesh();
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

inline bool ReadVec2(const raceman::physics::json::Object& object, const std::string& key, glm::vec2& out) {
    auto it = object.find(key);
    if (it == object.end() || !it->second.is_array()) {
        return false;
    }

    const auto& a = it->second.as_array();
    if (a.size() != 2 || !a[0].is_number() || !a[1].is_number()) {
        return false;
    }

    out = {
        static_cast<float>(a[0].as_number()),
        static_cast<float>(a[1].as_number())
    };
    return true;
}

inline bool ReadVec3(const raceman::physics::json::Object& object, const std::string& key, glm::vec3& out) {
    auto it = object.find(key);
    if (it == object.end() || !it->second.is_array()) {
        return false;
    }

    const auto& a = it->second.as_array();
    if (a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number()) {
        return false;
    }

    out = {
        static_cast<float>(a[0].as_number()),
        static_cast<float>(a[1].as_number()),
        static_cast<float>(a[2].as_number())
    };
    return true;
}

inline bool ReadVec4(const raceman::physics::json::Object& object, const std::string& key, glm::vec4& out) {
    auto it = object.find(key);
    if (it == object.end() || !it->second.is_array()) {
        return false;
    }

    const auto& a = it->second.as_array();
    if (a.size() != 4 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number() || !a[3].is_number()) {
        return false;
    }

    out = {
        static_cast<float>(a[0].as_number()),
        static_cast<float>(a[1].as_number()),
        static_cast<float>(a[2].as_number()),
        static_cast<float>(a[3].as_number())
    };
    return true;
}

inline bool ReadBool(const raceman::physics::json::Object& object, const std::string& key, bool& out) {
    auto it = object.find(key);
    if (it == object.end() || !it->second.is_bool()) {
        return false;
    }
    out = it->second.as_bool();
    return true;
}

inline bool ReadString(const raceman::physics::json::Object& object, const std::string& key, std::string& out) {
    auto it = object.find(key);
    if (it == object.end() || !it->second.is_string()) {
        return false;
    }
    out = it->second.as_string();
    return true;
}

inline std::string RigidbodyBodyTypeToString(RigidbodyBodyType bodyType) {
    if (bodyType == RigidbodyBodyType::Static) return "Static";
    if (bodyType == RigidbodyBodyType::Kinematic) return "Kinematic";
    return "Dynamic";
}

inline RigidbodyBodyType RigidbodyBodyTypeFromString(const std::string& value) {
    if (value == "Static") return RigidbodyBodyType::Static;
    if (value == "Kinematic") return RigidbodyBodyType::Kinematic;
    return RigidbodyBodyType::Dynamic;
}

inline std::string LightTypeToString(LightType type) {
    if (type == LightType::Directional) return "Directional";
    if (type == LightType::Spot) return "Spot";
    return "Point";
}

inline LightType LightTypeFromString(const std::string& value) {
    if (value == "Directional") return LightType::Directional;
    if (value == "Spot") return LightType::Spot;
    return LightType::Point;
}

inline std::string MeshColliderBuildQualityToString(MeshColliderBuildQuality quality) {
    if (quality == MeshColliderBuildQuality::Balanced) return "Balanced";
    return "BuildQuality";
}

inline MeshColliderBuildQuality MeshColliderBuildQualityFromString(const std::string& value) {
    if (value == "Balanced") return MeshColliderBuildQuality::Balanced;
    if (value == "BuildQuality" || value == "BuildSpeed") return MeshColliderBuildQuality::BuildQuality;
    return MeshColliderBuildQuality::BuildQuality;
}

inline std::string MeshColliderModeToString(MeshColliderMode mode) {
    if (mode == MeshColliderMode::ConvexHull) return "ConvexHull";
    return "TriangleMesh";
}

inline MeshColliderMode MeshColliderModeFromString(const std::string& value) {
    if (value == "ConvexHull") return MeshColliderMode::ConvexHull;
    return MeshColliderMode::TriangleMesh;
}

inline const char* InspectorComponentTypeToString(SceneInspectorComponentType type) {
    switch (type) {
    case SceneInspectorComponentType::Transform: return "Transform";
    case SceneInspectorComponentType::MeshFilter: return "MeshFilter";
    case SceneInspectorComponentType::MeshRenderer: return "MeshRenderer";
    case SceneInspectorComponentType::Script: return "Script";
    case SceneInspectorComponentType::Rigidbody: return "Rigidbody";
    case SceneInspectorComponentType::Vehicle: return "Vehicle";
    case SceneInspectorComponentType::CharacterController: return "CharacterController";
    case SceneInspectorComponentType::Collider: return "Collider";
    case SceneInspectorComponentType::Camera: return "Camera";
    case SceneInspectorComponentType::Cinemachine: return "Cinemachine";
    case SceneInspectorComponentType::Light: return "Light";
    case SceneInspectorComponentType::AudioListener: return "AudioListener";
    case SceneInspectorComponentType::AudioSource: return "AudioSource";
    case SceneInspectorComponentType::VehicleSound: return "VehicleSound";
    }
    return "Transform";
}

inline bool InspectorComponentTypeFromString(const std::string& value, SceneInspectorComponentType& outType) {
    if (value == "Transform") { outType = SceneInspectorComponentType::Transform; return true; }
    if (value == "MeshFilter") { outType = SceneInspectorComponentType::MeshFilter; return true; }
    if (value == "MeshRenderer") { outType = SceneInspectorComponentType::MeshRenderer; return true; }
    if (value == "Script") { outType = SceneInspectorComponentType::Script; return true; }
    if (value == "Rigidbody") { outType = SceneInspectorComponentType::Rigidbody; return true; }
    if (value == "Vehicle") { outType = SceneInspectorComponentType::Vehicle; return true; }
    if (value == "CharacterController") { outType = SceneInspectorComponentType::CharacterController; return true; }
    if (value == "Collider") { outType = SceneInspectorComponentType::Collider; return true; }
    if (value == "Camera") { outType = SceneInspectorComponentType::Camera; return true; }
    if (value == "Cinemachine") { outType = SceneInspectorComponentType::Cinemachine; return true; }
    if (value == "Light") { outType = SceneInspectorComponentType::Light; return true; }
    if (value == "AudioListener") { outType = SceneInspectorComponentType::AudioListener; return true; }
    if (value == "AudioSource") { outType = SceneInspectorComponentType::AudioSource; return true; }
    if (value == "VehicleSound") { outType = SceneInspectorComponentType::VehicleSound; return true; }
    return false;
}

inline std::string InspectorComponentKey(const std::string& objectId, SceneInspectorComponentType type) {
    return objectId + "|" + InspectorComponentTypeToString(type);
}

inline bool IsObjectNameCopySuffix(const std::string& name) {
    return name.size() >= 5 && name.substr(name.size() - 5) == " Copy";
}

inline void RemapVehicleObjectReferences(SceneObject& object, const std::unordered_map<std::string, std::string>& idRemap) {
    if (!object.hasVehicle) {
        return;
    }

    for (std::string& chassisObjectId : object.vehicle.chassisObjectIds) {
        const auto it = idRemap.find(chassisObjectId);
        if (it != idRemap.end()) {
            chassisObjectId = it->second;
        }
    }

    for (VehicleWheelBinding& wheelBinding : object.vehicle.wheelBindings) {
        const auto it = idRemap.find(wheelBinding.objectId);
        if (it != idRemap.end()) {
            wheelBinding.objectId = it->second;
        }
    }
}

inline void RemapVehicleObjectReferences(std::vector<SceneObject>& objects) {
    std::unordered_set<std::string> ids;
    ids.reserve(objects.size());
    for (const auto& object : objects) {
        ids.insert(object.id);
    }

    for (auto& object : objects) {
        if (!object.hasVehicle) {
            continue;
        }

        auto& chassisIds = object.vehicle.chassisObjectIds;
        chassisIds.erase(
            std::remove_if(chassisIds.begin(), chassisIds.end(),
                           [&ids](const std::string& id) { return ids.find(id) == ids.end(); }),
            chassisIds.end());

        auto& bindings = object.vehicle.wheelBindings;
        bindings.erase(
            std::remove_if(bindings.begin(), bindings.end(),
                           [&ids](const VehicleWheelBinding& binding) { return ids.find(binding.objectId) == ids.end(); }),
            bindings.end());
    }
}

inline bool ReadBoolArray(const raceman::physics::json::Object& object, const std::string& key, std::array<bool, kPhysicsLayerCount>& out) {
    auto it = object.find(key);
    if (it == object.end() || !it->second.is_array()) {
        return false;
    }

    const auto& values = it->second.as_array();
    if (values.size() != kPhysicsLayerCount) {
        return false;
    }

    for (int i = 0; i < kPhysicsLayerCount; ++i) {
        if (!values[static_cast<std::size_t>(i)].is_bool()) {
            return false;
        }
        out[static_cast<std::size_t>(i)] = values[static_cast<std::size_t>(i)].as_bool();
    }
    return true;
}

inline PhysicsLayerNames MakeDefaultPhysicsLayerNames() {
    PhysicsLayerNames names{};
    names[0] = "Default";
    names[1] = "World";
    names[2] = "Vehicle";
    names[3] = "VehicleWheel";
    names[4] = "Trigger";
    names[5] = "Player";
    names[6] = "Layer6";
    names[7] = "Layer7";
    return names;
}

inline PhysicsLayerCollisionMatrix MakeDefaultPhysicsLayerCollisionMatrix() {
    PhysicsLayerCollisionMatrix matrix{};
    for (int row = 0; row < kPhysicsLayerCount; ++row) {
        for (int column = 0; column < kPhysicsLayerCount; ++column) {
            matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = true;
        }
    }

    matrix[3][2] = false;
    matrix[2][3] = false;
    matrix[3][3] = false;
    return matrix;
}

inline std::string MakePhysicsLayerStorageName(const std::string& value, int fallbackIndex) {
    std::string trimmed = scene_editor_internal::TrimCopyLocal(value);
    if (trimmed.empty()) {
        return fallbackIndex == 0 ? "Default" : ("Layer" + std::to_string(fallbackIndex));
    }

    return trimmed;
}

inline ScriptFieldEntry MakeScriptFieldEntry(const ScriptFieldDefinition& definition) {
    return {definition.name, definition.type, definition.defaultValue};
}

inline bool IsScriptFieldValueCompatible(ScriptFieldType type, const ScriptFieldValue& value) {
    switch (type) {
    case ScriptFieldType::Bool: return std::holds_alternative<bool>(value);
    case ScriptFieldType::Int: return std::holds_alternative<int>(value);
    case ScriptFieldType::Float: return std::holds_alternative<float>(value);
    case ScriptFieldType::String: return std::holds_alternative<std::string>(value);
    case ScriptFieldType::Vec2: return std::holds_alternative<glm::vec2>(value);
    case ScriptFieldType::Vec3: return std::holds_alternative<glm::vec3>(value);
    case ScriptFieldType::Vec4: return std::holds_alternative<glm::vec4>(value);
    }
    return false;
}

inline bool SyncScriptAttachmentFields(ObjectScriptAttachment& attachment, const std::vector<ScriptFieldDefinition>& definitions) {
    bool changed = false;
    std::vector<ScriptFieldEntry> synced;
    synced.reserve(definitions.size());

    for (const ScriptFieldDefinition& definition : definitions) {
        auto existing = std::find_if(attachment.fields.begin(), attachment.fields.end(), [&](const ScriptFieldEntry& field) {
            return field.name == definition.name;
        });

        if (existing != attachment.fields.end() && existing->type == definition.type && IsScriptFieldValueCompatible(existing->type, existing->value)) {
            synced.push_back(*existing);
        } else {
            synced.push_back(MakeScriptFieldEntry(definition));
            changed = true;
        }
    }

    if (attachment.fields.size() != synced.size()) {
        changed = true;
    }
    attachment.fields = std::move(synced);
    return changed;
}

inline std::string MakeVehicleChassisBodyObjectId(const std::string& objectId) {
    return objectId + "::vehicle_chassis";
}

inline glm::mat4 BuildTransformMatrix(const Transform& transform) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, transform.position);
    const glm::vec3 rads = glm::radians(transform.rotationEuler);
    model = glm::rotate(model, rads.z, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, rads.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, rads.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, transform.scale);
    return model;
}

inline Transform TransformFromMatrix(const glm::mat4& matrix) {
    Transform transform;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::quat orientation;
    if (glm::decompose(matrix, transform.scale, orientation, transform.position, skew, perspective)) {
        transform.rotationEuler = glm::degrees(glm::eulerAngles(orientation));
    }
    return transform;
}

inline glm::vec3 TransformPoint(const glm::mat4& transform, const glm::vec3& point) {
    return glm::vec3(transform * glm::vec4(point, 1.0f));
}

inline glm::vec3 ToGlmVec3(const raceman::physics::Vector3& value) {
    return {value.x, value.y, value.z};
}

inline raceman::physics::Vector3 ToVehicleVec3(const glm::vec3& value) {
    return {value.x, value.y, value.z};
}

inline glm::mat3 VehicleToSceneBasis() {
    return glm::mat3(
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
}

inline glm::vec3 VehicleVectorToScene(const raceman::physics::Vector3& value) {
    return VehicleToSceneBasis() * ToGlmVec3(value);
}

inline raceman::physics::Vector3 SceneVectorToVehicle(const glm::vec3& value) {
    const glm::vec3 converted = glm::transpose(VehicleToSceneBasis()) * value;
    return ToVehicleVec3(converted);
}

inline glm::vec3 VehiclePseudoVectorToScene(const raceman::physics::Vector3& value) {
    return -VehicleVectorToScene(value);
}

inline raceman::physics::Vector3 ScenePseudoVectorToVehicle(const glm::vec3& value) {
    return ToVehicleVec3(-(glm::transpose(VehicleToSceneBasis()) * value));
}

inline float VehicleLongitudinalSpeed(const raceman::physics::VehicleRigidBodyState& state) {
    const raceman::physics::Vector3 forward = state.transform.rotation.rotate({0.0f, 1.0f, 0.0f});
    return raceman::physics::dot(state.linearVelocity, forward);
}

inline glm::quat ToGlmQuat(const raceman::physics::Quaternion& value) {
    return glm::quat(value.w, value.x, value.y, value.z);
}

inline raceman::physics::Quaternion ToVehicleQuat(const glm::quat& value) {
    return {value.w, value.x, value.y, value.z};
}

inline glm::quat VehicleQuatToScene(const raceman::physics::Quaternion& value) {
    const glm::mat3 basis = VehicleToSceneBasis();
    const glm::mat3 vehicleRotation = glm::mat3_cast(ToGlmQuat(value));
    return glm::quat_cast(basis * vehicleRotation * glm::transpose(basis));
}

inline raceman::physics::Quaternion SceneQuatToVehicle(const glm::quat& value) {
    const glm::mat3 basis = VehicleToSceneBasis();
    const glm::mat3 sceneRotation = glm::mat3_cast(value);
    return ToVehicleQuat(glm::quat_cast(glm::transpose(basis) * sceneRotation * basis));
}

inline Transform TransformFromVehicleTransform(const raceman::physics::Transform& transform) {
    Transform result;
    result.position = VehicleVectorToScene(transform.position);
    result.rotationEuler = glm::degrees(glm::eulerAngles(VehicleQuatToScene(transform.rotation)));
    result.scale = glm::vec3(1.0f);
    return result;
}

inline glm::mat4 BuildRotationOnlyMatrix(const glm::vec3& rotationEuler) {
    Transform rotationOnly;
    rotationOnly.rotationEuler = rotationEuler;
    return BuildTransformMatrix(rotationOnly);
}

inline Transform BuildWheelWorldTransformFromAuthoredLocal(const glm::mat4& vehicleWorldMatrix,
                                                           const Transform& authoredLocalTransform,
                                                           const Transform& runtimeChassisWorldTransform,
                                                           const Transform& runtimeWheelWorldTransform,
                                                           const VehicleWheelBinding& binding) {
    const glm::mat4 runtimeChassisWorldMatrix = BuildTransformMatrix(runtimeChassisWorldTransform);
    const glm::mat4 runtimeWheelWorldMatrix = BuildTransformMatrix(runtimeWheelWorldTransform);
    const glm::mat4 runtimeWheelRelativeMatrix = glm::inverse(runtimeChassisWorldMatrix) * runtimeWheelWorldMatrix;
    const Transform runtimeWheelRelativeTransform = TransformFromMatrix(runtimeWheelRelativeMatrix);

    glm::mat4 wheelWorldMatrix = vehicleWorldMatrix;
    wheelWorldMatrix = wheelWorldMatrix * glm::translate(glm::mat4(1.0f), authoredLocalTransform.position);
    wheelWorldMatrix = wheelWorldMatrix * BuildRotationOnlyMatrix(runtimeWheelRelativeTransform.rotationEuler);
    wheelWorldMatrix = wheelWorldMatrix * BuildRotationOnlyMatrix(authoredLocalTransform.rotationEuler);
    wheelWorldMatrix = wheelWorldMatrix * BuildRotationOnlyMatrix(binding.visualRotationEuler);
    wheelWorldMatrix = wheelWorldMatrix * glm::scale(glm::mat4(1.0f), authoredLocalTransform.scale);
    return TransformFromMatrix(wheelWorldMatrix);
}

inline void ApplyWorldTransformToSceneObject(std::vector<SceneObject>& objects,
                                             const std::function<int(const std::string&)>& findObjectIndexById,
                                             const std::function<glm::mat4(int)>& getObjectWorldMatrix,
                                             int objectIndex,
                                             const Transform& worldTransform,
                                             bool preserveScale) {
    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size())) {
        return;
    }

    const Transform previousLocal = objects[objectIndex].transform;
    glm::mat4 worldMatrix = BuildTransformMatrix(worldTransform);
    if (preserveScale) {
        worldMatrix = glm::scale(worldMatrix, previousLocal.scale);
    }

    const int parentIndex = findObjectIndexById(objects[objectIndex].parentId);
    if (parentIndex >= 0 && parentIndex != objectIndex) {
        objects[objectIndex].transform = TransformFromMatrix(glm::inverse(getObjectWorldMatrix(parentIndex)) * worldMatrix);
    } else {
        objects[objectIndex].transform = TransformFromMatrix(worldMatrix);
    }

    if (preserveScale) {
        objects[objectIndex].transform.scale = previousLocal.scale;
    }
}

inline bool IsVehicleWheelHelperObject(const VehicleComponent& vehicle, const std::string& objectId) {
    return std::any_of(vehicle.wheelBindings.begin(), vehicle.wheelBindings.end(), [&](const VehicleWheelBinding& binding) {
        return binding.objectId == objectId;
    });
}

inline bool HasVehicleChassisBindings(const SceneObject& object) {
    return HasEnabledColliderComponent(object) || !object.vehicle.chassisObjectIds.empty();
}

inline bool AppendSupportedVehicleChassisColliders(const SceneObject& object,
                                                   const glm::mat4& relativeMatrix,
                                                   std::vector<PhysicsColliderDesc>& outColliders) {
    const std::size_t beforeCount = outColliders.size();
    const Transform relativeTransform = TransformFromMatrix(relativeMatrix);
    const glm::vec3 relativeScale = glm::abs(relativeTransform.scale);
    const SceneColliderType colliderType = GetActiveColliderType(object);

    if (colliderType == SceneColliderType::Box && object.boxCollider.enabled && !object.boxCollider.isTrigger) {
        PhysicsColliderDesc collider;
        collider.type = PhysicsColliderType::Box;
        collider.center = TransformPoint(relativeMatrix, object.boxCollider.center);
        collider.rotationEuler = relativeTransform.rotationEuler;
        collider.scale = relativeScale;
        collider.size = object.boxCollider.size;
        outColliders.push_back(std::move(collider));
    }
    if (colliderType == SceneColliderType::Sphere && object.sphereCollider.enabled && !object.sphereCollider.isTrigger) {
        PhysicsColliderDesc collider;
        collider.type = PhysicsColliderType::Sphere;
        collider.center = TransformPoint(relativeMatrix, object.sphereCollider.center);
        collider.scale = relativeScale;
        collider.radius = object.sphereCollider.radius;
        outColliders.push_back(std::move(collider));
    }
    if (colliderType == SceneColliderType::Capsule && object.capsuleCollider.enabled && !object.capsuleCollider.isTrigger) {
        PhysicsColliderDesc collider;
        collider.type = PhysicsColliderType::Capsule;
        collider.center = TransformPoint(relativeMatrix, object.capsuleCollider.center);
        collider.rotationEuler = relativeTransform.rotationEuler;
        collider.scale = relativeScale;
        collider.radius = object.capsuleCollider.radius;
        collider.height = object.capsuleCollider.height;
        outColliders.push_back(std::move(collider));
    }
    if (colliderType == SceneColliderType::Mesh && object.meshCollider.enabled && !object.meshCollider.isTrigger &&
        object.hasMeshFilter && !object.meshFilter.sourcePath.empty()) {
        PhysicsColliderDesc collider;
        collider.type = PhysicsColliderType::Mesh;
        collider.center = relativeTransform.position;
        collider.rotationEuler = relativeTransform.rotationEuler;
        collider.scale = relativeScale;
        collider.meshAssetPath = object.meshFilter.sourcePath;
        collider.meshIndex = object.meshFilter.meshIndex;
        collider.meshName = object.meshFilter.meshName;
        collider.meshPivotOffset = object.meshFilter.pivotOffset;
        collider.meshBuildQuality = object.meshCollider.buildQuality;
        collider.meshMode = object.meshCollider.mode;
        outColliders.push_back(std::move(collider));
    }

    return outColliders.size() > beforeCount;
}

inline constexpr const char* kObjAssetPayload = "RACEMAN_PROJECT_OBJ";
inline constexpr const char* kMeshAssetPayload = "RACEMAN_PROJECT_MESH";
inline constexpr const char* kMaterialAssetPayload = "RACEMAN_PROJECT_MATERIAL";
inline constexpr const char* kProjectFilePayload = "RACEMAN_PROJECT_FILE";
inline constexpr const char* kHierarchyObjectPayload = "SCENE_HIERARCHY_OBJECT_INDEX";
inline constexpr const char* kHierarchyMultiObjectPayload = "SCENE_HIERARCHY_OBJECT_IDS";
inline constexpr const char* kPlaneObjAssetPath = "editor-assets/mesh/plane.obj";

} // namespace raceman::scene_editor_internal

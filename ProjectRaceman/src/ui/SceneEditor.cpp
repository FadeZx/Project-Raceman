#include "SceneEditorInternal.h"
#include "../rendering/ShaderRegistry.h"
#include "../input/InputManager.h"
#include "../physics/PhysicsWorld.h"
#include "../physics/SimpleJson.h"
#include "../physics/VehiclePhysics.h"
#include "../scripting/ScriptRegistry.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui/imgui_internal.h>
#include <stb_image.h>

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <unordered_set>
#include <array>
#include <iomanip>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

struct ColliderWorldAabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};
float MaxAbsComponent(const glm::vec3& value) {
    return (std::max)((std::max)(std::abs(value.x), std::abs(value.y)), std::abs(value.z));
}
glm::mat4 BuildRotationMatrix(const glm::vec3& rotationEuler) {
    glm::mat4 rotation(1.0f);
    const glm::vec3 rads = glm::radians(rotationEuler);
    rotation = glm::rotate(rotation, rads.z, glm::vec3(0.0f, 0.0f, 1.0f));
    rotation = glm::rotate(rotation, rads.y, glm::vec3(0.0f, 1.0f, 0.0f));
    rotation = glm::rotate(rotation, rads.x, glm::vec3(1.0f, 0.0f, 0.0f));
    return rotation;
}

glm::vec3 CameraForwardFromEuler(const glm::vec3& rotationEuler) {
    const glm::vec3 forward = glm::vec3(BuildRotationMatrix(rotationEuler) * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    return glm::length(forward) > 0.0001f ? glm::normalize(forward) : glm::vec3{0.0f, 0.0f, -1.0f};
}

glm::vec3 ForwardFromEuler(const glm::vec3& rotationEuler) {
    return CameraForwardFromEuler(rotationEuler);
}

fs::path ResolveProjectTexturePath(const std::string& value) {
    if (value.empty()) return {};
    fs::path path(NormalizeSlashes(value));
    if (path.is_absolute()) return path.lexically_normal();
    if (NormalizeSlashes(value).rfind("assets/", 0) == 0) {
        return ProjectAssetPathToAbsolute(value);
    }
    return (FindAssetsRoot() / path).lexically_normal();
}

unsigned int LoadMaterialTextureCached(const std::string& texturePath,
                                       std::unordered_map<std::string, unsigned int>& cache,
                                       Console* console) {
    const fs::path absolutePath = ResolveProjectTexturePath(texturePath);
    if (absolutePath.empty()) return 0;
    const std::string key = NormalizeSlashes(absolutePath.lexically_normal().string());
    if (auto existing = cache.find(key); existing != cache.end()) {
        return existing->second;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(key.c_str(), &width, &height, &channels, 4);
    if (data == nullptr || width <= 0 || height <= 0) {
        cache[key] = 0;
        if (console) console->AddError("Failed to load material texture: " + key);
        return 0;
    }

    unsigned int textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);

    cache[key] = textureId;
    return textureId;
}

glm::vec3 CameraUpFromEuler(const glm::vec3& rotationEuler) {
    const glm::vec3 up = glm::vec3(BuildRotationMatrix(rotationEuler) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    return glm::length(up) > 0.0001f ? glm::normalize(up) : glm::vec3{0.0f, 1.0f, 0.0f};
}

const char* InputDeviceTypeToStorage(InputDeviceType type) {
    switch (type) {
    case InputDeviceType::Keyboard: return "Keyboard";
    case InputDeviceType::Gamepad: return "Gamepad";
    case InputDeviceType::Wheel: return "Wheel";
    case InputDeviceType::Unknown:
    default: return "Unknown";
    }
}

InputDeviceType InputDeviceTypeFromStorage(const std::string& value) {
    const std::string lower = ToLowerCopy(value);
    if (lower == "keyboard") return InputDeviceType::Keyboard;
    if (lower == "gamepad") return InputDeviceType::Gamepad;
    if (lower == "wheel") return InputDeviceType::Wheel;
    return InputDeviceType::Unknown;
}

const char* InputBindingSourceToStorage(InputBindingSource source) {
    switch (source) {
    case InputBindingSource::Key: return "Key";
    case InputBindingSource::KeyPair: return "KeyPair";
    case InputBindingSource::Axis: return "Axis";
    case InputBindingSource::Button: return "Button";
    case InputBindingSource::None:
    default: return "None";
    }
}

InputBindingSource InputBindingSourceFromStorage(const std::string& value) {
    const std::string lower = ToLowerCopy(value);
    if (lower == "key") return InputBindingSource::Key;
    if (lower == "keypair") return InputBindingSource::KeyPair;
    if (lower == "axis") return InputBindingSource::Axis;
    if (lower == "button") return InputBindingSource::Button;
    return InputBindingSource::None;
}

const char* InputDevicePreferenceToStorage(InputDevicePreference value) {
    switch (value) {
    case InputDevicePreference::Keyboard: return "Keyboard";
    case InputDevicePreference::Gamepad: return "Gamepad";
    case InputDevicePreference::Wheel: return "Wheel";
    case InputDevicePreference::Specific: return "Specific";
    case InputDevicePreference::Any:
    default: return "Any";
    }
}

InputDevicePreference InputDevicePreferenceFromStorage(const std::string& value) {
    const std::string lower = ToLowerCopy(value);
    if (lower == "keyboard") return InputDevicePreference::Keyboard;
    if (lower == "gamepad") return InputDevicePreference::Gamepad;
    if (lower == "wheel") return InputDevicePreference::Wheel;
    if (lower == "specific") return InputDevicePreference::Specific;
    return InputDevicePreference::Any;
}

ColliderWorldAabb MakeAabbFromLocalBox(const glm::mat4& transform, const glm::vec3& center, const glm::vec3& size) {
    const glm::vec3 halfSize = glm::abs(size) * 0.5f;
    const glm::vec3 corners[8] = {
        center + glm::vec3{-halfSize.x, -halfSize.y, -halfSize.z},
        center + glm::vec3{ halfSize.x, -halfSize.y, -halfSize.z},
        center + glm::vec3{ halfSize.x,  halfSize.y, -halfSize.z},
        center + glm::vec3{-halfSize.x,  halfSize.y, -halfSize.z},
        center + glm::vec3{-halfSize.x, -halfSize.y,  halfSize.z},
        center + glm::vec3{ halfSize.x, -halfSize.y,  halfSize.z},
        center + glm::vec3{ halfSize.x,  halfSize.y,  halfSize.z},
        center + glm::vec3{-halfSize.x,  halfSize.y,  halfSize.z}
    };

    glm::vec3 minPoint = TransformPoint(transform, corners[0]);
    glm::vec3 maxPoint = minPoint;
    for (int i = 1; i < 8; ++i) {
        const glm::vec3 worldPoint = TransformPoint(transform, corners[i]);
        minPoint = {
            (std::min)(minPoint.x, worldPoint.x),
            (std::min)(minPoint.y, worldPoint.y),
            (std::min)(minPoint.z, worldPoint.z)
        };
        maxPoint = {
            (std::max)(maxPoint.x, worldPoint.x),
            (std::max)(maxPoint.y, worldPoint.y),
            (std::max)(maxPoint.z, worldPoint.z)
        };
    }
    return {minPoint, maxPoint};
}

ColliderWorldAabb MakeBoxColliderWorldAabb(const SceneObject& object) {
    return MakeAabbFromLocalBox(BuildTransformMatrix(object.transform), object.boxCollider.center, object.boxCollider.size);
}

ColliderWorldAabb MakeSphereColliderWorldAabb(const SceneObject& object) {
    const glm::mat4 transform = BuildTransformMatrix(object.transform);
    const glm::vec3 center = TransformPoint(transform, object.sphereCollider.center);
    const float radius = object.sphereCollider.radius * MaxAbsComponent(object.transform.scale);
    const glm::vec3 halfSize{radius, radius, radius};
    return {center - halfSize, center + halfSize};
}

ColliderWorldAabb MakeCapsuleColliderWorldAabb(const SceneObject& object) {
    const float radius = object.capsuleCollider.radius;
    const float halfHeight = (std::max)(object.capsuleCollider.height * 0.5f, radius);
    return MakeAabbFromLocalBox(
        BuildTransformMatrix(object.transform),
        object.capsuleCollider.center,
        glm::vec3{radius * 2.0f, halfHeight * 2.0f, radius * 2.0f});
}

std::vector<ColliderWorldAabb> BuildSolidColliderAabbs(const SceneObject& object) {
    std::vector<ColliderWorldAabb> colliders;
    const SceneColliderType colliderType = GetActiveColliderType(object);
    if (colliderType == SceneColliderType::Box && object.boxCollider.enabled && !object.boxCollider.isTrigger) {
        colliders.push_back(MakeBoxColliderWorldAabb(object));
    }
    if (colliderType == SceneColliderType::Sphere && object.sphereCollider.enabled && !object.sphereCollider.isTrigger) {
        colliders.push_back(MakeSphereColliderWorldAabb(object));
    }
    if (colliderType == SceneColliderType::Capsule && object.capsuleCollider.enabled && !object.capsuleCollider.isTrigger) {
        colliders.push_back(MakeCapsuleColliderWorldAabb(object));
    }
    return colliders;
}

bool AabbOverlap(const ColliderWorldAabb& a, const ColliderWorldAabb& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x
        && a.min.y < b.max.y && a.max.y > b.min.y
        && a.min.z < b.max.z && a.max.z > b.min.z;
}

glm::vec3 ComputeMinimumTranslation(const ColliderWorldAabb& dynamicBox, const ColliderWorldAabb& staticBox) {
    const float moveLeft = staticBox.min.x - dynamicBox.max.x;
    const float moveRight = staticBox.max.x - dynamicBox.min.x;
    const float moveDown = staticBox.min.y - dynamicBox.max.y;
    const float moveUp = staticBox.max.y - dynamicBox.min.y;
    const float moveBack = staticBox.min.z - dynamicBox.max.z;
    const float moveForward = staticBox.max.z - dynamicBox.min.z;

    glm::vec3 correction{moveLeft, 0.0f, 0.0f};
    float minDistance = std::abs(moveLeft);

    auto consider = [&](const glm::vec3& candidate) {
        const float distance = glm::length(candidate);
        if (distance < minDistance) {
            correction = candidate;
            minDistance = distance;
        }
    };

    consider({moveRight, 0.0f, 0.0f});
    consider({0.0f, moveDown, 0.0f});
    consider({0.0f, moveUp, 0.0f});
    consider({0.0f, 0.0f, moveBack});
    consider({0.0f, 0.0f, moveForward});
    return correction;
}

std::string SanitizeScriptClassName(std::string value) {
    value = scene_editor_internal::TrimCopyLocal(std::move(value));
    std::string out;
    out.reserve(value.size());
    bool makeUpper = true;
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_') {
            char c = static_cast<char>(ch);
            if (out.empty() && std::isdigit(uch)) {
                out.push_back('_');
            }
            if (makeUpper && std::isalpha(uch)) {
                c = static_cast<char>(std::toupper(uch));
            }
            out.push_back(c);
            makeUpper = false;
        } else {
            makeUpper = true;
        }
    }
    return out.empty() ? std::string("NewObjectScript") : out;
}

#if 0
bool ContainsText(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::string ScriptFieldTypeToString(ScriptFieldType type) {
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

ScriptFieldType ScriptFieldTypeFromString(const std::string& value) {
    if (value == "Bool") return ScriptFieldType::Bool;
    if (value == "Int") return ScriptFieldType::Int;
    if (value == "String") return ScriptFieldType::String;
    if (value == "Vec2") return ScriptFieldType::Vec2;
    if (value == "Vec3") return ScriptFieldType::Vec3;
    if (value == "Vec4") return ScriptFieldType::Vec4;
    return ScriptFieldType::Float;
}

void WriteScriptFieldValue(std::ostream& out, const ScriptFieldEntry& field) {
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

bool TryReadScriptFieldValue(const raceman::physics::json::Object& object, ScriptFieldType type, ScriptFieldValue& outValue) {
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

bool ShouldFallbackToBuiltInPlane(const SceneObject& object) {
    if (!object.hasMeshFilter || object.meshFilter.meshType != "Mesh") {
        return false;
    }
    const std::string sourcePath = ToLowerCopy(NormalizeSlashes(object.meshFilter.sourcePath));
    return sourcePath == "editor-assets/mesh/plane.obj" ||
           sourcePath == "assets/mesh/plane.obj" ||
           (ContainsText(sourcePath, "assets/imports/plane_") && EndsWith(sourcePath, "/plane.obj"));
}

void AddDefaultPlaneColliderToPlane(SceneObject& object) {
    SetActiveColliderType(object, SceneColliderType::Plane);
    object.planeCollider.normal = {0.0f, 1.0f, 0.0f};
    object.planeCollider.offset = 0.0f;
    object.planeCollider.infinite = true;
    object.planeCollider.halfExtent = 1000.0f;
}

void WriteTextFile(const fs::path& path, const std::string& content) {
    {
        std::ifstream in(path);
        if (in.good()) {
            std::stringstream buffer;
            buffer << in.rdbuf();
            if (buffer.str() == content) {
                return;
            }
        }
    }
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << content;
}

bool ReadTextFile(const fs::path& path, std::string& out) {
    std::ifstream in(path);
    if (!in.good()) {
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

fs::path ProjectRootPath() {
    return FindProjectRoot();
}

fs::path EngineRootPath() {
    return FindEngineRoot();
}

fs::path ResolveEditorPath(const std::string& path) {
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
#endif

void MigrateLegacyProjectLayout() {
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

        const fs::path legacyProjectFile = engineRoot / "project.raceman.json";
        const fs::path projectFile = projectRoot / "project.raceman.json";
        if (!fs::exists(projectFile) && fs::exists(legacyProjectFile)) {
            fs::copy_file(legacyProjectFile, projectFile, fs::copy_options::skip_existing);
        }
    } catch (...) {}
}

void InsertBeforeClosingItemGroup(const fs::path& projectPath, const std::string& entry) {
    std::string text;
    if (!ReadTextFile(projectPath, text) || ContainsText(text, entry)) {
        return;
    }

    const std::string marker = "  </ItemGroup>";
    const std::size_t pos = text.rfind(marker);
    if (pos == std::string::npos) {
        return;
    }
    text.insert(pos, entry + "\n");
    WriteTextFile(projectPath, text);
}

void AddProjectCompileEntry(const fs::path& projectPath, const std::string& relativePath) {
    InsertBeforeClosingItemGroup(projectPath, "    <ClCompile Include=\"" + relativePath + "\" />");
}

void AddProjectIncludeEntry(const fs::path& projectPath, const std::string& relativePath) {
    InsertBeforeClosingItemGroup(projectPath, "    <ClInclude Include=\"" + relativePath + "\" />");
}

void AddFilterCompileEntry(const fs::path& filtersPath, const std::string& relativePath) {
    InsertBeforeClosingItemGroup(filtersPath,
        "    <ClCompile Include=\"" + relativePath + "\">\n"
        "      <Filter>Source Files</Filter>\n"
        "    </ClCompile>");
}

void AddFilterIncludeEntry(const fs::path& filtersPath, const std::string& relativePath) {
    InsertBeforeClosingItemGroup(filtersPath,
        "    <ClInclude Include=\"" + relativePath + "\">\n"
        "      <Filter>Header Files</Filter>\n"
        "    </ClInclude>");
}

void RemoveProjectEntriesUnderScripts(const fs::path& projectPath) {
    std::string text;
    if (!ReadTextFile(projectPath, text)) {
        return;
    }

    std::stringstream in(text);
    std::string line;
    std::string out;
    bool changed = false;
    bool skippingBlock = false;
    std::string skipEndTag;

    while (std::getline(in, line)) {
        const std::string lineWithNewline = line + "\n";

        if (skippingBlock) {
            changed = true;
            if (ContainsText(line, skipEndTag)) {
                skippingBlock = false;
                skipEndTag.clear();
            }
            continue;
        }

        const bool scriptEntry = ContainsText(line, "Include=\"assets\\scripts\\")
            || ContainsText(line, "Include=\"Project\\assets\\");
        if (!scriptEntry) {
            out += lineWithNewline;
            continue;
        }

        changed = true;
        if (!ContainsText(line, "/>")) {
            if (ContainsText(line, "<ClInclude")) {
                skippingBlock = true;
                skipEndTag = "</ClInclude>";
            } else if (ContainsText(line, "<ClCompile")) {
                skippingBlock = true;
                skipEndTag = "</ClCompile>";
            }
        }
    }

    if (changed) {
        WriteTextFile(projectPath, out);
    }
}

struct ScriptSourceInfo {
    std::string name;
    std::string projectHeaderPath;
    std::string projectSourcePath;
};

std::vector<ScriptSourceInfo> FindCompleteScripts(const fs::path& assetsRoot) {
    std::vector<ScriptSourceInfo> scripts;
    if (!fs::exists(assetsRoot)) {
        return scripts;
    }

    for (const auto& entry : fs::recursive_directory_iterator(assetsRoot)) {
        if (!entry.is_regular_file() || ToLowerCopy(entry.path().extension().string()) != ".h") {
            continue;
        }

        const std::string scriptName = entry.path().stem().string();
        const fs::path sourcePath = entry.path().parent_path() / (scriptName + ".cpp");
        if (fs::exists(sourcePath)) {
            ScriptSourceInfo info;
            info.name = scriptName;
            info.projectHeaderPath = ToProjectAssetPath(entry.path(), assetsRoot);
            info.projectSourcePath = ToProjectAssetPath(sourcePath, assetsRoot);
            scripts.push_back(std::move(info));
        }
    }

    std::sort(scripts.begin(), scripts.end(), [](const ScriptSourceInfo& a, const ScriptSourceInfo& b) {
        return ToLowerCopy(a.projectSourcePath) < ToLowerCopy(b.projectSourcePath);
    });
    return scripts;
}

std::string BuildScriptRegistrySource(const std::vector<ScriptSourceInfo>& scripts) {
    // Compute relative path from src/scripting/ to the project root (may be Project1, Project, etc.)
    const std::string includePrefix = NormalizeSlashes(
        fs::relative(FindProjectRoot(), FindEngineRoot() / "src" / "scripting").string()) + "/";

    std::string registry;
    registry += "#include \"ScriptRegistry.h\"\n\n";
    registry += "#include <cstring>\n\n";
    for (const ScriptSourceInfo& script : scripts) {
        registry += "#include \"" + includePrefix + script.projectHeaderPath + "\"\n";
    }
    registry += "\nnamespace raceman {\nnamespace {\n\n";
    for (const ScriptSourceInfo& script : scripts) {
        const std::string& scriptName = script.name;
        registry += "std::unique_ptr<IObjectScript> Create" + scriptName + "() {\n";
        registry += "    return std::make_unique<scripts::" + scriptName + ">();\n";
        registry += "}\n\n";
    }
    registry += "struct ScriptExportEntry {\n";
    registry += "    const char* name;\n";
    registry += "    const char* path;\n";
    registry += "};\n\n";
    registry += "const ScriptExportEntry kScripts[] = {\n";
    for (const ScriptSourceInfo& script : scripts) {
        registry += "    {\"" + script.name + "\", \"" + script.projectSourcePath + "\"},\n";
    }
    registry += "};\n\n";
    registry += "} // namespace\n\n";
    registry += "extern \"C\" __declspec(dllexport) int RacemanGetScriptCount() {\n";
    registry += "    return static_cast<int>(sizeof(kScripts) / sizeof(kScripts[0]));\n";
    registry += "}\n\n";
    registry += "extern \"C\" __declspec(dllexport) const char* RacemanGetScriptName(int index) {\n";
    registry += "    const int count = RacemanGetScriptCount();\n";
    registry += "    return index >= 0 && index < count ? kScripts[index].name : nullptr;\n";
    registry += "}\n\n";
    registry += "extern \"C\" __declspec(dllexport) const char* RacemanGetScriptPath(int index) {\n";
    registry += "    const int count = RacemanGetScriptCount();\n";
    registry += "    return index >= 0 && index < count ? kScripts[index].path : nullptr;\n";
    registry += "}\n\n";
    registry += "extern \"C\" __declspec(dllexport) raceman::IObjectScript* RacemanCreateScriptByName(const char* name) {\n";
    registry += "    if (name == nullptr) return nullptr;\n";
    for (const ScriptSourceInfo& script : scripts) {
        registry += "    if (std::strcmp(name, \"" + script.name + "\") == 0) return new scripts::" + script.name + "();\n";
    }
    registry += "    return nullptr;\n";
    registry += "}\n\n";
    registry += "extern \"C\" __declspec(dllexport) void RacemanRegisterScripts(std::vector<raceman::ScriptDescriptor>& scripts) {\n";
    registry += "    scripts.clear();\n";
    for (const ScriptSourceInfo& script : scripts) {
        registry += "    scripts.push_back({\"" + script.name + "\", \"" + script.projectSourcePath + "\", &Create" + script.name + "});\n";
    }
    registry += "}\n\n";
    registry += "} // namespace raceman\n";
    return registry;
}

std::string BuildScriptProjectSource(const std::vector<ScriptSourceInfo>& scripts) {
    std::string project;
    project += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    project += "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
    project += "  <ItemGroup Label=\"ProjectConfigurations\">\n";
    project += "    <ProjectConfiguration Include=\"Debug|x64\"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>\n";
    project += "    <ProjectConfiguration Include=\"Release|x64\"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>\n";
    project += "  </ItemGroup>\n";
    project += "  <PropertyGroup Label=\"Globals\">\n";
    project += "    <VCProjectVersion>16.0</VCProjectVersion>\n";
    project += "    <Keyword>Win32Proj</Keyword>\n";
    project += "    <ProjectGuid>{B02D47B2-2BAA-4F5F-8E4C-4A0A61A6618F}</ProjectGuid>\n";
    project += "    <RootNamespace>ProjectScripts</RootNamespace>\n";
    project += "    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>\n";
    project += "  </PropertyGroup>\n";
    project += "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n";
    project += "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\" Label=\"Configuration\"><ConfigurationType>DynamicLibrary</ConfigurationType><UseDebugLibraries>true</UseDebugLibraries><PlatformToolset>v143</PlatformToolset><CharacterSet>MultiByte</CharacterSet></PropertyGroup>\n";
    project += "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\" Label=\"Configuration\"><ConfigurationType>DynamicLibrary</ConfigurationType><UseDebugLibraries>false</UseDebugLibraries><PlatformToolset>v143</PlatformToolset><WholeProgramOptimization>false</WholeProgramOptimization><CharacterSet>MultiByte</CharacterSet></PropertyGroup>\n";
    project += "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\n";
    project += "  <PropertyGroup><VcpkgEnableManifest>true</VcpkgEnableManifest></PropertyGroup>\n";
    project += "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\"><OutDir>$(SolutionDir)bin\\$(Configuration)\\</OutDir><IntDir>$(SolutionDir)bin-int\\ProjectScripts\\$(Configuration)\\</IntDir><TargetName>ProjectScripts</TargetName><LanguageStandard>stdcpp17</LanguageStandard></PropertyGroup>\n";
    project += "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\"><OutDir>$(SolutionDir)bin\\$(Configuration)\\</OutDir><IntDir>$(SolutionDir)bin-int\\ProjectScripts\\$(Configuration)\\</IntDir><TargetName>ProjectScripts</TargetName><LanguageStandard>stdcpp17</LanguageStandard></PropertyGroup>\n";
    project += "  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">\n";
    project += "    <ClCompile><WarningLevel>Level3</WarningLevel><SDLCheck>true</SDLCheck><PreprocessorDefinitions>WIN32;_DEBUG;_CONSOLE;JPH_FLOATING_POINT_EXCEPTIONS_ENABLED;JPH_PROFILE_ENABLED;JPH_DEBUG_RENDERER;JPH_OBJECT_STREAM;RACEMAN_SCRIPT_DLL;%(PreprocessorDefinitions)</PreprocessorDefinitions><ConformanceMode>true</ConformanceMode><AdditionalIncludeDirectories>$(ProjectDir)..\\includes;$(ProjectDir)includes;$(ProjectDir)editor-assets/third_party;$(ProjectDir)editor-assets/third_party/JoltPhysics-master;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories><LanguageStandard>stdcpp17</LanguageStandard><AdditionalOptions>/FS %(AdditionalOptions)</AdditionalOptions><ProgramDataBaseFileName>$(IntDir)%(Filename).compile.pdb</ProgramDataBaseFileName><RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary></ClCompile>\n";
    project += "    <Link><SubSystem>Console</SubSystem><GenerateDebugInformation>false</GenerateDebugInformation><AdditionalDependencies>ProjectRaceman.lib;%(AdditionalDependencies)</AdditionalDependencies><AdditionalLibraryDirectories>$(ProjectDir)bin\\$(Configuration);%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories></Link>\n";
    project += "  </ItemDefinitionGroup>\n";
    project += "  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\">\n";
    project += "    <ClCompile><WarningLevel>Level3</WarningLevel><FunctionLevelLinking>true</FunctionLevelLinking><IntrinsicFunctions>true</IntrinsicFunctions><PreprocessorDefinitions>WIN32;NDEBUG;_CONSOLE;JPH_DEBUG;JPH_FLOATING_POINT_EXCEPTIONS_ENABLED;JPH_PROFILE_ENABLED;JPH_DEBUG_RENDERER;JPH_OBJECT_STREAM;RACEMAN_SCRIPT_DLL;%(PreprocessorDefinitions)</PreprocessorDefinitions><ConformanceMode>true</ConformanceMode><AdditionalIncludeDirectories>$(ProjectDir)..\\includes;$(ProjectDir)includes;$(ProjectDir)editor-assets/third_party;$(ProjectDir)editor-assets/third_party/JoltPhysics-master;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories><LanguageStandard>stdcpp17</LanguageStandard><AdditionalOptions>/FS %(AdditionalOptions)</AdditionalOptions><ProgramDataBaseFileName>$(IntDir)%(Filename).compile.pdb</ProgramDataBaseFileName><WholeProgramOptimization>false</WholeProgramOptimization><RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary></ClCompile>\n";
    project += "    <Link><SubSystem>Console</SubSystem><GenerateDebugInformation>false</GenerateDebugInformation><EnableCOMDATFolding>true</EnableCOMDATFolding><OptimizeReferences>true</OptimizeReferences><LinkTimeCodeGeneration>Default</LinkTimeCodeGeneration><AdditionalDependencies>ProjectRaceman.lib;%(AdditionalDependencies)</AdditionalDependencies><AdditionalLibraryDirectories>$(ProjectDir)bin\\$(Configuration);%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories></Link>\n";
    project += "  </ItemDefinitionGroup>\n";
    project += "  <ItemGroup>\n";
    project += "    <ClInclude Include=\"src\\scripting\\ObjectScript.h\" />\n";
    project += "    <ClInclude Include=\"src\\scripting\\ScriptRegistry.h\" />\n";
    // Relative path from engine root (where .vcxproj lives) to the project root.
    std::string projectFolderPrefix = fs::relative(FindProjectRoot(), FindEngineRoot()).string();
    std::replace(projectFolderPrefix.begin(), projectFolderPrefix.end(), '/', '\\');

    for (const ScriptSourceInfo& script : scripts) {
        std::string headerProjectPath = projectFolderPrefix + "\\" + script.projectHeaderPath;
        std::replace(headerProjectPath.begin(), headerProjectPath.end(), '/', '\\');
        project += "    <ClInclude Include=\"" + headerProjectPath + "\" />\n";
    }
    project += "  </ItemGroup>\n";
    project += "  <ItemGroup>\n";
    project += "    <ClCompile Include=\"src\\scripting\\ScriptDllRegistry.cpp\" />\n";
    for (const ScriptSourceInfo& script : scripts) {
        std::string sourceProjectPath = projectFolderPrefix + "\\" + script.projectSourcePath;
        std::replace(sourceProjectPath.begin(), sourceProjectPath.end(), '/', '\\');
        project += "    <ClCompile Include=\"" + sourceProjectPath + "\" />\n";
    }
    project += "  </ItemGroup>\n";
    project += "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n";
    project += "</Project>\n";
    return project;
}

std::string BuildScriptProjectFiltersSource(const std::vector<ScriptSourceInfo>& scripts) {
    std::string filters;
    filters += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    filters += "<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
    filters += "  <ItemGroup>\n";
    filters += "    <Filter Include=\"Source Files\"><UniqueIdentifier>{2F35C900-AF28-44AF-B467-4A14C061D985}</UniqueIdentifier><Extensions>cpp;c;cc;cxx</Extensions></Filter>\n";
    filters += "    <Filter Include=\"Header Files\"><UniqueIdentifier>{F31E8774-B24C-445D-B71C-F655107179AB}</UniqueIdentifier><Extensions>h;hpp</Extensions></Filter>\n";
    filters += "  </ItemGroup>\n";
    filters += "  <ItemGroup>\n";
    filters += "    <ClInclude Include=\"src\\scripting\\ObjectScript.h\"><Filter>Header Files</Filter></ClInclude>\n";
    filters += "    <ClInclude Include=\"src\\scripting\\ScriptRegistry.h\"><Filter>Header Files</Filter></ClInclude>\n";
    for (const ScriptSourceInfo& script : scripts) {
        std::string headerProjectPath = "Project\\" + script.projectHeaderPath;
        std::replace(headerProjectPath.begin(), headerProjectPath.end(), '/', '\\');
        filters += "    <ClInclude Include=\"" + headerProjectPath + "\"><Filter>Header Files</Filter></ClInclude>\n";
    }
    filters += "  </ItemGroup>\n";
    filters += "  <ItemGroup>\n";
    filters += "    <ClCompile Include=\"src\\scripting\\ScriptDllRegistry.cpp\"><Filter>Source Files</Filter></ClCompile>\n";
    for (const ScriptSourceInfo& script : scripts) {
        std::string sourceProjectPath = "Project\\" + script.projectSourcePath;
        std::replace(sourceProjectPath.begin(), sourceProjectPath.end(), '/', '\\');
        filters += "    <ClCompile Include=\"" + sourceProjectPath + "\"><Filter>Source Files</Filter></ClCompile>\n";
    }
    filters += "  </ItemGroup>\n";
    filters += "</Project>\n";
    return filters;
}

} // namespace

SceneEditor::SceneEditor() {
    std::cout << "[Player] SceneEditor: initializing defaults..." << std::endl;
    // Default dirty callback just marks the scene dirty; SetOnDirty() chains on top.
    onDirty_ = [this]() {
        sceneDirty_ = true;
        gameViewportRenderDirty_ = true;
    };
    InputManager defaultInputManager;
    defaultInputManager.EnsureDefaultProfiles();
    defaultInputManager.EnsureDefaultWheelSettingsProfiles();
    inputProfiles_ = defaultInputManager.GetInputProfiles();
    wheelSettingsProfiles_ = defaultInputManager.GetWheelSettingsProfiles();
    ResetPhysicsLayerSettings();
    // Load materials at startup
    std::cout << "[Player] SceneEditor: loading materials..." << std::endl;
    materialManager_.LoadAll();
    std::cout << "[Player] SceneEditor: refreshing project files..." << std::endl;
    RefreshProjectFiles();
    std::cout << "[Player] SceneEditor: loading project..." << std::endl;
    LoadProject();
    std::cout << "[Player] SceneEditor: reloading materials..." << std::endl;
    materialManager_.LoadAll();
    std::cout << "[Player] SceneEditor: refreshing project files again..." << std::endl;
    RefreshProjectFiles();
    std::string scriptLoadError;
    bool playerMode = false;
#if defined(_WIN32)
    char* playerModeValue = nullptr;
    size_t playerModeLength = 0;
    playerMode = _dupenv_s(&playerModeValue, &playerModeLength, "RACEMAN_PLAYER_MODE") == 0 &&
                 playerModeValue != nullptr &&
                 std::string(playerModeValue) == "1";
    free(playerModeValue);
#else
    const char* playerModeValue = std::getenv("RACEMAN_PLAYER_MODE");
    playerMode = playerModeValue != nullptr && std::string(playerModeValue) == "1";
#endif
    if (!playerMode) {
        std::cout << "[Player] SceneEditor: loading scripts..." << std::endl;
        LoadScriptAssembly(&scriptLoadError);
    } else {
        std::cout << "[Player] SceneEditor: deferred script load for player runtime." << std::endl;
    }
    std::cout << "[Player] SceneEditor: ready." << std::endl;
}

SceneEditor::~SceneEditor() {
    // If a background physics build is running, cancel it and wait for the thread to finish
    // before any member destruction occurs (the thread holds a raw pointer into pendingWorld).
    if (playModeLoad_.buildThread && playModeLoad_.buildThread->joinable()) {
        if (playModeLoad_.progress) {
            playModeLoad_.progress->cancelRequested.store(true);
        }
        playModeLoad_.buildThread->join();
    }
    if (playModeLoad_.scriptBuildThread && playModeLoad_.scriptBuildThread->joinable()) {
        playModeLoad_.scriptBuildThread->join();
    }

    // If the app is closed while in play mode, restore the pre-play snapshot so
    // the saved scene on disk reflects the authored state, not runtime transforms.
    if (scriptsRunning_ && hasPlayModeSnapshot_) {
        objects_ = playModeSnapshot_.objects;
        selectedIndex_ = playModeSnapshot_.selectedIndex;
        selectedIndices_ = playModeSnapshot_.selectedIndices;
        playModeSnapshot_ = {};
        hasPlayModeSnapshot_ = false;
        scriptsRunning_ = false;
        SaveCurrentScene();
    }
    ClearScriptRuntime();
    UnloadScriptAssembly();
    for (const auto& [filename, textureId] : componentIconTextures_) {
        (void)filename;
        if (textureId != 0) {
            glDeleteTextures(1, &textureId);
        }
    }
    for (const auto& [path, textureId] : materialTextureCache_) {
        (void)path;
        if (textureId != 0) {
            glDeleteTextures(1, &textureId);
        }
    }
}

void SceneEditor::SetConsole(Console* console) {
    console_ = console;
    if (console_) {
        console_->SetCommandHandler([this](const std::string& command) {
            HandleConsoleCommand(command);
            return true;
        });
        if (inputManager_ != nullptr) {
            inputManager_->SetLogCallback([console = console_](const std::string& message) {
                if (console != nullptr) {
                    console->AddLog(message);
                }
            });
        }
    }
}

void SceneEditor::SetInputManager(InputManager* inputManager) {
    inputManager_ = inputManager;
    if (inputManager_ == nullptr) {
        return;
    }

    inputManager_->SetInputProfiles(inputProfiles_);
    inputProfiles_ = inputManager_->GetInputProfiles();
    inputManager_->SetWheelSettingsProfiles(wheelSettingsProfiles_);
    wheelSettingsProfiles_ = inputManager_->GetWheelSettingsProfiles();

    if (console_ != nullptr) {
        inputManager_->SetLogCallback([console = console_](const std::string& message) {
            if (console != nullptr) {
                console->AddLog(message);
            }
        });
    }
}

bool SceneEditor::TryGetGameCamera(glm::mat4& outView, glm::mat4& outProj, float aspect, glm::vec4* outClearColor) const {
    const SceneObject* fallbackCamera = nullptr;
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const SceneObject& object = objects_[i];
        if (!IsObjectEffectivelyEnabled(i) || !object.hasCamera || !object.camera.enabled) {
            continue;
        }
        if (fallbackCamera == nullptr) {
            fallbackCamera = &object;
        }
        if (object.camera.isMain) {
            fallbackCamera = &object;
            break;
        }
    }

    if (fallbackCamera == nullptr) {
        return false;
    }

    const CameraComponent& camera = fallbackCamera->camera;
    const int cameraIndex = static_cast<int>(fallbackCamera - objects_.data());
    glm::mat4 worldMatrix = GetObjectDisplayWorldMatrix(cameraIndex);
    const float safeAspect = aspect > 0.0001f ? aspect : 1.0f;
    const float fov = (std::max)(1.0f, (std::min)(camera.fieldOfViewDegrees, 179.0f));
    const float nearClip = (std::max)(0.001f, camera.nearClip);
    const float farClip = (std::max)(nearClip + 0.001f, camera.farClip);
    const glm::vec3 position = glm::vec3(worldMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    const glm::vec3 forward = glm::normalize(glm::vec3(worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::vec3(worldMatrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));

    outView = glm::lookAt(position, position + forward, up);
    outProj = glm::perspective(glm::radians(fov), safeAspect, nearClip, farClip);
    if (outClearColor) {
        *outClearColor = camera.clearColor;
    }
    return true;
}
void SceneEditor::AddMeshPlane() {
    AddPlane();
}

void SceneEditor::AddEmptyObject() {
    PushUndoState();

    SceneObject object;
    object.id = MakeId("gameobject");
    object.name = "GameObject";
    object.type = "GameObject";
    object.hasMeshFilter = false;
    object.hasMeshRenderer = false;
    object.hasScriptComponent = false;

    objects_.push_back(std::move(object));
    Select(static_cast<int>(objects_.size()) - 1);
    renamingObjectIndex_ = -1;
    inspectMaterial_ = false;
    if (console_) {
        console_->AddLog("Added empty GameObject.");
    }
    if (onDirty_) onDirty_();
}

void SceneEditor::RenderUI(float deltaTime) {
    auto elapsedMs = [](double start) {
        return static_cast<float>((glfwGetTime() - start) * 1000.0);
    };

    frameTimings_ = {};

    double timingStart = glfwGetTime();
    HandleEditorShortcuts();
    frameTimings_.shortcutsMs = elapsedMs(timingStart);

    timingStart = glfwGetTime();
    TickPlayModeLoading();          // check if async physics build is done; finalize on main thread
    RenderPlayModeLoadingPopup();   // show modal progress UI while building (before dockspace so ID stack is clean)
    frameTimings_.playModePopupMs = elapsedMs(timingStart);

    timingStart = glfwGetTime();
    if (scriptsRunning_) {
        UpdateScripts(deltaTime);
        UpdateVehiclePhysics(deltaTime);
        UpdatePhysics(deltaTime);
        UpdateVehicles(deltaTime);
        UpdateCinemachine(deltaTime);
        UpdateAudio(deltaTime);
    } else {
        PreviewCinemachineInEditor();
    }
    frameTimings_.runtimeUpdatesMs = elapsedMs(timingStart);

    timingStart = glfwGetTime();
    RenderDockspaceHost();
    frameTimings_.dockspaceMs = elapsedMs(timingStart);

    timingStart = glfwGetTime();
    RenderScenePanel();
    frameTimings_.scenePanelMs = elapsedMs(timingStart);

    timingStart = glfwGetTime();
    RenderInspectorPanel();
    frameTimings_.inspectorMs = elapsedMs(timingStart);

    timingStart = glfwGetTime();
    RenderProjectPanel();
    frameTimings_.browserMs = elapsedMs(timingStart);

    timingStart = glfwGetTime();
    RenderViewportPanel();
    frameTimings_.viewportPanelMs = elapsedMs(timingStart);

    timingStart = glfwGetTime();
    RenderShaderGraphEditorWindow();
    RenderVehicleConfigEditorWindow();
    RenderVehicleSoundEditorWindow();
    RenderTrackGeneratorWindow();
    frameTimings_.auxiliaryWindowsMs = elapsedMs(timingStart);

    RenderStatusBar(deltaTime);
}

float SceneEditor::GetViewportAspect() const {
    const glm::vec2 size = activeViewport_ == SceneEditorActiveViewport::Game ? gameViewportSize_ : sceneViewportSize_;
    return size.y > 0.5f ? size.x / size.y : 1.0f;
}

namespace {
RendererViewport BuildRenderViewportFromLogicalRect(const glm::vec2& position,
                                                   const glm::vec2& size,
                                                   int framebufferWidth,
                                                   int framebufferHeight) {
    RendererViewport viewport{};

    if (size.x <= 1.0f || size.y <= 1.0f) {
        return viewport;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float displayWidth = io.DisplaySize.x > 0.0f ? io.DisplaySize.x : static_cast<float>(framebufferWidth);
    const float displayHeight = io.DisplaySize.y > 0.0f ? io.DisplaySize.y : static_cast<float>(framebufferHeight);
    const float scaleX = static_cast<float>(framebufferWidth) / displayWidth;
    const float scaleY = static_cast<float>(framebufferHeight) / displayHeight;

    viewport.x = static_cast<int>(std::round(position.x * scaleX));
    viewport.y = static_cast<int>(std::round(position.y * scaleY));
    viewport.width = (std::max)(1, static_cast<int>(std::round(size.x * scaleX)));
    viewport.height = (std::max)(1, static_cast<int>(std::round(size.y * scaleY)));
    return viewport;
}

} // namespace

RendererViewport SceneEditor::GetRenderViewport(int framebufferWidth, int framebufferHeight) const {
    return activeViewport_ == SceneEditorActiveViewport::Game
        ? GetGameRenderViewport(framebufferWidth, framebufferHeight)
        : GetSceneRenderViewport(framebufferWidth, framebufferHeight);
}

RendererViewport SceneEditor::GetSceneRenderViewport(int framebufferWidth, int framebufferHeight) const {
    if (sceneViewportSize_.x <= 1.0f || sceneViewportSize_.y <= 1.0f) {
        return {};
    }
    return BuildRenderViewportFromLogicalRect(sceneViewportPos_, sceneViewportSize_, framebufferWidth, framebufferHeight);
}

RendererViewport SceneEditor::GetGameRenderViewport(int framebufferWidth, int framebufferHeight) const {
    if (gameViewportSize_.x <= 1.0f || gameViewportSize_.y <= 1.0f) {
        return {};
    }
    return BuildRenderViewportFromLogicalRect(gameViewportPos_, gameViewportSize_, framebufferWidth, framebufferHeight);
}

bool SceneEditor::ContainsViewportPoint(float x, float y) const {
    return ContainsSceneViewportPoint(x, y) || ContainsGameViewportPoint(x, y);
}

bool SceneEditor::ContainsSceneViewportPoint(float x, float y) const {
    return sceneViewportSize_.x > 1.0f
        && sceneViewportSize_.y > 1.0f
        && x >= sceneViewportPos_.x
        && y >= sceneViewportPos_.y
        && x < sceneViewportPos_.x + sceneViewportSize_.x
        && y < sceneViewportPos_.y + sceneViewportSize_.y;
}

bool SceneEditor::ContainsGameViewportPoint(float x, float y) const {
    return gameViewportSize_.x > 1.0f
        && gameViewportSize_.y > 1.0f
        && x >= gameViewportPos_.x
        && y >= gameViewportPos_.y
        && x < gameViewportPos_.x + gameViewportSize_.x
        && y < gameViewportPos_.y + gameViewportSize_.y;
}

void SceneEditor::RenderViewportPanel() {
    viewportHovered_ = false;
    viewportFocused_ = false;
    sceneViewportHovered_ = false;
    sceneViewportFocused_ = false;
    gameViewportHovered_ = false;
    gameViewportFocused_ = false;
    sceneViewportPos_ = glm::vec2(0.0f);
    sceneViewportSize_ = glm::vec2(0.0f);
    gameViewportPos_ = glm::vec2(0.0f);
    gameViewportSize_ = glm::vec2(0.0f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
    const ImGuiWindowFlags viewportWindowFlags = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;
    auto gameAspectValue = [&]() -> float {
        switch (gameViewportAspectIndex_) {
        case 1: return 16.0f / 9.0f;
        case 2: return 16.0f / 10.0f;
        case 3: return 4.0f / 3.0f;
        case 4: return 1.0f;
        case 5: return 9.0f / 16.0f;
        default: return 0.0f;
        }
    };
    auto renderGameViewToolbar = [&]() {
        const char* aspectOptions[] = {"Free", "16:9", "16:10", "4:3", "1:1", "9:16"};
        bool toolbarChanged = false;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        ImGui::TextDisabled("Aspect");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(92.0f);
        toolbarChanged |= ImGui::Combo("##GameViewAspect", &gameViewportAspectIndex_, aspectOptions, IM_ARRAYSIZE(aspectOptions));
        ImGui::SameLine();
        ImGui::TextDisabled("Zoom");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(142.0f);
        toolbarChanged |= ImGui::SliderFloat("##GameViewZoom", &gameViewportZoomScale_, 0.50f, 2.00f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        if (getProfilerVisible_ && setProfilerVisible_) {
            const bool profilerOn = getProfilerVisible_();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button,
                profilerOn ? ImVec4(0.22f, 0.45f, 0.88f, 0.92f)
                           : ImVec4(0.13f, 0.15f, 0.19f, 0.82f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.52f, 0.95f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.18f, 0.38f, 0.80f, 1.00f));
            if (ImGui::Button("Stats##GameViewToolbar")) {
                setProfilerVisible_(!profilerOn);
            }
            ImGui::PopStyleColor(3);
        }
        ImGui::PopStyleVar();
        if (toolbarChanged) {
            gameViewportRenderDirty_ = true;
        }
        if (toolbarChanged || (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
            activeViewport_ = SceneEditorActiveViewport::Game;
        }
        ImGui::Separator();
    };
    auto renderViewportSurface = [&](const char* childName,
                                     SceneEditorActiveViewport viewportType,
                                     unsigned int textureId,
                                     glm::vec2& outPos,
                                     glm::vec2& outSize,
                                     bool& outHovered) {
        const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::BeginChild(childName, ImVec2(0.0f, 0.0f), false, childFlags)) {
            const ImVec2 contentMin = ImGui::GetCursorScreenPos();
            const ImVec2 contentAvail = ImGui::GetContentRegionAvail();
            ImVec2 imageMin = contentMin;
            ImVec2 imageSize = contentAvail;
            if (viewportType == SceneEditorActiveViewport::Game) {
                const float aspect = gameAspectValue();
                if (aspect > 0.0f && contentAvail.x > 1.0f && contentAvail.y > 1.0f) {
                    imageSize.x = contentAvail.x;
                    imageSize.y = imageSize.x / aspect;
                    if (imageSize.y > contentAvail.y) {
                        imageSize.y = contentAvail.y;
                        imageSize.x = imageSize.y * aspect;
                    }
                }
                const float zoom = (std::max)(0.50f, (std::min)(2.00f, gameViewportZoomScale_));
                imageSize.x *= zoom;
                imageSize.y *= zoom;
                imageMin.x = contentMin.x + (contentAvail.x - imageSize.x) * 0.5f;
                imageMin.y = contentMin.y + (contentAvail.y - imageSize.y) * 0.5f;
            }
            outPos = {imageMin.x, imageMin.y};
            outSize = {imageSize.x, imageSize.y};
            if (viewportType == SceneEditorActiveViewport::Game &&
                (std::fabs(imageSize.x - lastRenderedGameViewportSize_.x) > 0.5f ||
                 std::fabs(imageSize.y - lastRenderedGameViewportSize_.y) > 0.5f)) {
                gameViewportRenderDirty_ = true;
            }
            outHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
                && ImRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y)).Contains(ImGui::GetIO().MousePos);

            const float mouseWheel = ImGui::GetIO().MouseWheel;
            if (outHovered && mouseWheel != 0.0f) {
                if (viewportType == SceneEditorActiveViewport::Game) {
                    gameViewportZoomScale_ = (std::max)(0.50f, (std::min)(2.00f, gameViewportZoomScale_ + mouseWheel * 0.10f));
                    gameViewportRenderDirty_ = true;
                    activeViewport_ = SceneEditorActiveViewport::Game;
                } else if (viewportType == SceneEditorActiveViewport::Scene && hasEditorCameraMatrices_ && onEditorCameraViewChanged_) {
                    const glm::mat4 cameraWorld = glm::inverse(editorCameraView_);
                    const glm::vec3 position(cameraWorld[3]);
                    glm::vec3 forward = -glm::vec3(cameraWorld[2]);
                    const float length = glm::length(forward);
                    if (length > 0.0001f) {
                        forward /= length;
                        const float speed = ImGui::GetIO().KeyShift ? 4.0f : (ImGui::GetIO().KeyCtrl ? 0.5f : 1.5f);
                        const glm::vec3 newPosition = position + forward * (mouseWheel * speed);
                        glm::mat4 movedCameraWorld = cameraWorld;
                        movedCameraWorld[3] = glm::vec4(newPosition, 1.0f);
                        onEditorCameraViewChanged_(glm::inverse(movedCameraWorld));
                        activeViewport_ = SceneEditorActiveViewport::Scene;
                    }
                }
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 contentMax{contentMin.x + contentAvail.x, contentMin.y + contentAvail.y};
            const ImVec2 imageMax{imageMin.x + imageSize.x, imageMin.y + imageSize.y};
            if (viewportType == SceneEditorActiveViewport::Game) {
                drawList->AddRectFilled(contentMin, contentMax, IM_COL32(12, 14, 18, 255));
                ImGui::SetCursorScreenPos(imageMin);
            }
            if (textureId != 0) {
                ImGui::Image(static_cast<ImTextureID>(textureId),
                    imageSize,
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
            } else {
                drawList->AddRectFilled(imageMin, imageMax, IM_COL32(24, 28, 34, 255));
                ImGui::SetCursorScreenPos(imageMin);
                ImGui::Dummy(imageSize);
            }
            drawList->AddRect(imageMin, imageMax, IM_COL32(70, 90, 120, 180));

            if (viewportType == SceneEditorActiveViewport::Game) {
                glm::mat4 view;
                glm::mat4 proj;
                const float aspect = imageSize.y > 0.5f ? imageSize.x / imageSize.y : 1.0f;
                if (!TryGetGameCamera(view, proj, aspect)) {
                    const char* noCameraText = "No Camera";
                    const ImVec2 noCameraSize = ImGui::CalcTextSize(noCameraText);
                    drawList->AddText(ImVec2((imageMin.x + imageMax.x) * 0.5f - noCameraSize.x * 0.5f, (imageMin.y + imageMax.y) * 0.5f - 8.0f), IM_COL32(255, 200, 96, 255), noCameraText);
                }
                ImGui::SetCursorScreenPos(contentMin);
                ImGui::Dummy(contentAvail);
            }

            if (viewportType == SceneEditorActiveViewport::Scene) {
                UpdateImGuizmo();
            }

            if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && outHovered) {
                activeViewport_ = viewportType;
            }
        }
        ImGui::EndChild();
    };

    bool sceneWindowOpen = ImGui::Begin("Scene View", nullptr, viewportWindowFlags);
    if (sceneWindowOpen) {
        renderViewportSurface("SceneViewportSurface", SceneEditorActiveViewport::Scene, sceneViewportTextureId_, sceneViewportPos_, sceneViewportSize_, sceneViewportHovered_);
        HandleTrackDrawingInput();
        // Accept scene asset drops from the project browser onto the viewport.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kMeshAssetPayload)) {
                const char* droppedPath = static_cast<const char*>(payload->Data);
                if (droppedPath != nullptr && droppedPath[0] != '\0') {
                    ImportObj(droppedPath);
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectFilePayload)) {
                const char* droppedPath = static_cast<const char*>(payload->Data);
                if (droppedPath != nullptr && droppedPath[0] != '\0') {
                    if (IsPrefabAssetPath(droppedPath)) {
                        InstantiatePrefab(droppedPath);
                    } else if (IsMeshAssetPath(droppedPath)) {
                        ImportObj(droppedPath);
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kObjAssetPayload)) {
                const char* droppedPath = static_cast<const char*>(payload->Data);
                if (droppedPath != nullptr && droppedPath[0] != '\0') {
                    ImportObj(droppedPath);
                }
            }
            ImGui::EndDragDropTarget();
        }
    } else {
        sceneViewportSize_ = glm::vec2(0.0f);
    }
    ImGui::End();

    bool gameWindowOpen = ImGui::Begin("Game View", nullptr, viewportWindowFlags);
    if (gameWindowOpen) {
        renderGameViewToolbar();
        renderViewportSurface("GameViewportSurface", SceneEditorActiveViewport::Game, gameViewportTextureId_, gameViewportPos_, gameViewportSize_, gameViewportHovered_);
    } else {
        gameViewportSize_ = glm::vec2(0.0f);
    }
    ImGui::End();

    // ── Stats toggle button ────────────────────────────────────────────────
    const bool hasSceneViewport = sceneViewportSize_.x > 1.0f && sceneViewportSize_.y > 1.0f;
    const bool hasGameViewport = gameViewportSize_.x > 1.0f && gameViewportSize_.y > 1.0f;
    if (hasSceneViewport && hasGameViewport) {
        viewportPanelPos_ = glm::vec2(
            (std::min)(sceneViewportPos_.x, gameViewportPos_.x),
            (std::min)(sceneViewportPos_.y, gameViewportPos_.y));
        viewportPanelSize_ = glm::vec2(
            (std::max)(sceneViewportPos_.x + sceneViewportSize_.x, gameViewportPos_.x + gameViewportSize_.x) - viewportPanelPos_.x,
            (std::max)(sceneViewportPos_.y + sceneViewportSize_.y, gameViewportPos_.y + gameViewportSize_.y) - viewportPanelPos_.y);
    } else if (hasSceneViewport) {
        viewportPanelPos_ = sceneViewportPos_;
        viewportPanelSize_ = sceneViewportSize_;
    } else if (hasGameViewport) {
        viewportPanelPos_ = gameViewportPos_;
        viewportPanelSize_ = gameViewportSize_;
    } else {
        viewportPanelPos_ = glm::vec2(0.0f);
        viewportPanelSize_ = glm::vec2(0.0f);
        activeViewport_ = SceneEditorActiveViewport::None;
    }

    if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        && !sceneViewportHovered_
        && !gameViewportHovered_) {
        activeViewport_ = SceneEditorActiveViewport::None;
    }

    viewportHovered_ = sceneViewportHovered_ || gameViewportHovered_;
    sceneViewportFocused_ = activeViewport_ == SceneEditorActiveViewport::Scene;
    gameViewportFocused_ = activeViewport_ == SceneEditorActiveViewport::Game;
    viewportFocused_ = sceneViewportFocused_ || gameViewportFocused_;
    ImGui::PopStyleColor();
}

void SceneEditor::RenderDockspaceHost() {
    constexpr float statusBarHeight = 24.0f;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, (std::max)(1.0f, viewport->WorkSize.y - statusBarHeight)), ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("EditorDockspaceHost", nullptr, hostFlags)) {
        const ImGuiID dockspaceId = ImGui::GetID("EditorDockspace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        if (!dockLayoutInitialized_) {
            dockLayoutInitialized_ = true;
            const char* dockedWindows[] = {"Scene", "Inspector", "Browser", "Scene View", "Game View"};
            bool hasSavedDockLayout = false;
            for (const char* windowName : dockedWindows) {
                if (ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(windowName))) {
                    if (settings->DockId != 0) {
                        hasSavedDockLayout = true;
                        break;
                    }
                }
            }

            if (!hasSavedDockLayout) {
                ImGui::DockBuilderRemoveNode(dockspaceId);
                ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dockspaceId, ImVec2(viewport->WorkSize.x, (std::max)(1.0f, viewport->WorkSize.y - statusBarHeight)));

                ImGuiID centerId = dockspaceId;
                ImGuiID leftId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.18f, nullptr, &centerId);
                ImGuiID rightId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.2682927f, nullptr, &centerId);
                ImGuiID bottomId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, 0.28f, nullptr, &centerId);
                ImGuiID gameViewId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.5f, nullptr, &centerId);

                ImGui::DockBuilderDockWindow("Scene", leftId);
                ImGui::DockBuilderDockWindow("Inspector", rightId);
                ImGui::DockBuilderDockWindow("Browser", bottomId);
                ImGui::DockBuilderDockWindow("Scene View", centerId);
                ImGui::DockBuilderDockWindow("Game View", gameViewId);
                ImGui::DockBuilderFinish(dockspaceId);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void SceneEditor::RenderStatusBar(float deltaTime) {
    constexpr float statusBarHeight = 24.0f;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 pos(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - statusBarHeight);
    const ImVec2 size(viewport->WorkSize.x, statusBarHeight);

    std::string sceneName = fs::path(savePath_).filename().string();
    if (sceneName.empty()) {
        sceneName = "Untitled Scene";
    }

    const PhysicsBuildProgress* progress = GetPhysicsBuildProgress();
    if (progress != nullptr) {
        const std::string task = progress->GetTask();
        if (task.find("Loaded:") == 0) {
            lastPhysicsCacheStatus_ = "Loaded from cache";
        } else if (task.find("Cooking:") == 0) {
            lastPhysicsCacheStatus_ = "Cooking";
        } else if (task.find("Cache check:") == 0) {
            lastPhysicsCacheStatus_ = "Checking cache";
        } else if (playModeLoad_.phase == PlayModeLoadState::Phase::BuildingPhysics) {
            lastPhysicsCacheStatus_ = "Cooking";
        }
    } else if (!scriptsRunning_ && playModeLoad_.phase == PlayModeLoadState::Phase::Idle) {
        lastPhysicsCacheStatus_ = "Ready";
    } else if (lastPhysicsCacheStatus_ == "Cooking" || lastPhysicsCacheStatus_ == "Checking cache") {
        lastPhysicsCacheStatus_ = "Ready";
    }

    const float fps = ImGui::GetIO().Framerate > 0.0f ? ImGui::GetIO().Framerate : (deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f);
    const float frameMs = deltaTime * 1000.0f;
    const int selectedCount = static_cast<int>(selectedIndices_.size());

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.060f, 0.070f, 0.085f, 1.00f));
    if (ImGui::Begin("##EditorStatusBar", nullptr, flags)) {
        ImGui::TextUnformatted(sceneName.c_str());
        ImGui::SameLine();
        ImGui::TextColored(sceneDirty_ ? ImVec4(1.0f, 0.70f, 0.25f, 1.0f) : ImVec4(0.48f, 0.82f, 0.58f, 1.0f),
            "%s", sceneDirty_ ? "Unsaved" : "Saved");
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Selected: %d", selectedCount);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Physics cache: %s", lastPhysicsCacheStatus_.c_str());

        const char* perfFormat = "FPS %.0f  %.2f ms";
        const float perfWidth = ImGui::CalcTextSize("FPS 999  99.99 ms").x;
        ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x, ImGui::GetWindowContentRegionMax().x - perfWidth));
        ImGui::Text(perfFormat, fps, frameMs);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void SceneEditor::HandleEditorShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (IsCtrlSPressed()) {
        SaveActiveAsset();
        return;
    }
    if (io.WantTextInput) {
        return;
    }
    const bool vehicleConfigShortcutTarget = showVehicleConfigEditor_ &&
        (vehicleConfigEditorFocused_ || vehicleConfigEditorHovered_);
    const bool vehicleSoundShortcutTarget = showVehicleSoundEditor_ &&
        (vehicleSoundEditorFocused_ || vehicleSoundEditorHovered_);
    const bool shaderGraphShortcutTarget = showShaderGraphEditor_ &&
        (shaderGraphEditorFocused_ || shaderGraphEditorHovered_);
    const bool materialShortcutTarget = inspectMaterial_ &&
        (inspectorPanelFocused_ || inspectorPanelHovered_);
    if (IsCtrlZPressed()) {
        if (shaderGraphShortcutTarget) {
            UndoShaderGraph();
        } else if (vehicleConfigShortcutTarget) {
            UndoVehicleConfig();
        } else if (vehicleSoundShortcutTarget) {
            UndoVehicleSound();
        } else if (materialShortcutTarget) {
            UndoMaterial();
        } else {
            Undo();
        }
        return;
    }
    if (IsCtrlYPressed()) {
        if (shaderGraphShortcutTarget) {
            RedoShaderGraph();
        } else if (vehicleConfigShortcutTarget) {
            RedoVehicleConfig();
        } else if (vehicleSoundShortcutTarget) {
            RedoVehicleSound();
        } else if (materialShortcutTarget) {
            RedoMaterial();
        } else {
            Redo();
        }
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
        const bool wantsComponentCopy = (inspectorPanelHovered_ || inspectorPanelFocused_) &&
            !inspectorKeyboardTargetComponentKey_.empty() &&
            selectedIndex_ >= 0 &&
            selectedIndex_ < static_cast<int>(objects_.size());
        if (wantsComponentCopy) {
            CopyInspectorComponentToClipboard(selectedIndex_, inspectorKeyboardTargetComponentType_);
        } else {
            CopySelectedObjectsToClipboard();
        }
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
        const bool wantsComponentPaste = (inspectorPanelHovered_ || inspectorPanelFocused_) &&
            componentClipboard_.hasValue &&
            (!inspectorKeyboardTargetComponentKey_.empty() || !selectedIndices_.empty());
        if (wantsComponentPaste) {
            std::vector<int> targetIndices = selectedIndices_;
            if (targetIndices.empty() && selectedIndex_ >= 0) {
                targetIndices.push_back(selectedIndex_);
            }
            PasteInspectorComponentFromClipboard(targetIndices, componentClipboard_.type);
        } else {
            PasteObjectsFromClipboard();
        }
        return;
    }

    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        const bool wantsSceneDelete =
            selectedIndex_ >= 0 &&
            selectedIndex_ < static_cast<int>(objects_.size()) &&
            activeViewport_ == SceneEditorActiveViewport::Scene &&
            !(inspectorPanelHovered_ || inspectorPanelFocused_) &&
            !(scenePanelHovered_ || scenePanelFocused_) &&
            !shaderGraphShortcutTarget &&
            !vehicleConfigShortcutTarget &&
            !vehicleSoundShortcutTarget;
        if (wantsSceneDelete) {
            DeleteSelectedObject();
            return;
        }
    }

    if (!io.KeyCtrl && !io.KeyAlt && !io.MouseDown[1]) {
        const bool wantsSceneGizmoShortcut =
            selectedIndex_ >= 0 &&
            activeViewport_ == SceneEditorActiveViewport::Scene &&
            !(inspectorPanelHovered_ || inspectorPanelFocused_);
        if (wantsSceneGizmoShortcut) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) {
                gizmoMode_ = GizmoMode::Move;
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E)) {
                gizmoMode_ = GizmoMode::Rotate;
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                gizmoMode_ = GizmoMode::Scale;
                return;
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            if ((inspectorPanelHovered_ || inspectorPanelFocused_) && !inspectorKeyboardTargetComponentKey_.empty()) {
                pendingInspectorToggleComponentKey_ = inspectorKeyboardTargetComponentKey_;
                return;
            }
            if ((scenePanelHovered_ || scenePanelFocused_) && !hierarchyKeyboardTargetObjectId_.empty()) {
                pendingHierarchyToggleObjectId_ = hierarchyKeyboardTargetObjectId_;
                return;
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            gizmoMode_ = GizmoMode::Move;
        } else if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            gizmoMode_ = GizmoMode::Rotate;
        } else if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            gizmoMode_ = GizmoMode::Scale;
        }
    }
}

bool SceneEditor::CopyInspectorComponentToClipboard(int objectIndex, SceneInspectorComponentType type) {
    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects_.size())) {
        return false;
    }
    if (!HasInspectorComponent(objects_[objectIndex], type)) {
        return false;
    }

    componentClipboard_.hasValue = true;
    componentClipboard_.type = type;
    componentClipboard_.sourceObject = objects_[objectIndex];
    if (console_) {
        console_->AddLog(std::string("Copied ") + InspectorComponentTypeToString(type) + " component.");
    }
    return true;
}

bool SceneEditor::PasteInspectorComponentFromClipboard(const std::vector<int>& targetIndices, SceneInspectorComponentType targetType) {
    if (!componentClipboard_.hasValue || componentClipboard_.type != targetType) {
        if (console_) {
            console_->AddWarning("No matching component clipboard data to paste.");
        }
        return false;
    }

    std::vector<int> validTargets;
    validTargets.reserve(targetIndices.size());
    for (int index : targetIndices) {
        if (index >= 0 && index < static_cast<int>(objects_.size())) {
            validTargets.push_back(index);
        }
    }
    if (validTargets.empty()) {
        return false;
    }

    PushUndoState();
    for (int index : validTargets) {
        SceneObject& target = objects_[index];
        switch (targetType) {
        case SceneInspectorComponentType::Transform:
            target.transform = componentClipboard_.sourceObject.transform;
            break;
        case SceneInspectorComponentType::MeshFilter:
            target.hasMeshFilter = true;
            target.meshFilter = componentClipboard_.sourceObject.meshFilter;
            break;
        case SceneInspectorComponentType::MeshRenderer:
            target.hasMeshRenderer = true;
            target.meshRenderer = componentClipboard_.sourceObject.meshRenderer;
            break;
        case SceneInspectorComponentType::Script:
            target.hasScriptComponent = true;
            target.scriptComponent = componentClipboard_.sourceObject.scriptComponent;
            break;
        case SceneInspectorComponentType::Rigidbody:
            target.hasCharacterController = false;
            target.characterController = CharacterControllerComponent{};
            target.hasRigidbody = true;
            target.rigidbody = componentClipboard_.sourceObject.rigidbody;
            break;
        case SceneInspectorComponentType::Vehicle:
            target.hasVehicle = true;
            target.vehicle = componentClipboard_.sourceObject.vehicle;
            break;
        case SceneInspectorComponentType::CharacterController:
            target.hasRigidbody = false;
            target.rigidbody = RigidbodyComponent{};
            target.hasCharacterController = true;
            target.characterController = componentClipboard_.sourceObject.characterController;
            break;
        case SceneInspectorComponentType::Collider:
            target.hasBoxCollider = componentClipboard_.sourceObject.hasBoxCollider;
            target.hasSphereCollider = componentClipboard_.sourceObject.hasSphereCollider;
            target.hasCapsuleCollider = componentClipboard_.sourceObject.hasCapsuleCollider;
            target.hasPlaneCollider = componentClipboard_.sourceObject.hasPlaneCollider;
            target.hasMeshCollider = componentClipboard_.sourceObject.hasMeshCollider;
            target.boxCollider = componentClipboard_.sourceObject.boxCollider;
            target.sphereCollider = componentClipboard_.sourceObject.sphereCollider;
            target.capsuleCollider = componentClipboard_.sourceObject.capsuleCollider;
            target.planeCollider = componentClipboard_.sourceObject.planeCollider;
            target.meshCollider = componentClipboard_.sourceObject.meshCollider;
            break;
        case SceneInspectorComponentType::Camera:
            target.hasCamera = true;
            target.camera = componentClipboard_.sourceObject.camera;
            break;
        case SceneInspectorComponentType::Cinemachine:
            target.hasCinemachine = true;
            target.cinemachine = componentClipboard_.sourceObject.cinemachine;
            break;
        case SceneInspectorComponentType::Light:
            target.hasLight = true;
            target.light = componentClipboard_.sourceObject.light;
            break;
        case SceneInspectorComponentType::AudioListener:
            target.hasAudioListener = true;
            target.audioListener = componentClipboard_.sourceObject.audioListener;
            break;
        case SceneInspectorComponentType::AudioSource:
            target.hasAudioSource = true;
            target.audioSource = componentClipboard_.sourceObject.audioSource;
            break;
        case SceneInspectorComponentType::VehicleSound:
            target.hasVehicleSound = true;
            target.vehicleSound = componentClipboard_.sourceObject.vehicleSound;
            break;
        }
        SyncInspectorComponentOrder(target);
    }

    if (onDirty_) {
        onDirty_();
    }
    if (console_) {
        console_->AddLog(std::string("Pasted ") + InspectorComponentTypeToString(targetType) + " component.");
    }
    return true;
}

void SceneEditor::CopySelectedObjectsToClipboard() {
    NormalizeSelection();
    if (selectedIndices_.empty()) {
        return;
    }

    std::unordered_set<std::string> selectedIds;
    for (int index : selectedIndices_) {
        if (index >= 0 && index < static_cast<int>(objects_.size())) {
            selectedIds.insert(objects_[index].id);
        }
    }

    std::vector<std::string> rootIds;
    std::vector<SceneObject> clipboardObjects;
    for (int index = 0; index < static_cast<int>(objects_.size()); ++index) {
        if (selectedIds.find(objects_[index].id) == selectedIds.end()) {
            continue;
        }
        if (!objects_[index].parentId.empty() && selectedIds.find(objects_[index].parentId) != selectedIds.end()) {
            continue;
        }

        rootIds.push_back(objects_[index].id);
        std::unordered_set<std::string> subtreeIds{objects_[index].id};
        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (const SceneObject& object : objects_) {
                if (subtreeIds.find(object.id) != subtreeIds.end()) {
                    continue;
                }
                if (!object.parentId.empty() && subtreeIds.find(object.parentId) != subtreeIds.end()) {
                    subtreeIds.insert(object.id);
                    expanded = true;
                }
            }
        }

        for (const SceneObject& object : objects_) {
            if (subtreeIds.find(object.id) != subtreeIds.end()) {
                clipboardObjects.push_back(object);
            }
        }
    }

    if (clipboardObjects.empty()) {
        return;
    }

    objectClipboard_.hasValue = true;
    objectClipboard_.objects = std::move(clipboardObjects);
    objectClipboard_.rootObjectIds = std::move(rootIds);
    if (console_) {
        console_->AddLog("Copied object selection.");
    }
}

void SceneEditor::PasteObjectsFromClipboard() {
    if (!objectClipboard_.hasValue || objectClipboard_.objects.empty()) {
        return;
    }

    PushUndoState();

    std::unordered_set<std::string> rootIds(objectClipboard_.rootObjectIds.begin(), objectClipboard_.rootObjectIds.end());
    std::unordered_map<std::string, std::string> idRemap;
    idRemap.reserve(objectClipboard_.objects.size());
    for (const SceneObject& sourceObject : objectClipboard_.objects) {
        idRemap.emplace(sourceObject.id, MakeId("gameobject"));
    }

    const std::size_t insertStart = objects_.size();
    std::vector<std::string> pastedRootIds;
    pastedRootIds.reserve(objectClipboard_.rootObjectIds.size());

    for (const SceneObject& sourceObject : objectClipboard_.objects) {
        SceneObject copy = sourceObject;
        copy.id = idRemap[sourceObject.id];
        const auto parentIt = idRemap.find(sourceObject.parentId);
        if (parentIt != idRemap.end()) {
            copy.parentId = parentIt->second;
        }
        if (rootIds.find(sourceObject.id) != rootIds.end()) {
            if (!IsObjectNameCopySuffix(copy.name)) {
                copy.name += " Copy";
            }
            pastedRootIds.push_back(copy.id);
        }
        RemapVehicleObjectReferences(copy, idRemap);
        objects_.push_back(std::move(copy));
    }

    selectedIndices_.clear();
    selectedIndex_ = -1;
    for (int index = static_cast<int>(insertStart); index < static_cast<int>(objects_.size()); ++index) {
        if (std::find(pastedRootIds.begin(), pastedRootIds.end(), objects_[index].id) != pastedRootIds.end()) {
            selectedIndices_.push_back(index);
            selectedIndex_ = index;
        }
    }
    NormalizeSelection();

    if (onDirty_) {
        onDirty_();
    }
    if (console_) {
        console_->AddLog("Pasted object selection.");
    }
}

void SceneEditor::AddPlane() {
    AddBuiltInPrimitiveObject("Plane");
}

void SceneEditor::AddTrackGeneratorObject() {
    PushUndoState();
    SceneObject object;
    object.id = MakeId("track_generator");
    object.name = "Track Generator";
    object.type = "GameObject";
    object.hasMeshFilter = false;
    object.hasMeshRenderer = false;
    object.hasScriptComponent = false;
    object.hasTrackGenerator = true;
    object.trackGenerator = TrackGeneratorComponent{};
    object.inspectorComponentOrder = DefaultInspectorComponentOrder();
    objects_.push_back(std::move(object));
    Select(static_cast<int>(objects_.size()) - 1);
    OpenTrackGenerator("");
    if (onDirty_) onDirty_();
    if (console_) console_->AddLog("Added Track Generator object.");
}

void SceneEditor::AddBuiltInPrimitiveObject(const std::string& meshType) {
    try {
        PushUndoState();
        SceneObject object;
        object.id = MakeId("mesh");
        object.name = meshType;
        object.type = "GameObject";
        if (meshType == "Plane") {
            object.transform.scale = {10.0f, 1.0f, 10.0f};
        }
        object.meshRenderer.materialId = "pbr_default";
        if (!ConfigureBuiltInPrimitive(object, meshType, builtInPrimitiveMeshes_)) {
            throw std::runtime_error("built-in primitive mesh");
        }
        if (meshType == "Plane") {
            AddDefaultPlaneColliderToPlane(object);
        }
        objects_.push_back(std::move(object));
        Select(static_cast<int>(objects_.size()) - 1);
        if (onDirty_) onDirty_();
        if (console_) {
            console_->AddLog(std::string("Added ") + meshType + ": " + objects_.back().id + " (" + objects_.back().name + ")");
        }
    } catch (...) {
        if (console_) {
            console_->AddWarning("Failed to add built-in primitive object.");
        }
    }
}

namespace {

std::string SanitizeTrackAssetName(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(c);
        } else if (c == '_' || c == '-' || c == ' ') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string("track") : out;
}

bool ProjectMouseToXZPlane(const glm::mat4& view,
                           const glm::mat4& proj,
                           const glm::vec2& viewportPos,
                           const glm::vec2& viewportSize,
                           const ImVec2& mouse,
                           glm::vec3& outPoint) {
    if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f) {
        return false;
    }
    const float localX = mouse.x - viewportPos.x;
    const float localY = mouse.y - viewportPos.y;
    if (localX < 0.0f || localY < 0.0f || localX > viewportSize.x || localY > viewportSize.y) {
        return false;
    }
    const float ndcX = (2.0f * localX) / viewportSize.x - 1.0f;
    const float ndcY = 1.0f - (2.0f * localY) / viewportSize.y;
    const glm::mat4 invViewProj = glm::inverse(proj * view);
    glm::vec4 nearPoint = invViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(nearPoint.w) < 0.0001f || std::abs(farPoint.w) < 0.0001f) {
        return false;
    }
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    const glm::vec3 origin = glm::vec3(nearPoint);
    const glm::vec3 direction = glm::normalize(glm::vec3(farPoint - nearPoint));
    if (std::abs(direction.y) < 0.0001f) {
        return false;
    }
    const float t = -origin.y / direction.y;
    if (t < 0.0f) {
        return false;
    }
    outPoint = origin + direction * t;
    outPoint.y = 0.0f;
    return true;
}

bool ProjectWorldToSceneScreen(const glm::mat4& view,
                               const glm::mat4& proj,
                               const glm::vec2& viewportPos,
                               const glm::vec2& viewportSize,
                               const glm::vec3& point,
                               ImVec2& out) {
    const glm::vec4 clip = proj * view * glm::vec4(point, 1.0f);
    if (std::abs(clip.w) < 0.0001f) {
        return false;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    out.x = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
    out.y = viewportPos.y + (0.5f - ndc.y * 0.5f) * viewportSize.y;
    return ndc.z >= -1.0f && ndc.z <= 1.0f;
}

} // namespace

void SceneEditor::OpenTrackGenerator(const std::string& trackPath) {
    showTrackGenerator_ = true;
    selectedTrackPointIndex_ = -1;
    draggingTrackPoint_ = false;
    trackGeneratorStatus_.clear();

    const std::string normalizedPath = NormalizeSlashes(trackPath);
    if (normalizedPath.empty()) {
        inspectedTrackPath_.clear();
        trackGeneratorMode_ = TrackGeneratorMode::Preset;
        trackSource_ = TrackSource{};
        trackSource_.closed = true;
        trackSource_.controlPoints = BuildTrackPresetPoints(trackSource_.presetType,
                                                            trackPresetLength_,
                                                            trackPresetWidth_,
                                                            trackPresetRadius_,
                                                            trackPresetPointCount_);
        return;
    }

    TrackSource loaded;
    std::string error;
    if (LoadTrackSource(ProjectAssetPathToAbsolute(normalizedPath).string(), loaded, &error)) {
        inspectedTrackPath_ = normalizedPath;
        trackSource_ = std::move(loaded);
        selectedProjectFile_ = normalizedPath;
        selectedProjectDirectory_ = ParentProjectDirectory(normalizedPath);
        trackGeneratorStatus_ = "Loaded " + normalizedPath;
    } else {
        inspectedTrackPath_ = normalizedPath;
        trackSource_ = TrackSource{};
        trackSource_.name = fs::path(normalizedPath).stem().string();
        trackGeneratorStatus_ = "Failed to load track: " + error;
    }
}

void SceneEditor::RenderTrackGeneratorWindow() {
    if (!showTrackGenerator_) {
        return;
    }

    if (!ImGui::Begin("Track Generator", &showTrackGenerator_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    auto markTrackBakeDirty = [&]() {
        trackBakeDirty_ = true;
        trackBakeDirtyTime_ = ImGui::GetTime();
    };
    auto renderMaterialSelector = [&](const char* label, const char* id, std::string& materialId) -> bool {
        bool changed = false;
        if (materialId.empty()) {
            materialId = "pbr_default";
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo(id, materialId.c_str())) {
            if (ImGui::Selectable("pbr_default", materialId == "pbr_default")) {
                materialId = "pbr_default";
                changed = true;
            }
            for (const std::string& file : projectFiles_) {
                if (!IsMaterialAssetPath(file)) {
                    continue;
                }
                const std::string idValue = MaterialIdFromAssetPath(file);
                const bool selected = materialId == idValue;
                if (ImGui::Selectable(idValue.c_str(), selected)) {
                    materialId = idValue;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        return changed;
    };

    char nameBuffer[128]{};
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", trackSource_.name.c_str());
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::InputText("Track Name", nameBuffer, sizeof(nameBuffer))) {
        trackSource_.name = nameBuffer;
    }
    ImGui::SameLine();
    const char* modePreview = trackGeneratorMode_ == TrackGeneratorMode::Draw ? "Draw" : "Preset";
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::BeginCombo("Mode", modePreview)) {
        if (ImGui::Selectable("Preset", trackGeneratorMode_ == TrackGeneratorMode::Preset)) {
            trackGeneratorMode_ = TrackGeneratorMode::Preset;
        }
        if (ImGui::Selectable("Draw", trackGeneratorMode_ == TrackGeneratorMode::Draw)) {
            trackGeneratorMode_ = TrackGeneratorMode::Draw;
            trackDrawAddTool_ = false;
        }
        ImGui::EndCombo();
    }

    bool settingsChanged = false;
    settingsChanged |= ImGui::Checkbox("Closed Track", &trackSource_.closed);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    settingsChanged |= ImGui::DragFloat("Road Width", &trackSource_.roadWidth, 0.25f, 1.0f, 100.0f, "%.1f m");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    settingsChanged |= ImGui::DragFloat("Shoulder", &trackSource_.shoulderWidth, 0.25f, 0.0f, 50.0f, "%.1f m");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    settingsChanged |= ImGui::DragFloat("Resolution", &trackSource_.segmentResolution, 0.1f, 0.25f, 20.0f, "%.1f m");
    settingsChanged |= renderMaterialSelector("Road Material", "##TrackRoadMaterial", trackSource_.materialId);
    settingsChanged |= renderMaterialSelector("Shoulder Material", "##TrackShoulderMaterial", trackSource_.shoulderMaterialId);
    settingsChanged |= ImGui::Checkbox("Realtime Bake", &trackRealtimeBake_);
    if (settingsChanged) {
        markTrackBakeDirty();
    }
    if (!inspectedTrackPath_.empty()) {
        ImGui::TextDisabled("Source: %s", inspectedTrackPath_.c_str());
    }
    if (!trackSource_.bakedMeshPath.empty()) {
        ImGui::TextDisabled("Road Mesh: %s", trackSource_.bakedMeshPath.c_str());
    }
    if (!trackSource_.bakedShoulderMeshPath.empty()) {
        ImGui::TextDisabled("Shoulder Mesh: %s", trackSource_.bakedShoulderMeshPath.c_str());
    }

    ImGui::Separator();
    if (trackGeneratorMode_ == TrackGeneratorMode::Preset) {
        const char* presetLabels[] = {"Oval", "Rounded Rectangle", "S-Curve Road"};
        int presetIndex = trackSource_.presetType == TrackPresetType::RoundedRectangle ? 1
                        : trackSource_.presetType == TrackPresetType::SCurve ? 2 : 0;
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::Combo("Preset", &presetIndex, presetLabels, IM_ARRAYSIZE(presetLabels))) {
            trackSource_.presetType = presetIndex == 1 ? TrackPresetType::RoundedRectangle
                                      : presetIndex == 2 ? TrackPresetType::SCurve
                                                         : TrackPresetType::Oval;
        }
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Length", &trackPresetLength_, 1.0f, 20.0f, 1000.0f, "%.0f m");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Width", &trackPresetWidth_, 1.0f, 10.0f, 1000.0f, "%.0f m");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Radius", &trackPresetRadius_, 0.5f, 1.0f, 250.0f, "%.1f m");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragInt("Point Count", &trackPresetPointCount_, 1.0f, 4, 128);
        if (ImGui::Button("Apply Preset")) {
            trackSource_.closed = trackSource_.presetType != TrackPresetType::SCurve;
            trackSource_.controlPoints = BuildTrackPresetPoints(trackSource_.presetType,
                                                                trackPresetLength_,
                                                                trackPresetWidth_,
                                                                trackPresetRadius_,
                                                                trackPresetPointCount_);
            selectedTrackPointIndex_ = -1;
            trackGeneratorStatus_ = "Preset points generated.";
            markTrackBakeDirty();
        }
    } else {
        if (ImGui::Selectable("Draw##TrackDrawTool", trackDrawAddTool_, 0, ImVec2(64.0f, 0.0f))) {
            trackDrawAddTool_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Selectable("Hand##TrackHandTool", !trackDrawAddTool_, 0, ImVec2(64.0f, 0.0f))) {
            trackDrawAddTool_ = false;
        }
        ImGui::TextDisabled(trackDrawAddTool_
            ? "Draw tool: left click Scene View to add points. Click the first point from the end to close."
            : "Hand tool: select and drag existing points.");
        ImGui::TextDisabled("Delete removes, Esc deselects, B bakes, arrows select. Insert adds the preview point.");
        if (trackDrawPreviewValid_) {
            ImGui::TextDisabled("Preview point: X %.2f  Z %.2f", trackDrawPreviewPoint_.x, trackDrawPreviewPoint_.z);
        } else {
            ImGui::TextDisabled("Preview point: move mouse over Scene View");
        }
    }

    ImGui::SeparatorText("Control Points");
    if (ImGui::SmallButton("Add Point")) {
        trackSource_.controlPoints.push_back(glm::vec3{0.0f});
        selectedTrackPointIndex_ = static_cast<int>(trackSource_.controlPoints.size()) - 1;
        markTrackBakeDirty();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        trackSource_.controlPoints.clear();
        selectedTrackPointIndex_ = -1;
        markTrackBakeDirty();
    }

    const float tableHeight = (std::min)(260.0f, 34.0f + static_cast<float>(trackSource_.controlPoints.size()) * 26.0f);
    if (ImGui::BeginTable("TrackControlPoints", 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, tableHeight))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("X");
        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Z");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(trackSource_.controlPoints.size()); ++i) {
            glm::vec3& point = trackSource_.controlPoints[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(std::to_string(i).c_str(), selectedTrackPointIndex_ == i, ImGuiSelectableFlags_SpanAllColumns)) {
                selectedTrackPointIndex_ = i;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##x", &point.x, 0.25f, -10000.0f, 10000.0f, "%.2f")) {
                markTrackBakeDirty();
            }
            ImGui::TableSetColumnIndex(2);
            point.y = 0.0f;
            ImGui::TextDisabled("0.00");
            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##z", &point.z, 0.25f, -10000.0f, 10000.0f, "%.2f")) {
                markTrackBakeDirty();
            }
            ImGui::TableSetColumnIndex(4);
            if (ImGui::SmallButton("Remove")) {
                trackSource_.controlPoints.erase(trackSource_.controlPoints.begin() + i);
                if (selectedTrackPointIndex_ >= static_cast<int>(trackSource_.controlPoints.size())) {
                    selectedTrackPointIndex_ = static_cast<int>(trackSource_.controlPoints.size()) - 1;
                }
                markTrackBakeDirty();
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::Button("Bake Track To Scene")) {
        BakeTrackToScene();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Source")) {
        std::string error;
        std::string sourcePath = inspectedTrackPath_;
        if (sourcePath.empty()) {
            const std::string base = SanitizeTrackAssetName(trackSource_.name);
            sourcePath = "assets/tracks/" + base + "/" + base + ".track.json";
        }
        if (SaveTrackSource(ProjectAssetPathToAbsolute(sourcePath).string(), trackSource_, &error)) {
            inspectedTrackPath_ = NormalizeSlashes(sourcePath);
            selectedProjectFile_ = inspectedTrackPath_;
            selectedProjectDirectory_ = ParentProjectDirectory(inspectedTrackPath_);
            RefreshProjectFiles();
            trackGeneratorStatus_ = "Saved " + inspectedTrackPath_;
        } else {
            trackGeneratorStatus_ = "Save failed: " + error;
        }
    }
    if (!trackGeneratorStatus_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", trackGeneratorStatus_.c_str());
    }
    if (trackRealtimeBake_ && trackBakeDirty_ && ImGui::GetTime() - trackBakeDirtyTime_ > 0.35) {
        BakeTrackToScene(true);
        trackBakeDirty_ = false;
    }

    ImGui::End();
}

bool SceneEditor::BakeTrackToScene(bool realtime) {
    TrackMeshData roadMesh;
    TrackMeshData shoulderMesh;
    std::string error;
    if (!BuildTrackRoadMesh(trackSource_, roadMesh, &error)) {
        trackGeneratorStatus_ = "Bake failed: " + error;
        if (console_ && !realtime) console_->AddError(trackGeneratorStatus_);
        return false;
    }
    const bool hasShoulder = trackSource_.shoulderWidth > 0.001f;
    if (hasShoulder && !BuildTrackShoulderMesh(trackSource_, shoulderMesh, &error)) {
        trackGeneratorStatus_ = "Shoulder bake failed: " + error;
        if (console_ && !realtime) console_->AddError(trackGeneratorStatus_);
        return false;
    }

    const std::string base = SanitizeTrackAssetName(trackSource_.name);
    trackSource_.name = base;
    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path folder = assetsRoot / "tracks" / base;
    const fs::path sourceAbs = folder / (base + ".track.json");
    const fs::path roadObjAbs = folder / (base + "_road.obj");
    const fs::path shoulderObjAbs = folder / (base + "_shoulder.obj");
    const std::string sourceProjectPath = ToProjectAssetPath(sourceAbs, assetsRoot);
    const std::string roadObjProjectPath = ToProjectAssetPath(roadObjAbs, assetsRoot);
    const std::string shoulderObjProjectPath = ToProjectAssetPath(shoulderObjAbs, assetsRoot);
    trackSource_.bakedMeshPath = roadObjProjectPath;
    trackSource_.bakedShoulderMeshPath = hasShoulder ? shoulderObjProjectPath : "";
    if (trackSource_.materialId.empty()) {
        trackSource_.materialId = "pbr_default";
    }
    if (trackSource_.shoulderMaterialId.empty()) {
        trackSource_.shoulderMaterialId = trackSource_.materialId;
    }

    if (!SaveTrackSource(sourceAbs.string(), trackSource_, &error)) {
        trackGeneratorStatus_ = "Source save failed: " + error;
        if (console_ && !realtime) console_->AddError(trackGeneratorStatus_);
        return false;
    }
    if (!BakeTrackObj(roadObjAbs.string(), roadMesh, &error)) {
        trackGeneratorStatus_ = "Road OBJ bake failed: " + error;
        if (console_ && !realtime) console_->AddError(trackGeneratorStatus_);
        return false;
    }
    if (hasShoulder && !BakeTrackObj(shoulderObjAbs.string(), shoulderMesh, &error)) {
        trackGeneratorStatus_ = "Shoulder OBJ bake failed: " + error;
        if (console_ && !realtime) console_->AddError(trackGeneratorStatus_);
        return false;
    }

    bool pushedBakeUndo = false;
    auto pushBakeUndo = [&]() {
        if (!realtime && !pushedBakeUndo) {
            PushUndoState();
            pushedBakeUndo = true;
        }
    };

    int ownerIndex = -1;
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size()) && objects_[selectedIndex_].hasTrackGenerator) {
        ownerIndex = selectedIndex_;
    } else {
        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            if (!objects_[i].hasTrackGenerator) {
                continue;
            }
            const std::string sourcePath = NormalizeSlashes(objects_[i].trackGenerator.trackSourcePath);
            if ((!inspectedTrackPath_.empty() && sourcePath == NormalizeSlashes(inspectedTrackPath_)) ||
                (!sourceProjectPath.empty() && sourcePath == NormalizeSlashes(sourceProjectPath)) ||
                sourcePath.empty()) {
                ownerIndex = i;
                break;
            }
        }
    }
    if (ownerIndex < 0) {
        pushBakeUndo();
        SceneObject owner;
        owner.id = MakeId("track_generator");
        owner.name = trackSource_.name + " Generator";
        owner.type = "GameObject";
        owner.hasMeshFilter = false;
        owner.hasMeshRenderer = false;
        owner.hasScriptComponent = false;
        owner.hasTrackGenerator = true;
        owner.trackGenerator = TrackGeneratorComponent{};
        owner.trackGenerator.trackSourcePath = NormalizeSlashes(sourceProjectPath);
        owner.inspectorComponentOrder = DefaultInspectorComponentOrder();
        objects_.push_back(std::move(owner));
        ownerIndex = static_cast<int>(objects_.size()) - 1;
    }
    const std::string ownerId = ownerIndex >= 0 ? objects_[ownerIndex].id : std::string{};

    auto upsertTrackMeshObject = [&](const std::string& objProjectPath,
                                     const std::string& objectName,
                                     const std::string& materialId,
                                     const std::string& preferredObjectId) -> int {
        std::string resolvedPath;
        std::shared_ptr<::Model> model;
        std::vector<ImportedMeshInfo> infos;
        if (!TryLoadMeshAsset(objProjectPath, resolvedPath, model, infos) || infos.empty()) {
            return -1;
        }

        int targetIndex = -1;
        if (!preferredObjectId.empty()) {
            targetIndex = FindObjectIndexById(preferredObjectId);
        }
        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            if (targetIndex >= 0) {
                break;
            }
            if (objects_[i].hasMeshFilter && NormalizeSlashes(objects_[i].meshFilter.sourcePath) == NormalizeSlashes(objProjectPath)) {
                targetIndex = i;
                break;
            }
        }

        if (targetIndex < 0) {
            SceneObject object;
            object.id = MakeId("track");
            object.name = objectName;
            object.type = "GameObject";
            object.parentId = ownerId;
            object.hasScriptComponent = false;
            object.hasMeshFilter = true;
            object.hasMeshRenderer = true;
            object.hasMeshCollider = true;
            object.meshRenderer.materialId = materialId;
            object.meshCollider.mode = MeshColliderMode::TriangleMesh;
            object.inspectorComponentOrder = DefaultInspectorComponentOrder();
            ApplyMeshInfoToSceneObject(object, infos.front(), model);
            object.meshFilter.sourcePath = objProjectPath;
            objects_.push_back(std::move(object));
            targetIndex = static_cast<int>(objects_.size()) - 1;
        } else {
            SceneObject& object = objects_[targetIndex];
            object.name = objectName;
            object.parentId = ownerId;
            object.hasMeshFilter = true;
            object.hasMeshRenderer = true;
            object.hasMeshCollider = true;
            object.meshRenderer.materialId = materialId;
            object.meshCollider.mode = MeshColliderMode::TriangleMesh;
            ApplyMeshInfoToSceneObject(object, infos.front(), model);
            object.meshFilter.sourcePath = objProjectPath;
            SyncInspectorComponentOrder(object);
        }
        return targetIndex;
    };

    if (!realtime) {
        pushBakeUndo();
    }
    const std::string preferredRoadId = ownerIndex >= 0 ? objects_[ownerIndex].trackGenerator.roadObjectId : std::string{};
    const std::string preferredShoulderId = ownerIndex >= 0 ? objects_[ownerIndex].trackGenerator.shoulderObjectId : std::string{};
    const int roadIndex = upsertTrackMeshObject(roadObjProjectPath, trackSource_.name + " Road", trackSource_.materialId, preferredRoadId);
    const int shoulderIndex = hasShoulder
        ? upsertTrackMeshObject(shoulderObjProjectPath, trackSource_.name + " Shoulder", trackSource_.shoulderMaterialId, preferredShoulderId)
        : -1;
    if (roadIndex < 0 || (hasShoulder && shoulderIndex < 0)) {
        trackGeneratorStatus_ = "Baked OBJ, but failed to load generated mesh into scene.";
        if (console_ && !realtime) console_->AddError(trackGeneratorStatus_);
        RefreshProjectFiles();
        return false;
    }

    if (ownerIndex >= 0 && ownerIndex < static_cast<int>(objects_.size())) {
        SceneObject& owner = objects_[ownerIndex];
        owner.trackGenerator.trackSourcePath = NormalizeSlashes(sourceProjectPath);
        owner.trackGenerator.roadObjectId = roadIndex >= 0 ? objects_[roadIndex].id : std::string{};
        owner.trackGenerator.shoulderObjectId = shoulderIndex >= 0 ? objects_[shoulderIndex].id : std::string{};
    }
    if (!realtime) {
        Select(ownerIndex >= 0 ? ownerIndex : roadIndex);
    }
    inspectedTrackPath_ = NormalizeSlashes(sourceProjectPath);
    selectedProjectFile_ = inspectedTrackPath_;
    selectedProjectDirectory_ = ParentProjectDirectory(inspectedTrackPath_);
    RefreshProjectFiles();
    if (onDirty_) onDirty_();
    trackGeneratorStatus_ = realtime ? "Realtime baked." : "Baked track: " + roadObjProjectPath;
    if (console_ && !realtime) console_->AddLog(trackGeneratorStatus_);
    return true;
}

void SceneEditor::HandleTrackDrawingInput() {
    if (!showTrackGenerator_ || trackGeneratorMode_ != TrackGeneratorMode::Draw ||
        !hasEditorCameraMatrices_ ||
        sceneViewportSize_.x <= 1.0f || sceneViewportSize_.y <= 1.0f) {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImVec2 clipMin(sceneViewportPos_.x, sceneViewportPos_.y);
    const ImVec2 clipMax(sceneViewportPos_.x + sceneViewportSize_.x, sceneViewportPos_.y + sceneViewportSize_.y);
    drawList->PushClipRect(clipMin, clipMax, true);
    std::vector<ImVec2> screens(trackSource_.controlPoints.size());
    std::vector<bool> visible(trackSource_.controlPoints.size(), false);
    for (int i = 0; i < static_cast<int>(trackSource_.controlPoints.size()); ++i) {
        ImVec2 screen;
        if (ProjectWorldToSceneScreen(editorCameraView_, editorCameraProj_, sceneViewportPos_, sceneViewportSize_, trackSource_.controlPoints[static_cast<std::size_t>(i)], screen)) {
            screens[static_cast<std::size_t>(i)] = screen;
            visible[static_cast<std::size_t>(i)] = true;
        }
    }
    const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.70f, 1.0f, 1.0f));
    const ImU32 selectedColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.62f, 0.18f, 1.0f));
    const ImU32 previewColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.42f, 0.92f, 0.62f, 0.95f));
    for (int i = 0; i + 1 < static_cast<int>(screens.size()); ++i) {
        if (visible[static_cast<std::size_t>(i)] && visible[static_cast<std::size_t>(i + 1)]) {
            drawList->AddLine(screens[static_cast<std::size_t>(i)], screens[static_cast<std::size_t>(i + 1)], lineColor, 2.0f);
        }
    }
    if (trackSource_.closed && screens.size() > 2 && visible.front() && visible.back()) {
        drawList->AddLine(screens.back(), screens.front(), lineColor, 2.0f);
    }
    for (int i = 0; i < static_cast<int>(screens.size()); ++i) {
        if (!visible[static_cast<std::size_t>(i)]) continue;
        const bool selected = i == selectedTrackPointIndex_;
        drawList->AddCircleFilled(screens[static_cast<std::size_t>(i)], selected ? 6.0f : 4.5f, selected ? selectedColor : lineColor);
    }

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    glm::vec3 previewPoint{0.0f};
    const bool hasPreviewPoint = ProjectMouseToXZPlane(editorCameraView_, editorCameraProj_, sceneViewportPos_, sceneViewportSize_, mouse, previewPoint);
    trackDrawPreviewValid_ = hasPreviewPoint;
    trackDrawPreviewPoint_ = previewPoint;
    ImVec2 previewScreen;
    if (hasPreviewPoint && ProjectWorldToSceneScreen(editorCameraView_, editorCameraProj_, sceneViewportPos_, sceneViewportSize_, previewPoint, previewScreen)) {
        drawList->AddCircle(previewScreen, 5.0f, previewColor, 18, 2.0f);
        int anchorIndex = trackDrawAddTool_
            ? (selectedTrackPointIndex_ >= 0 ? selectedTrackPointIndex_ : static_cast<int>(trackSource_.controlPoints.size()) - 1)
            : -1;
        if (trackDrawAddTool_ && anchorIndex >= 0 && anchorIndex < static_cast<int>(screens.size()) && visible[static_cast<std::size_t>(anchorIndex)]) {
            drawList->AddLine(screens[static_cast<std::size_t>(anchorIndex)], previewScreen, previewColor, 1.5f);
        }
    }
    drawList->PopClipRect();

    int hoveredPoint = -1;
    float bestDistSq = 100.0f;
    for (int i = 0; i < static_cast<int>(screens.size()); ++i) {
        if (!visible[static_cast<std::size_t>(i)]) continue;
        const float dx = mouse.x - screens[static_cast<std::size_t>(i)].x;
        const float dy = mouse.y - screens[static_cast<std::size_t>(i)].y;
        const float distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            hoveredPoint = i;
        }
    }

    if (!sceneViewportHovered_) {
        return;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredPoint >= 0) {
            const int lastIndex = static_cast<int>(trackSource_.controlPoints.size()) - 1;
            if (trackDrawAddTool_ && !trackSource_.closed && hoveredPoint == 0 && lastIndex >= 2 && selectedTrackPointIndex_ == lastIndex) {
                trackSource_.closed = true;
                selectedTrackPointIndex_ = 0;
                draggingTrackPoint_ = false;
                trackBakeDirty_ = true;
                trackBakeDirtyTime_ = ImGui::GetTime();
            } else {
                selectedTrackPointIndex_ = hoveredPoint;
                draggingTrackPoint_ = true;
            }
        } else {
            if (trackDrawAddTool_ && hasPreviewPoint) {
                trackSource_.controlPoints.push_back(previewPoint);
                selectedTrackPointIndex_ = static_cast<int>(trackSource_.controlPoints.size()) - 1;
                trackSource_.closed = false;
                draggingTrackPoint_ = false;
                trackBakeDirty_ = true;
                trackBakeDirtyTime_ = ImGui::GetTime();
            }
        }
    }
    if (draggingTrackPoint_ && selectedTrackPointIndex_ >= 0 &&
        selectedTrackPointIndex_ < static_cast<int>(trackSource_.controlPoints.size()) &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        glm::vec3 point;
        if (ProjectMouseToXZPlane(editorCameraView_, editorCameraProj_, sceneViewportPos_, sceneViewportSize_, mouse, point)) {
            trackSource_.controlPoints[static_cast<std::size_t>(selectedTrackPointIndex_)] = point;
            trackBakeDirty_ = true;
            trackBakeDirtyTime_ = ImGui::GetTime();
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        draggingTrackPoint_ = false;
    }
    if (selectedTrackPointIndex_ >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        trackSource_.controlPoints.erase(trackSource_.controlPoints.begin() + selectedTrackPointIndex_);
        selectedTrackPointIndex_ = (std::min)(selectedTrackPointIndex_, static_cast<int>(trackSource_.controlPoints.size()) - 1);
        draggingTrackPoint_ = false;
        trackBakeDirty_ = true;
        trackBakeDirtyTime_ = ImGui::GetTime();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        selectedTrackPointIndex_ = -1;
        draggingTrackPoint_ = false;
    }
    if (!trackSource_.controlPoints.empty() && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        selectedTrackPointIndex_ = (selectedTrackPointIndex_ + 1) % static_cast<int>(trackSource_.controlPoints.size());
    }
    if (!trackSource_.controlPoints.empty() && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        selectedTrackPointIndex_ = selectedTrackPointIndex_ <= 0
            ? static_cast<int>(trackSource_.controlPoints.size()) - 1
            : selectedTrackPointIndex_ - 1;
    }
    if (hasPreviewPoint && ImGui::IsKeyPressed(ImGuiKey_Insert)) {
        trackSource_.controlPoints.push_back(previewPoint);
        selectedTrackPointIndex_ = static_cast<int>(trackSource_.controlPoints.size()) - 1;
        trackSource_.closed = false;
        trackBakeDirty_ = true;
        trackBakeDirtyTime_ = ImGui::GetTime();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_B)) {
        BakeTrackToScene(false);
        trackBakeDirty_ = false;
    }
}


void SceneEditor::AddCameraObject() {
    PushUndoState();

    bool hasAnyCamera = false;
    for (const SceneObject& object : objects_) {
        if (object.hasCamera) {
            hasAnyCamera = true;
            break;
        }
    }

    SceneObject cameraObject;
    cameraObject.id = MakeId("camera");
    cameraObject.name = "Camera";
    cameraObject.type = "GameObject";
    cameraObject.transform.position = {0.0f, 2.0f, 5.0f};
    cameraObject.transform.rotationEuler = {-15.0f, 0.0f, 0.0f};
    cameraObject.hasMeshFilter = false;
    cameraObject.hasMeshRenderer = false;
    cameraObject.hasScriptComponent = false;
    cameraObject.hasCamera = true;
    cameraObject.camera = CameraComponent{};
    cameraObject.camera.isMain = !hasAnyCamera;

    objects_.push_back(std::move(cameraObject));
    selectedIndex_ = static_cast<int>(objects_.size()) - 1;
    selectedIndices_ = {selectedIndex_};
    inspectMaterial_ = false;
    renamingObjectIndex_ = -1;
    if (console_) {
        console_->AddLog("Added Camera object.");
    }
    if (onDirty_) onDirty_();
}

void SceneEditor::AddLightObject(LightType type) {
    PushUndoState();

    SceneObject lightObject;
    lightObject.id = MakeId("light");
    lightObject.type = "GameObject";
    lightObject.hasMeshFilter = false;
    lightObject.hasMeshRenderer = false;
    lightObject.hasScriptComponent = false;
    lightObject.hasLight = true;
    lightObject.light = LightComponent{};
    lightObject.light.type = type;

    if (type == LightType::Directional) {
        lightObject.name = "Directional Light";
        lightObject.transform.rotationEuler = {-45.0f, 35.0f, 0.0f};
        lightObject.light.intensity = 1.5f;
        lightObject.light.range = 100.0f;
    } else if (type == LightType::Spot) {
        lightObject.name = "Spot Light";
        lightObject.transform.position = {0.0f, 3.0f, 3.0f};
        lightObject.transform.rotationEuler = {-35.0f, 0.0f, 0.0f};
        lightObject.light.intensity = 4.0f;
        lightObject.light.range = 12.0f;
        lightObject.light.spotAngleDegrees = 35.0f;
    } else {
        lightObject.name = "Point Light";
        lightObject.transform.position = {0.0f, 2.0f, 0.0f};
        lightObject.light.intensity = 3.0f;
        lightObject.light.range = 10.0f;
    }

    objects_.push_back(std::move(lightObject));
    selectedIndex_ = static_cast<int>(objects_.size()) - 1;
    selectedIndices_ = {selectedIndex_};
    inspectMaterial_ = false;
    renamingObjectIndex_ = -1;
    if (console_) {
        console_->AddLog("Added " + objects_[selectedIndex_].name + " object.");
    }
    if (onDirty_) onDirty_();
}
std::vector<std::pair<std::string, std::string>> SceneEditor::ScanProjectScripts() const {
    std::vector<std::pair<std::string, std::string>> result;
    const std::vector<ScriptSourceInfo> scripts = FindCompleteScripts(FindAssetsRoot());
    result.reserve(scripts.size());
    for (const ScriptSourceInfo& info : scripts) {
        result.emplace_back(info.name, info.projectSourcePath);
    }
    return result;
}

bool SceneEditor::AttachScriptToSelected(const std::string& scriptName, const std::string& scriptPath) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size()) || scriptName.empty()) {
        return false;
    }

    PushUndoState();
    SceneObject& obj = objects_[selectedIndex_];
    obj.hasScriptComponent = true;
    ObjectScriptAttachment attachment;
    attachment.enabled = true;
    attachment.scriptName = scriptName;
    attachment.scriptPath = NormalizeSlashes(scriptPath);
    SyncAttachmentScriptFields(attachment);
    obj.scriptComponent.attachments.push_back(std::move(attachment));
    if (scriptsRunning_) {
        RebuildScriptRuntime();
    }
    if (console_) {
        console_->AddLog("Attached script " + scriptName + " to " + obj.name);
    }
    if (onDirty_) onDirty_();
    return true;
}

void SceneEditor::SyncScriptProjectFiles(bool logResult) {
    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path engineRoot = EngineRootPath();
    const fs::path projectPath = engineRoot / "Project Raceman.vcxproj";
    const fs::path filtersPath = engineRoot / "Project Raceman.vcxproj.filters";
    const fs::path scriptProjectPath = engineRoot / "ProjectScripts.vcxproj";
    const fs::path scriptFiltersPath = engineRoot / "ProjectScripts.vcxproj.filters";
    const fs::path solutionPath = engineRoot / "Project Raceman.sln";

    const std::vector<ScriptSourceInfo> scripts = FindCompleteScripts(assetsRoot);

    RemoveProjectEntriesUnderScripts(projectPath);
    RemoveProjectEntriesUnderScripts(filtersPath);

    WriteTextFile(engineRoot / "src" / "scripting" / "ScriptDllRegistry.cpp", BuildScriptRegistrySource(scripts));
    WriteTextFile(scriptProjectPath, BuildScriptProjectSource(scripts));
    WriteTextFile(scriptFiltersPath, BuildScriptProjectFiltersSource(scripts));

    std::string solutionText;
    if (ReadTextFile(solutionPath, solutionText) && !ContainsText(solutionText, "ProjectScripts.vcxproj")) {
        const std::string projectEntry =
            "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"ProjectScripts\", \"ProjectScripts.vcxproj\", \"{B02D47B2-2BAA-4F5F-8E4C-4A0A61A6618F}\"\n"
            "EndProject\n";
        const std::size_t globalPos = solutionText.find("Global\n");
        if (globalPos != std::string::npos) {
            solutionText.insert(globalPos, projectEntry);
        }
        const std::string configMarker = "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
        const std::size_t configPos = solutionText.find(configMarker);
        if (configPos != std::string::npos) {
            const std::size_t insertPos = configPos + configMarker.size();
            const std::string configs =
                "\t\t{B02D47B2-2BAA-4F5F-8E4C-4A0A61A6618F}.Debug|x64.ActiveCfg = Debug|x64\n"
                "\t\t{B02D47B2-2BAA-4F5F-8E4C-4A0A61A6618F}.Debug|x64.Build.0 = Debug|x64\n"
                "\t\t{B02D47B2-2BAA-4F5F-8E4C-4A0A61A6618F}.Release|x64.ActiveCfg = Release|x64\n"
                "\t\t{B02D47B2-2BAA-4F5F-8E4C-4A0A61A6618F}.Release|x64.Build.0 = Release|x64\n";
            solutionText.insert(insertPos, configs);
        }
        WriteTextFile(solutionPath, solutionText);
    }

    if (logResult && console_) {
        console_->AddLog("Synced script DLL project with " + std::to_string(scripts.size()) + " script(s).");
    }
}

bool SceneEditor::CreateScriptAsset(const std::string& requestedName, bool attachToSelected) {
    const std::string className = SanitizeScriptClassName(requestedName);
    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path scriptsDir = ProjectAssetPathToAbsolute(selectedProjectDirectory_);
    if (!IsUnderPath(scriptsDir, assetsRoot)) {
        if (console_) {
            console_->AddError("Script creation blocked outside assets: " + className);
        }
        return false;
    }
    const fs::path headerPath = scriptsDir / (className + ".h");
    const fs::path sourcePath = scriptsDir / (className + ".cpp");
    std::string objectScriptInclude = NormalizeSlashes(fs::relative(EngineRootPath() / "src" / "scripting" / "ObjectScript.h", scriptsDir).string());

    if (fs::exists(assetsRoot)) {
        for (const auto& entry : fs::recursive_directory_iterator(assetsRoot)) {
            const std::string extension = ToLowerCopy(entry.path().extension().string());
            if (entry.is_regular_file() && entry.path().stem().string() == className && (extension == ".h" || extension == ".cpp")) {
                if (console_) {
                    console_->AddError("Script already exists: " + className);
                }
                return false;
            }
        }
    }

    if (fs::exists(headerPath) || fs::exists(sourcePath)) {
        if (console_) {
            console_->AddError("Script already exists: " + className);
        }
        return false;
    }

    const std::string header =
        "#pragma once\n\n"
        "#include \"" + objectScriptInclude + "\"\n\n"
        "namespace raceman::scripts {\n\n"
        "class " + className + " : public raceman::IObjectScript {\n"
        "public:\n"
        "    void OnStart(raceman::ObjectScriptContext& context) override;\n"
        "    void OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) override;\n"
        "};\n\n"
        "} // namespace raceman::scripts\n";

    const std::string source =
        "#include \"" + className + ".h\"\n\n"
        "namespace raceman::scripts {\n\n"
        "void " + className + "::OnStart(raceman::ObjectScriptContext& context) {\n"
        "    context.Log(\"started\");\n"
        "    if (context.HasCamera()) {\n"
        "        context.Camera().SetFieldOfView(60.0f);\n"
        "    }\n"
        "}\n\n"
        "void " + className + "::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {\n"
        "    if (context.HasCamera()) {\n"
        "        auto cam = context.Camera();\n"
        "        cam.SetFieldOfView(cam.GetFieldOfView() + 5.0f * deltaTime);\n"
        "    }\n"
        "    (void)deltaTime;\n"
        "}\n\n"
        "} // namespace raceman::scripts\n";

    try {
        WriteTextFile(headerPath, header);
        WriteTextFile(sourcePath, source);
        std::cout << "[SceneEditor] Created script header: " << headerPath.string() << '\n';
        std::cout << "[SceneEditor] Created script source: " << sourcePath.string() << '\n';

        SyncScriptProjectFiles();
        std::cout << "[SceneEditor] Added script to project: " << className << '\n';

        const std::string scriptPath = ToProjectAssetPath(sourcePath, assetsRoot);
        if (attachToSelected && selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size())) {
            SceneObject& obj = objects_[selectedIndex_];
            PushUndoState();
            obj.hasScriptComponent = true;
            ObjectScriptAttachment attachment;
            attachment.enabled = true;
            attachment.scriptName = className;
            attachment.scriptPath = scriptPath;
            SyncAttachmentScriptFields(attachment);
            obj.scriptComponent.attachments.push_back(std::move(attachment));
            if (scriptsRunning_) {
                RebuildScriptRuntime();
            }
            if (onDirty_) onDirty_();
            Save(savePath_);
            std::cout << "[SceneEditor] Attached pending script " << className << " to " << obj.name << ".\n";
        }

        if (console_) {
            console_->AddLog("Created C++ script " + className + ": " + scriptPath);
            if (attachToSelected && selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size())) {
                console_->AddLog("Attached pending script " + className + " to " + objects_[selectedIndex_].name + " and saved the scene. It will build when Play starts.");
            } else {
                console_->AddLog("Press Play to build/load " + className + " before running it.");
            }
        }
        RefreshProjectFiles();
        return true;
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to create C++ script: " + className);
        }
        return false;
    }
}



void SceneEditor::Select(int index) {
    if (index >= 0 && index < static_cast<int>(objects_.size())) {
        selectedIndex_ = index;
        selectedIndices_ = {index};
        inspectMaterial_ = false;
    }
}

void SceneEditor::ToggleSelect(int index) {
    if (index < 0 || index >= static_cast<int>(objects_.size())) {
        return;
    }

    inspectMaterial_ = false;
    const auto it = std::find(selectedIndices_.begin(), selectedIndices_.end(), index);
    if (it != selectedIndices_.end()) {
        selectedIndices_.erase(it);
        if (selectedIndex_ == index) {
            selectedIndex_ = selectedIndices_.empty() ? -1 : selectedIndices_.back();
        }
    } else {
        selectedIndices_.push_back(index);
        selectedIndex_ = index;
    }
    NormalizeSelection();
}

bool SceneEditor::IsSelected(int index) const {
    return std::find(selectedIndices_.begin(), selectedIndices_.end(), index) != selectedIndices_.end();
}

int SceneEditor::FindObjectIndexById(const std::string& id) const {
    if (id.empty()) {
        return -1;
    }
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (objects_[i].id == id) {
            return i;
        }
    }
    return -1;
}

bool SceneEditor::IsObjectEffectivelyEnabled(int index) const {
    if (index < 0 || index >= static_cast<int>(objects_.size())) {
        return false;
    }

    int currentIndex = index;
    std::vector<std::string> visited;
    while (currentIndex >= 0 && currentIndex < static_cast<int>(objects_.size())) {
        const SceneObject& object = objects_[currentIndex];
        if (!object.enabled) {
            return false;
        }
        if (object.parentId.empty()) {
            return true;
        }
        if (std::find(visited.begin(), visited.end(), object.id) != visited.end()) {
            return false;
        }
        visited.push_back(object.id);
        currentIndex = FindObjectIndexById(object.parentId);
    }

    return true;
}

bool SceneEditor::IsDescendantOf(const std::string& objectId, const std::string& potentialAncestorId) const {
    int currentIndex = FindObjectIndexById(objectId);
    while (currentIndex >= 0 && currentIndex < static_cast<int>(objects_.size())) {
        const std::string parentId = objects_[currentIndex].parentId;
        if (parentId.empty()) {
            return false;
        }
        if (parentId == potentialAncestorId) {
            return true;
        }
        currentIndex = FindObjectIndexById(parentId);
    }
    return false;
}

bool SceneEditor::MoveObjectInHierarchy(int childIndex, int newParentIndex, int insertAfterIndex) {
    if (childIndex < 0 || childIndex >= static_cast<int>(objects_.size())) {
        return false;
    }
    if (newParentIndex >= static_cast<int>(objects_.size())) {
        return false;
    }
    if (insertAfterIndex >= static_cast<int>(objects_.size())) {
        return false;
    }
    if (newParentIndex == childIndex || insertAfterIndex == childIndex) {
        return false;
    }

    const std::string childId = objects_[childIndex].id;
    const std::string newParentId = newParentIndex >= 0 ? objects_[newParentIndex].id : std::string();
    if (!newParentId.empty() && IsDescendantOf(newParentId, childId)) {
        if (console_) {
            console_->AddWarning("Cannot move an object under its own child.");
        }
        return false;
    }

    std::unordered_set<std::string> movingIds{childId};
    bool added = true;
    while (added) {
        added = false;
        for (const SceneObject& object : objects_) {
            if (movingIds.find(object.id) != movingIds.end()) {
                continue;
            }
            if (!object.parentId.empty() && movingIds.find(object.parentId) != movingIds.end()) {
                movingIds.insert(object.id);
                added = true;
            }
        }
    }

    if (insertAfterIndex >= 0 && movingIds.find(objects_[insertAfterIndex].id) != movingIds.end()) {
        return false;
    }

    const std::string selectedId = (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size()))
        ? objects_[selectedIndex_].id
        : std::string();
    std::vector<std::string> selectedIds;
    selectedIds.reserve(selectedIndices_.size());
    for (int index : selectedIndices_) {
        if (index >= 0 && index < static_cast<int>(objects_.size())) {
            selectedIds.push_back(objects_[index].id);
        }
    }

    const glm::mat4 childWorldMatrix = GetObjectWorldMatrix(childIndex);

    PushUndoState();
    SceneObject& childObject = objects_[childIndex];
    childObject.parentId = newParentId;
    if (newParentIndex >= 0) {
        const glm::mat4 parentWorld = GetObjectWorldMatrix(newParentIndex);
        childObject.transform = TransformFromMatrix(glm::inverse(parentWorld) * childWorldMatrix);
    } else {
        childObject.transform = TransformFromMatrix(childWorldMatrix);
    }

    std::unordered_set<std::string> insertAfterSubtreeIds;
    if (insertAfterIndex >= 0) {
        insertAfterSubtreeIds.insert(objects_[insertAfterIndex].id);
        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (const SceneObject& object : objects_) {
                if (insertAfterSubtreeIds.find(object.id) != insertAfterSubtreeIds.end()) {
                    continue;
                }
                if (!object.parentId.empty() && insertAfterSubtreeIds.find(object.parentId) != insertAfterSubtreeIds.end()) {
                    insertAfterSubtreeIds.insert(object.id);
                    expanded = true;
                }
            }
        }
    }

    std::vector<SceneObject> movingObjects;
    std::vector<SceneObject> remainingObjects;
    movingObjects.reserve(movingIds.size());
    remainingObjects.reserve(objects_.size() - movingIds.size());
    for (SceneObject& object : objects_) {
        if (movingIds.find(object.id) != movingIds.end()) {
            movingObjects.push_back(std::move(object));
        } else {
            remainingObjects.push_back(std::move(object));
        }
    }

    std::size_t insertPosition = remainingObjects.size();
    if (insertAfterIndex >= 0) {
        insertPosition = 0;
        for (std::size_t i = 0; i < remainingObjects.size(); ++i) {
            if (insertAfterSubtreeIds.find(remainingObjects[i].id) != insertAfterSubtreeIds.end()) {
                insertPosition = i + 1;
            }
        }
    }

    remainingObjects.insert(remainingObjects.begin() + static_cast<std::ptrdiff_t>(insertPosition),
                            std::make_move_iterator(movingObjects.begin()),
                            std::make_move_iterator(movingObjects.end()));
    objects_ = std::move(remainingObjects);

    selectedIndices_.clear();
    selectedIndex_ = -1;
    for (int index = 0; index < static_cast<int>(objects_.size()); ++index) {
        if (objects_[index].id == selectedId) {
            selectedIndex_ = index;
        }
        if (std::find(selectedIds.begin(), selectedIds.end(), objects_[index].id) != selectedIds.end()) {
            selectedIndices_.push_back(index);
        }
    }
    NormalizeSelection();
    if (selectedIndex_ < 0) {
        selectedIndex_ = FindObjectIndexById(childId);
        NormalizeSelection();
    }
    if (onDirty_) onDirty_();
    return true;
}

void SceneEditor::SetParent(int childIndex, int parentIndex) {
    if (childIndex < 0 || childIndex >= static_cast<int>(objects_.size())) {
        return;
    }
    if (parentIndex >= static_cast<int>(objects_.size())) {
        return;
    }
    if (parentIndex == childIndex) {
        return;
    }

    const std::string newParentId = parentIndex >= 0 ? objects_[parentIndex].id : std::string();
    if (!newParentId.empty() && IsDescendantOf(newParentId, objects_[childIndex].id)) {
        if (console_) {
            console_->AddWarning("Cannot parent an object under its own child.");
        }
        return;
    }
    if (objects_[childIndex].parentId == newParentId) {
        return;
    }

    const glm::mat4 worldMatrix = GetObjectWorldMatrix(childIndex);
    PushUndoState();
    objects_[childIndex].parentId = newParentId;
    if (parentIndex >= 0) {
        const glm::mat4 parentWorld = GetObjectWorldMatrix(parentIndex);
        objects_[childIndex].transform = TransformFromMatrix(glm::inverse(parentWorld) * worldMatrix);
    } else {
        objects_[childIndex].transform = TransformFromMatrix(worldMatrix);
    }
    Select(childIndex);
    if (onDirty_) onDirty_();
}

glm::mat4 SceneEditor::GetObjectWorldMatrix(int index) const {
    if (index < 0 || index >= static_cast<int>(objects_.size())) {
        return glm::mat4(1.0f);
    }

    const SceneObject& object = objects_[index];
    const glm::mat4 local = BuildTransformMatrix(object.transform);
    const int parentIndex = FindObjectIndexById(object.parentId);
    if (parentIndex < 0 || parentIndex == index) {
        return local;
    }
    return GetObjectWorldMatrix(parentIndex) * local;
}

glm::mat4 SceneEditor::GetObjectDisplayWorldMatrix(int index) const {
    if (index < 0 || index >= static_cast<int>(objects_.size())) {
        return glm::mat4(1.0f);
    }

    const SceneObject& object = objects_[index];
    if (!object.hasCamera || !object.camera.enabled || !object.hasCinemachine || !object.cinemachine.enabled) {
        return GetObjectWorldMatrix(index);
    }

    auto stateIt = runtimeCinemachineStates_.find(object.id);
    if (scriptsRunning_ && stateIt != runtimeCinemachineStates_.end() && stateIt->second.initialized) {
        return glm::translate(glm::mat4(1.0f), stateIt->second.smoothedPosition) * glm::toMat4(stateIt->second.smoothedRotation);
    }

    auto findById = [this](const std::string& id) { return FindObjectIndexById(id); };
    auto getMatrix = [this](int idx) { return GetObjectWorldMatrix(idx); };
    glm::mat4 desiredWorld(1.0f);
    if (ComputeCinemachineDesiredWorldMatrix(object.cinemachine, index, objects_, findById, getMatrix, desiredWorld)) {
        return desiredWorld;
    }
    return GetObjectWorldMatrix(index);
}

glm::vec3 SceneEditor::GetObjectWorldPosition(int index) const {
    return glm::vec3(GetObjectWorldMatrix(index) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

void SceneEditor::RequestFocusSelectedObject() {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size()) || !onFocusObject_) {
        return;
    }

    const SceneObject& object = objects_[selectedIndex_];
    const glm::mat4 world = GetObjectDisplayWorldMatrix(selectedIndex_);
    const glm::vec3 center = glm::vec3(world[3]);
    float radius = 1.0f;

    if (object.hasMeshFilter) {
        const glm::vec3 minBounds = object.meshFilter.localBoundsMin;
        const glm::vec3 maxBounds = object.meshFilter.localBoundsMax;
        const glm::vec3 corners[8] = {
            {minBounds.x, minBounds.y, minBounds.z},
            {maxBounds.x, minBounds.y, minBounds.z},
            {maxBounds.x, maxBounds.y, minBounds.z},
            {minBounds.x, maxBounds.y, minBounds.z},
            {minBounds.x, minBounds.y, maxBounds.z},
            {maxBounds.x, minBounds.y, maxBounds.z},
            {maxBounds.x, maxBounds.y, maxBounds.z},
            {minBounds.x, maxBounds.y, maxBounds.z}
        };
        radius = 0.0f;
        for (const glm::vec3& corner : corners) {
            radius = (std::max)(radius, glm::length(TransformPoint(world, corner) - center));
        }
    } else {
        radius = (std::max)(0.5f, glm::length(object.transform.scale));
    }

    activeViewport_ = SceneEditorActiveViewport::Scene;
    onFocusObject_(center, (std::max)(0.5f, radius));
}

void SceneEditor::NormalizeSelection() {
    std::vector<int> normalized;
    normalized.reserve(selectedIndices_.size() + 1);
    for (int index : selectedIndices_) {
        if (index >= 0 && index < static_cast<int>(objects_.size())
            && std::find(normalized.begin(), normalized.end(), index) == normalized.end()) {
            normalized.push_back(index);
        }
    }
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size())
        && std::find(normalized.begin(), normalized.end(), selectedIndex_) == normalized.end()) {
        normalized.push_back(selectedIndex_);
    }
    selectedIndices_ = std::move(normalized);
    if (selectedIndices_.empty()) {
        selectedIndex_ = -1;
    } else if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())
        || std::find(selectedIndices_.begin(), selectedIndices_.end(), selectedIndex_) == selectedIndices_.end()) {
        selectedIndex_ = selectedIndices_.back();
    }
    QueueHierarchyRevealForSelection();
}

void SceneEditor::QueueHierarchyRevealForSelection() {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size())) {
        pendingHierarchyRevealObjectId_.clear();
        lastHierarchyRevealObjectId_.clear();
        return;
    }

    const std::string& selectedId = objects_[selectedIndex_].id;
    if (!lastHierarchyRevealObjectId_.empty() && lastHierarchyRevealObjectId_ == selectedId) {
        return;
    }

    lastHierarchyRevealObjectId_ = selectedId;
    pendingHierarchyRevealObjectId_ = selectedId;

    std::string parentId = objects_[selectedIndex_].parentId;
    std::unordered_set<std::string> visited;
    while (!parentId.empty()) {
        if (visited.find(parentId) != visited.end()) {
            break;
        }
        visited.insert(parentId);
        hierarchyOpenStates_[parentId] = true;
        const int parentIndex = FindObjectIndexById(parentId);
        if (parentIndex < 0 || parentIndex >= static_cast<int>(objects_.size())) {
            break;
        }
        parentId = objects_[parentIndex].parentId;
    }
}

void SceneEditor::PushUndoState() {
    NormalizeSelection();
    undoStack_.push_back({objects_, selectedIndex_, selectedIndices_});
    redoStack_.clear();
    constexpr std::size_t maxHistory = 64;
    if (undoStack_.size() > maxHistory) {
        undoStack_.erase(undoStack_.begin());
    }
}

void SceneEditor::PushVehicleConfigUndoState() {
    if (!showVehicleConfigEditor_ || inspectedVehicleConfigPath_.empty() || !inspectedVehicleConfigLoaded_) {
        return;
    }

    vehicleConfigUndoStack_.push_back({inspectedVehicleConfig_});
    vehicleConfigRedoStack_.clear();
    constexpr std::size_t maxHistory = 128;
    if (vehicleConfigUndoStack_.size() > maxHistory) {
        vehicleConfigUndoStack_.erase(vehicleConfigUndoStack_.begin());
    }
}

void SceneEditor::PushVehicleSoundUndoState() {
    if (!showVehicleSoundEditor_ || inspectedVehicleSoundPath_.empty() || !inspectedVehicleSoundLoaded_) {
        return;
    }

    vehicleSoundUndoStack_.push_back({inspectedVehicleSound_});
    vehicleSoundRedoStack_.clear();
    constexpr std::size_t maxHistory = 128;
    if (vehicleSoundUndoStack_.size() > maxHistory) {
        vehicleSoundUndoStack_.erase(vehicleSoundUndoStack_.begin());
    }
}

void SceneEditor::PushMaterialUndoState(const Material& snapshot) {
    if (!inspectMaterial_ || inspectedMaterialId_.empty()) {
        return;
    }

    materialUndoStack_.push_back({inspectedMaterialId_, snapshot});
    materialRedoStack_.clear();
    constexpr std::size_t maxHistory = 128;
    if (materialUndoStack_.size() > maxHistory) {
        materialUndoStack_.erase(materialUndoStack_.begin());
    }
}

void SceneEditor::Undo() {
    if (undoStack_.empty()) {
        return;
    }

    NormalizeSelection();
    redoStack_.push_back({objects_, selectedIndex_, selectedIndices_});
    const HistoryState state = std::move(undoStack_.back());
    undoStack_.pop_back();
    objects_ = state.objects;
    selectedIndex_ = state.selectedIndex;
    selectedIndices_ = state.selectedIndices;
    NormalizeSelection();
    renamingObjectIndex_ = -1;
    inspectMaterial_ = false;
    activeGizmoAxis_ = -1;
    if (onDirty_) onDirty_();
}

void SceneEditor::UndoMaterial() {
    if (materialUndoStack_.empty() || !inspectMaterial_) {
        return;
    }

    Material* current = materialManager_.Get(inspectedMaterialId_);
    if (current == nullptr) {
        return;
    }

    materialRedoStack_.push_back({inspectedMaterialId_, *current});
    const MaterialHistoryState state = materialUndoStack_.back();
    materialUndoStack_.pop_back();
    if (state.materialId != inspectedMaterialId_) {
        return;
    }
    *current = state.material;
    if (!materialManager_.Save(inspectedMaterialId_, *current) && console_) {
        console_->AddError("Failed to auto-save material undo: " + inspectedMaterialId_);
    }
    materialEditActive_ = false;
}

void SceneEditor::UndoVehicleConfig() {
    if (vehicleConfigUndoStack_.empty() || !showVehicleConfigEditor_) {
        return;
    }

    vehicleConfigRedoStack_.push_back({inspectedVehicleConfig_});
    inspectedVehicleConfig_ = vehicleConfigUndoStack_.back().config;
    vehicleConfigUndoStack_.pop_back();
    vehicleConfigEditActive_ = false;
}

void SceneEditor::Redo() {
    if (redoStack_.empty()) {
        return;
    }

    NormalizeSelection();
    undoStack_.push_back({objects_, selectedIndex_, selectedIndices_});
    const HistoryState state = std::move(redoStack_.back());
    redoStack_.pop_back();
    objects_ = state.objects;
    selectedIndex_ = state.selectedIndex;
    selectedIndices_ = state.selectedIndices;
    NormalizeSelection();
    renamingObjectIndex_ = -1;
    inspectMaterial_ = false;
    activeGizmoAxis_ = -1;
    if (onDirty_) onDirty_();
}


#if 0
void SceneEditor::Load(const std::string& path) {
    using namespace raceman::physics::json;
    objects_.clear();
    hierarchyOpenStates_.clear();
    selectedIndex_ = -1;
    selectedIndices_.clear();
    undoStack_.clear();
    redoStack_.clear();

    const fs::path sourcePath = ResolveEditorPath(path);
    if (!fs::exists(sourcePath)) return;

    std::ifstream in(sourcePath);
    if (!in.good()) return;
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string src = buffer.str();

    struct CachedLoadedMeshAsset {
        bool attempted{false};
        bool loaded{false};
        std::string resolvedPath;
        std::shared_ptr<::Model> model;
        std::vector<ImportedMeshInfo> infos;
    };
    std::unordered_map<std::string, CachedLoadedMeshAsset> meshAssetCache;

    try {
        Value root = parse(src);
        if (!root.is_object()) return;
        const auto& obj = root.as_object();
        auto it = obj.find("objects");
        if (it == obj.end() || !it->second.is_array()) return;

        const auto& arr = it->second.as_array();
        for (const auto& v : arr) {
            if (!v.is_object()) continue;
            const auto& o = v.as_object();

            SceneObject so;
            so.inspectorComponentOrder = DefaultInspectorComponentOrder();

            // id
            auto idIt = o.find("id");
            if (idIt != o.end() && idIt->second.is_string()) {
                so.id = idIt->second.as_string();
            }
            else {
                so.id = MakeId("obj");
            }

            ReadString(o, "parentId", so.parentId);

            // name
            auto nameIt = o.find("name");
            if (nameIt != o.end() && nameIt->second.is_string()) {
                so.name = nameIt->second.as_string();
            }
            else {
                so.name = "Object";
            }
            ReadString(o, "tag", so.tag);
            if (so.tag.empty()) {
                so.tag = "Untagged";
            }

            // type
            std::string legacyType = "GameObject";
            auto typeIt = o.find("type");
            if (typeIt != o.end() && typeIt->second.is_string()) {
                legacyType = typeIt->second.as_string();
            }
            so.type = "GameObject";
            so.physicsLayer = 0;

            // transform
            auto trIt = o.find("transform");
            if (trIt != o.end() && trIt->second.is_object()) {
                const auto& tr = trIt->second.as_object();

                auto posIt = tr.find("position");
                if (posIt != tr.end() && posIt->second.is_array()) {
                    const auto& a = posIt->second.as_array();
                    if (a.size() == 3 && a[0].is_number() && a[1].is_number() && a[2].is_number()) {
                        so.transform.position = {
                            static_cast<float>(a[0].as_number()),
                            static_cast<float>(a[1].as_number()),
                            static_cast<float>(a[2].as_number())
                        };
                    }
                }

                auto rotIt = tr.find("rotationEuler");
                if (rotIt != tr.end() && rotIt->second.is_array()) {
                    const auto& a = rotIt->second.as_array();
                    if (a.size() == 3 && a[0].is_number() && a[1].is_number() && a[2].is_number()) {
                        so.transform.rotationEuler = {
                            static_cast<float>(a[0].as_number()),
                            static_cast<float>(a[1].as_number()),
                            static_cast<float>(a[2].as_number())
                        };
                    }
                }

                auto scIt = tr.find("scale");
                if (scIt != tr.end() && scIt->second.is_array()) {
                    const auto& a = scIt->second.as_array();
                    if (a.size() == 3 && a[0].is_number() && a[1].is_number() && a[2].is_number()) {
                        so.transform.scale = {
                            static_cast<float>(a[0].as_number()),
                            static_cast<float>(a[1].as_number()),
                            static_cast<float>(a[2].as_number())
                        };
                    }
                }
            }

            ReadBool(o, "enabled", so.enabled);
            if (auto physicsLayerIt = o.find("physicsLayer"); physicsLayerIt != o.end() && physicsLayerIt->second.is_number()) {
                so.physicsLayer = ClampPhysicsLayerIndex(static_cast<int>(physicsLayerIt->second.as_number()));
            }
            if (auto componentOrderIt = o.find("componentOrder"); componentOrderIt != o.end() && componentOrderIt->second.is_array()) {
                so.inspectorComponentOrder.clear();
                for (const auto& orderValue : componentOrderIt->second.as_array()) {
                    if (!orderValue.is_string()) {
                        continue;
                    }
                    SceneInspectorComponentType componentType;
                    if (InspectorComponentTypeFromString(orderValue.as_string(), componentType)) {
                        so.inspectorComponentOrder.push_back(componentType);
                    }
                }
            }

            // color (optional)
            auto colIt = o.find("color");
            if (colIt != o.end() && colIt->second.is_array()) {
                const auto& a = colIt->second.as_array();
                if (a.size() == 4 && a[0].is_number() && a[1].is_number() && a[2].is_number() && a[3].is_number()) {
                    so.meshRenderer.color = {
                        static_cast<float>(a[0].as_number()),
                        static_cast<float>(a[1].as_number()),
                        static_cast<float>(a[2].as_number()),
                        static_cast<float>(a[3].as_number())
                    };
                }
            }

            // materialId (optional)
            auto matIt = o.find("materialId");
            if (matIt != o.end() && matIt->second.is_string()) {
                so.meshRenderer.materialId = matIt->second.as_string();
            }

            auto sourceIt = o.find("sourcePath");
            if (sourceIt != o.end() && sourceIt->second.is_string()) {
                so.meshFilter.sourcePath = NormalizeSlashes(sourceIt->second.as_string());
            }

            auto meshIndexIt = o.find("meshIndex");
            if (meshIndexIt != o.end() && meshIndexIt->second.is_number()) {
                so.meshFilter.meshIndex = static_cast<int>(meshIndexIt->second.as_number());
            }

            auto importedMaterialIt = o.find("importedMaterialName");
            if (importedMaterialIt != o.end() && importedMaterialIt->second.is_string()) {
                so.meshFilter.importedMaterialName = importedMaterialIt->second.as_string();
            }

            auto diffuseTextureIt = o.find("diffuseTexturePath");
            if (diffuseTextureIt != o.end() && diffuseTextureIt->second.is_string()) {
                so.meshFilter.diffuseTexturePath = NormalizeSlashes(diffuseTextureIt->second.as_string());
            }

            auto scriptAttachmentsIt = o.find("scriptAttachments");
            if (scriptAttachmentsIt != o.end() && scriptAttachmentsIt->second.is_array()) {
                const auto& scripts = scriptAttachmentsIt->second.as_array();
                for (const auto& scriptValue : scripts) {
                    if (!scriptValue.is_object()) {
                        continue;
                    }

                    const auto& scriptObject = scriptValue.as_object();
                    ObjectScriptAttachment script;
                    ReadBool(scriptObject, "enabled", script.enabled);
                    ReadString(scriptObject, "scriptName", script.scriptName);
                    ReadString(scriptObject, "scriptPath", script.scriptPath);
                    script.scriptPath = NormalizeSlashes(script.scriptPath);
                    auto fieldsIt = scriptObject.find("fields");
                    if (fieldsIt != scriptObject.end() && fieldsIt->second.is_array()) {
                        for (const auto& fieldValue : fieldsIt->second.as_array()) {
                            if (!fieldValue.is_object()) {
                                continue;
                            }
                            const auto& fieldObject = fieldValue.as_object();
                            ScriptFieldEntry field;
                            std::string typeName;
                            if (!ReadString(fieldObject, "name", field.name) || !ReadString(fieldObject, "type", typeName)) {
                                continue;
                            }
                            field.type = ScriptFieldTypeFromString(typeName);
                            if (!TryReadScriptFieldValue(fieldObject, field.type, field.value)) {
                                continue;
                            }
                            script.fields.push_back(std::move(field));
                        }
                    }
                    if (!script.scriptName.empty()) {
                        SyncAttachmentScriptFields(script);
                        so.scriptComponent.attachments.push_back(script);
                    }
                }
            }

            auto componentsIt = o.find("components");
            if (componentsIt != o.end() && componentsIt->second.is_array()) {
                so.hasMeshFilter = false;
                so.hasMeshRenderer = false;
                so.hasScriptComponent = false;
                so.hasRigidbody = false;
                so.hasVehicle = false;
                so.hasCharacterController = false;
                so.hasBoxCollider = false;
                so.hasSphereCollider = false;
                so.hasCapsuleCollider = false;
                so.hasPlaneCollider = false;
                so.hasMeshCollider = false;
                so.hasCamera = false;
                so.hasCinemachine = false;
                so.hasLight = false;

                const auto& components = componentsIt->second.as_array();
                for (const auto& componentValue : components) {
                    if (!componentValue.is_object()) {
                        continue;
                    }

                    const auto& component = componentValue.as_object();
                    std::string componentType;
                    ReadString(component, "type", componentType);

                    if (componentType == "Transform") {
                        ReadVec3(component, "position", so.transform.position);
                        ReadVec3(component, "rotationEuler", so.transform.rotationEuler);
                        ReadVec3(component, "scale", so.transform.scale);
                    } else if (componentType == "MeshFilter") {
                        so.hasMeshFilter = true;
                        ReadBool(component, "enabled", so.meshFilter.enabled);
                        ReadString(component, "meshType", so.meshFilter.meshType);
                        ReadString(component, "sourcePath", so.meshFilter.sourcePath);
                        so.meshFilter.sourcePath = NormalizeSlashes(so.meshFilter.sourcePath);

                        auto componentMeshIndexIt = component.find("meshIndex");
                        if (componentMeshIndexIt != component.end() && componentMeshIndexIt->second.is_number()) {
                            so.meshFilter.meshIndex = static_cast<int>(componentMeshIndexIt->second.as_number());
                        }

                        ReadString(component, "importedMaterialName", so.meshFilter.importedMaterialName);
                        ReadString(component, "diffuseTexturePath", so.meshFilter.diffuseTexturePath);
                        so.meshFilter.diffuseTexturePath = NormalizeSlashes(so.meshFilter.diffuseTexturePath);
                        ReadVec3(component, "pivotOffset", so.meshFilter.pivotOffset);
                    } else if (componentType == "MeshRenderer") {
                        so.hasMeshRenderer = true;
                        ReadBool(component, "enabled", so.meshRenderer.enabled);
                        ReadString(component, "materialId", so.meshRenderer.materialId);
                        ReadVec4(component, "color", so.meshRenderer.color);
                    } else if (componentType == "Script") {
                        so.hasScriptComponent = true;
                        ReadBool(component, "enabled", so.scriptComponent.enabled);
                        so.scriptComponent.attachments.clear();

                        auto attachmentsIt = component.find("attachments");
                        if (attachmentsIt != component.end() && attachmentsIt->second.is_array()) {
                            const auto& scripts = attachmentsIt->second.as_array();
                            for (const auto& scriptValue : scripts) {
                                if (!scriptValue.is_object()) {
                                    continue;
                                }

                                const auto& scriptObject = scriptValue.as_object();
                                ObjectScriptAttachment script;
                                ReadBool(scriptObject, "enabled", script.enabled);
                                ReadString(scriptObject, "scriptName", script.scriptName);
                                ReadString(scriptObject, "scriptPath", script.scriptPath);
                                script.scriptPath = NormalizeSlashes(script.scriptPath);
                                auto fieldsIt = scriptObject.find("fields");
                                if (fieldsIt != scriptObject.end() && fieldsIt->second.is_array()) {
                                    for (const auto& fieldValue : fieldsIt->second.as_array()) {
                                        if (!fieldValue.is_object()) {
                                            continue;
                                        }
                                        const auto& fieldObject = fieldValue.as_object();
                                        ScriptFieldEntry field;
                                        std::string typeName;
                                        if (!ReadString(fieldObject, "name", field.name) || !ReadString(fieldObject, "type", typeName)) {
                                            continue;
                                        }
                                        field.type = ScriptFieldTypeFromString(typeName);
                                        if (!TryReadScriptFieldValue(fieldObject, field.type, field.value)) {
                                            continue;
                                        }
                                        script.fields.push_back(std::move(field));
                                    }
                                }
                                if (!script.scriptName.empty()) {
                                    SyncAttachmentScriptFields(script);
                                    so.scriptComponent.attachments.push_back(script);
                                }
                            }
                        }
                    } else if (componentType == "Rigidbody") {
                        so.hasRigidbody = true;
                        ReadBool(component, "enabled", so.rigidbody.enabled);

                        std::string bodyType;
                        if (ReadString(component, "bodyType", bodyType)) {
                            so.rigidbody.bodyType = RigidbodyBodyTypeFromString(bodyType);
                        }

                        auto massIt = component.find("mass");
                        if (massIt != component.end() && massIt->second.is_number()) {
                            so.rigidbody.mass = (std::max)(0.0001f, static_cast<float>(massIt->second.as_number()));
                        }

                        ReadBool(component, "useGravity", so.rigidbody.useGravity);
                        if (auto linearDampingIt = component.find("linearDamping"); linearDampingIt != component.end() && linearDampingIt->second.is_number()) {
                            so.rigidbody.linearDamping = (std::max)(0.0f, static_cast<float>(linearDampingIt->second.as_number()));
                        }
                        if (auto angularDampingIt = component.find("angularDamping"); angularDampingIt != component.end() && angularDampingIt->second.is_number()) {
                            so.rigidbody.angularDamping = (std::max)(0.0f, static_cast<float>(angularDampingIt->second.as_number()));
                        }
                        if (auto frictionIt = component.find("friction"); frictionIt != component.end() && frictionIt->second.is_number()) {
                            so.rigidbody.friction = (std::max)(0.0f, static_cast<float>(frictionIt->second.as_number()));
                        }
                        if (auto restitutionIt = component.find("restitution"); restitutionIt != component.end() && restitutionIt->second.is_number()) {
                            so.rigidbody.restitution = (std::max)(0.0f, static_cast<float>(restitutionIt->second.as_number()));
                        }
                        ReadVec3(component, "velocity", so.rigidbody.velocity);
                        ReadVec3(component, "angularVelocity", so.rigidbody.angularVelocity);
                        if (auto freezePositionIt = component.find("freezePosition"); freezePositionIt != component.end() && freezePositionIt->second.is_array()) {
                            const auto& value = freezePositionIt->second.as_array();
                            if (value.size() == 3 && value[0].is_bool() && value[1].is_bool() && value[2].is_bool()) {
                                so.rigidbody.freezePositionX = value[0].as_bool();
                                so.rigidbody.freezePositionY = value[1].as_bool();
                                so.rigidbody.freezePositionZ = value[2].as_bool();
                            }
                        }
                        if (auto freezeRotationIt = component.find("freezeRotation"); freezeRotationIt != component.end() && freezeRotationIt->second.is_array()) {
                            const auto& value = freezeRotationIt->second.as_array();
                            if (value.size() == 3 && value[0].is_bool() && value[1].is_bool() && value[2].is_bool()) {
                                so.rigidbody.freezeRotationX = value[0].as_bool();
                                so.rigidbody.freezeRotationY = value[1].as_bool();
                                so.rigidbody.freezeRotationZ = value[2].as_bool();
                            }
                        }
                    } else if (componentType == "Vehicle") {
                        so.hasVehicle = true;
                        ReadBool(component, "enabled", so.vehicle.enabled);
                        so.vehicle.canTilt = true;
                        ReadBool(component, "canTilt", so.vehicle.canTilt);
                        ReadString(component, "configPath", so.vehicle.configPath);
                        so.vehicle.configPath = NormalizeSlashes(so.vehicle.configPath);
                        so.vehicle.inputProfileId = "default_vehicle";
                        ReadString(component, "inputProfileId", so.vehicle.inputProfileId);
                        std::string preferredInputDevice;
                        if (ReadString(component, "preferredInputDevice", preferredInputDevice)) {
                            so.vehicle.preferredInputDevice = InputDevicePreferenceFromStorage(preferredInputDevice);
                        }
                        ReadString(component, "preferredInputDeviceId", so.vehicle.preferredInputDeviceId);
                        so.vehicle.chassisObjectIds.clear();
                        if (auto chassisObjectIdsIt = component.find("chassisObjectIds"); chassisObjectIdsIt != component.end() && chassisObjectIdsIt->second.is_array()) {
                            for (const auto& chassisValue : chassisObjectIdsIt->second.as_array()) {
                                if (chassisValue.is_string()) {
                                    const std::string chassisObjectId = chassisValue.as_string();
                                    if (!chassisObjectId.empty()) {
                                        so.vehicle.chassisObjectIds.push_back(chassisObjectId);
                                    }
                                }
                            }
                        }
                        so.vehicle.wheelBindings.clear();
                        if (auto wheelBindingsIt = component.find("wheelBindings"); wheelBindingsIt != component.end() && wheelBindingsIt->second.is_array()) {
                            for (const auto& bindingValue : wheelBindingsIt->second.as_array()) {
                                if (!bindingValue.is_object()) {
                                    continue;
                                }
                                const auto& bindingObject = bindingValue.as_object();
                                VehicleWheelBinding binding;
                                ReadString(bindingObject, "wheelName", binding.wheelName);
                                ReadString(bindingObject, "objectId", binding.objectId);
                                ReadVec3(bindingObject, "visualRotationEuler", binding.visualRotationEuler);
                                if (!binding.wheelName.empty()) {
                                    so.vehicle.wheelBindings.push_back(std::move(binding));
                                }
                            }
                        }
                    } else if (componentType == "CharacterController") {
                        so.hasCharacterController = true;
                        ReadBool(component, "enabled", so.characterController.enabled);

                        auto heightIt = component.find("height");
                        if (heightIt != component.end() && heightIt->second.is_number()) {
                            so.characterController.height = (std::max)(0.001f, static_cast<float>(heightIt->second.as_number()));
                        }
                        auto radiusIt = component.find("radius");
                        if (radiusIt != component.end() && radiusIt->second.is_number()) {
                            so.characterController.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                        }
                        so.characterController.height = (std::max)(so.characterController.radius * 2.0f, so.characterController.height);
                        ReadVec3(component, "center", so.characterController.center);
                        auto stepHeightIt = component.find("stepHeight");
                        if (stepHeightIt != component.end() && stepHeightIt->second.is_number()) {
                            so.characterController.stepHeight = (std::max)(0.0f, static_cast<float>(stepHeightIt->second.as_number()));
                        }
                        auto slopeLimitIt = component.find("slopeLimitDegrees");
                        if (slopeLimitIt != component.end() && slopeLimitIt->second.is_number()) {
                            so.characterController.slopeLimitDegrees = (std::max)(1.0f, (std::min)(89.0f, static_cast<float>(slopeLimitIt->second.as_number())));
                        }
                        auto maxStrengthIt = component.find("maxStrength");
                        if (maxStrengthIt != component.end() && maxStrengthIt->second.is_number()) {
                            so.characterController.maxStrength = (std::max)(0.0f, static_cast<float>(maxStrengthIt->second.as_number()));
                        }
                        auto massIt = component.find("mass");
                        if (massIt != component.end() && massIt->second.is_number()) {
                            so.characterController.mass = (std::max)(0.001f, static_cast<float>(massIt->second.as_number()));
                        }
                    } else if (componentType == "Collider") {
                        std::string colliderTypeName;
                        if (ReadString(component, "colliderType", colliderTypeName)) {
                            const std::string lowerType = ToLowerCopy(colliderTypeName);
                            if (lowerType == "box") {
                                SetActiveColliderType(so, SceneColliderType::Box);
                                ReadBool(component, "enabled", so.boxCollider.enabled);
                                ReadBool(component, "isTrigger", so.boxCollider.isTrigger);
                                ReadVec3(component, "center", so.boxCollider.center);
                                ReadVec3(component, "size", so.boxCollider.size);
                                so.boxCollider.size = {
                                    (std::max)(0.001f, so.boxCollider.size.x),
                                    (std::max)(0.001f, so.boxCollider.size.y),
                                    (std::max)(0.001f, so.boxCollider.size.z)
                                };
                            } else if (lowerType == "sphere") {
                                SetActiveColliderType(so, SceneColliderType::Sphere);
                                ReadBool(component, "enabled", so.sphereCollider.enabled);
                                ReadBool(component, "isTrigger", so.sphereCollider.isTrigger);
                                ReadVec3(component, "center", so.sphereCollider.center);
                                if (auto radiusIt = component.find("radius"); radiusIt != component.end() && radiusIt->second.is_number()) {
                                    so.sphereCollider.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                                }
                            } else if (lowerType == "capsule") {
                                SetActiveColliderType(so, SceneColliderType::Capsule);
                                ReadBool(component, "enabled", so.capsuleCollider.enabled);
                                ReadBool(component, "isTrigger", so.capsuleCollider.isTrigger);
                                ReadVec3(component, "center", so.capsuleCollider.center);
                                if (auto radiusIt = component.find("radius"); radiusIt != component.end() && radiusIt->second.is_number()) {
                                    so.capsuleCollider.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                                }
                                if (auto heightIt = component.find("height"); heightIt != component.end() && heightIt->second.is_number()) {
                                    so.capsuleCollider.height = (std::max)(so.capsuleCollider.radius * 2.0f, static_cast<float>(heightIt->second.as_number()));
                                }
                            } else if (lowerType == "plane") {
                                SetActiveColliderType(so, SceneColliderType::Plane);
                                ReadBool(component, "enabled", so.planeCollider.enabled);
                                ReadBool(component, "isTrigger", so.planeCollider.isTrigger);
                                ReadVec3(component, "normal", so.planeCollider.normal);
                                if (glm::length2(so.planeCollider.normal) <= 0.000001f) {
                                    so.planeCollider.normal = {0.0f, 1.0f, 0.0f};
                                } else {
                                    so.planeCollider.normal = glm::normalize(so.planeCollider.normal);
                                }
                                if (auto offsetIt = component.find("offset"); offsetIt != component.end() && offsetIt->second.is_number()) {
                                    so.planeCollider.offset = static_cast<float>(offsetIt->second.as_number());
                                }
                                ReadBool(component, "infinite", so.planeCollider.infinite);
                                if (auto halfExtentIt = component.find("halfExtent"); halfExtentIt != component.end() && halfExtentIt->second.is_number()) {
                                    so.planeCollider.halfExtent = (std::max)(0.001f, static_cast<float>(halfExtentIt->second.as_number()));
                                }
                            } else if (lowerType == "mesh") {
                                SetActiveColliderType(so, SceneColliderType::Mesh);
                                ReadBool(component, "enabled", so.meshCollider.enabled);
                                ReadBool(component, "isTrigger", so.meshCollider.isTrigger);
                                std::string buildQuality;
                                if (ReadString(component, "buildQuality", buildQuality)) {
                                    so.meshCollider.buildQuality = MeshColliderBuildQualityFromString(buildQuality);
                                }
                                std::string meshMode;
                                if (ReadString(component, "mode", meshMode)) {
                                    so.meshCollider.mode = MeshColliderModeFromString(meshMode);
                                }
                            }
                        }
                    } else if (componentType == "BoxCollider") {
                        SetActiveColliderType(so, SceneColliderType::Box);
                        ReadBool(component, "enabled", so.boxCollider.enabled);
                        ReadBool(component, "isTrigger", so.boxCollider.isTrigger);
                        ReadVec3(component, "center", so.boxCollider.center);
                        ReadVec3(component, "size", so.boxCollider.size);
                        so.boxCollider.size = {
                            (std::max)(0.001f, so.boxCollider.size.x),
                            (std::max)(0.001f, so.boxCollider.size.y),
                            (std::max)(0.001f, so.boxCollider.size.z)
                        };
                    } else if (componentType == "SphereCollider") {
                        SetActiveColliderType(so, SceneColliderType::Sphere);
                        ReadBool(component, "enabled", so.sphereCollider.enabled);
                        ReadBool(component, "isTrigger", so.sphereCollider.isTrigger);
                        ReadVec3(component, "center", so.sphereCollider.center);

                        auto radiusIt = component.find("radius");
                        if (radiusIt != component.end() && radiusIt->second.is_number()) {
                            so.sphereCollider.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                        }
                    } else if (componentType == "CapsuleCollider") {
                        SetActiveColliderType(so, SceneColliderType::Capsule);
                        ReadBool(component, "enabled", so.capsuleCollider.enabled);
                        ReadBool(component, "isTrigger", so.capsuleCollider.isTrigger);
                        ReadVec3(component, "center", so.capsuleCollider.center);

                        auto radiusIt = component.find("radius");
                        if (radiusIt != component.end() && radiusIt->second.is_number()) {
                            so.capsuleCollider.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                        }
                        auto heightIt = component.find("height");
                        if (heightIt != component.end() && heightIt->second.is_number()) {
                            so.capsuleCollider.height = (std::max)(so.capsuleCollider.radius * 2.0f, static_cast<float>(heightIt->second.as_number()));
                        }
                    } else if (componentType == "PlaneCollider") {
                        SetActiveColliderType(so, SceneColliderType::Plane);
                        ReadBool(component, "enabled", so.planeCollider.enabled);
                        ReadBool(component, "isTrigger", so.planeCollider.isTrigger);
                        ReadVec3(component, "normal", so.planeCollider.normal);
                        if (glm::length2(so.planeCollider.normal) <= 0.000001f) {
                            so.planeCollider.normal = {0.0f, 1.0f, 0.0f};
                        } else {
                            so.planeCollider.normal = glm::normalize(so.planeCollider.normal);
                        }
                        auto offsetIt = component.find("offset");
                        if (offsetIt != component.end() && offsetIt->second.is_number()) {
                            so.planeCollider.offset = static_cast<float>(offsetIt->second.as_number());
                        }
                        ReadBool(component, "infinite", so.planeCollider.infinite);
                        auto halfExtentIt = component.find("halfExtent");
                        if (halfExtentIt != component.end() && halfExtentIt->second.is_number()) {
                            so.planeCollider.halfExtent = (std::max)(0.001f, static_cast<float>(halfExtentIt->second.as_number()));
                        }
                    } else if (componentType == "MeshCollider") {
                        SetActiveColliderType(so, SceneColliderType::Mesh);
                        ReadBool(component, "enabled", so.meshCollider.enabled);
                        ReadBool(component, "isTrigger", so.meshCollider.isTrigger);
                        std::string buildQuality;
                        if (ReadString(component, "buildQuality", buildQuality)) {
                            so.meshCollider.buildQuality = MeshColliderBuildQualityFromString(buildQuality);
                        }
                        std::string meshMode;
                        if (ReadString(component, "mode", meshMode)) {
                            so.meshCollider.mode = MeshColliderModeFromString(meshMode);
                        }
                    } else if (componentType == "Camera") {
                        so.hasCamera = true;
                        ReadBool(component, "enabled", so.camera.enabled);
                        ReadBool(component, "isMain", so.camera.isMain);
                        ReadVec4(component, "clearColor", so.camera.clearColor);

                        auto fovIt = component.find("fieldOfViewDegrees");
                        if (fovIt != component.end() && fovIt->second.is_number()) {
                            so.camera.fieldOfViewDegrees = (std::max)(1.0f, (std::min)(179.0f, static_cast<float>(fovIt->second.as_number())));
                        }
                        auto nearIt = component.find("nearClip");
                        if (nearIt != component.end() && nearIt->second.is_number()) {
                            so.camera.nearClip = (std::max)(0.001f, static_cast<float>(nearIt->second.as_number()));
                        }
                        auto farIt = component.find("farClip");
                        if (farIt != component.end() && farIt->second.is_number()) {
                            so.camera.farClip = (std::max)(so.camera.nearClip + 0.001f, static_cast<float>(farIt->second.as_number()));
                        }
                    } else if (componentType == "Cinemachine") {
                        so.hasCinemachine = true;
                        ReadBool(component, "enabled", so.cinemachine.enabled);
                        std::string cameraTypeStr;
                        if (ReadString(component, "cameraType", cameraTypeStr)) {
                            if (cameraTypeStr == "Follow") {
                                so.cinemachine.type = CinemachineCameraType::Follow;
                            } else if (cameraTypeStr == "LookAt") {
                                so.cinemachine.type = CinemachineCameraType::LookAt;
                            } else {
                                so.cinemachine.type = CinemachineCameraType::FollowAndLookAt;
                            }
                        }
                        ReadString(component, "followTargetId", so.cinemachine.followTargetId);
                        ReadString(component, "lookAtTargetId", so.cinemachine.lookAtTargetId);
                        if (ReadVec3(component, "followOffset", so.cinemachine.followOffset)) {
                            so.transform.position = so.cinemachine.followOffset;
                        }
                        auto pitchIt = component.find("pitchOffset");
                        if (pitchIt != component.end() && pitchIt->second.is_number()) {
                            so.cinemachine.pitchOffset = static_cast<float>(pitchIt->second.as_number());
                            so.transform.rotationEuler.x = so.cinemachine.pitchOffset;
                        }
                        auto yawIt = component.find("yawOffset");
                        if (yawIt != component.end() && yawIt->second.is_number()) {
                            so.cinemachine.yawOffset = static_cast<float>(yawIt->second.as_number());
                            so.transform.rotationEuler.y = so.cinemachine.yawOffset;
                        }
                        auto posDampIt = component.find("positionDamping");
                        if (posDampIt != component.end() && posDampIt->second.is_number()) {
                            so.cinemachine.positionDamping = (std::max)(0.0f, static_cast<float>(posDampIt->second.as_number()));
                        }
                        auto rotDampIt = component.find("rotationDamping");
                        if (rotDampIt != component.end() && rotDampIt->second.is_number()) {
                            so.cinemachine.rotationDamping = (std::max)(0.0f, static_cast<float>(rotDampIt->second.as_number()));
                        }
                    } else if (componentType == "Light") {
                        so.hasLight = true;
                        ReadBool(component, "enabled", so.light.enabled);
                        ReadVec3(component, "color", so.light.color);

                        std::string lightType;
                        if (ReadString(component, "lightType", lightType)) {
                            so.light.type = LightTypeFromString(lightType);
                        }

                        auto intensityIt = component.find("intensity");
                        if (intensityIt != component.end() && intensityIt->second.is_number()) {
                            so.light.intensity = (std::max)(0.0f, static_cast<float>(intensityIt->second.as_number()));
                        }
                        auto rangeIt = component.find("range");
                        if (rangeIt != component.end() && rangeIt->second.is_number()) {
                            so.light.range = (std::max)(0.001f, static_cast<float>(rangeIt->second.as_number()));
                        }
                        auto spotAngleIt = component.find("spotAngleDegrees");
                        if (spotAngleIt != component.end() && spotAngleIt->second.is_number()) {
                            so.light.spotAngleDegrees = (std::max)(1.0f, (std::min)(179.0f, static_cast<float>(spotAngleIt->second.as_number())));
                        }
                    }
                }
            }

            if (so.meshFilter.meshType.empty()) {
                if (legacyType == "Plane" || legacyType == "Mesh") {
                    so.meshFilter.meshType = legacyType;
                } else if (so.hasMeshFilter) {
                    so.meshFilter.meshType = !so.meshFilter.sourcePath.empty() ? std::string("Mesh") : std::string("Plane");
                }
            }

            if (so.hasCharacterController && so.hasRigidbody) {
                so.hasRigidbody = false;
                so.rigidbody = RigidbodyComponent{};
            }

            SyncInspectorComponentOrder(so);

            // attach render info for known types
            const std::string meshType = so.meshFilter.meshType.empty() ? std::string("Mesh") : so.meshFilter.meshType;
            if (so.hasMeshFilter && IsBuiltInPrimitiveMeshType(meshType)) {
                ConfigureBuiltInPrimitive(so, meshType, builtInPrimitiveMeshes_);
            } else if (so.hasMeshFilter && meshType == "Mesh" && !so.meshFilter.sourcePath.empty()) {
                try {
                    if (ShouldFallbackToBuiltInPlane(so)) {
                        ConfigureBuiltInPrimitive(so, "Plane", builtInPrimitiveMeshes_);
                    } else {
                        const std::string cacheKey = NormalizeSlashes(so.meshFilter.sourcePath);
                        CachedLoadedMeshAsset& cachedAsset = meshAssetCache[cacheKey];
                        if (!cachedAsset.attempted) {
                            cachedAsset.attempted = true;
                            cachedAsset.loaded = TryLoadMeshAsset(
                                so.meshFilter.sourcePath,
                                cachedAsset.resolvedPath,
                                cachedAsset.model,
                                cachedAsset.infos);
                        }

                        if (cachedAsset.loaded &&
                            so.meshFilter.meshIndex >= 0 &&
                            so.meshFilter.meshIndex < static_cast<int>(cachedAsset.infos.size())) {
                            so.meshFilter.meshType = "Mesh";
                            so.meshFilter.sourcePath = cachedAsset.resolvedPath;
                            ApplyMeshInfoToSceneObject(
                                so,
                                cachedAsset.infos[static_cast<std::size_t>(so.meshFilter.meshIndex)],
                                cachedAsset.model);
                        }
                    }
                } catch (...) {
                    if (ShouldFallbackToBuiltInPlane(so) && ConfigureBuiltInPrimitive(so, "Plane", builtInPrimitiveMeshes_)) {
                        AddDefaultPlaneColliderToPlane(so);
                    } else if (console_) {
                        console_->AddLog("Failed to reload mesh source: " + so.meshFilter.sourcePath);
                    }
                }
            }

            objects_.push_back(std::move(so));
        }

        std::vector<std::string> seenIds;
        std::vector<std::pair<std::string, std::string>> remappedIds;
        for (SceneObject& object : objects_) {
            const bool missingId = object.id.empty();
            const bool duplicateId = !missingId && std::find(seenIds.begin(), seenIds.end(), object.id) != seenIds.end();
            if (!missingId && !duplicateId) {
                seenIds.push_back(object.id);
                continue;
            }

            const std::string oldId = object.id;
            object.id = MakeId("gameobject");
            seenIds.push_back(object.id);
            if (!oldId.empty()) {
                remappedIds.emplace_back(oldId, object.id);
            }
        }

        for (SceneObject& object : objects_) {
            for (const auto& remap : remappedIds) {
                if (object.parentId == remap.first) {
                    object.parentId = remap.second;
                }
            }
        }

        auto hierarchyClosedIt = obj.find("hierarchyClosed");
        if (hierarchyClosedIt != obj.end() && hierarchyClosedIt->second.is_array()) {
            for (const auto& closedValue : hierarchyClosedIt->second.as_array()) {
                if (!closedValue.is_string()) {
                    continue;
                }
                hierarchyOpenStates_[closedValue.as_string()] = false;
            }
        }
        auto hierarchyOpenIt = obj.find("hierarchyOpen");
        if (hierarchyOpenIt != obj.end() && hierarchyOpenIt->second.is_array()) {
            for (const auto& openValue : hierarchyOpenIt->second.as_array()) {
                if (!openValue.is_string()) {
                    continue;
                }
                hierarchyOpenStates_[openValue.as_string()] = true;
            }
        }

        // Select first object if available
        for (SceneObject& object : objects_) {
            if (!object.parentId.empty() && FindObjectIndexById(object.parentId) < 0) {
                object.parentId.clear();
            }
        }
        if (!objects_.empty()) {
            Select(0);
        }
    } catch (const std::exception&) {
        // Silently ignore malformed files
    }
    
}

void SceneEditor::LoadProject() {
    using namespace raceman::physics::json;

    MigrateLegacyProjectLayout();

    projectName_ = "Project Raceman";
    assetsRootSetting_ = "assets";
    defaultScenePath_ = "assets/scenes/EditorScene.scene.json";
    lastScenePath_ = defaultScenePath_;
    selectedProjectDirectory_ = "assets";
    activeViewport_ = SceneEditorActiveViewport::Scene;
    ResetPhysicsLayerSettings();
    projectTags_ = {"Untagged"};

    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path scenesRoot = assetsRoot / "scenes";
    const fs::path projectFile = ProjectRootPath() / projectPath_;
    bool shouldSaveProject = false;

    try {
        fs::create_directories(scenesRoot);
    } catch (...) {}

    std::string projectSource;
    if (ReadTextFile(projectFile, projectSource)) {
        try {
            Value root = parse(projectSource);
            if (root.is_object()) {
                const auto& object = root.as_object();
                ReadString(object, "projectName", projectName_);
                ReadString(object, "assetsRoot", assetsRootSetting_);
                ReadString(object, "defaultScene", defaultScenePath_);
                ReadString(object, "lastScene", lastScenePath_);

                auto tagsIt = object.find("tags");
                if (tagsIt != object.end() && tagsIt->second.is_array()) {
                    projectTags_.clear();
                    for (const auto& tagValue : tagsIt->second.as_array()) {
                        if (tagValue.is_string()) {
                            projectTags_.push_back(tagValue.as_string());
                        }
                    }
                } else {
                    shouldSaveProject = true;
                }
                EnsureProjectTags();

                auto editorIt = object.find("editorState");
                if (editorIt != object.end() && editorIt->second.is_object()) {
                    const auto& editorState = editorIt->second.as_object();
                    ReadString(editorState, "selectedProjectDirectory", selectedProjectDirectory_);
                }

                auto physicsIt = object.find("physics");
                if (physicsIt != object.end() && physicsIt->second.is_object()) {
                    const auto& physicsSettings = physicsIt->second.as_object();

                    auto layersIt = physicsSettings.find("layers");
                    if (layersIt != physicsSettings.end() && layersIt->second.is_array()) {
                        const auto& layers = layersIt->second.as_array();
                        for (int layerIndex = 0; layerIndex < kPhysicsLayerCount && layerIndex < static_cast<int>(layers.size()); ++layerIndex) {
                            if (layers[static_cast<std::size_t>(layerIndex)].is_string()) {
                                physicsLayerNames_[static_cast<std::size_t>(layerIndex)] = MakePhysicsLayerStorageName(
                                    layers[static_cast<std::size_t>(layerIndex)].as_string(),
                                    layerIndex);
                            }
                        }
                    }

                    auto matrixIt = physicsSettings.find("collisionMatrix");
                    if (matrixIt != physicsSettings.end() && matrixIt->second.is_array()) {
                        const auto& rows = matrixIt->second.as_array();
                        for (int row = 0; row < kPhysicsLayerCount && row < static_cast<int>(rows.size()); ++row) {
                            if (!rows[static_cast<std::size_t>(row)].is_object()) {
                                continue;
                            }
                            std::array<bool, kPhysicsLayerCount> rowValues = physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)];
                            if (ReadBoolArray(rows[static_cast<std::size_t>(row)].as_object(), "values", rowValues)) {
                                physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)] = rowValues;
                            }
                        }
                    }
                }

                inputProfiles_.clear();
                wheelSettingsProfiles_.clear();
                auto inputIt = object.find("input");
                if (inputIt != object.end() && inputIt->second.is_object()) {
                    const auto& inputSettings = inputIt->second.as_object();
                    auto profilesIt = inputSettings.find("profiles");
                    if (profilesIt != inputSettings.end() && profilesIt->second.is_array()) {
                        for (const auto& profileValue : profilesIt->second.as_array()) {
                            if (!profileValue.is_object()) {
                                continue;
                            }
                            const auto& profileObject = profileValue.as_object();
                            InputProfile profile;
                            ReadString(profileObject, "id", profile.id);
                            ReadString(profileObject, "displayName", profile.displayName);
                            auto bindingsIt = profileObject.find("bindings");
                            if (bindingsIt != profileObject.end() && bindingsIt->second.is_array()) {
                                for (const auto& bindingValue : bindingsIt->second.as_array()) {
                                    if (!bindingValue.is_object()) {
                                        continue;
                                    }
                                    const auto& bindingObject = bindingValue.as_object();
                                    InputBinding binding;
                                    ReadString(bindingObject, "action", binding.action);
                                    std::string deviceType;
                                    if (ReadString(bindingObject, "deviceType", deviceType)) {
                                        binding.deviceType = InputDeviceTypeFromStorage(deviceType);
                                    }
                                    std::string sourceType;
                                    if (ReadString(bindingObject, "source", sourceType)) {
                                        binding.source = InputBindingSourceFromStorage(sourceType);
                                    }
                                    auto keyIt = bindingObject.find("key");
                                    if (keyIt != bindingObject.end() && keyIt->second.is_number()) binding.key = static_cast<int>(keyIt->second.as_number());
                                    auto negativeKeyIt = bindingObject.find("negativeKey");
                                    if (negativeKeyIt != bindingObject.end() && negativeKeyIt->second.is_number()) binding.negativeKey = static_cast<int>(negativeKeyIt->second.as_number());
                                    auto positiveKeyIt = bindingObject.find("positiveKey");
                                    if (positiveKeyIt != bindingObject.end() && positiveKeyIt->second.is_number()) binding.positiveKey = static_cast<int>(positiveKeyIt->second.as_number());
                                    auto axisIt = bindingObject.find("axis");
                                    if (axisIt != bindingObject.end() && axisIt->second.is_number()) binding.axis = static_cast<int>(axisIt->second.as_number());
                                    auto buttonIt = bindingObject.find("button");
                                    if (buttonIt != bindingObject.end() && buttonIt->second.is_number()) binding.button = static_cast<int>(buttonIt->second.as_number());
                                    ReadBool(bindingObject, "invert", binding.invert);
                                    auto deadzoneIt = bindingObject.find("deadzone");
                                    if (deadzoneIt != bindingObject.end() && deadzoneIt->second.is_number()) binding.deadzone = static_cast<float>(deadzoneIt->second.as_number());
                                    auto minIt = bindingObject.find("calibrationMin");
                                    if (minIt != bindingObject.end() && minIt->second.is_number()) binding.calibrationMin = static_cast<float>(minIt->second.as_number());
                                    auto centerIt = bindingObject.find("calibrationCenter");
                                    if (centerIt != bindingObject.end() && centerIt->second.is_number()) binding.calibrationCenter = static_cast<float>(centerIt->second.as_number());
                                    auto maxIt = bindingObject.find("calibrationMax");
                                    if (maxIt != bindingObject.end() && maxIt->second.is_number()) binding.calibrationMax = static_cast<float>(maxIt->second.as_number());
                                    auto responseIt = bindingObject.find("responseExponent");
                                    if (responseIt != bindingObject.end() && responseIt->second.is_number()) binding.responseExponent = static_cast<float>(responseIt->second.as_number());
                                    if (!binding.action.empty()) {
                                        profile.bindings.push_back(std::move(binding));
                                    }
                                }
                            }
                            if (!profile.id.empty()) {
                                inputProfiles_.push_back(std::move(profile));
                            }
                        }
                    }
                    auto wheelSettingsIt = inputSettings.find("wheelSettings");
                    if (wheelSettingsIt != inputSettings.end() && wheelSettingsIt->second.is_array()) {
                        for (const auto& wheelValue : wheelSettingsIt->second.as_array()) {
                            if (!wheelValue.is_object()) {
                                continue;
                            }
                            const auto& wheelObject = wheelValue.as_object();
                            WheelSettingsProfile profile;
                            ReadString(wheelObject, "id", profile.id);
                            ReadString(wheelObject, "displayName", profile.displayName);
                            ReadString(wheelObject, "deviceNamePattern", profile.deviceNamePattern);
                            if (auto it = wheelObject.find("steeringRangeDegrees"); it != wheelObject.end() && it->second.is_number()) profile.steeringRangeDegrees = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("steeringSensitivity"); it != wheelObject.end() && it->second.is_number()) profile.steeringSensitivity = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("steeringSaturation"); it != wheelObject.end() && it->second.is_number()) profile.steeringSaturation = static_cast<float>(it->second.as_number());
                            ReadBool(wheelObject, "steeringInvert", profile.steeringInvert);
                            if (auto it = wheelObject.find("steeringDeadzone"); it != wheelObject.end() && it->second.is_number()) profile.steeringDeadzone = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("steeringCalibrationMin"); it != wheelObject.end() && it->second.is_number()) profile.steeringCalibrationMin = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("steeringCalibrationCenter"); it != wheelObject.end() && it->second.is_number()) profile.steeringCalibrationCenter = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("steeringCalibrationMax"); it != wheelObject.end() && it->second.is_number()) profile.steeringCalibrationMax = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("steeringResponseExponent"); it != wheelObject.end() && it->second.is_number()) profile.steeringResponseExponent = static_cast<float>(it->second.as_number());
                            ReadBool(wheelObject, "combinedPedals", profile.combinedPedals);
                            ReadBool(wheelObject, "throttleInvert", profile.throttleInvert);
                            if (auto it = wheelObject.find("throttleDeadzone"); it != wheelObject.end() && it->second.is_number()) profile.throttleDeadzone = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("throttleCalibrationMin"); it != wheelObject.end() && it->second.is_number()) profile.throttleCalibrationMin = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("throttleCalibrationCenter"); it != wheelObject.end() && it->second.is_number()) profile.throttleCalibrationCenter = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("throttleCalibrationMax"); it != wheelObject.end() && it->second.is_number()) profile.throttleCalibrationMax = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("throttleResponseExponent"); it != wheelObject.end() && it->second.is_number()) profile.throttleResponseExponent = static_cast<float>(it->second.as_number());
                            ReadBool(wheelObject, "brakeInvert", profile.brakeInvert);
                            if (auto it = wheelObject.find("brakeDeadzone"); it != wheelObject.end() && it->second.is_number()) profile.brakeDeadzone = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("brakeCalibrationMin"); it != wheelObject.end() && it->second.is_number()) profile.brakeCalibrationMin = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("brakeCalibrationCenter"); it != wheelObject.end() && it->second.is_number()) profile.brakeCalibrationCenter = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("brakeCalibrationMax"); it != wheelObject.end() && it->second.is_number()) profile.brakeCalibrationMax = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("brakeResponseExponent"); it != wheelObject.end() && it->second.is_number()) profile.brakeResponseExponent = static_cast<float>(it->second.as_number());
                            ReadBool(wheelObject, "clutchInvert", profile.clutchInvert);
                            if (auto it = wheelObject.find("clutchDeadzone"); it != wheelObject.end() && it->second.is_number()) profile.clutchDeadzone = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("clutchCalibrationMin"); it != wheelObject.end() && it->second.is_number()) profile.clutchCalibrationMin = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("clutchCalibrationCenter"); it != wheelObject.end() && it->second.is_number()) profile.clutchCalibrationCenter = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("clutchCalibrationMax"); it != wheelObject.end() && it->second.is_number()) profile.clutchCalibrationMax = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("clutchResponseExponent"); it != wheelObject.end() && it->second.is_number()) profile.clutchResponseExponent = static_cast<float>(it->second.as_number());
                            ReadBool(wheelObject, "forceFeedbackEnabled", profile.forceFeedbackEnabled);
                            if (auto it = wheelObject.find("forceFeedbackOverallStrength"); it != wheelObject.end() && it->second.is_number()) profile.forceFeedbackOverallStrength = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("forceFeedbackSelfAligningTorque"); it != wheelObject.end() && it->second.is_number()) profile.forceFeedbackSelfAligningTorque = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("forceFeedbackRoadEffects"); it != wheelObject.end() && it->second.is_number()) profile.forceFeedbackRoadEffects = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("forceFeedbackSlipEffects"); it != wheelObject.end() && it->second.is_number()) profile.forceFeedbackSlipEffects = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("forceFeedbackCollisionEffects"); it != wheelObject.end() && it->second.is_number()) profile.forceFeedbackCollisionEffects = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("forceFeedbackDamper"); it != wheelObject.end() && it->second.is_number()) profile.forceFeedbackDamper = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("forceFeedbackFriction"); it != wheelObject.end() && it->second.is_number()) profile.forceFeedbackFriction = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("forceFeedbackSpring"); it != wheelObject.end() && it->second.is_number()) profile.forceFeedbackSpring = static_cast<float>(it->second.as_number());
                            if (auto it = wheelObject.find("forceFeedbackMinimumForce"); it != wheelObject.end() && it->second.is_number()) profile.forceFeedbackMinimumForce = static_cast<float>(it->second.as_number());
                            if (!profile.id.empty()) {
                                wheelSettingsProfiles_.push_back(std::move(profile));
                            }
                        }
                    }
                }
            }
        } catch (...) {
            shouldSaveProject = true;
        }
    } else {
        shouldSaveProject = true;
    }

    defaultScenePath_ = NormalizeSlashes(defaultScenePath_.empty() ? "assets/scenes/EditorScene.scene.json" : defaultScenePath_);
    lastScenePath_ = NormalizeSlashes(lastScenePath_.empty() ? defaultScenePath_ : lastScenePath_);

    std::string sceneToLoad = lastScenePath_;
    if (!IsSceneAssetPath(sceneToLoad)) {
        sceneToLoad = defaultScenePath_;
        shouldSaveProject = true;
    }

    if (!fs::exists(ResolveEditorPath(sceneToLoad))) {
        const fs::path defaultSceneAbsolute = ResolveEditorPath(defaultScenePath_);
        const fs::path legacyEditorScene = EngineRootPath() / "config" / "scenes" / "EditorScene.json";
        if (!fs::exists(defaultSceneAbsolute) && fs::exists(legacyEditorScene)) {
            try {
                fs::create_directories(defaultSceneAbsolute.parent_path());
                fs::copy_file(legacyEditorScene, defaultSceneAbsolute, fs::copy_options::overwrite_existing);
                shouldSaveProject = true;
            } catch (...) {}
        }
        sceneToLoad = defaultScenePath_;
    }

    savePath_ = NormalizeSlashes(sceneToLoad);
    if (fs::exists(ResolveEditorPath(savePath_))) {
        Load(savePath_);
    } else {
        objects_.clear();
        selectedIndex_ = -1;
        selectedIndices_.clear();
        undoStack_.clear();
        redoStack_.clear();
        CreateDefaultSceneObjects();
        Save(savePath_);
        shouldSaveProject = true;
    }

    lastScenePath_ = savePath_;
    RefreshProjectFiles();
    if (inputManager_ != nullptr) {
        inputManager_->SetInputProfiles(inputProfiles_);
        inputProfiles_ = inputManager_->GetInputProfiles();
        inputManager_->SetWheelSettingsProfiles(wheelSettingsProfiles_);
        wheelSettingsProfiles_ = inputManager_->GetWheelSettingsProfiles();
    }
    if (shouldSaveProject) {
        SaveProject();
    }
}

void SceneEditor::SaveProject() {
    try {
        EnsureProjectTags();
        const fs::path projectFile = ProjectRootPath() / projectPath_;
        fs::create_directories(projectFile.parent_path());
        std::ofstream out(projectFile, std::ios::trunc);
        if (!out.good()) {
            return;
        }

        out << "{\n";
        out << "  \"version\": 1,\n";
        out << "  \"projectName\": \"" << JsonEscape(projectName_) << "\",\n";
        out << "  \"assetsRoot\": \"" << JsonEscape(assetsRootSetting_) << "\",\n";
        out << "  \"defaultScene\": \"" << JsonEscape(NormalizeSlashes(defaultScenePath_)) << "\",\n";
        out << "  \"lastScene\": \"" << JsonEscape(NormalizeSlashes(lastScenePath_)) << "\",\n";
        out << "  \"tags\": [\n";
        for (std::size_t tagIndex = 0; tagIndex < projectTags_.size(); ++tagIndex) {
            out << "    \"" << JsonEscape(projectTags_[tagIndex]) << "\"" << (tagIndex + 1 < projectTags_.size() ? ",\n" : "\n");
        }
        out << "  ],\n";
        out << "  \"physics\": {\n";
        out << "    \"layers\": [\n";
        for (int layerIndex = 0; layerIndex < kPhysicsLayerCount; ++layerIndex) {
            const std::string layerName = MakePhysicsLayerStorageName(physicsLayerNames_[static_cast<std::size_t>(layerIndex)], layerIndex);
            out << "      \"" << JsonEscape(layerName) << "\"" << (layerIndex + 1 < kPhysicsLayerCount ? ",\n" : "\n");
        }
        out << "    ],\n";
        out << "    \"collisionMatrix\": [\n";
        for (int row = 0; row < kPhysicsLayerCount; ++row) {
            out << "      {\n";
            out << "        \"values\": [";
            for (int column = 0; column < kPhysicsLayerCount; ++column) {
                out << (physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] ? "true" : "false");
                if (column + 1 < kPhysicsLayerCount) {
                    out << ", ";
                }
            }
            out << "]\n";
            out << "      }" << (row + 1 < kPhysicsLayerCount ? ",\n" : "\n");
        }
        out << "    ]\n";
        out << "  },\n";
        out << "  \"input\": {\n";
        out << "    \"profiles\": [\n";
        for (std::size_t profileIndex = 0; profileIndex < inputProfiles_.size(); ++profileIndex) {
            const InputProfile& profile = inputProfiles_[profileIndex];
            out << "      {\n";
            out << "        \"id\": \"" << JsonEscape(profile.id) << "\",\n";
            out << "        \"displayName\": \"" << JsonEscape(profile.displayName) << "\",\n";
            out << "        \"bindings\": [\n";
            for (std::size_t bindingIndex = 0; bindingIndex < profile.bindings.size(); ++bindingIndex) {
                const InputBinding& binding = profile.bindings[bindingIndex];
                out << "          {\n";
                out << "            \"action\": \"" << JsonEscape(binding.action) << "\",\n";
                out << "            \"deviceType\": \"" << InputDeviceTypeToStorage(binding.deviceType) << "\",\n";
                out << "            \"source\": \"" << InputBindingSourceToStorage(binding.source) << "\",\n";
                out << "            \"key\": " << binding.key << ",\n";
                out << "            \"negativeKey\": " << binding.negativeKey << ",\n";
                out << "            \"positiveKey\": " << binding.positiveKey << ",\n";
                out << "            \"axis\": " << binding.axis << ",\n";
                out << "            \"button\": " << binding.button << ",\n";
                out << "            \"invert\": " << (binding.invert ? "true" : "false") << ",\n";
                out << "            \"deadzone\": " << binding.deadzone << ",\n";
                out << "            \"calibrationMin\": " << binding.calibrationMin << ",\n";
                out << "            \"calibrationCenter\": " << binding.calibrationCenter << ",\n";
                out << "            \"calibrationMax\": " << binding.calibrationMax << ",\n";
                out << "            \"responseExponent\": " << binding.responseExponent << "\n";
                out << "          }" << (bindingIndex + 1 < profile.bindings.size() ? ",\n" : "\n");
            }
            out << "        ]\n";
            out << "      }" << (profileIndex + 1 < inputProfiles_.size() ? ",\n" : "\n");
        }
        out << "    ],\n";
        out << "    \"wheelSettings\": [\n";
        for (std::size_t wheelIndex = 0; wheelIndex < wheelSettingsProfiles_.size(); ++wheelIndex) {
            const WheelSettingsProfile& wheel = wheelSettingsProfiles_[wheelIndex];
            out << "      {\n";
            out << "        \"id\": \"" << JsonEscape(wheel.id) << "\",\n";
            out << "        \"displayName\": \"" << JsonEscape(wheel.displayName) << "\",\n";
            out << "        \"deviceNamePattern\": \"" << JsonEscape(wheel.deviceNamePattern) << "\",\n";
            out << "        \"steeringRangeDegrees\": " << wheel.steeringRangeDegrees << ",\n";
            out << "        \"steeringSensitivity\": " << wheel.steeringSensitivity << ",\n";
            out << "        \"steeringSaturation\": " << wheel.steeringSaturation << ",\n";
            out << "        \"steeringInvert\": " << (wheel.steeringInvert ? "true" : "false") << ",\n";
            out << "        \"steeringDeadzone\": " << wheel.steeringDeadzone << ",\n";
            out << "        \"steeringCalibrationMin\": " << wheel.steeringCalibrationMin << ",\n";
            out << "        \"steeringCalibrationCenter\": " << wheel.steeringCalibrationCenter << ",\n";
            out << "        \"steeringCalibrationMax\": " << wheel.steeringCalibrationMax << ",\n";
            out << "        \"steeringResponseExponent\": " << wheel.steeringResponseExponent << ",\n";
            out << "        \"combinedPedals\": " << (wheel.combinedPedals ? "true" : "false") << ",\n";
            out << "        \"throttleInvert\": " << (wheel.throttleInvert ? "true" : "false") << ",\n";
            out << "        \"throttleDeadzone\": " << wheel.throttleDeadzone << ",\n";
            out << "        \"throttleCalibrationMin\": " << wheel.throttleCalibrationMin << ",\n";
            out << "        \"throttleCalibrationCenter\": " << wheel.throttleCalibrationCenter << ",\n";
            out << "        \"throttleCalibrationMax\": " << wheel.throttleCalibrationMax << ",\n";
            out << "        \"throttleResponseExponent\": " << wheel.throttleResponseExponent << ",\n";
            out << "        \"brakeInvert\": " << (wheel.brakeInvert ? "true" : "false") << ",\n";
            out << "        \"brakeDeadzone\": " << wheel.brakeDeadzone << ",\n";
            out << "        \"brakeCalibrationMin\": " << wheel.brakeCalibrationMin << ",\n";
            out << "        \"brakeCalibrationCenter\": " << wheel.brakeCalibrationCenter << ",\n";
            out << "        \"brakeCalibrationMax\": " << wheel.brakeCalibrationMax << ",\n";
            out << "        \"brakeResponseExponent\": " << wheel.brakeResponseExponent << ",\n";
            out << "        \"clutchInvert\": " << (wheel.clutchInvert ? "true" : "false") << ",\n";
            out << "        \"clutchDeadzone\": " << wheel.clutchDeadzone << ",\n";
            out << "        \"clutchCalibrationMin\": " << wheel.clutchCalibrationMin << ",\n";
            out << "        \"clutchCalibrationCenter\": " << wheel.clutchCalibrationCenter << ",\n";
            out << "        \"clutchCalibrationMax\": " << wheel.clutchCalibrationMax << ",\n";
            out << "        \"clutchResponseExponent\": " << wheel.clutchResponseExponent << ",\n";
            out << "        \"forceFeedbackEnabled\": " << (wheel.forceFeedbackEnabled ? "true" : "false") << ",\n";
            out << "        \"forceFeedbackOverallStrength\": " << wheel.forceFeedbackOverallStrength << ",\n";
            out << "        \"forceFeedbackSelfAligningTorque\": " << wheel.forceFeedbackSelfAligningTorque << ",\n";
            out << "        \"forceFeedbackRoadEffects\": " << wheel.forceFeedbackRoadEffects << ",\n";
            out << "        \"forceFeedbackSlipEffects\": " << wheel.forceFeedbackSlipEffects << ",\n";
            out << "        \"forceFeedbackCollisionEffects\": " << wheel.forceFeedbackCollisionEffects << ",\n";
            out << "        \"forceFeedbackDamper\": " << wheel.forceFeedbackDamper << ",\n";
            out << "        \"forceFeedbackFriction\": " << wheel.forceFeedbackFriction << ",\n";
            out << "        \"forceFeedbackSpring\": " << wheel.forceFeedbackSpring << ",\n";
            out << "        \"forceFeedbackMinimumForce\": " << wheel.forceFeedbackMinimumForce << "\n";
            out << "      }" << (wheelIndex + 1 < wheelSettingsProfiles_.size() ? ",\n" : "\n");
        }
        out << "    ]\n";
        out << "  },\n";
        out << "  \"editorState\": {\n";
        out << "    \"selectedProjectDirectory\": \"" << JsonEscape(NormalizeSlashes(selectedProjectDirectory_)) << "\"\n";
        out << "  }\n";
        out << "}\n";
        if (console_) {
            console_->AddLog("Project saved: " + NormalizeSlashes(projectFile.string()));
        }
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to save project file.");
        }
    }
}

#endif

void SceneEditor::RedoVehicleConfig() {
    if (vehicleConfigRedoStack_.empty() || !showVehicleConfigEditor_) {
        return;
    }

    vehicleConfigUndoStack_.push_back({inspectedVehicleConfig_});
    inspectedVehicleConfig_ = vehicleConfigRedoStack_.back().config;
    vehicleConfigRedoStack_.pop_back();
    vehicleConfigEditActive_ = false;
}

void SceneEditor::RedoMaterial() {
    if (materialRedoStack_.empty() || !inspectMaterial_) {
        return;
    }

    Material* current = materialManager_.Get(inspectedMaterialId_);
    if (current == nullptr) {
        return;
    }

    materialUndoStack_.push_back({inspectedMaterialId_, *current});
    const MaterialHistoryState state = materialRedoStack_.back();
    materialRedoStack_.pop_back();
    if (state.materialId != inspectedMaterialId_) {
        return;
    }
    *current = state.material;
    if (!materialManager_.Save(inspectedMaterialId_, *current) && console_) {
        console_->AddError("Failed to auto-save material redo: " + inspectedMaterialId_);
    }
    materialEditActive_ = false;
}

void SceneEditor::UndoVehicleSound() {
    if (vehicleSoundUndoStack_.empty() || !showVehicleSoundEditor_) {
        return;
    }

    vehicleSoundRedoStack_.push_back({inspectedVehicleSound_});
    inspectedVehicleSound_ = vehicleSoundUndoStack_.back().profile;
    vehicleSoundUndoStack_.pop_back();
    vehicleSoundEditActive_ = false;
}

void SceneEditor::RedoVehicleSound() {
    if (vehicleSoundRedoStack_.empty() || !showVehicleSoundEditor_) {
        return;
    }

    vehicleSoundUndoStack_.push_back({inspectedVehicleSound_});
    inspectedVehicleSound_ = vehicleSoundRedoStack_.back().profile;
    vehicleSoundRedoStack_.pop_back();
    vehicleSoundEditActive_ = false;
}

#if 0
void SceneEditor::NewScene() {
    NewScene("Untitled");
}

void SceneEditor::CreateDefaultSceneObjects() {
    try {
        SceneObject planeObject;
        planeObject.id = MakeId("mesh");
        planeObject.name = "Plane";
        planeObject.type = "GameObject";
        planeObject.transform.scale = {10.0f, 1.0f, 10.0f};
        planeObject.meshRenderer.materialId = "pbr_default";
        if (ConfigureBuiltInPrimitive(planeObject, "Plane", builtInPrimitiveMeshes_)) {
            AddDefaultPlaneColliderToPlane(planeObject);
            objects_.push_back(std::move(planeObject));
        }
    } catch (...) {
        if (console_) {
            console_->AddWarning("Failed to add default Plane object.");
        }
    }

    SceneObject cameraObject;
    cameraObject.id = MakeId("camera");
    cameraObject.name = "Camera";
    cameraObject.type = "GameObject";
    cameraObject.transform.position = {0.0f, 2.0f, 5.0f};
    cameraObject.transform.rotationEuler = {-15.0f, 0.0f, 0.0f};
    cameraObject.hasMeshFilter = false;
    cameraObject.hasMeshRenderer = false;
    cameraObject.hasScriptComponent = false;
    cameraObject.hasCamera = true;
    cameraObject.camera = CameraComponent{};
    cameraObject.camera.isMain = true;
    objects_.push_back(std::move(cameraObject));

    SceneObject lightObject;
    lightObject.id = MakeId("light");
    lightObject.name = "Directional Light";
    lightObject.type = "GameObject";
    lightObject.transform.rotationEuler = {-45.0f, 35.0f, 0.0f};
    lightObject.hasMeshFilter = false;
    lightObject.hasMeshRenderer = false;
    lightObject.hasScriptComponent = false;
    lightObject.hasLight = true;
    lightObject.light = LightComponent{};
    lightObject.light.type = LightType::Directional;
    lightObject.light.intensity = 1.5f;
    lightObject.light.range = 100.0f;
    objects_.push_back(std::move(lightObject));

    selectedIndex_ = -1;
    selectedIndices_.clear();
}

void SceneEditor::NewScene(const std::string& sceneName) {
    if (scriptsRunning_) {
        SetScriptsRunning(false);
    }
    ClearScriptRuntime();
    objects_.clear();
    hierarchyOpenStates_.clear();
    selectedIndex_ = -1;
    selectedIndices_.clear();
    undoStack_.clear();
    redoStack_.clear();
    playModeSnapshot_ = {};
    hasPlayModeSnapshot_ = false;
    renamingObjectIndex_ = -1;
    activeGizmoAxis_ = -1;
    hoveredGizmoAxis_ = -1;
    inspectMaterial_ = false;
    CreateDefaultSceneObjects();
    savePath_ = MakeUniqueSceneAssetPath(sceneName.empty() ? "Untitled" : sceneName);
    lastScenePath_ = savePath_;
    activeViewport_ = SceneEditorActiveViewport::Scene;
    SaveProject();
    RefreshProjectFiles();
    if (console_) {
        console_->AddLog("New scene: " + savePath_);
    }
    if (onDirty_) onDirty_();
}

void SceneEditor::SaveCurrentScene() {
    if (!IsSceneAssetPath(savePath_)) {
        savePath_ = MakeUniqueSceneAssetPath("Untitled");
    }
    Save(savePath_);
    lastScenePath_ = NormalizeSlashes(savePath_);
    if (defaultScenePath_.empty()) {
        defaultScenePath_ = lastScenePath_;
    }
    SaveProject();
    RefreshProjectFiles();
    if (console_) {
        console_->AddLog("Scene saved: " + lastScenePath_);
    }
}

void SceneEditor::SaveActiveAsset() {
    if (showShaderGraphEditor_ && !inspectedShaderGraphPath_.empty()) {
        SaveShaderGraphAsset();
        return;
    }

    if (inspectMaterial_ && !inspectedMaterialId_.empty()) {
        Material* material = materialManager_.Get(inspectedMaterialId_);
        if (material == nullptr) {
            materialManager_.LoadAll();
            material = materialManager_.Get(inspectedMaterialId_);
        }

        if (material != nullptr) {
            if (materialManager_.Save(inspectedMaterialId_, *material)) {
                materialManager_.LoadAll();
                if (console_) {
                    console_->AddLog("Saved material: " + inspectedMaterialId_);
                }
            } else if (console_) {
                console_->AddError("Failed to save material: " + inspectedMaterialId_);
            }
            return;
        }

        if (console_) {
            console_->AddError("Material not found: " + inspectedMaterialId_);
        }
        return;
    }

    if (showVehicleConfigEditor_ && !inspectedVehicleConfigPath_.empty()) {
        std::string error;
        const fs::path configPath = ProjectAssetPathToAbsolute(inspectedVehicleConfigPath_);
        if (physics::VehicleConfigLoader::saveToFile(configPath.string(), inspectedVehicleConfig_, &error)) {
            inspectedVehicleConfigLoaded_ = false;
            if (console_) {
                console_->AddLog("Saved vehicle config: " + inspectedVehicleConfigPath_);
            }
        } else if (console_) {
            console_->AddError(error.empty() ? ("Failed to save vehicle config: " + inspectedVehicleConfigPath_) : error);
        }
        return;
    }

    if (showVehicleSoundEditor_ && !inspectedVehicleSoundPath_.empty()) {
        std::string error;
        const fs::path soundPath = ProjectAssetPathToAbsolute(inspectedVehicleSoundPath_);
        if (VehicleSoundProfileLoader::saveToFile(soundPath.string(), inspectedVehicleSound_, &error)) {
            inspectedVehicleSoundLoaded_ = false;
            if (console_) {
                console_->AddLog("Saved vehicle sound profile: " + inspectedVehicleSoundPath_);
            }
        } else if (console_) {
            console_->AddError(error.empty() ? ("Failed to save vehicle sound profile: " + inspectedVehicleSoundPath_) : error);
        }
        return;
    }

    SaveCurrentScene();
}

void SceneEditor::SaveCurrentSceneAs() {
    std::string baseName = fs::path(savePath_).filename().string();
    const std::string suffix = ".scene.json";
    if (EndsWith(ToLowerCopy(baseName), suffix)) {
        baseName = baseName.substr(0, baseName.size() - suffix.size());
    } else {
        baseName = fs::path(baseName).stem().string();
    }
    if (baseName.empty()) {
        baseName = "Scene";
    }
    savePath_ = MakeUniqueSceneAssetPath(baseName);
    SaveCurrentScene();
}

#endif

bool SceneEditor::CreateSceneAsset(const std::string& requestedName, std::string* outScenePath) {
    std::string baseName = TrimCopyLocal(requestedName);
    if (baseName.empty()) {
        if (console_) {
            console_->AddError("Scene name cannot be empty.");
        }
        return false;
    }

    const std::string scenePath = MakeUniqueSceneAssetPath(baseName);
    const std::vector<SceneObject> previousObjects = objects_;
    const int previousSelectedIndex = selectedIndex_;
    const std::vector<int> previousSelectedIndices = selectedIndices_;
    const bool previousInspectMaterial = inspectMaterial_;
    const std::string previousInspectedMaterialId = inspectedMaterialId_;
    const bool previousShowVehicleConfigEditor = showVehicleConfigEditor_;
    const std::string previousInspectedVehicleConfigPath = inspectedVehicleConfigPath_;
    const physics::VehicleConfig previousInspectedVehicleConfig = inspectedVehicleConfig_;
    const bool previousInspectedVehicleConfigLoaded = inspectedVehicleConfigLoaded_;
    const std::string previousInspectedVehicleConfigError = inspectedVehicleConfigError_;

    try {
        objects_.clear();
        selectedIndex_ = -1;
        selectedIndices_.clear();
        inspectMaterial_ = false;
        inspectedMaterialId_.clear();
        showVehicleConfigEditor_ = false;
        inspectedVehicleConfigPath_.clear();
        inspectedVehicleConfigLoaded_ = false;
        inspectedVehicleConfigError_.clear();
        CreateDefaultSceneObjects();
        Save(scenePath);

        objects_ = previousObjects;
        selectedIndex_ = previousSelectedIndex;
        selectedIndices_ = previousSelectedIndices;
        inspectMaterial_ = previousInspectMaterial;
        inspectedMaterialId_ = previousInspectedMaterialId;
        showVehicleConfigEditor_ = previousShowVehicleConfigEditor;
        inspectedVehicleConfigPath_ = previousInspectedVehicleConfigPath;
        inspectedVehicleConfig_ = previousInspectedVehicleConfig;
        inspectedVehicleConfigLoaded_ = previousInspectedVehicleConfigLoaded;
        inspectedVehicleConfigError_ = previousInspectedVehicleConfigError;
        NormalizeSelection();

        if (outScenePath) {
            *outScenePath = scenePath;
        }
        RefreshProjectFiles();
        if (console_) {
            console_->AddLog("Created scene asset: " + scenePath);
        }
        return true;
    } catch (...) {
        objects_ = previousObjects;
        selectedIndex_ = previousSelectedIndex;
        selectedIndices_ = previousSelectedIndices;
        inspectMaterial_ = previousInspectMaterial;
        inspectedMaterialId_ = previousInspectedMaterialId;
        showVehicleConfigEditor_ = previousShowVehicleConfigEditor;
        inspectedVehicleConfigPath_ = previousInspectedVehicleConfigPath;
        inspectedVehicleConfig_ = previousInspectedVehicleConfig;
        inspectedVehicleConfigLoaded_ = previousInspectedVehicleConfigLoaded;
        inspectedVehicleConfigError_ = previousInspectedVehicleConfigError;
        NormalizeSelection();
        if (console_) {
            console_->AddError("Failed to create scene asset.");
        }
        return false;
    }
}

bool SceneEditor::CreateVehicleConfigAsset(const std::string& requestedName, std::string* outConfigPath) {
    std::string baseName = TrimCopyLocal(requestedName);
    if (baseName.empty()) {
        if (console_) {
            console_->AddError("Vehicle profile name cannot be empty.");
        }
        return false;
    }

    const std::string suffix = ".vehicle.json";
    const std::string lowerBaseName = ToLowerCopy(baseName);
    if (EndsWith(lowerBaseName, suffix)) {
        baseName.resize(baseName.size() - suffix.size());
    }

    std::string sanitized;
    sanitized.reserve(baseName.size());
    for (char& ch : baseName) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            sanitized.push_back(ch == ' ' ? '_' : ch);
        }
    }
    sanitized = TrimCopyLocal(sanitized);
    if (sanitized.empty()) {
        if (console_) {
            console_->AddError("Vehicle profile name must contain letters or numbers.");
        }
        return false;
    }

    const fs::path assetsRoot = FindAssetsRoot();
    fs::path targetPath = ProjectAssetPathToAbsolute(selectedProjectDirectory_ + "/" + sanitized + suffix);
    if (!IsUnderPath(targetPath, assetsRoot)) {
        if (console_) {
            console_->AddError("Vehicle profile creation blocked outside assets: " + sanitized);
        }
        return false;
    }

    int duplicateIndex = 1;
    while (fs::exists(targetPath)) {
        targetPath = ProjectAssetPathToAbsolute(selectedProjectDirectory_ + "/" + sanitized + "_" + std::to_string(duplicateIndex) + suffix);
        ++duplicateIndex;
    }

    physics::VehicleConfig config;
    config.name = sanitized;
    config.wheels = {
        {"Front Left",  {-0.85f,  1.35f, 0.0f}, 0.35f, 0.24f, 15.0f, 1.0f, 0.55f, 0.0f, 0.0f, 1.0f, 10000.0f, 8000.0f, 3000.0f, true,  true},
        {"Front Right", { 0.85f,  1.35f, 0.0f}, 0.35f, 0.24f, 15.0f, 1.0f, 0.55f, 0.0f, 0.0f, 1.0f, 10000.0f, 8000.0f, 3000.0f, true,  true},
        {"Rear Left",   {-0.85f, -1.35f, 0.0f}, 0.35f, 0.24f, 15.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f, 10000.0f, 8000.0f, 3200.0f, true,  true},
        {"Rear Right",  { 0.85f, -1.35f, 0.0f}, 0.35f, 0.24f, 15.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f, 10000.0f, 8000.0f, 3200.0f, true,  true}
    };

    std::string error;
    fs::create_directories(targetPath.parent_path());
    if (!physics::VehicleConfigLoader::saveToFile(targetPath.string(), config, &error)) {
        if (console_) {
            console_->AddError(error.empty() ? ("Failed to create vehicle profile: " + sanitized) : error);
        }
        return false;
    }

    const std::string createdProjectPath = ToProjectAssetPath(targetPath, assetsRoot);
    if (outConfigPath) {
        *outConfigPath = createdProjectPath;
    }
    RefreshProjectFiles();
    if (console_) {
        console_->AddLog("Created vehicle profile: " + createdProjectPath);
    }
    return true;
}

bool SceneEditor::CreateVehicleSoundAsset(const std::string& requestedName, std::string* outProfilePath) {
    std::string baseName = TrimCopyLocal(requestedName);
    if (baseName.empty()) {
        if (console_) {
            console_->AddError("Vehicle sound profile name cannot be empty.");
        }
        return false;
    }

    const std::string suffix = ".vehiclesound.json";
    const std::string lowerBaseName = ToLowerCopy(baseName);
    if (EndsWith(lowerBaseName, suffix)) {
        baseName.resize(baseName.size() - suffix.size());
    }

    std::string sanitized;
    sanitized.reserve(baseName.size());
    for (char& ch : baseName) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            sanitized.push_back(ch == ' ' ? '_' : ch);
        }
    }
    sanitized = TrimCopyLocal(sanitized);
    if (sanitized.empty()) {
        if (console_) {
            console_->AddError("Vehicle sound profile name must contain letters or numbers.");
        }
        return false;
    }

    const fs::path assetsRoot = FindAssetsRoot();
    fs::path targetPath = ProjectAssetPathToAbsolute(selectedProjectDirectory_ + "/" + sanitized + suffix);
    if (!IsUnderPath(targetPath, assetsRoot)) {
        if (console_) {
            console_->AddError("Vehicle sound profile creation blocked outside assets: " + sanitized);
        }
        return false;
    }

    int duplicateIndex = 1;
    while (fs::exists(targetPath)) {
        targetPath = ProjectAssetPathToAbsolute(selectedProjectDirectory_ + "/" + sanitized + "_" + std::to_string(duplicateIndex) + suffix);
        ++duplicateIndex;
    }

    raceman::VehicleSoundProfile profile = raceman::VehicleSoundProfileLoader::makeDefault();
    profile.name = sanitized;

    std::string error;
    fs::create_directories(targetPath.parent_path());
    if (!raceman::VehicleSoundProfileLoader::saveToFile(targetPath.string(), profile, &error)) {
        if (console_) {
            console_->AddError(error.empty() ? ("Failed to create vehicle sound profile: " + sanitized) : error);
        }
        return false;
    }

    const std::string createdProjectPath = ToProjectAssetPath(targetPath, assetsRoot);
    if (outProfilePath) {
        *outProfilePath = createdProjectPath;
    }
    RefreshProjectFiles();
    if (console_) {
        console_->AddLog("Created vehicle sound profile: " + createdProjectPath);
    }
    return true;
}

bool SceneEditor::OpenSceneAsset(const std::string& path) {
    const std::string scenePath = NormalizeSlashes(path);
    if (!IsSceneAssetPath(scenePath)) {
        return false;
    }
    if (!fs::exists(ResolveEditorPath(scenePath))) {
        if (console_) {
            console_->AddError("Scene asset not found: " + scenePath);
        }
        return false;
    }
    if (scriptsRunning_) {
        SetScriptsRunning(false);
    }
    ClearScriptRuntime();
    Load(scenePath);
    savePath_ = scenePath;
    lastScenePath_ = scenePath;
    SaveProject();
    RefreshProjectFiles();
    if (console_) {
        console_->AddLog("Opened scene: " + scenePath);
    }
    return true;
}

std::vector<std::string> SceneEditor::GetSceneAssetPaths() const {
    std::vector<std::string> scenes;
    for (const std::string& file : projectFiles_) {
        if (IsSceneAssetPath(file)) {
            scenes.push_back(file);
        }
    }
    return scenes;
}

void SceneEditor::UpdateProjectSceneReference(const std::string& oldPath, const std::string& newPath) {
    const std::string oldScenePath = NormalizeSlashes(oldPath);
    const std::string newScenePath = NormalizeSlashes(newPath);
    bool changed = false;

    if (NormalizeSlashes(defaultScenePath_) == oldScenePath) {
        defaultScenePath_ = !newScenePath.empty() ? newScenePath : "assets/scenes/EditorScene.scene.json";
        changed = true;
    }
    if (NormalizeSlashes(lastScenePath_) == oldScenePath) {
        lastScenePath_ = !newScenePath.empty() ? newScenePath : defaultScenePath_;
        changed = true;
    }
    if (NormalizeSlashes(savePath_) == oldScenePath) {
        savePath_ = !newScenePath.empty() ? newScenePath : lastScenePath_;
        changed = true;
    }

    if (changed) {
        SaveProject();
    }
}

std::string SceneEditor::MakeUniqueSceneAssetPath(const std::string& baseName) const {
    std::string cleanBase;
    cleanBase.reserve(baseName.size());
    for (char ch : baseName) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-') {
            cleanBase.push_back(ch);
        } else if (ch == ' ' || ch == '.') {
            cleanBase.push_back('_');
        }
    }
    if (cleanBase.empty()) {
        cleanBase = "Scene";
    }

    std::string directory = selectedProjectDirectory_.empty() ? std::string("assets") : NormalizeSlashes(selectedProjectDirectory_);
    if (directory != "assets" && directory.rfind("assets/", 0) != 0) {
        directory = "assets";
    }
    const std::string prefix = directory + "/";
    for (int i = 0; i < 10000; ++i) {
        const std::string candidate = prefix + cleanBase + (i == 0 ? "" : "_" + std::to_string(i)) + ".scene.json";
        if (!fs::exists(ResolveEditorPath(candidate))) {
            return candidate;
        }
    }
    return prefix + cleanBase + "_9999.scene.json";
}

std::string SceneEditor::MakeId(const std::string& base) {
    static int counter = 0;
    std::string prefix = base.empty() ? std::string("obj") : base;
    for (;;) {
        const std::string candidate = prefix + "_" + std::to_string(++counter);
        const auto existing = std::find_if(objects_.begin(), objects_.end(), [&](const SceneObject& object) {
            return object.id == candidate;
        });
        if (existing == objects_.end()) {
            return candidate;
        }
    }
}

SceneProfilerStats SceneEditor::CollectProfilerStats() const {
    SceneProfilerStats stats;
    std::unordered_map<std::string, SceneMeshContributorStats> meshContributors;

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        const SceneObject& object = objects_[objectIndex];
        if (!IsObjectEffectivelyEnabled(objectIndex)) {
            continue;
        }

        if (object.hasMeshFilter && object.hasMeshRenderer && object.meshFilter.enabled && object.meshRenderer.enabled &&
            object.meshFilter.vao != 0 && object.meshFilter.indexCount > 0) {
            ++stats.visibleMeshCount;
        }
        if (object.hasLight && object.light.enabled) {
            ++stats.visibleLightCount;
        }
        if (object.hasCharacterController && object.characterController.enabled) {
            ++stats.characterCount;
            continue;
        }

        const SceneColliderType colliderType = GetEnabledColliderType(object);
        if (colliderType == SceneColliderType::None) {
            continue;
        }

        ++stats.bodyCount;
        switch (colliderType) {
        case SceneColliderType::Box:
            ++stats.boxColliderCount;
            break;
        case SceneColliderType::Sphere:
            ++stats.sphereColliderCount;
            break;
        case SceneColliderType::Capsule:
            ++stats.capsuleColliderCount;
            break;
        case SceneColliderType::Plane:
            ++stats.planeColliderCount;
            break;
        case SceneColliderType::Mesh: {
            ++stats.meshColliderCount;
            if (object.meshCollider.mode == MeshColliderMode::ConvexHull) {
                ++stats.convexHullColliderCount;
            } else {
                ++stats.triangleMeshColliderCount;
            }

            if (object.hasMeshFilter && !object.meshFilter.sourcePath.empty()) {
                const std::string key = object.meshFilter.sourcePath + "#" + std::to_string(object.meshFilter.meshIndex) +
                                        "#" + std::to_string(static_cast<int>(object.meshCollider.mode));
                SceneMeshContributorStats& contributor = meshContributors[key];
                contributor.meshAssetPath = object.meshFilter.sourcePath;
                contributor.meshName = object.meshFilter.meshName;
                contributor.meshIndex = object.meshFilter.meshIndex;
                contributor.meshMode = object.meshCollider.mode;
                contributor.triangleCount = object.meshFilter.indexCount / 3;
                ++contributor.objectCount;
            }
            break;
        }
        case SceneColliderType::None:
            break;
        }
    }

    stats.meshContributors.reserve(meshContributors.size());
    for (auto& [key, contributor] : meshContributors) {
        (void)key;
        stats.meshContributors.push_back(std::move(contributor));
    }
    std::sort(stats.meshContributors.begin(), stats.meshContributors.end(),
              [](const SceneMeshContributorStats& a, const SceneMeshContributorStats& b) {
                  const std::uint64_t scoreA = static_cast<std::uint64_t>(a.objectCount) * (std::max<std::uint64_t>)(1, a.triangleCount);
                  const std::uint64_t scoreB = static_cast<std::uint64_t>(b.objectCount) * (std::max<std::uint64_t>)(1, b.triangleCount);
                  if (scoreA != scoreB) {
                      return scoreA > scoreB;
                  }
                  return a.meshAssetPath < b.meshAssetPath;
              });

    return stats;
}

void SceneEditor::SubmitDraws(Renderer& renderer, bool editorInteraction) {
    if (editorInteraction) {
        UpdateGizmo(renderer);
    }

    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const auto& o = objects_[i];
        if (!IsObjectEffectivelyEnabled(i) || !o.hasLight || !o.light.enabled) {
            continue;
        }

        const glm::mat4 worldMatrix = GetObjectWorldMatrix(i);
        LightDrawCommand light;
        if (o.light.type == LightType::Directional) {
            light.type = RenderLightType::Directional;
        } else if (o.light.type == LightType::Spot) {
            light.type = RenderLightType::Spot;
        } else {
            light.type = RenderLightType::Point;
        }
        light.position = glm::vec3(worldMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        light.direction = glm::normalize(glm::vec3(worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        light.color = o.light.color;
        light.intensity = o.light.intensity;
        light.range = o.light.range;
        light.spotAngleDegrees = o.light.spotAngleDegrees;
        renderer.SubmitLight(light);
    }

    // Extract frustum planes from clip matrix (Gribb-Hartmann method)
    // Column-major glm: vp[col][row]
    glm::vec4 frustumPlanes[6];
    if (enableFrustumCulling_) {
        const glm::mat4 vp = renderer.GetProj() * renderer.GetView();
        frustumPlanes[0] = { vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0] }; // Left
        frustumPlanes[1] = { vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0] }; // Right
        frustumPlanes[2] = { vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1] }; // Bottom
        frustumPlanes[3] = { vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1] }; // Top
        frustumPlanes[4] = { vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2] }; // Near
        frustumPlanes[5] = { vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2] }; // Far
    }

    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const auto& o = objects_[i];
        if (!IsObjectEffectivelyEnabled(i)) continue;
        if (!o.hasMeshFilter || !o.hasMeshRenderer) continue;
        if (!o.meshFilter.enabled || !o.meshRenderer.enabled) continue;
        if (o.meshFilter.vao == 0 || o.meshFilter.indexCount == 0) continue;

        MeshDrawCommand cmd;
        cmd.vao = o.meshFilter.vao;
        cmd.indexCount = o.meshFilter.indexCount;
        {
            glm::mat4 m = GetObjectWorldMatrix(i);
            const glm::vec3& po = o.meshFilter.pivotOffset;
            if (po.x != 0.0f || po.y != 0.0f || po.z != 0.0f) {
                m = m * glm::translate(glm::mat4(1.0f), -po);
            }
            cmd.modelMatrix = m;
        }

        // Frustum culling: test world-space AABB against all 6 planes
        if (enableFrustumCulling_) {
            const glm::vec3& lMin = o.meshFilter.localBoundsMin;
            const glm::vec3& lMax = o.meshFilter.localBoundsMax;
            const glm::vec3 corners[8] = {
                {lMin.x, lMin.y, lMin.z}, {lMax.x, lMin.y, lMin.z},
                {lMax.x, lMax.y, lMin.z}, {lMin.x, lMax.y, lMin.z},
                {lMin.x, lMin.y, lMax.z}, {lMax.x, lMin.y, lMax.z},
                {lMax.x, lMax.y, lMax.z}, {lMin.x, lMax.y, lMax.z}
            };
            glm::vec3 worldMin = glm::vec3(cmd.modelMatrix * glm::vec4(corners[0], 1.0f));
            glm::vec3 worldMax = worldMin;
            for (int c = 1; c < 8; ++c) {
                const glm::vec3 wp = glm::vec3(cmd.modelMatrix * glm::vec4(corners[c], 1.0f));
                if (wp.x < worldMin.x) worldMin.x = wp.x;
                if (wp.y < worldMin.y) worldMin.y = wp.y;
                if (wp.z < worldMin.z) worldMin.z = wp.z;
                if (wp.x > worldMax.x) worldMax.x = wp.x;
                if (wp.y > worldMax.y) worldMax.y = wp.y;
                if (wp.z > worldMax.z) worldMax.z = wp.z;
            }
            bool culled = false;
            for (int p = 0; p < 6; ++p) {
                const glm::vec4& plane = frustumPlanes[p];
                // Positive vertex: pick the corner that maximises dot(n, v)
                const glm::vec3 pv = {
                    plane.x >= 0.0f ? worldMax.x : worldMin.x,
                    plane.y >= 0.0f ? worldMax.y : worldMin.y,
                    plane.z >= 0.0f ? worldMax.z : worldMin.z
                };
                if (plane.x * pv.x + plane.y * pv.y + plane.z * pv.z + plane.w < 0.0f) {
                    culled = true;
                    break;
                }
            }
            if (culled) {
                renderer.ReportFrustumCulled();
                // When frustum cull debug is on, culled objects are simply not drawn
                // so you see exactly what the camera renders — no extra gizmos.
                continue;
            }
        }

        cmd.materialId = o.meshRenderer.materialId.empty() ? std::string("pbr_default") : o.meshRenderer.materialId;
        if (const Material* material = materialManager_.Get(cmd.materialId)) {
            cmd.shaderId = material->shader;
            cmd.color = {
                material->albedoColor[0],
                material->albedoColor[1],
                material->albedoColor[2],
                material->albedoColor[3]
            };
            cmd.emissiveColor = {
                material->emissiveColor[0],
                material->emissiveColor[1],
                material->emissiveColor[2]
            };
            cmd.metallic = material->metallic;
            cmd.roughness = material->roughness;
            cmd.uvTiling = {material->uvTiling[0], material->uvTiling[1]};
            cmd.uvOffset = {material->uvOffset[0], material->uvOffset[1]};
            cmd.unlit = ToLowerCopy(material->shader) == "unlit";
            cmd.materialTextureIds[0] = LoadMaterialTextureCached(material->texAlbedo, materialTextureCache_, console_);
            cmd.materialTextureIds[1] = LoadMaterialTextureCached(material->texNormal, materialTextureCache_, console_);
            cmd.materialTextureIds[2] = LoadMaterialTextureCached(material->texMetallic, materialTextureCache_, console_);
            cmd.materialTextureIds[3] = LoadMaterialTextureCached(material->texRoughness, materialTextureCache_, console_);
            cmd.materialTextureIds[4] = LoadMaterialTextureCached(material->texAo, materialTextureCache_, console_);
            const ShaderDefinition& shaderDefinition = ShaderRegistry::Resolve(material->shader);
            for (const ShaderDefinition::Property& property : shaderDefinition.properties) {
                if (property.id == "albedoColor" || property.id == "emissiveColor" || property.id == "metallic" ||
                    property.id == "roughness" || property.id == "uvTiling" || property.id == "uvOffset" ||
                    property.id == "albedoTexture" || property.id == "normalTexture" || property.id == "metallicTexture" ||
                    property.id == "roughnessTexture" || property.id == "aoTexture") {
                    continue;
                }
                MeshDrawCommand::MaterialUniform uniform;
                uniform.uniformName = property.uniformName;
                uniform.textureUseUniform = property.textureUseUniform;
                uniform.value.type = property.type;
                uniform.value.values[0] = property.defaultValues[0];
                uniform.value.values[1] = property.defaultValues[1];
                uniform.value.values[2] = property.defaultValues[2];
                uniform.value.values[3] = property.defaultValues[3];
                uniform.value.boolValue = property.defaultBool;
                if (auto propertyIt = material->properties.find(property.id); propertyIt != material->properties.end()) {
                    uniform.value = propertyIt->second;
                }
                if (uniform.value.type == MaterialPropertyType::Texture2D) {
                    uniform.textureId = LoadMaterialTextureCached(uniform.value.texturePath, materialTextureCache_, console_);
                }
                cmd.materialUniforms.push_back(uniform);
            }
        } else {
            cmd.color = o.meshRenderer.color;
        }
        cmd.diffuseTextureId = cmd.materialTextureIds[0] != 0 ? cmd.materialTextureIds[0] : o.meshFilter.diffuseTextureId;
        cmd.useDiffuseTexture = (cmd.diffuseTextureId != 0);

        renderer.SubmitMesh(cmd);
    }

    if (editorInteraction) {
        SubmitGizmo(renderer);
        SubmitCullingDebug(renderer);
    }
}

void SceneEditor::SetSavePath(const std::string& path) {
    savePath_ = NormalizeSlashes(path);
}

int SceneEditor::ClampPhysicsLayerIndex(int layer) const {
    return (std::max)(0, (std::min)(kPhysicsLayerCount - 1, layer));
}

const char* SceneEditor::GetPhysicsLayerName(int layer) const {
    const int clampedLayer = ClampPhysicsLayerIndex(layer);
    const std::string& name = physicsLayerNames_[static_cast<std::size_t>(clampedLayer)];
    return name.empty() ? "Default" : name.c_str();
}

void SceneEditor::ResetPhysicsLayerSettings() {
    physicsLayerNames_ = MakeDefaultPhysicsLayerNames();
    physicsLayerCollisionMatrix_ = MakeDefaultPhysicsLayerCollisionMatrix();
}

void SceneEditor::EnsureProjectTags() {
    std::vector<std::string> normalized;
    normalized.reserve(projectTags_.size() + 1);
    normalized.push_back("Untagged");
    for (const std::string& tag : projectTags_) {
        if (tag.empty() || tag == "Untagged") {
            continue;
        }
        if (std::find(normalized.begin(), normalized.end(), tag) == normalized.end()) {
            normalized.push_back(tag);
        }
    }
    projectTags_ = std::move(normalized);
}

bool SceneEditor::AddProjectTag(const std::string& tag) {
    const std::string normalizedTag = TrimCopyLocal(tag);
    if (normalizedTag.empty()) {
        return false;
    }
    EnsureProjectTags();
    if (std::find(projectTags_.begin(), projectTags_.end(), normalizedTag) != projectTags_.end()) {
        return false;
    }
    projectTags_.push_back(normalizedTag);
    SaveProject();
    return true;
}

bool SceneEditor::RemoveProjectTag(int index) {
    EnsureProjectTags();
    if (index <= 0 || index >= static_cast<int>(projectTags_.size())) {
        return false;
    }
    const std::string removedTag = projectTags_[static_cast<std::size_t>(index)];
    projectTags_.erase(projectTags_.begin() + index);
    for (SceneObject& object : objects_) {
        if (object.tag == removedTag) {
            object.tag = "Untagged";
        }
    }
    SaveProject();
    if (onDirty_) {
        onDirty_();
    }
    return true;
}

} // namespace raceman


#include "SceneEditorInternal.h"
#include "../physics/PhysicsWorld.h"
#include "../physics/SimpleJson.h"
#include "../scripting/ScriptRegistry.h"

#include <glad/glad.h>
#include <imgui/imgui_internal.h>

#include <cmath>
#include <iostream>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

struct ColliderWorldAabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

bool ReadVec3(const raceman::physics::json::Object& object, const std::string& key, glm::vec3& out) {
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

bool ReadVec4(const raceman::physics::json::Object& object, const std::string& key, glm::vec4& out) {
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

bool ReadBool(const raceman::physics::json::Object& object, const std::string& key, bool& out) {
    auto it = object.find(key);
    if (it == object.end() || !it->second.is_bool()) {
        return false;
    }
    out = it->second.as_bool();
    return true;
}

bool ReadString(const raceman::physics::json::Object& object, const std::string& key, std::string& out) {
    auto it = object.find(key);
    if (it == object.end() || !it->second.is_string()) {
        return false;
    }
    out = it->second.as_string();
    return true;
}

std::string RigidbodyBodyTypeToString(RigidbodyBodyType bodyType) {
    return bodyType == RigidbodyBodyType::Static ? "Static" : "Dynamic";
}

RigidbodyBodyType RigidbodyBodyTypeFromString(const std::string& value) {
    return value == "Static" ? RigidbodyBodyType::Static : RigidbodyBodyType::Dynamic;
}

std::string LightTypeToString(LightType type) {
    if (type == LightType::Directional) return "Directional";
    if (type == LightType::Spot) return "Spot";
    return "Point";
}

LightType LightTypeFromString(const std::string& value) {
    if (value == "Directional") return LightType::Directional;
    if (value == "Spot") return LightType::Spot;
    return LightType::Point;
}

float MaxAbsComponent(const glm::vec3& value) {
    return (std::max)((std::max)(std::abs(value.x), std::abs(value.y)), std::abs(value.z));
}

glm::mat4 BuildTransformMatrix(const Transform& transform) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, transform.position);
    const glm::vec3 rads = glm::radians(transform.rotationEuler);
    model = glm::rotate(model, rads.z, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, rads.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, rads.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, transform.scale);
    return model;
}

Transform TransformFromMatrix(const glm::mat4& matrix) {
    Transform transform;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::quat orientation;
    if (glm::decompose(matrix, transform.scale, orientation, transform.position, skew, perspective)) {
        transform.rotationEuler = glm::degrees(glm::eulerAngles(orientation));
    }
    return transform;
}

glm::vec3 TransformPoint(const glm::mat4& transform, const glm::vec3& point) {
    return glm::vec3(transform * glm::vec4(point, 1.0f));
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

glm::vec3 CameraUpFromEuler(const glm::vec3& rotationEuler) {
    const glm::vec3 up = glm::vec3(BuildRotationMatrix(rotationEuler) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    return glm::length(up) > 0.0001f ? glm::normalize(up) : glm::vec3{0.0f, 1.0f, 0.0f};
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
    if (object.hasBoxCollider && object.boxCollider.enabled && !object.boxCollider.isTrigger) {
        colliders.push_back(MakeBoxColliderWorldAabb(object));
    }
    if (object.hasSphereCollider && object.sphereCollider.enabled && !object.sphereCollider.isTrigger) {
        colliders.push_back(MakeSphereColliderWorldAabb(object));
    }
    if (object.hasCapsuleCollider && object.capsuleCollider.enabled && !object.capsuleCollider.isTrigger) {
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

bool ContainsText(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

bool LoadBuiltInPlaneMesh(SceneObject& object) {
    auto model = raceman::LoadModelFromFile(ProjectAssetPathToAbsolute(kPlaneObjAssetPath).string());
    const auto infos = raceman::GetMeshInfos(model);
    if (infos.empty()) {
        return false;
    }

    object.type = "Mesh";
    object.hasMeshFilter = true;
    object.hasMeshRenderer = true;
    object.meshRenderer.color = {1.0f, 1.0f, 1.0f, 1.0f};
    if (object.meshRenderer.materialId.empty()) {
        object.meshRenderer.materialId = "pbr_default";
    }
    object.meshFilter.sourcePath = kPlaneObjAssetPath;
    ApplyMeshInfoToSceneObject(object, infos.front(), model);
    return true;
}

bool ShouldFallbackToBuiltInPlane(const SceneObject& object) {
    if (!object.hasMeshFilter || object.meshFilter.meshType != "Mesh") {
        return false;
    }
    const std::string sourcePath = ToLowerCopy(NormalizeSlashes(object.meshFilter.sourcePath));
    return sourcePath == "assets/mesh/plane.obj" ||
           (ContainsText(sourcePath, "assets/imports/plane_") && EndsWith(sourcePath, "/plane.obj"));
}

void AddDefaultPlaneColliderToPlane(SceneObject& object) {
    object.hasBoxCollider = false;
    object.hasSphereCollider = false;
    object.hasCapsuleCollider = false;
    object.hasPlaneCollider = true;
    object.boxCollider = BoxColliderComponent{};
    object.sphereCollider = SphereColliderComponent{};
    object.capsuleCollider = CapsuleColliderComponent{};
    object.planeCollider = PlaneColliderComponent{};
    object.planeCollider.normal = {0.0f, 1.0f, 0.0f};
    object.planeCollider.offset = 0.0f;
    object.planeCollider.infinite = true;
    object.planeCollider.halfExtent = 1000.0f;
}

void WriteTextFile(const fs::path& path, const std::string& content) {
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
    std::string registry;
    registry += "#include \"ScriptRegistry.h\"\n\n";
    for (const ScriptSourceInfo& script : scripts) {
        registry += "#include \"../../Project/" + script.projectHeaderPath + "\"\n";
    }
    registry += "\nnamespace raceman {\nnamespace {\n\n";
    for (const ScriptSourceInfo& script : scripts) {
        const std::string& scriptName = script.name;
        registry += "std::unique_ptr<IObjectScript> Create" + scriptName + "() {\n";
        registry += "    return std::make_unique<scripts::" + scriptName + ">();\n";
        registry += "}\n\n";
    }
    registry += "} // namespace\n\n";
    registry += "const std::vector<ScriptDescriptor>& GetRegisteredScripts() {\n";
    registry += "    static const std::vector<ScriptDescriptor> scripts = {\n";
    for (const ScriptSourceInfo& script : scripts) {
        registry += "        {\"" + script.name + "\", \"" + script.projectSourcePath + "\", &Create" + script.name + "},\n";
    }
    registry += "    };\n";
    registry += "    return scripts;\n";
    registry += "}\n\n";
    registry += "const ScriptDescriptor* FindRegisteredScript(const std::string& name) {\n";
    registry += "    for (const ScriptDescriptor& script : GetRegisteredScripts()) {\n";
    registry += "        if (script.name == name) {\n";
    registry += "            return &script;\n";
    registry += "        }\n";
    registry += "    }\n";
    registry += "    return nullptr;\n";
    registry += "}\n\n";
    registry += "std::unique_ptr<IObjectScript> CreateRegisteredScript(const std::string& name) {\n";
    registry += "    const ScriptDescriptor* script = FindRegisteredScript(name);\n";
    registry += "    if (script == nullptr || script->create == nullptr) {\n";
    registry += "        return {};\n";
    registry += "    }\n";
    registry += "    return script->create();\n";
    registry += "}\n\n";
    registry += "} // namespace raceman\n";
    return registry;
}

} // namespace

SceneEditor::SceneEditor() {
    // Load materials at startup
    materialManager_.LoadAll();
    RefreshProjectFiles();
    LoadProject();
    materialManager_.LoadAll();
    RefreshProjectFiles();
}

SceneEditor::~SceneEditor() {
    for (const auto& [filename, textureId] : componentIconTextures_) {
        (void)filename;
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
    const glm::mat4 worldMatrix = GetObjectWorldMatrix(cameraIndex);
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
    HandleEditorShortcuts();
    UpdateScripts(deltaTime);
    UpdatePhysics(deltaTime);

    RenderDockspaceHost();
    RenderScenePanel();
    RenderInspectorPanel();
    RenderProjectPanel();
    RenderViewportPanel();
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
    return BuildRenderViewportFromLogicalRect(sceneViewportPos_, sceneViewportSize_, framebufferWidth, framebufferHeight);
}

RendererViewport SceneEditor::GetGameRenderViewport(int framebufferWidth, int framebufferHeight) const {
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
            outPos = {contentMin.x, contentMin.y};
            outSize = {contentAvail.x, contentAvail.y};
            outHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
                && ImRect(contentMin, ImVec2(contentMin.x + contentAvail.x, contentMin.y + contentAvail.y)).Contains(ImGui::GetIO().MousePos);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 contentMax{contentMin.x + contentAvail.x, contentMin.y + contentAvail.y};
            if (textureId != 0) {
                ImGui::Image(static_cast<ImTextureID>(textureId),
                    contentAvail,
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
            } else {
                drawList->AddRectFilled(contentMin, contentMax, IM_COL32(24, 28, 34, 255));
            }
            drawList->AddRect(contentMin, contentMax, IM_COL32(70, 90, 120, 180));

            if (viewportType == SceneEditorActiveViewport::Game) {
                glm::mat4 view;
                glm::mat4 proj;
                const float aspect = contentAvail.y > 0.5f ? contentAvail.x / contentAvail.y : 1.0f;
                if (!TryGetGameCamera(view, proj, aspect)) {
                    const char* noCameraText = "No Camera";
                    const ImVec2 noCameraSize = ImGui::CalcTextSize(noCameraText);
                    drawList->AddText(ImVec2((contentMin.x + contentMax.x) * 0.5f - noCameraSize.x * 0.5f, (contentMin.y + contentMax.y) * 0.5f - 8.0f), IM_COL32(255, 200, 96, 255), noCameraText);
                }
                ImGui::Dummy(contentAvail);
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
    } else {
        sceneViewportSize_ = glm::vec2(0.0f);
    }
    ImGui::End();

    bool gameWindowOpen = ImGui::Begin("Game View", nullptr, viewportWindowFlags);
    if (gameWindowOpen) {
        renderViewportSurface("GameViewportSurface", SceneEditorActiveViewport::Game, gameViewportTextureId_, gameViewportPos_, gameViewportSize_, gameViewportHovered_);
    } else {
        gameViewportSize_ = glm::vec2(0.0f);
    }
    ImGui::End();

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

    viewportHovered_ = sceneViewportHovered_ || gameViewportHovered_;
    sceneViewportFocused_ = activeViewport_ == SceneEditorActiveViewport::Scene;
    gameViewportFocused_ = activeViewport_ == SceneEditorActiveViewport::Game;
    viewportFocused_ = sceneViewportFocused_ || gameViewportFocused_;
    ImGui::PopStyleColor();
}

void SceneEditor::RenderDockspaceHost() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
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
                ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

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

void SceneEditor::HandleEditorShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (IsCtrlSPressed()) {
        SaveActiveAsset();
        return;
    }
    if (io.WantTextInput) {
        return;
    }
    if (IsCtrlZPressed()) {
        Undo();
        return;
    }
    if (IsCtrlYPressed()) {
        Redo();
        return;
    }

    if (!io.KeyCtrl && !io.KeyAlt && !io.MouseDown[1]) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            gizmoMode_ = GizmoMode::Move;
        } else if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            gizmoMode_ = GizmoMode::Rotate;
        } else if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            gizmoMode_ = GizmoMode::Scale;
        }
    }
}

void SceneEditor::UpdateScripts(float deltaTime) {
    if (!scriptsRunning_ || scriptsPaused_ || deltaTime <= 0.0f) {
        return;
    }

    for (RuntimeScriptInstance& runtimeScript : runtimeScripts_) {
        auto objectIt = std::find_if(objects_.begin(), objects_.end(), [&](const SceneObject& object) {
            return object.id == runtimeScript.objectId;
        });
        if (objectIt == objects_.end()) {
            continue;
        }
        const int objectIndex = static_cast<int>(std::distance(objects_.begin(), objectIt));
        if (!IsObjectEffectivelyEnabled(objectIndex) || !objectIt->hasScriptComponent || !objectIt->scriptComponent.enabled) {
            continue;
        }
        if (runtimeScript.attachmentIndex >= objectIt->scriptComponent.attachments.size()) {
            continue;
        }

        ObjectScriptAttachment& attachment = objectIt->scriptComponent.attachments[runtimeScript.attachmentIndex];
        if (!attachment.enabled || !runtimeScript.instance) {
            continue;
        }

        InputManager* scriptInput = ShouldRouteInputToGame() ? inputManager_ : nullptr;
        ObjectScriptContext context(*objectIt, console_, scriptInput, physicsWorld_.get());
        if (!runtimeScript.started) {
            runtimeScript.instance->OnStart(context);
            runtimeScript.started = true;
        }
        runtimeScript.instance->OnUpdate(context, deltaTime);
    }
}

void SceneEditor::UpdatePhysics(float deltaTime) {
    if (!scriptsRunning_ || scriptsPaused_ || deltaTime <= 0.0f) {
        return;
    }

    if (!physicsWorld_) {
        return;
    }

    for (const SceneObject& object : objects_) {
        if (object.hasRigidbody && object.rigidbody.enabled && object.rigidbody.bodyType == RigidbodyBodyType::Dynamic) {
            physicsWorld_->SetBodyVelocity(object.id, object.rigidbody.velocity);
        }
    }

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        const SceneObject& object = objects_[objectIndex];
        if (!object.hasCharacterController || !object.characterController.enabled || !physicsWorld_->HasCharacter(object.id)) {
            continue;
        }

        const Transform worldTransform = TransformFromMatrix(GetObjectWorldMatrix(objectIndex));
        physicsWorld_->SetCharacterTransform(object.id, worldTransform.position, worldTransform.rotationEuler);
        physicsWorld_->SetCharacterDesiredVelocity(object.id, object.characterController.moveInput);
        if (object.characterController.pendingJumpImpulse > 0.0f) {
            physicsWorld_->AddCharacterJumpImpulse(object.id, object.characterController.pendingJumpImpulse);
        }
    }

    physicsWorld_->Step(deltaTime);

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        SceneObject& object = objects_[objectIndex];
        if (!object.hasRigidbody || object.rigidbody.bodyType != RigidbodyBodyType::Dynamic) {
            continue;
        }

        PhysicsBodyState state;
        if (!physicsWorld_->GetBodyState(object.id, state)) {
            continue;
        }

        object.rigidbody.velocity = state.velocity;
        const Transform previousLocal = object.transform;
        Transform worldTransform;
        worldTransform.position = state.position;
        worldTransform.rotationEuler = state.rotationEuler;
        worldTransform.scale = glm::vec3(1.0f);

        glm::mat4 worldMatrix = BuildTransformMatrix(worldTransform);
        worldMatrix = glm::scale(worldMatrix, previousLocal.scale);
        const int parentIndex = FindObjectIndexById(object.parentId);
        if (parentIndex >= 0 && parentIndex != objectIndex) {
            object.transform = TransformFromMatrix(glm::inverse(GetObjectWorldMatrix(parentIndex)) * worldMatrix);
        } else {
            object.transform = TransformFromMatrix(worldMatrix);
        }
        object.transform.scale = previousLocal.scale;
    }

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        SceneObject& object = objects_[objectIndex];
        if (!object.hasCharacterController || !physicsWorld_->HasCharacter(object.id)) {
            continue;
        }

        PhysicsCharacterState state;
        if (!physicsWorld_->GetCharacterState(object.id, state)) {
            continue;
        }

        object.characterController.velocity = state.velocity;
        object.characterController.groundVelocity = state.groundVelocity;
        object.characterController.grounded = state.grounded;
        object.characterController.pendingJumpImpulse = 0.0f;

        const Transform previousLocal = object.transform;
        Transform worldTransform;
        worldTransform.position = state.position;
        worldTransform.rotationEuler = state.rotationEuler;
        worldTransform.scale = glm::vec3(1.0f);

        glm::mat4 worldMatrix = BuildTransformMatrix(worldTransform);
        worldMatrix = glm::scale(worldMatrix, previousLocal.scale);
        const int parentIndex = FindObjectIndexById(object.parentId);
        if (parentIndex >= 0 && parentIndex != objectIndex) {
            object.transform = TransformFromMatrix(glm::inverse(GetObjectWorldMatrix(parentIndex)) * worldMatrix);
        } else {
            object.transform = TransformFromMatrix(worldMatrix);
        }
        object.transform.scale = previousLocal.scale;
    }
}

void SceneEditor::ResetPhysicsVelocities() {
    for (SceneObject& object : objects_) {
        if (object.hasRigidbody) {
            object.rigidbody.velocity = {0.0f, 0.0f, 0.0f};
        }
        if (object.hasCharacterController) {
            object.characterController.velocity = {0.0f, 0.0f, 0.0f};
            object.characterController.groundVelocity = {0.0f, 0.0f, 0.0f};
            object.characterController.moveInput = {0.0f, 0.0f, 0.0f};
            object.characterController.pendingJumpImpulse = 0.0f;
            object.characterController.grounded = false;
        }
    }
}

void SceneEditor::SetScriptsRunning(bool running) {
    if (scriptsRunning_ == running) {
        return;
    }

    if (running) {
        SaveCurrentScene();
        playModeSnapshot_ = {objects_, selectedIndex_, selectedIndices_};
        hasPlayModeSnapshot_ = true;
        activeViewport_ = SceneEditorActiveViewport::Game;
        scriptsRunning_ = true;
        scriptsPaused_ = false;
        std::vector<PhysicsBodyDesc> physicsBodies;
        std::vector<PhysicsCharacterDesc> physicsCharacters;
        for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
            const SceneObject& object = objects_[objectIndex];
            if (!IsObjectEffectivelyEnabled(objectIndex)) {
                continue;
            }

            const Transform worldTransform = TransformFromMatrix(GetObjectWorldMatrix(objectIndex));
            if (object.hasCharacterController && object.characterController.enabled) {
                PhysicsCharacterDesc character;
                character.objectId = object.id;
                character.position = worldTransform.position;
                character.rotationEuler = worldTransform.rotationEuler;
                character.height = object.characterController.height;
                character.radius = object.characterController.radius;
                character.stepHeight = object.characterController.stepHeight;
                character.slopeLimitDegrees = object.characterController.slopeLimitDegrees;
                character.maxStrength = object.characterController.maxStrength;
                character.mass = object.characterController.mass;
                physicsCharacters.push_back(std::move(character));
                continue;
            }

            PhysicsBodyDesc body;
            body.objectId = object.id;
            body.position = worldTransform.position;
            body.rotationEuler = worldTransform.rotationEuler;
            body.scale = worldTransform.scale;
            body.bodyType = object.hasRigidbody && object.rigidbody.enabled && object.rigidbody.bodyType == RigidbodyBodyType::Dynamic
                ? PhysicsBodyType::Dynamic
                : PhysicsBodyType::Static;
            body.mass = object.hasRigidbody ? object.rigidbody.mass : 1.0f;
            body.useGravity = object.hasRigidbody ? object.rigidbody.useGravity : false;
            body.velocity = object.hasRigidbody ? object.rigidbody.velocity : glm::vec3{0.0f};

            if (object.hasBoxCollider && object.boxCollider.enabled) {
                PhysicsColliderDesc collider;
                collider.type = PhysicsColliderType::Box;
                collider.isTrigger = object.boxCollider.isTrigger;
                collider.center = object.boxCollider.center;
                collider.size = object.boxCollider.size;
                body.colliders.push_back(collider);
            }
            if (object.hasSphereCollider && object.sphereCollider.enabled) {
                PhysicsColliderDesc collider;
                collider.type = PhysicsColliderType::Sphere;
                collider.isTrigger = object.sphereCollider.isTrigger;
                collider.center = object.sphereCollider.center;
                collider.radius = object.sphereCollider.radius;
                body.colliders.push_back(collider);
            }
            if (object.hasCapsuleCollider && object.capsuleCollider.enabled) {
                PhysicsColliderDesc collider;
                collider.type = PhysicsColliderType::Capsule;
                collider.isTrigger = object.capsuleCollider.isTrigger;
                collider.center = object.capsuleCollider.center;
                collider.radius = object.capsuleCollider.radius;
                collider.height = object.capsuleCollider.height;
                body.colliders.push_back(collider);
            }
            if (object.hasPlaneCollider && object.planeCollider.enabled) {
                PhysicsColliderDesc collider;
                collider.type = PhysicsColliderType::Plane;
                collider.isTrigger = object.planeCollider.isTrigger;
                collider.normal = object.planeCollider.normal;
                collider.offset = object.planeCollider.offset;
                collider.infinite = object.planeCollider.infinite;
                collider.halfExtent = object.planeCollider.halfExtent;
                body.colliders.push_back(collider);
            }
            if (!body.colliders.empty()) {
                physicsBodies.push_back(std::move(body));
            }
        }
        physicsWorld_ = std::make_unique<PhysicsWorld>();
        physicsWorld_->Build(physicsBodies, physicsCharacters);
        RebuildScriptRuntime();
        if (console_) {
            console_->AddLog("Play mode started.");
        }
    } else {
        scriptsRunning_ = false;
        scriptsPaused_ = false;
        ClearScriptRuntime();
        if (physicsWorld_) {
            physicsWorld_->Clear();
            physicsWorld_.reset();
        }
        if (hasPlayModeSnapshot_) {
            objects_ = playModeSnapshot_.objects;
            selectedIndex_ = playModeSnapshot_.selectedIndex;
            selectedIndices_ = playModeSnapshot_.selectedIndices;
            NormalizeSelection();
            playModeSnapshot_ = {};
            hasPlayModeSnapshot_ = false;
        } else {
            ResetPhysicsVelocities();
        }
        activeViewport_ = SceneEditorActiveViewport::Scene;
        activeGizmoAxis_ = -1;
        hoveredGizmoAxis_ = -1;
        if (console_) {
            console_->AddLog("Play mode stopped.");
        }
    }
}

void SceneEditor::SetScriptsPaused(bool paused) {
    if (!scriptsRunning_ || scriptsPaused_ == paused) {
        return;
    }

    scriptsPaused_ = paused;
    if (!scriptsPaused_) {
        activeViewport_ = SceneEditorActiveViewport::Game;
    }
    if (console_) {
        console_->AddLog(scriptsPaused_ ? "Play mode paused." : "Play mode resumed.");
    }
}

void SceneEditor::ClearScriptRuntime() {
    runtimeScripts_.clear();
}

void SceneEditor::RebuildScriptRuntime() {
    ClearScriptRuntime();

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        SceneObject& object = objects_[objectIndex];
        if (!IsObjectEffectivelyEnabled(objectIndex) || !object.hasScriptComponent || !object.scriptComponent.enabled) {
            continue;
        }
        for (std::size_t i = 0; i < object.scriptComponent.attachments.size(); ++i) {
            const ObjectScriptAttachment& attachment = object.scriptComponent.attachments[i];
            if (!attachment.enabled || attachment.scriptName.empty()) {
                continue;
            }

            std::unique_ptr<IObjectScript> instance = CreateRegisteredScript(attachment.scriptName);
            if (!instance) {
                if (console_) {
                    console_->AddWarning("Script not registered, rebuild may be required: " + attachment.scriptName);
                }
                continue;
            }

            RuntimeScriptInstance runtimeScript;
            runtimeScript.objectId = object.id;
            runtimeScript.attachmentIndex = i;
            runtimeScript.instance = std::move(instance);
            runtimeScripts_.push_back(std::move(runtimeScript));
        }
    }
}

void SceneEditor::HandleConsoleCommand(const std::string& command) {
    const std::string trimmed = TrimCopyLocal(command);
    if (trimmed.empty()) {
        return;
    }

    if (trimmed == "help" || trimmed == "script.help") {
        if (console_) {
            console_->AddLog("Commands: script.help, script.list, script.run, script.pause, script.stop");
        }
        return;
    }
    if (trimmed == "script.run") {
        if (scriptsRunning_) {
            SetScriptsPaused(false);
        } else {
            SetScriptsRunning(true);
        }
        return;
    }
    if (trimmed == "script.pause") {
        SetScriptsPaused(true);
        return;
    }
    if (trimmed == "script.stop") {
        SetScriptsRunning(false);
        return;
    }
    if (trimmed == "script.list") {
        if (!console_) {
            return;
        }
        const auto& scripts = GetRegisteredScripts();
        if (scripts.empty()) {
            console_->AddLog("No registered scripts. Create a script, rebuild, then attach it.");
            return;
        }
        for (const ScriptDescriptor& script : scripts) {
            console_->AddLog(script.name + " (" + script.path + ")");
        }
        return;
    }

    if (console_) {
        console_->AddWarning("Unknown command: " + trimmed);
    }
}


void SceneEditor::AddPlane() {
    try {
        PushUndoState();
        SceneObject object;
        object.id = MakeId("mesh");
        object.name = "Plane";
        object.type = "Mesh";
        object.transform.scale = {10.0f, 1.0f, 10.0f};
        object.meshRenderer.materialId = "pbr_default";
        if (!LoadBuiltInPlaneMesh(object)) {
            throw std::runtime_error("plane mesh");
        }
        AddDefaultPlaneColliderToPlane(object);
        objects_.push_back(std::move(object));
        Select(static_cast<int>(objects_.size()) - 1);
        if (onDirty_) onDirty_();
        if (console_) {
            console_->AddLog(std::string("Added Plane: ") + objects_.back().id + " (" + objects_.back().name + ")");
        }
    } catch (...) {
        if (console_) {
            console_->AddWarning("Failed to add Plane object.");
        }
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
    cameraObject.type = "Camera";
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
    lightObject.type = "Light";
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

void SceneEditor::SyncScriptProjectFiles() {
    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path engineRoot = EngineRootPath();
    const fs::path projectPath = engineRoot / "Project Raceman.vcxproj";
    const fs::path filtersPath = engineRoot / "Project Raceman.vcxproj.filters";

    const std::vector<ScriptSourceInfo> scripts = FindCompleteScripts(assetsRoot);

    RemoveProjectEntriesUnderScripts(projectPath);
    RemoveProjectEntriesUnderScripts(filtersPath);
    for (const ScriptSourceInfo& script : scripts) {
        std::string headerProjectPath = "Project\\" + script.projectHeaderPath;
        std::string sourceProjectPath = "Project\\" + script.projectSourcePath;
        std::replace(headerProjectPath.begin(), headerProjectPath.end(), '/', '\\');
        std::replace(sourceProjectPath.begin(), sourceProjectPath.end(), '/', '\\');
        AddProjectIncludeEntry(projectPath, headerProjectPath);
        AddProjectCompileEntry(projectPath, sourceProjectPath);
        AddFilterIncludeEntry(filtersPath, headerProjectPath);
        AddFilterCompileEntry(filtersPath, sourceProjectPath);
    }

    WriteTextFile(engineRoot / "src" / "scripting" / "ScriptRegistry.cpp", BuildScriptRegistrySource(scripts));
    if (console_) {
        console_->AddLog("Synced script project files with " + std::to_string(scripts.size()) + " script(s).");
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
                console_->AddLog("Attached pending script " + className + " to " + objects_[selectedIndex_].name + " and saved the scene. Rebuild/restart before running it.");
            } else {
                console_->AddLog("Rebuild the app before attaching/running " + className + ".");
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

glm::vec3 SceneEditor::GetObjectWorldPosition(int index) const {
    return glm::vec3(GetObjectWorldMatrix(index) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

void SceneEditor::RequestFocusSelectedObject() {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size()) || !onFocusObject_) {
        return;
    }

    const SceneObject& object = objects_[selectedIndex_];
    const glm::mat4 world = GetObjectWorldMatrix(selectedIndex_);
    const glm::vec3 center = GetObjectWorldPosition(selectedIndex_);
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


void SceneEditor::Save(const std::string& path) {
    const fs::path targetPath = ResolveEditorPath(path);
    try {
        fs::create_directories(targetPath.parent_path());
    } catch (...) {}

    std::ofstream out(targetPath, std::ios::trunc);
    if (!out.good()) return;

    // Minimal JSON (manual)
    out << "{\n  \"objects\": [\n";
    for (size_t i = 0; i < objects_.size(); ++i) {
        const auto& o = objects_[i];
        const std::string meshType = o.meshFilter.meshType.empty() ? o.type : o.meshFilter.meshType;
        out << "    {\n";
        out << "      \"id\": \"" << JsonEscape(o.id) << "\",\n";
        out << "      \"parentId\": \"" << JsonEscape(o.parentId) << "\",\n";
        out << "      \"name\": \"" << JsonEscape(o.name) << "\",\n";
        out << "      \"type\": \"" << JsonEscape(o.type) << "\",\n";
        out << "      \"enabled\": " << (o.enabled ? "true" : "false") << ",\n";
        out << "      \"components\": [\n";
        out << "        {\n";
        out << "          \"type\": \"Transform\",\n";
        out << "          \"position\": [" << o.transform.position.x << ", " << o.transform.position.y << ", " << o.transform.position.z << "],\n";
        out << "          \"rotationEuler\": [" << o.transform.rotationEuler.x << ", " << o.transform.rotationEuler.y << ", " << o.transform.rotationEuler.z << "],\n";
        out << "          \"scale\": [" << o.transform.scale.x << ", " << o.transform.scale.y << ", " << o.transform.scale.z << "]\n";
        out << "        }";
        if (o.hasMeshFilter) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"MeshFilter\",\n";
            out << "          \"enabled\": " << (o.meshFilter.enabled ? "true" : "false") << ",\n";
            out << "          \"meshType\": \"" << JsonEscape(meshType) << "\",\n";
            out << "          \"sourcePath\": \"" << JsonEscape(NormalizeSlashes(o.meshFilter.sourcePath)) << "\",\n";
            out << "          \"meshIndex\": " << o.meshFilter.meshIndex << ",\n";
            out << "          \"importedMaterialName\": \"" << JsonEscape(o.meshFilter.importedMaterialName) << "\",\n";
            out << "          \"diffuseTexturePath\": \"" << JsonEscape(NormalizeSlashes(o.meshFilter.diffuseTexturePath)) << "\"\n";
            out << "        }";
        }
        if (o.hasMeshRenderer) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"MeshRenderer\",\n";
            out << "          \"enabled\": " << (o.meshRenderer.enabled ? "true" : "false") << ",\n";
            out << "          \"materialId\": \"" << JsonEscape(o.meshRenderer.materialId.empty() ? "pbr_default" : o.meshRenderer.materialId) << "\",\n";
            out << "          \"color\": [" << o.meshRenderer.color.r << ", " << o.meshRenderer.color.g << ", " << o.meshRenderer.color.b << ", " << o.meshRenderer.color.a << "]\n";
            out << "        }";
        }
        if (o.hasScriptComponent) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Script\",\n";
            out << "          \"enabled\": " << (o.scriptComponent.enabled ? "true" : "false") << ",\n";
            out << "          \"attachments\": [\n";
            for (size_t scriptIndex = 0; scriptIndex < o.scriptComponent.attachments.size(); ++scriptIndex) {
                const ObjectScriptAttachment& script = o.scriptComponent.attachments[scriptIndex];
                out << "            {\n";
                out << "              \"enabled\": " << (script.enabled ? "true" : "false") << ",\n";
                out << "              \"scriptName\": \"" << JsonEscape(script.scriptName) << "\",\n";
                out << "              \"scriptPath\": \"" << JsonEscape(NormalizeSlashes(script.scriptPath)) << "\"\n";
                out << "            }" << (scriptIndex + 1 < o.scriptComponent.attachments.size() ? ",\n" : "\n");
            }
            out << "          ]\n";
            out << "        }";
        }
        if (o.hasRigidbody) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Rigidbody\",\n";
            out << "          \"enabled\": " << (o.rigidbody.enabled ? "true" : "false") << ",\n";
            out << "          \"bodyType\": \"" << RigidbodyBodyTypeToString(o.rigidbody.bodyType) << "\",\n";
            out << "          \"mass\": " << o.rigidbody.mass << ",\n";
            out << "          \"useGravity\": " << (o.rigidbody.useGravity ? "true" : "false") << ",\n";
            out << "          \"velocity\": [" << o.rigidbody.velocity.x << ", " << o.rigidbody.velocity.y << ", " << o.rigidbody.velocity.z << "]\n";
            out << "        }";
        }
        if (o.hasCharacterController) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"CharacterController\",\n";
            out << "          \"enabled\": " << (o.characterController.enabled ? "true" : "false") << ",\n";
            out << "          \"height\": " << o.characterController.height << ",\n";
            out << "          \"radius\": " << o.characterController.radius << ",\n";
            out << "          \"stepHeight\": " << o.characterController.stepHeight << ",\n";
            out << "          \"slopeLimitDegrees\": " << o.characterController.slopeLimitDegrees << ",\n";
            out << "          \"maxStrength\": " << o.characterController.maxStrength << ",\n";
            out << "          \"mass\": " << o.characterController.mass << "\n";
            out << "        }";
        }
        if (o.hasBoxCollider) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"BoxCollider\",\n";
            out << "          \"enabled\": " << (o.boxCollider.enabled ? "true" : "false") << ",\n";
            out << "          \"isTrigger\": " << (o.boxCollider.isTrigger ? "true" : "false") << ",\n";
            out << "          \"center\": [" << o.boxCollider.center.x << ", " << o.boxCollider.center.y << ", " << o.boxCollider.center.z << "],\n";
            out << "          \"size\": [" << o.boxCollider.size.x << ", " << o.boxCollider.size.y << ", " << o.boxCollider.size.z << "]\n";
            out << "        }";
        }
        if (o.hasSphereCollider) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"SphereCollider\",\n";
            out << "          \"enabled\": " << (o.sphereCollider.enabled ? "true" : "false") << ",\n";
            out << "          \"isTrigger\": " << (o.sphereCollider.isTrigger ? "true" : "false") << ",\n";
            out << "          \"center\": [" << o.sphereCollider.center.x << ", " << o.sphereCollider.center.y << ", " << o.sphereCollider.center.z << "],\n";
            out << "          \"radius\": " << o.sphereCollider.radius << "\n";
            out << "        }";
        }
        if (o.hasCapsuleCollider) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"CapsuleCollider\",\n";
            out << "          \"enabled\": " << (o.capsuleCollider.enabled ? "true" : "false") << ",\n";
            out << "          \"isTrigger\": " << (o.capsuleCollider.isTrigger ? "true" : "false") << ",\n";
            out << "          \"center\": [" << o.capsuleCollider.center.x << ", " << o.capsuleCollider.center.y << ", " << o.capsuleCollider.center.z << "],\n";
            out << "          \"radius\": " << o.capsuleCollider.radius << ",\n";
            out << "          \"height\": " << o.capsuleCollider.height << "\n";
            out << "        }";
        }
        if (o.hasPlaneCollider) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"PlaneCollider\",\n";
            out << "          \"enabled\": " << (o.planeCollider.enabled ? "true" : "false") << ",\n";
            out << "          \"isTrigger\": " << (o.planeCollider.isTrigger ? "true" : "false") << ",\n";
            out << "          \"normal\": [" << o.planeCollider.normal.x << ", " << o.planeCollider.normal.y << ", " << o.planeCollider.normal.z << "],\n";
            out << "          \"offset\": " << o.planeCollider.offset << ",\n";
            out << "          \"infinite\": " << (o.planeCollider.infinite ? "true" : "false") << ",\n";
            out << "          \"halfExtent\": " << o.planeCollider.halfExtent << "\n";
            out << "        }";
        }
        if (o.hasCamera) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Camera\",\n";
            out << "          \"enabled\": " << (o.camera.enabled ? "true" : "false") << ",\n";
            out << "          \"isMain\": " << (o.camera.isMain ? "true" : "false") << ",\n";
            out << "          \"fieldOfViewDegrees\": " << o.camera.fieldOfViewDegrees << ",\n";
            out << "          \"nearClip\": " << o.camera.nearClip << ",\n";
            out << "          \"farClip\": " << o.camera.farClip << ",\n";
            out << "          \"clearColor\": [" << o.camera.clearColor.r << ", " << o.camera.clearColor.g << ", " << o.camera.clearColor.b << ", " << o.camera.clearColor.a << "]\n";
            out << "        }";
        }
        if (o.hasLight) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Light\",\n";
            out << "          \"enabled\": " << (o.light.enabled ? "true" : "false") << ",\n";
            out << "          \"lightType\": \"" << LightTypeToString(o.light.type) << "\",\n";
            out << "          \"color\": [" << o.light.color.r << ", " << o.light.color.g << ", " << o.light.color.b << "],\n";
            out << "          \"intensity\": " << o.light.intensity << ",\n";
            out << "          \"range\": " << o.light.range << ",\n";
            out << "          \"spotAngleDegrees\": " << o.light.spotAngleDegrees << "\n";
            out << "        }";
        }
        out << "\n";
        out << "      ]\n";
        out << "    }" << (i + 1 < objects_.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
}

void SceneEditor::Load(const std::string& path) {
    using namespace raceman::physics::json;
    objects_.clear();
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

            // type
            auto typeIt = o.find("type");
            if (typeIt != o.end() && typeIt->second.is_string()) {
                so.type = typeIt->second.as_string();
            }
            else {
                so.type = "Unknown";
            }

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
                    if (!script.scriptName.empty()) {
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
                so.hasCharacterController = false;
                so.hasBoxCollider = false;
                so.hasSphereCollider = false;
                so.hasCapsuleCollider = false;
                so.hasPlaneCollider = false;
                so.hasCamera = false;
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
                                if (!script.scriptName.empty()) {
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
                        ReadVec3(component, "velocity", so.rigidbody.velocity);
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
                    } else if (componentType == "BoxCollider") {
                        so.hasBoxCollider = true;
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
                        so.hasSphereCollider = true;
                        ReadBool(component, "enabled", so.sphereCollider.enabled);
                        ReadBool(component, "isTrigger", so.sphereCollider.isTrigger);
                        ReadVec3(component, "center", so.sphereCollider.center);

                        auto radiusIt = component.find("radius");
                        if (radiusIt != component.end() && radiusIt->second.is_number()) {
                            so.sphereCollider.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                        }
                    } else if (componentType == "CapsuleCollider") {
                        so.hasCapsuleCollider = true;
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
                        so.hasPlaneCollider = true;
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
                so.meshFilter.meshType = so.type;
            } else if (so.type == "Unknown") {
                so.type = so.meshFilter.meshType;
            }

            if (so.hasCharacterController && so.hasRigidbody) {
                so.hasRigidbody = false;
                so.rigidbody = RigidbodyComponent{};
            }

            // attach render info for known types
            const std::string meshType = so.meshFilter.meshType.empty() ? so.type : so.meshFilter.meshType;
            if (so.hasMeshFilter && meshType == "Plane") {
                if (!planePrim_) {
                    planePrim_ = std::make_unique<PrimitivePlane>();
                }
                so.type = "Plane";
                so.meshFilter.meshType = "Plane";
                so.meshFilter.vao = planePrim_->vao();
                so.meshFilter.indexCount = planePrim_->indexCount();
                if (so.meshRenderer.materialId.empty()) {
                    so.meshRenderer.materialId = "pbr_default";
                }
            }
            else if (so.hasMeshFilter && meshType == "Mesh" && !so.meshFilter.sourcePath.empty()) {
                try {
                    if (ShouldFallbackToBuiltInPlane(so)) {
                        LoadBuiltInPlaneMesh(so);
                    } else {
                        auto model = raceman::LoadModelFromFile(ProjectAssetPathToAbsolute(so.meshFilter.sourcePath).string());
                        const auto infos = raceman::GetMeshInfos(model);
                        if (so.meshFilter.meshIndex >= 0 && so.meshFilter.meshIndex < static_cast<int>(infos.size())) {
                            so.type = "Mesh";
                            so.meshFilter.meshType = "Mesh";
                            ApplyMeshInfoToSceneObject(so, infos[static_cast<std::size_t>(so.meshFilter.meshIndex)], model);
                        }
                    }
                } catch (...) {
                    if (ShouldFallbackToBuiltInPlane(so) && LoadBuiltInPlaneMesh(so)) {
                        AddDefaultPlaneColliderToPlane(so);
                    } else if (console_) {
                        console_->AddLog("Failed to reload mesh source: " + so.meshFilter.sourcePath);
                    }
                }
            }

            objects_.push_back(std::move(so));
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

                auto editorIt = object.find("editorState");
                if (editorIt != object.end() && editorIt->second.is_object()) {
                    const auto& editorState = editorIt->second.as_object();
                    ReadString(editorState, "selectedProjectDirectory", selectedProjectDirectory_);
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
    if (shouldSaveProject) {
        SaveProject();
    }
}

void SceneEditor::SaveProject() {
    try {
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

void SceneEditor::NewScene() {
    NewScene("Untitled");
}

void SceneEditor::CreateDefaultSceneObjects() {
    try {
        SceneObject planeObject;
        planeObject.id = MakeId("mesh");
        planeObject.name = "Plane";
        planeObject.type = "Mesh";
        planeObject.transform.scale = {10.0f, 1.0f, 10.0f};
        planeObject.meshRenderer.materialId = "pbr_default";
        if (LoadBuiltInPlaneMesh(planeObject)) {
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
    cameraObject.type = "Camera";
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
    lightObject.type = "Light";
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

    try {
        objects_.clear();
        selectedIndex_ = -1;
        selectedIndices_.clear();
        inspectMaterial_ = false;
        inspectedMaterialId_.clear();
        CreateDefaultSceneObjects();
        Save(scenePath);

        objects_ = previousObjects;
        selectedIndex_ = previousSelectedIndex;
        selectedIndices_ = previousSelectedIndices;
        inspectMaterial_ = previousInspectMaterial;
        inspectedMaterialId_ = previousInspectedMaterialId;
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
        NormalizeSelection();
        if (console_) {
            console_->AddError("Failed to create scene asset.");
        }
        return false;
    }
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
    return base + "_" + std::to_string(++counter);
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

    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const auto& o = objects_[i];
        if (!IsObjectEffectivelyEnabled(i)) continue;
        if (!o.hasMeshFilter || !o.hasMeshRenderer) continue;
        if (!o.meshFilter.enabled || !o.meshRenderer.enabled) continue;
        if (o.meshFilter.vao == 0 || o.meshFilter.indexCount == 0) continue;

        MeshDrawCommand cmd;
        cmd.vao = o.meshFilter.vao;
        cmd.indexCount = o.meshFilter.indexCount;
        cmd.modelMatrix = GetObjectWorldMatrix(i);
        cmd.materialId = o.meshRenderer.materialId.empty() ? std::string("pbr_default") : o.meshRenderer.materialId;
        if (const Material* material = materialManager_.Get(cmd.materialId)) {
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
            cmd.unlit = ToLowerCopy(material->shader) == "unlit";
        } else {
            cmd.color = o.meshRenderer.color;
        }
        cmd.diffuseTextureId = o.meshFilter.diffuseTextureId;
        cmd.useDiffuseTexture = (cmd.diffuseTextureId != 0);

        renderer.SubmitMesh(cmd);
    }

    if (editorInteraction) {
        SubmitGizmo(renderer);
    }
}

void SceneEditor::SetSavePath(const std::string& path) {
    savePath_ = NormalizeSlashes(path);
}

} // namespace raceman


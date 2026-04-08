#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"
#include "../scripting/ScriptRegistry.h"

#include <cmath>
#include <iostream>

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

        const bool scriptEntry = ContainsText(line, "Include=\"assets\\scripts\\");
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

std::vector<std::string> FindCompleteScriptNames(const fs::path& scriptsDir) {
    std::vector<std::string> scriptNames;
    if (!fs::exists(scriptsDir)) {
        return scriptNames;
    }

    for (const auto& entry : fs::directory_iterator(scriptsDir)) {
        if (!entry.is_regular_file() || ToLowerCopy(entry.path().extension().string()) != ".h") {
            continue;
        }

        const std::string scriptName = entry.path().stem().string();
        if (fs::exists(scriptsDir / (scriptName + ".cpp"))) {
            scriptNames.push_back(scriptName);
        }
    }

    std::sort(scriptNames.begin(), scriptNames.end());
    return scriptNames;
}

std::string BuildScriptRegistrySource(const std::vector<std::string>& scriptNames) {
    std::string registry;
    registry += "#include \"ScriptRegistry.h\"\n\n";
    for (const std::string& scriptName : scriptNames) {
        registry += "#include \"../../assets/scripts/" + scriptName + ".h\"\n";
    }
    registry += "\nnamespace raceman {\nnamespace {\n\n";
    for (const std::string& scriptName : scriptNames) {
        registry += "std::unique_ptr<IObjectScript> Create" + scriptName + "() {\n";
        registry += "    return std::make_unique<scripts::" + scriptName + ">();\n";
        registry += "}\n\n";
    }
    registry += "} // namespace\n\n";
    registry += "const std::vector<ScriptDescriptor>& GetRegisteredScripts() {\n";
    registry += "    static const std::vector<ScriptDescriptor> scripts = {\n";
    for (const std::string& scriptName : scriptNames) {
        registry += "        {\"" + scriptName + "\", \"assets/scripts/" + scriptName + ".cpp\", &Create" + scriptName + "},\n";
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
    // Optionally load previous scene
    Load(savePath_);
}

SceneEditor::~SceneEditor() = default;

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
    for (const SceneObject& object : objects_) {
        if (!object.enabled || !object.hasCamera || !object.camera.enabled) {
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
    const float safeAspect = aspect > 0.0001f ? aspect : 1.0f;
    const float fov = (std::max)(1.0f, (std::min)(camera.fieldOfViewDegrees, 179.0f));
    const float nearClip = (std::max)(0.001f, camera.nearClip);
    const float farClip = (std::max)(nearClip + 0.001f, camera.farClip);
    const glm::vec3 position = fallbackCamera->transform.position;
    const glm::vec3 forward = CameraForwardFromEuler(fallbackCamera->transform.rotationEuler);
    const glm::vec3 up = CameraUpFromEuler(fallbackCamera->transform.rotationEuler);

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

void SceneEditor::RenderUI(float deltaTime) {
    HandleEditorShortcuts();
    UpdateScripts(deltaTime);
    UpdatePhysics(deltaTime);

    RenderScenePanel();
    RenderInspectorPanel();
    RenderProjectPanel();
}

void SceneEditor::HandleEditorShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }

    if (IsCtrlSPressed()) {
        Save(savePath_);
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
        if (!objectIt->hasScriptComponent || !objectIt->scriptComponent.enabled) {
            continue;
        }
        if (runtimeScript.attachmentIndex >= objectIt->scriptComponent.attachments.size()) {
            continue;
        }

        ObjectScriptAttachment& attachment = objectIt->scriptComponent.attachments[runtimeScript.attachmentIndex];
        if (!attachment.enabled || !runtimeScript.instance) {
            continue;
        }

        ObjectScriptContext context(*objectIt, console_, inputManager_);
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

    const glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    const float step = (std::min)(deltaTime, 0.05f);

    for (SceneObject& object : objects_) {
        if (!object.enabled || !object.hasRigidbody) {
            continue;
        }
        if (!object.rigidbody.enabled) {
            continue;
        }
        if (object.rigidbody.bodyType != RigidbodyBodyType::Dynamic) {
            continue;
        }

        if (object.rigidbody.useGravity) {
            object.rigidbody.velocity += gravity * step;
        }
        object.transform.position += object.rigidbody.velocity * step;

        std::vector<ColliderWorldAabb> dynamicColliders = BuildSolidColliderAabbs(object);
        if (dynamicColliders.empty()) {
            continue;
        }

        for (const SceneObject& staticObject : objects_) {
            if (&staticObject == &object) {
                continue;
            }
            if (!staticObject.enabled) {
                continue;
            }
            const bool isStaticBody = !staticObject.hasRigidbody
                || !staticObject.rigidbody.enabled
                || staticObject.rigidbody.bodyType == RigidbodyBodyType::Static;
            if (!isStaticBody) {
                continue;
            }

            const std::vector<ColliderWorldAabb> staticColliders = BuildSolidColliderAabbs(staticObject);
            if (staticColliders.empty()) {
                continue;
            }

            for (const ColliderWorldAabb& staticBox : staticColliders) {
                for (const ColliderWorldAabb& dynamicBox : dynamicColliders) {
                    if (!AabbOverlap(dynamicBox, staticBox)) {
                        continue;
                    }

                    const glm::vec3 correction = ComputeMinimumTranslation(dynamicBox, staticBox);
                    object.transform.position += correction;
                    if (std::abs(correction.x) > 0.0f) {
                        object.rigidbody.velocity.x = 0.0f;
                    }
                    if (std::abs(correction.y) > 0.0f) {
                        object.rigidbody.velocity.y = 0.0f;
                    }
                    if (std::abs(correction.z) > 0.0f) {
                        object.rigidbody.velocity.z = 0.0f;
                    }
                    dynamicColliders = BuildSolidColliderAabbs(object);
                    break;
                }
            }
        }
    }
}

void SceneEditor::ResetPhysicsVelocities() {
    for (SceneObject& object : objects_) {
        if (object.hasRigidbody) {
            object.rigidbody.velocity = {0.0f, 0.0f, 0.0f};
        }
    }
}

void SceneEditor::SetScriptsRunning(bool running) {
    if (scriptsRunning_ == running) {
        return;
    }

    if (running) {
        playModeSnapshot_ = {objects_, selectedIndex_};
        hasPlayModeSnapshot_ = true;
        scriptsRunning_ = true;
        scriptsPaused_ = false;
        RebuildScriptRuntime();
        if (console_) {
            console_->AddLog("Play mode started.");
        }
    } else {
        scriptsRunning_ = false;
        scriptsPaused_ = false;
        ClearScriptRuntime();
        if (hasPlayModeSnapshot_) {
            objects_ = playModeSnapshot_.objects;
            selectedIndex_ = playModeSnapshot_.selectedIndex;
            if (selectedIndex_ >= static_cast<int>(objects_.size())) {
                selectedIndex_ = objects_.empty() ? -1 : static_cast<int>(objects_.size()) - 1;
            }
            playModeSnapshot_ = {};
            hasPlayModeSnapshot_ = false;
        } else {
            ResetPhysicsVelocities();
        }
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
    if (console_) {
        console_->AddLog(scriptsPaused_ ? "Play mode paused." : "Play mode resumed.");
    }
}

void SceneEditor::ClearScriptRuntime() {
    runtimeScripts_.clear();
}

void SceneEditor::RebuildScriptRuntime() {
    ClearScriptRuntime();

    for (SceneObject& object : objects_) {
        if (!object.hasScriptComponent || !object.scriptComponent.enabled) {
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
    const int previousCount = static_cast<int>(objects_.size());
    ImportObj(kPlaneObjAssetPath);
    if (static_cast<int>(objects_.size()) > previousCount && selectedIndex_ >= previousCount && selectedIndex_ < static_cast<int>(objects_.size())) {
        SceneObject& object = objects_[selectedIndex_];
        object.name = "Plane";
        object.type = "Mesh";
        object.meshFilter.meshType = "Mesh";
        object.transform.scale = {10.0f, 1.0f, 10.0f};
        if (console_) {
            console_->AddLog(std::string("Added Plane: ") + object.id + " (" + object.name + ")");
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
    inspectMaterial_ = false;
    renamingObjectIndex_ = -1;
    if (console_) {
        console_->AddLog("Added Camera object.");
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
    const fs::path scriptsDir = assetsRoot / "scripts";
    const fs::path projectRoot = assetsRoot.parent_path();
    const fs::path projectPath = projectRoot / "Project Raceman.vcxproj";
    const fs::path filtersPath = projectRoot / "Project Raceman.vcxproj.filters";

    const std::vector<std::string> scriptNames = FindCompleteScriptNames(scriptsDir);

    RemoveProjectEntriesUnderScripts(projectPath);
    RemoveProjectEntriesUnderScripts(filtersPath);
    for (const std::string& scriptName : scriptNames) {
        const std::string headerProjectPath = "assets\\scripts\\" + scriptName + ".h";
        const std::string sourceProjectPath = "assets\\scripts\\" + scriptName + ".cpp";
        AddProjectIncludeEntry(projectPath, headerProjectPath);
        AddProjectCompileEntry(projectPath, sourceProjectPath);
        AddFilterIncludeEntry(filtersPath, headerProjectPath);
        AddFilterCompileEntry(filtersPath, sourceProjectPath);
    }

    WriteTextFile(projectRoot / "src" / "scripting" / "ScriptRegistry.cpp", BuildScriptRegistrySource(scriptNames));
    if (console_) {
        console_->AddLog("Synced script project files with " + std::to_string(scriptNames.size()) + " script(s).");
    }
}

bool SceneEditor::CreateScriptAsset(const std::string& requestedName) {
    const std::string className = SanitizeScriptClassName(requestedName);
    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path scriptsDir = assetsRoot / "scripts";
    const fs::path headerPath = scriptsDir / (className + ".h");
    const fs::path sourcePath = scriptsDir / (className + ".cpp");

    if (fs::exists(headerPath) || fs::exists(sourcePath)) {
        if (console_) {
            console_->AddError("Script already exists: " + className);
        }
        return false;
    }

    const std::string header =
        "#pragma once\n\n"
        "#include \"../../src/scripting/ObjectScript.h\"\n\n"
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
        "}\n\n"
        "void " + className + "::OnUpdate(raceman::ObjectScriptContext& context, float deltaTime) {\n"
        "    (void)context;\n"
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

        const std::string scriptPath = "assets/scripts/" + className + ".cpp";
        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size())) {
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
            if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size())) {
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
        inspectMaterial_ = false;
    }
}

void SceneEditor::PushUndoState() {
    undoStack_.push_back({objects_, selectedIndex_});
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

    redoStack_.push_back({objects_, selectedIndex_});
    const HistoryState state = std::move(undoStack_.back());
    undoStack_.pop_back();
    objects_ = state.objects;
    selectedIndex_ = state.selectedIndex;
    if (selectedIndex_ >= static_cast<int>(objects_.size())) {
        selectedIndex_ = objects_.empty() ? -1 : static_cast<int>(objects_.size()) - 1;
    }
    renamingObjectIndex_ = -1;
    inspectMaterial_ = false;
    activeGizmoAxis_ = -1;
    if (onDirty_) onDirty_();
}

void SceneEditor::Redo() {
    if (redoStack_.empty()) {
        return;
    }

    undoStack_.push_back({objects_, selectedIndex_});
    const HistoryState state = std::move(redoStack_.back());
    redoStack_.pop_back();
    objects_ = state.objects;
    selectedIndex_ = state.selectedIndex;
    if (selectedIndex_ >= static_cast<int>(objects_.size())) {
        selectedIndex_ = objects_.empty() ? -1 : static_cast<int>(objects_.size()) - 1;
    }
    renamingObjectIndex_ = -1;
    inspectMaterial_ = false;
    activeGizmoAxis_ = -1;
    if (onDirty_) onDirty_();
}


void SceneEditor::Save(const std::string& path) {
    try {
        fs::create_directories(fs::path(path).parent_path());
    } catch (...) {}

    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) return;

    // Minimal JSON (manual)
    out << "{\n  \"objects\": [\n";
    for (size_t i = 0; i < objects_.size(); ++i) {
        const auto& o = objects_[i];
        const std::string meshType = o.meshFilter.meshType.empty() ? o.type : o.meshFilter.meshType;
        out << "    {\n";
        out << "      \"id\": \"" << JsonEscape(o.id) << "\",\n";
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
        out << "\n";
        out << "      ]\n";
        out << "    }" << (i + 1 < objects_.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
}

void SceneEditor::Load(const std::string& path) {
    using namespace raceman::physics::json;
    objects_.clear();
    undoStack_.clear();
    redoStack_.clear();

    if (!fs::exists(path)) return;

    std::ifstream in(path);
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
                so.hasBoxCollider = false;
                so.hasSphereCollider = false;
                so.hasCapsuleCollider = false;

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
                    }
                }
            }

            if (so.meshFilter.meshType.empty()) {
                so.meshFilter.meshType = so.type;
            } else if (so.type == "Unknown") {
                so.type = so.meshFilter.meshType;
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
                    auto model = raceman::LoadModelFromFile(so.meshFilter.sourcePath);
                    const auto infos = raceman::GetMeshInfos(model);
                    if (so.meshFilter.meshIndex >= 0 && so.meshFilter.meshIndex < static_cast<int>(infos.size())) {
                        so.type = "Mesh";
                        so.meshFilter.meshType = "Mesh";
                        ApplyMeshInfoToSceneObject(so, infos[static_cast<std::size_t>(so.meshFilter.meshIndex)], model);
                    }
                } catch (...) {
                    if (console_) {
                        console_->AddLog("Failed to reload mesh source: " + so.meshFilter.sourcePath);
                    }
                }
            }

            objects_.push_back(std::move(so));
        }

        // Select first object if available
        if (!objects_.empty()) {
            Select(0);
        }
    } catch (const std::exception&) {
        // Silently ignore malformed files
    }
    
}

std::string SceneEditor::MakeId(const std::string& base) {
    static int counter = 0;
    return base + "_" + std::to_string(++counter);
}

void SceneEditor::SubmitDraws(Renderer& renderer, bool editorInteraction) {
    if (editorInteraction) {
        UpdateGizmo(renderer);
    }

    for (const auto& o : objects_) {
        if (!o.enabled) continue;
        if (!o.hasMeshFilter || !o.hasMeshRenderer) continue;
        if (!o.meshFilter.enabled || !o.meshRenderer.enabled) continue;
        if (o.meshFilter.vao == 0 || o.meshFilter.indexCount == 0) continue;

        // Build model matrix from Transform (T * Rz * Ry * Rx * S)
        glm::mat4 M(1.0f);
        M = glm::translate(M, o.transform.position);
        glm::vec3 rads = glm::radians(o.transform.rotationEuler);
        M = glm::rotate(M, rads.z, glm::vec3(0,0,1));
        M = glm::rotate(M, rads.y, glm::vec3(0,1,0));
        M = glm::rotate(M, rads.x, glm::vec3(1,0,0));
        M = glm::scale(M, o.transform.scale);

        MeshDrawCommand cmd;
        cmd.vao = o.meshFilter.vao;
        cmd.indexCount = o.meshFilter.indexCount;
        cmd.modelMatrix = M;
        cmd.materialId = o.meshRenderer.materialId.empty() ? std::string("pbr_default") : o.meshRenderer.materialId;
        if (const Material* material = materialManager_.Get(cmd.materialId)) {
            cmd.color = {
                material->albedoColor[0],
                material->albedoColor[1],
                material->albedoColor[2],
                material->albedoColor[3]
            };
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
    savePath_ = path;
}

} // namespace raceman


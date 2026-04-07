#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"
#include "../scripting/ScriptRegistry.h"

#include <iostream>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

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

void SceneEditor::AddMeshPlane() {
    AddPlane();
}

void SceneEditor::RenderUI(float deltaTime) {
    HandleEditorShortcuts();
    UpdateScripts(deltaTime);

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
    if (!scriptsRunning_ || deltaTime <= 0.0f) {
        return;
    }

    for (RuntimeScriptInstance& runtimeScript : runtimeScripts_) {
        auto objectIt = std::find_if(objects_.begin(), objects_.end(), [&](const SceneObject& object) {
            return object.id == runtimeScript.objectId;
        });
        if (objectIt == objects_.end()) {
            continue;
        }
        if (runtimeScript.attachmentIndex >= objectIt->scriptAttachments.size()) {
            continue;
        }

        ObjectScriptAttachment& attachment = objectIt->scriptAttachments[runtimeScript.attachmentIndex];
        if (!attachment.enabled || !runtimeScript.instance) {
            continue;
        }

        ObjectScriptContext context(*objectIt, console_);
        if (!runtimeScript.started) {
            runtimeScript.instance->OnStart(context);
            runtimeScript.started = true;
        }
        runtimeScript.instance->OnUpdate(context, deltaTime);
    }
}

void SceneEditor::SetScriptsRunning(bool running) {
    if (scriptsRunning_ == running) {
        return;
    }

    scriptsRunning_ = running;
    if (scriptsRunning_) {
        RebuildScriptRuntime();
        if (console_) {
            console_->AddLog("Scripts running.");
        }
    } else {
        ClearScriptRuntime();
        if (console_) {
            console_->AddLog("Scripts paused.");
        }
    }
}

void SceneEditor::ClearScriptRuntime() {
    runtimeScripts_.clear();
}

void SceneEditor::RebuildScriptRuntime() {
    ClearScriptRuntime();

    for (SceneObject& object : objects_) {
        for (std::size_t i = 0; i < object.scriptAttachments.size(); ++i) {
            const ObjectScriptAttachment& attachment = object.scriptAttachments[i];
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
            console_->AddLog("Commands: script.help, script.list, script.run, script.pause");
        }
        return;
    }
    if (trimmed == "script.run") {
        SetScriptsRunning(true);
        return;
    }
    if (trimmed == "script.pause") {
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
        object.transform.scale = {10.0f, 1.0f, 10.0f};
        if (console_) {
            console_->AddLog(std::string("Added Plane: ") + object.id + " (" + object.name + ")");
        }
    }
}

bool SceneEditor::AttachScriptToSelected(const std::string& scriptName, const std::string& scriptPath) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(objects_.size()) || scriptName.empty()) {
        return false;
    }

    PushUndoState();
    SceneObject& obj = objects_[selectedIndex_];
    ObjectScriptAttachment attachment;
    attachment.enabled = true;
    attachment.scriptName = scriptName;
    attachment.scriptPath = NormalizeSlashes(scriptPath);
    obj.scriptAttachments.push_back(std::move(attachment));
    if (scriptsRunning_) {
        RebuildScriptRuntime();
    }
    if (console_) {
        console_->AddLog("Attached script " + scriptName + " to " + obj.name);
    }
    if (onDirty_) onDirty_();
    return true;
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

        fs::path projectRoot = assetsRoot.parent_path();
        const fs::path projectPath = projectRoot / "Project Raceman.vcxproj";
        const fs::path filtersPath = projectRoot / "Project Raceman.vcxproj.filters";
        const std::string headerProjectPath = "assets\\scripts\\" + className + ".h";
        const std::string sourceProjectPath = "assets\\scripts\\" + className + ".cpp";

        AddProjectIncludeEntry(projectPath, headerProjectPath);
        AddProjectCompileEntry(projectPath, sourceProjectPath);
        AddFilterIncludeEntry(filtersPath, headerProjectPath);
        AddFilterCompileEntry(filtersPath, sourceProjectPath);
        std::cout << "[SceneEditor] Added script to project: " << className << '\n';

        std::vector<std::string> scriptNames;
        if (fs::exists(scriptsDir)) {
            for (const auto& entry : fs::directory_iterator(scriptsDir)) {
                if (entry.is_regular_file() && ToLowerCopy(entry.path().extension().string()) == ".h") {
                    scriptNames.push_back(entry.path().stem().string());
                }
            }
        }
        std::sort(scriptNames.begin(), scriptNames.end());

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
        WriteTextFile(projectRoot / "src" / "scripting" / "ScriptRegistry.cpp", registry);
        std::cout << "[SceneEditor] Updated script registry with " << scriptNames.size() << " script(s).\n";

        const std::string scriptPath = "assets/scripts/" + className + ".cpp";
        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size())) {
            SceneObject& obj = objects_[selectedIndex_];
            PushUndoState();
            ObjectScriptAttachment attachment;
            attachment.enabled = true;
            attachment.scriptName = className;
            attachment.scriptPath = scriptPath;
            obj.scriptAttachments.push_back(std::move(attachment));
            if (scriptsRunning_) {
                RebuildScriptRuntime();
            }
            if (onDirty_) onDirty_();
            std::cout << "[SceneEditor] Attached pending script " << className << " to " << obj.name << ".\n";
        }

        if (console_) {
            console_->AddLog("Created C++ script " + className + ": " + scriptPath);
            if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size())) {
                console_->AddLog("Attached pending script " + className + " to " + objects_[selectedIndex_].name + ". Rebuild before running it.");
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
        out << "    {\n";
        out << "      \"id\": \"" << JsonEscape(o.id) << "\",\n";
        out << "      \"name\": \"" << JsonEscape(o.name) << "\",\n";
        out << "      \"type\": \"" << JsonEscape(o.type) << "\",\n";
        out << "      \"transform\": {\n";
        out << "        \"position\": [" << o.transform.position.x << ", " << o.transform.position.y << ", " << o.transform.position.z << "],\n";
        out << "        \"rotationEuler\": [" << o.transform.rotationEuler.x << ", " << o.transform.rotationEuler.y << ", " << o.transform.rotationEuler.z << "],\n";
        out << "        \"scale\": [" << o.transform.scale.x << ", " << o.transform.scale.y << ", " << o.transform.scale.z << "]\n";
        out << "      },\n";
        out << "      \"enabled\": " << (o.enabled ? "true" : "false") << ",\n";
        out << "      \"color\": [" << o.color.r << ", " << o.color.g << ", " << o.color.b << ", " << o.color.a << "],\n";
        out << "      \"materialId\": \"" << JsonEscape(o.materialId.empty() ? "pbr_default" : o.materialId) << "\"";
        if (o.type == "Mesh" && !o.sourcePath.empty()) {
            out << ",\n";
            out << "      \"sourcePath\": \"" << JsonEscape(NormalizeSlashes(o.sourcePath)) << "\",\n";
            out << "      \"meshIndex\": " << o.meshIndex << ",\n";
            out << "      \"importedMaterialName\": \"" << JsonEscape(o.importedMaterialName) << "\",\n";
            out << "      \"diffuseTexturePath\": \"" << JsonEscape(NormalizeSlashes(o.diffuseTexturePath)) << "\"";
        }
        if (!o.scriptAttachments.empty()) {
            out << ",\n";
            out << "      \"scriptAttachments\": [\n";
            for (size_t scriptIndex = 0; scriptIndex < o.scriptAttachments.size(); ++scriptIndex) {
                const ObjectScriptAttachment& script = o.scriptAttachments[scriptIndex];
                out << "        {\n";
                out << "          \"enabled\": " << (script.enabled ? "true" : "false") << ",\n";
                out << "          \"scriptName\": \"" << JsonEscape(script.scriptName) << "\",\n";
                out << "          \"scriptPath\": \"" << JsonEscape(NormalizeSlashes(script.scriptPath)) << "\"\n";
                out << "        }" << (scriptIndex + 1 < o.scriptAttachments.size() ? ",\n" : "\n");
            }
            out << "      ]\n";
        } else {
            out << "\n";
        }
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
                    so.color = {
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
                so.materialId = matIt->second.as_string();
            }

            auto sourceIt = o.find("sourcePath");
            if (sourceIt != o.end() && sourceIt->second.is_string()) {
                so.sourcePath = NormalizeSlashes(sourceIt->second.as_string());
            }

            auto meshIndexIt = o.find("meshIndex");
            if (meshIndexIt != o.end() && meshIndexIt->second.is_number()) {
                so.meshIndex = static_cast<int>(meshIndexIt->second.as_number());
            }

            auto importedMaterialIt = o.find("importedMaterialName");
            if (importedMaterialIt != o.end() && importedMaterialIt->second.is_string()) {
                so.importedMaterialName = importedMaterialIt->second.as_string();
            }

            auto diffuseTextureIt = o.find("diffuseTexturePath");
            if (diffuseTextureIt != o.end() && diffuseTextureIt->second.is_string()) {
                so.diffuseTexturePath = NormalizeSlashes(diffuseTextureIt->second.as_string());
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
                        so.scriptAttachments.push_back(script);
                    }
                }
            }

            // attach render info for known types
            if (so.type == "Plane") {
                if (!planePrim_) {
                    planePrim_ = std::make_unique<PrimitivePlane>();
                }
                so.vao = planePrim_->vao();
                so.indexCount = planePrim_->indexCount();
                if (so.materialId.empty()) {
                    so.materialId = "pbr_default";
                }
            }
            else if (so.type == "Mesh" && !so.sourcePath.empty()) {
                try {
                    auto model = raceman::LoadModelFromFile(so.sourcePath);
                    const auto infos = raceman::GetMeshInfos(model);
                    if (so.meshIndex >= 0 && so.meshIndex < static_cast<int>(infos.size())) {
                        ApplyMeshInfoToSceneObject(so, infos[static_cast<std::size_t>(so.meshIndex)], model);
                    }
                } catch (...) {
                    if (console_) {
                        console_->AddLog("Failed to reload mesh source: " + so.sourcePath);
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

void SceneEditor::SubmitDraws(Renderer& renderer) {
    UpdateGizmo(renderer);

    for (const auto& o : objects_) {
        if (!o.enabled) continue;
        if (o.vao == 0 || o.indexCount == 0) continue;

        // Build model matrix from Transform (T * Rz * Ry * Rx * S)
        glm::mat4 M(1.0f);
        M = glm::translate(M, o.transform.position);
        glm::vec3 rads = glm::radians(o.transform.rotationEuler);
        M = glm::rotate(M, rads.z, glm::vec3(0,0,1));
        M = glm::rotate(M, rads.y, glm::vec3(0,1,0));
        M = glm::rotate(M, rads.x, glm::vec3(1,0,0));
        M = glm::scale(M, o.transform.scale);

        MeshDrawCommand cmd;
        cmd.vao = o.vao;
        cmd.indexCount = o.indexCount;
        cmd.modelMatrix = M;
        cmd.materialId = o.materialId.empty() ? std::string("pbr_default") : o.materialId;
        if (const Material* material = materialManager_.Get(cmd.materialId)) {
            cmd.color = {
                material->albedoColor[0],
                material->albedoColor[1],
                material->albedoColor[2],
                material->albedoColor[3]
            };
        } else {
            cmd.color = o.color;
        }
        cmd.diffuseTextureId = o.diffuseTextureId;
        cmd.useDiffuseTexture = (cmd.diffuseTextureId != 0);

        renderer.SubmitMesh(cmd);
    }

    SubmitGizmo(renderer);
}

void SceneEditor::SetSavePath(const std::string& path) {
    savePath_ = path;
}

} // namespace raceman


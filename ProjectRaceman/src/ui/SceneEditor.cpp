#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

SceneEditor::SceneEditor() {
    // Load materials at startup
    materialManager_.LoadAll();
    RefreshProjectFiles();
    // Optionally load previous scene
    Load(savePath_);
}

SceneEditor::~SceneEditor() = default;

void SceneEditor::AddMeshPlane() {
    AddPlane();
}

void SceneEditor::RenderUI(float /*deltaTime*/) {
    // Handle Ctrl+S anywhere
    if (IsCtrlSPressed()) {
        Save(savePath_);
    }

    RenderScenePanel();
    RenderInspectorPanel();
    RenderProjectPanel();
}


void SceneEditor::AddPlane() {
    if (!planePrim_) {
        planePrim_ = std::make_unique<PrimitivePlane>();
    }

    SceneObject o;
    o.id = MakeId("plane");
    o.name = "Plane";
    o.type = "Plane";
    o.transform.position = {0.0f, 0.0f, 0.0f};
    o.transform.rotationEuler = {0.0f, 0.0f, 0.0f};
    o.transform.scale = {10.0f, 1.0f, 10.0f};

    // attach render info
    o.vao = planePrim_->vao();
    o.indexCount = planePrim_->indexCount();
    o.materialId = "pbr_default";

    objects_.push_back(o);
    Select(static_cast<int>(objects_.size() - 1));
    if (console_) {
        console_->AddLog(std::string("Added Plane: ") + o.id + " (" + o.name + ")");
    }
    if (onDirty_) onDirty_();
}



void SceneEditor::Select(int index) {
    if (index >= 0 && index < static_cast<int>(objects_.size())) {
        selectedIndex_ = index;
        inspectMaterial_ = false;
    }
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
        out << "      \"color\": [" << o.color.r << ", " << o.color.g << ", " << o.color.b << ", " << o.color.a << "],\n";
        out << "      \"materialId\": \"" << JsonEscape(o.materialId.empty() ? "pbr_default" : o.materialId) << "\"";
        if (o.type == "Mesh" && !o.sourcePath.empty()) {
            out << ",\n";
            out << "      \"sourcePath\": \"" << JsonEscape(NormalizeSlashes(o.sourcePath)) << "\",\n";
            out << "      \"meshIndex\": " << o.meshIndex << ",\n";
            out << "      \"importedMaterialName\": \"" << JsonEscape(o.importedMaterialName) << "\",\n";
            out << "      \"diffuseTexturePath\": \"" << JsonEscape(NormalizeSlashes(o.diffuseTexturePath)) << "\"\n";
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
    UpdateMoveGizmo(renderer);

    for (const auto& o : objects_) {
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

    SubmitMoveGizmo(renderer);
}

void SceneEditor::SetSavePath(const std::string& path) {
    savePath_ = path;
}

} // namespace raceman


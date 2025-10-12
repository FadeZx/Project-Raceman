#include "SceneEditor.h"
#include "../rendering/Renderer.h"
#include "../rendering/PrimitivePlane.h"
#include "./ObjImport.h"
#include "Console.h"

#include <imgui/imgui.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../physics/SimpleJson.h"

// Native Windows file dialog for .obj (Windows only)
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif


namespace fs = std::filesystem;

namespace raceman {

#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
static std::string OpenObjFileDialogWin32() {
    char fileBuffer[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(fileBuffer);
    }
    return std::string();
}
#endif

// Simple material options for editor preview
static const char* kMaterials[] = { "pbr_default", "pbr_metal", "pbr_rough" };
static constexpr int kMaterialCount = sizeof(kMaterials) / sizeof(kMaterials[0]);

static bool IsCtrlSPressed() {
    ImGuiIO& io = ImGui::GetIO();
    return (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S));
}

SceneEditor::SceneEditor() {
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
}

void SceneEditor::RenderScenePanel() {
    if (ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_MenuBar)) {
        // Add button with dropdown (Scene panel)
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Add")) {
                if (ImGui::BeginMenu("Mesh")) {
                    if (ImGui::MenuItem("Plane")) {
                        AddPlane();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Model")) {
                    if (ImGui::MenuItem(".obj")) {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
                        // Use native Windows file dialog
                        std::string selected = OpenObjFileDialogWin32();
                        if (!selected.empty()) {
                            ImportObj(selected);
                        }
#else
                        // Fallback: existing ImGui-based directory scanner
                        importPath_[0] = '\0';
                        objScanDir_ = "assets/mesh";
                        ScanObjDir(objScanDir_);
                        objSelectIndex_ = objFiles_.empty() ? -1 : 0;
                        showImportObjPopup_ = true;
#endif
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }






        // Trigger popup if requested
        if (showImportObjPopup_) { ImGui::OpenPopup("Import OBJ"); showImportObjPopup_ = false; }
        // Import OBJ popup
        if (ImGui::BeginPopupModal("Import OBJ", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Select a .obj file to import");
            ImGui::InputText("Directory", importPath_, sizeof(importPath_));
            ImGui::SameLine();
            if (ImGui::Button("Use")) {
                objScanDir_ = std::string(importPath_[0] ? importPath_ : objScanDir_.c_str());
                ScanObjDir(objScanDir_);
                objSelectIndex_ = objFiles_.empty() ? -1 : 0;
            }
            ImGui::Separator();
            const float listHeight = 200.0f;
            if (ImGui::BeginListBox("##objlist", ImVec2(500.0f, listHeight))) {
                for (int i = 0; i < static_cast<int>(objFiles_.size()); ++i) {
                    const bool selected = (i == objSelectIndex_);
                    if (ImGui::Selectable(objFiles_[i].c_str(), selected)) {
                        objSelectIndex_ = i;
                    }
                }
                ImGui::EndListBox();
            }
            ImGui::Separator();
            if (ImGui::Button("Import Selected")) {
                if (objSelectIndex_ >= 0 && objSelectIndex_ < static_cast<int>(objFiles_.size())) {
                    const std::string fullPath = (std::filesystem::path(objScanDir_) / objFiles_[objSelectIndex_]).string();
                    ImportObj(fullPath);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // List of objects
        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            const bool selected = (i == selectedIndex_);
            if (ImGui::Selectable(objects_[i].name.c_str(), selected)) {
                Select(i);
            }
        }

        // If nothing selected and has objects, select first for convenience
        if (selectedIndex_ < 0 && !objects_.empty()) {
            Select(0);
        }
    }
    ImGui::End();
}

void SceneEditor::RenderInspectorPanel() {
    if (ImGui::Begin("Inspector")) {
        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(objects_.size())) {
            SceneObject& obj = objects_[selectedIndex_];

            // Name
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", obj.name.c_str());
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                obj.name = nameBuf;
            }

            // Type (read-only)
            ImGui::TextDisabled("Type: %s", obj.type.c_str());

            // Transform
            ImGui::Separator();
            ImGui::TextUnformatted("Transform");

            ImGui::DragFloat3("Position", &obj.transform.position.x, 0.1f);
            ImGui::DragFloat3("Rotation (deg)", &obj.transform.rotationEuler.x, 0.5f);
            ImGui::DragFloat3("Scale", &obj.transform.scale.x, 0.1f);

            // Appearance
            ImGui::Separator();
            ImGui::TextUnformatted("Appearance");
            ImGui::ColorEdit4("Color", &obj.color.x);

            // Material
            int matIndex = 0;
            for (int i = 0; i < kMaterialCount; ++i) {
                if (obj.materialId == kMaterials[i]) { matIndex = i; break; }
            }
            if (ImGui::Combo("Material", &matIndex, kMaterials, kMaterialCount)) {
                obj.materialId = kMaterials[matIndex];
            }

            ImGui::Separator();
            if (ImGui::Button("Delete")) {
                // Remove selected object and update selection
                objects_.erase(objects_.begin() + selectedIndex_);
                if (objects_.empty()) {
                    selectedIndex_ = -1;
                } else {
                    if (selectedIndex_ >= static_cast<int>(objects_.size())) {
                        selectedIndex_ = static_cast<int>(objects_.size()) - 1;
                    }
                }
            }
        } else {
            ImGui::TextDisabled("No object selected.");
        }
    }
    ImGui::End();
}

void SceneEditor::ImportObj(const std::string& path) {
    namespace fs = std::filesystem;
    if (path.empty()) return;
    try {
        auto model = raceman::LoadModelFromFile(path);
        std::string baseName;
        try { baseName = fs::path(path).stem().string(); } catch (...) { baseName = "Mesh"; }

        const auto infos = raceman::GetMeshInfos(model);
        for (size_t i = 0; i < infos.size(); ++i) {
            const auto& info = infos[i];
            SceneObject o;
            o.id = MakeId("mesh");
            o.name = baseName + (infos.size() > 1 ? ("_" + std::to_string(i)) : "");
            o.type = "Mesh";
            o.transform.position = {0.0f, 0.0f, 0.0f};
            o.transform.rotationEuler = {0.0f, 0.0f, 0.0f};
            o.transform.scale = {1.0f, 1.0f, 1.0f};
            o.color = {1.0f, 1.0f, 1.0f, 1.0f};
            o.vao = info.vao;
            o.indexCount = info.indexCount;
            o.materialId = "pbr_default";
            o.modelRef = model;
            objects_.push_back(std::move(o));
        }

        if (!objects_.empty()) {
            Select(static_cast<int>(objects_.size()) - 1);
            if (console_) {
                console_->AddLog("Imported OBJ: " + path + " (" + std::to_string(infos.size()) + " mesh" + (infos.size() != 1 ? "es" : "") + ")");
            }
        }
    } catch (const std::exception&) {
        // ignore
    }
}

void SceneEditor::ScanObjDir(const std::string& dir) {
    objFiles_.clear();
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".obj") {
                std::filesystem::path p = entry.path();
                objFiles_.push_back(p.lexically_relative(dir).string());
            }
        }
        std::sort(objFiles_.begin(), objFiles_.end());
    } catch (...) {
        // ignore
    }
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
}



void SceneEditor::Select(int index) {
    if (index >= 0 && index < static_cast<int>(objects_.size())) {
        selectedIndex_ = index;
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
        out << "      \"id\": \"" << o.id << "\",\n";
        out << "      \"name\": \"" << o.name << "\",\n";
        out << "      \"type\": \"" << o.type << "\",\n";
        out << "      \"transform\": {\n";
        out << "        \"position\": [" << o.transform.position.x << ", " << o.transform.position.y << ", " << o.transform.position.z << "],\n";
        out << "        \"rotationEuler\": [" << o.transform.rotationEuler.x << ", " << o.transform.rotationEuler.y << ", " << o.transform.rotationEuler.z << "],\n";
        out << "        \"scale\": [" << o.transform.scale.x << ", " << o.transform.scale.y << ", " << o.transform.scale.z << "]\n";
        out << "      },\n";
        out << "      \"color\": [" << o.color.r << ", " << o.color.g << ", " << o.color.b << ", " << o.color.a << "],\n";
        out << "      \"materialId\": \"" << (o.materialId.empty() ? "pbr_default" : o.materialId) << "\"\n";
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
        cmd.color = o.color;

        renderer.SubmitMesh(cmd);
    }
}

} // namespace raceman
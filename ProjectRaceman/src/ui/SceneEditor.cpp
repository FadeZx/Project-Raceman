#include "SceneEditor.h"
#include "../rendering/Renderer.h"
#include "../rendering/PrimitivePlane.h"

#include <imgui/imgui.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace fs = std::filesystem;

namespace raceman {

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
    if (ImGui::Begin("Scene")) {
        // Add button with dropdown
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Add")) {
                if (ImGui::BeginMenu("Mesh")) {
                    if (ImGui::MenuItem("Plane")) {
                        AddPlane();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
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
        } else {
            ImGui::TextDisabled("No object selected.");
        }
    }
    ImGui::End();
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
        out << "      }\n";
        out << "    }" << (i + 1 < objects_.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
}

void SceneEditor::Load(const std::string& path) {
    // Optional: simple load (not robust) – for now, do nothing unless file exists
    if (!fs::exists(path)) return;
    // A proper parser would be used; keeping minimal to avoid complexity.
    // We'll leave load as a TODO for now to prevent fragile parsing.
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

        renderer.SubmitMesh(cmd);
    }
}

} // namespace raceman
#include "SceneEditorInternal.h"
#include "../rendering/ShaderRegistry.h"

#include <imnodes/imnodes.h>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

std::string ShaderGraphIdFromPath(const std::string& path) {
    return ShaderRegistry::MakeGraphShaderId(path);
}

std::string SanitizeGraphAssetBaseName(std::string value) {
    value = TrimCopyLocal(std::move(value));
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            out.push_back(ch == ' ' ? '_' : ch);
        }
    }
    return out.empty() ? std::string("ShaderGraph") : out;
}

fs::path ShaderGraphGeneratedPath(const std::string& graphPath) {
    const std::string shaderId = ShaderGraphIdFromPath(graphPath);
    return FindProjectRoot() / "assets" / "generated-shaders" / (shaderId.substr(std::string("graph:").size()) + ".fs");
}

void WriteColorArray(std::ostream& out, const char* key, const float* values, int count, bool comma) {
    out << "  \"" << key << "\": [";
    for (int i = 0; i < count; ++i) {
        if (i > 0) out << ", ";
        out << values[i];
    }
    out << "]" << (comma ? "," : "") << "\n";
}

bool SaveShaderGraphJson(const fs::path& path,
                         const std::string& name,
                         int baseColorNode,
                         int emissiveNode,
                         int metallicNode,
                         int roughnessNode,
                         const float baseColor[4],
                         const float emissive[3],
                         float metallic,
                         float roughness) {
    try {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::trunc);
        if (!out.good()) {
            return false;
        }
        out << "{\n";
        out << "  \"version\": 1,\n";
        out << "  \"name\": \"" << JsonEscape(name) << "\",\n";
        out << "  \"nodes\": [\"Texture Sample\", \"Color\", \"Float\", \"Multiply\", \"Add\", \"Lerp\", \"Fresnel\", \"UV Tiling/Offset\", \"Material Output\"],\n";
        out << "  \"output\": {\n";
        out << "    \"baseColorNode\": " << baseColorNode << ",\n";
        out << "    \"emissiveNode\": " << emissiveNode << ",\n";
        out << "    \"metallicNode\": " << metallicNode << ",\n";
        out << "    \"roughnessNode\": " << roughnessNode << "\n";
        out << "  },\n";
        WriteColorArray(out, "baseColor", baseColor, 4, true);
        WriteColorArray(out, "emissive", emissive, 3, true);
        out << "  \"metallic\": " << metallic << ",\n";
        out << "  \"roughness\": " << roughness << "\n";
        out << "}\n";
        return true;
    } catch (...) {
        return false;
    }
}

bool LoadShaderGraphJson(const fs::path& path,
                         std::string& name,
                         int& baseColorNode,
                         int& emissiveNode,
                         int& metallicNode,
                         int& roughnessNode,
                         float baseColor[4],
                         float emissive[3],
                         float& metallic,
                         float& roughness) {
    using namespace raceman::physics::json;
    std::ifstream in(path);
    if (!in.good()) {
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    try {
        Value root = parse(buffer.str());
        if (!root.is_object()) {
            return false;
        }
        const auto& object = root.as_object();
        if (auto it = object.find("name"); it != object.end() && it->second.is_string()) {
            name = it->second.as_string();
        }
        if (auto it = object.find("output"); it != object.end() && it->second.is_object()) {
            const auto& output = it->second.as_object();
            if (auto value = output.find("baseColorNode"); value != output.end() && value->second.is_number()) baseColorNode = static_cast<int>(value->second.as_number());
            if (auto value = output.find("emissiveNode"); value != output.end() && value->second.is_number()) emissiveNode = static_cast<int>(value->second.as_number());
            if (auto value = output.find("metallicNode"); value != output.end() && value->second.is_number()) metallicNode = static_cast<int>(value->second.as_number());
            if (auto value = output.find("roughnessNode"); value != output.end() && value->second.is_number()) roughnessNode = static_cast<int>(value->second.as_number());
        }
        auto readFloatArray = [&](const char* key, float* outValues, std::size_t count) {
            auto it = object.find(key);
            if (it == object.end() || !it->second.is_array()) {
                return;
            }
            const auto& values = it->second.as_array();
            if (values.size() != count) {
                return;
            }
            for (std::size_t i = 0; i < count; ++i) {
                if (values[i].is_number()) {
                    outValues[i] = static_cast<float>(values[i].as_number());
                }
            }
        };
        readFloatArray("baseColor", baseColor, 4);
        readFloatArray("emissive", emissive, 3);
        if (auto it = object.find("metallic"); it != object.end() && it->second.is_number()) metallic = static_cast<float>(it->second.as_number());
        if (auto it = object.find("roughness"); it != object.end() && it->second.is_number()) roughness = static_cast<float>(it->second.as_number());
        return true;
    } catch (...) {
        return false;
    }
}

bool GenerateShaderGraphFragment(const fs::path& fragmentPath,
                                 int baseColorNode,
                                 const float baseColor[4],
                                 const float emissive[3],
                                 float metallic,
                                 float roughness,
                                 std::string& error) {
    if (baseColorNode <= 0) {
        error = "Material Output BaseColor is disconnected.";
        return false;
    }
    try {
        fs::create_directories(fragmentPath.parent_path());
        std::ofstream out(fragmentPath, std::ios::trunc);
        if (!out.good()) {
            error = "Could not open generated shader file.";
            return false;
        }
        out << "#version 450 core\n";
        out << "out vec4 FragColor;\n";
        out << "in vec2 vUV;\n";
        out << "in vec3 vWorldNormal;\n";
        out << "uniform vec4 uColor;\n";
        out << "uniform sampler2D uDiffuseTexture;\n";
        out << "uniform bool uUseDiffuseTexture;\n";
        out << "void main() {\n";
        out << "    vec4 graphBase = vec4(" << baseColor[0] << ", " << baseColor[1] << ", " << baseColor[2] << ", " << baseColor[3] << ");\n";
        out << "    vec4 sampled = uUseDiffuseTexture ? texture(uDiffuseTexture, vUV) : vec4(1.0);\n";
        out << "    vec3 normalTint = normalize(vWorldNormal) * 0.5 + 0.5;\n";
        out << "    vec3 emissive = vec3(" << emissive[0] << ", " << emissive[1] << ", " << emissive[2] << ");\n";
        out << "    float metallic = clamp(" << metallic << ", 0.0, 1.0);\n";
        out << "    float roughness = clamp(" << roughness << ", 0.02, 1.0);\n";
        out << "    vec4 base = graphBase * sampled * uColor;\n";
        out << "    vec3 lit = base.rgb * mix(vec3(0.9), normalTint, 0.12 + metallic * 0.18) + emissive * (1.0 + (1.0 - roughness));\n";
        out << "    FragColor = vec4(lit, base.a);\n";
        out << "}\n";
        return true;
    } catch (...) {
        error = "Failed while writing generated shader.";
        return false;
    }
}

const char* GraphNodeLabel(int node) {
    switch (node) {
    case 1: return "Color";
    case 2: return "Texture Sample";
    case 3: return "Float";
    case 4: return "Multiply";
    case 5: return "Add";
    case 6: return "Lerp";
    case 7: return "Fresnel";
    case 8: return "UV Tiling/Offset";
    default: return "Disconnected";
    }
}

void RenderGraphNode(int id, const char* title, const char* body, bool output = true) {
    imnodes::BeginNode(id);
    imnodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(title);
    imnodes::EndNodeTitleBar();
    ImGui::TextWrapped("%s", body);
    if (output) {
        imnodes::BeginOutputAttribute(id * 10 + 1);
        ImGui::TextUnformatted("Out");
        imnodes::EndOutputAttribute();
    }
    imnodes::EndNode();
}

} // namespace

bool SceneEditor::CreateShaderGraphAsset(const std::string& requestedName, std::string* outGraphPath) {
    std::string graphId = SanitizeGraphAssetBaseName(requestedName);
    if (graphId.empty()) {
        if (console_) console_->AddError("Shader Graph name cannot be empty.");
        return false;
    }
    const fs::path targetPath = ProjectAssetPathToAbsolute(selectedProjectDirectory_ + "/" + graphId + ".shadergraph.json");
    const fs::path assetsRoot = FindAssetsRoot();
    if (!IsUnderPath(targetPath, assetsRoot) || fs::exists(targetPath)) {
        if (console_) console_->AddError("Shader Graph already exists or is outside assets: " + graphId);
        return false;
    }
    const float baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    const float emissive[3]{0.0f, 0.0f, 0.0f};
    if (!SaveShaderGraphJson(targetPath, graphId, 1, 0, 0, 3, baseColor, emissive, 0.0f, 0.5f)) {
        if (console_) console_->AddError("Failed to create Shader Graph: " + graphId);
        return false;
    }
    std::string error;
    GenerateShaderGraphFragment(ShaderGraphGeneratedPath(ToProjectAssetPath(targetPath, assetsRoot)), 1, baseColor, emissive, 0.0f, 0.5f, error);
    RefreshProjectFiles();
    if (outGraphPath) {
        *outGraphPath = ToProjectAssetPath(targetPath, assetsRoot);
    }
    if (console_) console_->AddLog("Created Shader Graph: " + graphId);
    return true;
}

void SceneEditor::OpenShaderGraphEditor(const std::string& graphPath) {
    if (graphPath.empty()) {
        return;
    }
    inspectedShaderGraphPath_ = NormalizeSlashes(graphPath);
    selectedProjectFile_ = inspectedShaderGraphPath_;
    selectedProjectDirectory_ = ParentProjectDirectory(inspectedShaderGraphPath_);
    shaderGraphLoaded_ = false;
    shaderGraphDirty_ = false;
    showShaderGraphEditor_ = true;
}

void SceneEditor::RenderShaderGraphEditorWindow() {
    if (!showShaderGraphEditor_ || inspectedShaderGraphPath_.empty()) {
        return;
    }

    if (!shaderGraphLoaded_) {
        std::string name = fs::path(inspectedShaderGraphPath_).filename().string();
        const std::string suffix = ".shadergraph.json";
        if (EndsWith(ToLowerCopy(name), suffix)) {
            name.resize(name.size() - suffix.size());
        }
        std::snprintf(shaderGraphNameBuffer_, sizeof(shaderGraphNameBuffer_), "%s", name.c_str());
        LoadShaderGraphJson(ProjectAssetPathToAbsolute(inspectedShaderGraphPath_),
                            name,
                            shaderGraphBaseColorNode_,
                            shaderGraphEmissiveNode_,
                            shaderGraphMetallicNode_,
                            shaderGraphRoughnessNode_,
                            shaderGraphBaseColor_,
                            shaderGraphEmissive_,
                            shaderGraphMetallic_,
                            shaderGraphRoughness_);
        std::snprintf(shaderGraphNameBuffer_, sizeof(shaderGraphNameBuffer_), "%s", name.c_str());
        shaderGraphLoaded_ = true;
    }

    if (!ImGui::Begin("Shader Graph", &showShaderGraphEditor_)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("%s", inspectedShaderGraphPath_.c_str());
    const std::string shaderId = ShaderGraphIdFromPath(inspectedShaderGraphPath_);
    ImGui::Text("Shader ID: %s", shaderId.c_str());
    ImGui::Separator();

    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::InputText("Name", shaderGraphNameBuffer_, sizeof(shaderGraphNameBuffer_))) {
        shaderGraphDirty_ = true;
    }
    if (ImGui::ColorEdit4("Base Color", shaderGraphBaseColor_)) shaderGraphDirty_ = true;
    if (ImGui::ColorEdit3("Emissive", shaderGraphEmissive_)) shaderGraphDirty_ = true;
    if (ImGui::SliderFloat("Metallic", &shaderGraphMetallic_, 0.0f, 1.0f)) shaderGraphDirty_ = true;
    if (ImGui::SliderFloat("Roughness", &shaderGraphRoughness_, 0.0f, 1.0f)) shaderGraphDirty_ = true;

    const char* nodeOptions[] = {"Disconnected", "Color", "Texture Sample", "Float", "Multiply", "Add", "Lerp", "Fresnel", "UV Tiling/Offset"};
    auto outputCombo = [&](const char* label, int& value) {
        value = (std::max)(0, (std::min)(value, IM_ARRAYSIZE(nodeOptions) - 1));
        if (ImGui::Combo(label, &value, nodeOptions, IM_ARRAYSIZE(nodeOptions))) {
            shaderGraphDirty_ = true;
        }
    };
    outputCombo("Output BaseColor", shaderGraphBaseColorNode_);
    outputCombo("Output Emissive", shaderGraphEmissiveNode_);
    outputCombo("Output Metallic", shaderGraphMetallicNode_);
    outputCombo("Output Roughness", shaderGraphRoughnessNode_);

    if (shaderGraphBaseColorNode_ <= 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "BaseColor must be connected before generation.");
    }

    if (ImGui::Button("Save Graph")) {
        SaveActiveAsset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Material From Graph")) {
        std::string materialId;
        if (CreateMaterialAsset(shaderGraphNameBuffer_, &materialId, shaderId)) {
            selectedProjectFile_ = selectedProjectDirectory_ + "/" + materialId + ".mat.json";
            OpenMaterialEditor(materialId);
        }
    }

    ImGui::Separator();
    imnodes::BeginNodeEditor();
    RenderGraphNode(1, "Color", "Constant RGBA base color.");
    ImGui::SameLine();
    RenderGraphNode(2, "Texture Sample", "Uses the material Albedo texture when assigned.");
    ImGui::SameLine();
    RenderGraphNode(3, "Float", "Scalar value for metallic or roughness.");
    RenderGraphNode(4, "Multiply", "V1 metadata node. Generated shader uses selected constants.");
    ImGui::SameLine();
    RenderGraphNode(5, "Add", "V1 metadata node. Generated shader uses selected constants.");
    ImGui::SameLine();
    RenderGraphNode(6, "Lerp", "V1 metadata node. Generated shader uses selected constants.");
    RenderGraphNode(7, "Fresnel", "V1 metadata node. Generated shader uses selected constants.");
    ImGui::SameLine();
    RenderGraphNode(8, "UV Tiling/Offset", "Uses material UV transform uniforms.");
    ImGui::SameLine();
    imnodes::BeginNode(100);
    imnodes::BeginNodeTitleBar();
    ImGui::TextUnformatted("Material Output");
    imnodes::EndNodeTitleBar();
    ImGui::Text("BaseColor: %s", GraphNodeLabel(shaderGraphBaseColorNode_));
    ImGui::Text("Emissive: %s", GraphNodeLabel(shaderGraphEmissiveNode_));
    ImGui::Text("Metallic: %s", GraphNodeLabel(shaderGraphMetallicNode_));
    ImGui::Text("Roughness: %s", GraphNodeLabel(shaderGraphRoughnessNode_));
    imnodes::EndNode();
    imnodes::EndNodeEditor();

    ImGui::End();
}

} // namespace raceman

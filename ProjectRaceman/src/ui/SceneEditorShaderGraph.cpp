#include "SceneEditorInternal.h"
#include "../rendering/ShaderRegistry.h"

#include <imnodes/imnodes.h>

#include <algorithm>
#include <functional>
#include <map>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

enum class GraphValueType {
    Any,
    Float,
    Vec2,
    Vec3,
    Vec4
};

struct CompiledValue {
    std::string code;
    GraphValueType type{GraphValueType::Any};
};

constexpr int kMaterialOutputNode = 100;
constexpr int kOutputPinOffset = 1;
constexpr int kInputPinOffset = 10;
constexpr std::size_t kMaxShaderGraphUndoStates = 80;

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

int OutputPin(int nodeId) {
    return nodeId * 100 + kOutputPinOffset;
}

int InputPin(int nodeId, int slot) {
    return nodeId * 100 + kInputPinOffset + slot;
}

int PinNodeId(int pinId) {
    return pinId / 100;
}

int PinSlot(int pinId) {
    return pinId % 100 - kInputPinOffset;
}

const char* NodeTypeLabel(const std::string& type) {
    if (type == "Color") return "Color";
    if (type == "Float") return "Float";
    if (type == "Vector2") return "Vector2";
    if (type == "Vector3") return "Vector3";
    if (type == "Vector4") return "Vector4";
    if (type == "TextureSample") return "Texture Sample";
    if (type == "UV") return "UV Tiling/Offset";
    if (type == "Add") return "Add";
    if (type == "Multiply") return "Multiply";
    if (type == "Lerp") return "Lerp";
    if (type == "Clamp") return "Clamp";
    if (type == "OneMinus") return "One Minus";
    if (type == "Fresnel") return "Fresnel";
    if (type == "Note") return "Note";
    if (type == "MaterialOutput") return "Material Output";
    return "Node";
}

GraphValueType OutputTypeForNode(const ShaderGraphNodeState& node) {
    if (node.type == "Float" || node.type == "Fresnel" || node.type == "Clamp") return GraphValueType::Float;
    if (node.type == "Vector2" || node.type == "UV") return GraphValueType::Vec2;
    if (node.type == "Vector3") return GraphValueType::Vec3;
    if (node.type == "Color" || node.type == "Vector4" || node.type == "TextureSample") return GraphValueType::Vec4;
    return GraphValueType::Any;
}

GraphValueType InputTypeForPin(int pinId) {
    const int nodeId = PinNodeId(pinId);
    const int slot = PinSlot(pinId);
    if (nodeId == kMaterialOutputNode) {
        switch (slot) {
        case 0: return GraphValueType::Vec4; // BaseColor
        case 1: return GraphValueType::Vec3; // Emissive
        case 2: return GraphValueType::Float; // Metallic
        case 3: return GraphValueType::Float; // Roughness
        case 4: return GraphValueType::Float; // Alpha
        case 5: return GraphValueType::Vec3; // Normal
        default: return GraphValueType::Any;
        }
    }
    return GraphValueType::Any;
}

bool IsTypeCompatible(GraphValueType source, GraphValueType target) {
    if (source == GraphValueType::Any || target == GraphValueType::Any) return true;
    if (source == target) return true;
    if (source == GraphValueType::Vec4 && target == GraphValueType::Vec3) return true;
    if (source == GraphValueType::Vec3 && target == GraphValueType::Vec4) return true;
    return false;
}

std::string FloatText(float value) {
    std::ostringstream out;
    out << value;
    std::string text = out.str();
    if (text.find('.') == std::string::npos && text.find('e') == std::string::npos && text.find('E') == std::string::npos) {
        text += ".0";
    }
    return text;
}

std::string VecText(const char* ctor, const float* values, int count) {
    std::string text = ctor;
    text += "(";
    for (int i = 0; i < count; ++i) {
        if (i > 0) text += ", ";
        text += FloatText(values[i]);
    }
    text += ")";
    return text;
}

CompiledValue ConvertValue(CompiledValue value, GraphValueType target) {
    if (target == GraphValueType::Any || value.type == target) return value;
    if (target == GraphValueType::Float) {
        if (value.type == GraphValueType::Vec4 || value.type == GraphValueType::Vec3 || value.type == GraphValueType::Vec2) {
            value.code = "(" + value.code + ").x";
            value.type = GraphValueType::Float;
        }
        return value;
    }
    if (target == GraphValueType::Vec3 && value.type == GraphValueType::Vec4) {
        value.code = "(" + value.code + ").rgb";
        value.type = GraphValueType::Vec3;
        return value;
    }
    if (target == GraphValueType::Vec4 && value.type == GraphValueType::Vec3) {
        value.code = "vec4(" + value.code + ", 1.0)";
        value.type = GraphValueType::Vec4;
        return value;
    }
    if (target == GraphValueType::Vec4 && value.type == GraphValueType::Float) {
        value.code = "vec4(" + value.code + ")";
        value.type = GraphValueType::Vec4;
        return value;
    }
    if (target == GraphValueType::Vec3 && value.type == GraphValueType::Float) {
        value.code = "vec3(" + value.code + ")";
        value.type = GraphValueType::Vec3;
        return value;
    }
    return value;
}

ShaderGraphNodeState MakeNode(int id, const std::string& type, const glm::vec2& position) {
    ShaderGraphNodeState node;
    node.id = id;
    node.type = type;
    node.title = NodeTypeLabel(type);
    node.position = position;
    if (type == "Float") node.floatValue = 0.5f;
    if (type == "Vector2") { node.vectorValue[0] = 1.0f; node.vectorValue[1] = 1.0f; }
    if (type == "Vector3") { node.vectorValue[0] = 1.0f; node.vectorValue[1] = 1.0f; node.vectorValue[2] = 1.0f; }
    if (type == "Vector4") { node.vectorValue[0] = 1.0f; node.vectorValue[1] = 1.0f; node.vectorValue[2] = 1.0f; node.vectorValue[3] = 1.0f; }
    if (type == "Note") {
        node.title = "Note";
        node.noteText = "Add graph note";
        node.color[0] = 0.95f;
        node.color[1] = 0.78f;
        node.color[2] = 0.28f;
        node.color[3] = 1.0f;
    }
    return node;
}

ShaderGraphNodeState* FindNode(std::vector<ShaderGraphNodeState>& nodes, int id) {
    for (auto& node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

const ShaderGraphNodeState* FindNode(const std::vector<ShaderGraphNodeState>& nodes, int id) {
    for (const auto& node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

const ShaderGraphLinkState* FindInputLink(const std::vector<ShaderGraphLinkState>& links, int endPin) {
    for (const auto& link : links) {
        if (link.endPin == endPin) return &link;
    }
    return nullptr;
}

GraphValueType InputTypeForPin(const std::vector<ShaderGraphNodeState>& nodes, int pinId) {
    const int nodeId = PinNodeId(pinId);
    const int slot = PinSlot(pinId);
    if (nodeId == kMaterialOutputNode) return InputTypeForPin(pinId);

    const ShaderGraphNodeState* node = FindNode(nodes, nodeId);
    if (node == nullptr) return GraphValueType::Any;
    if (node->type == "TextureSample" && slot == 0) return GraphValueType::Vec2;
    if (node->type == "Lerp" && (slot == 0 || slot == 1)) return GraphValueType::Vec4;
    if (node->type == "Lerp" && slot == 2) return GraphValueType::Float;
    if (node->type == "Clamp") return GraphValueType::Float;
    return GraphValueType::Any;
}

ImU32 SocketColor(GraphValueType type, const std::string& semantic = {}) {
    if (semantic == "Color") return ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.30f, 0.72f, 1.0f));
    if (semantic == "Texture") return ImGui::ColorConvertFloat4ToU32(ImVec4(0.38f, 0.76f, 0.95f, 1.0f));
    switch (type) {
    case GraphValueType::Float: return ImGui::ColorConvertFloat4ToU32(ImVec4(0.88f, 0.86f, 0.24f, 1.0f));
    case GraphValueType::Vec2:
    case GraphValueType::Vec3:
    case GraphValueType::Vec4:  return ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.78f, 0.92f, 1.0f));
    case GraphValueType::Any:
    default: return ImGui::ColorConvertFloat4ToU32(ImVec4(0.64f, 0.66f, 0.68f, 1.0f));
    }
}

GraphValueType InputSocketTypeForNode(const std::vector<ShaderGraphNodeState>& nodes, int nodeId, int slot) {
    return InputTypeForPin(nodes, InputPin(nodeId, slot));
}

GraphValueType OutputSocketTypeForNode(const ShaderGraphNodeState& node) {
    return OutputTypeForNode(node);
}

const char* OutputSocketSemantic(const ShaderGraphNodeState& node) {
    if (node.type == "Color") return "Color";
    if (node.type == "TextureSample") return "Texture";
    return "";
}

void BeginColoredInputAttribute(const std::vector<ShaderGraphNodeState>& nodes,
                                int nodeId,
                                int slot,
                                ImNodesPinShape shape = ImNodesPinShape_CircleFilled) {
    ImNodes::PushColorStyle(ImNodesCol_Pin, SocketColor(InputSocketTypeForNode(nodes, nodeId, slot)));
    ImNodes::PushColorStyle(ImNodesCol_PinHovered, SocketColor(InputSocketTypeForNode(nodes, nodeId, slot)));
    ImNodes::BeginInputAttribute(InputPin(nodeId, slot), shape);
}

void EndColoredInputAttribute() {
    ImNodes::EndInputAttribute();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
}

void BeginColoredOutputAttribute(const ShaderGraphNodeState& node,
                                 ImNodesPinShape shape = ImNodesPinShape_CircleFilled) {
    ImNodes::PushColorStyle(ImNodesCol_Pin, SocketColor(OutputSocketTypeForNode(node), OutputSocketSemantic(node)));
    ImNodes::PushColorStyle(ImNodesCol_PinHovered, SocketColor(OutputSocketTypeForNode(node), OutputSocketSemantic(node)));
    ImNodes::BeginOutputAttribute(OutputPin(node.id), shape);
}

void EndColoredOutputAttribute() {
    ImNodes::EndOutputAttribute();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
}

ImU32 LinkColorForPin(const std::vector<ShaderGraphNodeState>& nodes, int startPin) {
    const ShaderGraphNodeState* source = FindNode(nodes, PinNodeId(startPin));
    if (source == nullptr) return SocketColor(GraphValueType::Any);
    return SocketColor(OutputSocketTypeForNode(*source), OutputSocketSemantic(*source));
}

ImU32 BrightenColor(ImU32 color, float amount) {
    ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    value.x = value.x + (1.0f - value.x) * amount;
    value.y = value.y + (1.0f - value.y) * amount;
    value.z = value.z + (1.0f - value.z) * amount;
    value.w = 1.0f;
    return ImGui::ColorConvertFloat4ToU32(value);
}

ShaderGraphHistoryState CaptureShaderGraphState(const char* name,
                                                const std::vector<ShaderGraphNodeState>& nodes,
                                                const std::vector<ShaderGraphLinkState>& links,
                                                int nextNodeId,
                                                int nextLinkId,
                                                int selectedNodeId) {
    ShaderGraphHistoryState state;
    state.name = name != nullptr ? name : "";
    state.nodes = nodes;
    state.links = links;
    state.nextNodeId = nextNodeId;
    state.nextLinkId = nextLinkId;
    state.selectedNodeId = selectedNodeId;
    return state;
}

bool ReadFloatArray(const raceman::physics::json::Object& object, const char* key, float* outValues, std::size_t count) {
    auto it = object.find(key);
    if (it == object.end() || !it->second.is_array()) return false;
    const auto& values = it->second.as_array();
    if (values.size() != count) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!values[i].is_number()) return false;
        outValues[i] = static_cast<float>(values[i].as_number());
    }
    return true;
}

void EnsureDefaultGraphData(std::vector<ShaderGraphNodeState>& nodes,
                            std::vector<ShaderGraphLinkState>& links,
                            int& nextNodeId,
                            int& nextLinkId) {
    if (!nodes.empty()) return;
    nodes.push_back(MakeNode(1, "Color", {32.0f, 36.0f}));
    nodes.push_back(MakeNode(2, "TextureSample", {32.0f, 190.0f}));
    nodes.push_back(MakeNode(3, "Float", {32.0f, 344.0f}));
    nodes.push_back(MakeNode(4, "Multiply", {330.0f, 36.0f}));
    nodes.push_back(MakeNode(5, "Add", {330.0f, 190.0f}));
    nodes.push_back(MakeNode(6, "Lerp", {330.0f, 344.0f}));
    nodes.push_back(MakeNode(7, "Fresnel", {630.0f, 36.0f}));
    nodes.push_back(MakeNode(8, "UV", {630.0f, 190.0f}));
    nodes.push_back(MakeNode(kMaterialOutputNode, "MaterialOutput", {630.0f, 344.0f}));
    links.push_back({1, OutputPin(1), InputPin(kMaterialOutputNode, 0)});
    links.push_back({2, OutputPin(3), InputPin(kMaterialOutputNode, 3)});
    nextNodeId = 101;
    nextLinkId = 3;
}

void ApplyLegacyOutputFields(const std::vector<ShaderGraphLinkState>& links,
                             int& baseColorNode,
                             int& emissiveNode,
                             int& metallicNode,
                             int& roughnessNode) {
    baseColorNode = 0;
    emissiveNode = 0;
    metallicNode = 0;
    roughnessNode = 0;
    for (const auto& link : links) {
        const int sourceNode = PinNodeId(link.startPin);
        if (link.endPin == InputPin(kMaterialOutputNode, 0)) baseColorNode = sourceNode;
        if (link.endPin == InputPin(kMaterialOutputNode, 1)) emissiveNode = sourceNode;
        if (link.endPin == InputPin(kMaterialOutputNode, 2)) metallicNode = sourceNode;
        if (link.endPin == InputPin(kMaterialOutputNode, 3)) roughnessNode = sourceNode;
    }
}

bool SaveShaderGraphJson(const fs::path& path,
                         const std::string& name,
                         const std::vector<ShaderGraphNodeState>& nodes,
                         const std::vector<ShaderGraphLinkState>& links) {
    try {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::trunc);
        if (!out.good()) return false;
        out << "{\n";
        out << "  \"version\": 2,\n";
        out << "  \"name\": \"" << JsonEscape(name) << "\",\n";
        out << "  \"nodes\": [\n";
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto& node = nodes[i];
            out << "    {\n";
            out << "      \"id\": " << node.id << ",\n";
            out << "      \"type\": \"" << JsonEscape(node.type) << "\",\n";
            out << "      \"title\": \"" << JsonEscape(node.title) << "\",\n";
            out << "      \"position\": [" << node.position.x << ", " << node.position.y << "],\n";
            out << "      \"pins\": {\n";
            if (node.type == "Note") {
                out << "        \"outputs\": [],\n";
                out << "        \"inputs\": []\n";
            } else if (node.type != "MaterialOutput") {
                out << "        \"outputs\": [{\"id\": " << OutputPin(node.id) << ", \"name\": \"Out\"}],\n";
                out << "        \"inputs\": []\n";
            } else {
                out << "        \"outputs\": [],\n";
                out << "        \"inputs\": [\n";
                out << "          {\"id\": " << InputPin(node.id, 0) << ", \"name\": \"BaseColor\", \"type\": \"vec4\"},\n";
                out << "          {\"id\": " << InputPin(node.id, 1) << ", \"name\": \"Emissive\", \"type\": \"vec3\"},\n";
                out << "          {\"id\": " << InputPin(node.id, 2) << ", \"name\": \"Metallic\", \"type\": \"float\"},\n";
                out << "          {\"id\": " << InputPin(node.id, 3) << ", \"name\": \"Roughness\", \"type\": \"float\"},\n";
                out << "          {\"id\": " << InputPin(node.id, 4) << ", \"name\": \"Alpha\", \"type\": \"float\"},\n";
                out << "          {\"id\": " << InputPin(node.id, 5) << ", \"name\": \"Normal\", \"type\": \"vec3\"}\n";
                out << "        ]\n";
            }
            out << "      },\n";
            out << "      \"properties\": {\n";
            out << "        \"color\": [" << node.color[0] << ", " << node.color[1] << ", " << node.color[2] << ", " << node.color[3] << "],\n";
            out << "        \"value\": " << node.floatValue << ",\n";
            out << "        \"vector\": [" << node.vectorValue[0] << ", " << node.vectorValue[1] << ", " << node.vectorValue[2] << ", " << node.vectorValue[3] << "],\n";
            out << "        \"note\": \"" << JsonEscape(node.noteText) << "\"\n";
            out << "      }\n";
            out << "    }" << (i + 1 < nodes.size() ? "," : "") << "\n";
        }
        out << "  ],\n";
        out << "  \"links\": [\n";
        for (std::size_t i = 0; i < links.size(); ++i) {
            const auto& link = links[i];
            out << "    {\"id\": " << link.id << ", \"startPin\": " << link.startPin << ", \"endPin\": " << link.endPin << "}";
            out << (i + 1 < links.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return true;
    } catch (...) {
        return false;
    }
}

bool LoadShaderGraphJson(const fs::path& path,
                         std::string& name,
                         std::vector<ShaderGraphNodeState>& nodes,
                         std::vector<ShaderGraphLinkState>& links,
                         int& nextNodeId,
                         int& nextLinkId) {
    using namespace raceman::physics::json;
    std::ifstream in(path);
    if (!in.good()) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    try {
        Value root = parse(buffer.str());
        if (!root.is_object()) return false;
        const auto& object = root.as_object();
        if (auto it = object.find("name"); it != object.end() && it->second.is_string()) name = it->second.as_string();

        nodes.clear();
        links.clear();
        int maxNodeId = 0;
        int maxLinkId = 0;
        bool loadedV2 = false;
        if (auto nodesIt = object.find("nodes"); nodesIt != object.end() && nodesIt->second.is_array()) {
            for (const auto& nodeValue : nodesIt->second.as_array()) {
                if (!nodeValue.is_object()) continue;
                const auto& nodeObject = nodeValue.as_object();
                auto idIt = nodeObject.find("id");
                auto typeIt = nodeObject.find("type");
                if (idIt == nodeObject.end() || typeIt == nodeObject.end() || !idIt->second.is_number() || !typeIt->second.is_string()) continue;
                ShaderGraphNodeState node = MakeNode(static_cast<int>(idIt->second.as_number()), typeIt->second.as_string(), {0.0f, 0.0f});
                if (auto titleIt = nodeObject.find("title"); titleIt != nodeObject.end() && titleIt->second.is_string()) node.title = titleIt->second.as_string();
                if (auto posIt = nodeObject.find("position"); posIt != nodeObject.end() && posIt->second.is_array()) {
                    const auto& pos = posIt->second.as_array();
                    if (pos.size() == 2 && pos[0].is_number() && pos[1].is_number()) {
                        node.position = {static_cast<float>(pos[0].as_number()), static_cast<float>(pos[1].as_number())};
                    }
                }
                if (auto propsIt = nodeObject.find("properties"); propsIt != nodeObject.end() && propsIt->second.is_object()) {
                    const auto& props = propsIt->second.as_object();
                    ReadFloatArray(props, "color", node.color, 4);
                    ReadFloatArray(props, "vector", node.vectorValue, 4);
                    if (auto valueIt = props.find("value"); valueIt != props.end() && valueIt->second.is_number()) {
                        node.floatValue = static_cast<float>(valueIt->second.as_number());
                    }
                    if (auto noteIt = props.find("note"); noteIt != props.end() && noteIt->second.is_string()) {
                        node.noteText = noteIt->second.as_string();
                    }
                }
                maxNodeId = (std::max)(maxNodeId, node.id);
                loadedV2 = true;
                nodes.push_back(node);
            }
        }
        if (loadedV2) {
            if (auto linksIt = object.find("links"); linksIt != object.end() && linksIt->second.is_array()) {
                for (const auto& linkValue : linksIt->second.as_array()) {
                    if (!linkValue.is_object()) continue;
                    const auto& linkObject = linkValue.as_object();
                    auto idIt = linkObject.find("id");
                    auto startIt = linkObject.find("startPin");
                    auto endIt = linkObject.find("endPin");
                    if (idIt == linkObject.end() || startIt == linkObject.end() || endIt == linkObject.end()) continue;
                    if (!idIt->second.is_number() || !startIt->second.is_number() || !endIt->second.is_number()) continue;
                    ShaderGraphLinkState link;
                    link.id = static_cast<int>(idIt->second.as_number());
                    link.startPin = static_cast<int>(startIt->second.as_number());
                    link.endPin = static_cast<int>(endIt->second.as_number());
                    maxLinkId = (std::max)(maxLinkId, link.id);
                    links.push_back(link);
                }
            }
        } else {
            float baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
            float emissive[3]{0.0f, 0.0f, 0.0f};
            float metallic = 0.0f;
            float roughness = 0.5f;
            int baseColorNode = 1;
            int emissiveNode = 0;
            int metallicNode = 0;
            int roughnessNode = 3;
            if (auto outputIt = object.find("output"); outputIt != object.end() && outputIt->second.is_object()) {
                const auto& output = outputIt->second.as_object();
                if (auto value = output.find("baseColorNode"); value != output.end() && value->second.is_number()) baseColorNode = static_cast<int>(value->second.as_number());
                if (auto value = output.find("emissiveNode"); value != output.end() && value->second.is_number()) emissiveNode = static_cast<int>(value->second.as_number());
                if (auto value = output.find("metallicNode"); value != output.end() && value->second.is_number()) metallicNode = static_cast<int>(value->second.as_number());
                if (auto value = output.find("roughnessNode"); value != output.end() && value->second.is_number()) roughnessNode = static_cast<int>(value->second.as_number());
            }
            ReadFloatArray(object, "baseColor", baseColor, 4);
            ReadFloatArray(object, "emissive", emissive, 3);
            if (auto it = object.find("metallic"); it != object.end() && it->second.is_number()) metallic = static_cast<float>(it->second.as_number());
            if (auto it = object.find("roughness"); it != object.end() && it->second.is_number()) roughness = static_cast<float>(it->second.as_number());

            nodes.push_back(MakeNode(1, "Color", {32.0f, 36.0f}));
            std::copy(baseColor, baseColor + 4, nodes.back().color);
            nodes.push_back(MakeNode(2, "TextureSample", {32.0f, 190.0f}));
            nodes.push_back(MakeNode(3, "Float", {32.0f, 344.0f}));
            nodes.back().floatValue = roughness;
            nodes.push_back(MakeNode(4, "Multiply", {330.0f, 36.0f}));
            nodes.push_back(MakeNode(5, "Add", {330.0f, 190.0f}));
            nodes.push_back(MakeNode(6, "Lerp", {330.0f, 344.0f}));
            nodes.push_back(MakeNode(7, "Fresnel", {630.0f, 36.0f}));
            nodes.push_back(MakeNode(8, "UV", {630.0f, 190.0f}));
            nodes.push_back(MakeNode(9, "Color", {32.0f, 498.0f}));
            nodes.back().color[0] = emissive[0]; nodes.back().color[1] = emissive[1]; nodes.back().color[2] = emissive[2]; nodes.back().color[3] = 1.0f;
            nodes.push_back(MakeNode(10, "Float", {330.0f, 498.0f}));
            nodes.back().floatValue = metallic;
            nodes.push_back(MakeNode(kMaterialOutputNode, "MaterialOutput", {630.0f, 344.0f}));
            int linkId = 1;
            auto addLegacyLink = [&](int nodeId, int outputSlot) {
                if (nodeId > 0) links.push_back({linkId++, OutputPin(nodeId), InputPin(kMaterialOutputNode, outputSlot)});
            };
            addLegacyLink(baseColorNode, 0);
            addLegacyLink(emissiveNode > 0 ? emissiveNode : 9, 1);
            addLegacyLink(metallicNode > 0 ? metallicNode : 10, 2);
            addLegacyLink(roughnessNode, 3);
            maxNodeId = kMaterialOutputNode;
            maxLinkId = linkId - 1;
        }
        nextNodeId = (std::max)(101, maxNodeId + 1);
        nextLinkId = (std::max)(1, maxLinkId + 1);
        return true;
    } catch (...) {
        return false;
    }
}

CompiledValue CompileNodeExpression(const std::vector<ShaderGraphNodeState>& nodes,
                                    const std::vector<ShaderGraphLinkState>& links,
                                    int nodeId,
                                    std::unordered_set<int>& stack,
                                    std::string& error) {
    const ShaderGraphNodeState* node = FindNode(nodes, nodeId);
    if (node == nullptr) {
        error = "Missing shader graph node.";
        return {};
    }
    if (!stack.insert(nodeId).second) {
        error = "Shader graph contains a cycle.";
        return {};
    }

    auto input = [&](int slot, GraphValueType fallbackType, const std::string& fallback) -> CompiledValue {
        const ShaderGraphLinkState* link = FindInputLink(links, InputPin(nodeId, slot));
        if (link == nullptr) return {fallback, fallbackType};
        return CompileNodeExpression(nodes, links, PinNodeId(link->startPin), stack, error);
    };

    CompiledValue result;
    if (node->type == "Color") result = {VecText("vec4", node->color, 4), GraphValueType::Vec4};
    else if (node->type == "Float") result = {FloatText(node->floatValue), GraphValueType::Float};
    else if (node->type == "Vector2") result = {VecText("vec2", node->vectorValue, 2), GraphValueType::Vec2};
    else if (node->type == "Vector3") result = {VecText("vec3", node->vectorValue, 3), GraphValueType::Vec3};
    else if (node->type == "Vector4") result = {VecText("vec4", node->vectorValue, 4), GraphValueType::Vec4};
    else if (node->type == "TextureSample") {
        CompiledValue uv = ConvertValue(input(0, GraphValueType::Vec2, "vUV"), GraphValueType::Vec2);
        result = {"(uUseDiffuseTexture ? texture(uDiffuseTexture, " + uv.code + ") : vec4(1.0))", GraphValueType::Vec4};
    } else if (node->type == "UV") result = {"(vUV * uUvTiling + uUvOffset)", GraphValueType::Vec2};
    else if (node->type == "Add" || node->type == "Multiply") {
        CompiledValue a = input(0, GraphValueType::Float, "0.0");
        CompiledValue b = ConvertValue(input(1, a.type, node->type == "Multiply" ? "1.0" : "0.0"), a.type);
        result = {"(" + a.code + (node->type == "Multiply" ? " * " : " + ") + b.code + ")", a.type};
    } else if (node->type == "Lerp") {
        CompiledValue a = ConvertValue(input(0, GraphValueType::Vec4, "vec4(0.0)"), GraphValueType::Vec4);
        CompiledValue b = ConvertValue(input(1, GraphValueType::Vec4, "vec4(1.0)"), GraphValueType::Vec4);
        CompiledValue t = ConvertValue(input(2, GraphValueType::Float, "0.5"), GraphValueType::Float);
        result = {"mix(" + a.code + ", " + b.code + ", " + t.code + ")", GraphValueType::Vec4};
    } else if (node->type == "Clamp") {
        CompiledValue value = ConvertValue(input(0, GraphValueType::Float, "0.0"), GraphValueType::Float);
        CompiledValue minValue = ConvertValue(input(1, GraphValueType::Float, "0.0"), GraphValueType::Float);
        CompiledValue maxValue = ConvertValue(input(2, GraphValueType::Float, "1.0"), GraphValueType::Float);
        result = {"clamp(" + value.code + ", " + minValue.code + ", " + maxValue.code + ")", GraphValueType::Float};
    } else if (node->type == "OneMinus") {
        CompiledValue value = input(0, GraphValueType::Float, "0.0");
        result = {"(1.0 - " + value.code + ")", value.type};
    } else if (node->type == "Fresnel") {
        result = {"pow(1.0 - max(dot(normalize(vWorldNormal), vec3(0.0, 0.0, 1.0)), 0.0), 5.0)", GraphValueType::Float};
    } else {
        result = {"0.0", GraphValueType::Float};
    }

    stack.erase(nodeId);
    return result;
}

bool CompileShaderGraphFragment(const fs::path& fragmentPath,
                                const std::vector<ShaderGraphNodeState>& nodes,
                                const std::vector<ShaderGraphLinkState>& links,
                                std::string& error) {
    for (const ShaderGraphLinkState& link : links) {
        const ShaderGraphNodeState* source = FindNode(nodes, PinNodeId(link.startPin));
        const ShaderGraphNodeState* target = FindNode(nodes, PinNodeId(link.endPin));
        if (source == nullptr || target == nullptr) {
            error = "Shader graph contains a link to a missing node.";
            return false;
        }
        if (!IsTypeCompatible(OutputTypeForNode(*source), InputTypeForPin(nodes, link.endPin))) {
            error = "Shader graph contains an invalid link type.";
            return false;
        }
    }

    const auto compileOutput = [&](int slot, GraphValueType target, const std::string& fallback, bool required) -> CompiledValue {
        const ShaderGraphLinkState* link = FindInputLink(links, InputPin(kMaterialOutputNode, slot));
        if (link == nullptr) {
            if (required) error = "Material Output BaseColor is disconnected.";
            return {fallback, target};
        }
        std::unordered_set<int> stack;
        CompiledValue value = CompileNodeExpression(nodes, links, PinNodeId(link->startPin), stack, error);
        return ConvertValue(value, target);
    };

    CompiledValue baseColor = compileOutput(0, GraphValueType::Vec4, "vec4(1.0)", true);
    if (!error.empty()) return false;
    CompiledValue emissive = compileOutput(1, GraphValueType::Vec3, "vec3(0.0)", false);
    if (!error.empty()) return false;
    CompiledValue metallic = compileOutput(2, GraphValueType::Float, "0.0", false);
    if (!error.empty()) return false;
    CompiledValue roughness = compileOutput(3, GraphValueType::Float, "0.5", false);
    if (!error.empty()) return false;
    CompiledValue alpha = compileOutput(4, GraphValueType::Float, "(" + baseColor.code + ").a", false);
    if (!error.empty()) return false;

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
        out << "uniform vec2 uUvTiling;\n";
        out << "uniform vec2 uUvOffset;\n";
        out << "void main() {\n";
        out << "    vec4 graphBase = " << baseColor.code << ";\n";
        out << "    vec3 emissive = " << emissive.code << ";\n";
        out << "    float metallic = clamp(" << metallic.code << ", 0.0, 1.0);\n";
        out << "    float roughness = clamp(" << roughness.code << ", 0.02, 1.0);\n";
        out << "    float alpha = clamp(" << alpha.code << ", 0.0, 1.0);\n";
        out << "    vec3 normalTint = normalize(vWorldNormal) * 0.5 + 0.5;\n";
        out << "    vec4 base = graphBase * uColor;\n";
        out << "    vec3 lit = base.rgb * mix(vec3(0.9), normalTint, 0.12 + metallic * 0.18) + emissive * (1.0 + (1.0 - roughness));\n";
        out << "    FragColor = vec4(lit, base.a * alpha);\n";
        out << "}\n";
        return true;
    } catch (...) {
        error = "Failed while writing generated shader.";
        return false;
    }
}

bool LinkIsValid(const std::vector<ShaderGraphNodeState>& nodes, int startPin, int endPin) {
    const ShaderGraphNodeState* source = FindNode(nodes, PinNodeId(startPin));
    if (source == nullptr || PinNodeId(endPin) == PinNodeId(startPin)) return false;
    if (FindNode(nodes, PinNodeId(endPin)) == nullptr) return false;
    return IsTypeCompatible(OutputTypeForNode(*source), InputTypeForPin(nodes, endPin));
}

std::string NodeValuePreview(const ShaderGraphNodeState& node) {
    if (node.type == "Color") {
        return "RGBA " + FloatText(node.color[0]) + ", " + FloatText(node.color[1]) + ", " + FloatText(node.color[2]) + ", " + FloatText(node.color[3]);
    }
    if (node.type == "Float") return "Value " + FloatText(node.floatValue);
    if (node.type == "Vector2") return "XY " + FloatText(node.vectorValue[0]) + ", " + FloatText(node.vectorValue[1]);
    if (node.type == "Vector3") return "XYZ " + FloatText(node.vectorValue[0]) + ", " + FloatText(node.vectorValue[1]) + ", " + FloatText(node.vectorValue[2]);
    if (node.type == "Vector4") return "XYZW " + FloatText(node.vectorValue[0]) + ", " + FloatText(node.vectorValue[1]) + ", " + FloatText(node.vectorValue[2]) + ", " + FloatText(node.vectorValue[3]);
    if (node.type == "TextureSample") return "Texture: material albedo";
    if (node.type == "UV") return "Uses material tiling/offset";
    return {};
}

void PushNodeTint(const ShaderGraphNodeState& node) {
    if (node.type != "Note") return;
    const ImU32 base = ImGui::ColorConvertFloat4ToU32(ImVec4(node.color[0] * 0.45f, node.color[1] * 0.45f, node.color[2] * 0.45f, 1.0f));
    const ImU32 title = ImGui::ColorConvertFloat4ToU32(ImVec4(node.color[0] * 0.62f, node.color[1] * 0.62f, node.color[2] * 0.62f, 1.0f));
    ImNodes::PushColorStyle(ImNodesCol_NodeBackground, base);
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
}

void PopNodeTint(const ShaderGraphNodeState& node) {
    if (node.type != "Note") return;
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
}

ImVec4 NodeCategoryColor(const ShaderGraphNodeState& node) {
    if (node.type == "Color" || node.type == "Float" || node.type == "Vector2" || node.type == "Vector3" || node.type == "Vector4") {
        return ImVec4(0.22f, 0.54f, 0.28f, 1.0f);
    }
    if (node.type == "TextureSample" || node.type == "UV") {
        return ImVec4(0.58f, 0.26f, 0.74f, 1.0f);
    }
    if (node.type == "Add" || node.type == "Multiply" || node.type == "Lerp" || node.type == "Clamp" || node.type == "OneMinus") {
        return ImVec4(0.20f, 0.46f, 0.82f, 1.0f);
    }
    if (node.type == "Fresnel") {
        return ImVec4(0.82f, 0.30f, 0.24f, 1.0f);
    }
    if (node.type == "MaterialOutput") {
        return ImVec4(0.78f, 0.56f, 0.22f, 1.0f);
    }
    if (node.type == "Note") {
        return ImVec4(node.color[0] * 0.70f, node.color[1] * 0.70f, node.color[2] * 0.70f, 1.0f);
    }
    return ImVec4(0.32f, 0.34f, 0.38f, 1.0f);
}

void PushNodeCategoryTint(const ShaderGraphNodeState& node) {
    const ImVec4 titleColor = NodeCategoryColor(node);
    const ImVec4 bodyColor(titleColor.x * 0.24f + 0.08f,
                           titleColor.y * 0.24f + 0.08f,
                           titleColor.z * 0.24f + 0.08f,
                           1.0f);
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, ImGui::ColorConvertFloat4ToU32(titleColor));
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, ImGui::ColorConvertFloat4ToU32(ImVec4((std::min)(1.0f, titleColor.x + 0.10f), (std::min)(1.0f, titleColor.y + 0.10f), (std::min)(1.0f, titleColor.z + 0.10f), 1.0f)));
    ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, ImGui::ColorConvertFloat4ToU32(ImVec4((std::min)(1.0f, titleColor.x + 0.18f), (std::min)(1.0f, titleColor.y + 0.18f), (std::min)(1.0f, titleColor.z + 0.18f), 1.0f)));
    ImNodes::PushColorStyle(ImNodesCol_NodeBackground, ImGui::ColorConvertFloat4ToU32(bodyColor));
}

void PopNodeCategoryTint() {
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
}

void RenderNodeInlinePreview(const ShaderGraphNodeState& node) {
    if (node.type == "Color") {
        ImGui::ColorButton("##preview", ImVec4(node.color[0], node.color[1], node.color[2], node.color[3]), ImGuiColorEditFlags_NoTooltip, ImVec2(34.0f, 18.0f));
        ImGui::SameLine();
        ImGui::TextDisabled("Color");
    } else if (node.type == "Float") {
        const float filled = (std::max)(0.0f, (std::min)(1.0f, node.floatValue));
        ImGui::ProgressBar(filled, ImVec2(78.0f, 0.0f), FloatText(node.floatValue).c_str());
    } else if (node.type == "Vector2" || node.type == "Vector3" || node.type == "Vector4") {
        ImGui::TextDisabled("%s", NodeValuePreview(node).c_str());
    } else if (node.type == "TextureSample") {
        ImGui::Button("Tex", ImVec2(44.0f, 26.0f));
        ImGui::SameLine();
        ImGui::TextDisabled("Sample");
    }
}

bool RenderGraphNode(ShaderGraphNodeState& node, const std::vector<ShaderGraphNodeState>& nodes) {
    bool changed = false;
    PushNodeCategoryTint(node);
    ImNodes::BeginNode(node.id);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(node.title.empty() ? NodeTypeLabel(node.type) : node.title.c_str());
    ImNodes::EndNodeTitleBar();

    if (node.type == "MaterialOutput") {
        const char* labels[] = {"BaseColor", "Emissive", "Metallic", "Roughness", "Alpha", "Normal"};
        for (int slot = 0; slot < 6; ++slot) {
            BeginColoredInputAttribute(nodes, node.id, slot);
            ImGui::TextUnformatted(labels[slot]);
            EndColoredInputAttribute();
        }
    } else if (node.type == "Note") {
        char note[384]{};
        std::snprintf(note, sizeof(note), "%s", node.noteText.c_str());
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::InputTextMultiline("##noteText", note, sizeof(note), ImVec2(190.0f, 82.0f))) {
            node.noteText = note;
            changed = true;
        }
        ImGui::SetNextItemWidth(190.0f);
        changed |= ImGui::ColorEdit4("##noteColor", node.color, ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs);
    } else {
        RenderNodeInlinePreview(node);
        const bool editableConstant =
            node.type == "Color" || node.type == "Float" || node.type == "Vector2" ||
            node.type == "Vector3" || node.type == "Vector4";
        if (editableConstant) {
            ImGui::SetNextItemWidth(170.0f);
            if (node.type == "Color") {
                changed |= ImGui::ColorEdit4("##nodeColor", node.color, ImGuiColorEditFlags_NoLabel);
            } else if (node.type == "Float") {
                changed |= ImGui::DragFloat("##nodeFloat", &node.floatValue, 0.01f);
            } else if (node.type == "Vector2") {
                changed |= ImGui::DragFloat2("##nodeVector2", node.vectorValue, 0.01f);
            } else if (node.type == "Vector3") {
                changed |= ImGui::DragFloat3("##nodeVector3", node.vectorValue, 0.01f);
            } else if (node.type == "Vector4") {
                changed |= ImGui::DragFloat4("##nodeVector4", node.vectorValue, 0.01f);
            }
        } else {
            const std::string preview = NodeValuePreview(node);
            if (!preview.empty()) {
                ImGui::TextDisabled("%s", preview.c_str());
            }
        }
        if (node.type == "TextureSample") {
            BeginColoredInputAttribute(nodes, node.id, 0);
            ImGui::TextUnformatted("UV");
            EndColoredInputAttribute();
        } else if (node.type == "Add" || node.type == "Multiply") {
            BeginColoredInputAttribute(nodes, node.id, 0); ImGui::TextUnformatted("A"); EndColoredInputAttribute();
            BeginColoredInputAttribute(nodes, node.id, 1); ImGui::TextUnformatted("B"); EndColoredInputAttribute();
        } else if (node.type == "Lerp") {
            BeginColoredInputAttribute(nodes, node.id, 0); ImGui::TextUnformatted("A"); EndColoredInputAttribute();
            BeginColoredInputAttribute(nodes, node.id, 1); ImGui::TextUnformatted("B"); EndColoredInputAttribute();
            BeginColoredInputAttribute(nodes, node.id, 2); ImGui::TextUnformatted("T"); EndColoredInputAttribute();
        } else if (node.type == "Clamp") {
            BeginColoredInputAttribute(nodes, node.id, 0); ImGui::TextUnformatted("Value"); EndColoredInputAttribute();
            BeginColoredInputAttribute(nodes, node.id, 1); ImGui::TextUnformatted("Min"); EndColoredInputAttribute();
            BeginColoredInputAttribute(nodes, node.id, 2); ImGui::TextUnformatted("Max"); EndColoredInputAttribute();
        } else if (node.type == "OneMinus") {
            BeginColoredInputAttribute(nodes, node.id, 0); ImGui::TextUnformatted("In"); EndColoredInputAttribute();
        }
        BeginColoredOutputAttribute(node);
        ImGui::TextUnformatted("Out");
        EndColoredOutputAttribute();
    }
    ImNodes::EndNode();
    PopNodeCategoryTint();
    return changed;
}

bool RenderNodeInspector(ShaderGraphNodeState* node, const std::string& status) {
    ImGui::BeginChild("ShaderGraphProperties", ImVec2(260.0f, 0.0f), true);
    ImGui::TextUnformatted("Properties");
    ImGui::Separator();
    if (node == nullptr) {
        ImGui::TextWrapped("%s", status.empty() ? "Select a node to edit its properties." : status.c_str());
        ImGui::EndChild();
        return false;
    }
    bool changed = false;
    ImGui::Text("Node: %s", NodeTypeLabel(node->type));
    char title[128]{};
    std::snprintf(title, sizeof(title), "%s", node->title.c_str());
    if (ImGui::InputText("Title", title, sizeof(title))) { node->title = title; changed = true; }
    if (node->type == "Color") changed |= ImGui::ColorEdit4("Color", node->color);
    else if (node->type == "Float") changed |= ImGui::DragFloat("Value", &node->floatValue, 0.01f);
    else if (node->type == "Vector2") changed |= ImGui::DragFloat2("Vector", node->vectorValue, 0.01f);
    else if (node->type == "Vector3") changed |= ImGui::DragFloat3("Vector", node->vectorValue, 0.01f);
    else if (node->type == "Vector4") changed |= ImGui::DragFloat4("Vector", node->vectorValue, 0.01f);
    else if (node->type == "Note") {
        changed |= ImGui::ColorEdit4("Color", node->color);
        char note[512]{};
        std::snprintf(note, sizeof(note), "%s", node->noteText.c_str());
        if (ImGui::InputTextMultiline("Text", note, sizeof(note), ImVec2(-1.0f, 120.0f))) {
            node->noteText = note;
            changed = true;
        }
    } else if (node->type == "MaterialOutput") ImGui::TextWrapped("%s", status.empty() ? "Connect BaseColor before saving a generated shader." : status.c_str());
    else ImGui::TextDisabled("No editable properties.");
    ImGui::EndChild();
    return changed;
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

    std::vector<ShaderGraphNodeState> nodes;
    std::vector<ShaderGraphLinkState> links;
    int nextNodeId = 101;
    int nextLinkId = 1;
    EnsureDefaultGraphData(nodes, links, nextNodeId, nextLinkId);
    if (!SaveShaderGraphJson(targetPath, graphId, nodes, links)) {
        if (console_) console_->AddError("Failed to create Shader Graph: " + graphId);
        return false;
    }
    std::string error;
    CompileShaderGraphFragment(ShaderGraphGeneratedPath(ToProjectAssetPath(targetPath, assetsRoot)), nodes, links, error);
    RefreshProjectFiles();
    if (outGraphPath) *outGraphPath = ToProjectAssetPath(targetPath, assetsRoot);
    if (console_) console_->AddLog("Created Shader Graph: " + graphId);
    return true;
}

void SceneEditor::OpenShaderGraphEditor(const std::string& graphPath) {
    if (graphPath.empty()) return;
    inspectedShaderGraphPath_ = NormalizeSlashes(graphPath);
    selectedProjectFile_ = inspectedShaderGraphPath_;
    selectedProjectDirectory_ = ParentProjectDirectory(inspectedShaderGraphPath_);
    shaderGraphLoaded_ = false;
    shaderGraphDirty_ = false;
    shaderGraphUndoStack_.clear();
    shaderGraphRedoStack_.clear();
    shaderGraphDragUndoArmed_ = false;
    showShaderGraphEditor_ = true;
}

void SceneEditor::PushShaderGraphUndoState() {
    shaderGraphUndoStack_.push_back(CaptureShaderGraphState(shaderGraphNameBuffer_,
                                                            shaderGraphNodes_,
                                                            shaderGraphLinks_,
                                                            shaderGraphNextNodeId_,
                                                            shaderGraphNextLinkId_,
                                                            shaderGraphSelectedNodeId_));
    if (shaderGraphUndoStack_.size() > kMaxShaderGraphUndoStates) {
        shaderGraphUndoStack_.erase(shaderGraphUndoStack_.begin());
    }
    shaderGraphRedoStack_.clear();
}

void SceneEditor::UndoShaderGraph() {
    if (shaderGraphUndoStack_.empty()) return;
    shaderGraphRedoStack_.push_back(CaptureShaderGraphState(shaderGraphNameBuffer_,
                                                            shaderGraphNodes_,
                                                            shaderGraphLinks_,
                                                            shaderGraphNextNodeId_,
                                                            shaderGraphNextLinkId_,
                                                            shaderGraphSelectedNodeId_));
    const ShaderGraphHistoryState state = shaderGraphUndoStack_.back();
    shaderGraphUndoStack_.pop_back();
    shaderGraphNodes_ = state.nodes;
    shaderGraphLinks_ = state.links;
    shaderGraphNextNodeId_ = state.nextNodeId;
    shaderGraphNextLinkId_ = state.nextLinkId;
    shaderGraphSelectedNodeId_ = state.selectedNodeId;
    std::snprintf(shaderGraphNameBuffer_, sizeof(shaderGraphNameBuffer_), "%s", state.name.c_str());
    for (const auto& node : shaderGraphNodes_) {
        ImNodes::SetNodeGridSpacePos(node.id, ImVec2(node.position.x, node.position.y));
    }
    shaderGraphDirty_ = true;
}

void SceneEditor::RedoShaderGraph() {
    if (shaderGraphRedoStack_.empty()) return;
    shaderGraphUndoStack_.push_back(CaptureShaderGraphState(shaderGraphNameBuffer_,
                                                            shaderGraphNodes_,
                                                            shaderGraphLinks_,
                                                            shaderGraphNextNodeId_,
                                                            shaderGraphNextLinkId_,
                                                            shaderGraphSelectedNodeId_));
    const ShaderGraphHistoryState state = shaderGraphRedoStack_.back();
    shaderGraphRedoStack_.pop_back();
    shaderGraphNodes_ = state.nodes;
    shaderGraphLinks_ = state.links;
    shaderGraphNextNodeId_ = state.nextNodeId;
    shaderGraphNextLinkId_ = state.nextLinkId;
    shaderGraphSelectedNodeId_ = state.selectedNodeId;
    std::snprintf(shaderGraphNameBuffer_, sizeof(shaderGraphNameBuffer_), "%s", state.name.c_str());
    for (const auto& node : shaderGraphNodes_) {
        ImNodes::SetNodeGridSpacePos(node.id, ImVec2(node.position.x, node.position.y));
    }
    shaderGraphDirty_ = true;
}

bool SceneEditor::SaveShaderGraphAsset() {
    if (inspectedShaderGraphPath_.empty()) return false;
    const fs::path graphPath = ProjectAssetPathToAbsolute(inspectedShaderGraphPath_);
    ApplyLegacyOutputFields(shaderGraphLinks_, shaderGraphBaseColorNode_, shaderGraphEmissiveNode_, shaderGraphMetallicNode_, shaderGraphRoughnessNode_);
    const bool saved = SaveShaderGraphJson(graphPath, shaderGraphNameBuffer_, shaderGraphNodes_, shaderGraphLinks_);
    std::string error;
    const bool generated = saved && CompileShaderGraphFragment(ShaderGraphGeneratedPath(inspectedShaderGraphPath_), shaderGraphNodes_, shaderGraphLinks_, error);
    shaderGraphStatus_ = error;
    if (saved) {
        shaderGraphDirty_ = false;
        RefreshProjectFiles();
        if (console_) console_->AddLog(generated ? "Saved shader graph: " + inspectedShaderGraphPath_ : "Saved shader graph with validation error: " + error);
        return true;
    }
    if (console_) console_->AddError("Failed to save shader graph: " + inspectedShaderGraphPath_);
    return false;
}

void SceneEditor::RenderShaderGraphEditorWindow() {
    if (!showShaderGraphEditor_ || inspectedShaderGraphPath_.empty()) return;

    if (ImNodes::GetCurrentContext() == nullptr) {
        ImNodes::CreateContext();
        ImNodes::StyleColorsDark();
    }

    if (!shaderGraphLoaded_) {
        std::string name = fs::path(inspectedShaderGraphPath_).filename().string();
        const std::string suffix = ".shadergraph.json";
        if (EndsWith(ToLowerCopy(name), suffix)) name.resize(name.size() - suffix.size());
        shaderGraphNodes_.clear();
        shaderGraphLinks_.clear();
        LoadShaderGraphJson(ProjectAssetPathToAbsolute(inspectedShaderGraphPath_), name, shaderGraphNodes_, shaderGraphLinks_, shaderGraphNextNodeId_, shaderGraphNextLinkId_);
        EnsureDefaultGraphData(shaderGraphNodes_, shaderGraphLinks_, shaderGraphNextNodeId_, shaderGraphNextLinkId_);
        std::snprintf(shaderGraphNameBuffer_, sizeof(shaderGraphNameBuffer_), "%s", name.c_str());
        shaderGraphLoaded_ = true;
        shaderGraphSelectedNodeId_ = 0;
        for (const auto& node : shaderGraphNodes_) {
            ImNodes::SetNodeGridSpacePos(node.id, ImVec2(node.position.x, node.position.y));
        }
        ApplyLegacyOutputFields(shaderGraphLinks_, shaderGraphBaseColorNode_, shaderGraphEmissiveNode_, shaderGraphMetallicNode_, shaderGraphRoughnessNode_);
    }

    if (!ImGui::Begin("Shader Graph", &showShaderGraphEditor_)) {
        ImGui::End();
        return;
    }
    shaderGraphEditorFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    shaderGraphEditorHovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    auto createNodeAt = [&](const char* type, const ImVec2& screenPos) {
        PushShaderGraphUndoState();
        const int nodeId = shaderGraphNextNodeId_++;
        shaderGraphNodes_.push_back(MakeNode(nodeId, type, {0.0f, 0.0f}));
        ImNodes::SetNodeScreenSpacePos(nodeId, screenPos);
        const ImVec2 gridPos = ImNodes::GetNodeGridSpacePos(nodeId);
        shaderGraphNodes_.back().position = {gridPos.x, gridPos.y};
        shaderGraphSelectedNodeId_ = nodeId;
        shaderGraphDirty_ = true;
    };
    auto frameNode = [&](int nodeId) {
        const ShaderGraphNodeState* node = FindNode(shaderGraphNodes_, nodeId);
        if (node == nullptr) return;
        const ImVec2 canvasSize((std::max)(1.0f, shaderGraphCanvasSize_.x),
                                (std::max)(1.0f, shaderGraphCanvasSize_.y));
        ImNodes::EditorContextResetPanning(ImVec2(canvasSize.x * 0.5f - node->position.x,
                                                  canvasSize.y * 0.38f - node->position.y));
    };

    ImGui::TextDisabled("%s", inspectedShaderGraphPath_.c_str());
    const std::string shaderId = ShaderGraphIdFromPath(inspectedShaderGraphPath_);
    ImGui::Text("Shader ID: %s%s", shaderId.c_str(), shaderGraphDirty_ ? " *" : "");
    ImGui::Separator();
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::InputText("Name", shaderGraphNameBuffer_, sizeof(shaderGraphNameBuffer_))) {
        if (!ImGui::IsItemActivated()) PushShaderGraphUndoState();
        shaderGraphDirty_ = true;
    }
    if (ImGui::Button("Save Graph")) SaveShaderGraphAsset();
    ImGui::SameLine();
    if (ImGui::Button("Create Material From Graph")) {
        std::string materialId;
        if (CreateMaterialAsset(shaderGraphNameBuffer_, &materialId, shaderId)) {
            selectedProjectFile_ = selectedProjectDirectory_ + "/" + materialId + ".mat.json";
            OpenMaterialEditor(materialId);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame All")) frameNode(kMaterialOutputNode);
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        PushShaderGraphUndoState();
        int index = 0;
        for (auto& node : shaderGraphNodes_) {
            if (node.id == kMaterialOutputNode) {
                node.position = {630.0f, 344.0f};
            } else {
                node.position = {32.0f + static_cast<float>(index % 3) * 298.0f,
                                 36.0f + static_cast<float>(index / 3) * 154.0f};
                ++index;
            }
            ImNodes::SetNodeGridSpacePos(node.id, ImVec2(node.position.x, node.position.y));
        }
        shaderGraphDirty_ = true;
    }

    ImGui::BeginChild("ShaderGraphMain", ImVec2(0.0f, 0.0f), false);
    const float propertiesWidth = 270.0f;
    ImGui::BeginChild("ShaderGraphCanvas", ImVec2(-propertiesWidth, 0.0f), true);
    shaderGraphCanvasSize_ = {ImGui::GetWindowSize().x, ImGui::GetWindowSize().y};
    const bool canvasHoveredBeforeEditor = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_ChildWindows);
    const ShaderGraphHistoryState beforeInlineNodeEdit = CaptureShaderGraphState(shaderGraphNameBuffer_,
                                                                                 shaderGraphNodes_,
                                                                                 shaderGraphLinks_,
                                                                                 shaderGraphNextNodeId_,
                                                                                 shaderGraphNextLinkId_,
                                                                                 shaderGraphSelectedNodeId_);
    bool inlineNodeChanged = false;
    ImNodes::BeginNodeEditor();
    for (auto& node : shaderGraphNodes_) {
        inlineNodeChanged |= RenderGraphNode(node, shaderGraphNodes_);
    }
    for (const auto& link : shaderGraphLinks_) {
        const ImU32 linkColor = LinkColorForPin(shaderGraphNodes_, link.startPin);
        const bool linkSelected = ImNodes::IsLinkSelected(link.id);
        ImNodes::PushColorStyle(ImNodesCol_Link, linkColor);
        ImNodes::PushColorStyle(ImNodesCol_LinkHovered, BrightenColor(linkColor, 0.55f));
        ImNodes::PushColorStyle(ImNodesCol_LinkSelected, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.92f, 0.24f, 1.0f)));
        if (linkSelected) {
            ImNodes::PushStyleVar(ImNodesStyleVar_LinkThickness, 5.0f);
        }
        ImNodes::Link(link.id, link.startPin, link.endPin);
        if (linkSelected) {
            ImNodes::PopStyleVar();
        }
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }
    ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();
    if (inlineNodeChanged) {
        shaderGraphUndoStack_.push_back(beforeInlineNodeEdit);
        if (shaderGraphUndoStack_.size() > kMaxShaderGraphUndoStates) shaderGraphUndoStack_.erase(shaderGraphUndoStack_.begin());
        shaderGraphRedoStack_.clear();
        shaderGraphDirty_ = true;
    }

    int hoveredNodeId = 0;
    const bool hasHoveredNode = ImNodes::IsNodeHovered(&hoveredNodeId);
    if (canvasHoveredBeforeEditor && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const ImVec2 mousePos = ImGui::GetMousePos();
        shaderGraphContextScreenPos_ = {mousePos.x, mousePos.y};
        if (hasHoveredNode) {
            shaderGraphSelectedNodeId_ = hoveredNodeId;
            ImGui::OpenPopup("ShaderGraphNodeContext");
        } else {
            ImGui::OpenPopup("ShaderGraphCanvasContext");
        }
    }
    const ImVec2 contextPos(shaderGraphContextScreenPos_.x, shaderGraphContextScreenPos_.y);
    auto renderCreateNodeMenu = [&]() {
        const char* types[] = {"Color", "Float", "Vector2", "Vector3", "Vector4", "TextureSample", "UV", "Add", "Multiply", "Lerp", "Clamp", "OneMinus", "Fresnel", "Note"};
        for (const char* type : types) {
            if (ImGui::MenuItem(NodeTypeLabel(type))) {
                createNodeAt(type, contextPos);
            }
        }
    };
    if (ImGui::BeginPopup("ShaderGraphCanvasContext")) {
        if (ImGui::BeginMenu("Create Node")) {
            renderCreateNodeMenu();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Add Note")) createNodeAt("Note", contextPos);
        ImGui::Separator();
        if (ImGui::MenuItem("Save Graph", "Ctrl+S")) SaveShaderGraphAsset();
        if (ImGui::MenuItem("Frame All")) frameNode(kMaterialOutputNode);
        if (ImGui::MenuItem("Reset View")) {
            PushShaderGraphUndoState();
            int index = 0;
            for (auto& node : shaderGraphNodes_) {
                node.position = node.id == kMaterialOutputNode
                    ? glm::vec2{630.0f, 344.0f}
                    : glm::vec2{32.0f + static_cast<float>(index % 3) * 298.0f, 36.0f + static_cast<float>(index / 3) * 154.0f};
                if (node.id != kMaterialOutputNode) ++index;
                ImNodes::SetNodeGridSpacePos(node.id, ImVec2(node.position.x, node.position.y));
            }
            shaderGraphDirty_ = true;
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("ShaderGraphNodeContext")) {
        ShaderGraphNodeState* node = FindNode(shaderGraphNodes_, shaderGraphSelectedNodeId_);
        ImGui::TextDisabled("%s", node != nullptr ? (node->title.empty() ? NodeTypeLabel(node->type) : node->title.c_str()) : "Node");
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, node != nullptr && node->id != kMaterialOutputNode)) {
            PushShaderGraphUndoState();
            ShaderGraphNodeState copy = *node;
            copy.id = shaderGraphNextNodeId_++;
            copy.position += glm::vec2{30.0f, 30.0f};
            copy.title += " Copy";
            shaderGraphNodes_.push_back(copy);
            ImNodes::SetNodeGridSpacePos(copy.id, ImVec2(copy.position.x, copy.position.y));
            shaderGraphSelectedNodeId_ = copy.id;
            shaderGraphDirty_ = true;
        }
        if (ImGui::MenuItem("Frame Node", nullptr, false, node != nullptr)) {
            frameNode(shaderGraphSelectedNodeId_);
        }
        if (ImGui::MenuItem("Delete", "Del", false, node != nullptr && node->id != kMaterialOutputNode)) {
            PushShaderGraphUndoState();
            const int nodeId = shaderGraphSelectedNodeId_;
            shaderGraphNodes_.erase(std::remove_if(shaderGraphNodes_.begin(), shaderGraphNodes_.end(), [&](const ShaderGraphNodeState& graphNode) { return graphNode.id == nodeId; }), shaderGraphNodes_.end());
            shaderGraphLinks_.erase(std::remove_if(shaderGraphLinks_.begin(), shaderGraphLinks_.end(), [&](const ShaderGraphLinkState& link) {
                return PinNodeId(link.startPin) == nodeId || PinNodeId(link.endPin) == nodeId;
            }), shaderGraphLinks_.end());
            shaderGraphSelectedNodeId_ = 0;
            shaderGraphDirty_ = true;
        }
        ImGui::EndPopup();
    }

    int startPin = 0;
    int endPin = 0;
    if (ImNodes::IsLinkCreated(&startPin, &endPin)) {
        if (PinNodeId(startPin) == kMaterialOutputNode) std::swap(startPin, endPin);
        if (LinkIsValid(shaderGraphNodes_, startPin, endPin)) {
            PushShaderGraphUndoState();
            shaderGraphLinks_.erase(std::remove_if(shaderGraphLinks_.begin(), shaderGraphLinks_.end(), [&](const ShaderGraphLinkState& link) {
                return link.endPin == endPin;
            }), shaderGraphLinks_.end());
            shaderGraphLinks_.push_back({shaderGraphNextLinkId_++, startPin, endPin});
            shaderGraphDirty_ = true;
        } else {
            shaderGraphStatus_ = "Invalid link type.";
        }
    }
    int destroyedLink = 0;
    if (ImNodes::IsLinkDestroyed(&destroyedLink)) {
        PushShaderGraphUndoState();
        shaderGraphLinks_.erase(std::remove_if(shaderGraphLinks_.begin(), shaderGraphLinks_.end(), [&](const ShaderGraphLinkState& link) {
            return link.id == destroyedLink;
        }), shaderGraphLinks_.end());
        shaderGraphDirty_ = true;
    }
    int selectedNodeCount = ImNodes::NumSelectedNodes();
    if (selectedNodeCount > 0) {
        std::vector<int> selected(static_cast<std::size_t>(selectedNodeCount));
        ImNodes::GetSelectedNodes(selected.data());
        shaderGraphSelectedNodeId_ = selected.front();
    }
    int selectedLinkCount = ImNodes::NumSelectedLinks();
    const bool deletePressed = ImGui::IsKeyPressed(ImGuiKey_Delete);
    if (deletePressed && selectedLinkCount > 0) {
        PushShaderGraphUndoState();
        std::vector<int> selectedLinks(static_cast<std::size_t>(selectedLinkCount));
        ImNodes::GetSelectedLinks(selectedLinks.data());
        for (int linkId : selectedLinks) {
            shaderGraphLinks_.erase(std::remove_if(shaderGraphLinks_.begin(), shaderGraphLinks_.end(), [&](const ShaderGraphLinkState& link) {
                return link.id == linkId;
            }), shaderGraphLinks_.end());
        }
        shaderGraphDirty_ = true;
    } else if (deletePressed && shaderGraphSelectedNodeId_ != 0 && shaderGraphSelectedNodeId_ != kMaterialOutputNode) {
        PushShaderGraphUndoState();
        const int nodeId = shaderGraphSelectedNodeId_;
        shaderGraphNodes_.erase(std::remove_if(shaderGraphNodes_.begin(), shaderGraphNodes_.end(), [&](const ShaderGraphNodeState& node) { return node.id == nodeId; }), shaderGraphNodes_.end());
        shaderGraphLinks_.erase(std::remove_if(shaderGraphLinks_.begin(), shaderGraphLinks_.end(), [&](const ShaderGraphLinkState& link) {
            return PinNodeId(link.startPin) == nodeId || PinNodeId(link.endPin) == nodeId;
        }), shaderGraphLinks_.end());
        shaderGraphSelectedNodeId_ = 0;
        shaderGraphDirty_ = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_D) && ImGui::GetIO().KeyCtrl && shaderGraphSelectedNodeId_ != 0 && shaderGraphSelectedNodeId_ != kMaterialOutputNode) {
        if (const ShaderGraphNodeState* source = FindNode(shaderGraphNodes_, shaderGraphSelectedNodeId_)) {
            PushShaderGraphUndoState();
            ShaderGraphNodeState copy = *source;
            copy.id = shaderGraphNextNodeId_++;
            copy.position += glm::vec2{30.0f, 30.0f};
            copy.title += " Copy";
            shaderGraphNodes_.push_back(copy);
            ImNodes::SetNodeGridSpacePos(copy.id, ImVec2(copy.position.x, copy.position.y));
            shaderGraphDirty_ = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        shaderGraphDragUndoArmed_ = false;
    }
    for (auto& node : shaderGraphNodes_) {
        const ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.id);
        if (node.position.x != pos.x || node.position.y != pos.y) {
            if (!shaderGraphDragUndoArmed_) {
                PushShaderGraphUndoState();
                shaderGraphDragUndoArmed_ = true;
            }
            node.position = {pos.x, pos.y};
            shaderGraphDirty_ = true;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    const ShaderGraphHistoryState beforeInspectorState = CaptureShaderGraphState(shaderGraphNameBuffer_,
                                                                                 shaderGraphNodes_,
                                                                                 shaderGraphLinks_,
                                                                                 shaderGraphNextNodeId_,
                                                                                 shaderGraphNextLinkId_,
                                                                                 shaderGraphSelectedNodeId_);
    if (RenderNodeInspector(FindNode(shaderGraphNodes_, shaderGraphSelectedNodeId_), shaderGraphStatus_)) {
        shaderGraphUndoStack_.push_back(beforeInspectorState);
        if (shaderGraphUndoStack_.size() > kMaxShaderGraphUndoStates) shaderGraphUndoStack_.erase(shaderGraphUndoStack_.begin());
        shaderGraphRedoStack_.clear();
        shaderGraphDirty_ = true;
    }
    ImGui::EndChild();

    ApplyLegacyOutputFields(shaderGraphLinks_, shaderGraphBaseColorNode_, shaderGraphEmissiveNode_, shaderGraphMetallicNode_, shaderGraphRoughnessNode_);
    ImGui::End();
}

} // namespace raceman

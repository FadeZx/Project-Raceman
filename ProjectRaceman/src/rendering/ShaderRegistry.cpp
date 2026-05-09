#include "ShaderRegistry.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace raceman {

namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string SanitizeId(std::string value) {
    for (char& ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ':') {
            ch = static_cast<char>(std::tolower(uch));
        } else {
            ch = '_';
        }
    }
    return value.empty() ? std::string("shadergraph") : value;
}

std::string NormalizeSlashes(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

} // namespace

const std::vector<ShaderDefinition>& ShaderRegistry::BuiltInShaders() {
    static const std::vector<ShaderDefinition> shaders = {
        {"pbr", "PBR / Lit", "Default", "src/shaders/default/default.vs", "src/shaders/default/pbr.fs", true, true, true, true, true, false},
        {"unlit", "Unlit", "Default", "src/shaders/default/default.vs", "src/shaders/default/unlit.fs", false, false, false, false, true, false},
        {"transparent", "Transparent", "Default", "src/shaders/default/default.vs", "src/shaders/default/transparent.fs", true, false, true, true, true, true},
        {"emissive", "Emissive", "Default", "src/shaders/default/default.vs", "src/shaders/default/emissive.fs", false, false, false, true, true, false},
        {"normal_debug", "Normal Debug", "Debug", "src/shaders/default/default.vs", "src/shaders/default/normal_debug.fs", false, false, false, false, false, false},
        {"vertex_color", "Vertex Color", "Default", "src/shaders/default/default.vs", "src/shaders/default/vertex_color.fs", true, false, true, true, true, false},
    };
    return shaders;
}

const ShaderDefinition& ShaderRegistry::Fallback() {
    return BuiltInShaders().front();
}

bool ShaderRegistry::IsGraphShaderId(const std::string& id) {
    return Lower(id).rfind("graph:", 0) == 0;
}

bool ShaderRegistry::IsKnownShader(const std::string& id) {
    const std::string normalized = NormalizeShaderId(id);
    if (IsGraphShaderId(normalized)) {
        return !GraphFragmentPathForShaderId(normalized).empty();
    }
    const auto& shaders = BuiltInShaders();
    return std::any_of(shaders.begin(), shaders.end(), [&](const ShaderDefinition& shader) {
        return shader.id == normalized;
    });
}

const ShaderDefinition& ShaderRegistry::Resolve(const std::string& id) {
    const std::string normalized = NormalizeShaderId(id);
    const auto& shaders = BuiltInShaders();
    const auto it = std::find_if(shaders.begin(), shaders.end(), [&](const ShaderDefinition& shader) {
        return shader.id == normalized;
    });
    return it == shaders.end() ? Fallback() : *it;
}

std::string ShaderRegistry::NormalizeShaderId(const std::string& id) {
    const std::string value = Lower(id);
    if (value.empty()) {
        return "pbr";
    }
    if (value == "lit" || value == "default" || value == "simple") {
        return "pbr";
    }
    if (value == "normaldebug" || value == "normal-debug") {
        return "normal_debug";
    }
    if (value == "vertexcolor" || value == "vertex-color") {
        return "vertex_color";
    }
    return value;
}

std::string ShaderRegistry::MakeGraphShaderId(const std::string& graphAssetPath) {
    fs::path path(NormalizeSlashes(graphAssetPath));
    std::string filename = path.filename().string();
    const std::string suffix = ".shadergraph.json";
    const std::string lower = Lower(filename);
    if (lower.size() >= suffix.size() && lower.substr(lower.size() - suffix.size()) == suffix) {
        filename.resize(filename.size() - suffix.size());
    } else {
        filename = path.stem().string();
    }
    return "graph:" + SanitizeId(filename);
}

std::string ShaderRegistry::GraphFragmentPathForShaderId(const std::string& id) {
    if (!IsGraphShaderId(id)) {
        return {};
    }
    std::string name = id.substr(std::string("graph:").size());
    if (name.empty()) {
        return {};
    }
    return "Project/assets/generated-shaders/" + SanitizeId(name) + ".fs";
}

Material ShaderRegistry::MakeDefaultMaterial(const std::string& id, const std::string& name) {
    Material material;
    material.name = name.empty() ? "NewMaterial" : name;
    material.shader = NormalizeShaderId(id);
    if (!IsKnownShader(material.shader) && !IsGraphShaderId(material.shader)) {
        material.shader = "pbr";
    }
    material.albedoColor[0] = 1.0f;
    material.albedoColor[1] = 1.0f;
    material.albedoColor[2] = 1.0f;
    material.albedoColor[3] = material.shader == "transparent" ? 0.5f : 1.0f;
    material.metallic = 0.0f;
    material.roughness = material.shader == "pbr" ? 0.5f : 1.0f;
    if (material.shader == "emissive") {
        material.emissiveColor[0] = 1.0f;
        material.emissiveColor[1] = 1.0f;
        material.emissiveColor[2] = 1.0f;
    }
    return material;
}

} // namespace raceman

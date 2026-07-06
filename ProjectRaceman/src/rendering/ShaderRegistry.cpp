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

ShaderDefinition::Property MakeFloatProperty(const std::string& id,
                                             const std::string& label,
                                             const std::string& uniform,
                                             float defaultValue,
                                             float minValue,
                                             float maxValue) {
    ShaderDefinition::Property property;
    property.id = id;
    property.label = label;
    property.uniformName = uniform;
    property.type = MaterialPropertyType::Float;
    property.minValue = minValue;
    property.maxValue = maxValue;
    property.defaultValues[0] = defaultValue;
    return property;
}

ShaderDefinition::Property MakeColor3Property(const std::string& id,
                                               const std::string& label,
                                               const std::string& uniform,
                                               float r,
                                               float g,
                                               float b) {
    ShaderDefinition::Property property;
    property.id = id;
    property.label = label;
    property.uniformName = uniform;
    property.type = MaterialPropertyType::Vec3;
    property.color = true;
    property.defaultValues[0] = r;
    property.defaultValues[1] = g;
    property.defaultValues[2] = b;
    return property;
}

ShaderDefinition::Property MakeColor4Property(const std::string& id,
                                               const std::string& label,
                                               const std::string& uniform,
                                               float r,
                                               float g,
                                               float b,
                                               float a) {
    ShaderDefinition::Property property;
    property.id = id;
    property.label = label;
    property.uniformName = uniform;
    property.type = MaterialPropertyType::Vec4;
    property.color = true;
    property.defaultValues[0] = r;
    property.defaultValues[1] = g;
    property.defaultValues[2] = b;
    property.defaultValues[3] = a;
    return property;
}

ShaderDefinition::Property MakeVec2Property(const std::string& id,
                                             const std::string& label,
                                             const std::string& uniform,
                                             float x,
                                             float y,
                                             float minValue,
                                             float maxValue) {
    ShaderDefinition::Property property;
    property.id = id;
    property.label = label;
    property.uniformName = uniform;
    property.type = MaterialPropertyType::Vec2;
    property.minValue = minValue;
    property.maxValue = maxValue;
    property.defaultValues[0] = x;
    property.defaultValues[1] = y;
    return property;
}

ShaderDefinition::Property MakeTextureProperty(const std::string& id,
                                                const std::string& label,
                                                const std::string& uniform,
                                                const std::string& useUniform) {
    ShaderDefinition::Property property;
    property.id = id;
    property.label = label;
    property.uniformName = uniform;
    property.type = MaterialPropertyType::Texture2D;
    property.textureUseUniform = useUniform;
    return property;
}

std::vector<ShaderDefinition::Property> PbrProperties() {
    return {
        MakeColor4Property("albedoColor", "Albedo Color", "uColor", 1.0f, 1.0f, 1.0f, 1.0f),
        MakeFloatProperty("metallic", "Metallic", "uMetallic", 0.0f, 0.0f, 1.0f),
        MakeFloatProperty("roughness", "Roughness", "uRoughness", 0.5f, 0.0f, 1.0f),
        MakeFloatProperty("alphaCutoff", "Alpha Cutoff", "uAlphaCutoff", 0.0f, 0.0f, 1.0f),
        MakeColor3Property("emissiveColor", "Emissive Color", "uEmissiveColor", 0.0f, 0.0f, 0.0f),
        MakeVec2Property("uvTiling", "UV Tiling", "uUvTiling", 1.0f, 1.0f, 0.01f, 10.0f),
        MakeVec2Property("uvOffset", "UV Offset", "uUvOffset", 0.0f, 0.0f, -10.0f, 10.0f),
        MakeTextureProperty("albedoTexture", "Albedo", "uMaterialAlbedoTexture", "uUseMaterialAlbedoTexture"),
        MakeTextureProperty("normalTexture", "Normal", "uMaterialNormalTexture", "uUseMaterialNormalTexture"),
        MakeTextureProperty("metallicTexture", "Metallic", "uMaterialMetallicTexture", "uUseMaterialMetallicTexture"),
        MakeTextureProperty("roughnessTexture", "Roughness", "uMaterialRoughnessTexture", "uUseMaterialRoughnessTexture"),
        MakeTextureProperty("aoTexture", "AO", "uMaterialAoTexture", "uUseMaterialAoTexture"),
    };
}

std::vector<ShaderDefinition::Property> EmissiveProperties() {
    return {
        MakeColor4Property("albedoColor", "Albedo Color", "uColor", 1.0f, 1.0f, 1.0f, 1.0f),
        MakeColor3Property("emissiveColor", "Emissive Color", "uEmissiveColor", 1.0f, 1.0f, 1.0f),
        MakeFloatProperty("emissiveIntensity", "Emissive Intensity", "uEmissiveIntensity", 1.0f, 0.0f, 25.0f),
        MakeVec2Property("uvTiling", "UV Tiling", "uUvTiling", 1.0f, 1.0f, 0.01f, 10.0f),
        MakeVec2Property("uvOffset", "UV Offset", "uUvOffset", 0.0f, 0.0f, -10.0f, 10.0f),
        MakeTextureProperty("albedoTexture", "Albedo", "uMaterialAlbedoTexture", "uUseMaterialAlbedoTexture"),
        MakeTextureProperty("emissiveTexture", "Emissive", "uEmissiveTexture", "uUseEmissiveTexture"),
    };
}

std::vector<ShaderDefinition::Property> UnlitProperties() {
    return {
        MakeColor4Property("albedoColor", "Albedo Color", "uColor", 1.0f, 1.0f, 1.0f, 1.0f),
        MakeFloatProperty("alphaCutoff", "Alpha Cutoff", "uAlphaCutoff", 0.0f, 0.0f, 1.0f),
        MakeVec2Property("uvTiling", "UV Tiling", "uUvTiling", 1.0f, 1.0f, 0.01f, 10.0f),
        MakeVec2Property("uvOffset", "UV Offset", "uUvOffset", 0.0f, 0.0f, -10.0f, 10.0f),
        MakeTextureProperty("albedoTexture", "Albedo", "uMaterialAlbedoTexture", "uUseMaterialAlbedoTexture"),
    };
}

std::vector<ShaderDefinition::Property> TransparentProperties() {
    auto properties = PbrProperties();
    properties.erase(std::remove_if(properties.begin(), properties.end(), [](const ShaderDefinition::Property& property) {
        return property.id == "metallic" || property.id == "normalTexture" || property.id == "metallicTexture" ||
               property.id == "roughnessTexture" || property.id == "aoTexture";
    }), properties.end());
    return properties;
}

} // namespace

const std::vector<ShaderDefinition>& ShaderRegistry::BuiltInShaders() {
    static const std::vector<ShaderDefinition> shaders = {
        {"pbr", "PBR / Lit", "Default", "src/shaders/default/default.vs", "src/shaders/default/pbr.fs", true, true, true, true, true, false, PbrProperties()},
        {"unlit", "Unlit", "Default", "src/shaders/default/default.vs", "src/shaders/default/unlit.fs", false, false, false, false, true, false, UnlitProperties()},
        {"transparent", "Transparent", "Default", "src/shaders/default/default.vs", "src/shaders/default/transparent.fs", true, false, true, true, true, true, TransparentProperties()},
        {"emissive", "Emissive", "Default", "src/shaders/default/default.vs", "src/shaders/default/emissive.fs", false, false, false, true, true, false, EmissiveProperties()},
        {"normal_debug", "Normal Debug", "Debug", "src/shaders/default/default.vs", "src/shaders/default/normal_debug.fs", false, false, false, false, false, false},
        {"vertex_color", "Vertex Color", "Default", "src/shaders/default/default.vs", "src/shaders/default/vertex_color.fs", true, false, true, true, true, false, UnlitProperties()},
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
        material.properties["emissiveIntensity"].type = MaterialPropertyType::Float;
        material.properties["emissiveIntensity"].values[0] = 1.0f;
    }
    const ShaderDefinition& definition = Resolve(material.shader);
    for (const ShaderDefinition::Property& property : definition.properties) {
        auto& value = material.properties[property.id];
        value.type = property.type;
        value.values[0] = property.defaultValues[0];
        value.values[1] = property.defaultValues[1];
        value.values[2] = property.defaultValues[2];
        value.values[3] = property.defaultValues[3];
        value.boolValue = property.defaultBool;
    }
    return material;
}

} // namespace raceman

#pragma once

#include "Material.h"

#include <string>
#include <vector>

namespace raceman {

struct ShaderDefinition {
    std::string id;
    std::string displayName;
    std::string category;
    std::string vertexPath;
    std::string fragmentPath;
    bool usesLighting{true};
    bool supportsMetallic{true};
    bool supportsRoughness{true};
    bool supportsEmissive{true};
    bool supportsTextures{true};
    bool transparent{false};
    struct Property {
        std::string id;
        std::string label;
        std::string uniformName;
        MaterialPropertyType type{MaterialPropertyType::Float};
        bool color{false};
        float minValue{0.0f};
        float maxValue{1.0f};
        float defaultValues[4]{0.0f, 0.0f, 0.0f, 0.0f};
        bool defaultBool{false};
        std::string textureUseUniform;
    };
    std::vector<Property> properties;
};

class ShaderRegistry {
public:
    static const std::vector<ShaderDefinition>& BuiltInShaders();
    static const ShaderDefinition& Fallback();
    static bool IsKnownShader(const std::string& id);
    static const ShaderDefinition& Resolve(const std::string& id);
    static std::string NormalizeShaderId(const std::string& id);
    static bool IsGraphShaderId(const std::string& id);
    static std::string MakeGraphShaderId(const std::string& graphAssetPath);
    static std::string GraphFragmentPathForShaderId(const std::string& id);

    // Hand-written GLSL shaders authored as plain .vs/.fs files under assets/.
    // Unlike graph shaders (whose id maps to a single generated file by
    // convention), a code shader id carries the assets-relative folder so the
    // pair can live anywhere the user puts it:
    //   assets/shaders/water.fs  <->  "file:shaders/water"
    static bool IsCodeShaderId(const std::string& id);
    static std::string MakeCodeShaderId(const std::string& shaderAssetPath);
    static bool CodeShaderPathsForShaderId(const std::string& id,
                                           std::string& outVertexPath,
                                           std::string& outFragmentPath);
    static bool IsShaderSourceExtension(const std::string& extension);
    static Material MakeDefaultMaterial(const std::string& id, const std::string& name);
};

} // namespace raceman

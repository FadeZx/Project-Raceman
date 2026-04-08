#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace raceman {

struct Material {
    int version{1};
    std::string name{"pbr_default"};
    std::string shader{"pbr"};

    // Scalars
    float metallic{0.0f};
    float roughness{1.0f};

    // Colors (RGBA / RGB)
    float albedoColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float emissiveColor[3]{0.0f, 0.0f, 0.0f};

    // UV transforms
    float uvTiling[2]{1.0f, 1.0f};
    float uvOffset[2]{0.0f, 0.0f};

    // Texture paths (optional)
    std::string texAlbedo;
    std::string texNormal;
    std::string texMetallic;
    std::string texRoughness;
    std::string texAo;
};

class MaterialManager {
public:
    // Load all materials from project assets recursively.
    void LoadAll();
    // Persist a material to its known asset path, falling back to assets/<id>.mat.json.
    bool Save(const std::string& id, const Material& m);
    // Create a default material entry in memory (optionally auto-save)
    Material& CreateDefault(const std::string& id, bool autoSave = true);

    // Access
    bool Exists(const std::string& id) const;
    Material* Get(const std::string& id);
    const Material* Get(const std::string& id) const;

    std::vector<std::string> ListMaterialIds() const;

private:
    std::unordered_map<std::string, Material> materials_;
    std::unordered_map<std::string, std::string> materialPaths_;
    static std::string MaterialPath(const std::string& id);
    static bool LoadOne(const std::string& path, Material& out);
};

} // namespace raceman

#pragma once
#include <string>
#include <array>
#include <set>
#include <unordered_map>
#include <vector>

namespace raceman {

enum class MaterialPropertyType {
    Float,
    Vec2,
    Vec3,
    Vec4,
    Bool,
    Texture2D
};

struct MaterialPropertyValue {
    MaterialPropertyType type{MaterialPropertyType::Float};
    std::array<float, 4> values{0.0f, 0.0f, 0.0f, 0.0f};
    bool boolValue{false};
    std::string texturePath;
};

struct Material {
    int version{2};
    std::string name{"pbr_default"};
    std::string shader{"pbr"};

    // Scalars
    float metallic{0.0f};
    float roughness{1.0f};
    float clearCoat{0.0f};
    float clearCoatRoughness{0.1f};
    float anisotropy{0.0f};
    float transmission{0.0f};
    std::string alphaMode{"Opaque"};
    float alphaCutoff{0.5f};
    bool doubleSided{false};

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

    std::unordered_map<std::string, MaterialPropertyValue> properties;

    // Material variants: empty baseMaterialId means an independent/base
    // material. Fields whose id appears in overriddenFieldIds override the
    // base; all other fields are inherited and resolved lazily via
    // MaterialManager::Resolve(). Dynamic `properties` entries are keyed as
    // "prop:<name>" in overriddenFieldIds.
    std::string baseMaterialId;
    std::set<std::string> overriddenFieldIds;
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

    // Flattened effective material: walks the baseMaterialId chain root->leaf
    // and applies only the fields each level marks as overridden. Cycle-safe.
    Material Resolve(const std::string& id) const;
    // True if setting candidateId's base to proposedBaseId would create a
    // cycle (including candidateId == proposedBaseId).
    bool WouldCreateCycle(const std::string& candidateId, const std::string& proposedBaseId) const;

private:
    std::unordered_map<std::string, Material> materials_;
    std::unordered_map<std::string, std::string> materialPaths_;
    mutable std::unordered_map<std::string, Material> resolveCache_;
    static std::string MaterialPath(const std::string& id);
    static bool LoadOne(const std::string& path, Material& out);
};

} // namespace raceman

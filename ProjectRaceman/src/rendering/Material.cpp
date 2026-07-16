#include "Material.h"
#include "ShaderRegistry.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "../physics/SimpleJson.h"

namespace fs = std::filesystem;

namespace raceman {

namespace {

fs::path FindEngineRoot() {
    if (fs::exists("ProjectRaceman/src") && fs::is_directory("ProjectRaceman/src")) {
        return fs::absolute("ProjectRaceman").lexically_normal();
    }
    if (fs::exists("src") && fs::is_directory("src")) {
        return fs::absolute(".").lexically_normal();
    }
    return fs::absolute(".").lexically_normal();
}

fs::path FindProjectRoot() {
#if defined(_WIN32)
    char* overrideRoot = nullptr;
    std::size_t overrideRootLength = 0;
    if (_dupenv_s(&overrideRoot, &overrideRootLength, "RACEMAN_PROJECT_ROOT") == 0 && overrideRoot != nullptr) {
        std::string value = overrideRoot;
        std::free(overrideRoot);
        if (!value.empty()) {
            return fs::absolute(value).lexically_normal();
        }
    }
#else
    if (const char* overrideRoot = std::getenv("RACEMAN_PROJECT_ROOT")) {
        if (overrideRoot[0] != '\0') {
            return fs::absolute(overrideRoot).lexically_normal();
        }
    }
#endif

    const fs::path engineRoot = FindEngineRoot();
    try {
        for (const auto& entry : fs::directory_iterator(engineRoot)) {
            if (entry.is_directory() && fs::exists(entry.path() / "project.raceman.json")) {
                return entry.path().lexically_normal();
            }
        }
    } catch (...) {}
    return (engineRoot / "Project").lexically_normal();
}

fs::path FindAssetsRoot() {
    return (FindProjectRoot() / "assets").lexically_normal();
}

fs::path DefaultMaterialDirectory() {
    return FindAssetsRoot();
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

const char* MaterialPropertyTypeName(MaterialPropertyType type) {
    switch (type) {
    case MaterialPropertyType::Float: return "float";
    case MaterialPropertyType::Vec2: return "vec2";
    case MaterialPropertyType::Vec3: return "vec3";
    case MaterialPropertyType::Vec4: return "vec4";
    case MaterialPropertyType::Bool: return "bool";
    case MaterialPropertyType::Texture2D: return "texture2D";
    default: return "float";
    }
}

MaterialPropertyType ParseMaterialPropertyType(const std::string& value) {
    if (value == "vec2") return MaterialPropertyType::Vec2;
    if (value == "vec3") return MaterialPropertyType::Vec3;
    if (value == "vec4") return MaterialPropertyType::Vec4;
    if (value == "bool") return MaterialPropertyType::Bool;
    if (value == "texture2D") return MaterialPropertyType::Texture2D;
    return MaterialPropertyType::Float;
}

int MaterialPropertyComponentCount(MaterialPropertyType type) {
    switch (type) {
    case MaterialPropertyType::Vec2: return 2;
    case MaterialPropertyType::Vec3: return 3;
    case MaterialPropertyType::Vec4: return 4;
    default: return 1;
    }
}

} // namespace

static inline std::string trim_copy(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end());
    return s;
}

std::string MaterialManager::MaterialPath(const std::string& id) {
    return (DefaultMaterialDirectory() / (id + ".mat.json")).lexically_normal().string();
}

bool MaterialManager::LoadOne(const std::string& path, Material& out) {
    using namespace raceman::physics::json;
    std::ifstream in(path);
    if (!in.good()) return false;
    std::stringstream buf; buf << in.rdbuf();
    const std::string src = buf.str();
    try {
        Value root = parse(src);
        if (!root.is_object()) return false;
        const auto& obj = root.as_object();

        auto getf = [&](const auto& o, const char* key, float& dst){
            auto it = o.find(key); if (it!=o.end() && it->second.is_number()) dst = static_cast<float>(it->second.as_number());
        };
        auto getv2 = [&](const auto& o, const char* key, float dst[2]){
            auto it = o.find(key); if (it!=o.end() && it->second.is_array()){
                const auto& a = it->second.as_array();
                if (a.size()==2 && a[0].is_number() && a[1].is_number()){
                    dst[0] = static_cast<float>(a[0].as_number());
                    dst[1] = static_cast<float>(a[1].as_number());
                }
            }
        };
        auto getv3 = [&](const auto& o, const char* key, float dst[3]){
            auto it = o.find(key); if (it!=o.end() && it->second.is_array()){
                const auto& a = it->second.as_array();
                if (a.size()==3 && a[0].is_number() && a[1].is_number() && a[2].is_number()){
                    dst[0] = static_cast<float>(a[0].as_number());
                    dst[1] = static_cast<float>(a[1].as_number());
                    dst[2] = static_cast<float>(a[2].as_number());
                }
            }
        };
        auto getv4 = [&](const auto& o, const char* key, float dst[4]){
            auto it = o.find(key); if (it!=o.end() && it->second.is_array()){
                const auto& a = it->second.as_array();
                if (a.size()==4 && a[0].is_number() && a[1].is_number() && a[2].is_number() && a[3].is_number()){
                    dst[0] = static_cast<float>(a[0].as_number());
                    dst[1] = static_cast<float>(a[1].as_number());
                    dst[2] = static_cast<float>(a[2].as_number());
                    dst[3] = static_cast<float>(a[3].as_number());
                }
            }
        };
        auto gets = [&](const auto& o, const char* key, std::string& dst){
            auto it = o.find(key); if (it!=o.end() && it->second.is_string()) dst = it->second.as_string();
        };

        gets(obj, "name", out.name);
        gets(obj, "shader", out.shader);
        if (auto it = obj.find("version"); it != obj.end() && it->second.is_number()) {
            out.version = static_cast<int>(it->second.as_number());
        }
        out.shader = ShaderRegistry::NormalizeShaderId(out.shader);
        if (!ShaderRegistry::IsKnownShader(out.shader) && !ShaderRegistry::IsGraphShaderId(out.shader)) {
            out.shader = "pbr";
        }
        getf(obj, "metallic", out.metallic);
        getf(obj, "roughness", out.roughness);
        getf(obj, "clearCoat", out.clearCoat);
        getf(obj, "clearCoatRoughness", out.clearCoatRoughness);
        getf(obj, "anisotropy", out.anisotropy);
        getf(obj, "transmission", out.transmission);
        getf(obj, "alphaCutoff", out.alphaCutoff);
        gets(obj, "alphaMode", out.alphaMode);
        if (auto it = obj.find("doubleSided"); it != obj.end() && it->second.is_bool()) out.doubleSided = it->second.as_bool();
        if (auto it = obj.find("transparentSortPriority"); it != obj.end() && it->second.is_number()) out.transparentSortPriority = static_cast<int>(it->second.as_number());
        out.version = 2;
        getv4(obj, "albedoColor", out.albedoColor);
        getv3(obj, "emissiveColor", out.emissiveColor);
        getv2(obj, "uvTiling", out.uvTiling);
        getv2(obj, "uvOffset", out.uvOffset);

        if (auto it = obj.find("textures"); it != obj.end() && it->second.is_object()) {
            const auto& to = it->second.as_object();
            gets(to, "albedo", out.texAlbedo);
            gets(to, "normal", out.texNormal);
            gets(to, "metallic", out.texMetallic);
            gets(to, "roughness", out.texRoughness);
            gets(to, "ao", out.texAo);
        }

        if (auto it = obj.find("properties"); it != obj.end() && it->second.is_object()) {
            for (const auto& entry : it->second.as_object()) {
                if (!entry.second.is_object()) {
                    continue;
                }
                const auto& po = entry.second.as_object();
                MaterialPropertyValue property;
                if (auto typeIt = po.find("type"); typeIt != po.end() && typeIt->second.is_string()) {
                    property.type = ParseMaterialPropertyType(typeIt->second.as_string());
                }
                if (auto valueIt = po.find("value"); valueIt != po.end()) {
                    if (property.type == MaterialPropertyType::Bool && valueIt->second.is_bool()) {
                        property.boolValue = valueIt->second.as_bool();
                    } else if (property.type == MaterialPropertyType::Texture2D && valueIt->second.is_string()) {
                        property.texturePath = valueIt->second.as_string();
                    } else if (property.type == MaterialPropertyType::Float && valueIt->second.is_number()) {
                        property.values[0] = static_cast<float>(valueIt->second.as_number());
                    } else if (valueIt->second.is_array()) {
                        const auto& array = valueIt->second.as_array();
                        const int count = (std::min)(MaterialPropertyComponentCount(property.type), static_cast<int>(array.size()));
                        for (int i = 0; i < count; ++i) {
                            if (array[static_cast<std::size_t>(i)].is_number()) {
                                property.values[static_cast<std::size_t>(i)] = static_cast<float>(array[static_cast<std::size_t>(i)].as_number());
                            }
                        }
                    }
                }
                out.properties[entry.first] = property;
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

void MaterialManager::LoadAll() {
    materials_.clear();
    materialPaths_.clear();
    const fs::path dir = FindAssetsRoot();
    if (!fs::exists(dir)) return;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            if (path.extension() == ".json" && path.filename().string().find(".mat.json") != std::string::npos) {
                Material m;
                if (LoadOne(path.string(), m)) {
                    std::string stem = path.stem().string(); // "xxx.mat"
                    std::string id = stem;
                    // drop ".mat" if present
                    auto pos = id.rfind(".mat");
                    if (pos != std::string::npos) id = id.substr(0, pos);
                    if (m.name.empty()) m.name = id;
                    materials_[id] = m;
                    materialPaths_[id] = path.lexically_normal().string();
                }
            }
        }
    } catch (...) {
        // ignore
    }
}

bool MaterialManager::Save(const std::string& id, const Material& m) {
    std::string path = MaterialPath(id);
    if (auto it = materialPaths_.find(id); it != materialPaths_.end() && !it->second.empty()) {
        path = it->second;
    }

    try {
        fs::create_directories(fs::path(path).parent_path());
    } catch (...) {}

    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) return false;

    out << "{\n";
    out << "  \"version\": 2,\n";
    out << "  \"name\": \"" << JsonEscape(m.name.empty() ? id : m.name) << "\",\n";
    const std::string shaderId = ShaderRegistry::NormalizeShaderId(m.shader.empty() ? std::string("pbr") : m.shader);
    out << "  \"shader\": \"" << JsonEscape((ShaderRegistry::IsKnownShader(shaderId) || ShaderRegistry::IsGraphShaderId(shaderId)) ? shaderId : std::string("pbr")) << "\",\n";
    out << "  \"albedoColor\": [" << m.albedoColor[0] << ", " << m.albedoColor[1] << ", " << m.albedoColor[2] << ", " << m.albedoColor[3] << "],\n";
    out << "  \"metallic\": " << m.metallic << ",\n";
    out << "  \"roughness\": " << m.roughness << ",\n";
    out << "  \"clearCoat\": " << m.clearCoat << ",\n";
    out << "  \"clearCoatRoughness\": " << m.clearCoatRoughness << ",\n";
    out << "  \"anisotropy\": " << m.anisotropy << ",\n";
    out << "  \"transmission\": " << m.transmission << ",\n";
    out << "  \"alphaMode\": \"" << JsonEscape(m.alphaMode) << "\",\n";
    out << "  \"alphaCutoff\": " << m.alphaCutoff << ",\n";
    out << "  \"doubleSided\": " << (m.doubleSided ? "true" : "false") << ",\n";
    out << "  \"transparentSortPriority\": " << m.transparentSortPriority << ",\n";
    out << "  \"emissiveColor\": [" << m.emissiveColor[0] << ", " << m.emissiveColor[1] << ", " << m.emissiveColor[2] << "],\n";
    out << "  \"uvTiling\": [" << m.uvTiling[0] << ", " << m.uvTiling[1] << "],\n";
    out << "  \"uvOffset\": [" << m.uvOffset[0] << ", " << m.uvOffset[1] << "],\n";
    out << "  \"textures\": {\n";
    out << "    \"albedo\": \"" << JsonEscape(m.texAlbedo) << "\",\n";
    out << "    \"normal\": \"" << JsonEscape(m.texNormal) << "\",\n";
    out << "    \"metallic\": \"" << JsonEscape(m.texMetallic) << "\",\n";
    out << "    \"roughness\": \"" << JsonEscape(m.texRoughness) << "\",\n";
    out << "    \"ao\": \"" << JsonEscape(m.texAo) << "\"\n";
    out << "  },\n";
    out << "  \"properties\": {\n";
    std::size_t propertyIndex = 0;
    for (const auto& entry : m.properties) {
        const MaterialPropertyValue& property = entry.second;
        out << "    \"" << JsonEscape(entry.first) << "\": { \"type\": \"" << MaterialPropertyTypeName(property.type) << "\", \"value\": ";
        if (property.type == MaterialPropertyType::Bool) {
            out << (property.boolValue ? "true" : "false");
        } else if (property.type == MaterialPropertyType::Texture2D) {
            out << "\"" << JsonEscape(property.texturePath) << "\"";
        } else if (property.type == MaterialPropertyType::Float) {
            out << property.values[0];
        } else {
            const int count = MaterialPropertyComponentCount(property.type);
            out << "[";
            for (int i = 0; i < count; ++i) {
                if (i > 0) out << ", ";
                out << property.values[static_cast<std::size_t>(i)];
            }
            out << "]";
        }
        out << " }";
        if (++propertyIndex < m.properties.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  }\n";
    out << "}\n";
    return true;
}

Material& MaterialManager::CreateDefault(const std::string& id, bool autoSave) {
    Material m;
    m.name = id;
    m.shader = ShaderRegistry::NormalizeShaderId(m.shader);
    materials_[id] = m;
    if (autoSave) Save(id, m);
    return materials_[id];
}

bool MaterialManager::Exists(const std::string& id) const {
    return materials_.find(id) != materials_.end();
}

Material* MaterialManager::Get(const std::string& id) {
    auto it = materials_.find(id);
    return it == materials_.end() ? nullptr : &it->second;
}

const Material* MaterialManager::Get(const std::string& id) const {
    auto it = materials_.find(id);
    return it == materials_.end() ? nullptr : &it->second;
}

std::vector<std::string> MaterialManager::ListMaterialIds() const {
    std::vector<std::string> ids;
    ids.reserve(materials_.size());
    for (const auto& kv : materials_) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace raceman

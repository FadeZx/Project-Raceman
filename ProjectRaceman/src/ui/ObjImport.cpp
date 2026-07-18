#ifdef NOMINMAX
#undef NOMINMAX
#endif
#define NOMINMAX

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "ObjImport.h"
#include "../rendering/model.h"
#include "../rendering/mesh.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

namespace fs = std::filesystem;

namespace raceman {

namespace {
std::mutex& NormalRepairCacheMutex() {
    static std::mutex mutex;
    return mutex;
}

std::uint64_t FnvHash64(const std::string& value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

fs::path FindOwningProjectRoot(fs::path sourcePath) {
    std::error_code errorCode;
    sourcePath = fs::absolute(sourcePath, errorCode).lexically_normal().parent_path();
    if (errorCode) return {};
    for (int depth = 0; depth < 16 && !sourcePath.empty(); ++depth) {
        if (fs::is_regular_file(sourcePath / "project.raceman.json", errorCode)) return sourcePath;
        const fs::path parent = sourcePath.parent_path();
        if (parent == sourcePath) break;
        sourcePath = parent;
    }
    return {};
}

fs::path NormalRepairCachePath(const std::string& sourcePath) {
    std::error_code errorCode;
    const fs::path absolutePath = fs::absolute(fs::path(sourcePath), errorCode).lexically_normal();
    if (errorCode || !fs::is_regular_file(absolutePath, errorCode)) return {};
    const fs::path projectRoot = FindOwningProjectRoot(absolutePath);
    if (projectRoot.empty()) return {};

    const std::uintmax_t fileSize = fs::file_size(absolutePath, errorCode);
    if (errorCode) return {};
    const auto modifiedTime = fs::last_write_time(absolutePath, errorCode);
    if (errorCode) return {};
    const std::uint64_t pathHash = FnvHash64(absolutePath.generic_string());
    const auto timeValue = modifiedTime.time_since_epoch().count();
    return projectRoot / ".raceman-cache" / "model-normals" /
        ("v1_" + std::to_string(pathHash) + "_" + std::to_string(fileSize) + "_" + std::to_string(timeValue) + ".cache");
}

bool LoadNormalRepairCache(const fs::path& path, std::vector<std::size_t>& repairs) {
    repairs.clear();
    if (path.empty()) return false;
    std::ifstream input(path);
    std::string signature;
    int version = 0;
    std::size_t count = 0;
    if (!(input >> signature >> version >> count) || signature != "RACEMAN_NORMAL_REPAIRS" || version != 1 || count > 100000) return false;
    repairs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t meshIndex = 0;
        if (!(input >> meshIndex)) {
            repairs.clear();
            return false;
        }
        repairs.push_back(meshIndex);
    }
    return true;
}

void SaveNormalRepairCache(const fs::path& path, const std::vector<std::size_t>& repairs) {
    if (path.empty()) return;
    std::error_code errorCode;
    fs::create_directories(path.parent_path(), errorCode);
    if (errorCode) return;
    std::ofstream output(path, std::ios::trunc);
    if (!output.good()) return;
    output << "RACEMAN_NORMAL_REPAIRS 1 " << repairs.size() << '\n';
    for (std::size_t meshIndex : repairs) output << meshIndex << '\n';
}
} // namespace

std::shared_ptr<::Model> LoadModelFromFile(const std::string& path) {
    const fs::path cachePath = NormalRepairCachePath(path);
    std::vector<std::size_t> cachedRepairs;
    bool cacheLoaded = false;
    {
        std::lock_guard<std::mutex> lock(NormalRepairCacheMutex());
        cacheLoaded = LoadNormalRepairCache(cachePath, cachedRepairs);
    }
    if (cacheLoaded) {
        return std::make_shared<::Model>(path, false, &cachedRepairs, nullptr);
    }

    std::vector<std::size_t> detectedRepairs;
    auto model = std::make_shared<::Model>(path, false, nullptr, &detectedRepairs);
    {
        std::lock_guard<std::mutex> lock(NormalRepairCacheMutex());
        SaveNormalRepairCache(cachePath, detectedRepairs);
    }
    if (!detectedRepairs.empty()) {
        std::cout << "Cached inverted-normal repairs for " << detectedRepairs.size()
                  << " mesh(es) in " << fs::path(path).filename().string() << "." << std::endl;
    }
    return model;
}

std::vector<ImportedMeshInfo> GetMeshInfos(const std::shared_ptr<::Model>& model) {
    std::vector<ImportedMeshInfo> out;
    if (!model) return out;
    out.reserve(model->meshes.size());
    for (std::size_t i = 0; i < model->meshes.size(); ++i) {
        const ::Mesh& m = model->meshes[i];
        ImportedMeshInfo info{};
        info.vao = m.VAO;
        info.indexCount = static_cast<unsigned int>(m.indices.size());
        info.meshIndex = static_cast<unsigned int>(i);
        info.meshName = m.name;
        info.materialName = m.materialName;
        info.materialAlphaMode = m.materialAlphaMode;
        info.materialAlphaCutoff = m.materialAlphaCutoff;
        info.materialOpacity = m.materialOpacity;
        if (!m.vertices.empty()) {
            info.localBoundsMin = m.vertices.front().Position;
            info.localBoundsMax = m.vertices.front().Position;
            for (const ::Vertex& vertex : m.vertices) {
                info.localBoundsMin.x = (std::min)(info.localBoundsMin.x, vertex.Position.x);
                info.localBoundsMin.y = (std::min)(info.localBoundsMin.y, vertex.Position.y);
                info.localBoundsMin.z = (std::min)(info.localBoundsMin.z, vertex.Position.z);
                info.localBoundsMax.x = (std::max)(info.localBoundsMax.x, vertex.Position.x);
                info.localBoundsMax.y = (std::max)(info.localBoundsMax.y, vertex.Position.y);
                info.localBoundsMax.z = (std::max)(info.localBoundsMax.z, vertex.Position.z);
            }
        }
        auto assignTextureSlot = [&](const ::Texture& texture,
                                     std::string& path,
                                     std::vector<unsigned char>& embeddedData,
                                     std::string& embeddedExtension) {
            path = texture.path;
            embeddedData = texture.embeddedData;
            embeddedExtension = texture.embeddedExtension;
        };
        for (const ::Texture& texture : m.textures) {
            if (texture.type == "texture_albedo") {
                info.diffuseTextureId = texture.id;
                assignTextureSlot(texture, info.diffuseTexturePath, info.diffuseTextureEmbeddedData, info.diffuseTextureEmbeddedExtension);
            } else if (texture.type == "texture_normal") {
                assignTextureSlot(texture, info.normalTexturePath, info.normalTextureEmbeddedData, info.normalTextureEmbeddedExtension);
            } else if (texture.type == "texture_metallic") {
                assignTextureSlot(texture, info.metallicTexturePath, info.metallicTextureEmbeddedData, info.metallicTextureEmbeddedExtension);
            } else if (texture.type == "texture_roughness") {
                assignTextureSlot(texture, info.roughnessTexturePath, info.roughnessTextureEmbeddedData, info.roughnessTextureEmbeddedExtension);
            } else if (texture.type == "texture_ao") {
                assignTextureSlot(texture, info.aoTexturePath, info.aoTextureEmbeddedData, info.aoTextureEmbeddedExtension);
            }
        }
        // Store CPU vertex positions and indices for mouse-pick narrow phase.
        info.pickVertices.reserve(m.vertices.size());
        for (const ::Vertex& v : m.vertices) {
            info.pickVertices.push_back(v.Position);
        }
        info.pickIndices = m.indices;
        out.push_back(info);
    }
    return out;
}

bool GetCollisionMesh(const std::shared_ptr<::Model>& model, std::size_t meshIndex, ImportedCollisionMesh& outMesh) {
    outMesh.vertices.clear();
    outMesh.indices.clear();
    if (!model || meshIndex >= model->meshes.size()) {
        return false;
    }

    const ::Mesh& mesh = model->meshes[meshIndex];
    outMesh.vertices.reserve(mesh.vertices.size());
    for (const ::Vertex& vertex : mesh.vertices) {
        outMesh.vertices.push_back(vertex.Position);
    }
    outMesh.indices = mesh.indices;
    return !outMesh.vertices.empty() && outMesh.indices.size() >= 3;
}

} // namespace raceman

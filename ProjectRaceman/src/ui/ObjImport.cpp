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

namespace raceman {

std::shared_ptr<::Model> LoadModelFromFile(const std::string& path) {
    return std::make_shared<::Model>(path);
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
        for (const ::Texture& texture : m.textures) {
            if (texture.type == "texture_albedo") {
                info.diffuseTextureId = texture.id;
                info.diffuseTexturePath = texture.path;
                break;
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

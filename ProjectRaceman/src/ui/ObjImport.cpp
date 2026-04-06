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
        info.materialName = m.materialName;
        for (const ::Texture& texture : m.textures) {
            if (texture.type == "texture_albedo") {
                info.diffuseTextureId = texture.id;
                info.diffuseTexturePath = texture.path;
                break;
            }
        }
        out.push_back(info);
    }
    return out;
}

} // namespace raceman

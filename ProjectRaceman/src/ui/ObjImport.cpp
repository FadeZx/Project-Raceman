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
    for (const ::Mesh& m : model->meshes) {
        ImportedMeshInfo info{ m.VAO, static_cast<unsigned int>(m.indices.size()) };
        out.push_back(info);
    }
    return out;
}

} // namespace raceman
#pragma once
#include <memory>
#include <string>
#include <vector>

class Model; // global Model forward decl

namespace raceman {

// Minimal per-mesh info exposed to UI without including Mesh/Assimp
struct ImportedMeshInfo {
    unsigned int vao;
    unsigned int indexCount;
    unsigned int diffuseTextureId;
    unsigned int meshIndex;
    std::string materialName;
    std::string diffuseTexturePath;
};

// Loads a model using Assimp in a separate translation unit to avoid UI macro conflicts
std::shared_ptr<::Model> LoadModelFromFile(const std::string& path);

// Enumerate mesh infos (VAO and indexCount) for the loaded model
std::vector<ImportedMeshInfo> GetMeshInfos(const std::shared_ptr<::Model>& model);

} // namespace raceman

#pragma once
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

class Model; // global Model forward decl

namespace raceman {

// Minimal per-mesh info exposed to UI without including Mesh/Assimp
struct ImportedMeshInfo {
    unsigned int vao;
    unsigned int indexCount;
    unsigned int diffuseTextureId;
    unsigned int meshIndex;
    std::string meshName;
    std::string materialName;
    std::string materialAlphaMode;
    float materialAlphaCutoff{0.0f};
    float materialOpacity{1.0f};
    std::string diffuseTexturePath;
    std::string normalTexturePath;
    std::string metallicTexturePath;
    std::string roughnessTexturePath;
    std::string aoTexturePath;
    std::vector<unsigned char> diffuseTextureEmbeddedData;
    std::vector<unsigned char> normalTextureEmbeddedData;
    std::vector<unsigned char> metallicTextureEmbeddedData;
    std::vector<unsigned char> roughnessTextureEmbeddedData;
    std::vector<unsigned char> aoTextureEmbeddedData;
    std::string diffuseTextureEmbeddedExtension;
    std::string normalTextureEmbeddedExtension;
    std::string metallicTextureEmbeddedExtension;
    std::string roughnessTextureEmbeddedExtension;
    std::string aoTextureEmbeddedExtension;
    glm::vec3 localBoundsMin{0.0f};
    glm::vec3 localBoundsMax{0.0f};
    // CPU-side vertex positions and triangle indices for picking (narrow phase).
    std::vector<glm::vec3>    pickVertices;
    std::vector<unsigned int> pickIndices;
};

struct ImportedCollisionMesh {
    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
};

// Loads a model using Assimp in a separate translation unit to avoid UI macro conflicts
std::shared_ptr<::Model> LoadModelFromFile(const std::string& path);

// Enumerate mesh infos (VAO and indexCount) for the loaded model
std::vector<ImportedMeshInfo> GetMeshInfos(const std::shared_ptr<::Model>& model);
bool GetCollisionMesh(const std::shared_ptr<::Model>& model, std::size_t meshIndex, ImportedCollisionMesh& outMesh);

} // namespace raceman

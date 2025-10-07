#include "GarageScene.h"

#include "rendering/Renderer.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stdexcept>
#include <vector>

namespace raceman {

GarageScene::GarageScene(std::shared_ptr<Renderer> renderer)
    : Scene("Garage", std::move(renderer)) {}

void GarageScene::OnSceneActivated() {
    if (meshes_.empty()) {
        LoadAssets();
    }

    renderer_->SetupEnvironment("assets/environments/garage.hdr");
    renderer_->CreateShadowMaps(2048);
}

void GarageScene::Update(float) {
    // Static scene; nothing to update for now.
}

void GarageScene::Render(Renderer& renderer) {
    for (const auto& mesh : meshes_) {
        MeshDrawCommand cmd{};
        cmd.vao = mesh.vao;
        cmd.indexCount = mesh.indexCount;
        cmd.modelMatrix = mesh.transform;
        cmd.materialId = "garage_default";
        renderer.SubmitMesh(cmd);
    }
    renderer.Flush();
}

void GarageScene::RenderDebugUi(DebugUI& ui) {
    Scene::RenderDebugUi(ui);
}

void GarageScene::LoadAssets() {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile("assets/garage/garage.fbx",
                                             aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                 aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

    if (!scene || !scene->mRootNode) {
        throw std::runtime_error("Failed to load garage assets");
    }

    ProcessAssimpNode(scene, scene->mRootNode, glm::mat4(1.0f));
}

void GarageScene::ProcessAssimpNode(const aiScene* scene, const aiNode* node, const glm::mat4& parentTransform) {
    glm::mat4 transform = parentTransform * glm::transpose(glm::make_mat4(&node->mTransformation.a1));

    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes_.push_back(UploadMesh(mesh, transform));
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ProcessAssimpNode(scene, node->mChildren[i], transform);
    }
}

MeshResource GarageScene::UploadMesh(const aiMesh* mesh, const glm::mat4& transform) {
    MeshResource resource;

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    vertices.reserve(mesh->mNumVertices * 14);

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D& position = mesh->mVertices[i];
        const aiVector3D& normal = mesh->mNormals[i];
        const aiVector3D& tangent = mesh->mTangents ? mesh->mTangents[i] : aiVector3D(1.0f, 0.0f, 0.0f);
        const aiVector3D& bitangent = mesh->mBitangents ? mesh->mBitangents[i] : aiVector3D(0.0f, 1.0f, 0.0f);
        const aiVector3D& uv = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][i] : aiVector3D(0.0f, 0.0f, 0.0f);

        vertices.insert(vertices.end(), {position.x, position.y, position.z, normal.x, normal.y, normal.z, tangent.x,
                                         tangent.y, tangent.z, bitangent.x, bitangent.y, bitangent.z, uv.x, uv.y});
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            indices.push_back(face.mIndices[j]);
        }
    }

    glGenVertexArrays(1, &resource.vao);
    glBindVertexArray(resource.vao);

    glGenBuffers(1, &resource.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, resource.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * vertices.size(), vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &resource.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resource.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * indices.size(), indices.data(), GL_STATIC_DRAW);

    const unsigned int stride = 14 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(9 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(12 * sizeof(float)));

    resource.indexCount = static_cast<unsigned int>(indices.size());
    resource.transform = transform;

    glBindVertexArray(0);

    return resource;
}

} // namespace raceman

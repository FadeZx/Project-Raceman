#include "GarageScene.h"

#include "../rendering/Renderer.h"
#include "../rendering/Skybox.h"
#include "../rendering/shader.h"
#include "../ui/DebugUI.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <cmath>

#include <string>
#include <vector>

namespace {

glm::mat4 ConvertAssimpMatrix(const aiMatrix4x4& matrix)
{
    glm::mat4 result{1.0f};
    result[0][0] = matrix.a1;
    result[0][1] = matrix.a2;
    result[0][2] = matrix.a3;
    result[0][3] = matrix.a4;
    result[1][0] = matrix.b1;
    result[1][1] = matrix.b2;
    result[1][2] = matrix.b3;
    result[1][3] = matrix.b4;
    result[2][0] = matrix.c1;
    result[2][1] = matrix.c2;
    result[2][2] = matrix.c3;
    result[2][3] = matrix.c4;
    result[3][0] = matrix.d1;
    result[3][1] = matrix.d2;
    result[3][2] = matrix.d3;
    result[3][3] = matrix.d4;
    return result;
}

std::vector<std::string> BuildRacetrackSkyboxFaces()
{
    const std::string basePath = "assets/skybox/racetrack/";
    return {basePath + "px.jpg", basePath + "nx.jpg", basePath + "py.jpg", basePath + "ny.jpg", basePath + "pz.jpg",
            basePath + "nz.jpg"};
}

} // namespace

namespace raceman {

GarageScene::GarageScene(std::shared_ptr<Renderer> renderer)
    : Scene("Garage", std::move(renderer)) {}

void GarageScene::OnSceneActivated() {
    if (meshes_.empty()) {
        LoadAssets();
    }

    if (!skyboxShader_) {
        skyboxShader_ = std::make_unique<Shader>("src/shaders/skybox/skybox.vs", "src/shaders/skybox/skybox.fs");
        skyboxShader_->use();
        skyboxShader_->setInt("skybox", 0);
    }

    if (!skybox_) {
        skybox_ = std::make_unique<Skybox>(BuildRacetrackSkyboxFaces(), skyboxShader_->getID());
    }

    renderer_->SetupEnvironment("assets/environments/garage.hdr");
    renderer_->CreateShadowMaps(2048);

    const auto& rendererConfig = renderer_->GetConfig();
    projectionMatrix_ = glm::perspective(glm::radians(60.0f),
                                         static_cast<float>(rendererConfig.width) / static_cast<float>(rendererConfig.height),
                                         0.1f,
                                         100.0f);

    baseDisplayTransforms_.clear();
    for (std::size_t index : displayMeshIndices_) {
        if (index < meshes_.size()) {
            baseDisplayTransforms_.push_back(meshes_[index].transform);
        }
    }
    baseTransformCaptured_ = !baseDisplayTransforms_.empty();
    if (baseTransformCaptured_) {
        baseDisplayTransform_ = baseDisplayTransforms_.front();
    }
    accumulatedRotation_ = 0.0f;
}

void GarageScene::Update(float deltaTime) {
    if (displayMeshIndices_.empty() || baseDisplayTransforms_.size() != displayMeshIndices_.size()) {
        return;
    }

    if (rotateDisplayVehicle_) {
        accumulatedRotation_ += rotationSpeed_ * deltaTime;
        accumulatedRotation_ = std::fmod(accumulatedRotation_, 360.0f);
        const glm::mat4 rotation =
            glm::rotate(glm::mat4(1.0f), glm::radians(accumulatedRotation_), glm::vec3(0.0f, 1.0f, 0.0f));
        for (std::size_t i = 0; i < displayMeshIndices_.size(); ++i) {
            const std::size_t meshIndex = displayMeshIndices_[i];
            if (meshIndex < meshes_.size()) {
                meshes_[meshIndex].transform = baseDisplayTransforms_[i] * rotation;
            }
        }
    } else if (baseTransformCaptured_) {
        for (std::size_t i = 0; i < displayMeshIndices_.size(); ++i) {
            const std::size_t meshIndex = displayMeshIndices_[i];
            if (meshIndex < meshes_.size()) {
                meshes_[meshIndex].transform = baseDisplayTransforms_[i];
            }
        }
    }
}

void GarageScene::Render(Renderer& renderer) {
    const glm::mat4 view = glm::lookAt(cameraPosition_, cameraTarget_, cameraUp_);

    for (const auto& mesh : meshes_) {
        MeshDrawCommand cmd{};
        cmd.vao = mesh.vao;
        cmd.indexCount = mesh.indexCount;
        cmd.modelMatrix = mesh.transform;
        cmd.materialId = "garage_default";
        renderer.SubmitMesh(cmd);
    }
    renderer.Flush();

    if (skybox_) {
        skybox_->draw(view, projectionMatrix_);
    }
}

void GarageScene::RenderDebugUi(DebugUI&) {
    if (ImGui::Begin("Garage Lighting")) {
        ImGui::ColorEdit3("Ambient", &ambientLight_.x);
        ImGui::ColorEdit3("Directional", &directionalLightColor_.x);
        ImGui::SliderFloat3("Light Dir", &directionalLightDir_.x, -1.0f, 1.0f);
        ImGui::Checkbox("Rotate Display", &rotateDisplayVehicle_);
        ImGui::SliderFloat("Rotation Speed", &rotationSpeed_, 1.0f, 45.0f, "%.1f deg/s");

        auto& rendererSettings = renderer_->GetSettings();
        ImGui::Separator();
        ImGui::Text("Renderer Overrides");
        ImGui::Checkbox("Show Env Debug", &rendererSettings.showEnvironmentDebugView);
    }
    ImGui::End();
}

void GarageScene::LoadAssets() {
    meshes_.clear();
    displayMeshIndices_.clear();
    baseDisplayTransforms_.clear();
    baseTransformCaptured_ = false;

    const std::size_t wheelStart = meshes_.size();
    if (LoadModelFromFile("assets/car/Chevrolet-nascar-FLWheel.obj", glm::mat4(1.0f))) {
        const std::size_t wheelEnd = meshes_.size();
        for (std::size_t i = wheelStart; i < wheelEnd; ++i) {
            displayMeshIndices_.push_back(i);
        }
    }

    LoadModelFromFile("assets/garage/garage.fbx", glm::mat4(1.0f));

    if (meshes_.empty()) {
        CreateFallbackMesh();
    } else if (displayMeshIndices_.empty()) {
        displayMeshIndices_.push_back(0);
    }
}

bool GarageScene::LoadModelFromFile(const std::string& path, const glm::mat4& rootTransform)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

    if (!scene || !scene->mRootNode) {
        return false;
    }

    const std::size_t previousCount = meshes_.size();
    ProcessAssimpNode(scene, scene->mRootNode, rootTransform);
    return meshes_.size() > previousCount;
}

void GarageScene::ProcessAssimpNode(const aiScene* scene, const aiNode* node, const glm::mat4& parentTransform) {
    glm::mat4 transform = parentTransform * ConvertAssimpMatrix(node->mTransformation);

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

void GarageScene::CreateFallbackMesh() {
    meshes_.clear();
    displayMeshIndices_.clear();
    baseDisplayTransforms_.clear();
    meshes_.push_back(CreateUnitCubeMesh(2.0f));
    displayMeshIndices_.push_back(0);
    baseDisplayTransforms_.push_back(meshes_.front().transform);
    baseDisplayTransform_ = meshes_.front().transform;
    baseTransformCaptured_ = true;
}

} // namespace raceman

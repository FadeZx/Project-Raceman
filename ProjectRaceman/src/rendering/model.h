#ifndef MODEL_H
#define MODEL_H

#include <glad/glad.h> 

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "mesh.h"
#include "shader.h"

#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>
#include <map>
#include <vector>
#include <algorithm>
using namespace std;

unsigned int TextureFromFile(const char* path, const string& directory, bool gamma = false);
unsigned int TextureFromMemory(const unsigned char* data, int dataSize, bool gamma = false);

class Model
{
public:
    // model data 
    vector<Texture> textures_loaded;	// stores all the textures loaded so far, optimization to make sure textures aren't loaded more than once.
    vector<Mesh>    meshes;
    string directory;
    bool gammaCorrection;
    glm::vec3 startPosition;

    // constructor, expects a filepath to a 3D model.
    Model(string const& path, bool gamma = false) : gammaCorrection(gamma)
    {
        loadModel(path);
    }

    glm::vec3 getStartPosition() const {
        return startPosition;
    }

    // draws the model, and thus all its meshes
    void Draw(Shader& shader)
    {
        for (unsigned int i = 0; i < meshes.size(); i++)
            meshes[i].Draw(shader);
    }

private:
    string findFirstMtlDiffuseTexture() const
    {
        try {
            const std::filesystem::path dir(directory);
            if (!std::filesystem::exists(dir)) {
                return "";
            }

            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".mtl") {
                    continue;
                }

                std::ifstream in(entry.path());
                std::string line;
                while (std::getline(in, line)) {
                    auto first = std::find_if_not(line.begin(), line.end(), [](unsigned char ch) { return std::isspace(ch); });
                    line.erase(line.begin(), first);
                    if (line.rfind("map_Kd ", 0) == 0) {
                        std::string value = line.substr(7);
                        auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
                        value.erase(last, value.end());
                        if (!value.empty()) {
                            return value;
                        }
                    }
                }
            }
        } catch (...) {
            return "";
        }

        return "";
    }

    // loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
    void loadModel(string const& path)
    {
        // read file via ASSIMP
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
        // check for errors
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
        {
            cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
            return;
        }
        // retrieve the directory path of the filepath
        directory = std::filesystem::path(path).parent_path().string();

        // process ASSIMP's root node recursively
        processNode(scene->mRootNode, scene);
    }

    // processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
    void processNode(aiNode* node, const aiScene* scene)
    {
        // process each mesh located at the current node
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            // the node object only contains indices to index the actual objects in the scene. 
            // the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene));
        }
        // after we've processed all of the meshes (if any) we then recursively process each of the children nodes
        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            processNode(node->mChildren[i], scene);
        }

    }

    Mesh processMesh(aiMesh* mesh, const aiScene* scene)
    {
        // data to fill
        vector<Vertex> vertices;
        vector<unsigned int> indices;
        vector<Texture> textures;

        // walk through each of the mesh's vertices
        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex;
            glm::vec3 vector; // we declare a placeholder vector since assimp uses its own vector class that doesn't directly convert to glm's vec3 class so we transfer the data to this placeholder glm::vec3 first.
            // positions
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.Position = vector;
            // normals
            if (mesh->HasNormals())
            {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.Normal = vector;
            }
            // texture coordinates
            if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
            {
                glm::vec2 vec;
                // a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
                // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
                vec.x = mesh->mTextureCoords[0][i].x;
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexCoords = vec;
                // tangent
                vector.x = mesh->mTangents[i].x;
                vector.y = mesh->mTangents[i].y;
                vector.z = mesh->mTangents[i].z;
                vertex.Tangent = vector;
                // bitangent
                vector.x = mesh->mBitangents[i].x;
                vector.y = mesh->mBitangents[i].y;
                vector.z = mesh->mBitangents[i].z;
                vertex.Bitangent = vector;
            }
            else
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);

            vertices.push_back(vertex);
        }
        // now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            // retrieve all indices of the face and store them in the indices vector
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }
        // process materials
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        aiString matName;
        std::string materialName;
        if (material && material->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS) {
            materialName = matName.C_Str();
        }
        std::string materialAlphaMode;
        float materialAlphaCutoff = 0.0f;
        float materialOpacity = 1.0f;
        if (material) {
            float opacity = 1.0f;
            if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
                materialOpacity = opacity;
            }
            aiString alphaMode;
            if (material->Get("$mat.gltf.alphaMode", 0, 0, alphaMode) == AI_SUCCESS) {
                materialAlphaMode = alphaMode.C_Str();
                std::transform(materialAlphaMode.begin(), materialAlphaMode.end(), materialAlphaMode.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            }
            float alphaCutoff = 0.0f;
            if (material->Get("$mat.gltf.alphaCutoff", 0, 0, alphaCutoff) == AI_SUCCESS) {
                materialAlphaCutoff = alphaCutoff;
            }
            if (materialAlphaMode.empty() && materialOpacity < 0.999f) {
                materialAlphaMode = "blend";
            }
        }
        // we assume a convention for sampler names in the shaders. Each diffuse texture should be named
        // as 'texture_diffuseN' where N is a sequential number ranging from 1 to MAX_SAMPLER_NUMBER. 
        // Same applies to other texture as the following list summarizes:
        // diffuse: texture_diffuseN
        // specular: texture_specularN
        // normal: texture_normalN

        // 1. diffuse maps
        //vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
        //textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        //// 2. specular maps
        //vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
        //textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
        //// 3. normal maps
        //std::vector<Texture> normalMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
        //textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        //// 4. height maps
        //std::vector<Texture> heightMaps = loadMaterialTextures(material, aiTextureType_AMBIENT, "texture_height");
        //textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());

        vector<Texture> albedoMaps = loadMaterialTextures(material, scene, aiTextureType_DIFFUSE, "texture_albedo");
        textures.insert(textures.end(), albedoMaps.begin(), albedoMaps.end());

        vector<Texture> normalMaps = loadMaterialTextures(material, scene, aiTextureType_NORMALS, "texture_normal");
        textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());

        vector<Texture> metallicMaps = loadMaterialTextures(material, scene, aiTextureType_METALNESS, "texture_metallic");
        if (metallicMaps.empty()) {
            metallicMaps = loadMaterialTextures(material, scene, aiTextureType_SPECULAR, "texture_metallic");
        }
        textures.insert(textures.end(), metallicMaps.begin(), metallicMaps.end());

        vector<Texture> roughnessMaps = loadMaterialTextures(material, scene, aiTextureType_DIFFUSE_ROUGHNESS, "texture_roughness");
        textures.insert(textures.end(), roughnessMaps.begin(), roughnessMaps.end());

        vector<Texture> aoMaps = loadMaterialTextures(material, scene, aiTextureType_AMBIENT_OCCLUSION, "texture_ao");
        if (aoMaps.empty()) {
            aoMaps = loadMaterialTextures(material, scene, aiTextureType_LIGHTMAP, "texture_ao");
        }
        textures.insert(textures.end(), aoMaps.begin(), aoMaps.end());

        // return a mesh object created from the extracted mesh data
        return Mesh(vertices, indices, textures, materialName, std::string(mesh->mName.C_Str()), materialAlphaMode, materialAlphaCutoff, materialOpacity);

    }

    // checks all material textures of a given type and loads the textures if they're not loaded yet.
    // the required info is returned as a Texture struct.
    vector<Texture> loadMaterialTextures(aiMaterial* mat, const aiScene* scene, aiTextureType type, string typeName) {
        vector<Texture> textures;
        if (!mat) {
            return textures;
        }

        for (unsigned int i = 0; i < mat->GetTextureCount(type); i++) {
            aiString str;
            if (mat->GetTexture(type, i, &str) != AI_SUCCESS) {
                continue;
            }

            std::string texturePath = str.C_Str();
            if (texturePath.empty() && type == aiTextureType_DIFFUSE) {
                texturePath = findFirstMtlDiffuseTexture();
            }
            if (texturePath.empty()) {
                std::cout << "Skipping empty texture path for type: " << typeName << std::endl;
                continue;
            }

            // Debugging: Output the texture type and path
            //std::cout << "Checking texture: " << texturePath << " for type: " << typeName << std::endl;

            bool skip = false;
            for (unsigned int j = 0; j < textures_loaded.size(); j++) {
                if (textures_loaded[j].path == texturePath) {
                    textures.push_back(textures_loaded[j]);
                    skip = true;
                    break;
                }
            }
            if (!skip) {
                // If texture hasn't been loaded already, load it
                Texture texture;
                if (scene != nullptr) {
                    if (const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(texturePath.c_str())) {
                        texture.embeddedExtension = embeddedTexture->achFormatHint[0] != '\0' ? std::string(embeddedTexture->achFormatHint) : std::string("png");
                        if (embeddedTexture->mHeight == 0) {
                            const int byteCount = static_cast<int>(embeddedTexture->mWidth);
                            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(embeddedTexture->pcData);
                            texture.embeddedData.assign(bytes, bytes + byteCount);
                            texture.id = TextureFromMemory(texture.embeddedData.data(), byteCount);
                        } else {
                            const int byteCount = static_cast<int>(embeddedTexture->mWidth * embeddedTexture->mHeight * sizeof(aiTexel));
                            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(embeddedTexture->pcData);
                            texture.embeddedData.assign(bytes, bytes + byteCount);
                            texture.embeddedExtension = "rgba";
                            texture.id = 0;
                        }
                    }
                }
                if (texture.id == 0 && texture.embeddedData.empty()) {
                    texture.id = TextureFromFile(texturePath.c_str(), this->directory);  // Assuming TextureFromFile is your custom function
                }
                if (texture.id == 0) {
                    if (!texture.embeddedData.empty()) {
                        texture.type = typeName;
                        texture.path = texturePath;
                        textures.push_back(texture);
                        textures_loaded.push_back(texture);
                    }
                    continue;
                }
                texture.type = typeName;
                texture.path = texturePath;
                textures.push_back(texture);
                textures_loaded.push_back(texture);

                // Debugging: Notify that this texture was successfully loaded
                //std::cout << "Loaded texture: " << texturePath << " as type: " << typeName << std::endl;
            }
        }
        return textures;
    }
};


unsigned int TextureFromFile(const char* path, const string& directory, bool gamma)
{
    (void)gamma;
    std::filesystem::path texturePath(path);
    if (texturePath.is_relative()) {
        texturePath = std::filesystem::path(directory) / texturePath;
    }
    string filename = texturePath.lexically_normal().string();

    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char* data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
    if (data)
    {
        GLenum format = GL_RGB;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;
        else {
            std::cout << "Unsupported texture component count at path: " << filename << std::endl;
            stbi_image_free(data);
            glDeleteTextures(1, &textureID);
            return 0;
        }

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "Texture failed to load at path: " << filename << std::endl;
        glDeleteTextures(1, &textureID);
        textureID = 0;
        stbi_image_free(data);
    }

    return textureID;
}

unsigned int TextureFromMemory(const unsigned char* bytes, int dataSize, bool gamma)
{
    (void)gamma;
    if (bytes == nullptr || dataSize <= 0) {
        return 0;
    }

    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char* data = stbi_load_from_memory(bytes, dataSize, &width, &height, &nrComponents, 0);
    if (data)
    {
        GLenum format = GL_RGB;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;
        else {
            stbi_image_free(data);
            glDeleteTextures(1, &textureID);
            return 0;
        }

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        glDeleteTextures(1, &textureID);
        textureID = 0;
        stbi_image_free(data);
    }

    return textureID;
}
#endif

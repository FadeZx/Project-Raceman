#pragma once

#include "SceneEditor.h"
#include "./ObjImport.h"
#include "Console.h"
#include "../rendering/PrimitivePlane.h"
#include "../rendering/Renderer.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace raceman::scene_editor_internal {

namespace fs = std::filesystem;

inline std::string OpenObjFileDialogWin32() {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
    char fileBuffer[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(fileBuffer);
    }
#endif
    return std::string();
}

inline std::string TrimCopyLocal(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    return s;
}

inline std::string NormalizeSlashes(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

inline std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

inline bool EndsWith(const std::string& value, const std::string& suffix) {
    if (suffix.size() > value.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

inline bool IsObjAssetPath(const std::string& path) {
    return ToLowerCopy(fs::path(path).extension().string()) == ".obj";
}

inline bool IsMaterialAssetPath(const std::string& path) {
    return EndsWith(ToLowerCopy(NormalizeSlashes(path)), ".mat.json");
}

inline std::string MaterialIdFromAssetPath(const std::string& path) {
    std::string filename = fs::path(path).filename().string();
    const std::string suffix = ".mat.json";
    if (EndsWith(ToLowerCopy(filename), suffix)) {
        filename.resize(filename.size() - suffix.size());
    }
    return filename.empty() ? "pbr_default" : filename;
}

inline std::string ParentProjectDirectory(const std::string& path) {
    std::string parent = NormalizeSlashes(fs::path(path).parent_path().string());
    return parent.empty() ? std::string("assets") : parent;
}

inline fs::path FindAssetsRoot() {
    if (fs::exists("assets") && fs::is_directory("assets")) {
        return fs::absolute("assets").lexically_normal();
    }
    if (fs::exists("ProjectRaceman/assets") && fs::is_directory("ProjectRaceman/assets")) {
        return fs::absolute("ProjectRaceman/assets").lexically_normal();
    }
    return fs::absolute("assets").lexically_normal();
}

inline bool IsUnderPath(const fs::path& path, const fs::path& root) {
    auto absPath = fs::weakly_canonical(path);
    auto absRoot = fs::weakly_canonical(root);
    auto pathIt = absPath.begin();
    auto rootIt = absRoot.begin();
    for (; rootIt != absRoot.end(); ++rootIt, ++pathIt) {
        if (pathIt == absPath.end() || *pathIt != *rootIt) {
            return false;
        }
    }
    return true;
}

inline std::string ToProjectAssetPath(const fs::path& absolutePath, const fs::path& assetsRoot) {
    fs::path relative = fs::relative(fs::weakly_canonical(absolutePath), fs::weakly_canonical(assetsRoot));
    return NormalizeSlashes((fs::path("assets") / relative).lexically_normal().string());
}

inline fs::path ProjectAssetPathToAbsolute(const std::string& projectPath) {
    fs::path normalized = fs::path(NormalizeSlashes(projectPath));
    fs::path assetsRoot = FindAssetsRoot();
    auto it = normalized.begin();
    if (it != normalized.end() && *it == "assets") {
        ++it;
    }

    fs::path relative;
    for (; it != normalized.end(); ++it) {
        relative /= *it;
    }
    return (assetsRoot / relative).lexically_normal();
}

inline glm::vec3 GizmoAxisVector(int axis) {
    if (axis == 0) return {1.0f, 0.0f, 0.0f};
    if (axis == 1) return {0.0f, 1.0f, 0.0f};
    return {0.0f, 0.0f, 1.0f};
}

inline glm::vec4 GizmoAxisColor(int axis, bool highlighted) {
    const float alpha = 1.0f;
    if (highlighted) return {1.0f, 1.0f, 0.2f, alpha};
    if (axis == 0) return {1.0f, 0.15f, 0.12f, alpha};
    if (axis == 1) return {0.2f, 0.9f, 0.2f, alpha};
    return {0.2f, 0.45f, 1.0f, alpha};
}

inline bool ProjectWorldToScreen(const glm::vec3& world, const Renderer& renderer, glm::vec2& outScreen) {
    const auto& cfg = renderer.GetConfig();
    if (cfg.width <= 0 || cfg.height <= 0) {
        return false;
    }

    glm::vec4 clip = renderer.GetProj() * renderer.GetView() * glm::vec4(world, 1.0f);
    if (std::abs(clip.w) < 0.0001f) {
        return false;
    }

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outScreen.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(cfg.width);
    outScreen.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(cfg.height);
    return true;
}

inline float DistanceToScreenSegment(const glm::vec2& point, const glm::vec2& a, const glm::vec2& b) {
    glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    if (len2 <= 0.0001f) {
        return glm::length(point - a);
    }
    const float t = std::clamp(glm::dot(point - a, ab) / len2, 0.0f, 1.0f);
    return glm::length(point - (a + ab * t));
}

inline std::string JsonEscape(const std::string& value) {
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

inline void CopyFileCreatingDirs(const fs::path& from, const fs::path& to) {
    if (!fs::exists(from) || !fs::is_regular_file(from)) {
        return;
    }
    fs::create_directories(to.parent_path());
    fs::copy_file(from, to, fs::copy_options::overwrite_existing);
}

inline std::vector<std::string> ReadMaterialLibraries(const fs::path& objPath) {
    std::vector<std::string> libraries;
    std::ifstream in(objPath);
    std::string line;
    while (std::getline(in, line)) {
        line = TrimCopyLocal(line);
        if (line.rfind("mtllib ", 0) == 0) {
            std::string rest = TrimCopyLocal(line.substr(7));
            if (!rest.empty()) {
                libraries.push_back(rest);
            }
        }
    }
    return libraries;
}

inline std::vector<std::string> ReadDiffuseTextureRefs(const fs::path& mtlPath) {
    std::vector<std::string> textures;
    std::ifstream in(mtlPath);
    std::string line;
    while (std::getline(in, line)) {
        line = TrimCopyLocal(line);
        if (line.rfind("map_Kd ", 0) == 0) {
            std::string rest = TrimCopyLocal(line.substr(7));
            if (!rest.empty()) {
                textures.push_back(rest);
            }
        }
    }
    return textures;
}

inline std::string PrepareObjImportPath(const std::string& inputPath) {
    fs::path sourcePath(inputPath);
    if (sourcePath.empty()) {
        return {};
    }

    fs::path assetsRoot = FindAssetsRoot();
    fs::path absoluteSource = fs::absolute(sourcePath).lexically_normal();
    if (fs::exists(absoluteSource)) {
        absoluteSource = fs::weakly_canonical(absoluteSource);
    }

    if (IsUnderPath(absoluteSource, assetsRoot)) {
        return ToProjectAssetPath(absoluteSource, assetsRoot);
    }

    std::string stem = absoluteSource.stem().string();
    if (stem.empty()) {
        stem = "imported_obj";
    }

    fs::path importsRoot = assetsRoot / "imports";
    fs::path destDir;
    for (int i = 1; i < 10000; ++i) {
        destDir = importsRoot / (stem + "_" + std::to_string(i));
        if (!fs::exists(destDir)) {
            break;
        }
    }
    fs::create_directories(destDir);

    fs::path destObj = destDir / absoluteSource.filename();
    CopyFileCreatingDirs(absoluteSource, destObj);

    for (const std::string& mtlRef : ReadMaterialLibraries(absoluteSource)) {
        fs::path sourceMtl = absoluteSource.parent_path() / fs::path(mtlRef);
        fs::path destMtl = destDir / fs::path(mtlRef);
        CopyFileCreatingDirs(sourceMtl, destMtl);

        if (!fs::exists(sourceMtl)) {
            continue;
        }
        for (const std::string& texRef : ReadDiffuseTextureRefs(sourceMtl)) {
            fs::path sourceTexture = sourceMtl.parent_path() / fs::path(texRef);
            fs::path destTexture = destMtl.parent_path() / fs::path(texRef);
            CopyFileCreatingDirs(sourceTexture, destTexture);
        }
    }

    return ToProjectAssetPath(destObj, assetsRoot);
}

inline void ApplyMeshInfoToSceneObject(SceneObject& object, const ImportedMeshInfo& info, const std::shared_ptr<::Model>& model) {
    object.vao = info.vao;
    object.indexCount = info.indexCount;
    object.meshIndex = static_cast<int>(info.meshIndex);
    object.importedMaterialName = info.materialName;
    object.diffuseTexturePath = NormalizeSlashes(info.diffuseTexturePath);
    object.diffuseTextureId = info.diffuseTextureId;
    object.modelRef = model;
}

inline void BeginProjectAssetDrag(const std::string& payloadType, const std::string& payloadValue, const std::string& label) {
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload(payloadType.c_str(), payloadValue.c_str(), payloadValue.size() + 1);
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }
}

inline bool IsCtrlSPressed() {
    ImGuiIO& io = ImGui::GetIO();
    return (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S));
}

inline constexpr const char* kObjAssetPayload = "RACEMAN_PROJECT_OBJ";
inline constexpr const char* kMaterialAssetPayload = "RACEMAN_PROJECT_MATERIAL";

} // namespace raceman::scene_editor_internal

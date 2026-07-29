// Hand-written GLSL shaders ("code shaders"). Unlike shader graphs, whose
// fragment source is generated from a node document, these are plain .vs/.fs
// assets the user edits in their own IDE. The editor's job is to create the
// starter pair, notice external saves, recompile, and report errors.

#include "SceneEditorInternal.h"
#include "../rendering/ShaderRegistry.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

constexpr double kShaderCodeWatchDebounceSeconds = 0.15;
constexpr float kShaderCodeWatchIntervalSeconds = 0.25f;
// A shader that outgrows this is being generated, not hand-written; the preview
// is a convenience, not an editor.
constexpr std::size_t kShaderCodePreviewByteLimit = 64u * 1024u;

std::string SanitizeShaderCodeBaseName(std::string value) {
    value = TrimCopyLocal(std::move(value));
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            // Shader ids are lowercased by ShaderRegistry::NormalizeShaderId, so
            // fold the filename too and the id round-trips unambiguously.
            out.push_back(ch == ' ' ? '_' : static_cast<char>(std::tolower(uch)));
        }
    }
    return out.empty() ? std::string("shader") : out;
}

// Matches src/shaders/default/default.vs. A new code shader owns both stages so
// the user can edit vertex logic too, but starting from the engine's own vertex
// shader means the varyings the fragment template reads are always present.
const char* kVertexTemplate = R"(#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform vec2 uUvTiling;
uniform vec2 uUvOffset;

out vec2 vUV;
out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec3 vWorldTangent;
out vec3 vWorldBitangent;

void main() {
    vUV = aUV * uUvTiling + uUvOffset;
    vec4 worldPosition = uModel * vec4(aPos, 1.0);
    vWorldPosition = worldPosition.xyz;
    mat3 normalMatrix = mat3(transpose(inverse(uModel)));
    vWorldNormal = normalize(normalMatrix * aNormal);
    vWorldTangent = normalize(mat3(uModel) * aTangent);
    vWorldBitangent = normalize(mat3(uModel) * aBitangent);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

// The uniform block and MRT outputs below mirror what the shader graph compiler
// emits (see CompileShaderGraphFragment in SceneEditorShaderCode's sibling
// SceneEditorShaderGraph.cpp) and therefore what the renderer actually binds.
// Removing an output or renaming a uniform will silently break lighting.
const char* kFragmentTemplate = R"(#version 450 core

// ---------------------------------------------------------------------------
// Engine interface. These names are bound by the renderer - keep them as-is.
// ---------------------------------------------------------------------------
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalBuffer;
layout(location = 2) out vec4 AmbientBuffer;

in vec2 vUV;
in vec3 vWorldPosition;
in vec3 vWorldNormal;

uniform vec4 uColor;
uniform vec3 uEmissiveColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec2 uUvTiling;
uniform vec2 uUvOffset;

// Legacy per-draw diffuse texture, used when a mesh supplies a texture directly
// rather than through a material albedo slot. Bound to texture unit 0.
uniform sampler2D uDiffuseTexture;
uniform bool uUseDiffuseTexture;

uniform sampler2D uMaterialAlbedoTexture;
uniform sampler2D uMaterialNormalTexture;
uniform sampler2D uMaterialMetallicTexture;
uniform sampler2D uMaterialRoughnessTexture;
uniform sampler2D uMaterialAoTexture;
uniform bool uUseMaterialAlbedoTexture;
uniform bool uUseMaterialNormalTexture;
uniform bool uUseMaterialMetallicTexture;
uniform bool uUseMaterialRoughnessTexture;
uniform bool uUseMaterialAoTexture;

uniform vec3 uCameraPosition;
uniform vec3 uAmbientColor;

struct Light {
    int type;
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float range;
    float spotAngleDegrees;
};
uniform int uLightCount;
uniform Light uLights[8];

vec3 EvaluateAmbient(vec3 albedo, vec3 normal, float metallic, float roughness) {
    vec3 viewDir = normalize(uCameraPosition - vWorldPosition);
    vec3 specularColor = mix(vec3(0.04), albedo, metallic);
    vec3 ambientDiffuse = albedo * uAmbientColor * (1.0 - metallic * 0.75);
    float rim = pow(clamp(1.0 - max(dot(normal, viewDir), 0.0), 0.0, 1.0), 2.0);
    vec3 ambientSpecular = specularColor * (uAmbientColor + vec3(0.08)) * mix(0.15, 1.0, 1.0 - roughness) * (0.35 + metallic * 0.65) * (0.7 + rim * 0.3);
    return ambientDiffuse + ambientSpecular;
}

vec3 ApplyLighting(vec3 albedo, vec3 normal, float metallic, float roughness) {
    vec3 viewDir = normalize(uCameraPosition - vWorldPosition);
    float shininess = mix(96.0, 8.0, roughness);
    vec3 specularColor = mix(vec3(0.04), albedo, metallic);
    vec3 lit = EvaluateAmbient(albedo, normal, metallic, roughness);
    for (int i = 0; i < uLightCount; ++i) {
        Light light = uLights[i];
        vec3 lightDir;
        float attenuation = 1.0;
        if (light.type == 0) {
            lightDir = normalize(-light.direction);
        } else {
            vec3 toLight = light.position - vWorldPosition;
            float distanceToLight = length(toLight);
            if (distanceToLight > max(light.range, 0.001)) continue;
            lightDir = distanceToLight > 0.0001 ? toLight / distanceToLight : vec3(0.0, 1.0, 0.0);
            float rangeFactor = clamp(1.0 - distanceToLight / max(light.range, 0.001), 0.0, 1.0);
            attenuation = rangeFactor * rangeFactor;
            if (light.type == 2) {
                float cosTheta = dot(normalize(light.direction), normalize(vWorldPosition - light.position));
                float cutoff = cos(radians(clamp(light.spotAngleDegrees, 1.0, 179.0)) * 0.5);
                attenuation *= smoothstep(cutoff, min(cutoff + 0.08, 1.0), cosTheta);
            }
        }
        float ndotl = max(dot(normal, lightDir), 0.0);
        vec3 halfDir = normalize(lightDir + viewDir);
        float specular = pow(max(dot(normal, halfDir), 0.0), shininess) * ndotl * mix(0.2, 1.0, 1.0 - roughness);
        float broadSpecular = ndotl * metallic * mix(0.05, 0.45, roughness);
        vec3 radiance = light.color * max(light.intensity, 0.0) * attenuation;
        vec3 diffuse = albedo * ndotl * (1.0 - metallic);
        lit += (diffuse + specularColor * (specular + broadSpecular)) * radiance;
    }
    return lit;
}

// ---------------------------------------------------------------------------
// Your shader starts here. This default reproduces the built-in PBR result.
// ---------------------------------------------------------------------------
void main() {
    // Same precedence as the built-in PBR shader: a material albedo texture wins,
    // then the per-draw diffuse texture, then plain uColor.
    vec4 base = uUseMaterialAlbedoTexture
        ? texture(uMaterialAlbedoTexture, vUV)
        : (uUseDiffuseTexture ? texture(uDiffuseTexture, vUV) : vec4(1.0));
    base *= uColor;

    float metallic = uMetallic;
    if (uUseMaterialMetallicTexture) {
        metallic *= texture(uMaterialMetallicTexture, vUV).r;
    }
    metallic = clamp(metallic, 0.0, 1.0);

    float roughness = uRoughness;
    if (uUseMaterialRoughnessTexture) {
        roughness *= texture(uMaterialRoughnessTexture, vUV).r;
    }
    roughness = clamp(roughness, 0.02, 1.0);

    float ao = uUseMaterialAoTexture ? texture(uMaterialAoTexture, vUV).r : 1.0;

    vec3 normal = normalize(vWorldNormal);
    if (!gl_FrontFacing) normal = -normal;

    vec3 lit = ApplyLighting(base.rgb, normal, metallic, roughness) * ao + uEmissiveColor;

    NormalBuffer = vec4(normal, 1.0);
    AmbientBuffer = vec4(EvaluateAmbient(base.rgb, normal, metallic, roughness) * ao, base.a);
    FragColor = vec4(lit, base.a);
}
)";

bool WriteTextFile(const fs::path& path, const char* contents) {
    std::ofstream out(path, std::ios::trunc | std::ios::binary);
    if (!out.good()) {
        return false;
    }
    out << contents;
    return out.good();
}

// The fragment stage is what a code shader is addressed by, so a .vs selection
// maps onto its sibling .fs for compile and material-binding purposes.
std::string FragmentPeerOfShaderSource(const std::string& shaderSourcePath) {
    fs::path path(NormalizeSlashes(shaderSourcePath));
    path.replace_extension(".fs");
    return NormalizeSlashes(path.generic_string());
}

std::string ShaderStageLabel(const std::string& shaderSourcePath) {
    return ToLowerCopy(fs::path(shaderSourcePath).extension().string()) == ".vs" ? "Vertex Shader" : "Fragment Shader";
}

} // namespace

bool SceneEditor::CreateShaderCodeAsset(const std::string& requestedName, std::string* outFragmentPath) {
    const std::string baseName = SanitizeShaderCodeBaseName(requestedName);
    if (baseName.empty()) {
        if (console_) console_->AddError("Shader name cannot be empty.");
        return false;
    }

    const std::string fragmentProjectPath = selectedProjectDirectory_ + "/" + baseName + ".fs";
    const std::string vertexProjectPath = selectedProjectDirectory_ + "/" + baseName + ".vs";
    const fs::path fragmentPath = ProjectAssetPathToAbsolute(fragmentProjectPath);
    const fs::path vertexPath = ProjectAssetPathToAbsolute(vertexProjectPath);
    const fs::path assetsRoot = FindAssetsRoot();

    if (!IsUnderPath(fragmentPath, assetsRoot) || !IsUnderPath(vertexPath, assetsRoot)) {
        if (console_) console_->AddError("Shader target is outside the project assets folder: " + baseName);
        return false;
    }
    if (fs::exists(fragmentPath) || fs::exists(vertexPath)) {
        if (console_) console_->AddError("Shader already exists: " + baseName);
        return false;
    }

    std::error_code ec;
    fs::create_directories(fragmentPath.parent_path(), ec);
    if (!WriteTextFile(vertexPath, kVertexTemplate) || !WriteTextFile(fragmentPath, kFragmentTemplate)) {
        if (console_) console_->AddError("Failed to create shader: " + baseName);
        fs::remove(vertexPath, ec);
        fs::remove(fragmentPath, ec);
        return false;
    }

    RefreshProjectFiles();
    if (outFragmentPath) *outFragmentPath = fragmentProjectPath;
    if (console_) console_->AddLog("Created shader: " + fragmentProjectPath);
    return true;
}

bool SceneEditor::RecompileShaderCodeAsset(const std::string& shaderSourcePath, bool quiet) {
    if (shaderSourcePath.empty() || renderer_ == nullptr) {
        return false;
    }
    const std::string fragmentProjectPath = FragmentPeerOfShaderSource(shaderSourcePath);
    const std::string shaderId = ShaderRegistry::MakeCodeShaderId(fragmentProjectPath);
    if (shaderId.empty()) {
        return false;
    }

    std::string log;
    const bool ok = renderer_->TryCompileMaterialShader(shaderId, log);

    ShaderCodeStatus& status = shaderCodeStatus_[fragmentProjectPath];
    status.compiled = ok;
    status.log = log;
    // The .vs and .fs are compiled together into one program, so a failure
    // belongs to both files. Mirror the status onto the vertex peer if it exists
    // so selecting either file in the browser shows the same result.
    fs::path vertexPeer(NormalizeSlashes(fragmentProjectPath));
    vertexPeer.replace_extension(".vs");
    const std::string vertexProjectPath = NormalizeSlashes(vertexPeer.generic_string());
    if (fs::exists(ProjectAssetPathToAbsolute(vertexProjectPath))) {
        shaderCodeStatus_[vertexProjectPath] = status;
    }

    if (console_ && !quiet) {
        if (ok) {
            console_->AddLog("Recompiled shader: " + fragmentProjectPath);
        } else {
            console_->AddError("Shader compile failed: " + fragmentProjectPath + "\n" + log);
        }
    }
    return ok;
}

void SceneEditor::TickShaderCodeWatcher(float deltaTime) {
    shaderCodeWatchAccumulator_ += (std::max)(0.0f, deltaTime);
    if (shaderCodeWatchAccumulator_ < kShaderCodeWatchIntervalSeconds) {
        return;
    }
    shaderCodeWatchAccumulator_ = 0.0f;

    // Watch every hand-written shader source in the project, not just the ones
    // currently bound to a material: the user usually edits a shader before
    // assigning it, and reporting a compile error at that point is the point.
    // Generated graph output is excluded - the graph owns those files.
    const double now = ImGui::GetTime();
    for (const std::string& projectPath : projectFiles_) {
        if (!IsShaderSourceAssetPath(projectPath) || IsGeneratedShaderAssetPath(projectPath)) {
            continue;
        }
        std::error_code ec;
        const fs::file_time_type modifiedTime = fs::last_write_time(ProjectAssetPathToAbsolute(projectPath), ec);
        if (ec) {
            continue;
        }
        ShaderCodeWatchEntry& entry = shaderCodeWatch_[projectPath];
        if (!entry.seen) {
            // First sighting: record the mtime without recompiling. The shader is
            // either already compiled or has never been used.
            entry.modifiedTime = modifiedTime;
            entry.seen = true;
            continue;
        }
        if (entry.modifiedTime != modifiedTime) {
            entry.modifiedTime = modifiedTime;
            entry.pendingSince = now;
            entry.pending = true;
            continue;
        }
        if (entry.pending && now - entry.pendingSince >= kShaderCodeWatchDebounceSeconds) {
            entry.pending = false;
            RecompileShaderCodeAsset(projectPath, false);
        }
    }
}

void SceneEditor::RenderShaderCodeAssetInspector() {
    const std::string projectPath = selectedProjectFile_;
    if (projectPath.empty()) {
        return;
    }
    const fs::path absolutePath = ProjectAssetPathToAbsolute(projectPath);
    const std::string filename = fs::path(projectPath).filename().string();

    ImGui::TextUnformatted(filename.c_str());
    ImGui::TextDisabled("%s", ShaderStageLabel(projectPath).c_str());
    ImGui::TextDisabled("%s", projectPath.c_str());
    ImGui::Separator();

    const bool generated = IsGeneratedShaderAssetPath(projectPath);
    if (generated) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.35f, 1.0f));
        ImGui::TextWrapped("Generated by Shader Graph - edits here are overwritten the next time that graph is saved.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    if (ImGui::Button("Open in IDE")) {
        if (OpenProjectAssetInDefaultEditor(projectPath)) {
            if (console_) console_->AddLog("Opened shader: " + projectPath);
        } else if (console_) {
            console_->AddError("Failed to open shader: " + projectPath);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(generated);
    if (ImGui::Button("Recompile")) {
        RecompileShaderCodeAsset(projectPath, false);
    }
    ImGui::EndDisabled();

    ImGui::Spacing();

    const std::string statusKey = FragmentPeerOfShaderSource(projectPath);
    auto statusIt = shaderCodeStatus_.find(projectPath);
    if (statusIt == shaderCodeStatus_.end()) {
        statusIt = shaderCodeStatus_.find(statusKey);
    }
    if (statusIt == shaderCodeStatus_.end()) {
        ImGui::TextDisabled("Status: not compiled yet");
    } else if (statusIt->second.compiled) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "Status: Compiled");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "Status: Compile Error");
        if (!statusIt->second.log.empty()) {
            ImGui::BeginChild("##ShaderCodeErrors", ImVec2(0.0f, 140.0f), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.58f, 1.0f));
            ImGui::TextUnformatted(statusIt->second.log.c_str());
            ImGui::PopStyleColor();
            ImGui::EndChild();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Which materials would be affected by an edit to this file.
    if (!generated) {
        const std::string shaderId = ShaderRegistry::MakeCodeShaderId(statusKey);
        std::vector<std::string> users;
        for (const std::string& file : projectFiles_) {
            if (!IsMaterialAssetPath(file)) {
                continue;
            }
            const std::string materialId = MaterialIdFromAssetPath(file);
            const Material* material = materialManager_.Get(materialId);
            if (material != nullptr && ShaderRegistry::NormalizeShaderId(material->shader) == shaderId) {
                users.push_back(materialId);
            }
        }
        if (users.empty()) {
            ImGui::TextDisabled("Not used by any material");
        } else {
            ImGui::Text("Used by %d material%s", static_cast<int>(users.size()), users.size() == 1 ? "" : "s");
            ImGui::Indent();
            for (const std::string& user : users) {
                ImGui::TextDisabled("%s", user.c_str());
            }
            ImGui::Unindent();
        }
        ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Source (read-only)")) {
        std::error_code ec;
        const fs::file_time_type modifiedTime = fs::last_write_time(absolutePath, ec);
        if (shaderCodePreviewPath_ != projectPath || (!ec && shaderCodePreviewModifiedTime_ != modifiedTime)) {
            shaderCodePreviewPath_ = projectPath;
            shaderCodePreviewModifiedTime_ = modifiedTime;
            shaderCodePreviewText_.clear();
            std::ifstream in(absolutePath, std::ios::binary);
            if (in.good()) {
                std::ostringstream buffer;
                buffer << in.rdbuf();
                shaderCodePreviewText_ = buffer.str();
                if (shaderCodePreviewText_.size() > kShaderCodePreviewByteLimit) {
                    shaderCodePreviewText_.resize(kShaderCodePreviewByteLimit);
                    shaderCodePreviewText_ += "\n... (truncated)";
                }
            } else {
                shaderCodePreviewText_ = "(could not read file)";
            }
        }
        ImGui::InputTextMultiline("##ShaderCodeSource",
                                  shaderCodePreviewText_.data(),
                                  shaderCodePreviewText_.size() + 1,
                                  ImVec2(-1.0f, 320.0f),
                                  ImGuiInputTextFlags_ReadOnly);
    }
}

} // namespace raceman

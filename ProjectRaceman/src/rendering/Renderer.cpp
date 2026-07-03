#include "Renderer.h"
#include "shader.h"
#include "ShaderRegistry.h"

#include <glad/glad.h>

#include <algorithm>
#include <filesystem>
#include <glm/gtc/matrix_inverse.hpp>
#include <stdexcept>

namespace raceman {

namespace {

std::string ShaderPathForId(const ShaderDefinition& definition, bool vertex) {
    return vertex ? definition.vertexPath : definition.fragmentPath;
}

} // namespace

Renderer::Renderer(const RendererConfig& config) : config_(config) {
    viewport_.width = config.width;
    viewport_.height = config.height;
    InitializePipelines();
    InitializeQuad();
    BakeBrdfLut();
}

Renderer::~Renderer() {
    if (fullscreenQuad_ != 0) {
        glDeleteVertexArrays(1, &fullscreenQuad_);
    }
    if (lineVbo_ != 0) {
        glDeleteBuffers(1, &lineVbo_);
    }
    if (lineVao_ != 0) {
        glDeleteVertexArrays(1, &lineVao_);
    }
    if (captureFbo_ != 0) {
        glDeleteFramebuffers(1, &captureFbo_);
    }
    if (captureRbo_ != 0) {
        glDeleteRenderbuffers(1, &captureRbo_);
    }

    if (environmentMaps_.irradiance != 0) {
        glDeleteTextures(1, &environmentMaps_.irradiance);
    }
    if (environmentMaps_.prefiltered != 0) {
        glDeleteTextures(1, &environmentMaps_.prefiltered);
    }
    if (environmentMaps_.brdfLut != 0) {
        glDeleteTextures(1, &environmentMaps_.brdfLut);
    }

    if (!shadowMaps_.empty()) {
        glDeleteTextures(static_cast<GLsizei>(shadowMaps_.size()), shadowMaps_.data());
    }

    DestroyViewportTarget(sceneViewportTarget_);
    DestroyViewportTarget(gameViewportTarget_);
}

void Renderer::BeginFrame() {
    const int viewportX = (std::max)(0, viewport_.x);
    const int viewportWidth = (std::max)(1, viewport_.width);
    const int viewportHeight = (std::max)(1, viewport_.height);
    const int viewportY = (std::max)(0, config_.height - viewport_.y - viewportHeight);

    glEnable(GL_SCISSOR_TEST);
    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
    glScissor(viewportX, viewportY, viewportWidth, viewportHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(settings_.clearColor.r, settings_.clearColor.g, settings_.clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::EndFrame() { Flush(); }

void Renderer::ResetFrameStats() {
    frameStats_ = {};
}

void Renderer::Resize(int width, int height) {
    config_.width = (std::max)(1, width);
    config_.height = (std::max)(1, height);
    viewport_.x = 0;
    viewport_.y = 0;
    viewport_.width = config_.width;
    viewport_.height = config_.height;
}

void Renderer::SetViewport(const RendererViewport& viewport) {
    viewport_.x = (std::max)(0, viewport.x);
    viewport_.y = (std::max)(0, viewport.y);
    viewport_.width = (std::max)(1, viewport.width);
    viewport_.height = (std::max)(1, viewport.height);
}

void Renderer::EnsureViewportRenderTarget(ViewportRenderTarget target, int width, int height) {
    ViewportTarget& renderTarget = GetViewportTarget(target);
    width = (std::max)(1, width);
    height = (std::max)(1, height);

    if (renderTarget.framebuffer == 0) {
        glGenFramebuffers(1, &renderTarget.framebuffer);
    }
    if (renderTarget.colorTexture == 0) {
        glGenTextures(1, &renderTarget.colorTexture);
    }
    if (renderTarget.depthRenderbuffer == 0) {
        glGenRenderbuffers(1, &renderTarget.depthRenderbuffer);
    }

    if (renderTarget.width == width && renderTarget.height == height) {
        return;
    }

    renderTarget.width = width;
    renderTarget.height = height;

    glBindTexture(GL_TEXTURE_2D, renderTarget.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindRenderbuffer(GL_RENDERBUFFER, renderTarget.depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.colorTexture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, renderTarget.depthRenderbuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::BeginFrameToViewportTarget(ViewportRenderTarget target, const glm::vec3& clearColor) {
    const ViewportTarget& renderTarget = GetViewportTarget(target);
    if (renderTarget.framebuffer == 0 || renderTarget.width <= 0 || renderTarget.height <= 0) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.framebuffer);
    glEnable(GL_SCISSOR_TEST);
    glViewport(0, 0, renderTarget.width, renderTarget.height);
    glScissor(0, 0, renderTarget.width, renderTarget.height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::EndFrameToViewportTarget() {
    Flush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned int Renderer::GetViewportRenderTargetTexture(ViewportRenderTarget target) const {
    return GetViewportTarget(target).colorTexture;
}

void Renderer::SetupEnvironment(const std::string& hdrPath) {
    (void)hdrPath;
    // Placeholder for actual HDR environment capture.
    // In a real implementation, this would load the HDR texture, create cube maps and
    // precompute irradiance and specular prefiltered maps.
}

void Renderer::BakeBrdfLut() {
    if (environmentMaps_.brdfLut == 0) {
        glGenTextures(1, &environmentMaps_.brdfLut);
        glBindTexture(GL_TEXTURE_2D, environmentMaps_.brdfLut);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    if (captureFbo_ == 0) {
        glGenFramebuffers(1, &captureFbo_);
    }
    if (captureRbo_ == 0) {
        glGenRenderbuffers(1, &captureRbo_);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRbo_);

    // A real implementation would bind the BRDF integration shader and render a quad.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::CreateShadowMaps(int resolution) {
    unsigned int shadowMap = 0;
    glGenTextures(1, &shadowMap);
    glBindTexture(GL_TEXTURE_2D, shadowMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, resolution, resolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    // Attach to FBO as needed per-light; omitted for brevity.
    shadowMaps_.push_back(shadowMap);
}

void Renderer::DestroyViewportTarget(ViewportTarget& target) {
    if (target.depthRenderbuffer != 0) {
        glDeleteRenderbuffers(1, &target.depthRenderbuffer);
        target.depthRenderbuffer = 0;
    }
    if (target.colorTexture != 0) {
        glDeleteTextures(1, &target.colorTexture);
        target.colorTexture = 0;
    }
    if (target.framebuffer != 0) {
        glDeleteFramebuffers(1, &target.framebuffer);
        target.framebuffer = 0;
    }
    target.width = 0;
    target.height = 0;
}

Renderer::ViewportTarget& Renderer::GetViewportTarget(ViewportRenderTarget target) {
    return target == ViewportRenderTarget::Game ? gameViewportTarget_ : sceneViewportTarget_;
}

const Renderer::ViewportTarget& Renderer::GetViewportTarget(ViewportRenderTarget target) const {
    return target == ViewportRenderTarget::Game ? gameViewportTarget_ : sceneViewportTarget_;
}

void Renderer::SubmitMesh(const MeshDrawCommand& cmd) {
    drawList_.push_back(cmd);
    ++frameStats_.submittedMeshCount;
    frameStats_.submittedTriangleCount += cmd.indexCount / 3;
}

void Renderer::SubmitLight(const LightDrawCommand& cmd) {
    lightDrawList_.push_back(cmd);
    ++frameStats_.submittedLightCount;
}

void Renderer::SubmitLine(const DebugLineCommand& cmd) { lineDrawList_.push_back(cmd); }

void Renderer::Flush() {
    auto resolveShader = [&](const std::string& shaderId) -> Shader* {
        const std::string normalized = ShaderRegistry::NormalizeShaderId(shaderId);
        std::string cacheKey = normalized;
        std::string vertexPath;
        std::string fragmentPath;
        if (ShaderRegistry::IsGraphShaderId(normalized)) {
            cacheKey = normalized;
            vertexPath = "src/shaders/default/default.vs";
            fragmentPath = ShaderRegistry::GraphFragmentPathForShaderId(normalized);
        } else {
            const ShaderDefinition& definition = ShaderRegistry::Resolve(normalized);
            vertexPath = ShaderPathForId(definition, true);
            fragmentPath = ShaderPathForId(definition, false);
        }
        if (fragmentPath.empty() || !std::filesystem::exists(fragmentPath)) {
            cacheKey = "pbr";
            vertexPath = "src/shaders/default/default.vs";
            fragmentPath = "src/shaders/default/pbr.fs";
        }
        auto it = materialShaders_.find(cacheKey);
        if (it != materialShaders_.end()) {
            return it->second.get();
        }
        auto shader = std::make_unique<Shader>(vertexPath.c_str(), fragmentPath.c_str());
        Shader* result = shader.get();
        materialShaders_[cacheKey] = std::move(shader);
        return result;
    };

    // Draw editor meshes as real scene geometry, while preserving caller GL state.
    GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthWriteMask = GL_TRUE;
    GLint depthFunc = GL_LESS;
    GLint cullFaceMode = GL_BACK;
    GLint frontFaceMode = GL_CCW;
    GLint blendSrcRgb = GL_ONE;
    GLint blendDstRgb = GL_ZERO;
    GLint blendSrcAlpha = GL_ONE;
    GLint blendDstAlpha = GL_ZERO;
    GLint blendEquationRgb = GL_FUNC_ADD;
    GLint blendEquationAlpha = GL_FUNC_ADD;
    GLfloat lineWidth = 1.0f;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);
    glGetIntegerv(GL_FRONT_FACE, &frontFaceMode);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
    glGetFloatv(GL_LINE_WIDTH, &lineWidth);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Sort draw calls to minimize GPU state changes (texture binds, VAO switches).
    if (settings_.enableDrawCallSorting && drawList_.size() > 1) {
        std::sort(drawList_.begin(), drawList_.end(), [](const MeshDrawCommand& a, const MeshDrawCommand& b) {
            if (a.shaderId != b.shaderId) return a.shaderId < b.shaderId;
            if (a.diffuseTextureId != b.diffuseTextureId) return a.diffuseTextureId < b.diffuseTextureId;
            if (a.vao != b.vao) return a.vao < b.vao;
            return a.materialId < b.materialId;
        });
    }

    auto bindCommonUniforms = [&](Shader& shader) {
        shader.use();
        shader.setInt("uDiffuseTexture", 0);
        shader.setInt("uMaterialAlbedoTexture", 0);
        shader.setInt("uMaterialNormalTexture", 1);
        shader.setInt("uMaterialMetallicTexture", 2);
        shader.setInt("uMaterialRoughnessTexture", 3);
        shader.setInt("uMaterialAoTexture", 4);
        shader.setVec3("uAmbientColor", settings_.ambientColor);
        shader.setVec3("uCameraPosition", glm::vec3(glm::inverse(view_)[3]));
    };
    const int lightCount = static_cast<int>((std::min)(lightDrawList_.size(), static_cast<std::size_t>(8)));
    auto bindLights = [&](Shader& shader) {
        shader.setInt("uLightCount", lightCount);
        for (int i = 0; i < lightCount; ++i) {
            const LightDrawCommand& light = lightDrawList_[static_cast<std::size_t>(i)];
            const std::string prefix = "uLights[" + std::to_string(i) + "].";
            int type = 1;
            if (light.type == RenderLightType::Directional) {
                type = 0;
            } else if (light.type == RenderLightType::Spot) {
                type = 2;
            }
            shader.setInt(prefix + "type", type);
            shader.setVec3(prefix + "position", light.position);
            shader.setVec3(prefix + "direction", glm::length(light.direction) > 0.0001f ? glm::normalize(light.direction) : glm::vec3{0.0f, -1.0f, 0.0f});
            shader.setVec3(prefix + "color", light.color);
            shader.setFloat(prefix + "intensity", light.intensity);
            shader.setFloat(prefix + "range", (std::max)(0.001f, light.range));
            shader.setFloat(prefix + "spotAngleDegrees", (std::max)(1.0f, (std::min)(179.0f, light.spotAngleDegrees)));
        }
    };
    std::string boundShaderId;
    for (const auto& cmd : drawList_) {
        Shader* activeShader = resolveShader(cmd.unlit ? std::string("unlit") : cmd.shaderId);
        if (activeShader == nullptr) {
            continue;
        }
        const std::string currentShaderId = cmd.unlit ? std::string("unlit") : ShaderRegistry::NormalizeShaderId(cmd.shaderId);
        if (boundShaderId != currentShaderId) {
            bindCommonUniforms(*activeShader);
            bindLights(*activeShader);
            boundShaderId = currentShaderId;
        } else {
            activeShader->use();
        }

        const bool transparent = ShaderRegistry::Resolve(currentShaderId).transparent || cmd.color.a < 0.999f;
        if (transparent) {
            glEnable(GL_BLEND);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }

        // MVP = proj * view * model
        glm::mat4 mvp = proj_ * view_ * cmd.modelMatrix;
        activeShader->setMat4("uMVP", mvp);
        activeShader->setMat4("uModel", cmd.modelMatrix);
        // Per-object color
        activeShader->setVec4("uColor", cmd.color);
        activeShader->setVec3("uEmissiveColor", cmd.emissiveColor);
        activeShader->setFloat("uMetallic", (std::max)(0.0f, (std::min)(1.0f, cmd.metallic)));
        activeShader->setFloat("uRoughness", (std::max)(0.02f, (std::min)(1.0f, cmd.roughness)));
        activeShader->setVec2("uUvTiling", cmd.uvTiling);
        activeShader->setVec2("uUvOffset", cmd.uvOffset);
        activeShader->setBool("uUseDiffuseTexture", cmd.useDiffuseTexture && cmd.diffuseTextureId != 0);
        activeShader->setBool("uUseMaterialAlbedoTexture", cmd.materialTextureIds[0] != 0);
        activeShader->setBool("uUseMaterialNormalTexture", cmd.materialTextureIds[1] != 0);
        activeShader->setBool("uUseMaterialMetallicTexture", cmd.materialTextureIds[2] != 0);
        activeShader->setBool("uUseMaterialRoughnessTexture", cmd.materialTextureIds[3] != 0);
        activeShader->setBool("uUseMaterialAoTexture", cmd.materialTextureIds[4] != 0);
        for (int textureIndex = 0; textureIndex < static_cast<int>(cmd.materialTextureIds.size()); ++textureIndex) {
            if (cmd.materialTextureIds[static_cast<std::size_t>(textureIndex)] != 0) {
                glActiveTexture(GL_TEXTURE0 + textureIndex);
                glBindTexture(GL_TEXTURE_2D, cmd.materialTextureIds[static_cast<std::size_t>(textureIndex)]);
            }
        }
        int nextMaterialTextureUnit = 5;
        for (const MeshDrawCommand::MaterialUniform& uniform : cmd.materialUniforms) {
            if (uniform.uniformName.empty()) {
                continue;
            }
            const MaterialPropertyValue& value = uniform.value;
            switch (value.type) {
            case MaterialPropertyType::Float:
                activeShader->setFloat(uniform.uniformName, value.values[0]);
                break;
            case MaterialPropertyType::Vec2:
                activeShader->setVec2(uniform.uniformName, value.values[0], value.values[1]);
                break;
            case MaterialPropertyType::Vec3:
                activeShader->setVec3(uniform.uniformName, value.values[0], value.values[1], value.values[2]);
                break;
            case MaterialPropertyType::Vec4:
                activeShader->setVec4(uniform.uniformName, value.values[0], value.values[1], value.values[2], value.values[3]);
                break;
            case MaterialPropertyType::Bool:
                activeShader->setBool(uniform.uniformName, value.boolValue);
                break;
            case MaterialPropertyType::Texture2D:
                activeShader->setBool(uniform.textureUseUniform, uniform.textureId != 0);
                activeShader->setInt(uniform.uniformName, nextMaterialTextureUnit);
                if (uniform.textureId != 0) {
                    glActiveTexture(GL_TEXTURE0 + nextMaterialTextureUnit);
                    glBindTexture(GL_TEXTURE_2D, uniform.textureId);
                }
                ++nextMaterialTextureUnit;
                break;
            }
        }
        if (cmd.useDiffuseTexture && cmd.diffuseTextureId != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, cmd.diffuseTextureId);
        }

        glBindVertexArray(cmd.vao);
        glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT, nullptr);
        ++frameStats_.drawCallCount;
    }
    glBindVertexArray(0);
    drawList_.clear();
    lightDrawList_.clear();

    if (!lineDrawList_.empty()) {
        if (lineVao_ == 0) {
            glGenVertexArrays(1, &lineVao_);
            glGenBuffers(1, &lineVbo_);
            glBindVertexArray(lineVao_);
            glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
            glBindVertexArray(0);
        }

        Shader* lineShader = resolveShader("unlit");
        lineShader->use();
        lineShader->setMat4("uMVP", proj_ * view_);
        lineShader->setMat4("uModel", glm::mat4(1.0f));
        lineShader->setVec2("uUvTiling", glm::vec2(1.0f));
        lineShader->setVec2("uUvOffset", glm::vec2(0.0f));
        lineShader->setVec3("uEmissiveColor", glm::vec3(0.0f));
        lineShader->setFloat("uMetallic", 0.0f);
        lineShader->setFloat("uRoughness", 1.0f);
        lineShader->setBool("uUseDiffuseTexture", false);
        glBindVertexArray(lineVao_);

        auto sameLineStyle = [](const DebugLineCommand& a, const DebugLineCommand& b) {
            return a.depthMode == b.depthMode
                && a.width == b.width
                && a.color.x == b.color.x
                && a.color.y == b.color.y
                && a.color.z == b.color.z
                && a.color.w == b.color.w;
        };

        auto drawLineBatch = [&](DebugLineDepthMode mode, bool overlayPass) {
            if (mode == DebugLineDepthMode::AlwaysOnTop || overlayPass) {
                glDisable(GL_DEPTH_TEST);
            } else {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
            }
            glDisable(GL_CULL_FACE);
            glDepthMask(GL_FALSE);
            if (overlayPass) {
                glEnable(GL_BLEND);
                glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            } else {
                glDisable(GL_BLEND);
            }

            std::vector<glm::vec3> vertices;
            vertices.reserve(lineDrawList_.size() * 2);
            for (std::size_t i = 0; i < lineDrawList_.size();) {
                const DebugLineCommand& first = lineDrawList_[i];
                if (first.depthMode != mode) {
                    ++i;
                    continue;
                }

                vertices.clear();
                glm::vec4 color = first.color;
                if (overlayPass) {
                    color.a *= 0.25f;
                }
                const float width = first.width;

                std::size_t j = i;
                for (; j < lineDrawList_.size(); ++j) {
                    const DebugLineCommand& cmd = lineDrawList_[j];
                    if (!sameLineStyle(cmd, first)) {
                        break;
                    }
                    vertices.push_back(cmd.start);
                    vertices.push_back(cmd.end);
                }

                if (vertices.empty()) {
                    i = j;
                    continue;
                }

                glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
                const std::size_t vertexCount = vertices.size();
                const std::size_t requiredBytes = vertexCount * sizeof(glm::vec3);
                if (vertexCount > lineVertexCapacity_) {
                    glBufferData(GL_ARRAY_BUFFER, requiredBytes, vertices.data(), GL_DYNAMIC_DRAW);
                    lineVertexCapacity_ = vertexCount;
                } else {
                    glBufferSubData(GL_ARRAY_BUFFER, 0, requiredBytes, vertices.data());
                }
                glLineWidth(width);
                lineShader->setVec4("uColor", color);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertexCount));
                i = j;
            }
        };

        drawLineBatch(DebugLineDepthMode::DepthTested, false);
        drawLineBatch(DebugLineDepthMode::DepthTestedOverlay, false);
        drawLineBatch(DebugLineDepthMode::DepthTestedOverlay, true);
        drawLineBatch(DebugLineDepthMode::AlwaysOnTop, false);
        glBindVertexArray(0);
        lineDrawList_.clear();
    }

    // Restore previous state
    glLineWidth(lineWidth);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(depthWriteMask);
    glDepthFunc(depthFunc);
    glCullFace(cullFaceMode);
    glFrontFace(frontFaceMode);
    glBlendEquationSeparate(blendEquationRgb, blendEquationAlpha);
    glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);
    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

const RendererConfig& Renderer::GetConfig() const { return config_; }

void Renderer::SetCamera(const glm::mat4& view, const glm::mat4& proj) {
    view_ = view;
    proj_ = proj;
}

void Renderer::InitializePipelines() {
    materialShaders_["pbr"] = std::make_unique<Shader>("src/shaders/default/default.vs", "src/shaders/default/pbr.fs");
}

void Renderer::InitializeQuad() {
    if (fullscreenQuad_ == 0) {
        glGenVertexArrays(1, &fullscreenQuad_);
    }
}

} // namespace raceman

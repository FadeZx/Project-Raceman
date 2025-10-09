#include "Renderer.h"
#include "shader.h"

#include <glad/glad.h>

#include <stdexcept>

namespace raceman {

Renderer::Renderer(const RendererConfig& config) : config_(config) {
    InitializePipelines();
    InitializeQuad();
    BakeBrdfLut();
}

Renderer::~Renderer() {
    if (fullscreenQuad_ != 0) {
        glDeleteVertexArrays(1, &fullscreenQuad_);
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
}

void Renderer::BeginFrame() {
    glViewport(0, 0, config_.width, config_.height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(settings_.clearColor.r, settings_.clearColor.g, settings_.clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::EndFrame() { Flush(); }

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
    if (!settings_.enableShadows) {
        if (!shadowMaps_.empty()) {
            glDeleteTextures(static_cast<GLsizei>(shadowMaps_.size()), shadowMaps_.data());
            shadowMaps_.clear();
        }
        return;
    }

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

void Renderer::SubmitMesh(const MeshDrawCommand& cmd) { drawList_.push_back(cmd); }

void Renderer::Flush() {
    if (!simpleShader_) {
        // Fallback: create simple shader once
        simpleShader_ = std::make_unique<Shader>("src/shaders/simple/simple.vs", "src/shaders/simple/simple.fs");
    }

    // Force a safe, visible state for editor draws
    GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    simpleShader_->use();
    for (const auto& cmd : drawList_) {
        // MVP = proj * view * model
        glm::mat4 mvp = proj_ * view_ * cmd.modelMatrix;
        simpleShader_->setMat4("uMVP", mvp);
        // Bright color for visibility
        simpleShader_->setVec4("uColor", glm::vec4(1.0f, 0.2f, 0.2f, 1.0f));

        glBindVertexArray(cmd.vao);
        glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    drawList_.clear();

    // Restore previous state
    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}

const RendererConfig& Renderer::GetConfig() const { return config_; }

void Renderer::SetCamera(const glm::mat4& view, const glm::mat4& proj) {
    view_ = view;
    proj_ = proj;
}

void Renderer::InitializePipelines() {
    // Initialize shader programs, samplers, and render states required for PBR.
    // These are placeholders; actual shader compilation is left to the integration layer.
    // Prepare simple shader eagerly to avoid hiccup on first draw.
    if (!simpleShader_) {
        simpleShader_ = std::make_unique<Shader>("src/shaders/simple/simple.vs", "src/shaders/simple/simple.fs");
    }
}

void Renderer::InitializeQuad() {
    if (fullscreenQuad_ == 0) {
        glGenVertexArrays(1, &fullscreenQuad_);
    }
}

} // namespace raceman

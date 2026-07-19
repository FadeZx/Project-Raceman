#include "Renderer.h"
#include "shader.h"
#include "ShaderRegistry.h"

#include <glad/glad.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <stdexcept>

namespace raceman {

namespace {

std::string ShaderPathForId(const ShaderDefinition& definition, bool vertex) {
    return vertex ? definition.vertexPath : definition.fragmentPath;
}

float Halton(std::uint32_t index, std::uint32_t base) {
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

double SteadySeconds() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

} // namespace

Renderer::Renderer(const RendererConfig& config) : config_(config) {
    viewport_.width = config.width;
    viewport_.height = config.height;
    InitializePipelines();
    InitializeQuad();
    InitializeSsaoResources();
    BakeBrdfLut();
}

Renderer::~Renderer() {
    for (const auto& [path, texture] : reflectionProbeCubemapCache_) {
        (void)path;
        if (texture != 0) glDeleteTextures(1, &texture);
    }
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
    if (captureCubeVbo_ != 0) glDeleteBuffers(1, &captureCubeVbo_);
    if (captureCubeVao_ != 0) glDeleteVertexArrays(1, &captureCubeVao_);

    if (environmentMaps_.irradiance != 0) {
        glDeleteTextures(1, &environmentMaps_.irradiance);
    }
    if (environmentMaps_.prefiltered != 0) {
        glDeleteTextures(1, &environmentMaps_.prefiltered);
    }
    if (environmentMaps_.brdfLut != 0) {
        glDeleteTextures(1, &environmentMaps_.brdfLut);
    }

    if (directionalShadowMap_ != 0) glDeleteTextures(1, &directionalShadowMap_);
    if (directionalShadowFramebuffer_ != 0) glDeleteFramebuffers(1, &directionalShadowFramebuffer_);
    if (spotShadowMap_ != 0) glDeleteTextures(1, &spotShadowMap_);
    if (pointShadowMap_ != 0) glDeleteTextures(1, &pointShadowMap_);
    if (localShadowFramebuffer_ != 0) glDeleteFramebuffers(1, &localShadowFramebuffer_);
    if (ssaoNoiseTexture_ != 0) glDeleteTextures(1, &ssaoNoiseTexture_);

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
    glClearColor(settings_.editorClearColor.r, settings_.editorClearColor.g, settings_.editorClearColor.b, 1.0f);
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
    renderTarget.requestedWidth = width;
    renderTarget.requestedHeight = height;
    if (settings_.profile.dynamicResolution) {
        const float minimumScale = (std::clamp)(settings_.profile.minimumResolutionScale, 0.5f, 1.0f);
        renderTarget.resolutionScale = (std::clamp)(renderTarget.resolutionScale, minimumScale, 1.0f);
    } else {
        renderTarget.resolutionScale = 1.0f;
    }
    width = (std::max)(1, static_cast<int>(std::lround(static_cast<float>(width) * renderTarget.resolutionScale)));
    height = (std::max)(1, static_cast<int>(std::lround(static_cast<float>(height) * renderTarget.resolutionScale)));
    int ssaoResolutionDivisor = 2;
    if (settings_.profile.quality == GraphicsQualityTier::Low) ssaoResolutionDivisor = 4;
    if (settings_.profile.quality == GraphicsQualityTier::Ultra) ssaoResolutionDivisor = 1;
    const int desiredSsaoWidth = (std::max)(1, width / ssaoResolutionDivisor);
    const int desiredSsaoHeight = (std::max)(1, height / ssaoResolutionDivisor);

    if (renderTarget.framebuffer == 0) {
        glGenFramebuffers(1, &renderTarget.framebuffer);
    }
    if (renderTarget.hdrColorTexture == 0) {
        glGenTextures(1, &renderTarget.hdrColorTexture);
    }
    if (renderTarget.depthTexture == 0) {
        glGenTextures(1, &renderTarget.depthTexture);
    }
    if (renderTarget.normalTexture == 0) {
        glGenTextures(1, &renderTarget.normalTexture);
    }
    if (renderTarget.ambientTexture == 0) {
        glGenTextures(1, &renderTarget.ambientTexture);
    }
    if (renderTarget.materialTexture == 0) {
        glGenTextures(1, &renderTarget.materialTexture);
    }
    if (renderTarget.compositeFramebuffer == 0) {
        glGenFramebuffers(1, &renderTarget.compositeFramebuffer);
    }
    if (renderTarget.compositeTexture == 0) {
        glGenTextures(1, &renderTarget.compositeTexture);
    }
    if (renderTarget.ssrFramebuffer == 0) glGenFramebuffers(1, &renderTarget.ssrFramebuffer);
    if (renderTarget.ssrTexture == 0) glGenTextures(1, &renderTarget.ssrTexture);
    if (renderTarget.velocityFramebuffer == 0) glGenFramebuffers(1, &renderTarget.velocityFramebuffer);
    if (renderTarget.velocityTexture == 0) glGenTextures(1, &renderTarget.velocityTexture);
    if (renderTarget.motionBlurFramebuffer == 0) glGenFramebuffers(1, &renderTarget.motionBlurFramebuffer);
    if (renderTarget.motionBlurTexture == 0) glGenTextures(1, &renderTarget.motionBlurTexture);
    if (renderTarget.weatherFramebuffer == 0) glGenFramebuffers(1, &renderTarget.weatherFramebuffer);
    if (renderTarget.weatherTexture == 0) glGenTextures(1, &renderTarget.weatherTexture);
    if (renderTarget.depthOfFieldFramebuffer == 0) glGenFramebuffers(1, &renderTarget.depthOfFieldFramebuffer);
    if (renderTarget.depthOfFieldTexture == 0) glGenTextures(1, &renderTarget.depthOfFieldTexture);
    if (renderTarget.taaFramebuffers[0] == 0) glGenFramebuffers(2, renderTarget.taaFramebuffers.data());
    if (renderTarget.taaHistoryTextures[0] == 0) glGenTextures(2, renderTarget.taaHistoryTextures.data());
    if (renderTarget.taaSurfaceHistoryTextures[0] == 0) glGenTextures(2, renderTarget.taaSurfaceHistoryTextures.data());
    if (renderTarget.outputFramebuffer == 0) {
        glGenFramebuffers(1, &renderTarget.outputFramebuffer);
    }
    if (renderTarget.colorTexture == 0) {
        glGenTextures(1, &renderTarget.colorTexture);
    }
    if (renderTarget.hdrOutputFramebuffer == 0) {
        glGenFramebuffers(1, &renderTarget.hdrOutputFramebuffer);
    }
    if (renderTarget.hdrOutputTexture == 0) {
        glGenTextures(1, &renderTarget.hdrOutputTexture);
    }
    if (renderTarget.bloomFramebuffers[0] == 0) {
        glGenFramebuffers(static_cast<GLsizei>(renderTarget.bloomFramebuffers.size()), renderTarget.bloomFramebuffers.data());
    }
    if (renderTarget.bloomTextures[0] == 0) {
        glGenTextures(static_cast<GLsizei>(renderTarget.bloomTextures.size()), renderTarget.bloomTextures.data());
    }
    if (renderTarget.ssaoFramebuffer == 0) {
        glGenFramebuffers(1, &renderTarget.ssaoFramebuffer);
    }
    if (renderTarget.ssaoTexture == 0) {
        glGenTextures(1, &renderTarget.ssaoTexture);
    }
    if (renderTarget.ssaoBlurFramebuffer == 0) {
        glGenFramebuffers(1, &renderTarget.ssaoBlurFramebuffer);
    }
    if (renderTarget.ssaoBlurTexture == 0) {
        glGenTextures(1, &renderTarget.ssaoBlurTexture);
    }

    if (renderTarget.width == width && renderTarget.height == height &&
        renderTarget.ssaoWidth == desiredSsaoWidth && renderTarget.ssaoHeight == desiredSsaoHeight) {
        return;
    }

    renderTarget.width = width;
    renderTarget.height = height;
    renderTarget.bloomWidth = (std::max)(1, width / 2);
    renderTarget.bloomHeight = (std::max)(1, height / 2);
    renderTarget.ssaoWidth = desiredSsaoWidth;
    renderTarget.ssaoHeight = desiredSsaoHeight;
    renderTarget.hasPreviousViewProjection = false;
    renderTarget.previousModelMatrices.clear();
    renderTarget.taaHistoryValid = false;
    renderTarget.taaWriteIndex = 0;
    renderTarget.taaFrameIndex = 0;
    renderTarget.taaCurrentJitterUv = glm::vec2(0.0f);
    renderTarget.taaPreviousJitterUv = glm::vec2(0.0f);

    glBindTexture(GL_TEXTURE_2D, renderTarget.hdrColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, renderTarget.depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height,
                 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, renderTarget.normalTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, renderTarget.ambientTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, renderTarget.materialTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.hdrColorTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, renderTarget.normalTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, renderTarget.ambientTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, renderTarget.materialTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, renderTarget.depthTexture, 0);
    const GLenum sceneDrawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
    glDrawBuffers(static_cast<GLsizei>(sizeof(sceneDrawBuffers) / sizeof(sceneDrawBuffers[0])), sceneDrawBuffers);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create viewport color/depth/normal framebuffer");
    }

    glBindTexture(GL_TEXTURE_2D, renderTarget.compositeTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.compositeFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.compositeTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create SSAO composite framebuffer");
    }

    glBindTexture(GL_TEXTURE_2D, renderTarget.ssrTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.ssrFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.ssrTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create screen-space reflection framebuffer");
    }

    glBindTexture(GL_TEXTURE_2D, renderTarget.velocityTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, width, height, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.velocityFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.velocityTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, renderTarget.depthTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create motion-vector framebuffer");
    }

    glBindTexture(GL_TEXTURE_2D, renderTarget.motionBlurTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.motionBlurFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.motionBlurTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create motion-blur framebuffer");
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        glBindTexture(GL_TEXTURE_2D, renderTarget.taaHistoryTextures[historyIndex]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.taaFramebuffers[historyIndex]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.taaHistoryTextures[historyIndex], 0);
        glBindTexture(GL_TEXTURE_2D, renderTarget.taaSurfaceHistoryTextures[historyIndex]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, renderTarget.taaSurfaceHistoryTextures[historyIndex], 0);
        const GLenum taaDrawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, taaDrawBuffers);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            throw std::runtime_error("Failed to create temporal anti-aliasing history framebuffer");
        }
    }

    glBindTexture(GL_TEXTURE_2D, renderTarget.weatherTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.weatherFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.weatherTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create weather framebuffer");
    }

    glBindTexture(GL_TEXTURE_2D, renderTarget.depthOfFieldTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.depthOfFieldFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.depthOfFieldTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create depth-of-field framebuffer");
    }

    glBindTexture(GL_TEXTURE_2D, renderTarget.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.outputFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.colorTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create SDR output framebuffer");
    }

    glBindTexture(GL_TEXTURE_2D, renderTarget.hdrOutputTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.hdrOutputFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.hdrOutputTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create HDR scRGB output framebuffer");
    }

    for (std::size_t i = 0; i < renderTarget.bloomTextures.size(); ++i) {
        glBindTexture(GL_TEXTURE_2D, renderTarget.bloomTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                     renderTarget.bloomWidth, renderTarget.bloomHeight,
                     0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.bloomFramebuffers[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.bloomTextures[i], 0);
    }

    glBindTexture(GL_TEXTURE_2D, renderTarget.ssaoTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F,
                 renderTarget.ssaoWidth, renderTarget.ssaoHeight,
                 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.ssaoFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.ssaoTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create SSAO framebuffer");
    }

    glBindTexture(GL_TEXTURE_2D, renderTarget.ssaoBlurTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F,
                 renderTarget.ssaoWidth, renderTarget.ssaoHeight,
                 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.ssaoBlurFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget.ssaoBlurTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Failed to create SSAO blur framebuffer");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::BeginFrameToViewportTarget(ViewportRenderTarget target, const glm::vec3& clearColor) {
    ViewportTarget& renderTarget = GetViewportTarget(target);
    if (renderTarget.framebuffer == 0 || renderTarget.width <= 0 || renderTarget.height <= 0) {
        return;
    }

    const double nowSeconds = SteadySeconds();
    if (renderTarget.lastFrameBeginSeconds > 0.0) {
        const float frameTimeMs = static_cast<float>((nowSeconds - renderTarget.lastFrameBeginSeconds) * 1000.0);
        if (frameTimeMs > 1.0f && frameTimeMs < 250.0f) {
            renderTarget.smoothedFrameTimeMs = renderTarget.smoothedFrameTimeMs <= 0.0f
                ? frameTimeMs
                : renderTarget.smoothedFrameTimeMs * 0.90f + frameTimeMs * 0.10f;
            if (settings_.profile.dynamicResolution) {
                if (renderTarget.resolutionAdjustmentCooldown > 0) --renderTarget.resolutionAdjustmentCooldown;
                if (renderTarget.resolutionAdjustmentCooldown <= 0) {
                    const float targetFrameMs = 1000.0f / static_cast<float>((std::clamp)(settings_.profile.dynamicResolutionTargetFps, 30, 240));
                    const float minimumScale = (std::clamp)(settings_.profile.minimumResolutionScale, 0.5f, 1.0f);
                    float newScale = renderTarget.resolutionScale;
                    if (renderTarget.smoothedFrameTimeMs > targetFrameMs * 1.06f) newScale -= 0.0625f;
                    else if (renderTarget.smoothedFrameTimeMs < targetFrameMs * 0.85f) newScale += 0.0625f;
                    newScale = (std::clamp)(newScale, minimumScale, 1.0f);
                    if (std::abs(newScale - renderTarget.resolutionScale) > 0.001f) {
                        renderTarget.resolutionScale = newScale;
                        renderTarget.resolutionAdjustmentCooldown = 30;
                    }
                }
            }
        }
    }
    renderTarget.lastFrameBeginSeconds = nowSeconds;

    activeViewportTarget_ = target;
    viewportTargetActive_ = true;
    if (settings_.profile.antiAliasing == AntiAliasingMode::TAA) {
        ++renderTarget.taaFrameIndex;
    } else {
        renderTarget.taaHistoryValid = false;
        renderTarget.taaFrameIndex = 0;
    }
    SetCamera(view_, unjitteredProj_);

    glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.framebuffer);
    glEnable(GL_SCISSOR_TEST);
    glViewport(0, 0, renderTarget.width, renderTarget.height);
    glScissor(0, 0, renderTarget.width, renderTarget.height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const GLfloat invalidNormal[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 1, invalidNormal);
    const GLfloat noAmbient[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 2, noAmbient);
    const GLfloat noMaterial[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 3, noMaterial);
}

void Renderer::EndFrameToViewportTarget() {
    Flush();
    if (viewportTargetActive_) {
        ResolveViewportTarget(GetViewportTarget(activeViewportTarget_));
    }
    viewportTargetActive_ = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::PresentViewportTarget(ViewportRenderTarget target, const RendererViewport& destination) {
    const ViewportTarget& renderTarget = GetViewportTarget(target);
    if (renderTarget.outputFramebuffer == 0) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, renderTarget.outputFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    const int destinationY = (std::max)(0, config_.height - destination.y - destination.height);
    glBlitFramebuffer(0, 0, renderTarget.width, renderTarget.height,
                      destination.x, destinationY,
                      destination.x + destination.width, destinationY + destination.height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned int Renderer::GetViewportRenderTargetTexture(ViewportRenderTarget target) const {
    return GetViewportTarget(target).colorTexture;
}

unsigned int Renderer::GetViewportHdrOutputTexture(ViewportRenderTarget target) const {
    return settings_.profile.hdr ? GetViewportTarget(target).hdrOutputTexture : 0;
}

unsigned int Renderer::GetViewportDepthTexture(ViewportRenderTarget target) const {
    return GetViewportTarget(target).depthTexture;
}

unsigned int Renderer::GetViewportNormalTexture(ViewportRenderTarget target) const {
    return GetViewportTarget(target).normalTexture;
}

unsigned int Renderer::GetViewportSsaoTexture(ViewportRenderTarget target) const {
    const ViewportTarget& viewportTarget = GetViewportTarget(target);
    return viewportTarget.ssaoBlurTexture != 0 ? viewportTarget.ssaoBlurTexture : viewportTarget.ssaoTexture;
}

float Renderer::GetDynamicResolutionScale(ViewportRenderTarget target) const {
    return GetViewportTarget(target).resolutionScale;
}

void Renderer::RenderCaptureCube() const {
    if (captureCubeVao_ == 0) return;
    glBindVertexArray(captureCubeVao_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void Renderer::SetupEnvironment(unsigned int sourceCubemap) {
    environmentReady_ = false;
    environmentAverageLuminance_ = 0.0f;
    environmentMaps_.source = sourceCubemap;
    if (sourceCubemap == 0 || !irradianceShader_ || !prefilterShader_) return;

    if (captureFbo_ == 0) glGenFramebuffers(1, &captureFbo_);
    if (captureRbo_ == 0) glGenRenderbuffers(1, &captureRbo_);

    if (environmentMaps_.irradiance == 0) glGenTextures(1, &environmentMaps_.irradiance);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMaps_.irradiance);
    for (unsigned int face = 0; face < 6; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB16F,
                     32, 32, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    constexpr int prefilterSize = 128;
    constexpr int prefilterMipCount = 5;
    if (environmentMaps_.prefiltered == 0) glGenTextures(1, &environmentMaps_.prefiltered);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMaps_.prefiltered);
    for (int mip = 0; mip < prefilterMipCount; ++mip) {
        const int mipSize = (std::max)(1, prefilterSize >> mip);
        for (unsigned int face = 0; face < 6; ++face) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, GL_RGB16F,
                         mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, prefilterMipCount - 1);

    const glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const std::array<glm::mat4, 6> captureViews = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    };

    GLint previousFramebuffer = 0;
    GLint previousViewport[4]{};
    GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRbo_);

    irradianceShader_->use();
    irradianceShader_->setInt("environmentMap", 0);
    irradianceShader_->setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sourceCubemap);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 32, 32);
    glViewport(0, 0, 32, 32);
    for (unsigned int face = 0; face < 6; ++face) {
        irradianceShader_->setMat4("view", captureViews[face]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                               environmentMaps_.irradiance, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderCaptureCube();
    }

    prefilterShader_->use();
    prefilterShader_->setInt("environmentMap", 0);
    prefilterShader_->setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sourceCubemap);
    GLint sourceResolution = 1;
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &sourceResolution);
    prefilterShader_->setFloat("sourceResolution", static_cast<float>((std::max)(1, sourceResolution)));
    for (int mip = 0; mip < prefilterMipCount; ++mip) {
        const int mipSize = (std::max)(1, prefilterSize >> mip);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);
        glViewport(0, 0, mipSize, mipSize);
        prefilterShader_->setFloat("roughness", static_cast<float>(mip) / static_cast<float>(prefilterMipCount - 1));
        for (unsigned int face = 0; face < 6; ++face) {
            prefilterShader_->setMat4("view", captureViews[face]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                   environmentMaps_.prefiltered, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            RenderCaptureCube();
        }
    }

    const GLenum environmentFramebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    const bool framebufferComplete = environmentFramebufferStatus == GL_FRAMEBUFFER_COMPLETE;
    std::array<float, 32 * 32 * 3> irradiancePixels{};
    glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMaps_.irradiance);
    glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGB, GL_FLOAT, irradiancePixels.data());
    double luminanceSum = 0.0;
    std::size_t validPixelCount = 0;
    for (std::size_t pixel = 0; pixel < 32 * 32; ++pixel) {
        const float r = irradiancePixels[pixel * 3 + 0];
        const float g = irradiancePixels[pixel * 3 + 1];
        const float b = irradiancePixels[pixel * 3 + 2];
        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) continue;
        luminanceSum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        ++validPixelCount;
    }
    if (validPixelCount > 0) {
        environmentAverageLuminance_ = static_cast<float>(luminanceSum / static_cast<double>(validPixelCount));
    }
    environmentReady_ = framebufferComplete && environmentMaps_.brdfLut != 0 &&
        environmentAverageLuminance_ > 0.00001f;
    std::cout << "[Renderer] IBL environment: "
              << (environmentReady_ ? "baked" : "source mip fallback")
              << ", source face " << sourceResolution << " px"
              << ", irradiance " << environmentAverageLuminance_
              << ", framebuffer 0x" << std::hex << environmentFramebufferStatus << std::dec
              << ", brdf " << environmentMaps_.brdfLut << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}

unsigned int Renderer::LoadReflectionProbeCubemap(const std::string& path) {
    if (path.empty()) return 0;
    const std::string key = std::filesystem::path(path).lexically_normal().string();
    if (const auto cached = reflectionProbeCubemapCache_.find(key); cached != reflectionProbeCubemapCache_.end()) {
        return cached->second;
    }

    std::ifstream input(key, std::ios::binary);
    char magic[8]{};
    std::uint32_t version = 0;
    std::uint32_t resolution = 0;
    std::uint32_t channels = 0;
    input.read(magic, sizeof(magic));
    input.read(reinterpret_cast<char*>(&version), sizeof(version));
    input.read(reinterpret_cast<char*>(&resolution), sizeof(resolution));
    input.read(reinterpret_cast<char*>(&channels), sizeof(channels));
    if (!input || std::string(magic, sizeof(magic)) != "RCUBE001" || version != 1 ||
        channels != 3 || resolution < 16 || resolution > 2048) {
        std::cerr << "[Renderer] Invalid reflection probe cubemap: " << key << std::endl;
        return 0;
    }

    const std::size_t faceValueCount = static_cast<std::size_t>(resolution) * resolution * channels;
    std::vector<float> pixels(faceValueCount);
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    for (unsigned int face = 0; face < 6; ++face) {
        input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size() * sizeof(float)));
        if (!input) {
            glDeleteTextures(1, &texture);
            std::cerr << "[Renderer] Truncated reflection probe cubemap: " << key << std::endl;
            return 0;
        }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB16F,
                     static_cast<GLsizei>(resolution), static_cast<GLsizei>(resolution),
                     0, GL_RGB, GL_FLOAT, pixels.data());
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    reflectionProbeCubemapCache_[key] = texture;
    return texture;
}

bool Renderer::BakeReflectionProbe(const glm::vec3& position,
                                   int resolution,
                                   const std::string& outputPath,
                                   const std::function<void()>& submitScene,
                                   const std::function<void(int, int)>& progress) {
    if (!submitScene || outputPath.empty()) return false;
    resolution = (std::clamp)(resolution, 16, 1024);
    std::error_code directoryError;
    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path(), directoryError);
    if (directoryError) return false;

    GLint previousFramebuffer = 0;
    GLint previousViewport[4]{};
    GLint previousScissorBox[4]{};
    GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean previousDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean previousCull = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    const glm::mat4 savedView = view_;
    const glm::mat4 savedProjection = unjitteredProj_;
    const RendererViewport savedRendererViewport = viewport_;
    const bool savedViewportTargetActive = viewportTargetActive_;

    unsigned int cubemap = 0;
    glGenTextures(1, &cubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
    for (unsigned int face = 0; face < 6; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB16F,
                     resolution, resolution, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (captureFbo_ == 0) glGenFramebuffers(1, &captureFbo_);
    if (captureRbo_ == 0) glGenRenderbuffers(1, &captureRbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, resolution, resolution);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRbo_);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, resolution, resolution);
    viewport_ = {0, 0, resolution, resolution};
    viewportTargetActive_ = false;

    const glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 1000.0f);
    const std::array<glm::mat4, 6> captureViews = {
        glm::lookAt(position, position + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
        glm::lookAt(position, position + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
        glm::lookAt(position, position + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
        glm::lookAt(position, position + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
        glm::lookAt(position, position + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
        glm::lookAt(position, position + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0))};

    bool complete = true;
    for (unsigned int face = 0; face < 6; ++face) {
        glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemap, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            complete = false;
            break;
        }
        SetCamera(captureViews[face], captureProjection);
        glViewport(0, 0, resolution, resolution);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(settings_.profile.ambientColor.r, settings_.profile.ambientColor.g,
                     settings_.profile.ambientColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (environmentMaps_.source != 0 && reflectionCaptureSkyShader_) {
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            reflectionCaptureSkyShader_->use();
            reflectionCaptureSkyShader_->setInt("environmentMap", 0);
            reflectionCaptureSkyShader_->setMat4("projection", captureProjection);
            reflectionCaptureSkyShader_->setMat4("view", captureViews[face]);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMaps_.source);
            RenderCaptureCube();
        }
        submitScene();
        Flush();
        if (progress) progress(static_cast<int>(face) + 1, 6);
    }

    if (complete) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        const char magic[8] = {'R','C','U','B','E','0','0','1'};
        const std::uint32_t version = 1;
        const std::uint32_t storedResolution = static_cast<std::uint32_t>(resolution);
        const std::uint32_t channels = 3;
        output.write(magic, sizeof(magic));
        output.write(reinterpret_cast<const char*>(&version), sizeof(version));
        output.write(reinterpret_cast<const char*>(&storedResolution), sizeof(storedResolution));
        output.write(reinterpret_cast<const char*>(&channels), sizeof(channels));
        std::vector<float> pixels(static_cast<std::size_t>(resolution) * resolution * channels);
        for (unsigned int face = 0; face < 6 && output; ++face) {
            glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB, GL_FLOAT, pixels.data());
            output.write(reinterpret_cast<const char*>(pixels.data()),
                         static_cast<std::streamsize>(pixels.size() * sizeof(float)));
        }
        complete = static_cast<bool>(output);
    }

    SetCamera(savedView, savedProjection);
    viewport_ = savedRendererViewport;
    viewportTargetActive_ = savedViewportTargetActive;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
    if (previousScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (previousDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (previousCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);

    const std::string key = std::filesystem::path(outputPath).lexically_normal().string();
    if (complete) {
        if (const auto existing = reflectionProbeCubemapCache_.find(key); existing != reflectionProbeCubemapCache_.end()) {
            if (existing->second != 0) glDeleteTextures(1, &existing->second);
            existing->second = cubemap;
        } else {
            reflectionProbeCubemapCache_[key] = cubemap;
        }
    } else if (cubemap != 0) {
        glDeleteTextures(1, &cubemap);
    }
    return complete;
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

    if (brdfShader_ && fullscreenQuad_ != 0) {
        GLint previousFramebuffer = 0;
        GLint previousViewport[4]{};
        GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        glDisable(GL_DEPTH_TEST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, environmentMaps_.brdfLut, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glViewport(0, 0, 512, 512);
        glClear(GL_COLOR_BUFFER_BIT);
        brdfShader_->use();
        glBindVertexArray(fullscreenQuad_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::CreateShadowMaps(int resolution, int cascadeCount) {
    resolution = (std::max)(256, resolution);
    cascadeCount = (std::clamp)(cascadeCount, 1, 4);
    if (directionalShadowFramebuffer_ == 0) glGenFramebuffers(1, &directionalShadowFramebuffer_);
    if (directionalShadowMap_ == 0) glGenTextures(1, &directionalShadowMap_);
    if (directionalShadowResolution_ == resolution && directionalShadowCascadeCount_ == cascadeCount) return;

    GLint previousFramebuffer = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &previousTexture);
    directionalShadowResolution_ = resolution;
    directionalShadowCascadeCount_ = cascadeCount;
    glBindTexture(GL_TEXTURE_2D_ARRAY, directionalShadowMap_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
        resolution, resolution, cascadeCount, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
    glBindFramebuffer(GL_FRAMEBUFFER, directionalShadowFramebuffer_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, directionalShadowMap_, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(previousTexture));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
}

void Renderer::CreateLocalShadowMaps(int spotResolution, int spotCount, int pointResolution, int pointCount) {
    spotCount = (std::clamp)(spotCount, 0, 4);
    pointCount = (std::clamp)(pointCount, 0, 4);
    if (localShadowFramebuffer_ == 0) glGenFramebuffers(1, &localShadowFramebuffer_);
    if (spotCount > 0 && spotShadowMap_ == 0) glGenTextures(1, &spotShadowMap_);
    if (pointCount > 0 && pointShadowMap_ == 0) glGenTextures(1, &pointShadowMap_);

    if (spotCount > 0 && (spotShadowResolution_ != spotResolution || spotShadowLayerCount_ != spotCount)) {
        spotShadowResolution_ = spotResolution;
        spotShadowLayerCount_ = spotCount;
        glBindTexture(GL_TEXTURE_2D_ARRAY, spotShadowMap_);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
            spotResolution, spotResolution, spotCount, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
    }
    if (pointCount > 0 && (pointShadowResolution_ != pointResolution || pointShadowLightCount_ != pointCount)) {
        pointShadowResolution_ = pointResolution;
        pointShadowLightCount_ = pointCount;
        glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, pointShadowMap_);
        glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT32F,
            pointResolution, pointResolution, pointCount * 6, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, 0);
}

void Renderer::DestroyViewportTarget(ViewportTarget& target) {
    if (target.ssrFramebuffer != 0) glDeleteFramebuffers(1, &target.ssrFramebuffer);
    if (target.ssrTexture != 0) glDeleteTextures(1, &target.ssrTexture);
    target.ssrFramebuffer = target.ssrTexture = 0;
    if (target.depthOfFieldFramebuffer != 0) glDeleteFramebuffers(1, &target.depthOfFieldFramebuffer);
    if (target.depthOfFieldTexture != 0) glDeleteTextures(1, &target.depthOfFieldTexture);
    target.depthOfFieldFramebuffer = target.depthOfFieldTexture = 0;
    if (target.weatherFramebuffer != 0) glDeleteFramebuffers(1, &target.weatherFramebuffer);
    if (target.weatherTexture != 0) glDeleteTextures(1, &target.weatherTexture);
    target.weatherFramebuffer = target.weatherTexture = 0;
    if (target.taaFramebuffers[0] != 0) glDeleteFramebuffers(2, target.taaFramebuffers.data());
    if (target.taaHistoryTextures[0] != 0) glDeleteTextures(2, target.taaHistoryTextures.data());
    if (target.taaSurfaceHistoryTextures[0] != 0) glDeleteTextures(2, target.taaSurfaceHistoryTextures.data());
    target.taaFramebuffers = {0, 0};
    target.taaHistoryTextures = {0, 0};
    target.taaSurfaceHistoryTextures = {0, 0};
    target.taaHistoryValid = false;
    if (target.motionBlurFramebuffer != 0) glDeleteFramebuffers(1, &target.motionBlurFramebuffer);
    if (target.motionBlurTexture != 0) glDeleteTextures(1, &target.motionBlurTexture);
    if (target.velocityFramebuffer != 0) glDeleteFramebuffers(1, &target.velocityFramebuffer);
    if (target.velocityTexture != 0) glDeleteTextures(1, &target.velocityTexture);
    target.motionBlurFramebuffer = target.motionBlurTexture = 0;
    target.velocityFramebuffer = target.velocityTexture = 0;
    if (target.ssaoBlurFramebuffer != 0) {
        glDeleteFramebuffers(1, &target.ssaoBlurFramebuffer);
        target.ssaoBlurFramebuffer = 0;
    }
    if (target.ssaoBlurTexture != 0) {
        glDeleteTextures(1, &target.ssaoBlurTexture);
        target.ssaoBlurTexture = 0;
    }
    if (target.ssaoFramebuffer != 0) {
        glDeleteFramebuffers(1, &target.ssaoFramebuffer);
        target.ssaoFramebuffer = 0;
    }
    if (target.ssaoTexture != 0) {
        glDeleteTextures(1, &target.ssaoTexture);
        target.ssaoTexture = 0;
    }
    if (target.bloomFramebuffers[0] != 0) {
        glDeleteFramebuffers(static_cast<GLsizei>(target.bloomFramebuffers.size()), target.bloomFramebuffers.data());
        target.bloomFramebuffers = {0, 0};
    }
    if (target.bloomTextures[0] != 0) {
        glDeleteTextures(static_cast<GLsizei>(target.bloomTextures.size()), target.bloomTextures.data());
        target.bloomTextures = {0, 0};
    }
    if (target.depthTexture != 0) {
        glDeleteTextures(1, &target.depthTexture);
        target.depthTexture = 0;
    }
    if (target.normalTexture != 0) {
        glDeleteTextures(1, &target.normalTexture);
        target.normalTexture = 0;
    }
    if (target.ambientTexture != 0) {
        glDeleteTextures(1, &target.ambientTexture);
        target.ambientTexture = 0;
    }
    if (target.materialTexture != 0) {
        glDeleteTextures(1, &target.materialTexture);
        target.materialTexture = 0;
    }
    if (target.compositeFramebuffer != 0) {
        glDeleteFramebuffers(1, &target.compositeFramebuffer);
        target.compositeFramebuffer = 0;
    }
    if (target.compositeTexture != 0) {
        glDeleteTextures(1, &target.compositeTexture);
        target.compositeTexture = 0;
    }
    if (target.outputFramebuffer != 0) {
        glDeleteFramebuffers(1, &target.outputFramebuffer);
        target.outputFramebuffer = 0;
    }
    if (target.colorTexture != 0) {
        glDeleteTextures(1, &target.colorTexture);
        target.colorTexture = 0;
    }
    if (target.hdrOutputFramebuffer != 0) {
        glDeleteFramebuffers(1, &target.hdrOutputFramebuffer);
        target.hdrOutputFramebuffer = 0;
    }
    if (target.hdrOutputTexture != 0) {
        glDeleteTextures(1, &target.hdrOutputTexture);
        target.hdrOutputTexture = 0;
    }
    if (target.hdrColorTexture != 0) {
        glDeleteTextures(1, &target.hdrColorTexture);
        target.hdrColorTexture = 0;
    }
    if (target.framebuffer != 0) {
        glDeleteFramebuffers(1, &target.framebuffer);
        target.framebuffer = 0;
    }
    target.width = 0;
    target.height = 0;
    target.bloomWidth = 0;
    target.bloomHeight = 0;
    target.ssaoWidth = 0;
    target.ssaoHeight = 0;
}

void Renderer::ResolveViewportTarget(ViewportTarget& target) {
    if (target.outputFramebuffer == 0 || target.hdrColorTexture == 0 || toneMapShader_ == nullptr) {
        return;
    }

    const glm::mat4 currentViewProjection = proj_ * view_;
    const bool needsVelocity = settings_.profile.motionBlur || settings_.profile.antiAliasing == AntiAliasingMode::TAA;
    const bool velocityReady = needsVelocity && motionVectorShader_ && target.velocityFramebuffer != 0 &&
        target.velocityTexture != 0 && target.depthTexture != 0;
    if (velocityReady) {
        glBindFramebuffer(GL_FRAMEBUFFER, target.velocityFramebuffer);
        glViewport(0, 0, target.width, target.height);
        const GLfloat zeroVelocity[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        glClearBufferfv(GL_COLOR, 0, zeroVelocity);
        const glm::mat4 previousViewProjection = target.hasPreviousViewProjection
            ? target.previousViewProjection : currentViewProjection;
        if (cameraVelocityShader_) {
            // Detach depth while sampling it to avoid a framebuffer texture feedback loop.
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
            glDisable(GL_DEPTH_TEST);
            cameraVelocityShader_->use();
            cameraVelocityShader_->setInt("uDepthTexture", 0);
            cameraVelocityShader_->setMat4("uInverseCurrentViewProjection", glm::inverse(currentViewProjection));
            cameraVelocityShader_->setMat4("uPreviousViewProjection", previousViewProjection);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, target.depthTexture);
            glBindVertexArray(fullscreenQuad_);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, target.depthTexture, 0);
        }
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        motionVectorShader_->use();
        for (const MeshDrawCommand& cmd : motionVectorDrawList_) {
            if (cmd.transparent || cmd.color.a < 0.999f) continue;
            const auto previousIt = target.previousModelMatrices.find(cmd.motionId);
            const glm::mat4 previousModel = !cmd.motionId.empty() && previousIt != target.previousModelMatrices.end()
                ? previousIt->second : cmd.modelMatrix;
            motionVectorShader_->setMat4("uCurrentMVP", currentViewProjection * cmd.modelMatrix);
            motionVectorShader_->setMat4("uPreviousMVP", previousViewProjection * previousModel);
            const bool mirrored = glm::determinant(glm::mat3(cmd.modelMatrix)) < 0.0f;
            glFrontFace(mirrored ? GL_CW : GL_CCW);
            if (cmd.doubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            glBindVertexArray(cmd.vao);
            glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT, nullptr);
            if (!cmd.motionId.empty()) target.previousModelMatrices[cmd.motionId] = cmd.modelMatrix;
        }
        target.previousViewProjection = currentViewProjection;
        target.hasPreviousViewProjection = true;
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        glFrontFace(GL_CCW);
    } else {
        // Do not retain stale transforms while temporal effects are disabled.
        target.hasPreviousViewProjection = false;
        target.previousModelMatrices.clear();
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindVertexArray(fullscreenQuad_);

    const bool ssaoReady = settings_.profile.ssao && ssaoShader_ && ssaoBlurShader_ &&
        ssaoCompositeShader_ && ssaoNoiseTexture_ != 0 && target.ssaoFramebuffer != 0 &&
        target.ssaoTexture != 0 && target.ssaoBlurFramebuffer != 0 && target.ssaoBlurTexture != 0 &&
        target.compositeFramebuffer != 0 && target.compositeTexture != 0 && target.ambientTexture != 0;
    if (ssaoReady) {
        int sampleCount = 24;
        switch (settings_.profile.quality) {
        case GraphicsQualityTier::Low: sampleCount = 8; break;
        case GraphicsQualityTier::Medium: sampleCount = 16; break;
        case GraphicsQualityTier::Ultra: sampleCount = 32; break;
        case GraphicsQualityTier::High: break;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, target.ssaoFramebuffer);
        glViewport(0, 0, target.ssaoWidth, target.ssaoHeight);
        ssaoShader_->use();
        ssaoShader_->setInt("uDepthTexture", 0);
        ssaoShader_->setInt("uNormalTexture", 1);
        ssaoShader_->setInt("uNoiseTexture", 2);
        ssaoShader_->setInt("uSampleCount", sampleCount);
        ssaoShader_->setFloat("uRadius", (std::clamp)(settings_.profile.ssaoRadius, 0.05f, 5.0f));
        ssaoShader_->setFloat("uBias", (std::clamp)(settings_.profile.ssaoBias, 0.001f, 0.2f));
        ssaoShader_->setMat4("uProjection", proj_);
        ssaoShader_->setMat4("uInverseProjection", glm::inverse(proj_));
        ssaoShader_->setMat4("uView", view_);
        ssaoShader_->setVec2("uNoiseScale",
            static_cast<float>(target.ssaoWidth) / 4.0f,
            static_cast<float>(target.ssaoHeight) / 4.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, target.depthTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, target.normalTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture_);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindFramebuffer(GL_FRAMEBUFFER, target.ssaoBlurFramebuffer);
        glViewport(0, 0, target.ssaoWidth, target.ssaoHeight);
        ssaoBlurShader_->use();
        ssaoBlurShader_->setInt("uSsaoTexture", 0);
        ssaoBlurShader_->setInt("uDepthTexture", 1);
        ssaoBlurShader_->setInt("uNormalTexture", 2);
        ssaoBlurShader_->setVec2("uTexelSize",
            1.0f / static_cast<float>((std::max)(1, target.ssaoWidth)),
            1.0f / static_cast<float>((std::max)(1, target.ssaoHeight)));
        ssaoBlurShader_->setMat4("uInverseProjection", glm::inverse(proj_));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, target.ssaoTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, target.depthTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, target.normalTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindFramebuffer(GL_FRAMEBUFFER, target.compositeFramebuffer);
        glViewport(0, 0, target.width, target.height);
        ssaoCompositeShader_->use();
        ssaoCompositeShader_->setInt("uHdrScene", 0);
        ssaoCompositeShader_->setInt("uAmbientTexture", 1);
        ssaoCompositeShader_->setInt("uSsaoTexture", 2);
        ssaoCompositeShader_->setInt("uNormalTexture", 3);
        ssaoCompositeShader_->setFloat("uIntensity", (std::clamp)(settings_.profile.ssaoIntensity, 0.0f, 3.0f));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, target.hdrColorTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, target.ambientTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, target.ssaoBlurTexture);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, target.normalTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    unsigned int postProcessSceneTexture = ssaoReady ? target.compositeTexture : target.hdrColorTexture;
    const bool ssaoDebugActive = ssaoReady && settings_.profile.ssaoDebugView;
    const bool ssrReady = !ssaoDebugActive && settings_.profile.reflections &&
        settings_.profile.screenSpaceReflections && settings_.profile.iblDebugMode == 0 && ssrShader_ &&
        target.ssrFramebuffer != 0 && target.ssrTexture != 0 && target.materialTexture != 0;
    if (ssrReady) {
        glBindFramebuffer(GL_FRAMEBUFFER, target.ssrFramebuffer);
        glViewport(0, 0, target.width, target.height);
        ssrShader_->use();
        ssrShader_->setInt("uSceneTexture", 0);
        ssrShader_->setInt("uDepthTexture", 1);
        ssrShader_->setInt("uNormalTexture", 2);
        ssrShader_->setInt("uMaterialTexture", 3);
        ssrShader_->setMat4("uProjection", proj_);
        ssrShader_->setMat4("uInverseProjection", glm::inverse(proj_));
        ssrShader_->setMat4("uView", view_);
        ssrShader_->setVec2("uTexelSize", 1.0f / static_cast<float>(target.width), 1.0f / static_cast<float>(target.height));
        ssrShader_->setFloat("uIntensity", (std::clamp)(settings_.profile.ssrIntensity, 0.0f, 2.0f));
        ssrShader_->setFloat("uMaxDistance", (std::clamp)(settings_.profile.ssrMaxDistance, 1.0f, 200.0f));
        ssrShader_->setFloat("uThickness", (std::clamp)(settings_.profile.ssrThickness, 0.01f, 2.0f));
        ssrShader_->setInt("uMaxSteps", (std::clamp)(settings_.profile.ssrSteps, 8, 96));
        ssrShader_->setBool("uDebugView", settings_.profile.ssrDebugView);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postProcessSceneTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, target.depthTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, target.normalTexture);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, target.materialTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        postProcessSceneTexture = target.ssrTexture;
    }
    const bool ssrDebugActive = ssrReady && settings_.profile.ssrDebugView;
    const bool spatialDebugActive = ssaoDebugActive || ssrDebugActive;
    const bool depthOfFieldReady = !spatialDebugActive && settings_.profile.depthOfField && depthOfFieldShader_ &&
        target.depthOfFieldFramebuffer != 0 && target.depthOfFieldTexture != 0;
    if (depthOfFieldReady) {
        int sampleCount = 12;
        switch (settings_.profile.quality) {
        case GraphicsQualityTier::Low: sampleCount = 6; break;
        case GraphicsQualityTier::Medium: sampleCount = 8; break;
        case GraphicsQualityTier::Ultra: sampleCount = 16; break;
        case GraphicsQualityTier::High: break;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, target.depthOfFieldFramebuffer);
        glViewport(0, 0, target.width, target.height);
        depthOfFieldShader_->use();
        depthOfFieldShader_->setInt("uSceneTexture", 0);
        depthOfFieldShader_->setInt("uDepthTexture", 1);
        depthOfFieldShader_->setMat4("uInverseProjection", glm::inverse(proj_));
        depthOfFieldShader_->setVec2("uTexelSize", 1.0f / static_cast<float>(target.width), 1.0f / static_cast<float>(target.height));
        depthOfFieldShader_->setFloat("uFocusDistance", (std::max)(0.01f, settings_.profile.depthOfFieldFocusDistance));
        depthOfFieldShader_->setFloat("uFocusRange", (std::max)(0.01f, settings_.profile.depthOfFieldFocusRange));
        depthOfFieldShader_->setFloat("uMaxRadiusPixels", (std::clamp)(settings_.profile.depthOfFieldMaxRadius, 0.5f, 24.0f));
        depthOfFieldShader_->setInt("uSampleCount", sampleCount);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postProcessSceneTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, target.depthTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        postProcessSceneTexture = target.depthOfFieldTexture;
    }
    const bool taaReady = !spatialDebugActive && settings_.profile.antiAliasing == AntiAliasingMode::TAA &&
        velocityReady && taaShader_ && target.taaFramebuffers[0] != 0 && target.taaHistoryTextures[0] != 0 &&
        target.taaSurfaceHistoryTextures[0] != 0;
    if (taaReady) {
        const glm::mat4 cameraWorld = glm::inverse(view_);
        const glm::vec3 cameraPosition = glm::vec3(cameraWorld[3]);
        const glm::vec3 cameraForward = glm::normalize(-glm::vec3(cameraWorld[2]));
        if (target.taaHistoryValid) {
            const float cameraTravel = glm::length(cameraPosition - target.taaPreviousCameraPosition);
            const float facingSimilarity = glm::dot(cameraForward, target.taaPreviousCameraForward);
            if (cameraTravel > 5.0f || facingSimilarity < 0.5f) target.taaHistoryValid = false;
        }

        const int writeIndex = target.taaWriteIndex;
        const int readIndex = 1 - writeIndex;
        glBindFramebuffer(GL_FRAMEBUFFER, target.taaFramebuffers[writeIndex]);
        glViewport(0, 0, target.width, target.height);
        taaShader_->use();
        taaShader_->setInt("uCurrentTexture", 0);
        taaShader_->setInt("uHistoryTexture", 1);
        taaShader_->setInt("uVelocityTexture", 2);
        taaShader_->setInt("uDepthTexture", 3);
        taaShader_->setInt("uHistorySurfaceTexture", 4);
        taaShader_->setInt("uNormalTexture", 5);
        taaShader_->setMat4("uInverseProjection", glm::inverse(proj_));
        taaShader_->setVec2("uCurrentJitterUv", target.taaCurrentJitterUv.x, target.taaCurrentJitterUv.y);
        taaShader_->setVec2("uPreviousJitterUv", target.taaPreviousJitterUv.x, target.taaPreviousJitterUv.y);
        taaShader_->setVec2("uTexelSize", 1.0f / static_cast<float>(target.width), 1.0f / static_cast<float>(target.height));
        taaShader_->setFloat("uFeedback", target.taaHistoryValid ? (std::clamp)(settings_.profile.taaFeedback, 0.0f, 0.98f) : 0.0f);
        taaShader_->setFloat("uSharpness", (std::clamp)(settings_.profile.taaSharpness, 0.0f, 1.0f));
        taaShader_->setBool("uDebugView", settings_.profile.taaDebugView);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postProcessSceneTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, target.taaHistoryTextures[readIndex]);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, target.velocityTexture);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, target.depthTexture);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, target.taaSurfaceHistoryTextures[readIndex]);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, target.normalTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        postProcessSceneTexture = target.taaHistoryTextures[writeIndex];
        target.taaWriteIndex = readIndex;
        target.taaHistoryValid = true;
        target.taaPreviousJitterUv = target.taaCurrentJitterUv;
        target.taaPreviousCameraPosition = cameraPosition;
        target.taaPreviousCameraForward = cameraForward;
    } else if (settings_.profile.antiAliasing != AntiAliasingMode::TAA) {
        target.taaHistoryValid = false;
    }
    const bool motionBlurReady = !spatialDebugActive && !(taaReady && settings_.profile.taaDebugView) &&
        settings_.profile.motionBlur && velocityReady &&
        motionBlurShader_ && target.motionBlurFramebuffer != 0 && target.motionBlurTexture != 0;
    if (motionBlurReady) {
        glBindFramebuffer(GL_FRAMEBUFFER, target.motionBlurFramebuffer);
        glViewport(0, 0, target.width, target.height);
        motionBlurShader_->use();
        motionBlurShader_->setInt("uSceneTexture", 0);
        motionBlurShader_->setInt("uVelocityTexture", 1);
        motionBlurShader_->setInt("uDepthTexture", 2);
        motionBlurShader_->setFloat("uVelocityScale",
            (std::clamp)(settings_.profile.motionBlurIntensity, 0.0f, 2.0f) *
            (std::clamp)(settings_.profile.motionBlurShutterAngle, 0.0f, 360.0f) / 180.0f);
        motionBlurShader_->setInt("uSampleCount", (std::clamp)(settings_.profile.motionBlurSamples, 4, 32));
        motionBlurShader_->setFloat("uMaxRadiusPixels", (std::clamp)(settings_.profile.motionBlurMaxRadius, 1.0f, 64.0f));
        motionBlurShader_->setFloat("uMinimumVelocityPixels",
            (std::clamp)(settings_.profile.motionBlurMinimumVelocityPixels, 0.0f, 8.0f));
        motionBlurShader_->setVec2("uResolution", static_cast<float>(target.width), static_cast<float>(target.height));
        motionBlurShader_->setBool("uDebugView", settings_.profile.motionBlurDebugView);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postProcessSceneTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, target.velocityTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, target.depthTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        postProcessSceneTexture = target.motionBlurTexture;
    }
    const bool weatherReady = !spatialDebugActive && !(taaReady && settings_.profile.taaDebugView) &&
        settings_.profile.weather && settings_.profile.weatherIntensity > 0.001f && weatherShader_ &&
        target.weatherFramebuffer != 0 && target.weatherTexture != 0;
    if (weatherReady) {
        float particleDensity = 0.0f;
        if (settings_.profile.particles) {
            switch (settings_.profile.quality) {
            case GraphicsQualityTier::Low: particleDensity = 0.35f; break;
            case GraphicsQualityTier::Medium: particleDensity = 0.60f; break;
            case GraphicsQualityTier::Ultra: particleDensity = 1.25f; break;
            case GraphicsQualityTier::High: particleDensity = 1.0f; break;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, target.weatherFramebuffer);
        glViewport(0, 0, target.width, target.height);
        weatherShader_->use();
        weatherShader_->setInt("uSceneTexture", 0);
        weatherShader_->setInt("uDepthTexture", 1);
        weatherShader_->setVec2("uResolution", static_cast<float>(target.width), static_cast<float>(target.height));
        weatherShader_->setFloat("uTime", static_cast<float>(std::fmod(SteadySeconds(), 4096.0)));
        weatherShader_->setFloat("uIntensity", (std::clamp)(settings_.profile.weatherIntensity, 0.0f, 1.0f));
        weatherShader_->setFloat("uWind", (std::clamp)(settings_.profile.weatherWind, -2.0f, 2.0f));
        weatherShader_->setFloat("uParticleDensity", particleDensity);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postProcessSceneTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, target.depthTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        postProcessSceneTexture = target.weatherTexture;
    }
    const bool postDebugActive = spatialDebugActive || (taaReady && settings_.profile.taaDebugView) ||
        (motionBlurReady && settings_.profile.motionBlurDebugView);

    const bool bloomReady = !postDebugActive && settings_.profile.bloom && bloomExtractShader_ && bloomBlurShader_ &&
        target.bloomFramebuffers[0] != 0 && target.bloomTextures[0] != 0;
    if (bloomReady) {
        glViewport(0, 0, target.bloomWidth, target.bloomHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, target.bloomFramebuffers[0]);
        bloomExtractShader_->use();
        bloomExtractShader_->setInt("uHdrScene", 0);
        bloomExtractShader_->setFloat("uThreshold", (std::max)(0.0f, settings_.profile.bloomThreshold));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postProcessSceneTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        int blurIterations = 3;
        switch (settings_.profile.quality) {
        case GraphicsQualityTier::Low: blurIterations = 1; break;
        case GraphicsQualityTier::Medium: blurIterations = 2; break;
        case GraphicsQualityTier::Ultra: blurIterations = 4; break;
        case GraphicsQualityTier::High: break;
        }
        bloomBlurShader_->use();
        bloomBlurShader_->setInt("uImage", 0);
        const float radius = (std::clamp)(settings_.profile.bloomRadius, 0.25f, 3.0f);
        for (int iteration = 0; iteration < blurIterations; ++iteration) {
            glBindFramebuffer(GL_FRAMEBUFFER, target.bloomFramebuffers[1]);
            bloomBlurShader_->setVec2("uTexelStep", radius / static_cast<float>(target.bloomWidth), 0.0f);
            glBindTexture(GL_TEXTURE_2D, target.bloomTextures[0]);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_FRAMEBUFFER, target.bloomFramebuffers[0]);
            bloomBlurShader_->setVec2("uTexelStep", 0.0f, radius / static_cast<float>(target.bloomHeight));
            glBindTexture(GL_TEXTURE_2D, target.bloomTextures[1]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    toneMapShader_->use();
    toneMapShader_->setInt("uHdrScene", 0);
    toneMapShader_->setInt("uBloomTexture", 1);
    toneMapShader_->setInt("uSsaoTexture", 2);
    toneMapShader_->setBool("uDebugSsao", ssaoDebugActive);
    toneMapShader_->setFloat("uSsaoIntensity", (std::clamp)(settings_.profile.ssaoIntensity, 0.0f, 3.0f));
    toneMapShader_->setBool("uEnableBloom", bloomReady);
    toneMapShader_->setFloat("uBloomIntensity", (std::max)(0.0f, settings_.profile.bloomIntensity));
    toneMapShader_->setFloat("uExposure", (std::max)(0.01f, settings_.profile.exposure));
    toneMapShader_->setBool("uEnableColorGrading", settings_.profile.colorGrading && !postDebugActive);
    toneMapShader_->setFloat("uSaturation", (std::clamp)(settings_.profile.colorSaturation, 0.0f, 2.0f));
    toneMapShader_->setFloat("uContrast", (std::clamp)(settings_.profile.colorContrast, 0.5f, 2.0f));
    toneMapShader_->setFloat("uTemperature", (std::clamp)(settings_.profile.colorTemperature, -1.0f, 1.0f));
    toneMapShader_->setFloat("uTint", (std::clamp)(settings_.profile.colorTint, -1.0f, 1.0f));
    toneMapShader_->setBool("uEnableVignette", settings_.profile.vignette && !postDebugActive);
    toneMapShader_->setFloat("uVignetteIntensity", (std::clamp)(settings_.profile.vignetteIntensity, 0.0f, 1.0f));
    toneMapShader_->setFloat("uVignetteSmoothness", (std::clamp)(settings_.profile.vignetteSmoothness, 0.05f, 1.0f));
    toneMapShader_->setBool("uEnableFilmGrain", settings_.profile.filmGrain && !postDebugActive);
    toneMapShader_->setFloat("uFilmGrainIntensity", (std::clamp)(settings_.profile.filmGrainIntensity, 0.0f, 0.25f));
    toneMapShader_->setFloat("uTime", static_cast<float>(std::fmod(SteadySeconds(), 4096.0)));
    const float paperWhiteNits = (std::clamp)(settings_.profile.hdrPaperWhiteNits, 80.0f, 500.0f);
    const float peakBrightnessNits = (std::clamp)(settings_.profile.hdrPeakBrightnessNits,
        paperWhiteNits, 4000.0f);
    toneMapShader_->setFloat("uHdrPaperWhiteNits", paperWhiteNits);
    toneMapShader_->setFloat("uHdrPeakBrightnessNits", peakBrightnessNits);
    toneMapShader_->setBool("uEnableFxaa", settings_.profile.antiAliasing == AntiAliasingMode::FXAA);
    toneMapShader_->setVec2("uInverseResolution",
        1.0f / static_cast<float>((std::max)(1, target.width)),
        1.0f / static_cast<float>((std::max)(1, target.height)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, postProcessSceneTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomReady ? target.bloomTextures[0] : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssaoReady ? target.ssaoBlurTexture : 0);

    glViewport(0, 0, target.width, target.height);
    if (settings_.profile.hdr && target.hdrOutputFramebuffer != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, target.hdrOutputFramebuffer);
        toneMapShader_->setInt("uOutputMode", 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    // Editor panels and the current GLFW default framebuffer are SDR. Keep a
    // display-referred preview separate from the unclamped scRGB HDR output.
    glBindFramebuffer(GL_FRAMEBUFFER, target.outputFramebuffer);
    toneMapShader_->setInt("uOutputMode", settings_.profile.hdr ? 2 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    motionVectorDrawList_.clear();
}

Renderer::ViewportTarget& Renderer::GetViewportTarget(ViewportRenderTarget target) {
    return target == ViewportRenderTarget::Game ? gameViewportTarget_ : sceneViewportTarget_;
}

const Renderer::ViewportTarget& Renderer::GetViewportTarget(ViewportRenderTarget target) const {
    return target == ViewportRenderTarget::Game ? gameViewportTarget_ : sceneViewportTarget_;
}

void Renderer::SubmitMesh(const MeshDrawCommand& cmd) {
    drawList_.push_back(cmd);
    if (viewportTargetActive_ && (settings_.profile.motionBlur || settings_.profile.antiAliasing == AntiAliasingMode::TAA)) {
        motionVectorDrawList_.push_back(cmd);
    }
    ++frameStats_.submittedMeshCount;
    frameStats_.submittedTriangleCount += cmd.indexCount / 3;
}

void Renderer::SubmitShadowCaster(const MeshDrawCommand& cmd) {
    shadowCasterList_.push_back(cmd);
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

    const glm::vec3 cameraPosition = glm::vec3(glm::inverse(view_)[3]);
    std::stable_sort(reflectionProbeDrawList_.begin(), reflectionProbeDrawList_.end(),
        [&](const ReflectionProbeDrawCommand& left, const ReflectionProbeDrawCommand& right) {
            const glm::vec3 leftOutside = (glm::max)(glm::abs(cameraPosition - left.position) - left.boxExtents, glm::vec3(0.0f));
            const glm::vec3 rightOutside = (glm::max)(glm::abs(cameraPosition - right.position) - right.boxExtents, glm::vec3(0.0f));
            const float leftOutsideDistance = glm::dot(leftOutside, leftOutside);
            const float rightOutsideDistance = glm::dot(rightOutside, rightOutside);
            if (std::abs(leftOutsideDistance - rightOutsideDistance) > 0.0001f)
                return leftOutsideDistance < rightOutsideDistance;
            const glm::vec3 leftCenterOffset = cameraPosition - left.position;
            const glm::vec3 rightCenterOffset = cameraPosition - right.position;
            return glm::dot(leftCenterOffset, leftCenterOffset) < glm::dot(rightCenterOffset, rightCenterOffset);
        });
    auto commandIsTransparent = [](const MeshDrawCommand& cmd) {
        const std::string shaderId = cmd.unlit ? std::string("unlit") : ShaderRegistry::NormalizeShaderId(cmd.shaderId);
        return cmd.transparent || ShaderRegistry::Resolve(shaderId).transparent || cmd.color.a < 0.999f;
    };
    auto commandViewDepth = [&](const MeshDrawCommand& cmd) {
        const glm::vec3 sortCenter = cmd.hasTransparentSortBounds
            ? cmd.transparentSortCenter
            : glm::vec3(cmd.modelMatrix[3]);
        const glm::vec4 viewCenter = view_ * glm::vec4(sortCenter, 1.0f);
        // Sort blended objects back-to-front by their camera-space center. Adding
        // the bounds radius here makes a large foreground object appear farther
        // away than a smaller object behind it, causing the rear object to blend
        // over the foreground glass. Bounds remain useful for culling, but their
        // size must not determine transparent draw order.
        return -viewCenter.z;
    };

    std::vector<const MeshDrawCommand*> opaqueCommands;
    std::vector<const MeshDrawCommand*> transparentCommands;
    opaqueCommands.reserve(drawList_.size());
    transparentCommands.reserve(drawList_.size());
    for (const MeshDrawCommand& cmd : drawList_) {
        if (commandIsTransparent(cmd)) {
            transparentCommands.push_back(&cmd);
        } else {
            opaqueCommands.push_back(&cmd);
        }
    }

    auto sortByState = [](const MeshDrawCommand* a, const MeshDrawCommand* b) {
        if (a->shaderId != b->shaderId) return a->shaderId < b->shaderId;
        if (a->diffuseTextureId != b->diffuseTextureId) return a->diffuseTextureId < b->diffuseTextureId;
        if (a->vao != b->vao) return a->vao < b->vao;
        return a->materialId < b->materialId;
    };
    if (settings_.enableDrawCallSorting && opaqueCommands.size() > 1) {
        std::sort(opaqueCommands.begin(), opaqueCommands.end(), sortByState);
    }
    if (transparentCommands.size() > 1) {
        std::stable_sort(transparentCommands.begin(), transparentCommands.end(), [&](const MeshDrawCommand* a, const MeshDrawCommand* b) {
            return commandViewDepth(*a) > commandViewDepth(*b);
        });
    }

    const int lightCount = static_cast<int>((std::min)(lightDrawList_.size(), static_cast<std::size_t>(8)));
    int shadowLightIndex = -1;
    for (int i = 0; i < lightCount; ++i) {
        if (lightDrawList_[static_cast<std::size_t>(i)].type == RenderLightType::Directional &&
            lightDrawList_[static_cast<std::size_t>(i)].castShadows) {
            shadowLightIndex = i;
            break;
        }
    }

    constexpr int maxShadowCascades = 4;
    std::array<glm::mat4, maxShadowCascades> directionalLightMatrices{};
    std::array<float, maxShadowCascades> directionalCascadeSplits{};
    for (glm::mat4& matrix : directionalLightMatrices) matrix = glm::mat4(1.0f);
    int directionalCascadeCount = 0;
    bool directionalShadowReady = false;
    std::array<int, 4> spotShadowLightIndices{-1, -1, -1, -1};
    std::array<int, 4> pointShadowLightIndices{-1, -1, -1, -1};
    std::array<glm::mat4, 4> spotLightMatrices{glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
    int spotShadowCount = 0;
    int pointShadowCount = 0;
    if (settings_.profile.shadows && shadowLightIndex >= 0 &&
        (!opaqueCommands.empty() || !shadowCasterList_.empty()) && shadowDepthShader_) {
        int shadowResolution = 2048;
        switch (settings_.profile.quality) {
        case GraphicsQualityTier::Low: shadowResolution = 1024; break;
        case GraphicsQualityTier::Medium: shadowResolution = 1536; break;
        case GraphicsQualityTier::Ultra: shadowResolution = 4096; break;
        case GraphicsQualityTier::High: break;
        }
        if (settings_.profile.shadowResolution > 0) {
            shadowResolution = settings_.profile.shadowResolution;
        }
        directionalCascadeCount = (std::clamp)(settings_.profile.shadowCascadeCount, 1, maxShadowCascades);
        CreateShadowMaps(shadowResolution, directionalCascadeCount);

        float cameraNear = proj_[3][2] / (proj_[2][2] - 1.0f);
        float cameraFar = proj_[3][2] / (proj_[2][2] + 1.0f);
        cameraNear = std::abs(cameraNear);
        cameraFar = std::abs(cameraFar);
        if (!std::isfinite(cameraNear) || cameraNear < 0.001f) cameraNear = 0.1f;
        if (!std::isfinite(cameraFar) || cameraFar <= cameraNear) cameraFar = 1000.0f;
        const float shadowFar = (std::min)(cameraFar,
            (std::clamp)(settings_.profile.shadowDistance, 10.0f, 1000.0f));
        constexpr float splitLambda = 0.65f;
        for (int cascade = 0; cascade < directionalCascadeCount; ++cascade) {
            const float ratio = static_cast<float>(cascade + 1) / static_cast<float>(directionalCascadeCount);
            const float logarithmic = cameraNear * std::pow(shadowFar / cameraNear, ratio);
            const float uniform = cameraNear + (shadowFar - cameraNear) * ratio;
            directionalCascadeSplits[static_cast<std::size_t>(cascade)] =
                uniform * (1.0f - splitLambda) + logarithmic * splitLambda;
        }

        const glm::mat4 inverseViewProjection = glm::inverse(proj_ * view_);
        std::array<glm::vec3, 4> cameraNearCorners{};
        std::array<glm::vec3, 4> cameraFarCorners{};
        std::size_t cornerIndex = 0;
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                glm::vec4 nearCorner = inverseViewProjection * glm::vec4(
                    x == 0 ? -1.0f : 1.0f, y == 0 ? -1.0f : 1.0f, -1.0f, 1.0f);
                glm::vec4 farCorner = inverseViewProjection * glm::vec4(
                    x == 0 ? -1.0f : 1.0f, y == 0 ? -1.0f : 1.0f, 1.0f, 1.0f);
                nearCorner /= nearCorner.w;
                farCorner /= farCorner.w;
                cameraNearCorners[cornerIndex] = glm::vec3(nearCorner);
                cameraFarCorners[cornerIndex] = glm::vec3(farCorner);
                ++cornerIndex;
            }
        }

        glm::vec3 lightDirection = lightDrawList_[static_cast<std::size_t>(shadowLightIndex)].direction;
        if (glm::length(lightDirection) < 0.0001f) lightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        lightDirection = glm::normalize(lightDirection);
        const glm::vec3 up = std::abs(glm::dot(lightDirection, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.95f
            ? glm::vec3(1.0f, 0.0f, 0.0f)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        GLint previousFramebuffer = 0;
        GLint previousViewport[4]{};
        GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
        GLboolean previousColorMask[4]{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);

        glBindFramebuffer(GL_FRAMEBUFFER, directionalShadowFramebuffer_);
        glViewport(0, 0, directionalShadowResolution_, directionalShadowResolution_);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        shadowDepthShader_->use();
        auto drawShadowCaster = [&](const MeshDrawCommand& cmd, const glm::mat4& lightMatrix) {
            const bool mirroredTransform = glm::determinant(glm::mat3(cmd.modelMatrix)) < 0.0f;
            glFrontFace(mirroredTransform ? GL_CW : GL_CCW);
            if (cmd.doubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            shadowDepthShader_->setMat4("uLightMVP", lightMatrix * cmd.modelMatrix);
            glBindVertexArray(cmd.vao);
            glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT, nullptr);
            ++frameStats_.drawCallCount;
        };
        for (int cascade = 0; cascade < directionalCascadeCount; ++cascade) {
            const float cascadeNear = cascade == 0
                ? cameraNear : directionalCascadeSplits[static_cast<std::size_t>(cascade - 1)];
            const float cascadeFar = directionalCascadeSplits[static_cast<std::size_t>(cascade)];
            const float nearRatio = (cascadeNear - cameraNear) / (cameraFar - cameraNear);
            const float farRatio = (cascadeFar - cameraNear) / (cameraFar - cameraNear);
            std::array<glm::vec3, 8> cascadeCorners{};
            for (std::size_t i = 0; i < cameraNearCorners.size(); ++i) {
                const glm::vec3 ray = cameraFarCorners[i] - cameraNearCorners[i];
                cascadeCorners[i] = cameraNearCorners[i] + ray * nearRatio;
                cascadeCorners[i + 4] = cameraNearCorners[i] + ray * farRatio;
            }

            glm::vec3 cascadeCenter{0.0f};
            for (const glm::vec3& corner : cascadeCorners) cascadeCenter += corner;
            cascadeCenter /= static_cast<float>(cascadeCorners.size());
            float cascadeRadius = 1.0f;
            for (const glm::vec3& corner : cascadeCorners) {
                cascadeRadius = (std::max)(cascadeRadius, glm::length(corner - cascadeCenter));
            }
            // Quantizing the radius and snapping the projection origin prevents
            // visible shadow-map swimming during small camera movements.
            cascadeRadius = std::ceil(cascadeRadius * 16.0f) / 16.0f;
            const glm::mat4 lightView = glm::lookAt(
                cascadeCenter - lightDirection * cascadeRadius * 3.0f, cascadeCenter, up);
            glm::mat4 lightProjection = glm::ortho(
                -cascadeRadius, cascadeRadius, -cascadeRadius, cascadeRadius,
                0.1f, cascadeRadius * 6.0f);
            glm::vec4 shadowOrigin = lightProjection * lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            shadowOrigin *= static_cast<float>(directionalShadowResolution_) * 0.5f;
            const glm::vec4 roundedOrigin = glm::round(shadowOrigin);
            glm::vec4 roundOffset = (roundedOrigin - shadowOrigin) *
                (2.0f / static_cast<float>(directionalShadowResolution_));
            roundOffset.z = 0.0f;
            roundOffset.w = 0.0f;
            lightProjection[3] += roundOffset;
            const glm::mat4 lightMatrix = lightProjection * lightView;
            directionalLightMatrices[static_cast<std::size_t>(cascade)] = lightMatrix;

            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                directionalShadowMap_, 0, cascade);
            glClear(GL_DEPTH_BUFFER_BIT);
            for (const MeshDrawCommand* cmd : opaqueCommands) drawShadowCaster(*cmd, lightMatrix);
            for (const MeshDrawCommand& cmd : shadowCasterList_) {
                // Test camera-culled meshes against this cascade's light volume.
                const glm::vec3 centerLightSpace = glm::vec3(lightView * glm::vec4(cmd.transparentSortCenter, 1.0f));
                const float boundsRadius = cmd.hasTransparentSortBounds ? cmd.transparentSortRadius : 0.0f;
                const float lightDepth = -centerLightSpace.z;
                if (std::abs(centerLightSpace.x) > cascadeRadius + boundsRadius ||
                    std::abs(centerLightSpace.y) > cascadeRadius + boundsRadius ||
                    lightDepth + boundsRadius < 0.1f ||
                    lightDepth - boundsRadius > cascadeRadius * 6.0f) continue;
                drawShadowCaster(cmd, lightMatrix);
            }
        }

        glColorMask(previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        if (previousScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        directionalShadowReady = true;
    }

    if (settings_.profile.shadows && (!opaqueCommands.empty() || !shadowCasterList_.empty()) &&
        shadowDepthShader_ && pointShadowDepthShader_) {
        const int localLimit = (std::clamp)(settings_.profile.localShadowLightLimit, 0, 4);
        for (int i = 0; i < lightCount && spotShadowCount + pointShadowCount < localLimit; ++i) {
            const LightDrawCommand& light = lightDrawList_[static_cast<std::size_t>(i)];
            if (!light.castShadows || light.type == RenderLightType::Directional) continue;
            if (light.type == RenderLightType::Spot && spotShadowCount < 4) {
                spotShadowLightIndices[static_cast<std::size_t>(spotShadowCount++)] = i;
            } else if (light.type == RenderLightType::Point && pointShadowCount < 4) {
                pointShadowLightIndices[static_cast<std::size_t>(pointShadowCount++)] = i;
            }
        }
        int spotResolution = 1024;
        int pointResolution = 512;
        switch (settings_.profile.quality) {
        case GraphicsQualityTier::Low: spotResolution = 256; pointResolution = 256; break;
        case GraphicsQualityTier::Medium: spotResolution = 512; pointResolution = 256; break;
        case GraphicsQualityTier::Ultra: spotResolution = 2048; pointResolution = 1024; break;
        case GraphicsQualityTier::High: break;
        }
        if (settings_.profile.shadowResolution > 0) {
            spotResolution = (std::clamp)(settings_.profile.shadowResolution / 2, 256, 2048);
            pointResolution = (std::clamp)(settings_.profile.shadowResolution / 4, 256, 1024);
        }
        CreateLocalShadowMaps(spotResolution, spotShadowCount, pointResolution, pointShadowCount);

        GLint previousFramebuffer = 0;
        GLint previousViewport[4]{};
        const GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
        GLboolean previousColorMask[4]{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
        glBindFramebuffer(GL_FRAMEBUFFER, localShadowFramebuffer_);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);

        auto drawLocalCaster = [&](Shader& shader, const MeshDrawCommand& cmd) {
            const bool mirrored = glm::determinant(glm::mat3(cmd.modelMatrix)) < 0.0f;
            glFrontFace(mirrored ? GL_CW : GL_CCW);
            if (cmd.doubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            glBindVertexArray(cmd.vao);
            glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT, nullptr);
            ++frameStats_.drawCallCount;
        };

        shadowDepthShader_->use();
        glViewport(0, 0, spotResolution, spotResolution);
        for (int slot = 0; slot < spotShadowCount; ++slot) {
            const LightDrawCommand& light = lightDrawList_[static_cast<std::size_t>(spotShadowLightIndices[static_cast<std::size_t>(slot)])];
            glm::vec3 direction = glm::length(light.direction) > 0.0001f ? glm::normalize(light.direction) : glm::vec3(0.0f, -1.0f, 0.0f);
            const glm::vec3 up = std::abs(glm::dot(direction, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.95f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::mat4 lightMatrix = glm::perspective(glm::radians((std::clamp)(light.spotAngleDegrees, 1.0f, 175.0f)), 1.0f, 0.05f, (std::max)(0.1f, light.range)) *
                glm::lookAt(light.position, light.position + direction, up);
            spotLightMatrices[static_cast<std::size_t>(slot)] = lightMatrix;
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, spotShadowMap_, 0, slot);
            glClear(GL_DEPTH_BUFFER_BIT);
            for (const MeshDrawCommand* cmd : opaqueCommands) {
                shadowDepthShader_->setMat4("uLightMVP", lightMatrix * cmd->modelMatrix);
                drawLocalCaster(*shadowDepthShader_, *cmd);
            }
            for (const MeshDrawCommand& cmd : shadowCasterList_) {
                if (glm::length(cmd.transparentSortCenter - light.position) > light.range + cmd.transparentSortRadius) continue;
                shadowDepthShader_->setMat4("uLightMVP", lightMatrix * cmd.modelMatrix);
                drawLocalCaster(*shadowDepthShader_, cmd);
            }
        }

        static const std::array<glm::vec3, 6> cubeDirections{
            glm::vec3(1,0,0), glm::vec3(-1,0,0), glm::vec3(0,1,0), glm::vec3(0,-1,0), glm::vec3(0,0,1), glm::vec3(0,0,-1)};
        static const std::array<glm::vec3, 6> cubeUps{
            glm::vec3(0,-1,0), glm::vec3(0,-1,0), glm::vec3(0,0,1), glm::vec3(0,0,-1), glm::vec3(0,-1,0), glm::vec3(0,-1,0)};
        pointShadowDepthShader_->use();
        glViewport(0, 0, pointResolution, pointResolution);
        for (int slot = 0; slot < pointShadowCount; ++slot) {
            const LightDrawCommand& light = lightDrawList_[static_cast<std::size_t>(pointShadowLightIndices[static_cast<std::size_t>(slot)])];
            const float range = (std::max)(0.1f, light.range);
            const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, range);
            pointShadowDepthShader_->setVec3("uLightPosition", light.position);
            pointShadowDepthShader_->setFloat("uLightRange", range);
            for (int face = 0; face < 6; ++face) {
                const glm::mat4 lightMatrix = projection * glm::lookAt(light.position, light.position + cubeDirections[static_cast<std::size_t>(face)], cubeUps[static_cast<std::size_t>(face)]);
                glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, pointShadowMap_, 0, slot * 6 + face);
                glClear(GL_DEPTH_BUFFER_BIT);
                for (const MeshDrawCommand* cmd : opaqueCommands) {
                    pointShadowDepthShader_->setMat4("uLightMVP", lightMatrix * cmd->modelMatrix);
                    pointShadowDepthShader_->setMat4("uModel", cmd->modelMatrix);
                    drawLocalCaster(*pointShadowDepthShader_, *cmd);
                }
                for (const MeshDrawCommand& cmd : shadowCasterList_) {
                    if (glm::length(cmd.transparentSortCenter - light.position) > range + cmd.transparentSortRadius) continue;
                    pointShadowDepthShader_->setMat4("uLightMVP", lightMatrix * cmd.modelMatrix);
                    pointShadowDepthShader_->setMat4("uModel", cmd.modelMatrix);
                    drawLocalCaster(*pointShadowDepthShader_, cmd);
                }
            }
        }

        glColorMask(previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        if (previousScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        glDepthFunc(GL_LEQUAL);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    }

    auto bindCommonUniforms = [&](Shader& shader) {
        shader.use();
        shader.setInt("uDiffuseTexture", 0);
        shader.setInt("uMaterialAlbedoTexture", 0);
        shader.setInt("uMaterialNormalTexture", 1);
        shader.setInt("uMaterialMetallicTexture", 2);
        shader.setInt("uMaterialRoughnessTexture", 3);
        shader.setInt("uMaterialAoTexture", 4);
        shader.setVec3("uAmbientColor", settings_.profile.ambientColor);
        shader.setVec3("uCameraPosition", cameraPosition);
        shader.setBool("uStylized", settings_.profile.style == RenderStyle::Stylized);
        shader.setFloat("uStylizedBands", (std::max)(2.0f, settings_.profile.stylizedBands));
        shader.setFloat("uStylizedRimStrength", (std::max)(0.0f, settings_.profile.stylizedRimStrength));
        shader.setBool("uEnableDirectionalShadow", directionalShadowReady);
        shader.setInt("uShadowLightIndex", shadowLightIndex);
        shader.setMat4("uView", view_);
        shader.setInt("uShadowCascadeCount", directionalCascadeCount);
        for (int cascade = 0; cascade < maxShadowCascades; ++cascade) {
            const std::string index = "[" + std::to_string(cascade) + "]";
            shader.setMat4("uDirectionalLightMatrices" + index,
                directionalLightMatrices[static_cast<std::size_t>(cascade)]);
            shader.setFloat("uShadowCascadeSplits" + index,
                directionalCascadeSplits[static_cast<std::size_t>(cascade)]);
        }
        shader.setInt("uDirectionalShadowMap", 15);
        shader.setInt("uSpotShadowCount", spotShadowCount);
        shader.setInt("uPointShadowCount", pointShadowCount);
        shader.setInt("uSpotShadowMap", 13);
        shader.setInt("uPointShadowMap", 14);
        for (int slot = 0; slot < 4; ++slot) {
            const std::string index = "[" + std::to_string(slot) + "]";
            shader.setInt("uSpotShadowLightIndices" + index, spotShadowLightIndices[static_cast<std::size_t>(slot)]);
            shader.setMat4("uSpotLightMatrices" + index, spotLightMatrices[static_cast<std::size_t>(slot)]);
            shader.setInt("uPointShadowLightIndices" + index, pointShadowLightIndices[static_cast<std::size_t>(slot)]);
            if (slot < pointShadowCount) {
                const LightDrawCommand& light = lightDrawList_[static_cast<std::size_t>(pointShadowLightIndices[static_cast<std::size_t>(slot)])];
                shader.setVec3("uPointShadowPositions" + index, light.position);
                shader.setFloat("uPointShadowRanges" + index, (std::max)(0.1f, light.range));
            }
        }
        shader.setFloat("uShadowSoftness", (std::clamp)(settings_.profile.shadowSoftness, 0.0f, 8.0f));
        shader.setBool("uShadowCascadeDebugView", settings_.profile.shadowCascadeDebugView);
        const bool iblEnabled = environmentMaps_.source != 0 && settings_.profile.reflections;
        shader.setBool("uEnableIbl", iblEnabled);
        shader.setBool("uUseBakedIbl", environmentReady_);
        shader.setFloat("uEnvironmentIntensity", (std::clamp)(settings_.profile.environmentIntensity, 0.0f, 4.0f));
        shader.setFloat("uReflectionIntensity", (std::clamp)(settings_.profile.reflectionIntensity, 0.0f, 4.0f));
        shader.setInt("uIblDebugMode", settings_.profile.iblDebugMode);
        shader.setInt("uEnvironmentMap", 9);
        shader.setInt("uIrradianceMap", 10);
        shader.setInt("uPrefilterMap", 11);
        shader.setInt("uBrdfLut", 12);
        const int reflectionProbeCount = iblEnabled
            ? (std::min)(4, static_cast<int>(reflectionProbeDrawList_.size()))
            : 0;
        shader.setInt("uReflectionProbeCount", reflectionProbeCount);
        for (int probeIndex = 0; probeIndex < 4; ++probeIndex) {
            const std::string index = "[" + std::to_string(probeIndex) + "]";
            shader.setInt("uReflectionProbeMaps" + index, 5 + probeIndex);
            if (probeIndex < reflectionProbeCount) {
                const ReflectionProbeDrawCommand& probe = reflectionProbeDrawList_[static_cast<std::size_t>(probeIndex)];
                shader.setVec3("uReflectionProbePositions" + index, probe.position);
                shader.setVec3("uReflectionProbeExtents" + index, glm::vec3(
                    (std::max)(probe.boxExtents.x, 0.01f),
                    (std::max)(probe.boxExtents.y, 0.01f),
                    (std::max)(probe.boxExtents.z, 0.01f)));
                shader.setFloat("uReflectionProbeBlendDistances" + index, (std::max)(0.01f, probe.blendDistance));
                shader.setFloat("uReflectionProbeIntensities" + index, (std::max)(0.0f, probe.intensity));
                glActiveTexture(GL_TEXTURE5 + probeIndex);
                const unsigned int fallbackProbeMap = environmentReady_ ? environmentMaps_.prefiltered : environmentMaps_.source;
                glBindTexture(GL_TEXTURE_CUBE_MAP, probe.cubemapTexture != 0 ? probe.cubemapTexture : fallbackProbeMap);
            }
        }
        if (iblEnabled) {
            glActiveTexture(GL_TEXTURE9);
            glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMaps_.source);
            glActiveTexture(GL_TEXTURE10);
            glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMaps_.irradiance);
            glActiveTexture(GL_TEXTURE11);
            glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMaps_.prefiltered);
            glActiveTexture(GL_TEXTURE12);
            glBindTexture(GL_TEXTURE_2D, environmentMaps_.brdfLut);
        }
        if (directionalShadowReady) {
            glActiveTexture(GL_TEXTURE15);
            glBindTexture(GL_TEXTURE_2D_ARRAY, directionalShadowMap_);
        }
        if (spotShadowCount > 0) {
            glActiveTexture(GL_TEXTURE13);
            glBindTexture(GL_TEXTURE_2D_ARRAY, spotShadowMap_);
        }
        if (pointShadowCount > 0) {
            glActiveTexture(GL_TEXTURE14);
            glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, pointShadowMap_);
        }
    };
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
    auto drawMeshCommand = [&](const MeshDrawCommand& cmd) {
        Shader* activeShader = resolveShader(cmd.unlit ? std::string("unlit") : cmd.shaderId);
        if (activeShader == nullptr) {
            return;
        }
        const std::string currentShaderId = cmd.unlit ? std::string("unlit") : ShaderRegistry::NormalizeShaderId(cmd.shaderId);
        if (boundShaderId != currentShaderId) {
            bindCommonUniforms(*activeShader);
            bindLights(*activeShader);
            boundShaderId = currentShaderId;
        } else {
            activeShader->use();
        }

        const bool transparent = cmd.transparent || ShaderRegistry::Resolve(currentShaderId).transparent || cmd.color.a < 0.999f;
        const bool writesAmbient = glGetFragDataLocation(activeShader->ID, "AmbientBuffer") >= 0;
        const bool writesMaterial = glGetFragDataLocation(activeShader->ID, "MaterialBuffer") >= 0;
        if (transparent) {
            glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glColorMaski(2, writesAmbient ? GL_TRUE : GL_FALSE,
                         writesAmbient ? GL_TRUE : GL_FALSE,
                         writesAmbient ? GL_TRUE : GL_FALSE,
                         writesAmbient ? GL_TRUE : GL_FALSE);
            glColorMaski(3, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glEnable(GL_BLEND);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendFuncSeparatei(2, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else {
            glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glColorMaski(2, writesAmbient ? GL_TRUE : GL_FALSE,
                         writesAmbient ? GL_TRUE : GL_FALSE,
                         writesAmbient ? GL_TRUE : GL_FALSE,
                         writesAmbient ? GL_TRUE : GL_FALSE);
            glColorMaski(3, writesMaterial ? GL_TRUE : GL_FALSE,
                         writesMaterial ? GL_TRUE : GL_FALSE,
                         writesMaterial ? GL_TRUE : GL_FALSE,
                         writesMaterial ? GL_TRUE : GL_FALSE);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
        if (cmd.doubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);

        // A mirrored object or parent transform reverses triangle winding. Match the
        // front-face state to the submitted model matrix so back-face culling and
        // gl_FrontFacing based normal correction keep imported meshes solid.
        const bool mirroredTransform = glm::determinant(glm::mat3(cmd.modelMatrix)) < 0.0f;
        glFrontFace(mirroredTransform ? GL_CW : GL_CCW);

        // MVP = proj * view * model
        glm::mat4 mvp = proj_ * view_ * cmd.modelMatrix;
        activeShader->setMat4("uMVP", mvp);
        activeShader->setMat4("uModel", cmd.modelMatrix);
        // Per-object color
        activeShader->setVec4("uColor", cmd.color);
        activeShader->setVec3("uEmissiveColor", cmd.emissiveColor);
        activeShader->setFloat("uMetallic", (std::max)(0.0f, (std::min)(1.0f, cmd.metallic)));
        activeShader->setFloat("uRoughness", (std::max)(0.02f, (std::min)(1.0f, cmd.roughness)));
        activeShader->setFloat("uClearCoat", (std::clamp)(cmd.clearCoat, 0.0f, 1.0f));
        activeShader->setFloat("uClearCoatRoughness", (std::clamp)(cmd.clearCoatRoughness, 0.02f, 1.0f));
        activeShader->setFloat("uAnisotropy", (std::clamp)(cmd.anisotropy, -1.0f, 1.0f));
        activeShader->setFloat("uTransmission", (std::clamp)(cmd.transmission, 0.0f, 1.0f));
        activeShader->setFloat("uAlphaCutoff", (std::max)(0.0f, cmd.alphaCutoff));
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
        if (transparent && cmd.doubleSided) {
            // Alpha blending cannot correctly compose both sides of a glass shell
            // in one unordered draw. Render back faces first, then front faces.
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT, nullptr);
            glCullFace(GL_BACK);
            glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT, nullptr);
            frameStats_.drawCallCount += 2;
        } else {
            glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT, nullptr);
            ++frameStats_.drawCallCount;
        }
    };

    for (const MeshDrawCommand* cmd : opaqueCommands) {
        drawMeshCommand(*cmd);
    }
    for (const MeshDrawCommand* cmd : transparentCommands) {
        drawMeshCommand(*cmd);
    }
    // Debug lines and gizmos are overlays, not scene surfaces for SSAO.
    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glColorMaski(2, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glColorMaski(3, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glBindVertexArray(0);
    drawList_.clear();
    shadowCasterList_.clear();
    lightDrawList_.clear();
    reflectionProbeDrawList_.clear();

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
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(3, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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
    unjitteredProj_ = proj;
    proj_ = proj;
    if (viewportTargetActive_ && settings_.profile.antiAliasing == AntiAliasingMode::TAA) {
        ViewportTarget& target = GetViewportTarget(activeViewportTarget_);
        target.taaCurrentJitterUv = glm::vec2(0.0f);
        if (target.width > 0 && target.height > 0) {
            const std::uint32_t sampleIndex = ((std::max)(1u, target.taaFrameIndex) - 1u) % 16u + 1u;
            const float jitterStrength = (std::clamp)(settings_.profile.taaJitterStrength, 0.0f, 1.0f);
            target.taaCurrentJitterUv = glm::vec2(
                (Halton(sampleIndex, 2u) - 0.5f) * jitterStrength / static_cast<float>(target.width),
                (Halton(sampleIndex, 3u) - 0.5f) * jitterStrength / static_cast<float>(target.height));
            proj_[2][0] -= target.taaCurrentJitterUv.x * 2.0f;
            proj_[2][1] -= target.taaCurrentJitterUv.y * 2.0f;
        }
    }
}

void Renderer::SubmitReflectionProbe(const ReflectionProbeDrawCommand& cmd) {
    if (reflectionProbeDrawList_.size() < 64) reflectionProbeDrawList_.push_back(cmd);
}

void Renderer::InitializeSsaoResources() {
    std::mt19937 randomEngine(0x52414345u);
    std::uniform_real_distribution<float> unitDistribution(0.0f, 1.0f);
    std::uniform_real_distribution<float> signedDistribution(-1.0f, 1.0f);

    for (std::size_t i = 0; i < ssaoKernel_.size(); ++i) {
        glm::vec3 sample{
            signedDistribution(randomEngine),
            signedDistribution(randomEngine),
            unitDistribution(randomEngine)
        };
        sample = glm::normalize(sample) * unitDistribution(randomEngine);
        float scale = static_cast<float>(i) / static_cast<float>(ssaoKernel_.size() - 1);
        scale = 0.1f + 0.9f * scale * scale;
        ssaoKernel_[i] = sample * scale;
    }

    std::array<glm::vec3, 16> noise{};
    for (glm::vec3& direction : noise) {
        direction = glm::vec3(
            signedDistribution(randomEngine),
            signedDistribution(randomEngine),
            0.0f);
    }
    glGenTextures(1, &ssaoNoiseTexture_);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    if (ssaoShader_) {
        ssaoShader_->use();
        for (std::size_t i = 0; i < ssaoKernel_.size(); ++i) {
            ssaoShader_->setVec3("uSamples[" + std::to_string(i) + "]", ssaoKernel_[i]);
        }
    }
}

void Renderer::InitializePipelines() {
    materialShaders_["pbr"] = std::make_unique<Shader>("src/shaders/default/default.vs", "src/shaders/default/pbr.fs");
    toneMapShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/tonemap.fs");
    bloomExtractShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/bloom_extract.fs");
    bloomBlurShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/bloom_blur.fs");
    ssaoShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/ssao.fs");
    ssaoBlurShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/ssao_bilateral_blur.fs");
    ssaoCompositeShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/ssao_composite.fs");
    ssrShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/ssr.fs");
    motionVectorShader_ = std::make_unique<Shader>("src/shaders/post/motion_vectors.vs", "src/shaders/post/motion_vectors.fs");
    cameraVelocityShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/camera_velocity.fs");
    motionBlurShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/motion_blur.fs");
    weatherShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/weather.fs");
    depthOfFieldShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/depth_of_field.fs");
    taaShader_ = std::make_unique<Shader>("src/shaders/post/fullscreen.vs", "src/shaders/post/taa.fs");
    shadowDepthShader_ = std::make_unique<Shader>("src/shaders/shadow/directional_depth.vs", "src/shaders/shadow/directional_depth.fs");
    pointShadowDepthShader_ = std::make_unique<Shader>("src/shaders/shadow/point_depth.vs", "src/shaders/shadow/point_depth.fs");
    irradianceShader_ = std::make_unique<Shader>("src/shaders/PBR/cubemap.vs", "src/shaders/PBR/irradiance_convolution.fs");
    prefilterShader_ = std::make_unique<Shader>("src/shaders/PBR/cubemap.vs", "src/shaders/PBR/prefilter.fs");
    brdfShader_ = std::make_unique<Shader>("src/shaders/PBR/brdf.vs", "src/shaders/PBR/brdf.fs");
    reflectionCaptureSkyShader_ = std::make_unique<Shader>("src/shaders/PBR/background.vs", "src/shaders/PBR/reflection_capture_background.fs");
}

void Renderer::InitializeQuad() {
    if (fullscreenQuad_ == 0) {
        glGenVertexArrays(1, &fullscreenQuad_);
    }
    if (captureCubeVao_ == 0) {
        constexpr float cubeVertices[] = {
            -1,-1,-1,  -1,-1, 1,  -1, 1, 1,  1, 1,-1,  -1,-1,-1,  -1, 1,-1,
             1,-1, 1,   1,-1,-1,   1, 1,-1,  1, 1,-1,   1, 1, 1,   1,-1, 1,
            -1,-1, 1,  -1, 1, 1,   1, 1, 1,  1, 1, 1,   1,-1, 1,  -1,-1, 1,
            -1, 1,-1,   1, 1,-1,   1, 1, 1,  1, 1, 1,  -1, 1, 1,  -1, 1,-1,
            -1,-1,-1,   1,-1,-1,   1,-1, 1,  1,-1, 1,  -1,-1, 1,  -1,-1,-1,
            -1, 1, 1,  -1,-1, 1,  -1,-1,-1, -1,-1,-1, -1, 1,-1,  -1, 1, 1,
        };
        glGenVertexArrays(1, &captureCubeVao_);
        glGenBuffers(1, &captureCubeVbo_);
        glBindVertexArray(captureCubeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, captureCubeVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
    }
}

} // namespace raceman

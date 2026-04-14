#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Shader;

namespace raceman {

struct RendererConfig {
    int width{1920};
    int height{1080};
};

struct RendererViewport {
    int x{0};
    int y{0};
    int width{1920};
    int height{1080};
};

struct RendererSettings {
    glm::vec3 clearColor{0.02f, 0.02f, 0.02f};
    glm::vec3 ambientColor{0.08f, 0.08f, 0.08f};
    bool enableDrawCallSorting{true};
};

struct RendererFrameStats {
    std::uint32_t submittedMeshCount{0};
    std::uint32_t frustumCulledMeshCount{0};
    std::uint32_t submittedLightCount{0};
    std::uint32_t drawCallCount{0};
    std::uint64_t submittedTriangleCount{0};
};

struct MeshDrawCommand {
    unsigned int vao{0};
    unsigned int indexCount{0};
    glm::mat4 modelMatrix{1.0f};
    std::string materialId;
    glm::vec4 color{1.0f, 0.2f, 0.2f, 1.0f};
    glm::vec3 emissiveColor{0.0f};
    float metallic{0.0f};
    float roughness{1.0f};
    unsigned int diffuseTextureId{0};
    bool useDiffuseTexture{false};
    bool unlit{false};
};

enum class RenderLightType {
    Directional,
    Point,
    Spot
};

struct LightDrawCommand {
    RenderLightType type{RenderLightType::Point};
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f};
    float intensity{1.0f};
    float range{10.0f};
    float spotAngleDegrees{30.0f};
};

enum class DebugLineDepthMode {
    AlwaysOnTop,
    DepthTested,
    DepthTestedOverlay
};

struct DebugLineCommand {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
    glm::vec4 color{1.0f};
    float width{2.0f};
    DebugLineDepthMode depthMode{DebugLineDepthMode::AlwaysOnTop};
};

struct EnvironmentMaps {
    unsigned int irradiance{0};
    unsigned int prefiltered{0};
    unsigned int brdfLut{0};
};

enum class ViewportRenderTarget {
    Scene,
    Game
};

class Renderer {
public:
    explicit Renderer(const RendererConfig& config);
    ~Renderer();

    void BeginFrame();
    void EndFrame();
    void Resize(int width, int height);
    void SetViewport(const RendererViewport& viewport);
    void EnsureViewportRenderTarget(ViewportRenderTarget target, int width, int height);
    void BeginFrameToViewportTarget(ViewportRenderTarget target, const glm::vec3& clearColor);
    void EndFrameToViewportTarget();
    unsigned int GetViewportRenderTargetTexture(ViewportRenderTarget target) const;

    void SetupEnvironment(const std::string& hdrPath);
    void BakeBrdfLut();
    void CreateShadowMaps(int resolution);

    void SubmitMesh(const MeshDrawCommand& cmd);
    void ReportFrustumCulled() { ++frameStats_.frustumCulledMeshCount; }
    void SubmitLight(const LightDrawCommand& cmd);
    void SubmitLine(const DebugLineCommand& cmd);
    void Flush();
    void ResetFrameStats();

    // Fallback camera setup used by simple pipeline; later swap to scene camera
    void SetCamera(const glm::mat4& view, const glm::mat4& proj);
    const glm::mat4& GetView() const { return view_; }
    const glm::mat4& GetProj() const { return proj_; }

    const EnvironmentMaps& GetEnvironmentMaps() const { return environmentMaps_; }
    RendererSettings& GetSettings() { return settings_; }
    const RendererSettings& GetSettings() const { return settings_; }
    const RendererConfig& GetConfig() const;
    const RendererViewport& GetViewport() const { return viewport_; }
    const RendererFrameStats& GetFrameStats() const { return frameStats_; }

private:
    struct ViewportTarget {
        unsigned int framebuffer{0};
        unsigned int colorTexture{0};
        unsigned int depthRenderbuffer{0};
        int width{0};
        int height{0};
    };

    void InitializePipelines();
    void InitializeQuad();
    void DestroyViewportTarget(ViewportTarget& target);
    ViewportTarget& GetViewportTarget(ViewportRenderTarget target);
    const ViewportTarget& GetViewportTarget(ViewportRenderTarget target) const;

    RendererConfig config_{};
    RendererViewport viewport_{};
    std::vector<MeshDrawCommand> drawList_;
    std::vector<LightDrawCommand> lightDrawList_;
    EnvironmentMaps environmentMaps_{};
    RendererSettings settings_{};
    unsigned int captureFbo_{0};
    unsigned int captureRbo_{0};
    unsigned int fullscreenQuad_{0};
    unsigned int lineVao_{0};
    unsigned int lineVbo_{0};
    ViewportTarget sceneViewportTarget_{};
    ViewportTarget gameViewportTarget_{};
    std::vector<unsigned int> shadowMaps_;
    std::vector<DebugLineCommand> lineDrawList_;
    RendererFrameStats frameStats_{};

    // Simple fallback pipeline state
    std::unique_ptr<Shader> simpleShader_;
    glm::mat4 view_{1.0f};
    glm::mat4 proj_{1.0f};
};

} // namespace raceman

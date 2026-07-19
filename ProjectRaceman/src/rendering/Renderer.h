#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Material.h"

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

enum class RenderStyle {
    Realistic,
    Stylized
};

enum class GraphicsQualityTier {
    Low,
    Medium,
    High,
    Ultra
};

enum class AntiAliasingMode {
    None,
    FXAA,
    TAA,
    MSAA
};

struct GraphicsProfile {
    int version{1};
    RenderStyle style{RenderStyle::Realistic};
    GraphicsQualityTier quality{GraphicsQualityTier::High};
    AntiAliasingMode antiAliasing{AntiAliasingMode::FXAA};
    float taaFeedback{0.90f};
    float taaSharpness{0.20f};
    float taaJitterStrength{0.50f};
    bool taaDebugView{false};
    bool hdr{true};
    float hdrPaperWhiteNits{200.0f};
    float hdrPeakBrightnessNits{1000.0f};
    bool bloom{true};
    float bloomIntensity{0.7f};
    float bloomThreshold{1.0f};
    float bloomRadius{1.0f};
    bool colorGrading{true};
    float colorSaturation{1.0f};
    float colorContrast{1.0f};
    float colorTemperature{0.0f};
    float colorTint{0.0f};
    bool vignette{false};
    float vignetteIntensity{0.25f};
    float vignetteSmoothness{0.5f};
    bool filmGrain{false};
    float filmGrainIntensity{0.04f};
    bool depthOfField{false};
    float depthOfFieldFocusDistance{10.0f};
    float depthOfFieldFocusRange{5.0f};
    float depthOfFieldMaxRadius{8.0f};
    bool motionBlur{false};
    float motionBlurShutterAngle{180.0f};
    float motionBlurIntensity{1.0f};
    int motionBlurSamples{12};
    float motionBlurMaxRadius{24.0f};
    float motionBlurMinimumVelocityPixels{1.5f};
    bool motionBlurDebugView{false};
    bool ssao{true};
    float ssaoIntensity{1.0f};
    float ssaoRadius{0.75f};
    float ssaoBias{0.025f};
    bool ssaoDebugView{false};
    bool shadows{true};
    // 0 follows the quality tier; otherwise an explicit square map resolution.
    int shadowResolution{0};
    float shadowSoftness{2.0f};
    int shadowCascadeCount{4};
    float shadowDistance{150.0f};
    int localShadowLightLimit{2};
    bool shadowCascadeDebugView{false};
    bool reflections{true};
    float environmentIntensity{1.0f};
    float reflectionIntensity{1.0f};
    int iblDebugMode{0};
    bool screenSpaceReflections{true};
    float ssrIntensity{0.45f};
    float ssrMaxDistance{40.0f};
    float ssrThickness{0.25f};
    int ssrSteps{40};
    bool ssrDebugView{false};
    bool particles{true};
    bool weather{true};
    float weatherIntensity{0.0f};
    float weatherWind{0.25f};
    bool lod{true};
    bool dynamicResolution{false};
    float minimumResolutionScale{0.75f};
    int dynamicResolutionTargetFps{60};
    float exposure{1.0f};
    float stylizedBands{4.0f};
    float stylizedRimStrength{0.35f};
    glm::vec3 ambientColor{0.08f, 0.08f, 0.08f};
};

struct RendererSettings {
    // Editor Scene-view background only. Game-view clear color belongs to Camera.
    glm::vec3 editorClearColor{0.02f, 0.02f, 0.02f};
    bool enableDrawCallSorting{true};
    GraphicsProfile profile{};
};

struct DisplayHdrCapabilities {
    bool detected{false};
    bool hdrSupported{false};
    bool hdrEnabledInWindows{false};
    bool nativePresentationAvailable{false};
    int displayBitsPerColor{8};
    int windowBitsPerColor{8};
    float minimumLuminanceNits{0.0f};
    float maximumLuminanceNits{0.0f};
    float maximumFullFrameLuminanceNits{0.0f};
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
    std::string motionId;
    std::string materialId;
    glm::vec4 color{1.0f, 0.2f, 0.2f, 1.0f};
    glm::vec3 emissiveColor{0.0f};
    float metallic{0.0f};
    float roughness{1.0f};
    float clearCoat{0.0f};
    float clearCoatRoughness{0.1f};
    float anisotropy{0.0f};
    float transmission{0.0f};
    float alphaCutoff{0.0f};
    bool doubleSided{false};
    bool transparent{false};
    glm::vec3 transparentSortCenter{0.0f};
    float transparentSortRadius{0.0f};
    bool hasTransparentSortBounds{false};
    glm::vec2 uvTiling{1.0f, 1.0f};
    glm::vec2 uvOffset{0.0f, 0.0f};
    std::string shaderId{"pbr"};
    unsigned int diffuseTextureId{0};
    std::array<unsigned int, 5> materialTextureIds{0, 0, 0, 0, 0};
    struct MaterialUniform {
        std::string uniformName;
        std::string textureUseUniform;
        MaterialPropertyValue value;
        unsigned int textureId{0};
    };
    std::vector<MaterialUniform> materialUniforms;
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
    bool castShadows{true};
};

struct ReflectionProbeDrawCommand {
    glm::vec3 position{0.0f};
    glm::vec3 boxExtents{5.0f};
    float blendDistance{1.0f};
    float intensity{1.0f};
    unsigned int cubemapTexture{0};
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
    // Borrowed from SkyboxController; Renderer does not own this texture.
    unsigned int source{0};
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
    void PresentViewportTarget(ViewportRenderTarget target, const RendererViewport& destination);
    unsigned int GetViewportRenderTargetTexture(ViewportRenderTarget target) const;
    // Linear scRGB RGBA16F output. Valid when HDR Output is enabled; editor
    // panels intentionally use the SDR preview returned above.
    unsigned int GetViewportHdrOutputTexture(ViewportRenderTarget target) const;
    unsigned int GetViewportDepthTexture(ViewportRenderTarget target) const;
    unsigned int GetViewportNormalTexture(ViewportRenderTarget target) const;
    unsigned int GetViewportSsaoTexture(ViewportRenderTarget target) const;
    float GetDynamicResolutionScale(ViewportRenderTarget target) const;

    void SetupEnvironment(unsigned int sourceCubemap);
    bool BakeReflectionProbe(const glm::vec3& position,
                             int resolution,
                             const std::string& outputPath,
                             const std::function<void()>& submitScene,
                             const std::function<void(int, int)>& progress = {});
    unsigned int LoadReflectionProbeCubemap(const std::string& path);
    void BakeBrdfLut();
    void CreateShadowMaps(int resolution, int cascadeCount);
    void CreateLocalShadowMaps(int spotResolution, int spotCount, int pointResolution, int pointCount);

    void SubmitMesh(const MeshDrawCommand& cmd);
    // Submit a camera-culled mesh to the depth pass without drawing it in the color pass.
    void SubmitShadowCaster(const MeshDrawCommand& cmd);
    void ReportFrustumCulled() { ++frameStats_.frustumCulledMeshCount; }
    void SubmitLight(const LightDrawCommand& cmd);
    void SubmitReflectionProbe(const ReflectionProbeDrawCommand& cmd);
    void SubmitLine(const DebugLineCommand& cmd);
    void Flush();
    void ResetFrameStats();

    // Fallback camera setup used by simple pipeline; later swap to scene camera
    void SetCamera(const glm::mat4& view, const glm::mat4& proj);
    const glm::mat4& GetView() const { return view_; }
    const glm::mat4& GetProj() const { return proj_; }

    const EnvironmentMaps& GetEnvironmentMaps() const { return environmentMaps_; }
    bool HasEnvironmentSource() const { return environmentMaps_.source != 0; }
    bool IsEnvironmentBakeReady() const { return environmentReady_; }
    float GetEnvironmentAverageLuminance() const { return environmentAverageLuminance_; }
    RendererSettings& GetSettings() { return settings_; }
    const RendererSettings& GetSettings() const { return settings_; }
    const RendererConfig& GetConfig() const;
    const RendererViewport& GetViewport() const { return viewport_; }
    const RendererFrameStats& GetFrameStats() const { return frameStats_; }
    void SetDisplayHdrCapabilities(const DisplayHdrCapabilities& capabilities) { displayHdrCapabilities_ = capabilities; }
    const DisplayHdrCapabilities& GetDisplayHdrCapabilities() const { return displayHdrCapabilities_; }

private:
    struct ViewportTarget {
        unsigned int framebuffer{0};
        unsigned int hdrColorTexture{0};
        unsigned int depthTexture{0};
        unsigned int normalTexture{0};
        unsigned int ambientTexture{0};
        unsigned int materialTexture{0};
        unsigned int compositeFramebuffer{0};
        unsigned int compositeTexture{0};
        unsigned int ssrFramebuffer{0};
        unsigned int ssrTexture{0};
        unsigned int velocityFramebuffer{0};
        unsigned int velocityTexture{0};
        unsigned int motionBlurFramebuffer{0};
        unsigned int motionBlurTexture{0};
        unsigned int weatherFramebuffer{0};
        unsigned int weatherTexture{0};
        unsigned int depthOfFieldFramebuffer{0};
        unsigned int depthOfFieldTexture{0};
        std::array<unsigned int, 2> taaFramebuffers{0, 0};
        std::array<unsigned int, 2> taaHistoryTextures{0, 0};
        std::array<unsigned int, 2> taaSurfaceHistoryTextures{0, 0};
        int taaWriteIndex{0};
        std::uint32_t taaFrameIndex{0};
        bool taaHistoryValid{false};
        glm::vec2 taaCurrentJitterUv{0.0f};
        glm::vec2 taaPreviousJitterUv{0.0f};
        glm::vec3 taaPreviousCameraPosition{0.0f};
        glm::vec3 taaPreviousCameraForward{0.0f, 0.0f, -1.0f};
        unsigned int outputFramebuffer{0};
        unsigned int colorTexture{0};
        unsigned int hdrOutputFramebuffer{0};
        unsigned int hdrOutputTexture{0};
        std::array<unsigned int, 2> bloomFramebuffers{0, 0};
        std::array<unsigned int, 2> bloomTextures{0, 0};
        int bloomWidth{0};
        int bloomHeight{0};
        unsigned int ssaoFramebuffer{0};
        unsigned int ssaoTexture{0};
        unsigned int ssaoBlurFramebuffer{0};
        unsigned int ssaoBlurTexture{0};
        int ssaoWidth{0};
        int ssaoHeight{0};
        int width{0};
        int height{0};
        int requestedWidth{0};
        int requestedHeight{0};
        float resolutionScale{1.0f};
        float smoothedFrameTimeMs{0.0f};
        double lastFrameBeginSeconds{0.0};
        int resolutionAdjustmentCooldown{0};
        glm::mat4 previousViewProjection{1.0f};
        bool hasPreviousViewProjection{false};
        std::unordered_map<std::string, glm::mat4> previousModelMatrices;
    };

    void InitializePipelines();
    void InitializeQuad();
    void InitializeSsaoResources();
    void RenderCaptureCube() const;
    void ResolveViewportTarget(ViewportTarget& target);
    void DestroyViewportTarget(ViewportTarget& target);
    ViewportTarget& GetViewportTarget(ViewportRenderTarget target);
    const ViewportTarget& GetViewportTarget(ViewportRenderTarget target) const;

    RendererConfig config_{};
    RendererViewport viewport_{};
    std::vector<MeshDrawCommand> drawList_;
    std::vector<MeshDrawCommand> motionVectorDrawList_;
    std::vector<MeshDrawCommand> shadowCasterList_;
    std::vector<LightDrawCommand> lightDrawList_;
    std::vector<ReflectionProbeDrawCommand> reflectionProbeDrawList_;
    std::unordered_map<std::string, unsigned int> reflectionProbeCubemapCache_;
    EnvironmentMaps environmentMaps_{};
    RendererSettings settings_{};
    DisplayHdrCapabilities displayHdrCapabilities_{};
    unsigned int captureFbo_{0};
    unsigned int captureRbo_{0};
    unsigned int captureCubeVao_{0};
    unsigned int captureCubeVbo_{0};
    bool environmentReady_{false};
    float environmentAverageLuminance_{0.0f};
    unsigned int fullscreenQuad_{0};
    unsigned int lineVao_{0};
    unsigned int lineVbo_{0};
    std::size_t lineVertexCapacity_{0};
    ViewportTarget sceneViewportTarget_{};
    ViewportTarget gameViewportTarget_{};
    unsigned int directionalShadowFramebuffer_{0};
    unsigned int directionalShadowMap_{0};
    int directionalShadowResolution_{0};
    int directionalShadowCascadeCount_{0};
    unsigned int localShadowFramebuffer_{0};
    unsigned int spotShadowMap_{0};
    unsigned int pointShadowMap_{0};
    int spotShadowResolution_{0};
    int spotShadowLayerCount_{0};
    int pointShadowResolution_{0};
    int pointShadowLightCount_{0};
    unsigned int ssaoNoiseTexture_{0};
    std::array<glm::vec3, 32> ssaoKernel_{};
    std::vector<DebugLineCommand> lineDrawList_;
    RendererFrameStats frameStats_{};

    // Simple fallback pipeline state
    std::unique_ptr<Shader> simpleShader_;
    std::unique_ptr<Shader> toneMapShader_;
    std::unique_ptr<Shader> bloomExtractShader_;
    std::unique_ptr<Shader> bloomBlurShader_;
    std::unique_ptr<Shader> ssaoShader_;
    std::unique_ptr<Shader> ssaoBlurShader_;
    std::unique_ptr<Shader> ssaoCompositeShader_;
    std::unique_ptr<Shader> ssrShader_;
    std::unique_ptr<Shader> motionVectorShader_;
    std::unique_ptr<Shader> cameraVelocityShader_;
    std::unique_ptr<Shader> motionBlurShader_;
    std::unique_ptr<Shader> weatherShader_;
    std::unique_ptr<Shader> depthOfFieldShader_;
    std::unique_ptr<Shader> taaShader_;
    std::unique_ptr<Shader> shadowDepthShader_;
    std::unique_ptr<Shader> pointShadowDepthShader_;
    std::unique_ptr<Shader> irradianceShader_;
    std::unique_ptr<Shader> prefilterShader_;
    std::unique_ptr<Shader> brdfShader_;
    std::unique_ptr<Shader> reflectionCaptureSkyShader_;
    std::unordered_map<std::string, std::unique_ptr<Shader>> materialShaders_;
    ViewportRenderTarget activeViewportTarget_{ViewportRenderTarget::Scene};
    bool viewportTargetActive_{false};
    glm::mat4 view_{1.0f};
    glm::mat4 proj_{1.0f};
    glm::mat4 unjitteredProj_{1.0f};
};

} // namespace raceman

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
    SMAA
};

// One-click coherent weather setups. Applying a preset writes the whole weather
// block - rain, surface wetness, droplets and visibility - because those only
// look right when they agree with each other. Editing any of them afterwards
// leaves the preset reading Custom.
enum class WeatherPreset {
    Clear,
    Overcast,
    Damp,
    LightRain,
    HeavyRain,
    Storm,
    Custom
};

enum class FogMode {
    Off,
    // Classic start/end depth ramp. Cheap, ignores altitude, and mostly useful
    // for stylised looks or matching a fixed art direction.
    Linear,
    // Density falls off exponentially with world height and is integrated
    // analytically along the view ray, so valleys fill and hilltops clear.
    ExponentialHeight
};

// Fog uniforms resolved for whatever is currently rendering. The skybox is drawn
// outside Renderer (Application drives SkyboxController), so the resolved block
// has to be reachable from there rather than living inside the draw loop.
struct FogUniforms {
    int mode{0};
    glm::vec3 color{0.62f, 0.68f, 0.76f};
    float density{0.0f};
    float heightFalloff{0.0f};
    float baseHeight{0.0f};
    float startDistance{0.0f};
    float maxOpacity{1.0f};
    float linearStart{20.0f};
    float linearEnd{300.0f};
    bool affectsSky{true};
    bool useSkyColor{false};
    // Cubemap sampled for sky-matched inscatter, plus the mip that gives a broad
    // enough average to read as "the sky over there" rather than a sharp cloud.
    unsigned int skyMap{0};
    float skyMipLevel{3.0f};
    glm::vec3 sunDirection{0.0f, -1.0f, 0.0f};
    glm::vec3 sunColor{1.0f, 0.85f, 0.6f};
    float sunIntensity{0.0f};
    float sunExponent{8.0f};
    bool debugView{false};
};

// Scene View draw modes, mirroring Unity's shading dropdown and Unreal's view
// modes. These are editor preview state only: the Game View always renders with
// the project's graphics profile, so what ships is never changed by whatever the
// Scene View happens to be displaying. The cheap modes additionally skip the
// passes they cannot show, so previewing in Wireframe or Unlit costs far less
// than a full lit frame.
enum class SceneViewShadingMode {
    Shaded,
    ShadedWireframe,
    Wireframe,
    Unlit,
    Plain,
    MotionVectors,
    Ssao,
    ShadowCascades,
    Ssr,
    TaaResolve,
    IblDiffuseIrradiance,
    IblRawEnvironment,
    IblFinalSpecular,
    Fog,
    AutoExposure,
    Wetness,
    Count
};

struct GraphicsProfile {
    int version{1};
    RenderStyle style{RenderStyle::Realistic};
    GraphicsQualityTier quality{GraphicsQualityTier::High};
    AntiAliasingMode antiAliasing{AntiAliasingMode::FXAA};
    float taaFeedback{0.94f};
    float taaSharpness{0.10f};
    float taaJitterStrength{1.00f};
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
    bool weather{true};
    float weatherIntensity{0.0f};
    float weatherWind{0.25f};
    // Real, world-positioned rain drops (see Renderer::RenderRainParticles).
    // Draw distance in world metres - the box drops wrap inside, so this is
    // how far out an individual drop is still simulated before it is just
    // atmospheric haze. Bigger reads as heavier weather but needs more drops
    // to stay equally dense.
    float weatherDrawDistance{46.0f};
    float weatherStreakLength{0.55f};
    float weatherStreakWidth{0.018f};
    // Multiplies the intensity-driven base fall speed; 1 is the authored
    // default rather than a real-world m/s value.
    float weatherFallSpeedScale{1.0f};
    bool lod{true};
    bool dynamicResolution{false};
    float minimumResolutionScale{0.75f};
    int dynamicResolutionTargetFps{60};
    float exposure{1.0f};
    // Auto-exposure (eye adaptation). When on, `exposure` above is ignored and
    // the exposure comes from a histogram of the frame; autoExposureCompensation
    // is the artistic offset on top, in EV. Left off by default so existing
    // projects keep the exposure they were authored with.
    bool autoExposure{false};
    float autoExposureCompensation{0.0f};   // EV, positive = brighter
    float autoExposureSpeedUp{3.0f};        // rate when adapting to a brighter scene
    float autoExposureSpeedDown{1.0f};      // ... and to a darker one
    float autoExposureMinLuminance{0.002f}; // clamps on the adapted value itself
    float autoExposureMaxLuminance{40.0f};
    float autoExposureLowPercent{0.30f};    // histogram trim, darkest fraction
    float autoExposureHighPercent{0.95f};   // ... and brightest
    bool autoExposureDebugView{false};
    // Resolved view-mode state, written by ResolveProfileForTarget from
    // RendererSettings::sceneViewShading. Do not set these directly and do not
    // persist them: they are always cleared for the Game View, and so are the
    // *DebugView flags and iblDebugMode declared above.
    bool wireframeView{false};
    bool wireframeOverlay{false};
    bool forceUnlitShading{false};
    bool plainView{false};
    float stylizedBands{4.0f};
    float stylizedRimStrength{0.35f};
    glm::vec3 ambientColor{0.08f, 0.08f, 0.08f};

    // --- Environment: fog ---------------------------------------------------
    // Authored look, not performance. Deliberately NOT written by
    // ApplyGraphicsPreset, the same way ambientColor above is left alone:
    // dropping from Ultra to Low must not change what the weather looks like.
    // Only the volumetric quality knobs below belong to the quality tiers.
    FogMode fogMode{FogMode::Off};
    glm::vec3 fogColor{0.62f, 0.68f, 0.76f};
    float fogDensity{0.015f};        // extinction at fogBaseHeight, per metre
    float fogHeightFalloff{0.05f};   // 0 = uniform fog, higher = thins with altitude
    float fogBaseHeight{0.0f};       // world Y at which fogDensity applies
    float fogStartDistance{5.0f};    // metres of clear air before fog begins
    float fogMaxOpacity{1.0f};       // caps opacity; below 1 keeps far shapes readable
    bool fogAffectsSky{true};        // horizon haze band, the main sense-of-scale cue
    // Take the inscatter colour from the environment map along the view ray
    // instead of fogColor, so distant surfaces fade toward whatever the sky
    // actually is behind them. Falls back to fogColor when no environment is
    // baked. This is real aerial perspective, and it removes most of the
    // per-time-of-day fog colour authoring.
    bool fogUseSkyColor{false};
    float fogLinearStart{20.0f};     // Linear mode only
    float fogLinearEnd{300.0f};      // Linear mode only
    // Directional (sun) inscattering: a second lobe tinted toward the sun, which
    // is what produces haze glow on a low sun.
    glm::vec3 fogSunColor{1.0f, 0.85f, 0.6f};
    float fogSunIntensity{0.0f};     // 0 disables the lobe entirely
    float fogSunExponent{8.0f};      // tightness of the glow around the sun
    bool fogDebugView{false};        // Scene View only; cleared by ResolveProfileForTarget

    // --- Environment: surface wetness ---------------------------------------
    // Deliberately independent of the `weather` rain overlay above: a track that
    // has stopped being rained on is still wet, and drying out is the interesting
    // part. Also authored look, so ApplyGraphicsPreset leaves it alone.
    float wetness{0.0f};                // 0 = dry, 1 = saturated
    float wetnessPuddleAmount{0.6f};    // how much of the wet area pools into water
    float wetnessPuddleScale{6.0f};     // puddle size in world units
    float wetnessRippleStrength{0.0f};  // surface disturbance; 0 = still water
    float wetnessRippleSpeed{1.0f};
    // Droplets: what water does on surfaces too steep to pool on. Scale is in
    // world units, so bodywork beads want centimetres, not metres.
    float wetnessDropletScale{0.06f};
    float wetnessDropletStrength{0.6f};
    float wetnessRunoffSpeed{0.15f};
    bool wetnessDebugView{false};
    // Couples rain to wetness over time: the track soaks while it rains and dries
    // out after, which is the whole reason a drying line exists in a race. Runs
    // only in play mode, and drives a runtime override rather than the authored
    // `wetness` above, so a saved project never captures a mid-shower value.
    WeatherPreset weatherPreset{WeatherPreset::Clear};
    bool weatherAutoWetness{true};
    float weatherWetRate{0.10f};   // wetness gained per second at full rain
    float weatherDryRate{0.03f};   // wetness lost per second with no rain

    // --- Skid marks ---------------------------------------------------------
    // Runtime rubber laid by sliding tyres, emitted as decals. maxSkidMarks is a
    // genuine performance knob - every mark is its own draw call until the
    // renderer gains instancing - so it is the one field the quality presets set.
    bool skidMarks{true};
    float skidMarkSlipThreshold{0.25f};
    float skidMarkSpacing{0.35f};
    float skidMarkWidth{0.28f};
    float skidMarkOpacity{0.75f};
    float skidMarkFadeSeconds{0.0f};   // 0 = persist for the whole session
    int maxSkidMarks{300};
    glm::vec3 skidMarkColor{0.06f, 0.055f, 0.05f};
};

struct RendererSettings {
    // Editor Scene-view background only. Game-view clear color belongs to Camera.
    glm::vec3 editorClearColor{0.02f, 0.02f, 0.02f};
    bool enableDrawCallSorting{true};
    GraphicsProfile profile{};
    // Scene View only, and deliberately not persisted with the project's look
    // settings. The Game View ignores this entirely.
    SceneViewShadingMode sceneViewShading{SceneViewShadingMode::Shaded};
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
    float wetnessResponse{1.0f};
    float puddleAffinity{1.0f};
    float puddleScale{1.0f};
    float dropletScale{1.0f};
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

enum class ReflectionProbeVolumeShape {
    Box,
    Sphere
};

struct ReflectionProbeDrawCommand {
    glm::vec3 position{0.0f};
    ReflectionProbeVolumeShape shape{ReflectionProbeVolumeShape::Box};
    glm::vec3 boxExtents{5.0f};
    float sphereRadius{5.0f};
    float blendDistance{1.0f};
    float intensity{1.0f};
    unsigned int cubemapTexture{0};
};

// A region rain cannot reach: garage, tunnel, pit box, under a bridge. Spatial
// rather than material, because the surfaces inside are usually the same
// materials as the ones outside.
struct WeatherShelterDrawCommand {
    // Full box volume in world space; the transform's scale is the box size.
    glm::mat4 transform{1.0f};
    float amount{1.0f};    // 1 = completely dry inside
    float falloff{1.0f};   // metres of gradient inward from the volume wall
};

enum class DecalBlendMode {
    // Darkens what is underneath. The right default: skid marks, rubber, dirt,
    // oil and grime are all darkening operations.
    Multiply,
    // Replaces what is underneath. Use for decals with their own colour, such as
    // painted track markings or sponsor logos.
    AlphaBlend
};

struct DecalDrawCommand {
    // Full box volume in world space; the transform's scale is the box size.
    // Projection runs along the volume's local -Y.
    glm::mat4 transform{1.0f};
    unsigned int textureId{0};
    glm::vec4 color{1.0f};
    float opacity{1.0f};
    // Surfaces angled further than this from the projection axis get no decal,
    // which is what stops a road decal smearing down the face of a kerb.
    float angleFadeDegrees{70.0f};
    glm::vec2 uvTiling{1.0f, 1.0f};
    glm::vec2 uvOffset{0.0f, 0.0f};
    DecalBlendMode blendMode{DecalBlendMode::Multiply};
    // Draw order among decals; lower draws first. Ties break on submission order.
    int sortOrder{0};
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
    // Time-sliced realtime probe capture: renders `faceCount` cubemap faces this
    // call and returns the probe texture once a full six-face round has finished.
    unsigned int UpdateRealtimeReflectionProbe(const std::string& probeId,
                                               const glm::vec3& position,
                                               int resolution,
                                               int faceCount,
                                               const std::function<void()>& submitScene);
    unsigned int GetRealtimeReflectionProbeTexture(const std::string& probeId) const;
    void ReleaseRealtimeReflectionProbe(const std::string& probeId);
    void BakeBrdfLut();
    void CreateShadowMaps(int resolution, int cascadeCount);
    void CreateLocalShadowMaps(int spotResolution, int spotCount, int pointResolution, int pointCount);

    void SubmitMesh(const MeshDrawCommand& cmd);
    // Submit a camera-culled mesh to the depth pass without drawing it in the color pass.
    void SubmitShadowCaster(const MeshDrawCommand& cmd);
    void ReportFrustumCulled() { ++frameStats_.frustumCulledMeshCount; }
    void SubmitLight(const LightDrawCommand& cmd);
    void SubmitReflectionProbe(const ReflectionProbeDrawCommand& cmd);
    void SubmitDecal(const DecalDrawCommand& cmd);
    void SubmitWeatherShelter(const WeatherShelterDrawCommand& cmd);
    void SubmitLine(const DebugLineCommand& cmd);
    void Flush();
    void ResetFrameStats();

    // Material shader cache management. Shader programs are compiled lazily in
    // Flush() and cached forever; the editor needs to drop an entry when the
    // shader source behind it changes on disk (a graph re-save, or a hand-edited
    // .fs saved from the user's IDE).
    void InvalidateMaterialShader(const std::string& shaderId);
    void InvalidateAllMaterialShaders();
    // Drops the cached program and rebuilds it immediately, reporting the GL
    // info log on failure. The previously cached program is only replaced once
    // the new one compiles and links, so a broken edit never blacks out the
    // viewport. Must be called with the GL context current.
    bool TryCompileMaterialShader(const std::string& shaderId, std::string& outLog);

    // Fallback camera setup used by simple pipeline; later swap to scene camera
    void SetCamera(const glm::mat4& view, const glm::mat4& proj);
    const glm::mat4& GetView() const { return view_; }
    const glm::mat4& GetProj() const { return proj_; }

    // Asset previews should not inherit whatever sky the scene happens to have:
    // a material reads very differently under a sunset than under a neutral
    // studio, and a thumbnail is meant to describe the material, not the level.
    // These install a fixed neutral studio environment (built once, on demand)
    // for the draws issued between them, the way Unity previews materials.
    void PushPreviewEnvironment();
    void PopPreviewEnvironment();

    const EnvironmentMaps& GetEnvironmentMaps() const { return environmentMaps_; }
    bool HasEnvironmentSource() const { return environmentMaps_.source != 0; }
    bool IsEnvironmentBakeReady() const { return environmentReady_; }
    float GetEnvironmentAverageLuminance() const { return environmentAverageLuminance_; }
    RendererSettings& GetSettings() { return settings_; }
    const RendererSettings& GetSettings() const { return settings_; }
    // Profile actually used to draw `target`: the project profile untouched for
    // the Game View, and the project profile with the Scene View shading mode
    // folded in for the Scene View.
    GraphicsProfile ResolveProfileForTarget(ViewportRenderTarget target) const;
    // Fog block for whatever is rendering right now, with the sun direction taken
    // from the shadow-casting directional light in the current frame's light list.
    // Call between Submit and Flush; the sun falls back to straight down when the
    // scene has no directional light.
    FogUniforms GetFogUniforms() const;
    // Simulated wetness for play mode. Kept out of GraphicsProfile deliberately:
    // the authored value is what gets saved, this is what gets rendered.
    void SetRuntimeWetness(float wetness) { runtimeWetness_ = wetness; runtimeWetnessActive_ = true; }
    void ClearRuntimeWetness() { runtimeWetnessActive_ = false; }
    float GetEffectiveWetness() const { return runtimeWetnessActive_ ? runtimeWetness_ : settings_.profile.wetness; }
    glm::vec3 GetCameraPosition() const { return glm::vec3(glm::inverse(view_)[3]); }
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
        unsigned int smaaColorFramebuffer{0};
        unsigned int smaaColorTexture{0};
        unsigned int smaaHdrColorFramebuffer{0};
        unsigned int smaaHdrColorTexture{0};
        unsigned int smaaEdgeFramebuffer{0};
        unsigned int smaaEdgeTexture{0};
        unsigned int smaaWeightFramebuffer{0};
        unsigned int smaaWeightTexture{0};
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
        // Auto-exposure state. Per viewport target, because the Scene View and
        // Game View look at different things and must adapt independently.
        unsigned int luminanceHistogramBuffer{0};
        unsigned int adaptedLuminanceTexture{0};
        bool adaptedLuminanceInitialized{false};
        int width{0};
        int height{0};
        int requestedWidth{0};
        int requestedHeight{0};
        float resolutionScale{1.0f};
        float smoothedFrameTimeMs{0.0f};
        double lastFrameBeginSeconds{0.0};
        // Delta for this target measured at BeginFrame, consumed by the exposure
        // adaptation pass later in the same frame.
        float lastFrameDeltaSeconds{0.0f};
        int resolutionAdjustmentCooldown{0};
        glm::mat4 previousViewProjection{1.0f};
        bool hasPreviousViewProjection{false};
        std::unordered_map<std::string, glm::mat4> previousModelMatrices;
    };

    void InitializePipelines();
    void InitializeQuad();
    void InitializeSsaoResources();
    void RenderCaptureCube() const;
    bool CaptureReflectionProbeFaces(const glm::vec3& position,
                                     int resolution,
                                     unsigned int cubemap,
                                     int firstFace,
                                     int faceCount,
                                     const std::function<void()>& submitScene,
                                     const std::function<void(int, int)>& progress);
    void ResolveViewportTarget(ViewportTarget& target);
    void RenderOverlayLines(ViewportTarget& target);
    // Projected decals, drawn after all geometry while the MRT target is still
    // bound. Reads the depth and normal attachments of that same target, which is
    // why it issues a texture barrier first.
    void RenderDecals(const ViewportTarget& target);
    // Real, world-positioned rain drops, drawn as instanced streak billboards
    // while the MRT target's own depth attachment is still bound, so drops are
    // hardware depth-tested against every opaque surface exactly like any
    // other translucent geometry. This is what gives correct occlusion and
    // real parallax with camera translation, not just rotation.
    void RenderRainParticles(const ViewportTarget& target);
    void DestroyViewportTarget(ViewportTarget& target);
    ViewportTarget& GetViewportTarget(ViewportRenderTarget target);
    const ViewportTarget& GetViewportTarget(ViewportRenderTarget target) const;
    // Maps a material shader id (built-in, "graph:", or "file:") to the cache key
    // and the vertex/fragment sources behind it, falling back to pbr when the
    // fragment source is missing.
    void ResolveMaterialShaderSources(const std::string& shaderId,
                                      std::string& outCacheKey,
                                      std::string& outVertexPath,
                                      std::string& outFragmentPath) const;
    Shader* AcquireMaterialShader(const std::string& shaderId);
    // Convolves a source cubemap into irradiance + prefiltered reflection maps.
    // Shared by the scene environment and the preview studio environment.
    void BakeEnvironmentMaps(unsigned int sourceCubemap,
                             EnvironmentMaps& outMaps,
                             bool& outReady,
                             float& outAverageLuminance);
    // Procedural neutral studio cubemap: soft vertical gradient plus a key and
    // fill softbox, so smooth metals show a readable reflection instead of a
    // flat blown-out disc. Built once and reused.
    void EnsureStudioEnvironment();
    // The profile in force for whatever is rendering right now. Outside a
    // viewport pass - resource allocation, reflection probe bakes - this falls
    // back to the project profile, so Scene View overrides can never leak into
    // baked data or into the Game View's render targets.
    const GraphicsProfile& Profile() const {
        return viewportTargetActive_ ? activeProfile_ : settings_.profile;
    }

    RendererConfig config_{};
    RendererViewport viewport_{};
    std::vector<MeshDrawCommand> drawList_;
    std::vector<MeshDrawCommand> motionVectorDrawList_;
    std::vector<MeshDrawCommand> shadowCasterList_;
    std::vector<LightDrawCommand> lightDrawList_;
    std::vector<ReflectionProbeDrawCommand> reflectionProbeDrawList_;
    std::vector<DecalDrawCommand> decalDrawList_;
    std::vector<WeatherShelterDrawCommand> weatherShelterDrawList_;
    std::unordered_map<std::string, unsigned int> reflectionProbeCubemapCache_;
    struct RealtimeReflectionProbeState {
        unsigned int cubemap{0};
        int resolution{0};
        int nextFace{0};
        bool completedFullRound{false};
    };
    std::unordered_map<std::string, RealtimeReflectionProbeState> realtimeReflectionProbes_;
    EnvironmentMaps environmentMaps_{};
    // Preview studio environment, and the scene state saved while it is active.
    EnvironmentMaps studioEnvironmentMaps_{};
    bool studioEnvironmentReady_{false};
    float studioEnvironmentAverageLuminance_{0.0f};
    bool studioEnvironmentBuildAttempted_{false};
    unsigned int studioEnvironmentSource_{0};
    int previewEnvironmentDepth_{0};
    // True only while the studio environment is actually installed. The preview
    // uniform overrides key off this, never off the depth counter, so a failed
    // bake cannot leave them applied with no studio environment behind them.
    bool previewEnvironmentApplied_{false};
    EnvironmentMaps savedEnvironmentMaps_{};
    bool savedEnvironmentReady_{false};
    float savedEnvironmentAverageLuminance_{0.0f};
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
    std::unique_ptr<Shader> rainParticleShader_;
    unsigned int rainQuadVao_{0};
    unsigned int rainQuadVbo_{0};
    std::unique_ptr<Shader> depthOfFieldShader_;
    std::unique_ptr<Shader> taaShader_;
    std::unique_ptr<Shader> luminanceHistogramShader_;
    std::unique_ptr<Shader> luminanceAverageShader_;
    std::unique_ptr<Shader> smaaEdgeShader_;
    std::unique_ptr<Shader> smaaWeightsShader_;
    std::unique_ptr<Shader> smaaBlendShader_;
    std::unique_ptr<Shader> debugLineShader_;
    std::unique_ptr<Shader> decalShader_;
    unsigned int smaaAreaTexture_{0};
    unsigned int smaaSearchTexture_{0};
    std::unique_ptr<Shader> shadowDepthShader_;
    std::unique_ptr<Shader> pointShadowDepthShader_;
    std::unique_ptr<Shader> irradianceShader_;
    std::unique_ptr<Shader> prefilterShader_;
    std::unique_ptr<Shader> brdfShader_;
    std::unique_ptr<Shader> reflectionCaptureSkyShader_;
    std::unordered_map<std::string, std::unique_ptr<Shader>> materialShaders_;
    ViewportRenderTarget activeViewportTarget_{ViewportRenderTarget::Scene};
    bool viewportTargetActive_{false};
    GraphicsProfile activeProfile_{};
    glm::mat4 view_{1.0f};
    glm::mat4 proj_{1.0f};
    glm::mat4 unjitteredProj_{1.0f};
    // Sun direction for fog inscattering, carried across the point in the frame
    // where it is unavailable: the sky is drawn before SubmitDraws has populated
    // the light list, so without this the horizon would lose its sun glow while
    // the geometry in front of it kept it.
    mutable glm::vec3 lastSunDirection_{0.0f, -1.0f, 0.0f};
    mutable bool lastSunValid_{false};
    float runtimeWetness_{0.0f};
    bool runtimeWetnessActive_{false};
};

} // namespace raceman

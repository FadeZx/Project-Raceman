#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace raceman {

struct RendererConfig {
    int width{1920};
    int height{1080};
};

struct RendererSettings {
    float exposure{1.0f};
    float gamma{2.2f};
    glm::vec3 clearColor{0.02f, 0.02f, 0.02f};
    bool enableShadows{true};
    bool showEnvironmentDebugView{false};
};

struct MeshDrawCommand {
    unsigned int vao{0};
    unsigned int indexCount{0};
    glm::mat4 modelMatrix{1.0f};
    std::string materialId;
};

struct EnvironmentMaps {
    unsigned int irradiance{0};
    unsigned int prefiltered{0};
    unsigned int brdfLut{0};
};

class Renderer {
public:
    explicit Renderer(const RendererConfig& config);
    ~Renderer();

    void BeginFrame();
    void EndFrame();

    void SetupEnvironment(const std::string& hdrPath);
    void BakeBrdfLut();
    void CreateShadowMaps(int resolution);

    void SubmitMesh(const MeshDrawCommand& cmd);
    void Flush();

    const EnvironmentMaps& GetEnvironmentMaps() const { return environmentMaps_; }
    RendererSettings& GetSettings() { return settings_; }
    const RendererSettings& GetSettings() const { return settings_; }
    const RendererConfig& GetConfig() const;

private:
    void InitializePipelines();
    void InitializeQuad();

    RendererConfig config_{};
    std::vector<MeshDrawCommand> drawList_;
    EnvironmentMaps environmentMaps_{};
    RendererSettings settings_{};
    unsigned int captureFbo_{0};
    unsigned int captureRbo_{0};
    unsigned int fullscreenQuad_{0};
    std::vector<unsigned int> shadowMaps_;
};

} // namespace raceman

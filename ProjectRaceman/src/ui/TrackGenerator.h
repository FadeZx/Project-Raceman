#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace raceman {

enum class TrackGeneratorMode {
    Draw,
    Preset
};

enum class TrackPresetType {
    Oval,
    RoundedRectangle,
    SCurve
};

struct TrackSource {
    std::string name{"New Track"};
    bool closed{false};
    float roadWidth{10.0f};
    float shoulderWidth{2.0f};
    float segmentResolution{2.0f};
    TrackPresetType presetType{TrackPresetType::Oval};
    std::vector<glm::vec3> controlPoints;
    std::string bakedMeshPath;
    std::string bakedShoulderMeshPath;
    std::string materialId{"pbr_default"};
    std::string shoulderMaterialId{"pbr_default"};
};

struct TrackMeshData {
    struct Vertex {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec2 uv{0.0f};
    };

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

const char* TrackPresetTypeToString(TrackPresetType type);
TrackPresetType TrackPresetTypeFromString(const std::string& value);

std::vector<glm::vec3> BuildTrackPresetPoints(TrackPresetType type,
                                              float length,
                                              float width,
                                              float radius,
                                              int pointCount);

std::vector<glm::vec3> SampleCenterline(const TrackSource& source, std::string* outError = nullptr);
bool BuildTrackMesh(const TrackSource& source, TrackMeshData& outMesh, std::string* outError = nullptr);
bool BuildTrackRoadMesh(const TrackSource& source, TrackMeshData& outMesh, std::string* outError = nullptr);
bool BuildTrackShoulderMesh(const TrackSource& source, TrackMeshData& outMesh, std::string* outError = nullptr);
bool SaveTrackSource(const std::string& absolutePath, const TrackSource& source, std::string* outError = nullptr);
bool LoadTrackSource(const std::string& absolutePath, TrackSource& outSource, std::string* outError = nullptr);
bool BakeTrackObj(const std::string& absolutePath, const TrackMeshData& mesh, std::string* outError = nullptr);

} // namespace raceman

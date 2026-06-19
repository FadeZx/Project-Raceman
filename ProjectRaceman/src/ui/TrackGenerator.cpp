#include "TrackGenerator.h"
#include "../physics/SimpleJson.h"
#include "SceneEditorInternal.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace raceman {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float ClampPositive(float value, float fallback) {
    return value > 0.001f ? value : fallback;
}

std::string JsonEscapeLocal(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1)
        + (-p0 + p2) * t
        + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

glm::vec3 SafePoint(const std::vector<glm::vec3>& points, int index, bool closed) {
    const int count = static_cast<int>(points.size());
    if (closed) {
        int wrapped = index % count;
        if (wrapped < 0) {
            wrapped += count;
        }
        return points[static_cast<std::size_t>(wrapped)];
    }
    return points[static_cast<std::size_t>((std::max)(0, (std::min)(count - 1, index)))];
}

std::vector<glm::vec3> CleanPoints(const std::vector<glm::vec3>& points) {
    std::vector<glm::vec3> cleaned;
    cleaned.reserve(points.size());
    for (const glm::vec3& point : points) {
        glm::vec3 flat{point.x, 0.0f, point.z};
        if (!cleaned.empty() && glm::length(flat - cleaned.back()) < 0.05f) {
            continue;
        }
        cleaned.push_back(flat);
    }
    return cleaned;
}

std::vector<glm::vec3> SampleCenterlineInternal(const TrackSource& source, std::string* outError) {
    std::vector<glm::vec3> points = CleanPoints(source.controlPoints);
    const int count = static_cast<int>(points.size());
    if (count < 2) {
        if (outError) *outError = "Track needs at least 2 control points.";
        return {};
    }
    if (source.closed && count < 3) {
        if (outError) *outError = "Closed track needs at least 3 control points.";
        return {};
    }

    const float resolution = ClampPositive(source.segmentResolution, 2.0f);
    std::vector<glm::vec3> samples;
    const int segmentCount = source.closed ? count : count - 1;
    for (int segment = 0; segment < segmentCount; ++segment) {
        const glm::vec3 p1 = SafePoint(points, segment, source.closed);
        const glm::vec3 p2 = SafePoint(points, segment + 1, source.closed);
        const glm::vec3 p0 = SafePoint(points, segment - 1, source.closed);
        const glm::vec3 p3 = SafePoint(points, segment + 2, source.closed);
        const float distance = (std::max)(0.1f, glm::length(p2 - p1));
        const int steps = (std::max)(1, static_cast<int>(std::ceil(distance / resolution)));
        for (int step = 0; step < steps; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(steps);
            samples.push_back(CatmullRom(p0, p1, p2, p3, t));
        }
    }
    if (!source.closed) {
        samples.push_back(points.back());
    }
    return samples;
}

bool ReadFloat(const raceman::physics::json::Object& object, const std::string& key, float& out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->second.is_number()) {
        return false;
    }
    out = static_cast<float>(it->second.as_number());
    return true;
}

enum class TrackMeshSurface {
    Full,
    Road,
    Shoulder
};

} // namespace

const char* TrackPresetTypeToString(TrackPresetType type) {
    switch (type) {
    case TrackPresetType::RoundedRectangle: return "roundedRectangle";
    case TrackPresetType::SCurve: return "sCurve";
    case TrackPresetType::Oval:
    default: return "oval";
    }
}

TrackPresetType TrackPresetTypeFromString(const std::string& value) {
    if (value == "roundedRectangle") return TrackPresetType::RoundedRectangle;
    if (value == "sCurve") return TrackPresetType::SCurve;
    return TrackPresetType::Oval;
}

std::vector<glm::vec3> SampleCenterline(const TrackSource& source, std::string* outError) {
    return SampleCenterlineInternal(source, outError);
}

std::vector<glm::vec3> BuildTrackPresetPoints(TrackPresetType type,
                                              float length,
                                              float width,
                                              float radius,
                                              int pointCount) {
    length = (std::max)(20.0f, length);
    width = (std::max)(10.0f, width);
    radius = (std::max)(2.0f, radius);
    pointCount = (std::max)(8, pointCount);

    std::vector<glm::vec3> points;
    if (type == TrackPresetType::SCurve) {
        points.reserve(static_cast<std::size_t>(pointCount));
        for (int i = 0; i < pointCount; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(pointCount - 1);
            const float x = (t - 0.5f) * length;
            const float z = std::sin(t * kPi * 2.0f) * width * 0.35f;
            points.push_back({x, 0.0f, z});
        }
        return points;
    }

    points.reserve(static_cast<std::size_t>(pointCount));
    if (type == TrackPresetType::RoundedRectangle) {
        const float hx = length * 0.5f - radius;
        const float hz = width * 0.5f - radius;
        for (int i = 0; i < pointCount; ++i) {
            const float a = (static_cast<float>(i) / static_cast<float>(pointCount)) * kPi * 2.0f;
            const float cx = std::cos(a) >= 0.0f ? hx : -hx;
            const float cz = std::sin(a) >= 0.0f ? hz : -hz;
            points.push_back({cx + std::cos(a) * radius, 0.0f, cz + std::sin(a) * radius});
        }
        return points;
    }

    for (int i = 0; i < pointCount; ++i) {
        const float a = (static_cast<float>(i) / static_cast<float>(pointCount)) * kPi * 2.0f;
        points.push_back({std::cos(a) * length * 0.5f, 0.0f, std::sin(a) * width * 0.5f});
    }
    return points;
}

static bool BuildTrackSurfaceMesh(const TrackSource& source, TrackMeshSurface surface, TrackMeshData& outMesh, std::string* outError) {
    outMesh = {};
    std::string sampleError;
    const std::vector<glm::vec3> samples = SampleCenterline(source, &sampleError);
    if (samples.size() < 2) {
        if (outError) *outError = sampleError.empty() ? "Track sampling failed." : sampleError;
        return false;
    }

    const float roadHalf = ClampPositive(source.roadWidth, 10.0f) * 0.5f;
    const float shoulder = (std::max)(0.0f, source.shoulderWidth);
    const float totalHalf = roadHalf + shoulder;
    if (surface == TrackMeshSurface::Shoulder && shoulder <= 0.001f) {
        if (outError) *outError = "Shoulder width is zero.";
        return false;
    }

    std::vector<float> offsets;
    std::vector<std::pair<int, int>> strips;
    if (surface == TrackMeshSurface::Road) {
        offsets = {-roadHalf, roadHalf};
        strips = {{0, 1}};
    } else {
        offsets = {-totalHalf, -roadHalf, roadHalf, totalHalf};
        strips = surface == TrackMeshSurface::Shoulder
            ? std::vector<std::pair<int, int>>{{0, 1}, {2, 3}}
            : std::vector<std::pair<int, int>>{{0, 1}, {1, 2}, {2, 3}};
    }

    const int sampleCount = static_cast<int>(samples.size());
    const int segmentCount = source.closed ? sampleCount : sampleCount - 1;
    const int vertsPerSample = static_cast<int>(offsets.size());
    outMesh.vertices.reserve(samples.size() * offsets.size());
    outMesh.indices.reserve(static_cast<std::size_t>(segmentCount * strips.size() * 6));

    std::vector<float> distances(samples.size(), 0.0f);
    for (std::size_t i = 1; i < samples.size(); ++i) {
        distances[i] = distances[i - 1] + glm::length(samples[i] - samples[i - 1]);
    }

    for (int i = 0; i < sampleCount; ++i) {
        const glm::vec3 prev = samples[static_cast<std::size_t>(i == 0 ? (source.closed ? sampleCount - 1 : 0) : i - 1)];
        const glm::vec3 next = samples[static_cast<std::size_t>(i + 1 >= sampleCount ? (source.closed ? 0 : sampleCount - 1) : i + 1)];
        glm::vec3 tangent = next - prev;
        if (glm::length(tangent) < 0.0001f) {
            tangent = {1.0f, 0.0f, 0.0f};
        }
        tangent = glm::normalize(tangent);
        const glm::vec3 side = glm::normalize(glm::vec3(-tangent.z, 0.0f, tangent.x));
        const glm::vec3 center = samples[static_cast<std::size_t>(i)];
        const float u = distances[static_cast<std::size_t>(i)] / 10.0f;
        const glm::vec3 normal{0.0f, 1.0f, 0.0f};
        for (float offset : offsets) {
            const float v = totalHalf > 0.001f ? (offset + totalHalf) / (totalHalf * 2.0f) : 0.5f;
            outMesh.vertices.push_back({center + side * offset, normal, {u, v}});
        }
    }

        auto addQuad = [&](int a, int b, int c, int d) {
            outMesh.indices.push_back(static_cast<unsigned int>(a));
            outMesh.indices.push_back(static_cast<unsigned int>(c));
            outMesh.indices.push_back(static_cast<unsigned int>(b));
            outMesh.indices.push_back(static_cast<unsigned int>(c));
            outMesh.indices.push_back(static_cast<unsigned int>(d));
            outMesh.indices.push_back(static_cast<unsigned int>(b));
        };
    for (int i = 0; i < segmentCount; ++i) {
        const int j = i + 1 >= sampleCount ? 0 : i + 1;
        const int a = i * vertsPerSample;
        const int b = j * vertsPerSample;
        for (const auto& strip : strips) {
            addQuad(a + strip.first, b + strip.first, a + strip.second, b + strip.second);
        }
    }

    if (!outMesh.vertices.empty()) {
        outMesh.boundsMin = outMesh.vertices.front().position;
        outMesh.boundsMax = outMesh.vertices.front().position;
        for (const TrackMeshData::Vertex& vertex : outMesh.vertices) {
            outMesh.boundsMin.x = (std::min)(outMesh.boundsMin.x, vertex.position.x);
            outMesh.boundsMin.y = (std::min)(outMesh.boundsMin.y, vertex.position.y);
            outMesh.boundsMin.z = (std::min)(outMesh.boundsMin.z, vertex.position.z);
            outMesh.boundsMax.x = (std::max)(outMesh.boundsMax.x, vertex.position.x);
            outMesh.boundsMax.y = (std::max)(outMesh.boundsMax.y, vertex.position.y);
            outMesh.boundsMax.z = (std::max)(outMesh.boundsMax.z, vertex.position.z);
        }
    }
    return true;
}

bool BuildTrackMesh(const TrackSource& source, TrackMeshData& outMesh, std::string* outError) {
    return BuildTrackSurfaceMesh(source, TrackMeshSurface::Full, outMesh, outError);
}

bool BuildTrackRoadMesh(const TrackSource& source, TrackMeshData& outMesh, std::string* outError) {
    return BuildTrackSurfaceMesh(source, TrackMeshSurface::Road, outMesh, outError);
}

bool BuildTrackShoulderMesh(const TrackSource& source, TrackMeshData& outMesh, std::string* outError) {
    return BuildTrackSurfaceMesh(source, TrackMeshSurface::Shoulder, outMesh, outError);
}

bool SaveTrackSource(const std::string& absolutePath, const TrackSource& source, std::string* outError) {
    try {
        fs::create_directories(fs::path(absolutePath).parent_path());
        std::ofstream out(absolutePath, std::ios::binary);
        if (!out.good()) {
            if (outError) *outError = "Unable to open track source for writing.";
            return false;
        }
        out << "{\n";
        out << "  \"name\": \"" << JsonEscapeLocal(source.name) << "\",\n";
        out << "  \"closed\": " << (source.closed ? "true" : "false") << ",\n";
        out << "  \"roadWidth\": " << source.roadWidth << ",\n";
        out << "  \"shoulderWidth\": " << source.shoulderWidth << ",\n";
        out << "  \"segmentResolution\": " << source.segmentResolution << ",\n";
        out << "  \"presetType\": \"" << TrackPresetTypeToString(source.presetType) << "\",\n";
        out << "  \"bakedMeshPath\": \"" << JsonEscapeLocal(scene_editor_internal::NormalizeSlashes(source.bakedMeshPath)) << "\",\n";
        out << "  \"bakedShoulderMeshPath\": \"" << JsonEscapeLocal(scene_editor_internal::NormalizeSlashes(source.bakedShoulderMeshPath)) << "\",\n";
        out << "  \"materialId\": \"" << JsonEscapeLocal(source.materialId) << "\",\n";
        out << "  \"shoulderMaterialId\": \"" << JsonEscapeLocal(source.shoulderMaterialId) << "\",\n";
        out << "  \"controlPoints\": [\n";
        for (std::size_t i = 0; i < source.controlPoints.size(); ++i) {
            const glm::vec3& p = source.controlPoints[i];
            out << "    [" << p.x << ", " << p.y << ", " << p.z << "]" << (i + 1 < source.controlPoints.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return true;
    } catch (const std::exception& ex) {
        if (outError) *outError = ex.what();
        return false;
    }
}

bool LoadTrackSource(const std::string& absolutePath, TrackSource& outSource, std::string* outError) {
    try {
        std::ifstream in(absolutePath, std::ios::binary);
        if (!in.good()) {
            if (outError) *outError = "Unable to open track source.";
            return false;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        const auto root = raceman::physics::json::parse(buffer.str());
        if (!root.is_object()) {
            if (outError) *outError = "Track source root must be an object.";
            return false;
        }
        TrackSource source;
        const auto& object = root.as_object();
        scene_editor_internal::ReadString(object, "name", source.name);
        scene_editor_internal::ReadBool(object, "closed", source.closed);
        ReadFloat(object, "roadWidth", source.roadWidth);
        ReadFloat(object, "shoulderWidth", source.shoulderWidth);
        ReadFloat(object, "segmentResolution", source.segmentResolution);
        std::string preset;
        if (scene_editor_internal::ReadString(object, "presetType", preset)) {
            source.presetType = TrackPresetTypeFromString(preset);
        }
        scene_editor_internal::ReadString(object, "bakedMeshPath", source.bakedMeshPath);
        source.bakedMeshPath = scene_editor_internal::NormalizeSlashes(source.bakedMeshPath);
        scene_editor_internal::ReadString(object, "bakedShoulderMeshPath", source.bakedShoulderMeshPath);
        source.bakedShoulderMeshPath = scene_editor_internal::NormalizeSlashes(source.bakedShoulderMeshPath);
        scene_editor_internal::ReadString(object, "materialId", source.materialId);
        scene_editor_internal::ReadString(object, "shoulderMaterialId", source.shoulderMaterialId);
        const auto pointsIt = object.find("controlPoints");
        if (pointsIt != object.end() && pointsIt->second.is_array()) {
            for (const auto& value : pointsIt->second.as_array()) {
                if (!value.is_array() || value.as_array().size() < 3) {
                    continue;
                }
                const auto& arr = value.as_array();
                if (arr[0].is_number() && arr[1].is_number() && arr[2].is_number()) {
                    source.controlPoints.push_back({
                        static_cast<float>(arr[0].as_number()),
                        static_cast<float>(arr[1].as_number()),
                        static_cast<float>(arr[2].as_number())
                    });
                }
            }
        }
        outSource = std::move(source);
        return true;
    } catch (const std::exception& ex) {
        if (outError) *outError = ex.what();
        return false;
    }
}

bool BakeTrackObj(const std::string& absolutePath, const TrackMeshData& mesh, std::string* outError) {
    try {
        fs::create_directories(fs::path(absolutePath).parent_path());
        std::ofstream out(absolutePath, std::ios::binary);
        if (!out.good()) {
            if (outError) *outError = "Unable to open OBJ for writing.";
            return false;
        }
        out << "# Project Raceman generated track\n";
        out << "o track\n";
        for (const auto& vertex : mesh.vertices) {
            out << "v " << vertex.position.x << " " << vertex.position.y << " " << vertex.position.z << "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            out << "vt " << vertex.uv.x << " " << vertex.uv.y << "\n";
        }
        out << "vn 0 1 0\n";
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const unsigned int a = mesh.indices[i] + 1;
            const unsigned int b = mesh.indices[i + 1] + 1;
            const unsigned int c = mesh.indices[i + 2] + 1;
            out << "f " << a << "/" << a << "/1 "
                << b << "/" << b << "/1 "
                << c << "/" << c << "/1\n";
        }
        return true;
    } catch (const std::exception& ex) {
        if (outError) *outError = ex.what();
        return false;
    }
}

} // namespace raceman

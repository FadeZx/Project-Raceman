#include "SkyboxController.h"

#include "Skybox.h"
#include "shader.h"

#include <filesystem>
#include <vector>

namespace raceman {
namespace fs = std::filesystem;

namespace {
fs::path ProjectRootPath() {
    if (fs::exists("ProjectRaceman/src") && fs::is_directory("ProjectRaceman/src")) {
        return fs::absolute("ProjectRaceman/Project").lexically_normal();
    }
    if (fs::exists("src") && fs::is_directory("src")) {
        return fs::absolute("Project").lexically_normal();
    }
    return fs::absolute("Project").lexically_normal();
}

fs::path EngineRootPath() {
    if (fs::exists("ProjectRaceman/src") && fs::is_directory("ProjectRaceman/src")) {
        return fs::absolute("ProjectRaceman").lexically_normal();
    }
    if (fs::exists("src") && fs::is_directory("src")) {
        return fs::absolute(".").lexically_normal();
    }
    return fs::absolute(".").lexically_normal();
}

std::string ProjectPath(const std::string& relativePath) {
    return (ProjectRootPath() / fs::path(relativePath)).lexically_normal().string();
}

std::string EnginePath(const std::string& relativePath) {
    return (EngineRootPath() / fs::path(relativePath)).lexically_normal().string();
}
} // namespace

static SkyboxFaces MakeDefaultFaces() {
    // Default to the sunset set.
    return SkyboxFaces{
        ProjectPath("assets/skybox/sunset/px.jpg"),
        ProjectPath("assets/skybox/sunset/nx.jpg"),
        ProjectPath("assets/skybox/sunset/py.jpg"),
        ProjectPath("assets/skybox/sunset/ny.jpg"),
        ProjectPath("assets/skybox/sunset/pz.jpg"),
        ProjectPath("assets/skybox/sunset/nz.jpg"),
    };
}

SkyboxFaces SkyboxController::DefaultFaces() { return MakeDefaultFaces(); }

SkyboxController::SkyboxController() : faces_(DefaultFaces()) {
    // Default shader paths relative to executable working directory
    vsPath_ = EnginePath("src/shaders/skybox/skybox.vs");
    fsPath_ = EnginePath("src/shaders/skybox/skybox.fs");
}

SkyboxController::~SkyboxController() = default;

void SkyboxController::SetFaces(const SkyboxFaces& faces) {
    faces_ = faces;
}

void SkyboxController::SetFace(int index, const std::string& path) {
    if (index >= 0 && index < 6) {
        faces_[static_cast<size_t>(index)] = path;
    }
}

const SkyboxFaces& SkyboxController::GetFaces() const {
    return faces_;
}

void SkyboxController::SetShaderPaths(const std::string& vsPath, const std::string& fsPath) {
    vsPath_ = vsPath;
    fsPath_ = fsPath;
    shader_.reset(); // force recreate on next Reload
}

unsigned int SkyboxController::GetProgramId() const {
    return shader_ ? shader_->getID() : 0u;
}

void SkyboxController::Reload() {
    if (!shader_) {
        shader_ = std::make_unique<Shader>(vsPath_.c_str(), fsPath_.c_str());
    }
    std::vector<std::string> faceVec(faces_.begin(), faces_.end());
    skybox_ = std::make_unique<Skybox>(faceVec, shader_->getID());
}

void SkyboxController::Draw(const glm::mat4& view, const glm::mat4& projection) {
    if (skybox_) {
        skybox_->draw(view, projection);
    }
}

} // namespace raceman

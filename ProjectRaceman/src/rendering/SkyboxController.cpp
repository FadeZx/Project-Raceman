#include "SkyboxController.h"

#include "Skybox.h"
#include "shader.h"

#include <vector>

namespace raceman {

static SkyboxFaces MakeDefaultFaces() {
    // Default to racetrack set if available
    return SkyboxFaces{
        "assets/skybox/sunset/px.jpg",
        "assets/skybox/sunset/nx.jpg",
        "assets/skybox/sunset/py.jpg",
        "assets/skybox/sunset/ny.jpg",
        "assets/skybox/sunset/pz.jpg",
        "assets/skybox/sunset/nz.jpg",
    };
}

SkyboxFaces SkyboxController::DefaultFaces() { return MakeDefaultFaces(); }

SkyboxController::SkyboxController() : faces_(DefaultFaces()) {
    // Default shader paths relative to executable working directory
    vsPath_ = "src/shaders/skybox/skybox.vs";
    fsPath_ = "src/shaders/skybox/skybox.fs";
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
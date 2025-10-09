#include "Application.h"
#include "physics/PhysicsLayer.h"
#include "rendering/Renderer.h"
#include "scenes/GarageScene.h"
#include "scenes/SimulationScene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <vector>

int main() {
    using namespace raceman;

    ApplicationConfig config;
    Application app(config);

    auto renderer = app.GetRendererPtr();

    auto garageScene = std::make_shared<GarageScene>(renderer);
    auto simulationScene = std::make_shared<SimulationScene>(renderer);

    app.RegisterScene(garageScene);
    app.RegisterScene(simulationScene);
    app.SwitchScene(0);

    app.Run();

    return 0;
}

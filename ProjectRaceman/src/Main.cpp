#include "Application.h"
#include "physics/PhysicsLayer.h"
#include "rendering/Renderer.h"
#include "scenes/GarageScene.h"
#include "scenes/SimulationScene.h"

#include <memory>

int main() {
    using namespace raceman;

    ApplicationConfig config;
    Application app(config);

    auto renderer = std::shared_ptr<Renderer>(&app.GetRenderer(), [](Renderer*) {});

    auto garageScene = std::make_shared<GarageScene>(renderer);
    auto simulationScene = std::make_shared<SimulationScene>(renderer);

    auto physics = std::make_shared<PhysicsLayer>();
    simulationScene->SetPhysicsLayer(physics);

    app.RegisterScene(garageScene);
    app.RegisterScene(simulationScene);
    app.SwitchScene(0);

    app.Run();

    return 0;
}

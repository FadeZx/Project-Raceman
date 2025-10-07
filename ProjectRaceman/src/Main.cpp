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

    auto physics = std::make_shared<PhysicsLayer>();

    std::vector<RigidBodyState> bodies;
    RigidBodyState carA;
    carA.meshId = "car_a";
    carA.transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, -5.0f));
    bodies.push_back(carA);

    RigidBodyState carB;
    carB.meshId = "car_b";
    carB.transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, -6.5f));
    bodies.push_back(carB);

    physics->SetBodies(std::move(bodies));
    simulationScene->SetPhysicsLayer(physics);

    app.RegisterScene(garageScene);
    app.RegisterScene(simulationScene);
    app.SwitchScene(0);

    app.Run();

    return 0;
}

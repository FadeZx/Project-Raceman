#pragma once
#include <glm/glm.hpp>

namespace Physics {

    struct Params {
        float m = 1200.0f;
        float Iz = 1800.0f;
        float lf = 1.2f;
        float lr = 1.3f;
        float Cf = 70000.0f;
        float Cr = 80000.0f;
        float mu = 1.1f;
        float CdA = 0.6f;
        float ClA = 0.0f;
        float rho = 1.225f;
        float g = 9.81f;
        float Fx_max = 8000.0f;
        float Fb_max = 12000.0f;
    };

    struct VehicleState {
        glm::vec3 pos{ 0, 0.5f, 0 };
        float yaw = 0.0f;  // rad
        float vx = 0.0f;   // m/s
        float vy = 0.0f;   // m/s
        float r = 0.0f;   // rad/s
    };

    struct Controls { float steer = 0, throttle = 0, brake = 0; };

    extern Params P;          // DECLARATIONS (no storage here)
    extern VehicleState gCar;

    void Init();
    void Update(double dt,const Controls& c);

} // namespace Physics

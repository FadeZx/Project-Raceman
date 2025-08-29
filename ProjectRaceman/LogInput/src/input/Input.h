#pragma once
#include <SDL3/SDL.h>

namespace Input {

    struct InputState {
        float steer = 0.0f; // -1..1
        float throttle = 0.0f; // 0..1
        float brake = 0.0f; // 0..1
        bool gearUp = false;
        bool gearDown = false;
        bool handbrake = false;
    };

    // init/cleanup
    void Init();
    void Shutdown();

    // poll from SDL events + keyboard
    void Update();

    // get latest normalized state
    const InputState& GetState();
}

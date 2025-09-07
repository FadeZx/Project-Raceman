#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <unordered_map>

namespace Input {

    enum class Action {
        SteerLeft,
        SteerRight,
        Throttle,
        Brake,
        GearUp,
        GearDown,
        Handbrake
    };

    struct InputState {
        float steer = 0.0f;   // -1..1
        float throttle = 0.0f; // 0..1
        float brake = 0.0f;    // 0..1
        bool  gearUp = false;
        bool  gearDown = false;
        bool  handbrake = false;
    };

    extern std::unordered_map<Action, SDL_Scancode> Bindings;

    void Init();
    void Shutdown();
    void Update();

    // Persist bindings
    void SaveBindings(const char* path);
    bool LoadBindings(const char* path);

	const InputState& GetState();

    // Names
    const char* GetActionName(Action a);
}

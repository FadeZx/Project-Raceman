#include "Input.h"

namespace Input {
    static InputState s_State;

    void Init() {
        // later: open controllers, wheels etc.
    }

    void Shutdown() {
        // later: close controllers, wheels etc.
    }

    void Update() {
        const bool* ks = SDL_GetKeyboardState(nullptr);

        s_State.steer = (ks[SDL_SCANCODE_A] ? -1.0f : 0.0f)
            + (ks[SDL_SCANCODE_D] ? 1.0f : 0.0f);
        s_State.throttle = ks[SDL_SCANCODE_W] ? 1.0f : 0.0f;
        s_State.brake = ks[SDL_SCANCODE_S] ? 1.0f : 0.0f;

        s_State.gearUp = ks[SDL_SCANCODE_E];
        s_State.gearDown = ks[SDL_SCANCODE_Q];
        s_State.handbrake = ks[SDL_SCANCODE_SPACE];
    }

    const InputState& GetState() {
        return s_State;
    }
}

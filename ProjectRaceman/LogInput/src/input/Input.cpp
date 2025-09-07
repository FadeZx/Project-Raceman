#include "Input.h"
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <cctype>      // isspace, isdigit
#include <algorithm>   // std::max/min

namespace Input {

    // -------------------- State & bindings --------------------
    static InputState s_State;
    std::unordered_map<Action, SDL_Scancode> Bindings;

    // -------------- Names (no kActionNames needed) ------------
    const char* GetActionName(Action a) {
        switch (a) {
        case Action::SteerLeft:  return "SteerLeft";
        case Action::SteerRight: return "SteerRight";
        case Action::Throttle:   return "Throttle";
        case Action::Brake:      return "Brake";
        case Action::GearUp:     return "GearUp";
        case Action::GearDown:   return "GearDown";
        case Action::Handbrake:  return "Handbrake";
        default:                 return "Unknown";
        }
    }
    static Action FromName(const std::string& s) {
        if (s == "SteerLeft")  return Action::SteerLeft;
        if (s == "SteerRight") return Action::SteerRight;
        if (s == "Throttle")   return Action::Throttle;
        if (s == "Brake")      return Action::Brake;
        if (s == "GearUp")     return Action::GearUp;
        if (s == "GearDown")   return Action::GearDown;
        if (s == "Handbrake")  return Action::Handbrake;
        return Action::Throttle; // fallback
    }

    // ---------------------- Lifecycle -------------------------
    void Init() {
        // Default WASD + Space
        Bindings[Action::SteerLeft] = SDL_SCANCODE_A;
        Bindings[Action::SteerRight] = SDL_SCANCODE_D;
        Bindings[Action::Throttle] = SDL_SCANCODE_W;
        Bindings[Action::Brake] = SDL_SCANCODE_S;
        Bindings[Action::GearUp] = SDL_SCANCODE_E;
        Bindings[Action::GearDown] = SDL_SCANCODE_Q;
        Bindings[Action::Handbrake] = SDL_SCANCODE_SPACE;

        // Optional: try to load persisted bindings if present
        (void)LoadBindings("bindings.json");
    }

    void Shutdown() {
        // nothing yet
    }

    // ---------------------- Update ----------------------------
    void Update() {
        // SDL3 returns const bool*
        const bool* ks = SDL_GetKeyboardState(nullptr);

        auto isDown = [&](Action a) -> bool {
            auto it = Bindings.find(a);
            if (it == Bindings.end()) return false;
            int sc = static_cast<int>(it->second);
            if (sc < 0 || sc >= static_cast<int>(SDL_SCANCODE_COUNT)) return false;
            return ks[sc]; // ks[...] is already bool
            };

        s_State.steer =
            (isDown(Action::SteerLeft) ? -1.0f : 0.0f) +
            (isDown(Action::SteerRight) ? 1.0f : 0.0f);

        s_State.throttle = isDown(Action::Throttle) ? 1.0f : 0.0f;
        s_State.brake = isDown(Action::Brake) ? 1.0f : 0.0f;
        s_State.gearUp = isDown(Action::GearUp);
        s_State.gearDown = isDown(Action::GearDown);
        s_State.handbrake = isDown(Action::Handbrake);
    }

    const InputState& GetState() { return s_State; }

    // -------------------- Save / Load JSON --------------------
    void SaveBindings(const char* path) {
        std::ofstream f(path, std::ios::trunc);
        if (!f) return;
        f << "{\n";
        bool first = true;
        for (auto& kv : Bindings) {
            if (!first) f << ",\n";
            first = false;
            Action act = kv.first;
            int sc = static_cast<int>(kv.second);
            f << "  \"" << GetActionName(act) << "\": " << sc;
        }
        f << "\n}\n";
    }

    bool LoadBindings(const char* path) {
        std::ifstream f(path);
        if (!f) return false;

        std::string s((std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());

        size_t i = 0;
        auto skip = [&] {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            };
        auto eat = [&](char c) -> bool {
            skip();
            if (i < s.size() && s[i] == c) { ++i; return true; }
            return false;
            };
        auto readString = [&]() -> std::string {
            skip();
            if (!eat('\"')) return {};
            size_t j = i;
            while (i < s.size() && s[i] != '\"') ++i;
            std::string r = s.substr(j, i - j);
            (void)eat('\"');
            return r;
            };
        auto readInt = [&]() -> int {
            skip();
            bool neg = false;
            if (i < s.size() && s[i] == '-') { neg = true; ++i; }
            int x = 0;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                x = x * 10 + (s[i] - '0');
                ++i;
            }
            return neg ? -x : x;
            };

        skip();
        if (!eat('{')) return false;

        // Rebuild map
        std::unordered_map<Action, SDL_Scancode> loaded;

        while (true) {
            skip();
            if (eat('}')) break;
            std::string key = readString();
            skip(); (void)eat(':');
            int sc = readInt();
            loaded[FromName(key)] = static_cast<SDL_Scancode>(sc);
            skip();
            if (eat(',')) continue;
            else { (void)eat('}'); break; }
        }

        if (!loaded.empty()) Bindings = std::move(loaded);
        return true;
    }

} // namespace Input

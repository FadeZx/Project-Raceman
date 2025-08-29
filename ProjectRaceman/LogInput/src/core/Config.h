#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace Core {
    class Config {
    public:
        static bool Load(const std::string& path);
        static int GetWindowWidth() { return s_WindowWidth; }
        static int GetWindowHeight() { return s_WindowHeight; }
        static bool GetVSync() { return s_VSync; }

    private:
        static int s_WindowWidth;
        static int s_WindowHeight;
        static bool s_VSync;
    };
}

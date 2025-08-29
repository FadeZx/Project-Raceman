#include "Config.h"
#include "Log.h"
#include <fstream>

using json = nlohmann::json;

int  Core::Config::s_WindowWidth = 1280;
int  Core::Config::s_WindowHeight = 720;
bool Core::Config::s_VSync = true;

bool Core::Config::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        Log::Warn("Config file not found, using defaults.");
        return false;
    }
    json j;
    f >> j;

    if (j.contains("window_width"))  s_WindowWidth = j["window_width"];
    if (j.contains("window_height")) s_WindowHeight = j["window_height"];
    if (j.contains("vsync"))         s_VSync = j["vsync"];

    Log::Info("Config loaded: " + std::to_string(s_WindowWidth) + "x" +
        std::to_string(s_WindowHeight));
    return true;
}

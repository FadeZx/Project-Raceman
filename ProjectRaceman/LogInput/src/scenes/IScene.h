// scenes/IScene.h
#pragma once
#include <SDL3/SDL.h>

struct SceneContext {
    int    winW = 1280;
    int    winH = 720;
    double dtFixed = 1.0 / 120.0;
    SDL_Window* window = nullptr;   // optional, if you need it
};

class IScene {
public:
    virtual ~IScene() = default;

    virtual bool  Init(const SceneContext& ctx) = 0;
    virtual void  HandleEvent(const SDL_Event& e) = 0;
    virtual void  Update(double frameDt, double fixedDtAccumulatorStep) = 0;
    virtual void  Render() = 0;
    virtual void  ImGuiUI() = 0;
    virtual void  Shutdown() = 0;
};

#include "App.h"
#include "../core/Log.h"
#include "../core/Config.h"
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl3.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <memory>
#include "../scenes/IScene.h"
#include "../scenes/SceneGarage.h"
#include "../scenes/SceneSim.h"

enum class Mode { Garage, Sim };

int App::Run() {
    Core::Log::Info("Starting App...");
    Core::Config::Load("config.json");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) { return -1; }
    SDL_Window* window = SDL_CreateWindow(
        "SimRacer",
        Core::Config::GetWindowWidth(),
        Core::Config::GetWindowHeight(),
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glctx);
    SDL_GL_SetSwapInterval(Core::Config::GetVSync() ? 1 : 0);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return -1;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FRAMEBUFFER_SRGB);

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, glctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    // initial size
    int winW, winH; SDL_GetWindowSizeInPixels(window, &winW, &winH);

    // Scene host
    Mode mode = Mode::Garage;
    std::unique_ptr<IScene> scene;
    auto switchTo = [&](Mode m) {
        if (scene) { scene->Shutdown(); scene.reset(); }
        mode = m;
        SceneContext ctx; ctx.winW = winW; ctx.winH = winH; ctx.dtFixed = 1.0 / 120.0;
        if (mode == Mode::Garage) scene = std::make_unique<SceneGarage>();
        else                      scene = std::make_unique<SceneSim>();
        scene->Init(ctx);
        };
    switchTo(mode);

    // Timing
    Uint64 prevTicks = SDL_GetTicks();
    bool running = true;
    while (running) {
        // Frame timing
        Uint64 now = SDL_GetTicks();
        double frameDt = (now - prevTicks) / 1000.0;
        if (frameDt > 0.25) frameDt = 0.25;
        prevTicks = now;

        // Events
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                winW = e.window.data1; winH = e.window.data2;
            }
            if (scene) scene->HandleEvent(e);
        }

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Top menu bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Mode")) {
                bool g = (mode == Mode::Garage);
                bool s = (mode == Mode::Sim);
                if (ImGui::MenuItem("Garage", nullptr, g)) switchTo(Mode::Garage);
                if (ImGui::MenuItem("Sim", nullptr, s)) switchTo(Mode::Sim);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Scene UI + update + render
        if (scene) {
            scene->ImGuiUI();
            scene->Update(frameDt, 1.0 / 120.0); // fixed step size provided for info
            scene->Render();
        }

        // Render ImGui
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    if (scene) { scene->Shutdown(); scene.reset(); }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

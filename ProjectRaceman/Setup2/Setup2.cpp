#include <SDL3/SDL.h>
#include <glad/glad.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl3.h>      // SDL3 backend
#include <imgui/backends/imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <chrono>
#include <string>
#include <cstdio>

// -------- fixed timestep settings --------
static constexpr double PHYS_DT = 1.0 / 300.0; // 300 Hz physics
static constexpr int    MAX_PHYS_STEPS = 8;

// --------- simple physics stub ----------
struct VehicleState {
    glm::vec3 pos{ 0.0f, 0.0f, 0.0f };
    glm::vec3 vel{ 0.0f, 0.0f, 0.0f };
    float yaw = 0.0f;
};

void physics_step(VehicleState& car, double dt, float steer, float throttle, float brake) {
    const float a = 4.0f * throttle - 6.0f * brake; // m/s^2
    car.vel.x += a * (float)dt;
    car.pos.x += car.vel.x * (float)dt;
    car.yaw += steer * 0.2f * (float)dt;
    car.vel *= 0.999f;
}

int main(int, char**)
{
    // -------- SDL init (SDL3) --------
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {           // SDL3: no SDL_INIT_TIMER, GAMECONTROLLER -> GAMEPAD
        std::printf("SDL_Init Error: %s\n", SDL_GetError());
        return -1;
    }

    // GL context attributes (OpenGL 3.3 Core)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // SDL3: CreateWindow signature changed (no x,y)
    const Uint32 winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY; // SDL_WINDOW_ALLOW_HIGHDPI -> HIGH_PIXEL_DENSITY
    SDL_Window* window = SDL_CreateWindow("SimRacer Starter", 1280, 720, winFlags);
    if (!window) { std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError()); return -1; }

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    if (!glctx) { std::printf("SDL_GL_CreateContext failed: %s\n", SDL_GetError()); return -1; }

    SDL_GL_MakeCurrent(window, glctx);
    SDL_GL_SetSwapInterval(1); // vsync on

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::printf("gladLoadGLLoader failed\n");
        return -1;
    }

    // -------- ImGui init --------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, glctx);            // SDL3 backend
    ImGui_ImplOpenGL3_Init("#version 330");

    // -------- app state --------
    bool running = true;
    VehicleState car{};
    float steer = 0.0f, throttle = 0.0f, brake = 0.0f;

    auto now = std::chrono::high_resolution_clock::now();
    double accumulator = 0.0;

    // -------- main loop --------
    while (running) {
        // events
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);                // SDL3 backend
            if (e.type == SDL_EVENT_QUIT) running = false;  // SDL3: SDL_QUIT -> SDL_EVENT_QUIT
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
                && e.window.windowID == SDL_GetWindowID(window)) { // SDL3: no .window.event field anymore
                running = false;
            }
        }

        // keyboard (SDL3 returns const bool*)
        const bool* ks = SDL_GetKeyboardState(nullptr);
        steer = (ks[SDL_SCANCODE_A] ? -1.0f : 0.0f) + (ks[SDL_SCANCODE_D] ? 1.0f : 0.0f);
        throttle = ks[SDL_SCANCODE_W] ? 1.0f : 0.0f;
        brake = ks[SDL_SCANCODE_S] ? 1.0f : 0.0f;

        // fixed timestep update
        auto newNow = std::chrono::high_resolution_clock::now();
        double frameTime = std::chrono::duration<double>(newNow - now).count();
        now = newNow;
        accumulator += frameTime;

        int steps = 0;
        while (accumulator >= PHYS_DT && steps < MAX_PHYS_STEPS) {
            physics_step(car, PHYS_DT, steer, throttle, brake);
            accumulator -= PHYS_DT;
            steps++;
        }

        // drawable size (SDL3)
        int fbw = 0, fbh = 0;
        SDL_GetWindowSizeInPixels(window, &fbw, &fbh);      // SDL3: replaces SDL_GL_GetDrawableSize
        glViewport(0, 0, fbw, fbh);
        glEnable(GL_FRAMEBUFFER_SRGB);
        glClearColor(0.05f, 0.07f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();                          // SDL3 backend
        ImGui::NewFrame();

        // HUD
        ImGui::Begin("Telemetry");
        ImGui::Text("pos: (%.2f, %.2f, %.2f)", car.pos.x, car.pos.y, car.pos.z);
        ImGui::Text("vel x: %.2f m/s", car.vel.x);
        ImGui::Text("yaw: %.3f rad", car.yaw);
        ImGui::Separator();
        ImGui::SliderFloat("steer", &steer, -1.0f, 1.0f);
        ImGui::SliderFloat("throttle", &throttle, 0.0f, 1.0f);
        ImGui::SliderFloat("brake", &brake, 0.0f, 1.0f);
        ImGui::TextUnformatted("WASD right now; wheel later via SDL3 gamepad or HID.");
        ImGui::End();

        // render imgui + swap
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();                              // SDL3 backend
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(glctx);                           // SDL3: renamed from DeleteContext
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

#include "App.h"
#include "../core/Log.h"
#include "../core/Config.h"

#include "../input/Input.h"

#include "../render/Camera.h"
#include "../render/Shader.h"
#include "../render/SimpleMesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl3.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <Shader.h>

#include <camera.h>
#include "../Physics/Physics.h"

#include <filesystem>
namespace fs = std::filesystem;


int App::Run() {
    Core::Log::Info("Starting App...");

    Core::Config::Load("config.json");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        Core::Log::Error("SDL_Init failed");
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow("SimRacer",
        Core::Config::GetWindowWidth(),
        Core::Config::GetWindowHeight(),
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glctx);
    SDL_GL_SetSwapInterval(Core::Config::GetVSync() ? 1 : 0);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        Core::Log::Error("Failed to init GLAD");
        return -1;
    }

    // --- GL state ---
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FRAMEBUFFER_SRGB);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FRAMEBUFFER_SRGB);

    // --- Renderer setup (use your Shader class) ---
    const std::string vsPath = "src/shaders/basic.vs";
    const std::string fsPath = "src/shaders/basic.fs";

    Core::Log::Info("VS path: " + vsPath);
    Core::Log::Info("FS path: " + fsPath);

    std::error_code ec;
    bool vsExists = std::filesystem::exists(vsPath, ec);
    bool fsExists = std::filesystem::exists(fsPath, ec);
    if (!vsExists || !fsExists) {
        Core::Log::Error(std::string("Shader missing. VS? ") + (vsExists ? "yes" : "no") +
            " FS? " + (fsExists ? "yes" : "no"));
        return -1;
    }
    Shader shader(vsPath.c_str(), fsPath.c_str()); // constructor compiles/links


    GLuint gridVao = Render::SimpleMesh::makeGrid(20, 1.0f);
    GLuint cubeVao = Render::SimpleMesh::makeCube();

    Render::Camera cam;
    int w0 = Core::Config::GetWindowWidth(), h0 = Core::Config::GetWindowHeight();
    cam.setPerspective(60.0f, float(w0) / float(h0), 0.1f, 1000.0f);
    cam.setView({ -6.f, 4.f, 8.f }, { 0.f, 0.f, 0.f });

    // Init Input&Physics
    Input::Init();
    Physics::Init();

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, glctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    // --- Fixed timestep timing ---
    const double dt = 1.0 / 120.0;  // 120 Hz physics
    double accumulator = 0.0;
    Uint64 prevTicks = SDL_GetTicks();

    bool running = true;
    while (running) {
        // timing
        Uint64 now = SDL_GetTicks();
        double frameTime = (now - prevTicks) / 1000.0;
        if (frameTime > 0.25) frameTime = 0.25;
        prevTicks = now;
        accumulator += frameTime;

        // events
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
        }

        // 1) Read device once, then let UI edit a copy
        Input::Update();
        auto controls = Input::GetState(); // copy (steer/throttle/brake)

        // --- ImGui begin (ONE frame per loop) ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Input Debug UI edits 'controls'
        ImGui::Begin("Input Debug");
        ImGui::SliderFloat("Steer", &controls.steer, -1.0f, 1.0f);
        ImGui::VSliderFloat("Throttle", ImVec2(40, 120), &controls.throttle, 0.0f, 1.0f, "T");
        ImGui::SameLine();
        ImGui::VSliderFloat("Brake", ImVec2(40, 120), &controls.brake, 0.0f, 1.0f, "B");
        ImGui::End();

        // Vehicle Params window (as you had)
        ImGui::Begin("Vehicle Params");
        ImGui::SliderFloat("mu", &Physics::P.mu, 0.2f, 2.5f);
        ImGui::SliderFloat("Cf", &Physics::P.Cf, 20000.f, 160000.f);
        ImGui::SliderFloat("Cr", &Physics::P.Cr, 20000.f, 160000.f);
        ImGui::SliderFloat("FxMax", &Physics::P.Fx_max, 1000.f, 20000.f);
        ImGui::SliderFloat("FbMax", &Physics::P.Fb_max, 1000.f, 30000.f);
        ImGui::SliderFloat("lf", &Physics::P.lf, 0.8f, 1.8f);
        ImGui::SliderFloat("lr", &Physics::P.lr, 0.8f, 1.8f);
        ImGui::End();

        // 2) Fixed-step physics using edited controls
        while (accumulator >= dt) {
            Physics::Controls pc{ controls.steer, controls.throttle, controls.brake };
            Physics::Update(dt, pc);
            accumulator -= dt;
        }

        // 3) Render scene
        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        cam.setPerspective(60.0f, w > 0 ? float(w) / float(h) : 1.0f, 0.1f, 1000.0f);

        shader.use();
        shader.setMat4("uView", cam.view());
        shader.setMat4("uProj", cam.proj());

        // grid
        glm::mat4 model(1.0f);
        shader.setMat4("uModel", model);
        shader.setVec3("uColor", 0.4f, 0.4f, 0.45f);
        glBindVertexArray(gridVao);
        glDrawArrays(GL_LINES, 0, Render::SimpleMesh::gridVertexCount(20, 1.0f));

        // car cube at physics pose
        model = glm::translate(glm::mat4(1), Physics::gCar.pos)
            * glm::rotate(glm::mat4(1), Physics::gCar.yaw, glm::vec3(0, 1, 0));
        shader.setMat4("uModel", model);
        shader.setVec3("uColor", 0.9f, 0.2f, 0.2f);
        glBindVertexArray(cubeVao);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // Physics info (optional)
        ImGui::Begin("Physics");
        ImGui::Text("dt (fixed): %.4f s", dt);
        ImGui::Text("accumulator: %.3f ms", accumulator * 1000.0);
        ImGui::Text("pos: (%.2f, %.2f, %.2f)", Physics::gCar.pos.x, Physics::gCar.pos.y, Physics::gCar.pos.z);
        ImGui::Text("yaw: %.2f deg", Physics::gCar.yaw * 57.29578f);
        ImGui::End();

        // 4) ImGui render once
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

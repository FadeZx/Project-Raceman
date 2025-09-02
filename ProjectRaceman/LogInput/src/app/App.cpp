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

    // Input
    Input::Init();
    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, glctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
        }

        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // include depth


        Input::Update();
        const auto& input = Input::GetState();

        // update camera aspect on resize
        cam.setPerspective(60.0f, w > 0 ? float(w) / float(h) : 1.0f, 0.1f, 1000.0f);

        // --- draw grid + cube ---
        shader.use();
        shader.setMat4("uView", cam.view());
        shader.setMat4("uProj", cam.proj());

        glm::mat4 model(1.0f);
        shader.setMat4("uModel", model);
        shader.setVec3("uColor", 0.4f, 0.4f, 0.45f);
        glBindVertexArray(gridVao);
        glDrawArrays(GL_LINES, 0, Render::SimpleMesh::gridVertexCount(20, 1.0f));

        model = glm::translate(glm::mat4(1), glm::vec3(0.0f, 0.5f, 0.0f));
        shader.setMat4("uModel", model);
        shader.setVec3("uColor", 0.9f, 0.2f, 0.2f);
        glBindVertexArray(cubeVao);
        glDrawArrays(GL_TRIANGLES, 0, 36);

		// --- ImGui ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(20, 50), ImGuiCond_FirstUseEver);   // X=20, Y=50
        ImGui::SetNextWindowSize(ImVec2(200, 250), ImGuiCond_FirstUseEver); // Width=200, Height=250
        ImGui::Begin("Input Debug");
        ImGui::SliderFloat("Steer", (float*)&input.steer, -1.0f, 1.0f);
        // Pedals
        ImGui::Text("Pedals:");
        ImGui::PushID("Pedals");
        ImGui::BeginGroup();
        ImGui::VSliderFloat("##Throttle", ImVec2(40, 120), (float*)&input.throttle, 0.0f, 1.0f, "T");
        ImGui::SameLine();
        ImGui::VSliderFloat("##Brake", ImVec2(40, 120), (float*)&input.brake, 0.0f, 1.0f, "B");
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::End();

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

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
#include "../Physics/Powertrain.h"   // path to your Powertrain.h


#include <filesystem>
namespace fs = std::filesystem;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif




// --- utility helpers (instead of ImLerp / ImClamp) ---
template<typename T>
static inline T Lerp(const T& a, const T& b, float t) {
    return a + (b - a) * t;
}

template<typename T>
static inline T Clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// ---- Simple radial gauge helpers (ImGui) ----
static void DrawRadialGauge(const char* label,
    float value, float vmin, float vmax,
    float redline = -1.0f,
    const char* units = "",
    float size_px = 180.0f)
{
    using namespace ImGui;
    ImDrawList* dl = GetWindowDrawList();

    const ImVec2 p0 = GetCursorScreenPos();
    const float  r = size_px * 0.5f;
    const ImVec2 c = ImVec2(p0.x + r, p0.y + r);

    // reserve space
    InvisibleButton(label, ImVec2(size_px, size_px));
    const float start_ang = M_PI * 0.75f;   // 135°
    const float end_ang = M_PI * 0.25f;   // 45°
    const int   segs = 96;

    auto angOf = [&](float v) {
        float t = (v - vmin) / (vmax - vmin);
        t = Clamp(t, 0.0f, 1.0f);
        return start_ang + (end_ang - start_ang) * t;
        };

    // background arc
    dl->PathClear();
    for (int i = 0; i <= segs; ++i) {
        float t = (float)i / (float)segs;
        float a = start_ang + (end_ang - start_ang) * t;
        dl->PathLineTo(ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r));
    }
    dl->PathStroke(GetColorU32(ImGuiCol_FrameBg), 0, 6.0f);

    // redline arc (if provided)
    if (redline > vmin && redline < vmax) {
        float aR = angOf(redline);
        dl->PathClear();
        for (int i = 0; i <= segs; ++i) {
            float t = (float)i / (float)segs;
            float a = aR + (end_ang - aR) * t;
            if (a < aR) break;
            dl->PathLineTo(ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r));
        }
        dl->PathStroke(IM_COL32(220, 60, 60, 255), 0, 6.0f);
    }

    // ticks (major at 0,25,50,75,100%)
    for (int i = 0; i <= 10; ++i) {
        if (i % 2 != 0) continue;
        float t = i / 10.0f;
        float a = start_ang + (end_ang - start_ang) * t;
        ImVec2 p1 = ImVec2(c.x + cosf(a) * (r - 8), c.y + sinf(a) * (r - 8));
        ImVec2 p2 = ImVec2(c.x + cosf(a) * (r - 20), c.y + sinf(a) * (r - 20));
        dl->AddLine(p1, p2, GetColorU32(ImGuiCol_Text), 2.0f);

        // label
        char buf[32];
        float vTick = Lerp(vmin, vmax, t);
        if (units && units[0]) snprintf(buf, sizeof(buf), "%.0f", vTick);
        else                    snprintf(buf, sizeof(buf), "%.0f", vTick);
        ImVec2 ts = CalcTextSize(buf);
        ImVec2 tp = ImVec2(c.x + cosf(a) * (r - 32) - ts.x * 0.5f,
            c.y + sinf(a) * (r - 32) - ts.y * 0.5f);
        dl->AddText(tp, GetColorU32(ImGuiCol_Text), buf);
    }

    // needle
    float aV = angOf(value);
    ImVec2 n0 = ImVec2(c.x + cosf(aV) * (r - 10), c.y + sinf(aV) * (r - 10));
    dl->AddLine(c, n0, GetColorU32(ImGuiCol_PlotHistogramHovered), 3.0f);
    dl->AddCircleFilled(c, 5.0f, GetColorU32(ImGuiCol_PlotHistogramHovered));

    // center readout
    char vbuf[64];
    if (units && units[0]) snprintf(vbuf, sizeof(vbuf), "%.0f %s", value, units);
    else                   snprintf(vbuf, sizeof(vbuf), "%.0f", value);
    ImVec2 ts = CalcTextSize(vbuf);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), GetColorU32(ImGuiCol_Text), vbuf);

    // title
    ImVec2 ls = CalcTextSize(label);
    dl->AddText(ImVec2(c.x - ls.x * 0.5f, p0.y + size_px - ls.y), GetColorU32(ImGuiCol_TextDisabled), label);
}

static void ShiftLightBar(float rpm, float redline, float width = 180.0f)
{
    using namespace ImGui;
    float t = Clamp((rpm - (redline * 0.85f)) / (redline * 0.15f), 0.0f, 1.0f);
    ImVec2 p = GetCursorScreenPos();
    ImVec2 s = ImVec2(width, 8.0f);
    ImU32 bg = GetColorU32(ImGuiCol_FrameBg);
    GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), bg, 4.0f);
    ImU32 fg = IM_COL32((int)(255 * t), (int)(255 * (1.0f - t)), 0, 255);
    GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + s.x * t, p.y + s.y), fg, 4.0f);
    Dummy(s);
}



int App::Run() {
    Core::Log::Info("Starting App...");

    Core::Config::Load("config.json");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
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
    Powertrain::Init();


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

    static bool prevGearUp = false;
    static bool prevGearDown = false;

    // In App.cpp (outside the while loop)
    float kbdThrottle = 0.0f;
    float kbdBrake = 0.0f;

    // helper
    auto slew = [](float current, float target, float rise_per_s, float fall_per_s, float dt) {
        float rate = (target > current) ? rise_per_s : fall_per_s;
        float delta = std::clamp(target - current, -rate * dt, rate * dt);
        return current + delta;
        };

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

        // ---- poll inputs from devices once ----
        Input::Update();
        // After Input::Update();
        auto in = Input::GetState();

        static bool prevUp = false, prevDn = false;

        float speed = std::sqrt(Physics::gCar.vx * Physics::gCar.vx + Physics::gCar.vy * Physics::gCar.vy);


        // Up: R->N, N->1, 1->2...
        if (in.gearUp && !prevUp) {
            if (Powertrain::S.gear < 0) {
                Powertrain::SetNeutral();
            }
            else {
                Powertrain::GearUp();
            }
        }
        prevUp = in.gearUp;

        // Down: 3->2->1->N->R (but only allow N->R at near-zero speed)
        if (in.gearDown && !prevDn) {
            if (Powertrain::S.gear > 1) {
                Powertrain::GearDown();
            }
            else if (Powertrain::S.gear == 1) {
                Powertrain::SetNeutral();
            }
            else if (Powertrain::S.gear == 0) {
                if (speed < 0.5f) Powertrain::SetReverse();  // gate by speed
                // else ignore
            }
        }
        prevDn = in.gearDown;

        // --- keyboard throttle/brake smoothing (restore this) ---
        const float tgtT = in.throttle ? 1.0f : 0.0f;  // digital → target
        const float tgtB = in.brake ? 1.0f : 0.0f;

        kbdThrottle = slew(kbdThrottle, tgtT, 5.0f, 8.0f, (float)dt); // ~0.2s to full
        kbdBrake = slew(kbdBrake, tgtB, 6.0f, 10.0f, (float)dt);


        // then feed physics/powertrain with the smoothed values:
        Physics::Controls controls{ in.steer, kbdThrottle, kbdBrake };

        // ---- start one ImGui frame ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // ---------- Key Logger ----------
        ImGui::Begin("Key Logger");
        {
            const bool* ks = SDL_GetKeyboardState(nullptr); // SDL3 returns const bool*
            for (auto& kv : Input::Bindings) {
                Input::Action action = kv.first;
                int sc = static_cast<int>(kv.second);
                bool down = (sc >= 0 && sc < static_cast<int>(SDL_SCANCODE_COUNT)) ? ks[sc] : false;
                ImGui::Text("%s (%s): %s",
                    Input::GetActionName(action),
                    SDL_GetScancodeName(static_cast<SDL_Scancode>(sc)),
                    down ? "DOWN" : "up");
            }
        }
        ImGui::End();

        // ---------- Input Remap ----------
        ImGui::Begin("Input Remap");
        {
            static Input::Action waitingFor = (Input::Action)-1;
            for (auto& kv : Input::Bindings) {
                auto action = kv.first;
                auto sc = kv.second;

                ImGui::PushID((int)action);
                ImGui::Text("%s", Input::GetActionName(action));
                ImGui::SameLine(220);

                if (waitingFor == action) {
                    ImGui::Text("[Press a key...]");
                    const bool* ks = SDL_GetKeyboardState(nullptr);
                    for (int i = 0; i < static_cast<int>(SDL_SCANCODE_COUNT); ++i) {
                        if (ks[i]) {
                            Input::Bindings[action] = static_cast<SDL_Scancode>(i);
                            waitingFor = (Input::Action)-1;
                            break;
                        }
                    }
                }
                else {
                    if (ImGui::Button(SDL_GetScancodeName(sc))) {
                        waitingFor = action;
                    }
                }
                ImGui::PopID();
            }
            if (ImGui::Button("Save")) Input::SaveBindings("bindings.json");
            ImGui::SameLine();
            if (ImGui::Button("Load")) Input::LoadBindings("bindings.json");
        }
        ImGui::End();

        // ---------- Input Debug (edits 'controls' copy) ----------
        ImGui::Begin("Input Debug");
        ImGui::SliderFloat("Steer", &controls.steer, -1.0f, 1.0f);
        ImGui::VSliderFloat("Throttle", ImVec2(40, 120), &controls.throttle, 0.0f, 1.0f, "T");
        ImGui::SameLine();
        ImGui::VSliderFloat("Brake", ImVec2(40, 120), &controls.brake, 0.0f, 1.0f, "B");
        ImGui::End();

        // ---------- Vehicle Params ----------
        ImGui::Begin("Vehicle Params");
        ImGui::SliderFloat("mu", &Physics::P.mu, 0.2f, 2.5f);
        ImGui::SliderFloat("Cf", &Physics::P.Cf, 20000.f, 160000.f);
        ImGui::SliderFloat("Cr", &Physics::P.Cr, 20000.f, 160000.f);
        ImGui::SliderFloat("FxMax", &Physics::P.Fx_max, 1000.f, 20000.f);
        ImGui::SliderFloat("FbMax", &Physics::P.Fb_max, 1000.f, 30000.f);
        ImGui::SliderFloat("lf", &Physics::P.lf, 0.8f, 1.8f);
        ImGui::SliderFloat("lr", &Physics::P.lr, 0.8f, 1.8f);
        ImGui::End();


        ImGui::Begin("Dash");
        {
            // Smooth the displayed values a bit so the needle isn’t jittery
            static float disp_rpm = 0.0f;
            static float disp_kmh = 0.0f;
            const float rpm = Powertrain::S.rpm;
            const float kmh = speed * 3.6f;
            const float lerp_a = 0.20f; // smoothing (0..1)

            disp_rpm = Lerp(disp_rpm, rpm, lerp_a);
            disp_kmh = Lerp(disp_kmh, kmh, lerp_a);

            // Tachometer (0..redline*1.1)
            float red = Powertrain::P.redline_rpm;
            DrawRadialGauge("Tachometer", disp_rpm, 0.0f, red * 1.10f, red, "RPM", 200.0f);
            ShiftLightBar(rpm, red, 200.0f);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // Speedometer (0..260 km/h)
            float vmax_kmh = 260.0f;
            DrawRadialGauge("Speedometer", disp_kmh, 0.0f, vmax_kmh, -1.0f, "km/h", 200.0f);

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // Small text readouts
            std::string gearStr = (Powertrain::S.gear < 0) ? "R" :
                (Powertrain::S.gear == 0) ? "N" :
                std::to_string(Powertrain::S.gear);
            ImGui::Text("Gear: %s", gearStr.c_str());
            ImGui::Text("Throttle: %.0f%%   Brake: %.0f%%",
                controls.throttle * 100.0f, controls.brake * 100.0f);
            ImGui::Text("Clutch:  %.0f%%", Powertrain::S.clutch_alpha * 100.0f);
        }
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

        ImGui::Begin("Powertrain");
        {
            std::string gearStr = (Powertrain::S.gear < 0) ? "R"
                : (Powertrain::S.gear == 0) ? "N"
                : std::to_string(Powertrain::S.gear);
            ImGui::Text("Gear: %s / %d", gearStr.c_str(), (int)Powertrain::P.gears.size());
            ImGui::Text("RPM:  %.0f", Powertrain::S.rpm);
            ImGui::Checkbox("Auto Shift", &Powertrain::S.auto_shift);

            ImGui::Separator();
            ImGui::Text("Clutch α: %.2f", Powertrain::S.clutch_alpha); // optional telemetry
            ImGui::Text("Speed: %.1f km/h", speed * 3.6f);

            // Tune params
            ImGui::SeparatorText("Ratios / Eff.");
            ImGui::SliderFloat("Final Drive", &Powertrain::P.final_drive, 2.5f, 5.0f);
            ImGui::SliderFloat("Wheel Radius", &Powertrain::P.wheel_radius, 0.25f, 0.40f);
            ImGui::SliderFloat("Driveline Eff", &Powertrain::P.driveline_eff, 0.80f, 0.99f);

            ImGui::SeparatorText("RPM");
            ImGui::SliderFloat("Idle", &Powertrain::P.idle_rpm, 600.f, 1200.f);
            ImGui::SliderFloat("Redline", &Powertrain::P.redline_rpm, 6000.f, 8000.f);


            ImGui::SeparatorText("Gears");
            for (size_t i = 0; i < Powertrain::P.gears.size(); ++i) {
                ImGui::PushID((int)i);
                float r = Powertrain::P.gears[i];
                if (ImGui::SliderFloat("ratio", &r, 0.5f, 4.0f)) Powertrain::P.gears[i] = r;
                ImGui::PopID();
            }
        }
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

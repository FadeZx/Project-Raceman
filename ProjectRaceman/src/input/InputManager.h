#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

typedef void (*GLFWkeyfun)(GLFWwindow*, int, int, int, int);

namespace raceman {

class InputManager {
public:
    InputManager() = default;
    ~InputManager() = default;

    void AttachToWindow(GLFWwindow* window);
    void Update();

    bool WasKeyPressed(int key) const;
    bool IsKeyDown(int key) const;

    void RegisterKeyCallback(const std::function<void(int)>& callback);

    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

private:
    GLFWwindow* window_{nullptr};
    std::unordered_map<int, bool> keyState_;
    std::unordered_map<int, bool> keyPressed_;
    std::vector<std::function<void(int)>> keyCallbacks_;
};

} // namespace raceman

#include "InputManager.h"

#include <GLFW/glfw3.h>

namespace raceman {

void InputManager::AttachToWindow(GLFWwindow* window) {
    window_ = window;
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, &InputManager::KeyCallback);
}

void InputManager::Update() {
    for (auto& [key, pressed] : keyPressed_) {
        pressed = false;
    }
}

bool InputManager::WasKeyPressed(int key) const {
    auto it = keyPressed_.find(key);
    return it != keyPressed_.end() && it->second;
}

bool InputManager::IsKeyDown(int key) const {
    auto it = keyState_.find(key);
    return it != keyState_.end() && it->second;
}

void InputManager::RegisterKeyCallback(const std::function<void(int)>& callback) {
    keyCallbacks_.push_back(callback);
}

void InputManager::KeyCallback(GLFWwindow* window, int key, int, int action, int) {
    auto* manager = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!manager) {
        return;
    }

    if (action == GLFW_PRESS) {
        manager->keyState_[key] = true;
        manager->keyPressed_[key] = true;
        for (auto& callback : manager->keyCallbacks_) {
            callback(key);
        }
    } else if (action == GLFW_RELEASE) {
        manager->keyState_[key] = false;
    }
}

} 
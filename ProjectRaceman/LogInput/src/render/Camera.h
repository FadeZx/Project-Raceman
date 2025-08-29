#pragma once
#include <glm/glm.hpp>

namespace Render {
    class Camera {
    public:
        void setPerspective(float fovy_deg, float aspect, float znear, float zfar);
        void setView(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up = { 0,1,0 });

        const glm::mat4& proj() const { return m_proj; }
        const glm::mat4& view() const { return m_view; }

    private:
        glm::mat4 m_proj{ 1.0f };
        glm::mat4 m_view{ 1.0f };
    };
}

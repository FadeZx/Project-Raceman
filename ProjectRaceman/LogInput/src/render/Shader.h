#pragma once
#include <string>
#include <glm/mat4x4.hpp>
#include <glad/glad.h>

namespace Render {

    class Shader {
    public:
        Shader() = default;
        Shader(const char* vsPath, const char* fsPath) { loadFromFiles(vsPath, fsPath, nullptr); }
        ~Shader();

        bool  loadFromFiles(const std::string& vsPath, const std::string& fsPath, std::string* logOut);
        void  use() const { glUseProgram(m_program); }
        GLuint id() const { return m_program; }

        // Uniform helpers
        void setMat4(const char* name, const glm::mat4& m) const;
        void setMat4(const char* name, const float* m) const;   // raw pointer variant
        void setVec3(const char* name, float x, float y, float z) const;

    private:
        GLuint m_program = 0;
    };

} // namespace Render

#pragma once
#include <string>
#include <glad/glad.h>

namespace Render {
    class Shader {
    public:
        Shader() = default;
        ~Shader();

        bool loadFromFiles(const std::string& vsPath, const std::string& fsPath, std::string* logOut = nullptr);
        void use() const { glUseProgram(m_program); }
        GLuint id() const { return m_program; }

        // convenience
        void setMat4(const char* name, const float* m) const;
        void setVec3(const char* name, float x, float y, float z) const;

    private:
        GLuint m_program = 0;
    };
}

#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <vector>

class Shader
{
public:
    unsigned int ID;
    // constructor generates the shader on the fly
    // ------------------------------------------------------------------------
    Shader(const char* vertexPath, const char* fragmentPath)
    {
        // 1. retrieve the vertex/fragment source code from filePath
        std::string vertexCode;
        std::string fragmentCode;
        std::ifstream vShaderFile;
        std::ifstream fShaderFile;
        // ensure ifstream objects can throw exceptions:
        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try
        {
            // open files
            vShaderFile.open(vertexPath);
            fShaderFile.open(fragmentPath);
            std::stringstream vShaderStream, fShaderStream;
            // read file's buffer contents into streams
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            // close file handlers
            vShaderFile.close();
            fShaderFile.close();
            // convert stream into string
            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();
        }
        catch (std::ifstream::failure& e)
        {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
            valid_ = false;
            compileLog_ += "Could not read shader source: ";
            compileLog_ += e.what();
            compileLog_ += '\n';
        }
        const char* vShaderCode = vertexCode.c_str();
        const char* fShaderCode = fragmentCode.c_str();
        // 2. compile shaders
        unsigned int vertex, fragment;
        // vertex shader
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);
        checkCompileErrors(vertex, "VERTEX");
        // fragment Shader
        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");
        // shader Program
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        checkCompileErrors(ID, "PROGRAM");
        // delete the shaders as they're linked into our program now and no longer necessary
        glDeleteShader(vertex);
        glDeleteShader(fragment);

    }
    // activate the shader
    // ------------------------------------------------------------------------
    void use() const
    {
        glUseProgram(ID);
    }
    // utility uniform functions
    // ------------------------------------------------------------------------
    void setBool(const std::string& name, bool value) const
    {
        glUniform1i(location(name), (int)value);
    }
    // ------------------------------------------------------------------------
    void setInt(const std::string& name, int value) const
    {
        glUniform1i(location(name), value);
    }
    // ------------------------------------------------------------------------
    void setFloat(const std::string& name, float value) const
    {
        glUniform1f(location(name), value);
    }
    // ------------------------------------------------------------------------
    void setVec2(const std::string& name, const glm::vec2& value) const
    {
        glUniform2fv(location(name), 1, &value[0]);
    }
    void setVec2(const std::string& name, float x, float y) const
    {
        glUniform2f(location(name), x, y);
    }
    // ------------------------------------------------------------------------
    void setVec3(const std::string& name, const glm::vec3& value) const
    {
        glUniform3fv(location(name), 1, &value[0]);
    }
    void setVec3(const std::string& name, float x, float y, float z) const
    {
        glUniform3f(location(name), x, y, z);
    }
    // ------------------------------------------------------------------------
    void setVec4(const std::string& name, const glm::vec4& value) const
    {
        glUniform4fv(location(name), 1, &value[0]);
    }
    void setVec4(const std::string& name, float x, float y, float z, float w) const
    {
        glUniform4f(location(name), x, y, z, w);
    }
    // ------------------------------------------------------------------------
    void setMat2(const std::string& name, const glm::mat2& mat) const
    {
        glUniformMatrix2fv(location(name), 1, GL_FALSE, &mat[0][0]);
    }
    // ------------------------------------------------------------------------
    void setMat3(const std::string& name, const glm::mat3& mat) const
    {
        glUniformMatrix3fv(location(name), 1, GL_FALSE, &mat[0][0]);
    }
    // ------------------------------------------------------------------------
    void setMat4(const std::string& name, const glm::mat4& mat) const
    {
        glUniformMatrix4fv(location(name), 1, GL_FALSE, &mat[0][0]);
    }

    unsigned int getID() const {
        return ID;
    }

    // True when every stage compiled and the program linked. Callers that build
    // shaders from user-authored source check this before caching the program,
    // so a broken edit never replaces the last working one.
    bool IsValid() const { return valid_; }

    // Accumulated GL info logs for the failed stages; empty when IsValid().
    const std::string& CompileLog() const { return compileLog_; }

private:
    bool valid_{true};
    std::string compileLog_;

    // glGetUniformLocation does a driver-side name lookup; every setXxx call
    // used to pay that cost every frame for every uniform on every draw. This
    // cache turns repeat lookups (the overwhelming majority) into a local
    // hash-map hit instead of a driver round trip.
    mutable std::unordered_map<std::string, GLint> uniformLocationCache_;
    GLint location(const std::string& name) const
    {
        auto it = uniformLocationCache_.find(name);
        if (it != uniformLocationCache_.end()) {
            return it->second;
        }
        const GLint loc = glGetUniformLocation(ID, name.c_str());
        uniformLocationCache_.emplace(name, loc);
        return loc;
    }

    // utility function for checking shader compilation/linking errors.
    // ------------------------------------------------------------------------
    // GLSL drivers emit one line per diagnostic, so a shader with a handful of
    // errors easily overruns a fixed 1024-byte buffer. Query the real length and
    // size the buffer to it, otherwise the editor shows a truncated log.
    static std::string fetchInfoLog(GLuint object, bool isProgram)
    {
        GLint length = 0;
        if (isProgram) {
            glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
        } else {
            glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
        }
        if (length <= 1) {
            return {};
        }
        std::vector<GLchar> buffer(static_cast<std::size_t>(length));
        GLsizei written = 0;
        if (isProgram) {
            glGetProgramInfoLog(object, length, &written, buffer.data());
        } else {
            glGetShaderInfoLog(object, length, &written, buffer.data());
        }
        return std::string(buffer.data(), static_cast<std::size_t>(written));
    }

    void checkCompileErrors(GLuint shader, std::string type)
    {
        GLint success = GL_FALSE;
        const bool isProgram = type == "PROGRAM";
        if (isProgram) {
            glGetProgramiv(shader, GL_LINK_STATUS, &success);
        } else {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        }
        if (success) {
            return;
        }

        const std::string infoLog = fetchInfoLog(shader, isProgram);
        const char* prefix = isProgram ? "ERROR::PROGRAM_LINKING_ERROR of type: " : "ERROR::SHADER_COMPILATION_ERROR of type: ";
        std::cout << prefix << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;

        valid_ = false;
        compileLog_ += type;
        compileLog_ += ":\n";
        compileLog_ += infoLog;
        if (!infoLog.empty() && infoLog.back() != '\n') {
            compileLog_ += '\n';
        }
    }
};
#endif
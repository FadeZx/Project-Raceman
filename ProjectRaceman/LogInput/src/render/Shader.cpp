#include "Shader.h"
#include <fstream>
#include <sstream>

using namespace Render;

static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) return {};     // return empty if path doesn’t exist
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}


static GLuint compile(GLenum type, const std::string& src, std::string& log) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        log.resize(len ? len - 1 : 0);
        if (len) glGetShaderInfoLog(s, len, nullptr, log.data());
        glDeleteShader(s); return 0;
    }
    return s;
}

Shader::~Shader() { if (m_program) glDeleteProgram(m_program); }

bool Shader::loadFromFiles(const std::string& vsPath, const std::string& fsPath, std::string* logOut) {
    std::string log;
    std::string vs = readFile(vsPath);
    std::string fs = readFile(fsPath);

    GLuint vsId = compile(GL_VERTEX_SHADER, vs, log);
    if (!vsId) { if (logOut) *logOut = "VS: " + log; return false; }
    GLuint fsId = compile(GL_FRAGMENT_SHADER, fs, log);
    if (!fsId) { if (logOut) *logOut = "FS: " + log; glDeleteShader(vsId); return false; }
    if (vs.empty() || fs.empty()) {
        if (logOut) *logOut = "Shader file empty. VS bytes=" + std::to_string(vs.size()) + " FS bytes=" + std::to_string(fs.size());
        return false;
    }
    m_program = glCreateProgram();
    glAttachShader(m_program, vsId);
    glAttachShader(m_program, fsId);
    glLinkProgram(m_program);
    glDeleteShader(vsId);
    glDeleteShader(fsId);

    GLint ok = 0; glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &len);
        if (logOut) {
            logOut->resize(len ? len - 1 : 0);
            if (len) glGetProgramInfoLog(m_program, len, nullptr, logOut->data());
        }
        glDeleteProgram(m_program); m_program = 0;
        return false;
    }
    return true;
}

void Shader::setMat4(const char* name, const float* m) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, m);
}

void Shader::setVec3(const char* name, float x, float y, float z) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc >= 0) glUniform3f(loc, x, y, z);
}

#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Shader
{
public:
    unsigned int ID;
    // constructor generates the shader on the fly
    // ------------------------------------------------------------------------
    Shader(const char* vertexPath, const char* fragmentPath)
    {
        // 1. retrieve the vertex/fragment source code, splicing in any #include
        //    directives before the driver ever sees the text.
        const std::string vertexCode = LoadShaderSource(vertexPath, "VERTEX");
        const std::string fragmentCode = LoadShaderSource(fragmentPath, "FRAGMENT");
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
    // Compute program. Distinct from the two-argument graphics constructor by
    // arity; marked explicit so a stray string literal cannot become a Shader.
    // Include resolution works here exactly as it does for the graphics stages.
    explicit Shader(const char* computePath)
    {
        const std::string computeCode = LoadShaderSource(computePath, "COMPUTE");
        const char* cShaderCode = computeCode.c_str();
        unsigned int compute = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(compute, 1, &cShaderCode, NULL);
        glCompileShader(compute);
        checkCompileErrors(compute, "COMPUTE");
        ID = glCreateProgram();
        glAttachShader(ID, compute);
        glLinkProgram(ID);
        checkCompileErrors(ID, "PROGRAM");
        glDeleteShader(compute);
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

    // Per-stage source-string table produced by the #include resolver: index 0 is
    // always the top-level .vs/.fs, and every spliced file gets the next index.
    // GLSL info logs report errors as "<sourceIndex>(<line>)", so this is what
    // turns "3(17): error" back into a file the user can actually open.
    std::unordered_map<std::string, std::vector<std::string>> includeSources_;

    // ------------------------------------------------------------------------
    // #include support
    //
    // GLSL has no preprocessor include of its own, and the shaders here need to
    // share code (BRDF helpers, fog, and later the clustered-light lookup) across
    // the built-in materials, graph-generated shaders, and hand-authored .fs
    // files. Resolving includes in one place here covers all three, because every
    // one of them is built through this constructor.
    //
    // Deliberate limitations, documented rather than fixed:
    //  - No real tokenizer: a #include inside a block comment is still processed.
    //  - Include-once semantics (see below), so no header guards are needed.
    //  - Editing an included file does not invalidate shaders that pull it in;
    //    use the editor's recompile-all path to pick those edits up.
    // ------------------------------------------------------------------------

    // Fallback search root, so a project-authored shader living outside
    // src/shaders can still reach the shared helpers with #include <common/...>.
    static const char* ShaderIncludeRoot() { return "src/shaders"; }

    struct IncludeContext
    {
        // Canonical paths already spliced into this stage. GLSL has no header
        // guards, so including a file twice would redefine every function in it;
        // include-once is both the sane default and what breaks include cycles.
        std::unordered_set<std::string> included;
        std::vector<std::string> sourceNames;
        std::string log;
        bool failed{false};
    };

    static bool ReadFileText(const std::filesystem::path& path, std::string& outText)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file) {
            return false;
        }
        std::ostringstream stream;
        stream << file.rdbuf();
        outText = stream.str();
        return true;
    }

    // Recognises `#include "path"` and `#include <path>` with arbitrary leading
    // whitespace. Anything else is passed through untouched.
    static bool ParseIncludeDirective(const std::string& line, std::string& outPath, bool& outAngled)
    {
        std::size_t cursor = line.find_first_not_of(" \t");
        if (cursor == std::string::npos || line.compare(cursor, 8, "#include") != 0) {
            return false;
        }
        cursor = line.find_first_not_of(" \t", cursor + 8);
        if (cursor == std::string::npos) {
            return false;
        }
        char closing;
        if (line[cursor] == '"') {
            closing = '"';
            outAngled = false;
        } else if (line[cursor] == '<') {
            closing = '>';
            outAngled = true;
        } else {
            return false;
        }
        const std::size_t end = line.find(closing, cursor + 1);
        if (end == std::string::npos || end == cursor + 1) {
            return false;
        }
        outPath = line.substr(cursor + 1, end - cursor - 1);
        return true;
    }

    // Quoted includes resolve next to the including file first; angled includes
    // check the engine shader root first. Both fall back to the other.
    static bool ResolveIncludePath(const std::string& requested,
                                   const std::filesystem::path& sourceDirectory,
                                   bool angled,
                                   std::filesystem::path& outPath)
    {
        const std::filesystem::path local = sourceDirectory / requested;
        const std::filesystem::path shared = std::filesystem::path(ShaderIncludeRoot()) / requested;
        const std::filesystem::path candidates[2] = {
            angled ? shared : local,
            angled ? local : shared
        };
        for (const std::filesystem::path& candidate : candidates) {
            std::error_code exists_ec;
            if (!std::filesystem::exists(candidate, exists_ec) || exists_ec) {
                continue;
            }
            std::error_code canonical_ec;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, canonical_ec);
            outPath = canonical_ec ? candidate : canonical;
            return true;
        }
        return false;
    }

    // GLSL permits exactly one #version, and it must come first. Blank the line
    // rather than erasing it so the included file's line numbers still match the
    // file on disk, which is what makes the #line directives below truthful.
    static void StripVersionDirectives(std::string& source)
    {
        std::istringstream in(source);
        std::ostringstream out;
        std::string line;
        while (std::getline(in, line)) {
            const std::size_t cursor = line.find_first_not_of(" \t");
            const bool isVersion = cursor != std::string::npos &&
                line.compare(cursor, 8, "#version") == 0;
            if (!isVersion) {
                out << line;
            }
            out << '\n';
        }
        source = out.str();
    }

    // Splices includes and brackets each one with #line directives, so a syntax
    // error inside a shared helper is reported against that helper's own line
    // numbers instead of an offset into the assembled text.
    static std::string ResolveIncludes(const std::string& source,
                                       const std::filesystem::path& sourceDirectory,
                                       int sourceIndex,
                                       int depth,
                                       IncludeContext& context)
    {
        constexpr int kMaxIncludeDepth = 8;
        std::ostringstream out;
        std::istringstream in(source);
        std::string line;
        int lineNumber = 0;
        while (std::getline(in, line)) {
            ++lineNumber;
            std::string requested;
            bool angled = false;
            if (!ParseIncludeDirective(line, requested, angled)) {
                out << line << '\n';
                continue;
            }

            if (depth >= kMaxIncludeDepth) {
                context.failed = true;
                context.log += "Include depth limit (" + std::to_string(kMaxIncludeDepth) +
                    ") exceeded at \"" + requested + "\"\n";
                continue;
            }

            std::filesystem::path resolved;
            if (!ResolveIncludePath(requested, sourceDirectory, angled, resolved)) {
                context.failed = true;
                context.log += "Could not resolve #include \"" + requested + "\" from " +
                    (sourceDirectory.empty() ? std::string(".") : sourceDirectory.generic_string()) + "\n";
                continue;
            }

            const std::string key = resolved.generic_string();
            if (!context.included.insert(key).second) {
                // Already pulled in for this stage. Skip it, but restore the line
                // counter so everything after the directive still lines up.
                out << "#line " << (lineNumber + 1) << ' ' << sourceIndex << '\n';
                continue;
            }

            std::string included;
            if (!ReadFileText(resolved, included)) {
                context.failed = true;
                context.log += "Could not read included file: " + key + "\n";
                continue;
            }
            StripVersionDirectives(included);

            const int includedIndex = static_cast<int>(context.sourceNames.size());
            context.sourceNames.push_back(key);
            out << "#line 1 " << includedIndex << '\n';
            out << ResolveIncludes(included, resolved.parent_path(), includedIndex, depth + 1, context);
            out << "#line " << (lineNumber + 1) << ' ' << sourceIndex << '\n';
        }
        return out.str();
    }

    std::string LoadShaderSource(const char* path, const char* stage)
    {
        const std::filesystem::path filePath(path != nullptr ? path : "");
        std::string source;
        if (!ReadFileText(filePath, source)) {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << filePath.string() << std::endl;
            valid_ = false;
            compileLog_ += stage;
            compileLog_ += ":\nCould not read shader source: ";
            compileLog_ += filePath.string();
            compileLog_ += '\n';
            return {};
        }

        IncludeContext context;
        std::error_code ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(filePath, ec);
        context.sourceNames.push_back((ec ? filePath : canonical).generic_string());
        context.included.insert(context.sourceNames.front());

        std::string resolved = ResolveIncludes(source, filePath.parent_path(), 0, 0, context);

        if (context.failed) {
            valid_ = false;
            compileLog_ += stage;
            compileLog_ += ":\n";
            compileLog_ += context.log;
            std::cout << "ERROR::SHADER::INCLUDE_RESOLUTION_FAILED of type: " << stage << "\n"
                      << context.log << " -- --------------------------------------------------- -- " << std::endl;
        }
        if (context.sourceNames.size() > 1) {
            includeSources_[stage] = std::move(context.sourceNames);
        }
        return resolved;
    }

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
        // Drivers report the source-string index alongside the line, e.g.
        // "1(24): error C0000". Without the legend that index is meaningless to
        // anyone reading the log in the editor's shader compile panel.
        const std::string legend = includeSourceLegend(type);
        const char* prefix = isProgram ? "ERROR::PROGRAM_LINKING_ERROR of type: " : "ERROR::SHADER_COMPILATION_ERROR of type: ";
        std::cout << prefix << type << "\n" << infoLog << legend << "\n -- --------------------------------------------------- -- " << std::endl;

        valid_ = false;
        compileLog_ += type;
        compileLog_ += ":\n";
        compileLog_ += infoLog;
        if (!infoLog.empty() && infoLog.back() != '\n') {
            compileLog_ += '\n';
        }
        compileLog_ += legend;
    }

    std::string includeSourceLegend(const std::string& stage) const
    {
        const auto it = includeSources_.find(stage);
        if (it == includeSources_.end() || it->second.empty()) {
            return {};
        }
        std::string legend = "Source strings:\n";
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            legend += "  " + std::to_string(i) + " = " + it->second[i] + '\n';
        }
        return legend;
    }
};
#endif
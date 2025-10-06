
#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif

// Mesh.cpp (top of file, once in the whole project)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <iostream>
#include "Mesh.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include <glad/glad.h>
#include <filesystem>
#include <sstream>
namespace fs = std::filesystem;


using namespace Render;


static inline bool fileExists(const std::string& p) {
    std::error_code ec;
    return fs::exists(fs::u8path(p), ec);
}
static std::string parseMapPath(const char* lineAfterKeyword) {
    // Trim EOL
    std::string s(lineAfterKeyword);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();

    // Trim leading spaces
    size_t i = s.find_first_not_of(" \t");
    if (i == std::string::npos) return {};
    s.erase(0, i);

    // If quoted => return quoted block
    if (s.find('"') != std::string::npos) {
        size_t q1 = s.find('"');
        size_t q2 = s.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
            return s.substr(q1 + 1, q2 - q1 - 1);
    }

    // Handle optional flags (e.g. -o, -s, -bm). Path starts at first token that doesn't begin with '-'
    size_t pos = 0;
    while (true) {
        // skip spaces
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
        if (pos >= s.size()) return {};

        if (s[pos] != '-') break;         // first non-option token => path begins here

        // consume current flag token
        size_t next = s.find_first_of(" \t", pos);
        if (next == std::string::npos) return {}; // malformed
        pos = next;

        // optional numeric args after flag (skip until next token that starts with '-' or end)
        while (pos < s.size()) {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
            if (pos >= s.size()) break;
            if (s[pos] == '-') break;     // next option, stop consuming args
            // consume this arg token
            next = s.find_first_of(" \t", pos);
            if (next == std::string::npos) { pos = s.size(); break; }
            pos = next;
        }
    }

    // The rest of the line is the path (may contain spaces)
    std::string path = s.substr(pos);
    // trim trailing spaces
    size_t end = path.find_last_not_of(" \t");
    if (end != std::string::npos) path.resize(end + 1); else path.clear();
    return path;
}


static GLuint LoadTexture2D(const std::string& path, bool flipY = true) {
    // 0) Make sure there is a current GL context on this thread
#if defined(SDL_MAJOR_VERSION)
    if (!SDL_GL_GetCurrentContext()) {
        std::cerr << "[LoadTexture2D] No current GL context! path=" << path << "\n";
        return 0;
    }
#endif

    // 1) Decode with stb_image (force 4 channels to avoid format mismatches)
    if (flipY) stbi_set_flip_vertically_on_load(1);
    int w = 0, h = 0, n_in = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n_in, 4); // <- force 4
    if (!data) {
        std::cerr << "[LoadTexture2D] Failed to load image: " << path << "\n";
        return 0;
    }
    if (w <= 0 || h <= 0) {
        std::cerr << "[LoadTexture2D] Invalid image size: " << path << " (" << w << "x" << h << ")\n";
        stbi_image_free(data);
        return 0;
    }

    // 2) Create & upload (always RGBA8)
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) {
        std::cerr << "[LoadTexture2D] glGenTextures returned 0 (no context?) path=" << path << "\n";
        stbi_image_free(data);
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // robust for odd widths
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
     std::cout << "[LoadTexture2D] OK " << path << " (" << w << "x" << h << ")\n";
    return tex;
}
bool Mesh::build(const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices)
{
    destroy();
    if (vertices.empty() || indices.empty()) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
        vertices.size() * sizeof(Vertex),
        vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        indices.size() * sizeof(uint32_t),
        indices.data(), GL_STATIC_DRAW);

    // layout: pos (0), nrm (1), uv (2)
    const GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, nrm));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, uv));

    glBindVertexArray(0);

    indexCount_ = indices.size();
    return true;
}

void Mesh::draw() const {
    if (!vao_ || !indexCount_) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, (GLsizei)indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Mesh::destroy() {
    if (ebo_) glDeleteBuffers(1, &ebo_), ebo_ = 0;
    if (vbo_) glDeleteBuffers(1, &vbo_), vbo_ = 0;
    if (vao_) glDeleteVertexArrays(1, &vao_), vao_ = 0;
    indexCount_ = 0;
}

// -------- Tiny OBJ loader (triangles only, no materials) --------
// Supports lines beginning with: v, vt, vn, f
// Faces must be triangles. Accepts "i/j/k", "i//k", "i/j", "i".
struct Triplet { int v = -1, t = -1, n = -1; };
static bool parseIndex(const char* s, int& out, const char** next) {
    // parse integer (possibly negative)
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s) return false;
    out = (int)v;
    *next = end;
    return true;
}
static bool parseFaceItem(const char* s, Triplet& tri, const char** next) {
    // formats: v / vt / vn separated by '/'
    // start with v
    if (!parseIndex(s, tri.v, next)) return false;
    if (**next == '/') {
        ++(*next);
        if (**next != '/') { // vt exists
            parseIndex(*next, tri.t, next);
        }
        if (**next == '/') {
            ++(*next);
            parseIndex(*next, tri.n, next); // vn (may or may not parse)
        }
    }
    return true;
}

bool Mesh::loadOBJ(const std::string& path, std::string* errOut) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (errOut) *errOut = "Could not open OBJ: " + path;
        return false;
    }

    // base directory for resolving mtllib / textures
    std::string baseDir = path;
    {
        auto pos = baseDir.find_last_of("/\\");
        baseDir = (pos == std::string::npos) ? std::string() : baseDir.substr(0, pos + 1);
    }
    std::cerr << "[OBJ] load: " << path << "\n";
    std::cerr << "[OBJ] baseDir: " << baseDir << "\n";

    std::string mtlFile;          // mtllib filename
    std::string currentMtl;       // last usemtl seen
    std::vector<glm::vec3> V;
    std::vector<glm::vec2> T;
    std::vector<glm::vec3> N;
    std::vector<Vertex> outVerts;
    std::vector<uint32_t> outIdx;

    struct Key { int v, t, n; };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return ((size_t)(k.v + 100000) * 73856093) ^
                ((size_t)(k.t + 100000) * 19349663) ^
                ((size_t)(k.n + 100000) * 83492791);
        }
    };
    struct KeyEq {
        bool operator()(const Key& a, const Key& b) const {
            return a.v == b.v && a.t == b.t && a.n == b.n;
        }
    };
    std::unordered_map<Key, uint32_t, KeyHash, KeyEq> lookup;

    auto trim_eol = [](std::string& s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        };

    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || std::strlen(line) < 2) continue;

        if (line[0] == 'm' && std::strncmp(line, "mtllib ", 7) == 0) {
            mtlFile = std::string(line + 7); trim_eol(mtlFile);
            continue;
        }
        if (line[0] == 'u' && std::strncmp(line, "usemtl ", 7) == 0) {
            currentMtl = std::string(line + 7); trim_eol(currentMtl);
            continue;
        }

        if (line[0] == 'v' && line[1] == ' ') {
            glm::vec3 p;
            std::sscanf(line + 2, "%f %f %f", &p.x, &p.y, &p.z);
            V.push_back(p);
        }
        else if (line[0] == 'v' && line[1] == 't') {
            glm::vec2 uv;
            std::sscanf(line + 3, "%f %f", &uv.x, &uv.y);
            T.push_back(uv);
        }
        else if (line[0] == 'v' && line[1] == 'n') {
            glm::vec3 n;
            std::sscanf(line + 3, "%f %f %f", &n.x, &n.y, &n.z);
            N.push_back(n);
        }
        else if (line[0] == 'f' && line[1] == ' ') {
            const char* s = line + 2;
            Triplet vertsOnFace[128];
            int count = 0;
            while (*s == ' ' || *s == '\t') ++s;
            while (*s && *s != '\n' && *s != '\r') {
                if (count >= (int)(sizeof(vertsOnFace) / sizeof(vertsOnFace[0]))) break;
                Triplet tri{}; const char* next = s;
                if (!parseFaceItem(s, tri, &next)) break;
                vertsOnFace[count++] = tri;
                s = next; while (*s == ' ' || *s == '\t') ++s;
            }
            if (count < 3) goto bad_face;

            auto fixIndex = [](int idx, int count)->int {
                if (idx > 0) return idx - 1;
                if (idx < 0) return count + idx;
                return -1;
                };
            auto addVertex = [&](const Triplet& it)->uint32_t {
                Key key{ fixIndex(it.v,(int)V.size()),
                         fixIndex(it.t,(int)T.size()),
                         fixIndex(it.n,(int)N.size()) };
                auto itFound = lookup.find(key);
                if (itFound == lookup.end()) {
                    Vertex vv{};
                    vv.pos = (key.v >= 0 && key.v < (int)V.size()) ? V[key.v] : glm::vec3(0);
                    vv.uv = (key.t >= 0 && key.t < (int)T.size()) ? T[key.t] : glm::vec2(0);
                    vv.nrm = (key.n >= 0 && key.n < (int)N.size()) ? N[key.n] : glm::vec3(0, 1, 0);
                    uint32_t newIndex = (uint32_t)outVerts.size();
                    outVerts.push_back(vv);
                    lookup.emplace(key, newIndex);
                    return newIndex;
                }
                return itFound->second;
                };

            uint32_t i0 = addVertex(vertsOnFace[0]);
            for (int i = 2; i < count; ++i) {
                uint32_t i1 = addVertex(vertsOnFace[i - 1]);
                uint32_t i2 = addVertex(vertsOnFace[i]);
                outIdx.push_back(i0); outIdx.push_back(i1); outIdx.push_back(i2);
            }
        }
        continue;

    bad_face:
        if (errOut) *errOut = "Non-triangle or malformed face in " + path;
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    if (outVerts.empty() || outIdx.empty()) {
        if (errOut) *errOut = "OBJ produced no geometry: " + path;
        return false;
    }

    // Build geometry
    if (!build(outVerts, outIdx)) {
        if (errOut) *errOut = "OpenGL build failed for OBJ: " + path;
        return false;
    }

    // If there is an MTL, read its diffuse map (map_Kd)
   // If exporter forgot to write mtllib, try <objBasename>.mtl as a fallback
   // If there is an MTL, read its diffuse map (map_Kd)
    if (!mtlFile.empty()) {
        const std::string mtlPath =
            (fs::path(mtlFile).is_absolute() ? mtlFile : (baseDir + mtlFile));

        std::cerr << "[MTL] mtllib: " << mtlFile << "\n";
        std::cerr << "[MTL] full:   " << mtlPath << "  (exists=" << (fileExists(mtlPath) ? "yes" : "no") << ")\n";

        FILE* mf = std::fopen(mtlPath.c_str(), "rb");
        if (!mf) {
            std::cerr << "[MTL] Could not open MTL file\n";
        }
        else {
            std::string wanted = currentMtl; // may be empty
            std::string mapKdMatched, mapKdFirst;
            std::string cur;

            char l[2048];
            auto trim = [&](std::string& s) { while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back(); };

            while (std::fgets(l, sizeof(l), mf)) {
                if (l[0] == '#' || std::strlen(l) < 2) continue;

                if (!std::strncmp(l, "newmtl ", 7)) {
                    cur = std::string(l + 7); trim(cur);
                }
                else if (!std::strncmp(l, "map_Kd ", 7)) {
                    std::string p = parseMapPath(l + 7); // you already have this
                    if (!p.empty()) {
                        if (mapKdFirst.empty()) mapKdFirst = p;        // remember the first one we encounter
                        if (!wanted.empty() && cur == wanted) {        // prefer the one matching usemtl
                            mapKdMatched = p;
                            break;
                        }
                    }
                }
            }
            std::fclose(mf);

            // prefer matched, otherwise first
            std::string mapKd = !mapKdMatched.empty() ? mapKdMatched : mapKdFirst;
            if (!mapKd.empty()) {
                const std::string fullTex = (fs::path(mapKd).is_absolute() ? mapKd : (baseDir + mapKd));
                std::cerr << "[TEX] chosen: " << fullTex << " (exists=" << (fileExists(fullTex) ? "yes" : "no") << ")\n";
                tex_ = LoadTexture2D(fullTex);
                if (tex_) { texPath_ = fullTex; std::cerr << "[TEX] loaded OK\n"; }
                else { std::cerr << "[TEX] load FAILED (format? DDS not supported by stb_image)\n"; }
            }
            else {
                std::cerr << "[MTL] No map_Kd anywhere in MTL\n";
            }

        }
    }
    else {
        std::cerr << "[MTL] No mtllib in OBJ (and no fallback)\n";
    }

    return true;
}



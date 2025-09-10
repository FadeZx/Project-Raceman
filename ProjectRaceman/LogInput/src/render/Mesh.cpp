
#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include "Mesh.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

using namespace Render;

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

    std::vector<glm::vec3> V;
    std::vector<glm::vec2> T;
    std::vector<glm::vec3> N;

    std::vector<Vertex> outVerts;
    std::vector<uint32_t> outIdx;

    // map "unique triplet" -> index
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

    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        // skip comments/blank
        if (line[0] == '#' || std::strlen(line) < 2) continue;

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
            // Expect exactly 3 vertices (triangles)
            const char* s = line + 2;
            Triplet a{}, b{}, c{};
            if (!parseFaceItem(s, a, &s)) goto bad_face;
            while (*s == ' ') ++s;
            if (!parseFaceItem(s, b, &s)) goto bad_face;
            while (*s == ' ') ++s;
            if (!parseFaceItem(s, c, &s)) goto bad_face;

            Triplet items[3] = { a, b, c };
            for (int i = 0; i < 3; ++i) {
                // convert OBJ 1-based (and negatives) to 0-based indices
                auto fixIndex = [](int idx, int count)->int {
                    if (idx > 0) return idx - 1;
                    if (idx < 0) return count + idx;
                    return -1;
                    };
                Key key{
                    fixIndex(items[i].v, (int)V.size()),
                    fixIndex(items[i].t, (int)T.size()),
                    fixIndex(items[i].n, (int)N.size())
                };

                auto it = lookup.find(key);
                if (it == lookup.end()) {
                    Vertex vv{};
                    vv.pos = (key.v >= 0 && key.v < (int)V.size()) ? V[key.v] : glm::vec3(0);
                    vv.uv = (key.t >= 0 && key.t < (int)T.size()) ? T[key.t] : glm::vec2(0);
                    vv.nrm = (key.n >= 0 && key.n < (int)N.size()) ? N[key.n] : glm::vec3(0, 1, 0);

                    uint32_t newIndex = (uint32_t)outVerts.size();
                    outVerts.push_back(vv);
                    lookup.emplace(key, newIndex);
                    outIdx.push_back(newIndex);
                }
                else {
                    outIdx.push_back(it->second);
                }
            }
        }
        // ignore: usemtl, mtllib, o, s, g...
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
    return build(outVerts, outIdx);
}

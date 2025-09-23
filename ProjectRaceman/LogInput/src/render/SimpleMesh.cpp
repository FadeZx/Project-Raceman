#include "SimpleMesh.h"
#include <vector>
#include <glad/glad.h>
using namespace Render;



static int g_axesCount = 0;

GLuint SimpleMesh::makeAxes(float axisLen, float shaft) {
    // 3 colored axes as lines from origin
    struct V { float x, y, z; };
    std::vector<V> v;
    v.reserve(6);
    // X (red)
    v.push_back({ 0,0,0 }); v.push_back({ axisLen,0,0 });
    // Y (green)
    v.push_back({ 0,0,0 }); v.push_back({ 0,axisLen,0 });
    // Z (blue)
    v.push_back({ 0,0,0 }); v.push_back({ 0,0,axisLen });

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(V)), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
    glBindVertexArray(0);
    g_axesCount = (int)v.size();
    return vao;
}
int SimpleMesh::axesVertexCount() { return g_axesCount; }

GLuint SimpleMesh::makeGrid(int halfExtent, float step) {
    std::vector<float> v;
    for (int i = -halfExtent; i <= halfExtent; ++i) {
        float x = i * step;
        // line parallel to Z
        v.push_back(x); v.push_back(0.0f); v.push_back(-halfExtent * step);
        v.push_back(x); v.push_back(0.0f); v.push_back(halfExtent * step);
        // line parallel to X
        v.push_back(-halfExtent * step); v.push_back(0.0f); v.push_back(x);
        v.push_back(halfExtent * step); v.push_back(0.0f); v.push_back(x);
    }

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    return vao;
}

int SimpleMesh::gridVertexCount(int halfExtent, float /*step*/) {
    // 2 lines per i, 2 vertices per line, (2*halfExtent+1) values of i
    return (2 * halfExtent + 1) * 2 * 2;
}

GLuint SimpleMesh::makeCube() {
    const float v[] = {
        // positions (a unit cube centered at origin)
        -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,
        -0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f,
        -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
        -0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
        -0.5f,-0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f, 0.5f, 0.5f,
        -0.5f,-0.5f,-0.5f, -0.5f, 0.5f, 0.5f, -0.5f,-0.5f, 0.5f,
         0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,
         0.5f,-0.5f,-0.5f,  0.5f, 0.5f, 0.5f,  0.5f,-0.5f, 0.5f,
        -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,
        -0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f,
        -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f
    };

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    return vao;
}

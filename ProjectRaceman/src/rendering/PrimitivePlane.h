#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

namespace raceman {

// Simple unit XZ plane at y=0, from (-0.5,0,-0.5) to (0.5,0,0.5)
class PrimitivePlane {
public:
    PrimitivePlane() { create(); }
    ~PrimitivePlane() { destroy(); }

    unsigned int vao() const { return vao_; }
    unsigned int indexCount() const { return indexCount_; }

private:
    void create() {
        if (vao_ != 0) return;

        // Positions (x,y,z), Normals, UVs
        struct V { float x,y,z; float nx,ny,nz; float u,v; };
        const V verts[] = {
            {-0.5f, 0.0f, -0.5f, 0,1,0, 0,0},
            { 0.5f, 0.0f, -0.5f, 0,1,0, 1,0},
            { 0.5f, 0.0f,  0.5f, 0,1,0, 1,1},
            {-0.5f, 0.0f,  0.5f, 0,1,0, 0,1},
        };
        const unsigned int idx[] = {
            0,1,2,
            2,3,0
        };
        indexCount_ = 6;

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

        // layout: 0 pos, 1 normal, 2 uv
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(6*sizeof(float)));

        glBindVertexArray(0);
    }

    void destroy() {
        if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
        if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
        if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
        indexCount_ = 0;
    }

private:
    unsigned int vao_{0};
    unsigned int vbo_{0};
    unsigned int ebo_{0};
    unsigned int indexCount_{0};
};

} // namespace raceman
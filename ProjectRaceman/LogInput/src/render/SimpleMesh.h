#pragma once
#include <glad/glad.h>
#include <vector>

namespace Render {

    // Very small helper to draw a grid and a unit cube
    class SimpleMesh {
    public:
        static GLuint makeGrid(int halfExtent = 10, float step = 1.0f); // returns VAO, lines
        static GLuint makeCube();                                       // returns VAO, triangles
        static int    gridVertexCount(int halfExtent, float step);      // for glDrawArrays
    };
}

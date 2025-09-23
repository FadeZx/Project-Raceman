#pragma once
#include <glad/glad.h>
#include <vector>

namespace Render {
    struct SimpleMesh {
        static GLuint makeGrid(int halfCount, float spacing);         // you already have
        static int    gridVertexCount(int halfCount, float spacing);  // you already have
		static GLuint makeCube();                                     // you already have

        static GLuint makeAxes(float axisLen = 1.5f, float shaft = 0.015f);
        static int    axesVertexCount(); // returns number of vertices for GL_LINES
    };
}


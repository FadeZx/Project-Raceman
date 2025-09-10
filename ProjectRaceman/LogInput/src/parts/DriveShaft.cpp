#include "DriveShaft.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glad/glad.h>

namespace Vehicle {

    static glm::mat4 lineTransform(const glm::vec3& A, const glm::vec3& B) {
        // build a transform that takes a unit cylinder z∈[-0.5,0.5] to A→B
        glm::vec3 d = B - A;
        float L = glm::length(d);
        if (L < 1e-5f) return glm::translate(glm::mat4(1), A);
        glm::vec3 z = glm::normalize(d);
        glm::vec3 x = glm::normalize(glm::cross(glm::vec3(0, 1, 0), z));
        if (glm::length(x) < 1e-5f) x = glm::vec3(1, 0, 0);
        glm::vec3 y = glm::normalize(glm::cross(z, x));

        glm::mat4 R(1);
        R[0] = glm::vec4(x, 0);
        R[1] = glm::vec4(y, 0);
        R[2] = glm::vec4(z, 0);
        glm::mat4 S = glm::scale(glm::mat4(1), { 0.05f, 0.05f, L }); // thin rod
        glm::mat4 T = glm::translate(glm::mat4(1), (A + B) * 0.5f);
        return T * R * glm::rotate(glm::mat4(1), 0.0f, { 1,0,0 }) * S;
    }

    void DriveShaft::draw(const glm::mat4& vehicleXform, GLuint shader, const glm::vec3& color) const {
        glm::vec4 A4 = vehicleXform * glm::vec4(localA, 1);
        glm::vec4 B4 = vehicleXform * glm::vec4(localB, 1);
        glm::mat4 M = lineTransform(glm::vec3(A4), glm::vec3(B4));
        M = M * model.local;

        glUseProgram(shader);
        GLint locM = glGetUniformLocation(shader, "uModel");
        GLint locC = glGetUniformLocation(shader, "uColor");
        if (locM >= 0) glUniformMatrix4fv(locM, 1, GL_FALSE, &M[0][0]);
        if (locC >= 0) glUniform3fv(locC, 1, &color[0]);
        model.draw();
    }

} // namespace Vehicle

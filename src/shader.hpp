#pragma once

#include "graphics.hpp"

GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource);

inline constexpr const char* fullscreenVertex = R"glsl(
out vec2 clipPosition;
void main() {
    const vec2 vertices[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    clipPosition = vertices[gl_VertexID];
    gl_Position = vec4(clipPosition, 0.0, 1.0);
}
)glsl";

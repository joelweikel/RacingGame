#pragma once

#include "driving.hpp"

#include <string_view>
#include <vector>

#include "graphics.hpp"

class Hud {
public:
    Hud();
    ~Hud();

    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    void render(const CarState& car, float redlineRpm, int width, int height);

private:
    struct Pixel {
        glm::vec2 position;
        glm::vec4 color;
    };

    void rectangle(float x, float y, float width, float height, const glm::vec4& color);
    void text(std::string_view value, float x, float y, float cell, const glm::vec4& color);

    std::vector<Pixel> pixels_;
    GLuint program_ = 0;
    GLuint vertexArray_ = 0;
    GLuint buffer_ = 0;
    GLint viewport_ = -1;
};

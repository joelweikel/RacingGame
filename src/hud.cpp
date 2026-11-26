#include "hud.hpp"
#include "shader.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace {

constexpr const char* vertexShader = R"glsl(
layout (location = 0) in vec2 position;
layout (location = 1) in vec4 color;
uniform vec2 uViewport;
out vec4 vertexColor;
void main() {
    gl_Position = vec4(position / uViewport * 2.0 - 1.0, 0.0, 1.0);
    vertexColor = color;
}
)glsl";

constexpr const char* fragmentShader = R"glsl(
in vec4 vertexColor;
out vec4 pixelColor;
void main() {
    pixelColor = vertexColor;
}
)glsl";

constexpr std::string_view glyphNames = "0123456789AEGHLMNOPRTU";
constexpr std::uint8_t glyphs[][7] = {
    {14,17,19,21,25,17,14}, // 0
    {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},
    {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},
    {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}, // 9
    {14,17,17,31,17,17,17}, // A
    {31,16,16,30,16,16,31}, // E
    {14,17,16,23,17,17,15}, // G
    {17,17,17,31,17,17,17}, // H
    {16,16,16,16,16,16,31}, // L
    {17,27,21,21,17,17,17}, // M
    {17,25,25,21,19,19,17}, // N
    {14,17,17,17,17,17,14}, // O
    {30,17,17,30,16,16,16}, // P
    {30,17,17,30,20,18,17}, // R
    {31,4,4,4,4,4,4}, // T
    {17,17,17,17,17,17,14}, // U
};
static_assert(glyphNames.size() == sizeof(glyphs) / sizeof(glyphs[0]));

} // namespace

Hud::Hud()
{
    pixels_.reserve(2400);
    program_ = createShaderProgram(vertexShader, fragmentShader);
    viewport_ = glGetUniformLocation(program_, "uViewport");

    glGenVertexArrays(1, &vertexArray_);
    glGenBuffers(1, &buffer_);
    glBindVertexArray(vertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Pixel), reinterpret_cast<const void*>(offsetof(Pixel, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Pixel), reinterpret_cast<const void*>(offsetof(Pixel, color)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

Hud::~Hud()
{
    glDeleteVertexArrays(1, &vertexArray_);
    glDeleteBuffers(1, &buffer_);
    glDeleteProgram(program_);
}

void Hud::rectangle(float x, float y, float width, float height, const glm::vec4& color)
{
    const Pixel bottomLeft{{x, y}, color};
    const Pixel bottomRight{{x + width, y}, color};
    const Pixel topRight{{x + width, y + height}, color};
    const Pixel topLeft{{x, y + height}, color};
    pixels_.insert(pixels_.end(), {bottomLeft, bottomRight, topRight, bottomLeft, topRight, topLeft});
}

void Hud::text(std::string_view value, float x, float y, float cell, const glm::vec4& color)
{
    for (const char character : value) {
        const auto glyph = glyphNames.find(character);
        if (glyph != std::string_view::npos) {
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < 5; ++column) {
                    if (glyphs[glyph][row] & (1 << (4 - column))) {
                        rectangle(x + column * cell, y + (6 - row) * cell, cell, cell, color);
                    }
                }
            }
        }
        x += 6.0f * cell;
    }
}

void Hud::render(const CarState& car, float redlineRpm, int width, int height)
{
    const float scale = std::min({height / 720.0f, width / 425.0f, 2.0f});
    const float x = width - 252.0f * scale;
    const float y = 20.0f * scale;
    constexpr glm::vec4 panel{0.04f, 0.08f, 0.10f, 0.90f};
    constexpr glm::vec4 line{0.14f, 0.26f, 0.29f, 1.0f};
    constexpr glm::vec4 light{0.95f, 0.97f, 0.97f, 1.0f};
    constexpr glm::vec4 label{0.60f, 0.76f, 0.78f, 1.0f};
    constexpr glm::vec4 accent{0.40f, 0.93f, 0.79f, 1.0f};
    constexpr glm::vec4 shiftLight{1.0f, 0.46f, 0.23f, 1.0f};

    pixels_.clear();
    rectangle(x, y, 232.0f * scale, 116.0f * scale, panel);
    rectangle(x, y + 114.0f * scale, 232.0f * scale, 2.0f * scale, accent);
    rectangle(x + 124.0f * scale, y + 42.0f * scale, scale, 57.0f * scale, line);

    text("MPH", x + 16.0f * scale, y + 87.0f * scale, 2.0f * scale, label);
    const long mph = std::min(999L, std::lround(glm::length(car.velocity) * metersPerSecondToMph));
    text(std::to_string(mph), x + 16.0f * scale, y + 44.0f * scale, 5.0f * scale, light);
    text("GEAR", x + 143.0f * scale, y + 87.0f * scale, 2.0f * scale, label);
    text(car.gear < 0 ? "R" : std::to_string(car.gear), x + 143.0f * scale, y + 44.0f * scale, 5.0f * scale, accent);
    text("RPM", x + 16.0f * scale, y + 22.0f * scale, 1.7f * scale, label);
    text(car.automatic ? "AUTO" : "MANUAL", x + 143.0f * scale, y + 22.0f * scale, 1.7f * scale, label);

    const float revs = std::clamp(car.rpm / redlineRpm, 0.0f, 1.0f);
    rectangle(x + 16.0f * scale, y + 12.0f * scale, 200.0f * scale, 4.0f * scale, line);
    rectangle(x + 16.0f * scale, y + 12.0f * scale, 200.0f * scale * revs, 4.0f * scale,
              revs > 0.90f ? shiftLight : accent);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glUniform2f(viewport_, static_cast<float>(width), static_cast<float>(height));
    glBindVertexArray(vertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, pixels_.size() * sizeof(Pixel), pixels_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(pixels_.size()));
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

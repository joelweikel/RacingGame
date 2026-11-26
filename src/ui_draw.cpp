#include "ui_draw.hpp"
#include "shader.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

constexpr float fontPixels = 52.0f;
constexpr glm::vec2 solid{-1.0f};
constexpr float pi = 3.141592654f;

constexpr auto vertexShader = R"glsl(
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
uniform vec2 uViewport;
uniform vec2 uOrigin;
uniform float uScale;
out vec2 texCoord;
out vec4 tint;
void main() {
    vec2 p = (uOrigin + position * uScale) / uViewport;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    texCoord = uv;
    tint = color;
}
)glsl";

constexpr auto fragmentShader = R"glsl(
in vec2 texCoord;
in vec4 tint;
uniform sampler2D uAtlas;
out vec4 pixelColor;
void main() {
    float coverage = texCoord.x < 0.0 ? 1.0 : texture(uAtlas, texCoord).r;
    pixelColor = vec4(tint.rgb, tint.a * coverage);
}
)glsl";

glm::vec2 polar(float angle)
{
    return {std::cos(angle), std::sin(angle)};
}

} // namespace

UiDraw::UiDraw(const std::filesystem::path& assets)
{
    constexpr int atlasSize = 1024;
    std::vector<unsigned char> bitmap(atlasSize * atlasSize);
    const std::array fonts{"Barlow-SemiBold.ttf", "BarlowCondensed-ExtraBoldItalic.ttf"};
    stbtt_pack_context pack{};
    if (!stbtt_PackBegin(&pack, bitmap.data(), atlasSize, atlasSize, 0, 1, nullptr)) {
        throw std::runtime_error("Could not initialize the UI font atlas");
    }
    for (std::size_t style = 0; style < fonts.size(); ++style) {
        std::ifstream file(assets / fonts[style], std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        std::array<stbtt_packedchar, 95> metrics{};
        if (bytes.empty() || !stbtt_PackFontRange(&pack, bytes.data(), 0, fontPixels, 32,
                                                 static_cast<int>(metrics.size()), metrics.data())) {
            stbtt_PackEnd(&pack);
            throw std::runtime_error("Could not load UI font " + std::string(fonts[style]));
        }
        for (std::size_t i = 0; i < metrics.size(); ++i) {
            const auto& c = metrics[i];
            glyphs_[style][i] = {{c.xoff, c.yoff, c.xoff2, c.yoff2},
                glm::vec4(c.x0, c.y0, c.x1, c.y1) / static_cast<float>(atlasSize), c.xadvance};
        }
    }
    stbtt_PackEnd(&pack);

    vertices_.reserve(16000);
    program_ = createShaderProgram(vertexShader, fragmentShader);
    viewportUniform_ = glGetUniformLocation(program_, "uViewport");
    scaleUniform_ = glGetUniformLocation(program_, "uScale");
    originUniform_ = glGetUniformLocation(program_, "uOrigin");
    glGenTextures(1, &atlas_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasSize, atlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &buffer_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, uv)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, color)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

UiDraw::~UiDraw()
{
    glDeleteTextures(1, &atlas_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &buffer_);
    glDeleteProgram(program_);
}

void UiDraw::begin(int width, int height)
{
    viewport_ = {width, height};
    scale_ = std::min(viewport_.x / uiSize.x, viewport_.y / uiSize.y);
    origin_ = (viewport_ - uiSize * scale_) * 0.5f;
    vertices_.clear();
}

glm::vec2 UiDraw::fromScreen(glm::vec2 p) const { return (p - origin_) / scale_; }
glm::vec2 UiDraw::toScreen(glm::vec2 p) const { return origin_ + p * scale_; }

void UiDraw::finish()
{
    if (vertices_.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glUniform2f(viewportUniform_, viewport_.x, viewport_.y);
    glUniform2f(originUniform_, origin_.x, origin_.y);
    glUniform1f(scaleUniform_, scale_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void UiDraw::quad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d)
{
    vertices_.insert(vertices_.end(), {a, b, c, a, c, d});
}

void UiDraw::triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 color)
{
    vertices_.insert(vertices_.end(), {{a, solid, color}, {b, solid, color}, {c, solid, color}});
}

void UiDraw::rect(UiBox b, glm::vec4 color) { gradient(b, color, color); }

void UiDraw::gradient(UiBox b, glm::vec4 top, glm::vec4 bottom)
{
    quad({{b.x, b.y}, solid, top}, {{b.x + b.w, b.y}, solid, top},
         {{b.x + b.w, b.y + b.h}, solid, bottom}, {{b.x, b.y + b.h}, solid, bottom});
}

void UiDraw::rounded(UiBox b, float radius, glm::vec4 color)
{
    radius = std::min(radius, std::min(b.w, b.h) * 0.5f);
    const glm::vec2 centers[]{{b.x + b.w - radius, b.y + radius}, {b.x + b.w - radius, b.y + b.h - radius},
                              {b.x + radius, b.y + b.h - radius}, {b.x + radius, b.y + radius}};
    glm::vec2 previous{b.x + radius, b.y};
    for (int corner = 0; corner < 4; ++corner) {
        for (int step = 0; step <= 10; ++step) {
            const auto point = centers[corner] + polar((corner - 1.0f + step * 0.1f) * pi * 0.5f) * radius;
            triangle(b.center(), previous, point, color);
            previous = point;
        }
    }
}

void UiDraw::line(glm::vec2 from, glm::vec2 to, float thickness, glm::vec4 color)
{
    const auto delta = to - from;
    const float length = glm::length(delta);
    if (length < 0.0001f) return;
    const glm::vec2 side = glm::vec2(-delta.y, delta.x) * (0.5f * thickness / length);
    quad({from - side, solid, color}, {to - side, solid, color},
         {to + side, solid, color}, {from + side, solid, color});
}

void UiDraw::arc(glm::vec2 center, glm::vec2 radius, float start, float end, float thickness, glm::vec4 color)
{
    const int steps = std::max(1, static_cast<int>(std::abs(end - start) * 22.0f));
    auto previous = center + polar(start) * radius;
    for (int i = 1; i <= steps; ++i) {
        const auto next = center + polar(start + (end - start) * i / steps) * radius;
        line(previous, next, thickness, color);
        previous = next;
    }
}

void UiDraw::circle(glm::vec2 center, float radius, glm::vec4 color)
{
    auto previous = center + glm::vec2(radius, 0);
    for (int step = 1; step <= 40; ++step) {
        const auto next = center + polar(step * pi / 20.0f) * radius;
        triangle(center, previous, next, color);
        previous = next;
    }
}

void UiDraw::poly(std::initializer_list<glm::vec2> points, glm::vec4 color)
{
    if (points.size() < 3) return;
    for (auto p = points.begin() + 1; p + 1 != points.end(); ++p) {
        triangle(*points.begin(), *p, *(p + 1), color);
    }
}

void UiDraw::text(std::string_view value, glm::vec2 top, float size, glm::vec4 color, UiFont font)
{
    const float ratio = size / fontPixels;
    glm::vec2 pen = top + glm::vec2(0, size * 0.82f);
    for (unsigned char c : value) {
        if (c < 32 || c > 126) c = '?';
        const auto& g = glyphs_[static_cast<std::size_t>(font)][c - 32];
        const glm::vec2 a = pen + glm::vec2(g.bounds.x, g.bounds.y) * ratio;
        const glm::vec2 b = pen + glm::vec2(g.bounds.z, g.bounds.w) * ratio;
        quad({a, {g.uv.x, g.uv.y}, color}, {{b.x, a.y}, {g.uv.z, g.uv.y}, color},
             {b, {g.uv.z, g.uv.w}, color}, {{a.x, b.y}, {g.uv.x, g.uv.w}, color});
        pen.x += g.advance * ratio;
    }
}

float UiDraw::measure(std::string_view value, float size, UiFont font) const
{
    float advance = 0.0f;
    for (unsigned char c : value) {
        if (c < 32 || c > 126) c = '?';
        advance += glyphs_[static_cast<std::size_t>(font)][c - 32].advance;
    }
    return advance * size / fontPixels;
}

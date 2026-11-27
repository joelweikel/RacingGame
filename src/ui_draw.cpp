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

struct FontFace {
    const char* file;
    float pixels;
    int first;
    int count;
};

constexpr std::array fonts{FontFace{"Barlow-SemiBold.ttf", 52.0f, 32, 95},
                           FontFace{"BarlowCondensed-ExtraBoldItalic.ttf", 104.0f, 32, 95},
                           FontFace{"Spectral-BoldItalic.ttf", 112.0f, '0', 10}};
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

constexpr auto blurFragment = R"glsl(
in vec2 clipPosition;
uniform sampler2D uInput;
uniform vec2 uStep;
out vec4 pixelColor;
void main() {
    vec2 uv = clipPosition * 0.5 + 0.5;
    vec3 color = texture(uInput, uv).rgb;
    if (uStep != vec2(0.0)) {
        const vec4 offsets = vec4(1.458430, 3.403985, 5.351806, 7.302941);
        const vec4 weights = vec4(0.233308, 0.135928, 0.051383, 0.012595);
        color *= 0.133571;
        for (int i = 0; i < 4; ++i) {
            vec2 tap = uStep * offsets[i];
            color += (texture(uInput, uv + tap).rgb + texture(uInput, uv - tap).rgb) * weights[i];
        }
    }
    pixelColor = vec4(color, 0.0);
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
    stbtt_pack_context pack{};
    if (!stbtt_PackBegin(&pack, bitmap.data(), atlasSize, atlasSize, 0, 1, nullptr)) {
        throw std::runtime_error("Could not initialize the UI font atlas");
    }
    for (std::size_t style = 0; style < fonts.size(); ++style) {
        const auto& font = fonts[style];
        std::ifstream file(assets / font.file, std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        std::array<stbtt_packedchar, 95> metrics{};
        if (bytes.empty() || !stbtt_PackFontRange(&pack, bytes.data(), 0, font.pixels, font.first,
                                                 font.count, metrics.data())) {
            stbtt_PackEnd(&pack);
            throw std::runtime_error("Could not load UI font " + std::string(font.file));
        }
        for (int i = 0; i < font.count; ++i) {
            const auto& c = metrics[i];
            glyphs_[style][font.first - 32 + i] = {{c.xoff, c.yoff, c.xoff2, c.yoff2},
                glm::vec4(c.x0, c.y0, c.x1, c.y1) / static_cast<float>(atlasSize), c.xadvance};
        }
    }
    stbtt_PackEnd(&pack);

    vertices_.reserve(16000);
    lights_.reserve(512);
    program_ = createShaderProgram(vertexShader, fragmentShader);
    try {
        blurProgram_ = createShaderProgram(fullscreenVertex, blurFragment);
    } catch (...) {
        glDeleteProgram(program_);
        throw;
    }
    viewportUniform_ = glGetUniformLocation(program_, "uViewport");
    scaleUniform_ = glGetUniformLocation(program_, "uScale");
    originUniform_ = glGetUniformLocation(program_, "uOrigin");
    blurStep_ = glGetUniformLocation(blurProgram_, "uStep");
    glGenFramebuffers(static_cast<GLsizei>(bloomFrames_.size()), bloomFrames_.data());
    glGenTextures(static_cast<GLsizei>(bloomTextures_.size()), bloomTextures_.data());
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
    glDeleteFramebuffers(static_cast<GLsizei>(bloomFrames_.size()), bloomFrames_.data());
    glDeleteTextures(static_cast<GLsizei>(bloomTextures_.size()), bloomTextures_.data());
    glDeleteTextures(1, &atlas_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &buffer_);
    glDeleteProgram(program_);
    glDeleteProgram(blurProgram_);
}

void UiDraw::begin(int width, int height)
{
    viewport_ = {width, height};
    scale_ = std::min(viewport_.x / uiSize.x, viewport_.y / uiSize.y);
    origin_ = (viewport_ - uiSize * scale_) * 0.5f;
    vertices_.clear();
    lights_.clear();
}

glm::vec2 UiDraw::fromScreen(glm::vec2 p) const { return (p - origin_) / scale_; }
glm::vec2 UiDraw::toScreen(glm::vec2 p) const { return origin_ + p * scale_; }

UiBox UiDraw::viewportBounds() const
{
    const auto top = fromScreen(glm::vec2(0));
    return {top.x, top.y, viewport_.x / scale_, viewport_.y / scale_};
}

void UiDraw::finish()
{
    if (vertices_.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawVertices(vertices_);
    if (!lights_.empty()) drawBloom();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void UiDraw::drawVertices(const std::vector<Vertex>& vertices) const
{
    glUseProgram(program_);
    glUniform2f(viewportUniform_, viewport_.x, viewport_.y);
    glUniform2f(originUniform_, origin_.x, origin_.y);
    glUniform1f(scaleUniform_, scale_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
}

void UiDraw::resizeBloom(glm::ivec2 size)
{
    glActiveTexture(GL_TEXTURE0);
    for (std::size_t i = 0; i < bloomTextures_.size(); ++i) {
        glBindTexture(GL_TEXTURE_2D, bloomTextures_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFrames_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTextures_[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            throw std::runtime_error("Could not create UI bloom framebuffer");
        }
    }
    bloomSize_ = size;
}

void UiDraw::drawBloom()
{
    const auto size = glm::max(glm::ivec2(1), glm::ivec2(viewport_ / (2.0f * std::max(1.0f, scale_))));
    if (size != bloomSize_) resizeBloom(size);

    // Blur only the UI's light sources so the scenery and text retain their detail.
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFrames_[0]);
    glViewport(0, 0, size.x, size.y);
    constexpr float clear[4]{};
    glClearBufferfv(GL_COLOR, 0, clear);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    drawVertices(lights_);

    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFrames_[1]);
    glUseProgram(blurProgram_);
    glBindTexture(GL_TEXTURE_2D, bloomTextures_[0]);
    glUniform2f(blurStep_, 1.0f / size.x, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, bloomFrames_[0]);
    glBindTexture(GL_TEXTURE_2D, bloomTextures_[1]);
    glUniform2f(blurStep_, 0, 1.0f / size.y);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, static_cast<int>(viewport_.x), static_cast<int>(viewport_.y));
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBindTexture(GL_TEXTURE_2D, bloomTextures_[0]);
    glUniform2f(blurStep_, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
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

void UiDraw::beveled(UiBox b, float radius, glm::vec4 top, glm::vec4 bottom,
                    glm::vec4 rimTop, glm::vec4 rimBottom)
{
    constexpr float bevel = 2.0f;
    roundedGradient(b, radius, rimTop, rimBottom);
    roundedGradient({b.x + bevel, b.y + bevel, b.w - bevel * 2, b.h - bevel * 2}, radius - bevel, top, bottom);
}

void UiDraw::bloom(UiBox b, glm::vec4 color)
{
    const Vertex a{{b.x, b.y}, solid, color}, c{{b.x + b.w, b.y + b.h}, solid, color};
    lights_.insert(lights_.end(), {a, {{c.position.x, a.position.y}, solid, color}, c,
                                  a, c, {{a.position.x, c.position.y}, solid, color}});
}

void UiDraw::rounded(UiBox b, float radius, glm::vec4 color)
{
    roundedGradient(b, radius, color, color);
}

void UiDraw::roundedGradient(UiBox b, float radius, glm::vec4 top, glm::vec4 bottom)
{
    radius = std::clamp(radius, 0.0f, std::min(b.w, b.h) * 0.5f);
    const glm::vec2 centers[]{{b.x + b.w - radius, b.y + radius}, {b.x + b.w - radius, b.y + b.h - radius},
                              {b.x + radius, b.y + b.h - radius}, {b.x + radius, b.y + radius}};
    glm::vec2 previous{b.x + radius, b.y};
    const Vertex center{b.center(), solid, glm::mix(top, bottom, 0.5f)};
    const auto vertex = [&](glm::vec2 point) {
        return Vertex{point, solid, glm::mix(top, bottom, (point.y - b.y) / b.h)};
    };
    for (int corner = 0; corner < 4; ++corner) {
        for (int step = 0; step <= 10; ++step) {
            const auto point = centers[corner] + polar((corner - 1.0f + step * 0.1f) * pi * 0.5f) * radius;
            vertices_.insert(vertices_.end(), {center, vertex(previous), vertex(point)});
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
    const auto inner = glm::max(radius - thickness * 0.5f, glm::vec2(0));
    const auto outer = radius + thickness * 0.5f;
    auto previous = polar(start);
    for (int i = 1; i <= steps; ++i) {
        const auto next = polar(start + (end - start) * i / steps);
        quad({center + previous * inner, solid, color}, {center + next * inner, solid, color},
             {center + next * outer, solid, color}, {center + previous * outer, solid, color});
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
    const auto style = static_cast<std::size_t>(font);
    const float ratio = size / fonts[style].pixels;
    glm::vec2 pen = top + glm::vec2(0, size * 0.82f);
    for (unsigned char c : value) {
        if (c < 32 || c > 126) c = '?';
        const auto& g = glyphs_[style][c - 32];
        const glm::vec2 a = pen + glm::vec2(g.bounds.x, g.bounds.y) * ratio;
        const glm::vec2 b = pen + glm::vec2(g.bounds.z, g.bounds.w) * ratio;
        quad({a, {g.uv.x, g.uv.y}, color}, {{b.x, a.y}, {g.uv.z, g.uv.y}, color},
             {b, {g.uv.z, g.uv.w}, color}, {{a.x, b.y}, {g.uv.x, g.uv.w}, color});
        pen.x += g.advance * ratio;
    }
}

float UiDraw::measure(std::string_view value, float size, UiFont font) const
{
    const auto style = static_cast<std::size_t>(font);
    float advance = 0.0f;
    for (unsigned char c : value) {
        if (c < 32 || c > 126) c = '?';
        advance += glyphs_[style][c - 32].advance;
    }
    return advance * size / fonts[style].pixels;
}

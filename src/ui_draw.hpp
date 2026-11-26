#pragma once

#include "graphics.hpp"

#include <array>
#include <filesystem>
#include <initializer_list>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

inline constexpr glm::vec2 uiSize{1280.0f, 720.0f};
enum class UiFont { Label, Display };

struct UiBox {
    float x, y, w, h;
    bool contains(glm::vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
    glm::vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
};

class UiDraw {
public:
    explicit UiDraw(const std::filesystem::path& assets);
    ~UiDraw();
    UiDraw(const UiDraw&) = delete;
    UiDraw& operator=(const UiDraw&) = delete;

    void begin(int width, int height);
    glm::vec2 fromScreen(glm::vec2 p) const;
    glm::vec2 toScreen(glm::vec2 p) const;
    void finish();
    void rect(UiBox box, glm::vec4 color);
    void gradient(UiBox box, glm::vec4 top, glm::vec4 bottom);
    void rounded(UiBox box, float radius, glm::vec4 color);
    void line(glm::vec2 from, glm::vec2 to, float thickness, glm::vec4 color);
    void arc(glm::vec2 center, glm::vec2 radius, float start, float end, float thickness, glm::vec4 color);
    void circle(glm::vec2 center, float radius, glm::vec4 color);
    void poly(std::initializer_list<glm::vec2> points, glm::vec4 color);
    void text(std::string_view value, glm::vec2 top, float size, glm::vec4 color, UiFont font = UiFont::Label);
    float measure(std::string_view value, float size, UiFont font = UiFont::Label) const;

private:
    struct Vertex {
        glm::vec2 position, uv;
        glm::vec4 color;
    };
    struct Glyph {
        glm::vec4 bounds, uv;
        float advance = 0.0f;
    };
    void quad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d);
    void triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 color);

    std::array<std::array<Glyph, 95>, 2> glyphs_{};
    std::vector<Vertex> vertices_;
    glm::vec2 viewport_{uiSize}, origin_{0.0f};
    float scale_ = 1.0f;
    GLuint program_ = 0, vao_ = 0, buffer_ = 0, atlas_ = 0;
    GLint viewportUniform_ = -1, scaleUniform_ = -1, originUniform_ = -1;
};

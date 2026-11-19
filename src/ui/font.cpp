#include "engine/ui/font.hpp"

#include "engine/ui/theme.hpp"

#include "Logger.h"
#include "Rendering/TextRenderer.h"

#include <cmath>
#include <string>

namespace engine::ui
{

std::size_t utf8Length(std::string_view text, std::size_t i) noexcept
{
    const auto c = static_cast<unsigned char>(text[i]);
    std::size_t n = 1;
    if ((c & 0xE0U) == 0xC0U)
        n = 2;
    else if ((c & 0xF0U) == 0xE0U)
        n = 3;
    else if ((c & 0xF8U) == 0xF0U)
        n = 4;
    if (i + n > text.size())
        return 1;
    for (std::size_t k = 1; k < n; ++k)
    {
        if ((static_cast<unsigned char>(text[i + k]) & 0xC0U) != 0x80U)
            return 1;
    }
    return n;
}

std::size_t utf8Previous(std::string_view text, std::size_t i) noexcept
{
    if (i == 0)
        return 0;
    std::size_t j = i - 1;
    while (j > 0 && (static_cast<unsigned char>(text[j]) & 0xC0U) == 0x80U && i - j < 4)
        --j;
    return j;
}

std::size_t Font::caretIndex(std::string_view text, float size, float x, const FontFace *face) const
{
    std::size_t best = 0;
    float bestDistance = std::abs(x);
    for (std::size_t i = 0; i < text.size();)
    {
        i += utf8Length(text, i);
        const float d = std::abs(measure(text.substr(0, i), size, face).x - x);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

// ── FixedFont ────────────────────────────────────────────────────────────────────────────────────────────────────────

glm::vec2 FixedFont::measure(std::string_view text, float size, const FontFace *) const
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); i += utf8Length(text, i))
        ++count;
    return {static_cast<float>(count) * advance_ * size, size};
}

void FixedFont::draw(renderer::DrawList &list, std::string_view text, glm::vec2 position, float size, const glm::vec4 &color,
                     const FontFace *) const
{
    renderer::DrawGlyphs run;
    run.color = glm::vec3(color);
    float x = position.x;
    const float w = advance_ * size;
    for (std::size_t i = 0; i < text.size(); i += utf8Length(text, i))
    {
        if (text[i] != ' ')
        {
            const float y0 = position.y;
            const float y1 = position.y + size;
            const float verts[] = {x, y0, 0, 0, x, y1, 0, 1, x + w, y1, 1, 1, x, y0, 0, 0, x + w, y1, 1, 1, x + w, y0, 1, 0};
            run.vertices.insert(run.vertices.end(), std::begin(verts), std::end(verts));
        }
        x += w;
    }
    if (!run.vertices.empty())
        list.glyphs(std::move(run));
}

// ── TextRendererFont ─────────────────────────────────────────────────────────────────────────────────────────────────

void TextRendererFont::select(const FontFace *face) const
{
    if (face == nullptr)
    {
        renderer_->setActiveFont(defaultName_);
        return;
    }
    const std::string key = face->path.generic_string();
    auto it = loaded_.find(key);
    if (it == loaded_.end())
    {
        const bool ok = renderer_->loadFont(key, key, static_cast<unsigned int>(baseSize_));
        if (!ok)
        {
            LOG_WARNING("UI: font '", face->name, "' (", key, ") cannot be loaded; the default font is used");
        }
        it = loaded_.emplace(key, ok).first;
    }
    renderer_->setActiveFont(it->second ? key : defaultName_);
}

glm::vec2 TextRendererFont::measure(std::string_view text, float size, const FontFace *face) const
{
    select(face);
    const float scale = size / baseSize_;
    glm::vec2 m = renderer_->measureText(std::string(text), scale);
    // Hauteur de ligne constante (indépendante des caractères présents) : les lignes d'un panneau s'alignent.
    m.y = std::round(renderer_->measureText("Hg", scale).y);
    return m;
}

void TextRendererFont::draw(renderer::DrawList &list, std::string_view text, glm::vec2 position, float size, const glm::vec4 &color,
                            const FontFace *face) const
{
    select(face);
    renderer_->setDrawListSink(&list);
    renderer_->renderText(std::string(text), position.x, position.y, size / baseSize_, glm::vec3(color), ::UI::TextAnchor::TopLeft);
    renderer_->setDrawListSink(nullptr);
}

} // namespace engine::ui

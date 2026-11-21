#include "orosmo/ui/font.hpp"

#include "orosmo/ui/theme.hpp"

#include <orosmo/core/log.hpp>
#include "orosmo/ui/text_renderer.hpp"

#include <cmath>
#include <string>

namespace orosmo::ui
{

char32_t utf8Decode(std::string_view text, std::size_t i, std::size_t &length) noexcept
{
    length = 1;
    const auto b0 = static_cast<unsigned char>(text[i]);
    if (b0 < 0x80U)
        return b0;
    std::size_t n = 0;
    char32_t cp = 0;
    if (b0 >= 0xC0U && b0 < 0xE0U)
    {
        n = 2;
        cp = b0 & 0x1FU;
    }
    else if (b0 >= 0xE0U && b0 < 0xF0U)
    {
        n = 3;
        cp = b0 & 0x0FU;
    }
    else if (b0 >= 0xF0U && b0 < 0xF8U)
    {
        n = 4;
        cp = b0 & 0x07U;
    }
    else // continuation isolée (80–BF) ou F8–FF
        return 0xFFFD;
    if (i + n > text.size())
        return 0xFFFD;
    for (std::size_t k = 1; k < n; ++k)
    {
        const auto b = static_cast<unsigned char>(text[i + k]);
        if ((b & 0xC0U) != 0x80U)
            return 0xFFFD;
        cp = (cp << 6U) | (b & 0x3FU);
    }
    // Forme la plus courte seulement (C0 AF n'est pas « / »), ni demi-code de substitution, ni valeur au-delà de U+10FFFF.
    static constexpr char32_t kMinimum[] = {0, 0, 0x80U, 0x800U, 0x10000U};
    if (cp < kMinimum[n] || (cp >= 0xD800U && cp <= 0xDFFFU) || cp > 0x10FFFFU)
        return 0xFFFD;
    length = n;
    return cp;
}

std::size_t utf8Length(std::string_view text, std::size_t i) noexcept
{
    std::size_t n = 1;
    (void)utf8Decode(text, i, n);
    return n;
}

std::size_t utf8Previous(std::string_view text, std::size_t i) noexcept
{
    if (i == 0)
        return 0;
    // Une séquence valide qui finit exactement en `i` commence par un octet de tête (jamais une continuation) : c'est l'unité
    // précédente. Sinon l'octet `i - 1` est une unité à lui seul (invalide, ou ASCII).
    for (std::size_t back = 2; back <= 4 && back <= i; ++back)
    {
        if (utf8Length(text, i - back) == back)
            return i - back;
    }
    return i - 1;
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
    run.alpha = color.a;
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
    // Clé : fichier + date de modification lue au chargement du thème ; un fichier remplacé (thème rechargé) est relu.
    const std::string key = face->path.generic_string() + "@" + std::to_string(face->stamp);
    auto it = loaded_.find(key);
    if (it == loaded_.end())
    {
        const bool ok = renderer_->loadFont(face->path.generic_string(), key, static_cast<unsigned int>(baseSize_));
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
    renderer_->renderText(std::string(text), position.x, position.y, size / baseSize_, color, TextAnchor::TopLeft); // alpha compris
    renderer_->setDrawListSink(nullptr);
}

} // namespace orosmo::ui

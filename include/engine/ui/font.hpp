// Texte de l'interface : mesure et dessin d'une ligne. `Font` est l'interface ; `FixedFont` (chasse fixe, sans atlas) sert aux tests
// et au mode sans GPU ; `TextRendererFont` adapte le rendu de texte existant (FreeType + atlas MSDF, `UI::TextRenderer`).
#pragma once

#include "engine/renderer/draw_list.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <string_view>

namespace UI
{
class TextRenderer;
}

namespace engine::ui
{

class Font
{
  public:
    virtual ~Font() = default;
    // Taille d'une ligne de texte (UTF-8) à `size` pixels : largeur, hauteur de ligne.
    virtual glm::vec2 measure(std::string_view text, float size) const = 0;
    // Ajoute la ligne à `list` ; `position` est le coin haut gauche de la boîte de ligne.
    virtual void draw(renderer::DrawList &list, std::string_view text, glm::vec2 position, float size, const glm::vec4 &color) const = 0;
    // Position (octet, début d'un caractère UTF-8) la plus proche de l'abscisse `x` mesurée depuis le début de la ligne.
    std::size_t caretIndex(std::string_view text, float size, float x) const;
};

// Chaque caractère fait `advance` × size de large, la ligne `size` de haut. Le dessin ajoute une série de glyphes SANS atlas
// (ignorée par le canevas) : six sommets par caractère non blanc, ce que les tests comptent.
class FixedFont final : public Font
{
  public:
    explicit FixedFont(float advance = 0.5f) : advance_(advance) {}
    glm::vec2 measure(std::string_view text, float size) const override;
    void draw(renderer::DrawList &list, std::string_view text, glm::vec2 position, float size, const glm::vec4 &color) const override;

  private:
    float advance_;
};

// Police chargée par `UI::TextRenderer` (taille de base de la police : `baseSize` pixels, 48 pour la police par défaut). Le texte est
// lu octet par octet (ASCII ; voir docs/GAPS.md, REN-36). Le TextRenderer doit vivre plus longtemps que cet objet.
class TextRendererFont final : public Font
{
  public:
    explicit TextRendererFont(::UI::TextRenderer &renderer, float baseSize = 48.0f) : renderer_(&renderer), baseSize_(baseSize) {}
    glm::vec2 measure(std::string_view text, float size) const override;
    void draw(renderer::DrawList &list, std::string_view text, glm::vec2 position, float size, const glm::vec4 &color) const override;

  private:
    ::UI::TextRenderer *renderer_;
    float baseSize_;
};

// Longueur en octets du caractère UTF-8 qui commence à `text[i]` (1 pour un octet invalide).
std::size_t utf8Length(std::string_view text, std::size_t i) noexcept;
// Début du caractère qui précède la position `i`.
std::size_t utf8Previous(std::string_view text, std::size_t i) noexcept;

} // namespace engine::ui

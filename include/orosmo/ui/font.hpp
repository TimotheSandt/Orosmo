// Texte de l'interface : mesure et dessin d'une ligne. `Font` est l'interface ; `FixedFont` (chasse fixe, sans atlas) sert aux tests
// et au mode sans GPU ; `TextRendererFont` adapte le rendu de texte (FreeType + atlas MSDF, `TextRenderer`, text_renderer.hpp).
#pragma once

#include "orosmo/renderer/draw_list.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace orosmo::ui
{

class TextRenderer; // text_renderer.hpp

struct FontFace; // theme.hpp : police nommée par un thème (fichier)

// `face` : police du style (thème, `"font"`) ; nulle : la police par défaut.
class Font
{
  public:
    virtual ~Font() = default;
    // Taille d'une ligne de texte (UTF-8) à `size` pixels : largeur, hauteur de ligne.
    virtual glm::vec2 measure(std::string_view text, float size, const FontFace *face = nullptr) const = 0;
    // Ajoute la ligne à `list` ; `position` est le coin haut gauche de la boîte de ligne.
    virtual void draw(renderer::DrawList &list, std::string_view text, glm::vec2 position, float size, const glm::vec4 &color,
                      const FontFace *face = nullptr) const = 0;
    // Position (octet, début d'un caractère UTF-8) la plus proche de l'abscisse `x` mesurée depuis le début de la ligne.
    std::size_t caretIndex(std::string_view text, float size, float x, const FontFace *face = nullptr) const;
};

// Chaque caractère fait `advance` × size de large, la ligne `size` de haut. Le dessin ajoute une série de glyphes SANS atlas
// (ignorée par le canevas) : six sommets par caractère non blanc, ce que les tests comptent.
class FixedFont final : public Font
{
  public:
    explicit FixedFont(float advance = 0.5f) : advance_(advance) {}
    glm::vec2 measure(std::string_view text, float size, const FontFace *face = nullptr) const override;
    void draw(renderer::DrawList &list, std::string_view text, glm::vec2 position, float size, const glm::vec4 &color,
              const FontFace *face = nullptr) const override;

  private:
    float advance_;
};

// Polices de `TextRenderer` (FreeType + MSDF ; taille de base `baseSize` pixels). Texte UTF-8 : ASCII, Latin-1 et Latin
// étendu A (accents), ponctuation typographique courante. `defaultName` : police déjà chargée par l'application ; les polices des
// thèmes sont chargées au premier usage (une police illisible est signalée une fois et remplacée par celle par défaut). Le
// TextRenderer doit vivre plus longtemps que cet objet.
class TextRendererFont final : public Font
{
  public:
    explicit TextRendererFont(TextRenderer &renderer, float baseSize = 48.0f, std::string defaultName = "default")
        : renderer_(&renderer), baseSize_(baseSize), defaultName_(std::move(defaultName))
    {
    }
    glm::vec2 measure(std::string_view text, float size, const FontFace *face = nullptr) const override;
    void draw(renderer::DrawList &list, std::string_view text, glm::vec2 position, float size, const glm::vec4 &color,
              const FontFace *face = nullptr) const override;

  private:
    void select(const FontFace *face) const;
    TextRenderer *renderer_;
    float baseSize_;
    std::string defaultName_;
    mutable std::map<std::string, bool> loaded_; // fichier -> chargé (faux : illisible, police par défaut)
};

// Décodage UTF-8 commun à l'édition et au rendu (mêmes unités) : point de code qui commence à `text[i]` et sa longueur en octets
// (`length`). Une séquence invalide (octet de continuation isolé, F8–FF, séquence tronquée ou trop longue, demi-code de
// substitution, valeur au-delà de U+10FFFF) donne U+FFFD sur UN octet.
char32_t utf8Decode(std::string_view text, std::size_t i, std::size_t &length) noexcept;
// Longueur en octets du caractère UTF-8 qui commence à `text[i]` (1 pour un octet invalide).
std::size_t utf8Length(std::string_view text, std::size_t i) noexcept;
// Début du caractère qui précède la position `i` (un seul octet en arrière si celui-ci est invalide).
std::size_t utf8Previous(std::string_view text, std::size_t i) noexcept;

} // namespace orosmo::ui

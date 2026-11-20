// UI-03 (couleur, dégradé, courbe) : conversions de couleur (RGB, HSV, HSL, hexadécimal), roue de couleur, éditeur de couleur à
// plusieurs formats, éditeur de dégradé, éditeur de courbe. Les types `Gradient` et `Curve` sont des données simples, évaluables
// par le jeu (couleur d'un biome selon l'altitude, répartition des hauteurs...).
#pragma once

#include "engine/renderer/draw_list.hpp"
#include "engine/ui/widgets.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine::ui
{

// ── Conversions (composantes dans [0, 1] ; teinte en [0, 1) = [0°, 360°)) ──
glm::vec3 rgbToHsv(glm::vec3 rgb) noexcept;
glm::vec3 hsvToRgb(glm::vec3 hsv) noexcept;
glm::vec3 rgbToHsl(glm::vec3 rgb) noexcept;
glm::vec3 hslToRgb(glm::vec3 hsl) noexcept;
// "#rrggbb" ou "#rrggbbaa" (avec alpha si `alpha`) ; lecture : avec ou sans « # », 6 ou 8 chiffres.
std::string toHex(const Color &c, bool alpha);
std::optional<Color> fromHex(std::string_view text) noexcept;

// ── Dégradé ──
struct GradientStop
{
    float position = 0.0f; // [0, 1]
    Color color{1.0f};
    bool operator==(const GradientStop &) const = default;
};

struct Gradient
{
    std::vector<GradientStop> stops{{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}}, {1.0f, {1.0f, 1.0f, 1.0f, 1.0f}}};
    // Couleur en `t` (interpolation linéaire entre les arrêts triés ; avant le premier et après le dernier : leur couleur).
    Color evaluate(float t) const;
    void sort();
    bool operator==(const Gradient &) const = default;
};

// ── Courbe ──
struct Curve
{
    std::vector<glm::vec2> points{{0.0f, 0.0f}, {1.0f, 1.0f}}; // dans [0, 1]², triés par x
    bool smooth = true; // interpolation cubique monotone (pas de dépassement) ; sinon linéaire
    float evaluate(float x) const;
    bool operator==(const Curve &) const = default;
};

// Roue de couleur : anneau des teintes, carré saturation × valeur au centre. Glisser sur l'anneau change la teinte, dans le carré
// la saturation et la valeur. La teinte est gardée quand la couleur devient grise (pas de saut).
class ColorWheel : public Widget
{
  public:
    explicit ColorWheel(Binding<Color> value);
    Color color() const;
    float hue() const noexcept { return hue_; }

    bool interactive() const override { return true; }
    void onPress(Ui &ui, glm::vec2 mouse) override;
    void onDrag(Ui &ui, glm::vec2 mouse) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawBackground(const Ui &, renderer::DrawList &, const Style &) const override {}
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    struct Geometry
    {
        glm::vec2 center;
        float outer;
        float inner;
        Rect square;
    };
    Geometry geometry() const;
    void pick(Ui &ui, glm::vec2 mouse);
    void refreshImages(int size) const;

    Binding<Color> value_;
    float hue_ = 0.0f;
    int part_ = 0; // 1 : anneau, 2 : carré
    mutable std::shared_ptr<renderer::AtlasImage> ring_;
    mutable std::shared_ptr<renderer::AtlasImage> square_;
    mutable float squareHue_ = -1.0f;
    std::optional<Color> pending_;
};

enum class ColorFormat : std::uint8_t
{
    kRgb,
    kHsv,
    kHsl,
};

// Éditeur de couleur complet : roue, format choisi (RGB 0-255, HSV, HSL) avec trois curseurs, alpha (facultatif), champ
// hexadécimal, aperçu de la couleur d'origine et de la nouvelle.
class ColorEditor : public VBox
{
  public:
    ColorEditor(Binding<Color> value, bool alpha = false);
    Color color() const { return value_.value(); }
    ColorFormat format() const noexcept { return format_; }
    void clearChildren() override {} // structure interne : pas de contenu effaçable

  private:
    void rebuildChannels();
    Binding<Color> value_;
    bool alpha_;
    ColorFormat format_ = ColorFormat::kRgb;
    int formatIndex_ = 0;
    VBox *channels_;
    Color original_;
    std::string hex_;
};

// Éditeur de dégradé : la barre du dégradé et ses arrêts ; glisser un arrêt le déplace, cliquer sur la barre en ajoute un (de la
// couleur à cet endroit), clic droit sur un arrêt le supprime (il en reste toujours un) ; l'arrêt choisi se règle dans un
// éditeur de couleur sous la barre.
class GradientEditor : public VBox
{
  public:
    explicit GradientEditor(Binding<Gradient> value);
    Gradient gradient() const { return value_.value(); }
    int selected() const noexcept { return selected_; }
    void clearChildren() override {} // structure interne : pas de contenu effaçable

  private:
    friend class GradientBar;
    Binding<Gradient> value_;
    int selected_ = 0;
    Color editing_{1.0f};
};

// Éditeur de courbe : glisser un point le déplace (les extrémités restent en x = 0 et x = 1), cliquer ailleurs en ajoute un, clic
// droit sur un point le supprime (il en reste au moins deux).
class CurveEditor : public Widget
{
  public:
    explicit CurveEditor(Binding<Curve> value);
    Curve curve() const { return value_.value(); }

    bool interactive() const override { return true; }
    void onPress(Ui &ui, glm::vec2 mouse) override;
    void onDrag(Ui &ui, glm::vec2 mouse) override;
    bool onSecondaryClick(Ui &ui, glm::vec2 mouse) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    Rect area(const Ui &ui) const;
    glm::vec2 toScreen(const Rect &a, glm::vec2 p) const;
    glm::vec2 toCurve(const Rect &a, glm::vec2 s) const;
    int pointAt(const Ui &ui, glm::vec2 mouse) const;
    void write(Ui &ui, Curve c);

    Binding<Curve> value_;
    int dragging_ = -1;
    std::optional<Curve> pending_;
};

} // namespace engine::ui

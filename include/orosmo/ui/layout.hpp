// UI-06 : conteneurs de mise en page. `VBox` / `HBox` : enfants l'un après l'autre, espacés de `spacing` (thème) ; sur l'axe
// principal, les enfants `fill` se partagent la place restante selon leur poids ; sur l'axe secondaire, chacun est aligné (début,
// centre, fin, étiré). `Grid` : colonnes (un enfant peut en couvrir plusieurs), largeur de colonne = plus large enfant ; une colonne
// qui contient un enfant `fill` prend la place restante. `Overlay` : enfants superposés, chacun aligné sur les deux axes.
// `ScrollView` : un contenu plus grand que la vue, défilement à la molette et par la barre, découpé à la vue.
#pragma once

#include "orosmo/ui/widget.hpp"

namespace orosmo::ui
{

enum class Axis : std::uint8_t
{
    kHorizontal,
    kVertical,
};

class Box : public Widget
{
  public:
    Box(Axis axis, std::string styleName = "box");
    Axis axis() const noexcept { return axis_; }

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void arrange(Ui &ui, const Rect &content, const Style &style) override;

  private:
    Axis axis_;
};

class VBox : public Box
{
  public:
    explicit VBox(std::string styleName = "box") : Box(Axis::kVertical, std::move(styleName)) {}
};

class HBox : public Box
{
  public:
    explicit HBox(std::string styleName = "box") : Box(Axis::kHorizontal, std::move(styleName)) {}
};

// Boîte verticale avec le style "panel" (fond, bordure, ombre du thème).
class Panel : public VBox
{
  public:
    Panel() : VBox("panel") {}
};

class Grid : public Widget
{
  public:
    explicit Grid(int columns, std::string styleName = "box");
    int columns() const noexcept { return columns_; }

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void arrange(Ui &ui, const Rect &content, const Style &style) override;

  private:
    struct Cell
    {
        Widget *widget;
        int row;
        int column;
        int span;
    };
    std::vector<Cell> cells() const;
    void computeTracks(Ui &ui, glm::vec2 available, const Style &style);

    int columns_;
    std::vector<float> columnWidths_;
    std::vector<float> rowHeights_;
    std::vector<bool> fillColumns_;
};

class Overlay : public Widget
{
  public:
    explicit Overlay(std::string styleName = "box") : Widget(std::move(styleName)) {}

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void arrange(Ui &ui, const Rect &content, const Style &style) override;
};

class ScrollView : public Widget
{
  public:
    explicit ScrollView(std::string styleName = "scroll") : Widget(std::move(styleName)) {}

    float scroll() const noexcept { return scroll_; }
    float maxScroll() const noexcept { return maxScroll_; }
    void setScroll(float y) noexcept;

    bool interactive() const override { return maxScroll_ > 0.0f; }
    bool clipsChildren() const override { return true; }
    Rect childClip() const override { return viewport_; }
    bool onWheel(Ui &ui, glm::vec2 wheel) override;
    void onPress(Ui &ui, glm::vec2 mouse) override;
    void onDrag(Ui &ui, glm::vec2 mouse) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void arrange(Ui &ui, const Rect &content, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    Rect thumb() const;

    float scroll_ = 0.0f;
    float maxScroll_ = 0.0f;
    float contentHeight_ = 0.0f;
    Rect viewport_;
    float barWidth_ = 0.0f;
    bool draggingBar_ = false;
    float dragOffset_ = 0.0f;
};

} // namespace orosmo::ui

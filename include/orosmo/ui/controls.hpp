// UI-03 (suite) : contrôles de saisie. Même règles que widgets.hpp : apparence lue dans le thème (type de style entre parenthèses),
// valeurs par liaison (écriture différée), événement onChange.
//
//   RadioButton (radio)        un choix parmi plusieurs : coché quand la valeur liée vaut `value`
//   RangeSlider (slider)       plage [bas, haut] à deux poignées (même comportement qu'un Slider : clic, glissement relatif, Maj,
//                              clic droit = défauts)
//   SpinBox (textField, button) champ numérique avec boutons − et + (et molette)
//   TextArea (textField)       éditeur de texte sur plusieurs lignes : sélection, presse-papiers, défilement, numéros de ligne
//
// La sélection et le presse-papiers (Maj + flèches, glisser, Ctrl+A, C, X, V) valent aussi pour TextField et NumberField.
#pragma once

#include "engine/ui/widgets.hpp"

#include <array>
#include <string>

namespace engine::ui
{

class RadioButton : public Widget
{
  public:
    RadioButton(std::string text, int value, Binding<int> selected);
    bool checked() const { return selected_.value() == value_; }

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onRelease(Ui &ui, glm::vec2 mouse, bool inside) override;
    void onKey(Ui &ui, int key) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawBackground(const Ui &, renderer::DrawList &, const Style &) const override {}
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    void select(Ui &ui);
    std::string text_;
    int value_;
    Binding<int> selected_;
};

class RangeSlider : public Widget
{
  public:
    RangeSlider(Binding<double> low, Binding<double> high, double min, double max, double step = 0.0);
    double low() const;
    double high() const;
    std::string formatted() const;
    RangeSlider &setDefaults(double low, double high)
    {
        defaultLow_ = low;
        defaultHigh_ = high;
        return *this;
    }

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onPress(Ui &ui, glm::vec2 mouse) override;
    void onDrag(Ui &ui, glm::vec2 mouse) override;
    void onRelease(Ui &ui, glm::vec2 mouse, bool inside) override;
    bool onSecondaryClick(Ui &ui, glm::vec2 mouse) override;
    void onKey(Ui &ui, int key) override;
    // Poignée prise par le dernier appui ou la dernière touche (0 : basse, 1 : haute).
    int activeHandle() const noexcept { return handle_; }

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    double quantize(double v) const;
    double valueAt(const Ui &ui, float x) const;
    float xOf(const Ui &ui, double v) const;
    void write(Ui &ui, int handle, double v);

    Binding<double> low_;
    Binding<double> high_;
    double min_;
    double max_;
    double step_;
    double defaultLow_;
    double defaultHigh_;
    int handle_ = 0;
    bool dragging_ = false;
    float pressX_ = 0.0f;
    float lastX_ = 0.0f;
    double dragValue_ = 0.0;
    std::array<bool, 2> hasPending_{false, false};
    std::array<double, 2> pending_{0.0, 0.0};
};

class SpinBox : public Widget
{
  public:
    SpinBox(Binding<double> value, double min, double max, double step = 1.0, bool integer = true);
    double value() const { return value_.value(); }
    void step(Ui &ui, double direction);
    void clearChildren() override {} // champ et boutons internes : pas de contenu effaçable

    bool onWheel(Ui &ui, glm::vec2 wheel) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void arrange(Ui &ui, const Rect &content, const Style &style) override;
    void drawBackground(const Ui &, renderer::DrawList &, const Style &) const override {}

  private:
    Binding<double> value_;
    double min_;
    double max_;
    double step_;
    Widget *field_;
    Widget *minus_;
    Widget *plus_;
    Ui *ui_ = nullptr; // interface de la dernière mise en page (les boutons écrivent par elle)
};

class TextArea : public Widget
{
  public:
    explicit TextArea(Binding<std::string> value);
    TextArea &setLineNumbers(bool on) { lineNumbers_ = on; return *this; }
    TextArea &setReadOnly(bool on) { readOnly_ = on; return *this; }
    // Retour à la ligne automatique à la largeur du widget (aux espaces si possible). Par défaut : oui.
    TextArea &setWrap(bool on) { wrap_ = on; return *this; }
    bool wraps() const noexcept { return wrap_; }
    // Rangées affichées (une ligne longue en donne plusieurs avec le retour automatique) ; après une mise en page.
    std::size_t rowCount(const Ui &ui) const;
    std::string text() const;
    const TextEditState &edit() const noexcept { return edit_; }
    float scroll() const noexcept { return scroll_; }

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    // Modifiable : ni en lecture seule, ni liée à une valeur sans setter (la modification disparaîtrait à la perte du focus).
    bool editable() const { return !readOnly_ && value_.writable(); }
    bool acceptsText() const override { return editable(); }
    bool handlesEditCommands() const override { return true; } // sélection et copie, même en lecture seule
    bool wantsTab() const override { return editable(); }
    bool clipsChildren() const override { return true; }
    void onPress(Ui &ui, glm::vec2 mouse) override;
    void onDrag(Ui &ui, glm::vec2 mouse) override;
    void onKey(Ui &ui, int key) override;
    void onText(Ui &ui, std::string_view text) override;
    bool onWheel(Ui &ui, glm::vec2 wheel) override;
    void onFocusChanged(Ui &ui, bool focused) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void arrange(Ui &ui, const Rect &content, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    void syncFromBinding(); // tampon <- valeur liée ; un texte différent efface l'historique et ramène le curseur sur un caractère
    // Rangée affichée : une ligne du texte, ou un morceau d'une ligne coupée par le retour automatique.
    struct Line
    {
        std::size_t begin;
        std::size_t end;    // sans le '\n'
        bool first = true;  // début d'une ligne du texte (numéro affiché)
        bool last = true;   // fin d'une ligne du texte
        std::size_t number = 0; // numéro de la ligne du texte (0 : la première)
    };
    std::vector<Line> lines(const Ui &ui, const Style &style) const;
    static std::size_t lineOf(const std::vector<Line> &lines, std::size_t position);
    std::size_t positionAt(const Ui &ui, glm::vec2 mouse) const;
    float gutter(const Ui &ui, const Style &style) const;
    void changed(Ui &ui);
    void revealCaret(const Ui &ui);

    Binding<std::string> value_;
    TextEditState edit_;
    bool lineNumbers_ = false;
    bool readOnly_ = false;
    bool wrap_ = true;
    bool syncing_ = true; // le tampon suit la valeur liée tant que le champ n'a pas le focus
    float scroll_ = 0.0f;
    float desiredX_ = -1.0f; // abscisse gardée pendant les déplacements haut / bas
};

} // namespace engine::ui

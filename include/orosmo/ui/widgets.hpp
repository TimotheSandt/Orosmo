// UI-03 : widgets de base. Chacun lit son apparence dans le thème (type de style entre parenthèses), signale ses changements par
// `onChange` et ses clics par `onClick`, et lit / écrit sa valeur par une `Binding` (écriture différée, voir widget.hpp).
//
//   Label (label)            texte fixe ou lu à chaque image
//   Button (button)          clic à la souris, ou Entrée / Espace avec le focus
//   Checkbox (checkbox)      booléen
//   Slider (slider)          nombre borné, glisser ou flèches ; entier si `step` >= 1
//   Dropdown (dropdown)      choix dans une liste (fenêtre surgissante : popup, item)
//   TextField (textField)    texte modifiable ; validé par Entrée ou la perte du focus, Échap annule
//   NumberField (textField)  nombre saisi au clavier (refus d'une saisie invalide)
//   ColorPicker (colorPicker) couleur : aperçu et curseurs R, G, B (A) ; cliquer sur l'aperçu ouvre une fenêtre surgissante avec
//                             l'éditeur complet (ColorEditor : roue, RGB/HSV/HSL, hexadécimal) lié à la même couleur ; sans effet
//                             si la couleur n'est pas modifiable
//   Plot (plot)              graphique simple : barres d'une série de valeurs
//   Tabs (tabs, tab)         onglets : une page visible à la fois
//   Section (section)        bloc repliable avec un titre
//   Separator (separator)    trait
//   ScrollView (scroll)      défilement : voir layout.hpp ; infobulles : Widget::setTooltip (tooltip)
#pragma once

#include "orosmo/ui/layout.hpp"
#include "orosmo/ui/widget.hpp"

#include <functional>
#include <string>
#include <vector>

namespace orosmo::ui
{

class Label : public Widget
{
  public:
    explicit Label(std::string text = {}, std::string styleName = "label");
    Label &setText(std::string text);
    // Texte relu à chaque image (interpolation du langage d'interface).
    Label &bindText(std::function<std::string()> text);
    std::string text() const;
    Label &setTextAlign(Align align) { textAlign_ = align; return *this; }

  protected:
    Rect contentExtent(Ui &ui, const Style &style) const override;
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    std::string text_;
    std::function<std::string()> source_;
    Align textAlign_ = Align::kStart;
};

class Button : public Widget
{
  public:
    explicit Button(std::string text = {}, std::string styleName = "button");
    Button &setText(std::string text) { text_ = std::move(text); source_ = nullptr; return *this; } // remplace un texte lié
    Button &bindText(std::function<std::string()> text) { source_ = std::move(text); return *this; }
    std::string text() const { return source_ ? source_() : text_; }
    // Raccourci clavier qui clique le bouton (« Ctrl+R », « F5 » ; normalizeShortcut).
    Button &setShortcut(std::string shortcut);
    const std::string &shortcut() const noexcept { return shortcut_; }

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onRelease(Ui &ui, glm::vec2 mouse, bool inside) override;
    void onKey(Ui &ui, int key) override;
    bool onShortcut(Ui &ui, std::string_view shortcut) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    std::string text_;
    std::function<std::string()> source_;
    std::string shortcut_; // normalisé
};

class Checkbox : public Widget
{
  public:
    Checkbox(std::string text, Binding<bool> value);
    bool checked() const { return value_.value(); }

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onRelease(Ui &ui, glm::vec2 mouse, bool inside) override;
    void onKey(Ui &ui, int key) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawBackground(const Ui &, renderer::DrawList &, const Style &) const override {} // la case seule a le fond du style
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    void toggle(Ui &ui);
    std::string text_;
    Binding<bool> value_;
    bool pending_ = false;    // valeur programmée, pas encore écrite (fin de l'image)
    bool hasPending_ = false;
};

class Slider : public Widget
{
  public:
    // `step` > 0 : valeurs arrondies au pas (entières si le pas est entier).
    //
    // Souris : un clic SANS glisser place la valeur sous le pointeur (au relâchement) ; un glissement part de la valeur COURANTE
    // et la déplace de la distance parcourue (le point de départ du clic n'est pas imposé : réglage précis) ; Maj pendant le
    // glissement : dix fois plus fin. Clic droit : valeur par défaut (celle de la liaison à la construction, ou `setDefault`).
    Slider(Binding<double> value, double min, double max, double step = 0.0);
    Slider &setDefault(double value) { default_ = value; return *this; }
    double defaultValue() const noexcept { return default_; }
    double value() const;
    double min() const noexcept { return min_; }
    double max() const noexcept { return max_; }
    // Texte affiché ("12", "0.350") : décimales selon le pas, précédé du préfixe.
    std::string formatted() const;
    Slider &setPrefix(std::string prefix) { prefix_ = std::move(prefix); return *this; }

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onPress(Ui &ui, glm::vec2 mouse) override;
    void onDrag(Ui &ui, glm::vec2 mouse) override;
    void onRelease(Ui &ui, glm::vec2 mouse, bool inside) override;
    bool onSecondaryClick(Ui &ui, glm::vec2 mouse) override;
    void onKey(Ui &ui, int key) override;
    bool usesArrowKey(int key, bool fromGamepad) const override;
    // Distance (pixels) au-delà de laquelle un appui devient un glissement.
    static constexpr float kDragThreshold = 3.0f;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    void setFromMouse(Ui &ui, float x);
    void write(Ui &ui, double v);
    double quantize(double v) const;

    Binding<double> value_;
    double min_;
    double max_;
    double step_;
    std::string prefix_;
    double default_ = 0.0;
    bool dragging_ = false;
    float pressX_ = 0.0f;   // abscisse de l'appui
    float lastX_ = 0.0f;    // abscisse de la dernière position prise en compte
    double dragValue_ = 0.0; // valeur non arrondie pendant le glissement (le pas ne bloque pas les petits déplacements)
    bool hasPending_ = false;
    double pending_ = 0.0; // valeur écrite pendant l'image (affichée avant que l'écriture différée soit appliquée)
};

class Dropdown : public Widget
{
  public:
    Dropdown(std::function<std::vector<std::string>()> options, Binding<int> selected);
    Dropdown(std::vector<std::string> options, Binding<int> selected);
    int selected() const { return selected_.value(); }
    std::vector<std::string> options() const { return options_ ? options_() : std::vector<std::string>{}; }

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onRelease(Ui &ui, glm::vec2 mouse, bool inside) override;
    void onKey(Ui &ui, int key) override;
    bool usesArrowKey(int key, bool fromGamepad) const override;
    // Choix d'une option (clic dans la liste) : écrit la valeur, ferme la liste, émet onChange.
    void choose(Ui &ui, int index);

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    void open(Ui &ui);
    std::function<std::vector<std::string>()> options_;
    Binding<int> selected_;
};

// Édition de texte partagée par les champs : tampon, curseur (octet), ancre de sélection.
struct TextEditState
{
    std::string text;
    std::size_t caret = 0;
    std::size_t anchor = 0; // sélection = [min(caret, anchor), max(...)) ; vide si égaux

    bool hasSelection() const noexcept { return caret != anchor; }
    std::size_t selectionStart() const noexcept { return std::min(caret, anchor); }
    std::size_t selectionEnd() const noexcept { return std::max(caret, anchor); }
    std::string selected() const { return text.substr(selectionStart(), selectionEnd() - selectionStart()); }
    void eraseSelection();
    void insert(std::string_view s); // remplace la sélection
    void moveTo(std::size_t position, bool extend);
    void selectAll();

    // ── Annuler / rétablir ──
    // Appelé AVANT une modification : garde l'état actuel. Les frappes successives forment un seul pas (jusqu'à une espace, un
    // retour à la ligne ou un déplacement du curseur), de même les effacements successifs.
    enum class EditKind : std::uint8_t
    {
        kOther,
        kTyping,
        kDeleting,
    };
    void beginEdit(EditKind kind, std::string_view inserted = {});
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    void resetHistory() noexcept;
    static constexpr std::size_t kMaxHistory = 200;

  private:
    struct Snapshot
    {
        std::string text;
        std::size_t caret;
        std::size_t anchor;
    };
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    EditKind lastKind_ = EditKind::kOther;
    bool groupOpen_ = false;
};

class TextField : public Widget
{
  public:
    explicit TextField(Binding<std::string> value, std::string styleName = "textField");
    // Texte affiché : la saisie en cours avec le focus, sinon la valeur liée.
    std::string text() const;
    std::size_t caret() const noexcept { return edit_.caret; }
    const TextEditState &edit() const noexcept { return edit_; }
    bool editing() const noexcept { return editing_; }

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    bool acceptsText() const override { return true; }
    void onPress(Ui &ui, glm::vec2 mouse) override;
    void onDrag(Ui &ui, glm::vec2 mouse) override;
    void onKey(Ui &ui, int key) override;
    bool usesArrowKey(int key, bool fromGamepad) const override;
    void onText(Ui &ui, std::string_view text) override;
    void onFocusChanged(Ui &ui, bool focused) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;
    std::size_t positionAt(const Ui &ui, glm::vec2 mouse) const;
    // Valide la saisie : faux pour la refuser (la valeur liée est alors gardée).
    virtual bool commit(Ui &ui, const std::string &text);
    virtual std::string current() const;

  private:
    Binding<std::string> value_;
    TextEditState edit_;
    bool editing_ = false;
};

class NumberField : public TextField
{
  public:
    NumberField(Binding<double> value, bool integer, double min = -kUnboundedNumber, double max = kUnboundedNumber);
    static constexpr double kUnboundedNumber = 1.0e300;
    // Affichage : écriture la plus courte qui relit la même valeur en float (défaut, paramètres float) ou en double.
    NumberField &setDoublePrecision(bool on) noexcept { doublePrecision_ = on; return *this; }

  protected:
    bool commit(Ui &ui, const std::string &text) override;
    std::string current() const override;

  private:
    Binding<double> number_;
    bool integer_;
    bool doublePrecision_ = false;
    double min_;
    double max_;
};

class ColorPicker : public Widget
{
  public:
    ColorPicker(Binding<Color> value, bool alpha = false);
    Color color() const { return value_.value(); }
    void clearChildren() override {} // curseurs internes : pas de contenu effaçable
    bool interactive() const override { return true; } // le carré d'aperçu reçoit le clic (les curseurs sont des enfants)
    bool focusable() const override { return true; }   // Entrée et Espace ouvrent l'éditeur
    void onRelease(Ui &ui, glm::vec2 mouse, bool inside) override;
    void onKey(Ui &ui, int key) override;
    const Rect &swatchRect() const noexcept { return swatch_; }

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void arrange(Ui &ui, const Rect &content, const Style &style) override;
    void drawBackground(const Ui &ui, renderer::DrawList &list, const Style &style) const override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    void openEditor(Ui &ui);
    Binding<Color> value_;
    bool alpha_;
    Rect swatch_; // carré d'aperçu, posé par arrange
};

class Plot : public Widget
{
  public:
    // Valeurs lues à chaque image ; bornes automatiques (min et max de la série, 0 compris) sauf `setRange`.
    explicit Plot(std::function<std::vector<float>()> values);
    Plot &setRange(float min, float max) { min_ = min; max_ = max; fixedRange_ = true; return *this; }

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    std::function<std::vector<float>()> values_;
    float min_ = 0.0f;
    float max_ = 1.0f;
    bool fixedRange_ = false;
};

class Tabs : public VBox
{
  public:
    Tabs();
    explicit Tabs(Binding<int> selected);
    // Ajoute une page et son onglet ; renvoie la page.
    Widget &addPage(std::string title, std::unique_ptr<Widget> page);
    int selected() const;
    void select(Ui &ui, int index);
    std::size_t pageCount() const noexcept { return pages_.size(); }
    void clearChildren() override; // retire les pages et leurs onglets ; l'en-tête reste

  private:
    HBox *header_;
    std::vector<Widget *> pages_;
    Binding<int> selected_;
    int local_ = 0;
};

class Section : public VBox
{
  public:
    explicit Section(std::string title, bool expanded = true);
    // Contenu du bloc (masqué quand le bloc est replié).
    VBox &body() noexcept { return *body_; }
    bool expanded() const noexcept { return expanded_; }
    void setExpanded(bool expanded) noexcept { expanded_ = expanded; }
    void clearChildren() override { body_->clearChildren(); } // vide le contenu ; l'en-tête et le corps restent

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;

  private:
    VBox *body_;
    bool expanded_;
};

class Separator : public Widget
{
  public:
    Separator() : Widget("separator") {}

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;
};

} // namespace orosmo::ui

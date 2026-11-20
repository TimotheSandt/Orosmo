// UI-03 / UI-06 : base des widgets. Un widget a un type de style (clé du thème : "button", "label"...), une taille demandée par axe
// (ajustée au contenu, fixe, ou remplissage pondéré, avec minimum et maximum), une marge extérieure, un alignement dans la place que
// lui donne son parent, et des événements nommés (clic, changement, survol, focus). La marge intérieure, l'espacement, les couleurs
// et les tailles de police viennent du THÈME (aucune valeur en dur) ; une surcharge locale (`overrideStyle`) nomme un jeton.
//
// Mise en page en deux passes : `measure` (du bas vers le haut : taille souhaitée) puis `layout` (du haut vers le bas : rectangle
// final). Coordonnées en pixels logiques, origine en haut à gauche (comme `DrawList`).
#pragma once

#include "engine/renderer/draw_list.hpp"
#include "engine/ui/theme.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine::ui
{

class Ui;
class Widget;

struct Rect
{
    glm::vec2 position{0.0f};
    glm::vec2 size{0.0f};

    bool contains(glm::vec2 p) const noexcept
    {
        return p.x >= position.x && p.y >= position.y && p.x < position.x + size.x && p.y < position.y + size.y;
    }
    glm::vec2 end() const noexcept { return position + size; }
    bool operator==(const Rect &) const = default;
};

struct Insets
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    static Insets all(float v) noexcept { return {v, v, v, v}; }
    static Insets symmetric(float horizontal, float vertical) noexcept { return {horizontal, vertical, horizontal, vertical}; }
    float horizontal() const noexcept { return left + right; }
    float vertical() const noexcept { return top + bottom; }
    glm::vec2 total() const noexcept { return {horizontal(), vertical()}; }
    bool operator==(const Insets &) const = default;
};

inline constexpr float kUnbounded = std::numeric_limits<float>::infinity();

enum class SizeMode : std::uint8_t
{
    kFit,   // taille du contenu
    kFixed, // `value` pixels
    kFill,  // la place restante du parent, partagée selon `value` (poids)
};

struct SizeSpec
{
    SizeMode mode = SizeMode::kFit;
    float value = 0.0f;
    float min = 0.0f;
    float max = kUnbounded;

    static SizeSpec fit() noexcept { return {}; }
    static SizeSpec fixed(float pixels) noexcept { return {SizeMode::kFixed, pixels, 0.0f, kUnbounded}; }
    static SizeSpec fill(float weight = 1.0f) noexcept { return {SizeMode::kFill, weight, 0.0f, kUnbounded}; }
    float clamp(float v) const noexcept { return v < min ? min : (v > max ? max : v); }
};

// Alignement dans la place donnée par le parent, sur l'axe secondaire d'une boîte (et sur les deux axes d'une superposition).
enum class Align : std::uint8_t
{
    kStart,
    kCenter,
    kEnd,
    kStretch,
};

// Ce qui dépasse du widget (enfants plus grands que lui, ou placés en dehors) : visible et cliquable (par défaut), ou coupé à son
// rectangle. Pour faire défiler le contenu : ScrollView.
enum class Overflow : std::uint8_t
{
    kVisible,
    kHidden,
};

enum class EventKind : std::uint8_t
{
    kClick,
    kChange,
    kHover, // `active` : entrée (vrai) ou sortie (faux)
    kFocus, // `active` : focus pris (vrai) ou perdu (faux)
};

struct Event
{
    EventKind kind = EventKind::kClick;
    Widget *source = nullptr;
    bool active = true;
};

using Handler = std::function<void(const Event &)>;

// Appelle `f(event)` si `f` accepte un `const Event &`, sinon `f()` : le code généré par le langage d'interface lie ainsi une
// fonction C++ quelle que soit sa signature (avec ou sans l'événement).
template <typename F> Handler handler(F f)
{
    return [f = std::move(f)](const Event &event) mutable
    {
        if constexpr (std::is_invocable_v<F &, const Event &>)
            (void)f(event);
        else
            (void)f();
    };
}

// Liaison typée dans les deux sens (docs/03 §12) : le widget LIT par `get` à chaque image et ÉCRIT par `set`. L'écriture est différée
// (`Ui::defer`) : elle est appliquée à la fin de `Ui::update`, visible à la lecture suivante (sémantique de DeferredValue<T>).
// Sans `set`, la valeur est en lecture seule (le widget l'affiche sans permettre de la changer).
template <typename T> struct Binding
{
    std::function<T()> get;
    std::function<void(const T &)> set;

    static Binding ref(T &value)
    {
        return {[p = &value] { return *p; }, [p = &value](const T &v) { *p = v; }};
    }
    static Binding constant(T value)
    {
        return {[v = std::move(value)] { return v; }, {}};
    }
    bool writable() const noexcept { return static_cast<bool>(set); }
    T value() const { return get ? get() : T{}; }
};

// Liaison numérique (curseur, champ numérique) vers un entier ou un flottant de n'importe quel type : lecture en double, écriture
// arrondie au plus proche pour un entier (et bornée à son type).
template <typename T> Binding<double> numberBinding(T &value)
{
    static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>);
    return {[p = &value] { return static_cast<double>(*p); },
            [p = &value](const double &v)
            {
                if constexpr (std::is_integral_v<T>)
                {
                    const double r = v < 0.0 ? -std::floor(-v + 0.5) : std::floor(v + 0.5);
                    const double lo = static_cast<double>(std::numeric_limits<T>::lowest());
                    const double hi = static_cast<double>(std::numeric_limits<T>::max());
                    *p = static_cast<T>(r < lo ? lo : (r > hi ? hi : r));
                }
                else
                    *p = static_cast<T>(v);
            }};
}

// Liaison entière (liste déroulante, onglets) vers un entier ou une énumération.
template <typename T> Binding<int> intBinding(T &value)
{
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    return {[p = &value] { return static_cast<int>(*p); }, [p = &value](const int &v) { *p = static_cast<T>(v); }};
}

// Liaison de couleur vers un glm::vec4 (RGBA) ou un glm::vec3 (RGB, alpha 1).
template <typename T> Binding<glm::vec4> colorBinding(T &value)
{
    if constexpr (std::is_same_v<T, glm::vec3>)
        return {[p = &value] { return glm::vec4(*p, 1.0f); }, [p = &value](const glm::vec4 &c) { *p = glm::vec3(c); }};
    else
        return Binding<glm::vec4>::ref(value);
}

// Texte d'un libellé lié (interpolation du langage d'interface) : les morceaux mis bout à bout ; un nombre flottant a au plus deux
// décimales (« 59.87 », « 120 »), un booléen s'écrit true / false.
namespace detail
{
std::string textPiece(double value);
inline std::string textPiece(const std::string &s) { return s; }
inline std::string textPiece(std::string_view s) { return std::string(s); }
inline std::string textPiece(const char *s) { return s != nullptr ? std::string(s) : std::string(); }
inline std::string textPiece(bool b) { return b ? "true" : "false"; }
template <typename T>
    requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
std::string textPiece(T v)
{
    return std::to_string(v);
}
template <typename T>
    requires std::is_floating_point_v<T>
std::string textPiece(T v)
{
    return textPiece(static_cast<double>(v));
}
} // namespace detail
template <typename... Args> std::string text(const Args &...args)
{
    std::string out;
    ((out += detail::textPiece(args)), ...);
    return out;
}

class Widget
{
  public:
    explicit Widget(std::string styleName);
    virtual ~Widget();
    Widget(const Widget &) = delete;
    Widget &operator=(const Widget &) = delete;

    // ── Arbre ──
    template <typename T> T &add(std::unique_ptr<T> child)
    {
        T &ref = *child;
        adopt(std::move(child));
        return ref;
    }
    template <typename T, typename... Args> T &emplace(Args &&...args) { return add(std::make_unique<T>(std::forward<Args>(args)...)); }
    void clearChildren();
    std::span<const std::unique_ptr<Widget>> children() const noexcept { return children_; }
    Widget *parent() const noexcept { return parent_; }
    // Recherche en profondeur d'un widget par identifiant (lui-même compris).
    Widget *find(std::string_view id) noexcept;
    template <typename T> T *findAs(std::string_view id) noexcept { return dynamic_cast<T *>(find(id)); }

    // ── Identification et style ──
    Widget &setId(std::string id);
    const std::string &id() const noexcept { return id_; }
    const std::string &styleName() const noexcept { return styleName_; }
    Widget &setStyleName(std::string name);
    // Surcharge locale d'une propriété du thème (UI-07) : nom de jeton, couleur "#..." ou nombre. Résolue à chaque image avec le thème
    // courant ; une valeur qui ne se résout pas (jeton absent) est ignorée et signalée une fois dans le journal.
    Widget &overrideStyle(StyleProperty property, std::string value);
    // Style résolu dans l'état courant, surcharges comprises.
    Style style(const Theme &theme) const;

    // ── Mise en page (UI-06) ──
    SizeSpec width;
    SizeSpec height;
    Insets margin;
    Align alignX = Align::kStretch;
    Align alignY = Align::kStretch;
    int columnSpan = 1; // nombre de colonnes occupées dans une grille
    Overflow overflow = Overflow::kVisible;

    Widget &setWidth(SizeSpec s) { width = s; return *this; }
    Widget &setHeight(SizeSpec s) { height = s; return *this; }
    Widget &setMargin(Insets m) { margin = m; return *this; }
    Widget &setAlign(Align x, Align y) { alignX = x; alignY = y; return *this; }

    // ── État ──
    Widget &setEnabled(bool enabled) { enabled_ = enabled; return *this; }
    Widget &setVisible(bool visible) { visible_ = visible; return *this; }
    // Visible / actif selon une expression lue à chaque image (en plus de setVisible / setEnabled).
    Widget &bindVisible(std::function<bool()> f) { visibleIf_ = std::move(f); return *this; }
    Widget &bindEnabled(std::function<bool()> f) { enabledIf_ = std::move(f); return *this; }
    bool visible() const;
    // Actif : lui et tous ses parents.
    bool enabled() const;
    Widget &setTooltip(std::string text) { tooltip_ = std::move(text); return *this; }
    const std::string &tooltip() const noexcept { return tooltip_; }
    WidgetState state() const;
    bool hovered() const noexcept { return hovered_; }
    bool pressed() const noexcept { return pressed_; }
    bool focused() const noexcept { return focused_; }

    // ── Événements ──
    Widget &onClick(Handler h) { handlers_[0] = std::move(h); return *this; }
    Widget &onChange(Handler h) { handlers_[1] = std::move(h); return *this; }
    Widget &onHover(Handler h) { handlers_[2] = std::move(h); return *this; }
    Widget &onFocus(Handler h) { handlers_[3] = std::move(h); return *this; }

    // ── Résultat de la mise en page ──
    const Rect &rect() const noexcept { return rect_; }
    glm::vec2 measured() const noexcept { return measured_; }

    // Passe 1 : taille souhaitée (sans la marge extérieure) quand le parent offre `available` (kUnbounded : sans limite).
    glm::vec2 measure(Ui &ui, glm::vec2 available);
    // Passe 2 : place le widget dans `rect` (sans la marge extérieure) et ses enfants.
    void layout(Ui &ui, const Rect &rect);
    void draw(const Ui &ui, renderer::DrawList &list) const;

    // ── Comportement (redéfini par les widgets) ──
    virtual bool interactive() const { return false; } // reçoit les clics (le survol, l'état appuyé)
    virtual bool focusable() const { return false; }   // prend le focus clavier (Tab, clic)
    // Un clic donne le focus au widget (faux pour les menus : ils ne le prennent qu'au clavier, et le focus courant reste).
    virtual bool focusOnClick() const { return true; }
    virtual bool clipsChildren() const { return false; }
    virtual bool acceptsText() const { return false; }  // reçoit la saisie de texte (l'application ne doit pas lire les touches)
    virtual void onPress(Ui &ui, glm::vec2 mouse);
    virtual void onDrag(Ui &ui, glm::vec2 mouse);
    virtual void onRelease(Ui &ui, glm::vec2 mouse, bool inside);
    // Clic du bouton secondaire (droit) sur le widget ; vrai s'il l'a utilisé (curseur : valeur par défaut). Sinon, l'interface
    // cherche un menu contextuel (ContextMenu) parmi les enfants du widget et de ses parents.
    virtual bool onSecondaryClick(Ui &ui, glm::vec2 mouse);
    // Ouvre le menu contextuel que ce widget représente (ContextMenu) ; faux pour les autres widgets.
    virtual bool openContextMenu(Ui &ui, glm::vec2 mouse);
    // Le pointeur entre sur le widget ou en sort (après l'état « survolé », avant l'événement onHover).
    virtual void onHoverChanged(Ui &ui, bool hovered);
    // Clic hors de la fenêtre modale que ce widget possède (Dialog).
    virtual void onOutsideClick(Ui &ui);
    // Sous-arbre possédé par le widget mais hors de ses enfants (corps d'une boîte de dialogue, éléments d'un menu) : parcouru par
    // `find` et par le suivi des pointeurs, dessiné par l'interface à part (fenêtre modale, menu).
    virtual Widget *detached() const noexcept { return nullptr; }
    // Le widget garde la touche Tab pour lui (éditeur de texte) au lieu de passer le focus.
    virtual bool wantsTab() const { return false; }
    virtual bool onWheel(Ui &ui, glm::vec2 wheel);
    virtual void onKey(Ui &ui, int key);                // engine::ui::Key
    virtual void onText(Ui &ui, std::string_view text); // UTF-8 saisi
    virtual void onFocusChanged(Ui &ui, bool focused);
    // Raccourci clavier normalisé (`normalizeShortcut` : "Ctrl+S", "Ctrl+Shift+Z", "F5") ; vrai si le widget l'a pris (élément
    // de menu, bouton). L'interface le propose aux widgets visibles et actifs, dans l'ordre de l'arbre.
    virtual bool onShortcut(Ui &ui, std::string_view shortcut);

    // Programme l'événement : il est appelé à la fin de `Ui::update`, après les écritures différées qui le précèdent.
    void emit(Ui &ui, EventKind kind, bool active = true);
    // Appelle le gestionnaire tout de suite (relais d'un événement d'un enfant, déjà différé).
    void fire(EventKind kind, bool active = true);

    // Fond, bordure et ombre du style sur `r` (rien si le fond et la bordure sont transparents).
    static void drawBox(renderer::DrawList &list, const Rect &r, const Style &style);

  protected:
    // Taille du contenu (sans la marge intérieure du style) quand `available` est offert au contenu.
    virtual glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style);
    // Place les enfants dans `content` (rectangle du widget moins la marge intérieure). Par défaut : superposés.
    virtual void arrange(Ui &ui, const Rect &content, const Style &style);
    // Fond du widget (par défaut : `drawBox` sur tout son rectangle). Un widget dont le fond du style ne couvre qu'une partie (case à
    // cocher, aperçu de couleur) la redéfinit.
    virtual void drawBackground(const Ui &ui, renderer::DrawList &list, const Style &style) const;
    // Dessine le widget (le fond et la bordure sont déjà dessinés), puis `drawChildren`.
    virtual void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const;
    void drawChildren(const Ui &ui, renderer::DrawList &list) const;
    // Ligne de texte centrée verticalement dans `r`, alignée à gauche, au centre ou à droite.
    static void drawText(const Ui &ui, renderer::DrawList &list, std::string_view text, const Rect &r, const Style &style, Align align,
                         const Color *color = nullptr);
    static Rect inset(const Rect &r, float padding) noexcept;

    Rect rect_;
    glm::vec2 measured_{0.0f};

  private:
    friend class Ui;
    void adopt(std::unique_ptr<Widget> child);

    std::string styleName_;
    std::string id_;
    std::string tooltip_;
    std::vector<std::unique_ptr<Widget>> children_;
    Widget *parent_ = nullptr;
    std::vector<std::pair<StyleProperty, std::string>> overrides_;
    std::function<bool()> visibleIf_;
    std::function<bool()> enabledIf_;
    std::array<Handler, 4> handlers_;
    bool enabled_ = true;
    bool visible_ = true;
    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;
    mutable bool overrideWarned_ = false;
};

} // namespace engine::ui

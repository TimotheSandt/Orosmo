// UI-01 / UI-03 : contexte d'une interface : racine de l'arbre, thème courant (rechargé à chaud), police, registre d'actions
// nommées, entrées de l'image, focus, fenêtres surgissantes (liste déroulante) et infobulles. Aucun singleton : l'application
// possède son `Ui` et le passe au code généré par le langage d'interface.
//
// Une image : `update(input, viewport)` (rechargement du thème, entrées sur la mise en page affichée, écritures différées et
// événements, nouvelle mise en page), puis `draw(list)` (liste de dessin pour `CanvasRenderer`).
#pragma once

#include "engine/ui/font.hpp"
#include "engine/ui/theme.hpp"
#include "engine/ui/widget.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui
{

// Touches de l'interface (indépendantes de la plateforme ; l'application traduit les siennes).
enum Key : int
{
    kKeyNone = 0,
    kKeyBackspace,
    kKeyDelete,
    kKeyEnter,
    kKeyEscape,
    kKeyTab,
    kKeyLeft,
    kKeyRight,
    kKeyUp,
    kKeyDown,
    kKeyHome,
    kKeyEnd,
    kKeySpace,
};

// Entrées d'une image, en pixels logiques de l'interface.
struct InputFrame
{
    glm::vec2 mouse{-1.0e9f};
    bool mouseDown = false;     // bouton principal enfoncé (l'appui et le relâchement sont déduits de l'image précédente)
    glm::vec2 wheel{0.0f};      // molette (y > 0 : vers le haut)
    std::string text;           // texte saisi pendant l'image (UTF-8)
    std::vector<int> keys;      // touches appuyées pendant l'image (Key)
    bool shift = false;
    float deltaTime = 1.0f / 60.0f;
};

// Actions appelées par leur nom (docs/03 §12, mode développement) : un événement du langage d'interface qui nomme une fonction
// libre (`.onClick(regenerate)`) la retrouve ici à l'appel. Une action absente est signalée une fois dans le journal.
class ActionRegistry
{
  public:
    void add(std::string name, Handler action);
    void remove(std::string_view name);
    bool contains(std::string_view name) const;
    // Appelle l'action ; faux si elle n'est pas enregistrée.
    bool invoke(std::string_view name, const Event &event);
    // Gestionnaire qui appelle l'action `name` au moment de l'événement (liaison tardive : l'action peut être enregistrée après).
    Handler bind(std::string name);
    // Noms demandés par `bind` qui ne sont pas enregistrés (à vérifier au démarrage).
    std::vector<std::string> missing() const;

  private:
    std::map<std::string, Handler, std::less<>> actions_;
    std::set<std::string, std::less<>> requested_;
    std::set<std::string, std::less<>> warned_;
};

class Ui
{
  public:
    explicit Ui(const Font &font);
    ~Ui();
    Ui(const Ui &) = delete;
    Ui &operator=(const Ui &) = delete;

    const Font &font() const noexcept { return *font_; }
    ThemeHost &themes() noexcept { return themes_; }
    const Theme &theme() const noexcept { return themes_.current(); }
    ActionRegistry &actions() noexcept { return actions_; }

    // Remplace l'arbre. Pendant `update` (depuis un événement), le remplacement est appliqué à la fin de l'image.
    void setRoot(std::unique_ptr<Widget> root);
    Widget *root() const noexcept { return root_.get(); }
    Widget *find(std::string_view id) const noexcept;

    void update(const InputFrame &input, glm::vec2 viewport);
    void draw(renderer::DrawList &list) const;
    // Mise en page seule (sans entrées) : tests, première image.
    void layout(glm::vec2 viewport);

    // La souris est sur un élément de l'interface (l'application ne doit pas l'utiliser pour la caméra), ou un glissement a commencé
    // sur l'interface.
    bool wantsMouse() const noexcept { return wantsMouse_; }
    // Un widget qui reçoit le texte a le focus (l'application ne doit pas interpréter les touches).
    bool wantsKeyboard() const noexcept;

    Widget *hovered() const noexcept { return hovered_; }
    Widget *focused() const noexcept { return focused_; }
    void focus(Widget *widget);
    // Ordre de tabulation : widgets visibles, actifs et `focusable`, dans l'ordre de l'arbre.
    void focusNext(bool backwards = false);

    // Écriture différée (liaison) ou événement : appelé à la fin de `update`, dans l'ordre.
    void defer(std::function<void()> action);

    // Fenêtre surgissante (liste d'une liste déroulante) : dessinée au-dessus de l'arbre, sous `anchor` (ou au-dessus s'il n'y a pas
    // la place), au moins aussi large que lui. Un clic en dehors la ferme. Une seule à la fois.
    void openPopup(Widget &owner, std::unique_ptr<Widget> content, const Rect &anchor);
    void closePopup();
    bool popupOpenFor(const Widget &owner) const noexcept { return popupOwner_ == &owner; }
    Widget *popup() const noexcept { return popup_.get(); }

    // Délai avant l'affichage d'une infobulle (secondes).
    void setTooltipDelay(float seconds) noexcept { tooltipDelay_ = seconds; }
    // Texte de l'infobulle affichée (vide : aucune).
    const std::string &visibleTooltip() const noexcept { return tooltipText_; }

    // Un widget va être détruit (retiré de l'arbre) : l'interface oublie les pointeurs qui le visent.
    void forget(const Widget &widget) noexcept;

    glm::vec2 viewport() const noexcept { return viewport_; }
    glm::vec2 mouse() const noexcept { return mouse_; }
    float time() const noexcept { return time_; }

  private:
    Widget *hitTest(glm::vec2 p) const;
    static Widget *hitTestIn(Widget &w, glm::vec2 p);
    void setHovered(Widget *target);
    void flushDeferred();
    void collectFocusable(Widget &w, std::vector<Widget *> &out) const;
    void layoutPopup();

    const Font *font_;
    ThemeHost themes_;
    ActionRegistry actions_;
    std::unique_ptr<Widget> root_;
    std::unique_ptr<Widget> pendingRoot_;
    bool updating_ = false;
    std::vector<std::function<void()>> deferred_;

    Widget *hovered_ = nullptr;
    Widget *active_ = nullptr;
    Widget *focused_ = nullptr;
    std::unique_ptr<Widget> popup_;
    Widget *popupOwner_ = nullptr;
    Rect popupAnchor_;

    glm::vec2 viewport_{0.0f};
    glm::vec2 mouse_{-1.0e9f};
    bool mouseWasDown_ = false;
    bool wantsMouse_ = false;
    float time_ = 0.0f;
    float hoverTime_ = 0.0f;
    float tooltipDelay_ = 0.6f;
    std::string tooltipText_;
    std::string tooltipCandidate_;
    glm::vec2 tooltipAt_{0.0f};
};

} // namespace engine::ui

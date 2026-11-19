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

#include <deque>
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
    kKeyPageUp,
    kKeyPageDown,
    kKeySelectAll, // Ctrl+A : l'application traduit les raccourcis
    kKeyCopy,      // Ctrl+C
    kKeyCut,       // Ctrl+X
    kKeyPaste,     // Ctrl+V
};

// Où s'ouvre une fenêtre surgissante par rapport à son ancre.
enum class PopupPlacement : std::uint8_t
{
    kBelow,   // sous l'ancre (au-dessus s'il n'y a pas la place), au moins aussi large (liste déroulante, menu)
    kRight,   // à droite de l'ancre (sous-menu)
    kAtPoint, // au coin haut gauche de l'ancre (menu contextuel au pointeur)
};

enum class NotificationKind : std::uint8_t
{
    kInfo,
    kSuccess,
    kWarning,
    kError,
};

// Notification temporaire (Ui::notify), affichée par un widget `Notifications`.
struct Notification
{
    std::uint64_t id = 0;
    std::string text;
    NotificationKind kind = NotificationKind::kInfo;
    float duration = 4.0f;  // secondes
    float remaining = 4.0f; // secondes avant disparition
};

// Entrées d'une image, en pixels logiques de l'interface.
struct InputFrame
{
    glm::vec2 mouse{-1.0e9f};
    bool mouseDown = false;     // bouton principal enfoncé (l'appui et le relâchement sont déduits de l'image précédente)
    bool secondaryDown = false; // bouton secondaire (droit) enfoncé
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

    // Fenêtres surgissantes (liste déroulante, menus, sous-menus) : dessinées au-dessus de tout, en pile. Ouvrir depuis un widget
    // situé DANS une fenêtre ouverte (sous-menu) ferme celles au-dessus de lui ; depuis ailleurs, ferme toutes les autres. Un clic
    // hors de toutes les ferme. `content` est possédée par l'interface ; la version par référence affiche un sous-arbre que
    // `owner` possède (menus déclarés dans la vue).
    void openPopup(Widget &owner, std::unique_ptr<Widget> content, const Rect &anchor, PopupPlacement placement = PopupPlacement::kBelow);
    void openPopup(Widget &owner, Widget &content, const Rect &anchor, PopupPlacement placement = PopupPlacement::kBelow);
    void closePopup(); // toutes
    bool popupOpenFor(const Widget &owner) const noexcept;
    bool anyPopupOpen() const noexcept { return !popups_.empty(); }
    // Fenêtre du dessus (nulle : aucune).
    Widget *popup() const noexcept;

    // Fenêtre modale (Dialog) : appelé pendant la mise en page par le widget qui la possède ; centrée, au-dessus de l'arbre, sur un
    // voile ; seule elle reçoit la souris et le clavier (Tab reste dedans). Échap ou un clic en dehors sont transmis à `owner`
    // (onKey(kKeyEscape), onOutsideClick).
    void showModal(Widget &owner, Widget &panel);
    bool modalOpen() const noexcept { return !modals_.empty(); }

    // Notifications temporaires : affichées par les widgets `Notifications` de l'arbre, retirées après leur durée (ou au clic).
    void notify(std::string text, NotificationKind kind = NotificationKind::kInfo, float seconds = 4.0f);
    std::deque<Notification> &notifications() noexcept { return notifications_; }
    void dismissNotification(std::uint64_t id);

    // Presse-papiers (champs de texte : Ctrl+C, X, V). Par défaut, un presse-papiers interne ; l'application branche celui du
    // système (platform : Window::GetClipboard / SetClipboard).
    void setClipboard(std::function<std::string()> get, std::function<void(const std::string &)> set);
    std::string clipboardText() const;
    void setClipboardText(const std::string &text);

    // Délai avant l'affichage d'une infobulle (secondes).
    void setTooltipDelay(float seconds) noexcept { tooltipDelay_ = seconds; }
    // Texte de l'infobulle affichée (vide : aucune).
    const std::string &visibleTooltip() const noexcept { return tooltipText_; }

    // Un widget va être détruit (retiré de l'arbre) : l'interface oublie les pointeurs qui le visent.
    void forget(const Widget &widget) noexcept;

    glm::vec2 viewport() const noexcept { return viewport_; }
    // Maj enfoncée pendant l'image (réglage fin d'un curseur).
    bool shiftHeld() const noexcept { return shift_; }
    glm::vec2 mouse() const noexcept { return mouse_; }
    float time() const noexcept { return time_; }

  private:
    Widget *hitTest(glm::vec2 p) const;
    static Widget *hitTestIn(Widget &w, glm::vec2 p);
    void setHovered(Widget *target);
    void flushDeferred();
    void collectFocusable(Widget &w, std::vector<Widget *> &out) const;
    void layoutPopups();
    struct PopupEntry
    {
        Widget *owner = nullptr;
        std::unique_ptr<Widget> owned; // nulle : contenu emprunté (`view` appartient à `owner`)
        Widget *view = nullptr;
        Rect anchor;
        PopupPlacement placement = PopupPlacement::kBelow;
    };
    struct ModalEntry
    {
        Widget *owner = nullptr;
        Widget *panel = nullptr;
    };
    void pushPopup(Widget &owner, std::unique_ptr<Widget> owned, Widget &view, const Rect &anchor, PopupPlacement placement);
    void closePopupsFrom(std::size_t level);
    void forgetSubtree(const Widget *root);

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
    std::vector<PopupEntry> popups_;
    std::vector<ModalEntry> modals_;
    std::deque<Notification> notifications_;
    std::uint64_t nextNotification_ = 1;
    std::function<std::string()> clipboardGet_;
    std::function<void(const std::string &)> clipboardSet_;
    std::string clipboard_;

    glm::vec2 viewport_{0.0f};
    glm::vec2 mouse_{-1.0e9f};
    bool mouseWasDown_ = false;
    bool secondaryWasDown_ = false;
    bool shift_ = false;
    bool wantsMouse_ = false;
    float time_ = 0.0f;
    float hoverTime_ = 0.0f;
    float tooltipDelay_ = 0.6f;
    std::string tooltipText_;
    std::string tooltipCandidate_;
    glm::vec2 tooltipAt_{0.0f};
};

} // namespace engine::ui

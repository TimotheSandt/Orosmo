// UI-04 et UI-10 : outils de debug du moteur (A79 lot L10), construits sur orosmo::ui (sans Dear ImGui, A79 choix 6 : aucune
// dépendance ni second chemin de dessin ; thèmes, lot 2D et réflexion réutilisés). Quatre onglets dans un panneau à droite :
//
//   - Console     : journal du moteur et commandes enregistrées par nom (`debug::Console`).
//   - Widgets     : UI-10, arbre des widgets de l'interface inspectée (`inspect`), avec le type, l'identifiant, le style et le
//                   rectangle calculé par la mise en page (position, taille) de chacun ; le survol d'une ligne surligne le widget
//                   dans l'interface inspectée (un clic garde le surlignage) ; sans ligne survolée, le widget survolé de
//                   l'interface inspectée est surligné.
//   - Entities    : entités d'un `scene::World` (`inspectWorld`) ; l'entité choisie affiche ses composants, champ par champ, d'après
//                   leur description à l'exécution (`reflectedPanel` sur `core::FieldDesc`), modifiables.
//   - Profiler    : historique des temps d'image (`FrameHistory`, poussé par l'application) et mesures nommées de `core::Profiler`.
//
// Fermés, les outils ne font RIEN : ni mise en page, ni lecture des entrées, ni commande de dessin (UI-13). Ils ont leur propre
// `Ui` (le thème de l'application s'y charge : `ui().themes()`) ; l'apparence vient des styles `debugPanel`, `debugRow`,
// `debugRowSelected`, `debugText`, `debugInput`, `debugWarning`, `debugError`, `debugHighlight` du thème (res/themes/base.json).
//
//   orosmo::ui::debug::DebugTools tools(font);
//   tools.ui().themes().load("res/themes/dark.json");
//   tools.inspect(&appUi);  tools.inspectWorld(&world);
//   // chaque image : tools.handleToggle(input) (F12) ; tools.update(input, viewport) ; si tools.wantsMouse(), ne pas donner la
//   // souris à l'application ; après appUi.draw(list) : tools.draw(list).
#pragma once

#include "orosmo/ui/debug_console.hpp"
#include "orosmo/ui/frame_history.hpp"
#include "orosmo/ui/ui.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orosmo::scene
{
class World;
struct Entity;
} // namespace orosmo::scene

namespace orosmo::ui::debug
{

// UI-10 : une ligne de l'arbre des widgets (copie : aucun pointeur vers l'interface inspectée).
struct WidgetInfo
{
    int depth = 0;          // 0 : racine
    std::string type;       // nom de la classe C++ sans espace de noms (« Button »)
    std::string id;         // vide si aucun
    std::string style;      // nom du style du thème
    Rect rect;              // rectangle calculé par la dernière mise en page
    glm::vec2 measured{0.0f}; // taille souhaitée (passe de mesure)
    bool visible = true;    // le widget et ses parents sont visibles
};

// Parcours en profondeur dans l'ordre des enfants ; au plus `maxRows` lignes.
std::vector<WidgetInfo> describeWidgetTree(const Widget &root, std::size_t maxRows = 1000);
// Nom de la classe d'un widget (« Button », « VBox »).
std::string widgetTypeName(const Widget &widget);
// « Button #play [button] 10,20 120x30 » (indentation de deux espaces par niveau ; « (hidden) » si masqué).
std::string describeWidget(const WidgetInfo &info);

// UI-04 : panneau des composants d'une entité (une section par composant, champs d'après `ComponentDesc::fields`, identifiants
// « <composant>.<champ> »). Les valeurs sont relues dans le monde à chaque image (un composant peut changer d'adresse) ; le monde
// doit vivre plus longtemps que le panneau. Entité morte : un libellé.
std::unique_ptr<Widget> entityPanel(scene::World &world, scene::Entity entity);
// « #3.1  Transform, Velocity »
std::string describeEntity(const scene::World &world, scene::Entity entity);

enum class DebugTab : int
{
    kConsole = 0,
    kWidgets = 1,
    kEntities = 2,
    kProfiler = 3,
};
// « console », « widgets », « entities », « profiler » ; faux si le nom est inconnu.
bool debugTabFromName(std::string_view name, DebugTab &out);

class DebugTools
{
  public:
    static constexpr std::size_t kConsoleRows = 18;     // lignes de la console affichées (les plus récentes)
    static constexpr std::size_t kMaxEntityRows = 200;  // entités listées au plus
    static constexpr float kProfilerRefreshSeconds = 0.25f;
    static constexpr const char *kToggleShortcut = "F12";

    explicit DebugTools(const Font &font);
    ~DebugTools();
    DebugTools(const DebugTools &) = delete;
    DebugTools &operator=(const DebugTools &) = delete;

    Ui &ui() noexcept { return *ui_; }
    Console &console() noexcept { return console_; }
    FrameHistory &frames() noexcept { return frames_; }

    void open();
    void close();
    void toggle() { isOpen() ? close() : open(); }
    bool isOpen() const noexcept { return open_; }
    void selectTab(DebugTab tab);
    DebugTab tab() const noexcept { return static_cast<DebugTab>(tab_); }

    // Interface inspectée (UI-10) ; nulle : aucune. Elle doit vivre tant qu'elle est inspectée.
    void inspect(const Ui *target) noexcept { target_ = target; }
    // Monde inspecté (UI-04) ; nul : aucun. Il doit vivre tant qu'il est inspecté.
    void inspectWorld(scene::World *world);
    scene::World *world() const noexcept { return world_; }
    // Entité choisie dans l'onglet Entities (kNullEntity : aucune).
    void selectEntity(scene::Entity entity);
    scene::Entity selectedEntity() const noexcept;

    // F12 dans les raccourcis de l'image (`InputFrame::shortcuts`) : ouvre ou ferme ; vrai si les outils ont basculé.
    bool handleToggle(const InputFrame &input);
    // Fermés : rien. Ouverts : journal relu, contenu de l'onglet visible rafraîchi, puis `Ui::update`.
    void update(const InputFrame &input, glm::vec2 viewport);
    // Fermés : rien. Ouverts : surlignage du widget inspecté, puis le panneau.
    void draw(renderer::DrawList &list) const;

    bool wantsMouse() const noexcept { return open_ && ui_->wantsMouse(); }
    bool wantsKeyboard() const noexcept { return open_ && ui_->wantsKeyboard(); }

    // UI-10 : arbre affiché (dernière mise à jour de l'onglet Widgets) et rectangle surligné (aucun : nullptr).
    const std::vector<WidgetInfo> &widgetTree() const noexcept { return tree_; }
    const Rect *highlight() const noexcept { return highlight_.has ? &highlight_.rect : nullptr; }

  private:
    struct Highlight
    {
        bool has = false;
        Rect rect;
    };
    void build();
    void refreshConsole();
    void refreshWidgets();
    void refreshEntities();
    void refreshProfiler(float deltaTime);

    std::unique_ptr<Ui> ui_;
    Console console_;
    FrameHistory frames_;
    const Ui *target_ = nullptr;
    scene::World *world_ = nullptr;
    bool open_ = false;
    int tab_ = 0;

    // Console
    std::vector<Widget *> consoleRows_;
    std::uint64_t consoleRevision_ = ~std::uint64_t{0};
    // Widgets (UI-10)
    std::vector<WidgetInfo> tree_;
    std::vector<std::string> treeText_;
    Widget *treeList_ = nullptr;
    int hoveredRow_ = -1;
    int pinnedRow_ = -1;
    Highlight highlight_;
    // Entités
    std::vector<scene::Entity> entities_;
    std::vector<std::string> entityText_;
    std::uint32_t selectedIndex_ = 0xFFFFFFFFu;
    std::uint32_t selectedGeneration_ = 0;
    std::string selectedSignature_;
    Widget *entityList_ = nullptr;
    Widget *entityBody_ = nullptr;
    // Profileur
    std::vector<std::string> profilerText_;
    Widget *profilerList_ = nullptr;
    std::string frameText_;
    float sinceProfilerRefresh_ = kProfilerRefreshSeconds;
};

} // namespace orosmo::ui::debug

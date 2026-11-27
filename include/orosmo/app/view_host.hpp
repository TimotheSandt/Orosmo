// UI-08 (A79 lot L8) : vue `.cui` racine d'une interface, rechargée à chaud en développement.
//
//   cui::HudView view{.state = &state};       // structure générée (HudView.gen.h), état lié tenu par l'hôte
//   app::ViewHost views(interface, {.game = &game, .sources = "src", .buildCommand = "make game", .buildDirectory = root});
//   views.setRoot(view);                       // une fois
//   views.update();                            // à chaque image, hors de Ui::update
//
// Deux façons de construire la vue, choisies à la compilation de l'hôte :
//   - version finale (et tout projet sans bibliothèque de jeu) : le C++ généré est lié dans l'exécutable, `setRoot` appelle
//     `View::build` directement, rien n'est observé ni reconstruit ;
//   - développement (`OROSMO_CUI_RELOADABLE`, défini par le Makefile du projet quand les vues vont dans la bibliothèque de jeu) :
//     le C++ généré est compilé dans la bibliothèque de jeu ; la vue est retrouvée par son point d'entrée
//     (`orosmoCuiView_<Vue>`, orosmo/ui/view_entry.hpp) dans la version chargée de `settings.game`. À l'enregistrement d'un
//     `.cui` de `sources`, `buildCommand` est lancée sur un autre thread (cui puis la bibliothèque) ; le module de jeu recharge
//     la bibliothèque reconstruite (`GameModule::update`), puis `update` recrée l'arbre en gardant focus, défilement et sections
//     ouvertes par identifiant de widget (`ui::replaceRoot`). Le même arbre est recréé après tout rechargement du jeu.
//     Échec (erreur de `cui` ou du compilateur, vue absente, structure de la vue changée) : l'ancienne vue reste, l'erreur est
//     journalisée, gardée (`lastError`) et affichée par `Ui::notify` (un widget `Notifications` de l'arbre la montre).
//     Pendant une construction, l'arbre n'est pas recréé ; si elle échoue, la version du jeu chargée entre-temps est ignorée.
//     Premier montage en échec (aucune vue précédente) : la vue demandée est retenue et montée par une version suivante.
//
// L'état lié (`@Mutable var state`) et la structure de la vue vivent côté hôte, et survivent donc au rechargement. Une structure
// qui change (champ `@Mutable` / `@Snapshot` ajouté, bloc `@cui-cpp` modifié) ne peut pas être lue par l'exécutable compilé avant :
// la vue n'est pas reconstruite et il faut relancer le programme reconstruit (`cui_bindings`, G361).
//
// Durée de vie : déclarer le `ViewHost` APRÈS l'`Ui` et l'état lié (détruit avant eux) ; la destruction retire l'arbre de `Ui`
// avant de libérer la version de la bibliothèque qui l'a construit, et attend la fin d'une construction en cours.
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/platform/process.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>

namespace orosmo::ui
{
class Ui;
class Widget;
} // namespace orosmo::ui

namespace orosmo::app
{

class GameModule;

struct ViewHostSettings
{
    // Bibliothèque de jeu qui contient les vues (développement). Ignorée en version finale.
    GameModule *game = nullptr;
    // Développement : `.cui` observés (dossier, récursif, ou fichier) et commande qui les recompile avec la bibliothèque de jeu,
    // lancée dans `buildDirectory` (vide : dossier courant). Commande vide : rien n'est lancé (la vue suit seulement les
    // rechargements de la bibliothèque, reconstruite par l'utilisateur).
    std::filesystem::path sources{};
    std::string buildCommand{};
    std::filesystem::path buildDirectory{};
    // Délai après la dernière modification d'un `.cui` avant de lancer la commande (un éditeur écrit en plusieurs fois).
    std::chrono::milliseconds settleDelay{200};
    std::chrono::milliseconds pollInterval{100};
    // Erreurs aussi affichées par `Ui::notify` (kError).
    bool notify = true;
    // Lance la construction et rend son résultat à venir. Vide : `platform::runCommand` sur un autre thread. Les tests le
    // remplacent pour piloter la fin de la construction (sans processus ni horloge).
    std::function<std::future<core::Result<platform::CommandResult>>(const std::string &command,
                                                                     const std::filesystem::path &directory)>
        startBuild{};
};

class ViewHost
{
  public:
    explicit ViewHost(ui::Ui &ui, ViewHostSettings settings = {});
    ~ViewHost();
    ViewHost(const ViewHost &) = delete;
    ViewHost &operator=(const ViewHost &) = delete;

    // Construit la vue et l'installe comme racine de l'interface (garde focus, défilement, sections de l'arbre précédent). La
    // structure est copiée et gardée (ses pointeurs vers l'état lié doivent rester valides). Hors de `Ui::update` (kBusy).
    // Développement : kInvalidArgument sans module de jeu, erreurs de `GameModule::findSymbol`, kVersionMismatch si la
    // bibliothèque a été construite avec une autre structure de la vue.
    template <class View> core::Result<> setRoot(View view);

    // Une image (hors de `Ui::update`) : observe les `.cui`, lance et suit la construction, recrée l'arbre après un rechargement
    // de la bibliothèque de jeu. Sans effet en version finale.
    void update();

    // Vues compilées dans la bibliothèque de jeu (développement).
    bool reloadable() const noexcept;
    // Une construction (`buildCommand`) est en cours.
    bool building() const noexcept;
    // Arbres recréés depuis `setRoot` (après un rechargement) ; constructions lancées ; échecs (construction, vue).
    std::uint32_t rebuilds() const noexcept;
    std::uint32_t builds() const noexcept;
    std::uint32_t failures() const noexcept;
    // Dernière erreur (vide s'il n'y en a pas eu) et sortie de la dernière construction.
    const std::optional<core::Error> &lastError() const noexcept;
    const std::string &lastBuildOutput() const noexcept;

    struct Impl;
    class Reloader; // développement : src/app/view_reloader.cpp

  private:
    using LinkedBuild = std::unique_ptr<ui::Widget> (*)(const void *view, ui::Ui &ui);
    core::Result<> mountLinked(std::shared_ptr<const void> view, LinkedBuild build);
    core::Result<> mountFromLibrary(const char *name, std::uint64_t bindings, std::size_t size, std::shared_ptr<const void> view);

    std::unique_ptr<Impl> impl_;
};

template <class View> core::Result<> ViewHost::setRoot(View view)
{
    auto shared = std::make_shared<const View>(std::move(view));
#if defined(OROSMO_CUI_RELOADABLE)
    return mountFromLibrary(View::cui_name, View::cui_bindings, sizeof(View), std::move(shared));
#else
    return mountLinked(std::move(shared), [](const void *v, ui::Ui &ui) { return static_cast<const View *>(v)->build(ui); });
#endif
}

} // namespace orosmo::app

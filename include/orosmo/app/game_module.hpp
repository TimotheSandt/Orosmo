// Module de jeu (GAME-01, GAME-02, GAME-07 ; A79 lot L5) : la logique du jeu, compilée à part, branchée sur le moteur.
//
// Deux façons de l'obtenir, même comportement une fois démarré :
//   - développement : `GameModule::load(settings)` charge la bibliothèque dynamique (copie sous un nom unique,
//     `platform::DynamicLibrary`), puis la RECHARGE quand le fichier change (`core::FileWatcher`, vu par `update`) : la nouvelle
//     copie est chargée et démarrée (`init`) pendant que l'ancienne reste en mémoire ; l'ancienne n'est déchargée qu'après le
//     succès de la nouvelle. Échec (fichier illisible, `orosmoGameApi` absent, ABI différente, `init` qui rend faux) :
//     l'ancienne version reste active, inchangée, et l'erreur est journalisée (`lastError`).
//   - version finale : `GameModule::fromApi(orosmoGameApi(), cibles)` avec la logique liée statiquement (OROSMO_GAME_STATIC) :
//     ni bibliothèque dynamique, ni observateur de fichiers. `load` et le code de rechargement sont dans un fichier objet à part
//     de `libapp.a` (`src/app/game_reloader.cpp`) que l'éditeur de liens ne prend pas s'il n'est pas appelé (vérifié par `nm`
//     sur `bin/tests/test_game_static`, apps/demo/Makefile).
//
// Ordre de vie : les cibles (`GameTargets`) doivent survivre au module ; `stop()` (ou la destruction) appelle `shutdown`,
// retire systèmes et actions, puis décharge. Les composants et leurs données restent dans le monde.
//
// Migration (GAME-03, lot L6) : un composant de l'ancienne version que la nouvelle enregistre avec une autre disposition est
// migré par le moteur pendant l'`init` de la nouvelle (`lastMigrations`) ; si cet `init` échoue, le monde est remis tel qu'il
// était et l'ancienne version reprend. Après chaque passage réussi, les actions demandées par l'interface et absentes sont
// journalisées (`missingActions`, GAME-04). Les paramètres JSON enregistrés par le jeu sont relus à chaque `update` (GAME-05).
#pragma once

#include <orosmo/app/game_api.hpp>
#include <orosmo/app/game_context.hpp>
#include <orosmo/core/error.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orosmo::app
{

struct GameModuleSettings
{
    // Bibliothèque de jeu (`<nom>` + `platform::DynamicLibrary::extension()`), surveillée si `watch`.
    std::filesystem::path path;
    GameTargets targets;
    bool watch = true;
    // Délai après la dernière modification avant de recharger (l'éditeur de liens écrit le fichier en plusieurs fois).
    std::chrono::milliseconds settleDelay{250};
    // Cadence d'interrogation du fichier (`core::FileWatcher::setPollInterval`).
    std::chrono::milliseconds pollInterval{100};
};

class GameModule
{
  public:
    // Développement : charge et démarre la bibliothèque. Erreurs : kInvalidArgument (cibles sans monde), kNotFound (fichier ou
    // symbole `orosmoGameApi` absent), kIo (chargeur), kVersionMismatch (ABI), kCancelled (`init` a rendu faux).
    static core::Result<GameModule> load(GameModuleSettings settings);
    // Version finale (ou tests) : démarre une table liée dans l'exécutable. Mêmes erreurs, sans rechargement.
    static core::Result<GameModule> fromApi(const OrosmoGameApi *api, GameTargets targets);

    GameModule(GameModule &&other) noexcept;
    GameModule &operator=(GameModule &&other) noexcept;
    GameModule(const GameModule &) = delete;
    GameModule &operator=(const GameModule &) = delete;
    ~GameModule();

    // Une image : en développement, regarde si la bibliothèque a changé et la recharge (après `settleDelay`) ; puis appelle
    // `update` du jeu ; avant, relit les fichiers de paramètres modifiés. À appeler hors d'un pas de simulation.
    void update(double dt);

    // Recharge tout de suite le fichier de `settings.path` (développement). kUnsupported en version statique ; sinon erreurs de
    // `load`, l'ancienne version restant active.
    core::Result<> reload();

    // Version statique (tests, outils) : remplace la version active par une autre table liée dans l'exécutable, par la même
    // séquence que le rechargement (migration, retour en arrière). kUnsupported avec une bibliothèque dynamique (utiliser
    // `reload`) ; erreurs de `fromApi`, l'ancienne version restant active.
    core::Result<> replace(const OrosmoGameApi *api);

    // `shutdown`, retrait des systèmes et actions, déchargement. Sans effet si déjà arrêté.
    void stop();

    bool running() const noexcept;
    bool hotReloadable() const noexcept;
    // Nombre de versions démarrées (1 après le premier chargement) ; échecs de rechargement depuis le début.
    std::uint32_t version() const noexcept;
    std::uint32_t failedReloads() const noexcept;
    // Dernière erreur de rechargement (vide s'il n'y en a pas eu).
    const std::optional<core::Error> &lastError() const noexcept;
    std::string name() const;
    // Fichier chargé (copie sous un nom unique) ; vide en version statique.
    std::filesystem::path loadedPath() const;

    // Ce que la version active a enregistré (noms, ordre d'enregistrement).
    std::vector<std::string> components() const;
    std::vector<std::string> systems() const;
    std::vector<std::string> actions() const;
    // Types de composants enregistrés par une version précédente et plus par l'active : gardés dans le monde avec leurs données
    // (la décision de les retirer revient à la migration, L6).
    const std::vector<std::string> &orphanComponents() const noexcept;
    // Décision L6 (G313) : les orphelins sont gardés par défaut (un type retiré le temps d'une édition retrouve ses données) ;
    // cet appel les retire du monde, données comprises (`World::unregisterComponent`). Rend le nombre de types retirés.
    std::size_t removeOrphanComponents();
    // Composants migrés par le dernier passage réussi (vide s'il n'y en a pas eu).
    const std::vector<scene::ComponentMigration> &lastMigrations() const noexcept;
    // Actions demandées par l'interface et absentes après le dernier passage réussi (aussi journalisées).
    const std::vector<std::string> &missingActions() const noexcept;
    // Noms des fichiers de paramètres de la version active.
    std::vector<std::string> params() const;

    struct Impl;
    // Rechargement (développement seulement) : implémenté dans src/app/game_reloader.cpp.
    class Reloader;

  private:
    explicit GameModule(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace orosmo::app

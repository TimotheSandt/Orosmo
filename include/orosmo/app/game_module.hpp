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
// Pour plus tard : la migration d'un composant dont la disposition change (L6) se branchera dans le passage d'une version à
// l'autre (`GameModule::Impl::switchTo`, src/app/game_module.cpp) ; aujourd'hui l'`init` de la nouvelle version échoue
// (kAlreadyExists) et l'ancienne reste active. Le journal de `ActionRegistry::missing()` après rechargement est aussi L6.
#pragma once

#include <orosmo/app/game_api.hpp>
#include <orosmo/app/game_context.hpp>
#include <orosmo/core/error.hpp>

#include <chrono>
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
    // `update` du jeu. À appeler hors d'un pas de simulation.
    void update(double dt);

    // Recharge tout de suite le fichier de `settings.path` (développement). kUnsupported en version statique ; sinon erreurs de
    // `load`, l'ancienne version restant active.
    core::Result<> reload();

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

    struct Impl;
    // Rechargement (développement seulement) : implémenté dans src/app/game_reloader.cpp.
    class Reloader;

  private:
    explicit GameModule(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace orosmo::app

// Contexte passé aux points d'entrée de la bibliothèque de jeu (GAME-01, GAME-02 ; A79 lot L5).
//
// Le moteur en est propriétaire (`GameModule`) ; la bibliothèque ne le garde pas d'un appel à l'autre. Tout ce que le jeu
// enregistre passe par lui, PAR NOM, et le module le note pour la version qui l'a enregistré :
//   - composants : `registerComponent` (description copiée dans le monde ; un nouvel enregistrement identique après
//     rechargement garde les données, `scene::World::registerComponent`) ;
//   - systèmes : `addSystem` (dans la `sim::Simulation` cible) ;
//   - actions de l'interface : `addAction` (dans le `ui::ActionRegistry` cible, appelées par nom : `.onClick(nom)` du langage) ;
//   - paramètres de jeu : `registerParams` (fichier JSON tenu et relu à chaud par le moteur, GAME-05, lot L6).
// Au rechargement, les systèmes et actions de l'ancienne version sont retirés avant l'`init` de la nouvelle (et remis si elle
// échoue) ; les composants et leurs données restent dans le monde (état côté moteur, GAME-02). Un composant qu'une version
// précédente avait enregistré et dont la disposition change est MIGRÉ par le moteur (GAME-03, lot L6 :
// `scene::World::migrateComponent`) ; si l'`init` de la nouvelle version échoue ensuite, le monde est remis tel qu'il était.
#pragma once

#include <orosmo/app/params_host.hpp>
#include <orosmo/core/error.hpp>
#include <orosmo/scene/component.hpp>
#include <orosmo/scene/world.hpp>
#include <orosmo/sim/simulation.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orosmo::ui
{
struct Event;
class ActionRegistry;
} // namespace orosmo::ui

namespace orosmo::app
{
namespace detail
{
struct GameContextAccess; // src/app/game_module_impl.hpp
struct ReloadJournal;     // src/app/game_module_impl.hpp
}

// Même type que `ui::Handler` (sans inclure toute l'interface).
using ActionHandler = std::function<void(const ui::Event &)>;

// Ce que le jeu peut remplir. `world` est obligatoire (à défaut : celui de `simulation`) ; `simulation` et `actions` sont
// facultatifs (un enregistrement sans cible est refusé, kUnsupported). Les cibles doivent survivre au `GameModule`.
struct GameTargets
{
    scene::World *world = nullptr;
    sim::Simulation *simulation = nullptr;
    ui::ActionRegistry *actions = nullptr;
    // Dossier des chemins relatifs de `registerParams` (vide : dossier courant) et réglages de leur observation.
    std::filesystem::path paramsDirectory{};
    ParamsSettings params{};
};

// Noms enregistrés par une version de la bibliothèque (les fonctions restent côté moteur, copiées, jusqu'au déchargement de la
// version qui les a fournies).
struct GameRegistrations
{
    std::vector<std::string> components;
    std::vector<sim::SystemDesc> systems;
    std::vector<std::pair<std::string, ActionHandler>> actions;
    // Fichiers de paramètres (tenus par le moteur ; partagés avec la version suivante s'ils restent identiques).
    std::vector<std::pair<std::string, std::shared_ptr<ParamsFile>>> params;
};

class GameContext
{
  public:
    GameContext(const GameContext &) = delete;
    GameContext &operator=(const GameContext &) = delete;

    scene::World &world() const noexcept { return *targets_.world; }
    sim::Simulation *simulation() const noexcept { return targets_.simulation; }
    ui::ActionRegistry *actions() const noexcept { return targets_.actions; }

    // Numéro de la version en cours de chargement ou active : 1 au premier chargement, +1 à chaque rechargement réussi.
    std::uint32_t version() const noexcept { return version_; }
    // Vrai pendant l'`init` et l'`onReload` d'une version qui en remplace une autre.
    bool reloading() const noexcept { return reloading_; }

    // Enregistre (ou retrouve, si la disposition est la même) un type de composant. Au rechargement, un type enregistré par une
    // version précédente dont la disposition change est migré (`scene::World::migrateComponent` : champs communs copiés,
    // nouveaux par défaut ; repli sauvegarde/restauration si un champ est retiré ou change de type ; compte rendu dans
    // `GameModule::lastMigrations`) ; ses anciens identifiants sont alors refusés. Erreurs de `World::registerComponent`
    // (kAlreadyExists : même nom, autre disposition, type de l'hôte ou d'une autre version) ou de `migrateComponent`.
    core::Result<scene::ComponentId> registerComponent(scene::ComponentDesc desc);

    template <core::Reflected T>
        requires scene::ComponentData<T>
    core::Result<scene::ComponentType<T>> registerComponent(std::string name)
    {
        auto id = registerComponent(scene::describeComponent<T>(std::move(name)));
        if (!id)
            return std::unexpected(std::move(id.error()));
        return scene::ComponentType<T>{*id};
    }

    // Ajoute un système à la simulation cible. kUnsupported sans simulation ; erreurs de `Simulation::addSystem`.
    core::Result<> addSystem(sim::SystemDesc system);

    // Enregistre une action de l'interface. kUnsupported sans registre ; kAlreadyExists si le nom est déjà pris (par l'hôte ou
    // par cette version) ; kInvalidArgument : nom vide ou fonction vide.
    core::Result<> addAction(std::string name, ActionHandler handler);

    // Paramètres de jeu (GAME-05) : fichier JSON `path` (relatif à `GameTargets::paramsDirectory`) lu dans une structure décrite,
    // relu par le moteur à chaque `GameModule::update` quand il change (dernière valeur valide gardée). La valeur se lit par la
    // poignée (`Params<T>::get`, à capturer par valeur). Même nom, même fichier et même disposition qu'à la version précédente :
    // même fichier (pas de relecture). kAlreadyExists : nom déjà pris par cette version ; kInvalidArgument : description.
    core::Result<const ParamsFile *> registerParams(std::string name, scene::ComponentDesc desc, std::filesystem::path path);

    template <core::Reflected T>
        requires scene::ComponentData<T>
    core::Result<Params<T>> registerParams(std::string name, std::filesystem::path path)
    {
        auto file = registerParams(name, scene::describeComponent<T>(name), std::move(path));
        if (!file)
            return std::unexpected(std::move(file.error()));
        return Params<T>(*file);
    }

  private:
    friend struct detail::GameContextAccess;

    explicit GameContext(GameTargets targets) noexcept : targets_(targets) {}

    // Enregistrements de la version en cours de chargement, sinon de la version active ; nul après l'arrêt (kUnsupported).
    core::Result<GameRegistrations *> recording(std::string_view what);

    GameTargets targets_;
    GameRegistrations *recording_ = nullptr;
    detail::ReloadJournal *journal_ = nullptr; // rechargement en cours (migrations) ; nul hors d'un `init`
    std::uint32_t version_ = 0;
    bool reloading_ = false;
};

} // namespace orosmo::app

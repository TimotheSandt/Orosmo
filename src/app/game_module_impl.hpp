// Détails internes de `app::GameModule` partagés entre game_module.cpp (commun, version statique) et game_reloader.cpp
// (développement : bibliothèque dynamique et observateur de fichiers). game_module.cpp n'appelle le rechargement que par les
// fonctions virtuelles de `Reloader` : sans `GameModule::load`, game_reloader.o n'est pas lié (GAME-07).
#pragma once

#include <orosmo/app/game_module.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orosmo::app
{

// État d'un passage d'une version à l'autre, vu par `GameContext` pendant l'`init` de la nouvelle (GAME-03).
struct detail::ReloadJournal
{
    // Enregistrements de la version remplacée (nul au premier démarrage) et types orphelins : les types « du jeu », les seuls
    // qu'un changement de disposition migre (un type de l'hôte reste refusé, kAlreadyExists).
    const GameRegistrations *previous = nullptr;
    const std::vector<std::string> *orphans = nullptr;
    std::vector<scene::ComponentMigration> migrations;

    bool ownsComponent(std::string_view name) const;
};

// Accès du module aux membres privés du contexte (le jeu ne voit que l'interface publique).
struct detail::GameContextAccess
{
    static GameContext create(GameTargets targets) noexcept { return GameContext(targets); }
    static const GameTargets &targets(const GameContext &context) noexcept { return context.targets_; }
    static GameRegistrations *&recording(GameContext &context) noexcept { return context.recording_; }
    static std::uint32_t &version(GameContext &context) noexcept { return context.version_; }
    static bool &reloading(GameContext &context) noexcept { return context.reloading_; }
    static detail::ReloadJournal *&journal(GameContext &context) noexcept { return context.journal_; }
};

class GameModule::Reloader
{
  public:
    virtual ~Reloader() = default;
    // Appelé à chaque `GameModule::update`, avant l'`update` du jeu.
    virtual void poll(Impl &impl) = 0;
    virtual core::Result<> reloadNow(Impl &impl) = 0;
    virtual std::filesystem::path loadedPath() const = 0;
    // Décharge la bibliothèque active (après `shutdown` et le retrait de ses enregistrements).
    virtual void unloadActive() noexcept = 0;
};

struct GameModule::Impl
{
    explicit Impl(GameTargets targets) noexcept : context(detail::GameContextAccess::create(targets)) {}
    ~Impl();

    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;

    // Démarre `api` : premier démarrage, ou remplacement de la version active (qui n'est pas déchargée ici : l'appelant le fait
    // après le retour, une fois ses fonctions détruites). En cas d'échec, la version active reste en place, avec ses systèmes et
    // actions remis, et les enregistrements de la nouvelle sont détruits avant le retour.
    core::Result<> switchTo(const OrosmoGameApi *api);
    // `shutdown`, retrait des enregistrements, déchargement.
    void stop() noexcept;
    void recordFailure(const core::Error &error);

    GameContext context;
    const OrosmoGameApi *api = nullptr;
    GameRegistrations active;
    std::unique_ptr<Reloader> reloader;
    std::uint32_t version = 0;
    std::uint32_t failedReloads = 0;
    std::optional<core::Error> lastError;
    std::vector<std::string> orphans;
    // Dernier passage réussi : composants migrés (GAME-03) et actions demandées par l'interface mais absentes (GAME-04).
    std::vector<scene::ComponentMigration> lastMigrations;
    std::vector<std::string> missingActions;
};

// Contrôle de la table d'un jeu avant tout appel (nulle, ABI, `init` absent).
core::Result<> validateGameApi(const OrosmoGameApi *api);

} // namespace orosmo::app

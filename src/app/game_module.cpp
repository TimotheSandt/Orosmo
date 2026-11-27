// app::GameContext et la partie commune de app::GameModule (GAME-01, GAME-02, GAME-07 ; A79 lot L5). Rien ici ne charge de
// bibliothèque ni n'observe de fichier : c'est tout ce qu'une version finale statique lie (voir game_reloader.cpp).
#include "game_module_impl.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/ui/ui.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace orosmo::app
{
using core::ErrorCode;

// ── GameContext ──────────────────────────────────────────────────────────────

core::Result<GameRegistrations *> GameContext::recording(std::string_view what)
{
    if (recording_ == nullptr)
        return core::makeError(ErrorCode::kUnsupported, std::string(what) + ": game module stopped");
    return recording_;
}

core::Result<scene::ComponentId> GameContext::registerComponent(scene::ComponentDesc desc)
{
    auto record = recording("registerComponent");
    if (!record)
        return std::unexpected(std::move(record.error()));
    std::string name = desc.name;
    auto id = targets_.world->registerComponent(std::move(desc));
    if (!id)
        return id;
    auto &names = (*record)->components;
    if (std::ranges::find(names, name) == names.end())
        names.push_back(std::move(name));
    return id;
}

core::Result<> GameContext::addSystem(sim::SystemDesc system)
{
    auto record = recording("addSystem");
    if (!record)
        return std::unexpected(std::move(record.error()));
    if (targets_.simulation == nullptr)
        return core::makeError(ErrorCode::kUnsupported, "addSystem(" + system.name + "): no simulation in the game targets");
    if (auto added = targets_.simulation->addSystem(system); !added)
        return added;
    (*record)->systems.push_back(std::move(system));
    return {};
}

core::Result<> GameContext::addAction(std::string name, ActionHandler handler)
{
    auto record = recording("addAction");
    if (!record)
        return std::unexpected(std::move(record.error()));
    if (targets_.actions == nullptr)
        return core::makeError(ErrorCode::kUnsupported, "addAction(" + name + "): no action registry in the game targets");
    if (name.empty() || !handler)
        return core::makeError(ErrorCode::kInvalidArgument, "addAction: empty name or handler");
    if (targets_.actions->contains(name))
        return core::makeError(ErrorCode::kAlreadyExists, "addAction(" + name + "): name already taken");
    targets_.actions->add(name, handler);
    (*record)->actions.emplace_back(std::move(name), std::move(handler));
    return {};
}

// ── Enregistrements d'une version ────────────────────────────────────────────

using Access = detail::GameContextAccess;

namespace
{
// Retire de la simulation et du registre les systèmes et actions d'une version (les copies restent dans `regs`).
void detach(const GameTargets &targets, const GameRegistrations &regs)
{
    if (targets.simulation != nullptr)
        for (const sim::SystemDesc &system : regs.systems)
            targets.simulation->removeSystem(system.name);
    if (targets.actions != nullptr)
        for (const auto &action : regs.actions)
            targets.actions->remove(action.first);
}

// Remet les systèmes et actions d'une version (rechargement refusé : l'ancienne version reprend).
void attach(const GameTargets &targets, const GameRegistrations &regs)
{
    if (targets.simulation != nullptr)
        for (const sim::SystemDesc &system : regs.systems)
            if (auto added = targets.simulation->addSystem(system); !added)
                LOG_ERROR(static_cast<int>(added.error().code), "game module: cannot restore system '" + system.name + "': " + added.error().describe());
    if (targets.actions != nullptr)
        for (const auto &action : regs.actions)
            targets.actions->add(action.first, action.second);
}

std::string apiName(const OrosmoGameApi *api) { return api != nullptr && api->name != nullptr ? api->name : "game"; }
} // namespace

core::Result<> validateGameApi(const OrosmoGameApi *api)
{
    if (api == nullptr)
        return core::makeError(ErrorCode::kInvalidArgument, "game api: orosmoGameApi() returned null");
    if (api->abiVersion != kGameAbiVersion)
        return core::makeError(ErrorCode::kVersionMismatch, "game api: abiVersion " + std::to_string(api->abiVersion) +
                                                                ", engine expects " + std::to_string(kGameAbiVersion));
    if (api->init == nullptr)
        return core::makeError(ErrorCode::kInvalidArgument, "game api: init is null");
    return {};
}

core::Result<> GameModule::Impl::switchTo(const OrosmoGameApi *next)
{
    if (auto valid = validateGameApi(next); !valid)
        return valid;

    const bool replacing = api != nullptr;
    // L'ancienne version cède ses noms ; ses fonctions restent dans `previous` (bibliothèque encore chargée).
    GameRegistrations previous = std::move(active);
    active = {};
    detach(Access::targets(context), previous);

    const std::uint32_t previousVersion = Access::version(context);
    Access::recording(context) = &active;
    Access::version(context) = version + 1;
    Access::reloading(context) = replacing;

    // `init` peut écrire dans le monde (et tirer des nombres) avant d'échouer : copie de l'état du moteur, remise en cas d'échec,
    // pour que la version active reprenne inchangée (copie profonde du monde à chaque chargement : G319).
    const GameTargets &targets = Access::targets(context);
    scene::World savedWorld = *targets.world;
    std::optional<core::Random> savedRandom;
    if (targets.simulation != nullptr)
        savedRandom = targets.simulation->random();

    // L6 : la migration des composants dont la disposition change se branchera ici (avant l'`init` de la nouvelle version).
    if (!next->init(&context))
    {
        *targets.world = std::move(savedWorld);
        if (savedRandom)
            targets.simulation->random() = *savedRandom;
        // Retour à l'ancienne version : ses noms reprennent leur place ; les fonctions de la nouvelle sont détruites ici, avant que
        // l'appelant ne décharge sa bibliothèque.
        detach(Access::targets(context), active);
        active = std::move(previous);
        attach(Access::targets(context), active);
        Access::recording(context) = replacing ? &active : nullptr;
        Access::version(context) = previousVersion;
        Access::reloading(context) = false;
        return core::makeError(ErrorCode::kCancelled, apiName(next) + ": init failed" +
                                                          (replacing ? ", previous version kept" : ""));
    }

    if (replacing && next->onReload != nullptr)
        next->onReload(&context);
    Access::reloading(context) = false;

    if (replacing)
        for (std::string &name : previous.components)
            if (std::ranges::find(active.components, name) == active.components.end() &&
                std::ranges::find(orphans, name) == orphans.end())
            {
                LOG_WARNING("game module: component type '" + name + "' is no longer registered by " + apiName(next) +
                            "; kept in the world with its data");
                orphans.push_back(std::move(name));
            }
    // Un type de nouveau enregistré n'est plus orphelin.
    std::erase_if(orphans, [&](const std::string &name) { return std::ranges::find(active.components, name) !=
                                                                 active.components.end(); });

    api = next;
    ++version;
    LOG_INFO(apiName(next) + (replacing ? ": reloaded, version " : ": started, version ") + std::to_string(version));
    return {};
    // `previous` (fonctions de l'ancienne version) est détruit ici, avant le déchargement de son code par l'appelant.
}

void GameModule::Impl::stop() noexcept
{
    if (api == nullptr)
        return;
    if (api->shutdown != nullptr)
        api->shutdown(&context);
    detach(Access::targets(context), active);
    active = {};
    Access::recording(context) = nullptr;
    LOG_INFO(apiName(api) + ": stopped");
    api = nullptr;
    if (reloader)
        reloader->unloadActive();
}

void GameModule::Impl::recordFailure(const core::Error &error)
{
    ++failedReloads;
    lastError = error;
    LOG_ERROR(static_cast<int>(error.code), "game module: reload failed, previous version kept: " + error.describe());
}

GameModule::Impl::~Impl()
{
    stop();
    reloader.reset();
}

// ── GameModule ───────────────────────────────────────────────────────────────

GameModule::GameModule(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
GameModule::GameModule(GameModule &&other) noexcept = default;
GameModule &GameModule::operator=(GameModule &&other) noexcept = default;
GameModule::~GameModule() = default;

core::Result<GameModule> GameModule::fromApi(const OrosmoGameApi *api, GameTargets targets)
{
    if (targets.world == nullptr && targets.simulation != nullptr)
        targets.world = &targets.simulation->world();
    if (targets.world == nullptr)
        return core::makeError(ErrorCode::kInvalidArgument, "game module: no world in the targets");
    auto impl = std::make_unique<Impl>(targets);
    if (auto started = impl->switchTo(api); !started)
        return std::unexpected(std::move(started.error()));
    return GameModule(std::move(impl));
}

void GameModule::update(double dt)
{
    if (!impl_)
        return;
    if (impl_->reloader && impl_->api != nullptr)
        impl_->reloader->poll(*impl_);
    if (impl_->api != nullptr && impl_->api->update != nullptr)
        impl_->api->update(&impl_->context, dt);
}

core::Result<> GameModule::reload()
{
    if (!impl_ || impl_->api == nullptr)
        return core::makeError(ErrorCode::kInvalidArgument, "game module: not running");
    if (!impl_->reloader)
        return core::makeError(ErrorCode::kUnsupported, "game module: static build, no hot reload");
    return impl_->reloader->reloadNow(*impl_);
}

void GameModule::stop()
{
    if (impl_)
        impl_->stop();
}

bool GameModule::running() const noexcept { return impl_ && impl_->api != nullptr; }
bool GameModule::hotReloadable() const noexcept { return impl_ && impl_->reloader != nullptr; }
std::uint32_t GameModule::version() const noexcept { return impl_ ? impl_->version : 0; }
std::uint32_t GameModule::failedReloads() const noexcept { return impl_ ? impl_->failedReloads : 0; }

const std::optional<core::Error> &GameModule::lastError() const noexcept
{
    static const std::optional<core::Error> kNone;
    return impl_ ? impl_->lastError : kNone;
}

std::string GameModule::name() const { return impl_ ? apiName(impl_->api) : std::string(); }

std::filesystem::path GameModule::loadedPath() const
{
    return impl_ && impl_->reloader ? impl_->reloader->loadedPath() : std::filesystem::path();
}

std::vector<std::string> GameModule::components() const
{
    return impl_ ? impl_->active.components : std::vector<std::string>{};
}

std::vector<std::string> GameModule::systems() const
{
    std::vector<std::string> names;
    if (impl_)
        for (const sim::SystemDesc &system : impl_->active.systems)
            names.push_back(system.name);
    return names;
}

std::vector<std::string> GameModule::actions() const
{
    std::vector<std::string> names;
    if (impl_)
        for (const auto &action : impl_->active.actions)
            names.push_back(action.first);
    return names;
}

const std::vector<std::string> &GameModule::orphanComponents() const noexcept
{
    static const std::vector<std::string> kNone;
    return impl_ ? impl_->orphans : kNone;
}

} // namespace orosmo::app
